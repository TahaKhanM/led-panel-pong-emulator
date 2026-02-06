/*
 * -----------------------------------------------------------------------------
 * Pong LED Panel Coursework - game.c
 * -----------------------------------------------------------------------------
 * This file contains the complete gameplay logic and software framebuffer for a
 * Pong-style game running on a 32x32 RGB LED matrix.
 *
 * High-level architecture
 * -----------------------
 * 1) Logical framebuffer (gameMatrix)
 *    - gameMatrix[y][x] stores a single character colour code for each pixel.
 *    - Colour codes used by this coursework are:
 *        'X' = off/black, 'R' = red, 'G' = green, 'B' = blue,
 *        'C' = cyan, 'M' = magenta, 'Y' = yellow, 'W' = white.
 *
 * 2) Rendering model
 *    - Drawing functions (drawBorders, drawPaddle, drawBall, drawDigit, etc.)
 *      write into gameMatrix only; they do not talk to hardware directly.
 *    - updateDisplay() performs the physical refresh by scanning the panel:
 *        - The panel is multiplexed as two 16-row halves (top rows 0..15 and
 *          bottom rows 16..31).
 *        - For each row address i (0..15), updateDisplay shifts 192 bits:
 *            32 pixels x 3 colour planes x 2 halves = 192
 *          and then latches the data for the selected row pair (i and i+16).
 *    - The low-level I/O primitives (PrepareLatch, LatchRegister, SelectRow,
 *      PushBit, ClearRow, getRawInput, delay_ms, etc.) are provided by the
 *      hardware abstraction layer declared in panel.h and implemented by:
 *        - panel_hw.c (STM32/libopencm3 target), or
 *        - panel_emu.c (web/WASM emulator target).
 *
 * 3) Input model
 *    - Each paddle reads an analogue joystick via ADC channels using getRawInput.
 *    - The raw readings are normalised between minPaddleVal/maxPaddleVal and
 *      mapped into a paddle Y position on screen.
 *
 * 4) Game state machine
 *    - gameMode controls which screen/logic runs:
 *        0: Start screen
 *        1: Active gameplay
 *        2: Point-won pause / serve wait
 *        3: Win screen
 *    - cycle is a coarse "tick" counter used for timing together with refreshRate.
 *
 * Important note on correctness
 * -----------------------------
 * This file is written to match the coursework panel driver’s bit ordering and
 * row-pair scanning scheme. Any change to the ordering in displayRow() or the
 * scan loop in updateDisplay() will change what appears on the physical panel.
 * -----------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "panel.h"

/* -----------------------------------------------------------------------------
 * Compile-time configuration constants
 * -----------------------------------------------------------------------------
 * The following #defines specify the panel geometry and the game element sizes.
 * Many drawing routines assume these dimensions (e.g., paddleHeight, borderWidth).
 * Changing them will change gameplay layout and may require adjusting glyph placement.
 * ----------------------------------------------------------------------------- */

#define panelWidth 32
#define panelHeight 32
#define ballSize 1
#define ballSpeed 1
#define paddleWidth 1
#define paddleHeight 4
#define paddleGap 2
#define netWidth 2
#define borderWidth 1
#define winScore 10
#define maxPaddleVal 105
#define minPaddleVal 555

#define refreshDelay 1 // refresh rate of about 20 hz (20.8333.. not accounting for calculations)
#define refreshRate 60 //  (1000)/(refreshDelay * 16)
#define screenLength 5 // 5 seconds for start and winning screen

char lPaddleColour = 'R';
char rPaddleColour = 'B';
char ballColour = 'W';
char netColour = 'W';
char borderColour = 'W';
char scoreColour = 'W';
char textColour = 'W';
char textBackgroundColour = 'X';

char coloursCycle[7] = {'M', 'R', 'G', 'B', 'R', 'Y', 'C'};

// all of which are giving the top left of each object
int lPaddleX;
int rPaddleX;
int lPaddleY;
int rPaddleY;
float ballX;
float ballY;

int oldLPaddleY;
int oldRPaddleY;
float oldBallX;
float oldBallY;

float ballVelocityX;
float ballVelocityY;
bool lServe = false;

int lScore = 0;
int rScore = 0;

// 0 for Start; 1 for Game; 2 for point Won Pause 3 for Winner Screen
/* The main UI/game state:
 *   0 = Start screen
 *   1 = Gameplay (ball moves)
 *   2 = Point-won pause / waiting for serve gesture
 *   3 = Winner screen
 */
int gameMode;
int cycle = 0;
int startPoint;
bool newMode = true;
int winCycle = 0;

