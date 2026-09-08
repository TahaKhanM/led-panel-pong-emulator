#include <assert.h>
#include <stdio.h>
#include "../emulator/src/panel_emu.c"

int main(void) {
  setupPanel();
  SelectRow(0);
  for (int i = 0; i < 192; i++) PushBit(i == 0 || i == 191);
  assert(framebuffer[0] == 0); /* shift without latch cannot change pixels */
  LatchRegister();
  assert(framebuffer[0] == 1); /* top row first red */
  assert(framebuffer[(16 * 32 + 31) * 3 + 2] == 1); /* bottom last blue */
  for (int i = 0; i < 192; i++) PushBit(0);
  PushBit(1); /* overflow leaves the newest 192 bits */
  SelectRow(15);
  LatchRegister();
  assert(framebuffer[(31 * 32 + 31) * 3 + 2] == 1);
  assert(framebuffer[0] == 1); /* another row remains latched */
  ClearRow(15);
  assert(framebuffer[(31 * 32 + 31) * 3 + 2] == 1);
  LatchRegister();
  assert(framebuffer[(31 * 32 + 31) * 3 + 2] == 0);
  setupPanel();
  for (unsigned i = 0; i < sizeof(framebuffer); i++) assert(framebuffer[i] == 0);
  puts("protocol checks passed");
}
