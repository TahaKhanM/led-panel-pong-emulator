/*
  panel_emu.c

  What this file does
  -------------------
  This file is the Web/WASM implementation of the LED panel + joystick HAL declared in panel.h.

  It is designed to behave like the coursework STM32 driver:

  - The game code pushes individual bits via PushBit() into a shift-register chain.
  - The game selects a multiplexed row address with SelectRow().
  - The game calls PrepareLatch() and LatchRegister() to control when shifted bits are committed.

  To emulate this faithfully (rather than shortcutting via a framebuffer API), this file:

  1) Stores the most recent 192 shifted bits (2 halves * 3 colour planes * 32 pixels) in an
     emulated shift-register buffer.
  2) On LatchRegister(), decodes those bits into a latched 32x32 RGB framebuffer (1-bit per channel).
  3) Calls a JavaScript renderer (window.Emu.renderFrame) with a pointer to the framebuffer and the
     currently selected row-pair so the browser can draw either:
       - an integrated view (what a person perceives), or
       - a row-scanning debug view (only the currently active row-pair).

  Joystick inputs are also emulated here:
  - getRawInput(channel) calls into JavaScript (window.Emu.getAdc) to obtain a value that mimics
    the ADC reading used on the STM32 build.

  Timing is handled by delay_ms():
  - On WASM it yields using emscripten_sleep(), optionally respecting Pause/Step controls exposed
    by JavaScript.

  Important: This file must be compiled with Emscripten (emcc) for the web build.
*/

#include "panel.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
  #include <emscripten/threading.h>
#endif

// -----------------------------------------------------------------------------
// Panel geometry and protocol constants (fixed by the coursework hardware)
// -----------------------------------------------------------------------------

// The coursework panel is a 32x32 RGB matrix.
#define PANEL_PIXEL_WIDTH   32
#define PANEL_PIXEL_HEIGHT  32

// The panel is multiplexed as two 16-row halves; the row address selects a row-pair.
#define PANEL_ROW_PAIRS     16

// 192 bits per row-pair: 2 halves * (R,G,B planes) * 32 pixels.
#define PANEL_SHIFT_BITS    192

// -----------------------------------------------------------------------------
// JavaScript interop (only active under Emscripten)
// -----------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__

/*
  js_render_frame

  Bridge from C/WASM to JavaScript to request a redraw.

  Parameters:
    - framebuffer_ptr: pointer (in WASM linear memory) to PANEL_PIXEL_WIDTH*PANEL_PIXEL_HEIGHT*3 bytes.
      Each pixel is stored as three bytes [R,G,B], where each channel is either 0 or 1.
    - active_row_pair: 0..15 indicating the currently selected multiplexed row address.
    - display_on:      0/1 indicating whether the display should be treated as enabled.

  JavaScript side:
    - emulator.js implements window.Emu.renderFrame(...) which reads the framebuffer from the
      WASM heap and draws it to a 32x32 canvas.
*/
EM_JS(void, js_render_frame, (const uint8_t* framebuffer_ptr, int active_row_pair, int display_on), {
  if (window.Emu && typeof window.Emu.renderFrame === "function") {
    window.Emu.renderFrame(framebuffer_ptr, active_row_pair, display_on);
  }
});

/*
  js_get_adc

  Bridge from C/WASM to JavaScript to read an emulated ADC channel.

  The coursework joystick wiring exposes different ADC channels for "up" and "down" directions.
  The browser emulator maps sliders/keyboard input into values that mimic the expected raw readings.
*/
EM_JS(int, js_get_adc, (int channel), {
  if (window.Emu && typeof window.Emu.getAdc === "function") {
    return window.Emu.getAdc(channel) | 0;
  }
  return 0;
});

/*
  js_is_paused

  Query JavaScript for whether the emulator is currently paused.

  This allows delay_ms() to yield without running the game loop forward, and enables a step-by-step
  mode in the browser UI.
*/
EM_JS(int, js_is_paused, (), {
  if (window.Emu && typeof window.Emu.isPaused === "function") {
    return window.Emu.isPaused() ? 1 : 0;
  }
  return 0;
});