void initGameMatrix(void);
void initGame(void);
void updateDisplay(void);
void displayRow(char matrixRow[]);
void drawPaddles(void);
void eraseOldPaddles(int paddleX, int oldPaddleY);
void drawPaddle(int paddleX, int paddleY, int oldPaddleY, char paddleColour);
void drawBall(void);
void eraseOldBall(void);
void drawNet(void);
void drawBorders(void);
void detectCollisions(void);
bool detectPointWin(void);
void displayScores(void);
int handleWin(void);
void displayStart(void);
void displayWinner(int winner);
void drawDigit(int digit, int startingX, int startingY);
void drawCharacter(char character, int startingX, int startingY);
void updateBall(void);
void tempDisplay(void);
void startScreen(void);
void mainGame(void);
void winScreen(void);
int convertInputToPaddlePosition(int inputValue);
void setupPanel(void);
void setupInput(void);
void updatePaddlePositions(void);
int getRawPaddleInput(int whichPaddle);
bool inputCheck(float minimumValue, float maximumValue, int chosePaddle);

// X R G B C Y M W
/* -----------------------------------------------------------------------------
 * Framebuffer and glyph tables
 * -----------------------------------------------------------------------------
 * gameMatrix is the 32x32 logical framebuffer. Each cell stores a colour code.
 *
 * displayDigits is a small 6x4 bitmap font used for letters in "P1/P2 WINS START".
 * digits is a 5x4 bitmap font for numeric score rendering.
 *
 * colours maps a colour index (0..7) to {R,G,B} bit-planes used by displayRow().
 * ----------------------------------------------------------------------------- */

char gameMatrix[32][32];

// P 1 2 W I N S ' ' T A R

int displayDigits[11][6][4] = {
  {{1, 1, 1, 0}, {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 1, 0}, {1, 0, 0, 0}, {1, 0, 0, 0}},
  {{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 1, 0}},
  {{0, 1, 1, 0}, {1, 0, 0, 1}, {0, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 0, 0}, {1, 1, 1, 1}},
  {{1, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}},
  {{1, 1, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 1, 0}},
  {{1, 0, 0, 1}, {1, 1, 0, 1}, {1, 0, 1, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}},
  {{0, 1, 1, 1}, {1, 0, 0, 0}, {0, 1, 1, 0}, {0, 0, 0, 1}, {0, 0, 0, 1}, {1, 1, 1, 0}},
  {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
  {{1, 1, 1, 1}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}},
  {{0, 1, 1, 0}, {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 1, 1}, {1, 0, 0, 1}, {1, 0, 0, 1}},
  {{1, 1, 1, 0}, {1, 0, 0, 1}, {1, 0, 0, 1}, {1, 1, 1, 0}, {1, 0, 0, 1}, {1, 0, 0, 1}},
};

// 0 1 2 3 4 5 6 7 8 9
int digits[10][5][4] = {{{0, 1, 1, 0}, {1, 1, 0, 1}, {1, 1, 1, 1}, {1, 0, 1, 1}, {0, 1, 1, 0}},
{{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 1, 1}},
{{0, 1, 1, 0}, {1, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}, {1, 1, 1, 1}},
{{1, 1, 1, 0}, {0, 0, 0, 1}, {0, 1, 1, 0}, {0, 0, 0, 1}, {1, 1, 1, 0}},
{{0, 0, 1, 0}, {0, 1, 0, 0}, {1, 0, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 1}},
{{1, 1, 1, 1}, {1, 0, 0, 0}, {1, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 1, 0}},
{{0, 1, 1, 1}, {1, 0, 0, 0}, {1, 1, 1, 1}, {1, 0, 0, 1}, {0, 1, 1, 0}},
{{1, 1, 1, 1}, {0, 0, 0, 1}, {0, 0, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}},
{{0, 1, 1, 0}, {1, 0, 0, 1}, {0, 1, 1, 0}, {1, 0, 0, 1}, {1, 1, 1, 1}},
{{1, 1, 1, 1}, {1, 0, 0, 1}, {0, 1, 1, 1}, {0, 0, 0, 1}, {1, 1, 1, 0}}};

// X R G B Y C M

int colours[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};
/*
 * initGameMatrix
 * Clears the 32x32 logical framebuffer (gameMatrix) to the background colour code 'X'.
 * The game draws everything (borders, paddles, ball, text) by writing colour codes into gameMatrix.
 * updateDisplay() later scans gameMatrix row-by-row and pushes the corresponding RGB bitstream
 * to the panel shift registers.
 */

