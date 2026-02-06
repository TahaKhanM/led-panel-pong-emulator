/*
  panel.h

  What this file does
  -------------------
  This header defines the hardware abstraction layer (HAL) boundary between:
    (1) the game logic (e.g., Pong state update and drawing code), and
    (2) the platform-specific I/O needed to talk to the 32x32 LED panel and joysticks.

  The key idea is that the game code calls *only* the functions declared here.
  At build time you choose exactly one implementation file that provides these
  functions:

    - panel_hw.c   : STM32/libopencm3 implementation (real GPIO + ADC)
    - panel_emu.c  : Web/WASM implementation (browser canvas + JS-controlled inputs)

  This interface intentionally excludes any higher-level rendering helper such as
  updateDisplay(). Instead, it provides the low-level operations that the existing
  coursework code already uses (PrepareLatch, PushBit, SelectRow, LatchRegister, etc.).

  Important behavioural notes
  ---------------------------
  - The physical panel is multiplexed: at any instant a single row address selects a
    *pair* of rows (one in the top half, one in the bottom half). The game code loads
    192 bits (2 halves * 3 colour planes * 32 pixels) and then latches them.
  - delay_ms() must be non-blocking in the browser build (implemented in panel_emu.c)
    so the UI thread remains responsive.
*/

#ifndef PANEL_API_H
#define PANEL_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
  setupPanel

  Initialise all resources required to drive the LED panel.

  On real hardware this configures GPIO ports/pins and sets safe default levels.
  In the emulator this initialises the emulated shift-register state and clears the
  emulated framebuffer.
*/
void setupPanel(void);

/*
  setupInput

  Initialise all resources required to read joystick inputs.

  On real hardware this configures the ADC and any required GPIO/analog setup.
  In the emulator this typically performs no work because joystick values are supplied
  by JavaScript.
*/
void setupInput(void);

/*
  getRawInput

  Read a raw analogue input value for a given ADC channel identifier.

  On hardware this returns the ADC conversion value (often 12-bit, 0..4095).
  In the emulator this returns a synthetic value chosen to mimic the range and direction
  expected by the game code.
*/
uint32_t getRawInput(int channelValue);

/*
  delay_ms

  Pause execution for approximately `ms` milliseconds.

  On hardware this is typically a busy-wait (coarse timing).
  In the emulator this must yield control back to the browser event loop so that rendering
  and input continue to work. The panel_emu.c implementation uses Emscripten-friendly
  yielding.
*/
void delay_ms(uint32_t ms);

/*
  PrepareLatch

  Prepare the latch line before shifting a new row's worth of data.

  On typical LED panel interfaces, data bits are shifted in while the latch is low.
  After the full row payload is shifted, the latch is toggled high to make the new data
  visible on the selected row-pair.
*/
void PrepareLatch(void);

/*
  LatchRegister

  Toggle/activate the latch so that the data currently in the shift register becomes visible.

  This function is the point at which newly shifted pixel bits are committed to the display.
  In the emulator implementation, this is also where we convert the most recent shifted bits
  into the emulated framebuffer for the currently selected row-pair.
*/
void LatchRegister(void);

/*
  SelectRow

  Select which multiplexed row address is active.

  For a 32x32 panel wired as two 16-row halves, the row address typically selects a row-pair:
    - top row:    rowAddress
    - bottom row: rowAddress + 16

  The exact numbering convention depends on the coursework implementation; the emulator mirrors
  the convention used by the game code.
*/
void SelectRow(int row);

/*
  PushBit

  Shift a single data bit into the panel's shift-register chain.

  The game code calls PushBit many times (192 bits per row-pair) to load the colour planes.
  On hardware this toggles GPIO pins for the data line and clock.
  In the emulator this appends the bit into an emulated shift-register buffer.
*/
void PushBit(int onoff);

/*
  ClearRow

  Clear a specific row address by shifting zeros for a full row payload.

  The coursework hardware implementation selects a row and then pushes 192 zero bits. The
  emulator mirrors that behaviour so that game logic that assumes the clear occurs remains
  consistent.
*/
void ClearRow(int row);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PANEL_API_H