/*
  js_consume_step

  Query JavaScript for whether a "single step" token is available.

  When paused, delay_ms() will return once if a step token is consumed, letting the game advance
  in controlled increments.
*/
EM_JS(int, js_consume_step, (), {
  if (window.Emu && typeof window.Emu.consumeStep === "function") {
    return window.Emu.consumeStep() ? 1 : 0;
  }
  return 0;
});

/*
  js_set_display_state

  Notify JavaScript of whether the display should be treated as enabled.

  This is used purely for UI/state display (e.g., showing "Display: on/off"), and does not affect
  the underlying framebuffer state.
*/
EM_JS(void, js_set_display_state, (int on), {
  if (window.EmuUI && typeof window.EmuUI.setDisplayEnabled === "function") {
    window.EmuUI.setDisplayEnabled(!!on);
  }
});

#else
// Non-Emscripten stubs so the file can be compiled outside the browser if needed.
static void js_render_frame(const uint8_t* framebuffer_ptr, int active_row_pair, int display_on) {
  (void)framebuffer_ptr; (void)active_row_pair; (void)display_on;
}
static int js_get_adc(int channel) { (void)channel; return 0; }
static int js_is_paused(void) { return 0; }
static int js_consume_step(void) { return 0; }
static void js_set_display_state(int on) { (void)on; }
#endif

// -----------------------------------------------------------------------------
// Emulated panel internal state
// -----------------------------------------------------------------------------

// Latched 32x32 framebuffer stored as [R,G,B] bytes per pixel (each channel is 0 or 1).
static uint8_t latchedFramebufferRgb[PANEL_PIXEL_WIDTH * PANEL_PIXEL_HEIGHT * 3];

// Most recent shifted bits, stored as a circular buffer to emulate a shift-register chain.
static uint8_t shiftRegisterBits[PANEL_SHIFT_BITS];
static int shiftRegisterOldestIndex = 0;  // index of the oldest (first) bit in the circular buffer
static int shiftRegisterBitCount = 0;     // number of valid bits currently stored (<= PANEL_SHIFT_BITS)

// Current multiplexed row address (0..15). This selects a row-pair: top=r, bottom=r+16.
static int selectedRowPairIndex = 0;

// These state flags mirror the mental model used during development/debugging.
static bool latchLineIsLow = false;
static bool displayIsEnabled = true;

/*
  shiftRegisterPushBit

  Append one bit into the emulated shift-register buffer.

  On the real panel, each clock pulse pushes the input bit one position along a chain of registers.
  In this emulator we model that by storing the most recent PANEL_SHIFT_BITS bits. When more than
  PANEL_SHIFT_BITS bits are pushed, the oldest bits are overwritten (exactly how a fixed-length
  shift register behaves).
*/
static inline void shiftRegisterPushBit(uint8_t bitValue) {
  bitValue = (bitValue ? 1u : 0u);

  if (shiftRegisterBitCount < PANEL_SHIFT_BITS) {
    int writeIndex = (shiftRegisterOldestIndex + shiftRegisterBitCount) % PANEL_SHIFT_BITS;
    shiftRegisterBits[writeIndex] = bitValue;
    shiftRegisterBitCount++;
  } else {
    // Overwrite the oldest bit and advance the "oldest" pointer.
    shiftRegisterOldestIndex = (shiftRegisterOldestIndex + 1) % PANEL_SHIFT_BITS;
    int writeIndex = (shiftRegisterOldestIndex + PANEL_SHIFT_BITS - 1) % PANEL_SHIFT_BITS;
    shiftRegisterBits[writeIndex] = bitValue;
  }
}