void initGameMatrix(void)
{
  for (int i = 0; i < panelHeight; i++)
  {
    for (int j = 0; j < panelWidth; j++)
    {
      gameMatrix[i][j] = 'X';
    }
  }
}
/*
 * initGame
 * Resets per-round game state (paddle positions, ball position, velocity, serve side, and old-* trackers).
 * This is called when entering the main game mode and after a point is scored, so that the ball and paddles
 * start from a consistent baseline while scores persist across points until a win condition is reached.
 */

void initGame(void)
{
  lPaddleY = panelHeight / 2 - 2;
  lPaddleX = paddleGap;
  
  rPaddleY = panelHeight / 2 - 2;
  rPaddleX = panelWidth - paddleGap - paddleWidth;
  
  oldLPaddleY = lPaddleY;
  oldRPaddleY = rPaddleY;
  
  ballX = panelWidth / 2 - 1;
  ballY = panelHeight / 2 - 1;
  
  lServe = !lServe;
  if (lServe)
  {
    ballX -= 2;
    ballVelocityX = -ballSpeed;
  }
  else
  {
    ballX += 2;
    ballVelocityX = ballSpeed;
  }
  
  oldBallX = ballX;
  oldBallY = ballY;
}
/*
 * updateDisplay
 * Implements the panel refresh / scan routine for a multiplexed 32x32 LED matrix that is wired as two 16-row halves.
 *
 * For each row address i in [0..15]:
 *   1) ClearRow(i) shifts 0s for that row payload (prevents ghosting on hardware).
 *   2) PrepareLatch() sets the latch low so the display stops showing while we shift new bits.
 *   3) SelectRow(i+1) drives the A/B/C/D row address lines (this implementation uses i+1, matching the coursework
 *      wiring/driver conventions).
 *   4) displayRow(gameMatrix[i]) shifts 96 bits for the top half row i (32 pixels * 3 colour planes).
 *   5) displayRow(gameMatrix[i+16]) shifts 96 bits for the corresponding bottom half row (i+16).
 *   6) LatchRegister() commits the 192 shifted bits into the panel output register so the selected row-pair displays.
 *   7) delay_ms(refreshDelay) holds the row briefly before advancing to the next row-pair.
 *
 * The combination of fast row scanning and human persistence of vision yields an apparently stable full frame.
 */

void updateDisplay(void)
{
  for (int i = 0; i < panelHeight / 2; i++)
  {
    // Scan one row address at a time (row-pair i and i+16 on a 32x32 panel).
    ClearRow(i);
    PrepareLatch();
    SelectRow(i + 1);
    displayRow(gameMatrix[i]);
    displayRow(gameMatrix[i + 16]);
    LatchRegister();
    delay_ms(refreshDelay);
  }
}
/*
 * displayRow
 * Converts one logical row of 32 colour codes (matrixRow[0..31]) into the physical serial bitstream expected by the panel.
 *
 * The panel uses 3 bit-planes per pixel (R, G, B). This function loops colour-plane-first (j = 0..2) and then pixel index
 * (i = 0..31) to push bits in the order assumed by the coursework hardware driver.
 *
 * Each character in matrixRow is mapped to a colour index (0..7) and then into colours[colourIndex][j] where:
 *   colours[k] = {Rbit, Gbit, Bbit} for the colour k.
 * PushBit() is called once per bit to shift it into the panel.
 */

void displayRow(char matrixRow[])
{
  int colourIndex;
  for (int j = 0; j < 3; j++)
  {
    for (int i = 0; i < panelWidth; i++)
    {
      switch (matrixRow[i])
      {
        case 'X':
        colourIndex = 0;
        break;
        case 'W':
        colourIndex = 7;
        break;
        case 'R':
        colourIndex = 1;
        break;
        case 'B':
        colourIndex = 3;
        break;
        case 'C':
        colourIndex = 5;
        break;
        case 'M':
        colourIndex = 6;
        break;
        case 'Y':
        colourIndex = 4;
        break;
        case 'G':
        colourIndex = 2;
        break;
      }
      
      // Push the bit for the current colour plane (j=0..2) for this pixel.
      PushBit(colours[colourIndex][j]);
    }
  }
}
/*
 * drawPaddles
 * Draws both paddles into gameMatrix and updates the stored "old" paddle positions so the next frame can erase and redraw
 * efficiently. The left and right paddles are drawn independently using drawPaddle().
 */

