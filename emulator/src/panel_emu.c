/* A 192-bit serial register, latched into one zero-based RGB row pair. */
#include "panel.h"
#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
EM_JS(void, render, (const uint8_t *pixels, int row), {
  if (window.Emu) window.Emu.renderFrame(pixels, row, true, HEAPU8);
});
EM_JS(int, adc, (int channel), {
  return window.Emu ? window.Emu.getAdc(channel) | 0 : 0;
});
EM_JS(int, paused, (), { return window.Emu && window.Emu.isPaused(); });
EM_JS(int, step, (), { return window.Emu && window.Emu.consumeStep(); });
#else
static void render(const uint8_t *pixels, int row) { (void)pixels; (void)row; }
static int adc(int channel) { (void)channel; return 0; }
#endif

#define WIDTH 32
#define SHIFT_BITS 192
static uint8_t framebuffer[32 * 32 * 3];
static uint8_t shift_bits[SHIFT_BITS];
static unsigned oldest;
static unsigned selected_row;

void setupPanel(void) {
  memset(framebuffer, 0, sizeof(framebuffer));
  memset(shift_bits, 0, sizeof(shift_bits));
  oldest = selected_row = 0;
}

void setupInput(void) {}

uint32_t getRawInput(int channel) {
  int raw = adc(channel);
  return raw < 0 ? 0 : raw > 4095 ? 4095 : (uint32_t)raw;
}

void PrepareLatch(void) {
  /* Shifting does not change latched pixels. A low latch is not output-enable. */
}

void PushBit(int value) {
  shift_bits[oldest] = value != 0;
  oldest = (oldest + 1) % SHIFT_BITS;
}

void SelectRow(int row) {
  /* Match the four address lines in panel_hw.c. */
  selected_row = (unsigned)row & 15u;
}

void LatchRegister(void) {
  for (unsigned half = 0; half < 2; half++) {
    for (unsigned channel = 0; channel < 3; channel++) {
      for (unsigned x = 0; x < WIDTH; x++) {
        unsigned source = (oldest + (half * 3 + channel) * WIDTH + x) % SHIFT_BITS;
        unsigned pixel = ((selected_row + half * 16) * WIDTH + x) * 3 + channel;
        framebuffer[pixel] = shift_bits[source];
      }
    }
  }
  render(framebuffer, (int)selected_row);
}

void ClearRow(int row) {
  SelectRow(row);
  for (int i = 0; i < SHIFT_BITS; i++) PushBit(0);
  /* The caller must latch to make the zero payload visible. */
}

void delay_ms(uint32_t ms) {
#ifdef __EMSCRIPTEN__
  while (paused() && !step()) emscripten_sleep(16);
  if (ms) emscripten_sleep(ms);
#else
  (void)ms; /* Native protocol tests have no wall-clock dependency. */
#endif
}