/*
  shiftRegisterGetBit

  Read a bit from the emulated shift-register buffer using a logical index.

  Parameters:
    - logicalIndex: 0..PANEL_SHIFT_BITS-1, where 0 refers to the oldest stored bit and
      PANEL_SHIFT_BITS-1 refers to the most recently pushed bit.

  This indexing scheme matches how the game builds up each row payload: the first bits pushed
  correspond to the earliest colour plane positions.
*/
static inline uint8_t shiftRegisterGetBit(int logicalIndex) {
  int physicalIndex = (shiftRegisterOldestIndex + logicalIndex) % PANEL_SHIFT_BITS;
  return shiftRegisterBits[physicalIndex] & 1u;
}

/*
  commitShiftRegisterToFramebufferForSelectedRow

  Decode the currently stored 192-bit row payload into the latched framebuffer, using the currently
  selected multiplexed row address.

  The coursework game code pushes bits in this order for each row-pair (as implemented by displayRow):

    Top half (row r):
      - 32 bits red plane   (x = 0..31)
      - 32 bits green plane (x = 0..31)
      - 32 bits blue plane  (x = 0..31)

    Bottom half (row r+16):
      - 32 bits red plane
      - 32 bits green plane
      - 32 bits blue plane

  LatchRegister() calls this function to "commit" the shifted data into the visible framebuffer.
*/
static void commitShiftRegisterToFramebufferForSelectedRow(void) {
  const int rowPair = (selectedRowPairIndex & 0x0F);
  const int topRowY = rowPair;
  const int bottomRowY = rowPair + 16;

  for (int x = 0; x < PANEL_PIXEL_WIDTH; x++) {
    // Top row planes
    uint8_t topR = shiftRegisterGetBit(0 * PANEL_PIXEL_WIDTH + x);
    uint8_t topG = shiftRegisterGetBit(1 * PANEL_PIXEL_WIDTH + x);
    uint8_t topB = shiftRegisterGetBit(2 * PANEL_PIXEL_WIDTH + x);

    // Bottom row planes
    uint8_t bottomR = shiftRegisterGetBit(3 * PANEL_PIXEL_WIDTH + x);
    uint8_t bottomG = shiftRegisterGetBit(4 * PANEL_PIXEL_WIDTH + x);
    uint8_t bottomB = shiftRegisterGetBit(5 * PANEL_PIXEL_WIDTH + x);

    int topPixelIndex = (topRowY * PANEL_PIXEL_WIDTH + x) * 3;
    int bottomPixelIndex = (bottomRowY * PANEL_PIXEL_WIDTH + x) * 3;

    latchedFramebufferRgb[topPixelIndex + 0] = topR;
    latchedFramebufferRgb[topPixelIndex + 1] = topG;
    latchedFramebufferRgb[topPixelIndex + 2] = topB;

    latchedFramebufferRgb[bottomPixelIndex + 0] = bottomR;
    latchedFramebufferRgb[bottomPixelIndex + 1] = bottomG;
    latchedFramebufferRgb[bottomPixelIndex + 2] = bottomB;
  }
}

// -----------------------------------------------------------------------------
// panel.h API implementations (Web/WASM)
// -----------------------------------------------------------------------------

/*
  setupPanel

  Initialise the emulated panel state.

  This clears the latched framebuffer (all pixels off), clears the shift-register buffer, and
  resets the selected row address to a known value. The shift register is treated as "fully valid"
  containing zeros so that early reads behave deterministically.

  The emulator also notifies the UI that the display is enabled.
*/
void setupPanel(void) {
  memset(latchedFramebufferRgb, 0, sizeof(latchedFramebufferRgb));
  memset(shiftRegisterBits, 0, sizeof(shiftRegisterBits));
  shiftRegisterOldestIndex = 0;
  shiftRegisterBitCount = PANEL_SHIFT_BITS; // treat as fully initialised with zeros
  selectedRowPairIndex = 0;
  latchLineIsLow = false;
  displayIsEnabled = true;

  js_set_display_state(1);
}

/*
  setupInput

  Initialise the emulated joystick/input subsystem.

  On the STM32 build this configures ADC registers. In the emulator no hardware needs initialising;
  joystick values are provided on demand by JavaScript.
*/
void setupInput(void) {
  // No work required for the browser emulator.
}