void drawPaddles(void)
{
  drawPaddle(lPaddleX, lPaddleY, oldLPaddleY, lPaddleColour);
  drawPaddle(rPaddleX, rPaddleY, oldRPaddleY, rPaddleColour);
  oldLPaddleY = lPaddleY;
  oldRPaddleY = rPaddleY;
}
/*
 * eraseOldPaddles
 * Clears the previous paddle rectangle from gameMatrix by writing the background code 'X' over the region defined by:
 *   x in [paddleX, paddleX + paddleWidth)
 *   y in [oldPaddleY, oldPaddleY + paddleHeight)
 * This is used to remove the paddle's previous position before drawing the paddle at its new position.
 */
void eraseOldPaddles(int paddleX, int oldPaddleY)
{
  int x;
  int y;
  for (int i = 0; i < paddleWidth; i++)
  {
    x = paddleX + i;
    for (int j = 0; j < paddleHeight; j++)
    {
      y = oldPaddleY + j;
      gameMatrix[y][x] = 'X';
    }
  }
}
/*
 * drawPaddle
 * Moves a single paddle from its old y-position to its current y-position.
 *
 * Steps:
 *   1) Erase the old paddle footprint (eraseOldPaddles).
 *   2) Clamp the new y-position to panel bounds so the paddle stays on screen.
 *   3) Write paddleColour into gameMatrix over the paddle rectangle.
 *
 * This function writes only to gameMatrix; updateDisplay() later pushes the updated framebuffer to the panel.
 */

void drawPaddle(int paddleX, int paddleY, int oldPaddleY, char paddleColour)
{
  int x;
  int y;
  eraseOldPaddles(paddleX, oldPaddleY);
  for (int i = 0; i < paddleWidth; i++)
  {
    x = paddleX + i;
    for (int j = 0; j < paddleHeight; j++)
    {
      y = paddleY + j;
      gameMatrix[y][x] = paddleColour;
    }
  }
}
/*
 * drawBall
 * Writes the ball into gameMatrix at the current (ballX, ballY) position and records the previous ball position so it can
 * be erased on the next update. The ball is represented as a small square (ballSize) but in this implementation ballSize=1
 * so it is a single pixel.
 */

void drawBall(void)
{
  int x;
  int y;
  oldBallX = ballX;
  oldBallY = ballY;
  for (int i = 0; i < ballSize; i++)
  {
    x = ((int)ballX) + i;
    for (int j = 0; j < ballSize; j++)
    {
      y = ((int)ballY) + j;
      gameMatrix[y][x] = ballColour;
      
    }
  }
}
/*
 * eraseOldBall
 * Clears the ball's previously drawn pixel(s) from gameMatrix by writing background 'X'. This prevents trails as the ball
 * moves. The position erased is tracked by oldBallX/oldBallY.
 */

void eraseOldBall(void)
{
  int x;
  int y;
  for (int i = 0; i < ballSize; i++)
  {
    x = ((int)oldBallX) + i;
    for (int j = 0; j < ballSize; j++)
    {
      y = ((int)oldBallY) + j;
      gameMatrix[y][x] = 'X';
    }
  }
}
/*
 * drawNet
 * Draws the centre net line down the middle of the screen using netColour. This is purely a visual element and does not
 * affect collision logic (collisions are handled separately based on ball/paddle coordinates).
 */

void drawNet(void)
{
  int x;
  int y;
  for (int i = 0; i < netWidth; i++)
  {
    x = panelWidth / 2 + i - netWidth / 2;
    for (int j = 0; j < panelHeight; j++)
    {
      y = j;
      if ((y % (netWidth * 2)) < netWidth)
      {
        gameMatrix[y][x] = netColour;
      }
    }
  }
}
/*
 * drawBorders
 * Draws the outer border rectangle of the playfield into gameMatrix using borderColour. The border provides a visual
 * boundary and is also used conceptually by collision logic to constrain ball movement.
 */

void drawBorders(void)
{
  for (int j = 0; j < borderWidth; j++)
  {
    for (int i = 0; i < panelWidth; i++)
    {
      gameMatrix[j][i] = borderColour;
      gameMatrix[panelHeight - 1 - j][i] = borderColour;
    }
  }
}
/*
 * drawWinBorders
 * Draws a special border style for the win screen. This is used to visually differentiate the win state from gameplay.
 */

void drawWinBorders(void) {
  for (int j = 0; j < borderWidth; j++)
  {
    for (int i = 0; i < panelWidth; i++)
    {
      gameMatrix[j][i] = borderColour;
      gameMatrix[i][j] = borderColour;
      gameMatrix[panelHeight - 1 - j][i] = borderColour;
      gameMatrix[i][panelWidth - 1 - j] = borderColour;
    }
  }
}
/*
 * detectCollisions
 * Updates the ball velocity (ballVelocityX/Y) based on collisions with:
 *   - top/bottom borders,
 *   - left/right paddles (including rebound direction),
 *   - and potentially other playfield elements depending on the current state.
 *
 * Collision detection is performed using the ball's current position and the paddle rectangles. When a collision is
 * detected, the corresponding velocity component is inverted and/or adjusted.
 */

