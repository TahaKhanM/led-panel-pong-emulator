#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#define main pong_main
#include "../src/game.c"
#undef main

static int bits[192], pushed, latched, row;
void setupPanel(void) {}
void setupInput(void) {}
void delay_ms(uint32_t ms) { (void)ms; }
uint32_t getRawInput(int channel) { (void)channel; return 330; }
void PrepareLatch(void) { pushed = 0; }
void PushBit(int bit) { assert(pushed < 192); bits[pushed++] = bit; }
void SelectRow(int r) { row = r; }
void ClearRow(int r) { (void)r; assert(0 && "redundant clearing should not be needed"); }
void LatchRegister(void) { assert(pushed == 192); assert(row == latched % 16); latched++; }

int main(void) {
  initGameMatrix();
  gameMatrix[0][0] = 'R';
  gameMatrix[0][31] = 'B';
  PrepareLatch(); displayRow(gameMatrix[0]);
  assert(pushed == 96 && bits[0] == 1 && bits[95] == 1 && bits[32] == 0);
  gameMatrix[0][0] = '?';
  PrepareLatch(); displayRow(gameMatrix[0]); assert(bits[0] == 0);
  updateDisplay(); assert(latched == 16);
  for (int value = -1000; value < 6000; value++) {
    int y = convertInputToPaddlePosition(value);
    assert(y >= borderWidth && y <= panelHeight - paddleHeight - borderWidth);
  }
  initGame();
  ballVelocityY = 100; initGame(); assert(ballVelocityY == .5f);
  ballX = lPaddleX + 1; ballY = lPaddleY + paddleHeight + 0.1f; ballVelocityX = -1;
  detectCollisions(); assert(ballVelocityX == -1); /* a near miss is a miss */
  ballY = lPaddleY + 1; detectCollisions(); assert(ballVelocityX == 1);
  ballVelocityY = .7f; detectCollisions(); assert(ballVelocityY == .7f); /* moving away */
  drawPaddle(-1, -2, 100, 'R');
  ballX = -5; ballY = 90; drawBall(); eraseOldBall();
  drawDigit(-1, 0, 0); drawDigit(10, 0, 0); drawCharacter('?', -1, -1);
  lScore = 10; rScore = 1; gameMode = 3; newMode = true; cycle = 0; latched = 0;
  winScreen(); assert(winnerNumber == 0);
  cycle = 120; winScreen(); assert(winnerNumber == 0);
  lScore = 1; rScore = 10; newMode = true; winScreen(); assert(winnerNumber == 1);
  /* Wrap-safe unsigned elapsed ticks. */
  cycle = UINT32_MAX; cycle++; assert(cycle == 0);
  puts("game checks passed");
}