/*
  getRawInput

  Return an emulated ADC reading for the requested channel.

  The browser supplies joystick position via sliders/keyboard. emulator.js maps that position into
  a raw value that mimics what the STM32 ADC would return, and this function forwards the request
  to that JS mapping.
*/
uint32_t getRawInput(int channelValue) {
  int raw = js_get_adc(channelValue);
  if (raw < 0) raw = 0;
  return (uint32_t)raw;
}

/*
  PrepareLatch

  Emulate pulling the latch line low before shifting in a new row payload.

  On hardware, the latch pin controls when the shift register contents are made visible. The
  coursework code calls PrepareLatch() before shifting bits and then calls LatchRegister() to
  commit them.

  In the emulator, we use this to update a UI-visible "display enabled" indicator.
*/
void PrepareLatch(void) {
  latchLineIsLow = true;
  displayIsEnabled = false;
  js_set_display_state(0);
}

/*
  LatchRegister

  Emulate pulsing the latch line high to commit shifted bits to the selected row-pair.

  This is the key synchronisation point between the low-level bitstream and the visible state.
  The emulator:
    1) Marks the display as enabled,
    2) Decodes the most recent 192 shifted bits into the latched framebuffer for the currently
       selected multiplexed row address,
    3) Requests a render via JavaScript so the canvas reflects the updated state.
*/
void LatchRegister(void) {
  latchLineIsLow = false;
  displayIsEnabled = true;
  js_set_display_state(1);

  commitShiftRegisterToFramebufferForSelectedRow();

  // Render on every latch so row scanning can be observed.
  js_render_frame(latchedFramebufferRgb, (selectedRowPairIndex & 0x0F), (int)displayIsEnabled);
}

/*
  SelectRow

  Select which multiplexed row address is currently active.

  The coursework game calls SelectRow(i + 1) while supplying data for row i (and i+16), which
  implies a 1-based row selector. To mirror that convention, the emulator stores (row-1) modulo 16.

  This does not immediately change the framebuffer; it only affects where the next LatchRegister()
  commit is written.
*/
void SelectRow(int row) {
  selectedRowPairIndex = (row - 1) & 0x0F;
}

/*
  PushBit

  Shift one bit into the emulated shift register.

  The game code calls PushBit() for each colour plane bit (192 times per row-pair). This function
  models the hardware behaviour by appending the bit to the end of a fixed-size shift-register
  buffer, discarding the oldest bit if more than PANEL_SHIFT_BITS have been pushed.
*/
void PushBit(int onoff) {
  shiftRegisterPushBit((uint8_t)(onoff ? 1 : 0));
}

/*
  ClearRow

  Clear the current shift-register payload for a given row by pushing 192 zero bits.

  The hardware driver selects a row address and shifts in zeros to ensure the displayed row-pair
  is blank before new data is loaded. The emulator mirrors this behaviour exactly so that any
  game logic relying on the clear step behaves consistently.
*/
void ClearRow(int row) {
  SelectRow(row);
  for (int bitIndex = 0; bitIndex < PANEL_SHIFT_BITS; bitIndex++) {
    PushBit(0);
  }
}

/*
  delay_ms

  Provide a millisecond-scale delay that behaves appropriately for each build target.

  - Browser/WASM (Emscripten):
      Uses emscripten_sleep() so the JavaScript event loop can continue processing input and
      rendering. Also honours Pause/Step controls by waiting while paused.

  - Non-browser builds:
      Falls back to a simple busy-wait loop (similar to the coursework hardware code). This is not
      intended for precise timing; it exists only to keep the interface consistent.
*/
void delay_ms(uint32_t ms) {
#ifdef __EMSCRIPTEN__
  for (;;) {
    if (!js_is_paused()) break;
    if (js_consume_step()) break;
    emscripten_sleep(16);
  }

  if (ms > 0) {
    emscripten_sleep((int)ms);
  }
#else
  for (volatile unsigned int tmr = ms; tmr > 0; tmr--) {
    __asm__("nop");
  }
#endif
}