void detectCollisions(void)
{
  float yRandom;
  if ((((ballX - lPaddleX) <= paddleWidth) && ((ballX - lPaddleX) >= 0)) && (((ballY - lPaddleY) <= (paddleHeight + ballSize) && ((ballY - lPaddleY) >= (-ballSize)))))
  {
    if (ballVelocityX < 0)
    {
      ballVelocityX *= -1;
    }
    yRandom = ((-(lPaddleY + (paddleHeight/2)) + ballY)) / ((paddleHeight+1)/2);
    ballVelocityY = (ballSpeed * yRandom);
  }
  else if ((((rPaddleX - ballX) <= ballSize) && ((rPaddleX - ballX) >= 0)) && (((ballY - rPaddleY) <= (paddleHeight + ballSize) && ((ballY - rPaddleY) >= (-ballSize)))))
  {
    if (ballVelocityX > 0)
    {
      ballVelocityX *= -1;
    }
    yRandom = ((-(rPaddleY + (paddleHeight/2)) + ballY)) / ((paddleHeight+1)/2);
    ballVelocityY = (ballSpeed * yRandom);
  }
  
  if (ballY >= (panelHeight - 1 - borderWidth))
  {
    if (ballVelocityY > 0) {
      ballVelocityY *= -1;
    } else if (ballVelocityY == 0) {
      ballVelocityY = -0.5;
    }
  } else if ((ballY <= borderWidth + 1)) {
    if (ballVelocityY < 0) {
      ballVelocityY *= -1;
    } else if (ballVelocityY == 0) {
      ballVelocityY = 0.5;
    }
  }
}
/*
 * detectPointWin
 * Detects when the ball has gone past a paddle (i.e., a point has been scored).
 *
 * If a point is detected:
 *   - increments the appropriate player's score,
 *   - sets serve state (lServe) for the next round,
 *   - resets ball/paddle state as needed (via initGame or by directly setting positions),
 *   - and returns true so the state machine can transition to the next mode.
 *
 * Returns false when no point has been scored in this tick.
 */

bool detectPointWin(void)
{
  if ((ballX >= (panelWidth - ballSize)))
  {
    lScore += 1;
    return true;
  }
  else if ((ballX < 0))
  {
    rScore += 1;
    return true;
  }
  else
  {
    return false;
  }
}
/*
 * displayScores
 * Renders the current left and right scores into gameMatrix, typically near the top of the screen, using scoreColour.
 * Uses drawDigit() to paint 4x5 digit bitmaps from the digits[][][] lookup table.
 */

void displayScores(void)
{
  drawDigit(lScore, ((panelWidth / 2) - (3 * netWidth)), 2);
  drawDigit(rScore, ((panelWidth / 2) + (netWidth)), 2);
}
/*
 * handleWin
 * Determines the winner based on lScore/rScore and prepares the win screen visuals.
 * Returns an integer representing the winning player (e.g., 1 for left, 2 for right) so that displayWinner() can show the
 * correct text.
 */

int handleWin(void)
{
  int winner;
  if (lScore > rScore)
  {
    winner = 0;
  }
  else
  {
    winner = 1;
  }
  displayWinner(winner);
  return winner;
}
/*
 * displayStart
 * Draws the start screen ("START" and/or a prompt) into gameMatrix using textColour/textBackgroundColour.
 * The start screen is shown when gameMode == 0 and waits for a user input gesture to begin gameplay.
 */

void displayStart(void)
{
  // START 5 character = 20 pixels long
  int spacing = 1;
  int startOffsetX = ((panelWidth - 20) - (spacing * 6)) / 2;
  int characterLength = 4;
  drawCharacter('S', (startOffsetX + (characterLength * 0) + (spacing * 1)), ((panelHeight / 2) - 3));
  drawCharacter('T', ((startOffsetX + (characterLength * 1)) + (spacing * 2)), ((panelHeight / 2) - 3));
  drawCharacter('A', ((startOffsetX + (characterLength * 2)) + (spacing * 3)), ((panelHeight / 2) - 3));
  drawCharacter('R', ((startOffsetX + (characterLength * 3)) + (spacing * 4)), ((panelHeight / 2) - 3));
  drawCharacter('T', ((startOffsetX + (characterLength * 4)) + (spacing * 5)), ((panelHeight / 2) - 3));
}
/*
 * displayWinner
 * Draws the win screen messaging (e.g., "P1 WINS" or "P2 WINS") into gameMatrix.
 * This function relies on the displayDigits[][][] glyph table and drawCharacter() to paint 6x4 character bitmaps.
 */

void displayWinner(int winner)
{
  // P1 WINS 7 character = 28 pixels long
  int startOffsetX = 2;
  int characterLength = 4;
  drawCharacter('P', (startOffsetX + (characterLength * 0)), ((panelHeight / 2) - 3));
  if (winner == 0)
  {
    drawCharacter('1', (startOffsetX + (characterLength * 1) + 1), ((panelHeight / 2) - 3));
  }
  else
  {
    drawCharacter('2', (startOffsetX + (characterLength * 1)+ 1), ((panelHeight / 2) - 3));
  }
  drawCharacter('W', (startOffsetX + (characterLength * 2)+3), ((panelHeight / 2) - 3));
  drawCharacter('I', (startOffsetX + (characterLength * 3)+3), ((panelHeight / 2) - 3));
  drawCharacter('N', (startOffsetX + (characterLength * 4)+3), ((panelHeight / 2) - 3));
  drawCharacter('S', (startOffsetX + (characterLength * 5)+4), ((panelHeight / 2) - 3));
  drawWinBorders();
}
/*
 * drawDigit
 * Draws a single numeric digit (0..9) into gameMatrix at a given top-left position using the digits[][][] bitmap table.
 * Each digit is a 5x4 bitmap. Any '1' in the bitmap is drawn with scoreColour; '0' leaves the existing pixel unchanged
 * or writes background depending on caller usage.
 */

void drawDigit(int digit, int startingX, int startingY)
{
  
  int x;
  int y;
  for (int i = 0; i < 5; i++)
  {
    y = startingY + i;
    for (int j = 0; j < 4; j++)
    {
      x = startingX + j;
      if (y >= 0 && y < panelHeight &&
        x >= 0 && x < panelWidth)
        {
          if (digits[digit][i][j] == 1)
          {
            gameMatrix[y][x] = scoreColour;
          }
          else
          {
            gameMatrix[y][x] = 'X';
          }
        }
      }
    }
  }
/*
 * drawCharacter
 * Draws one character used in the start/win screens into gameMatrix at the specified top-left position.
 * Characters are selected via a switch statement and mapped to an index into displayDigits[][][] (6x4 glyphs).
 * Bitmap pixels set to 1 are drawn using textColour; pixels set to 0 are filled with textBackgroundColour.
 */
  
  void drawCharacter(char character, int startingX, int startingY)
  {
    int index;
    switch (character)
    {
      case 'P':
      index = 0;
      break;
      case '1':
      index = 1;
      break;
      case '2':
      index = 2;
      break;
      case 'W':
      index = 3;
      break;
      case 'I':
      index = 4;
      break;
      case 'N':
      index = 5;
      break;
      case 'S':
      index = 6;
      break;
      case ' ':
      index = 7;
      break;
      case 'T':
      index = 8;
      break;
      case 'A':
      index = 9;
      break;
      case 'R':
      index = 10;
      break;
    }
    int x;
    int y;
    for (int i = 0; i < 6; i++)
    {
      y = startingY + i;
      for (int j = 0; j < 4; j++)
      {
        x = startingX + j;
        if (y >= 0 && y < panelHeight && x >= 0 && x < panelWidth)
        {
          if (displayDigits[index][i][j] == 1)
          {
            gameMatrix[y][x] = textColour;
          }
          else
          {
            gameMatrix[y][x] = textBackgroundColour;
          }
        }
      }
    }
  }
/*
 * updateBall
 * Advances the ball position by adding the current velocity components (ballVelocityX/Y) to (ballX, ballY).
 * This is the core motion integration step; collision handling (which may flip velocity) is performed separately.
 */
  
  void updateBall(void)
  {
    ballX += ballVelocityX;
    ballY += ballVelocityY;
    
  if (ballY >= (panelHeight - 1 - borderWidth))
  {
    ballY = (panelHeight - 1 - borderWidth) + 0.00001;
  } else if ((ballY <= borderWidth + 1)) {
    ballY = borderWidth + 1 - 0.00001;
  }
  }
/*
 * tempDisplay
 * Auxiliary rendering routine used during development/testing to visualise intermediate states or patterns.
 * This does not change the physical display directly; it writes into gameMatrix and relies on updateDisplay() to refresh.
 */
  
  void tempDisplay(void)
  {
    for (int i = 0; i < 32; i++)
    {
      for (int j = 0; j < 32; j++)
      {
        if (gameMatrix[i][j] == 'X')
        {
          printf(" ");
        }
        else
        {
          printf("*");
        }
      }
      printf("\n");
    }
    for (int k = 0; k < 10; k++)
    {
      printf("\n");
    }
  }
/*
 * bound
 * Utility clamp that constrains an integer value to the inclusive range [lowerBound, upperBound].
 * Used to keep paddles and other objects within the visible panel coordinates.
 */
  
  static inline float bound(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
  }
/*
 * convertInputToPaddlePosition
 * Maps a raw joystick reading (inputValue) into a paddle Y position in screen coordinates.
 *
 * The raw joystick values are assumed to lie between minPaddleVal and maxPaddleVal, and are normalised to a [0..1] range.
 * The resulting normalised value is then scaled to the valid paddle travel range (0..panelHeight - paddleHeight - 1).
 *
 * This function uses floating-point normalisation to preserve smooth control.
 */
  
  int convertInputToPaddlePosition(int inputValue)
  {
    // normalise to 0..1 (0 = top, 1 = bottom)
    float norm = ((float)inputValue - (float)minPaddleVal) /
    ((float)maxPaddleVal - (float)minPaddleVal);
    
    norm = bound(norm);
    
    int maxY = panelHeight - paddleHeight - borderWidth;   // your existing convention
    int y = (int)(norm * (float)maxY + 0.5f);    // round to nearest
    
    if (y < borderWidth) y = borderWidth;
    if (y > maxY) y = maxY;
    return y;
  }
/*
 * getRawPaddleInput
 * Reads the joystick channels for one paddle and returns a single raw value representing the vertical axis.
 *
 * The coursework wiring uses two ADC channels per joystick (one for "up" direction and one for "down" direction).
 * This function reads both and selects whichever channel is currently active (non-zero) so the game logic can treat the
 * joystick as a single-axis input.
 */
  
  int getRawPaddleInput(int whichPaddle)
  {
    uint32_t leftUp;
    uint32_t leftDown;
    uint32_t rightUp;
    uint32_t rightDown;
    if (whichPaddle == 0)
    {
      int rawLeft;
      leftUp = getRawInput(1);
      leftDown = getRawInput(2);
      if (leftUp != 0)
      {
        rawLeft = (int)leftUp;
      }
      else
      {
        rawLeft = (int)leftDown;
      }
      return rawLeft;
    }
    else
    {
      int rawRight;
      rightUp = getRawInput(6);
      rightDown = getRawInput(7);
      if (rightUp != 0)
      {
        rawRight = (int)rightUp;
      }
      else
      {
        rawRight = (int)rightDown;
      }
      return rawRight;
    }
  }
/*
 * updatePaddlePositions
 * Reads both joysticks via getRawPaddleInput() and updates lPaddleY/rPaddleY by converting the raw ADC values to screen
 * coordinates with convertInputToPaddlePosition(). This updates only the logical positions; drawing occurs separately.
 */
  
  void updatePaddlePositions(void)
  {
    int rawLeft = getRawPaddleInput(0);
    int rawRight = getRawPaddleInput(1);
    
    lPaddleY = convertInputToPaddlePosition(rawLeft);
    rPaddleY = convertInputToPaddlePosition(rawRight);
  }
/*
 * inputCheck
 * Convenience helper used by the state machine to detect whether a given paddle input is inside or outside a normalised
 * window.
 *
 * The function:
 *   - reads both paddles,
 *   - normalises each raw reading to [0..1] using minPaddleVal/maxPaddleVal,
 *   - and returns true when the selected paddle's normalised value is outside [minimumValue, maximumValue].
 *
 * This is used to detect "any movement" or "return to centre" gestures without needing exact thresholds in the calling code.
 */
  
  bool inputCheck(float minimumValue, float maximumValue, int chosenPaddle)
  {
    int rawLeft = getRawPaddleInput(0);
    int rawRight = getRawPaddleInput(1);
    
    float normaliseLeft = ((float)rawLeft - (float)minPaddleVal) /
    ((float)maxPaddleVal - (float)minPaddleVal);
    
    float normaliseRight = ((float)rawRight - (float)minPaddleVal) /
    ((float)maxPaddleVal - (float)minPaddleVal);
    if ((((normaliseLeft <= minimumValue) || (normaliseLeft >= maximumValue)) && (chosenPaddle == 0)) || (((normaliseRight >= maximumValue) || (normaliseRight <= minimumValue)) && (chosenPaddle == 1)))
    {
      return true;
    }
    else
    {
      return false;
    }
  }
/*
 * startScreen
 * Implements the start-screen state (gameMode == 0) as a small state machine:
 *   - On first entry (newMode == true), it clears the framebuffer, draws borders and the start prompt, and records the
 *     entry time in startPoint.
 *   - While active, it waits for a joystick gesture (inputCheck thresholds) to transition into gameplay mode.
 *   - updateDisplay() is called each cycle to keep the panel refreshed.
 */
  
  void startScreen(void)
  {
    if (newMode)
    {
      initGameMatrix();
      drawBorders();
      displayStart();
      newMode = false;
      startPoint = cycle;
    }
    else if (inputCheck(0.1, 0.9, 0) && inputCheck(0.1, 0.9, 1))
    {
      gameMode = 1;
      newMode = true;
    }
    updateDisplay();
  }
/*
 * mainGame
 * Implements the main gameplay states:
 *   - gameMode == 1: active play (ball moves, collisions are processed).
 *   - gameMode == 2: point-scored pause / serve-wait (ball/paddles displayed but ball movement paused until serve gesture).
 *
 * On entering the mode (newMode == true), it initialises the framebuffer and round state.
 * Each tick it:
 *   - checks for point scoring and transitions to either win screen (mode 3) or serve pause (mode 2),
 *   - otherwise updates/draws ball, paddles, net, scores, and runs collision + motion when in active play,
 *   - and finally calls updateDisplay() to push the updated framebuffer to the panel.
 */
  
  void mainGame(void)
  {
    if (newMode)
    {
      initGameMatrix();
      initGame();
      drawBorders();
      newMode = false;
      startPoint = cycle;
    }
    if (detectPointWin())
    {
      if ((lScore >= winScore) || (rScore >= winScore))
      {
        gameMode = 3;
        newMode = true;
      }
      else
      {
        gameMode = 2;
        newMode = true;
      }
    }
    else
    {
      eraseOldBall();
      displayScores();
      drawBall();
      updatePaddlePositions();
      drawPaddles();
      drawNet();
      if (gameMode == 1)
      {
        drawNet();
        detectCollisions();
        updateBall();
        
        updateDisplay();
      }
      else if ((gameMode == 2) && ((inputCheck(0.4, 0.6, 0)) && (lServe)) || ((inputCheck(0.4, 0.6, 1)) && (!lServe)))
      {
        updateDisplay();
        gameMode = 1;
      } 
      else {
        updateDisplay();
      }
    }
  }
/*
 * winScreen
 * Implements the win-screen state (gameMode == 3):
 *   - On entry, it clears the framebuffer, determines the winner, draws borders, and records entry time.
 *   - Periodically cycles colours for a simple animated effect.
 *   - After a minimum display time, waits for both players to move their joysticks (gesture) before returning to the start
 *     screen and resetting scores.
 * updateDisplay() is called each cycle to keep the panel refreshed.
 */
  
  void winScreen(void)
  {
    int winnerNumber;
    if (newMode)
    {
      initGameMatrix();
      winnerNumber = handleWin();
      drawBorders();
      newMode = false;
      startPoint = cycle;
    }
    else if ((inputCheck(0.1, 0.9, 0) && inputCheck(0.1, 0.9, 1)) && (((cycle - startPoint) / refreshRate) >= screenLength))
    {
      textColour = 'W';
      textBackgroundColour = 'X';
      borderColour = 'W';
      gameMode = 0;
      lScore = 0;
      rScore = 0;
      newMode = true;
    }
    else if (cycle % (int)(refreshRate*2) == 0)
    {
      winCycle += 1;
      textColour = coloursCycle[(winCycle)%7];
      textBackgroundColour = coloursCycle[(winCycle+2)%7];
      borderColour = coloursCycle[(winCycle+1)%7];
      displayWinner(winnerNumber);

    }
    updateDisplay();
  }
/*
 * main
 * Program entry point for the STM32 target:
 *   - Initialises the LED panel GPIO/ADC via setupPanel() and setupInput().
 *   - Runs an infinite loop that dispatches to the current screen handler based on gameMode.
 *   - Increments the global cycle counter each iteration; cycle is used as a coarse timing source together with refreshRate.
 *
 * The loop never exits on embedded hardware; return 0 is included for completeness.
 */
  
  int main(void)
  {
    
    setupPanel();
    setupInput();
    
    while (true)
    {
      
      if (gameMode == 0)
      {
        startScreen();
      }
      else if (gameMode == 3)
      {
        winScreen();
      }
      else
      {
        mainGame();
      }
      cycle += 1;
    }
    
    return 0;
  }