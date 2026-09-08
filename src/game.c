/* Shared Pong gameplay and RGB scanout. panel.h supplies platform I/O.
 * Framebuffer coordinates and row-pair addresses are zero-based. */


#include <stdint.h>
#include <stdbool.h>
#include "panel.h"

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

#define refreshDelay 1 // One requested millisecond per row pair; browser timing is approximate.
#define refreshRate 60 // Nominal game ticks per second, not a measured wall-clock rate.
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

int gameMode;
uint32_t cycle = 0;
uint32_t startPoint;
bool newMode = true;
unsigned int winCycle = 0;
static int winnerNumber = 0;

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

char gameMatrix[32][32];

static void setPixel(int x, int y, char colour) {
  if (x >= 0 && x < panelWidth && y >= 0 && y < panelHeight) {
    gameMatrix[y][x] = colour;
  }
}

// P 1 2 W I N S ' ' T A R

const int displayDigits[11][6][4] = {
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
const int digits[10][5][4] = {{{0, 1, 1, 0}, {1, 1, 0, 1}, {1, 1, 1, 1}, {1, 0, 1, 1}, {0, 1, 1, 0}},
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

const int colours[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};

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

  ballVelocityY = 0.5f;
  oldBallX = ballX;
  oldBallY = ballY;
}

void updateDisplay(void)
{
  for (int i = 0; i < panelHeight / 2; i++)
  {
    // Scan one row address at a time (row-pair i and i+16 on a 32x32 panel).
    PrepareLatch();
    SelectRow(i);
    displayRow(gameMatrix[i]);
    displayRow(gameMatrix[i + 16]);
    LatchRegister();
    delay_ms(refreshDelay);
  }
}

void displayRow(char matrixRow[])
{
  int colourIndex;
  for (int j = 0; j < 3; j++)
  {
    for (int i = 0; i < panelWidth; i++)
    {
      switch (matrixRow[i])
      {
        default:
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

void drawPaddles(void)
{
  drawPaddle(lPaddleX, lPaddleY, oldLPaddleY, lPaddleColour);
  drawPaddle(rPaddleX, rPaddleY, oldRPaddleY, rPaddleColour);
  oldLPaddleY = lPaddleY;
  oldRPaddleY = rPaddleY;
}

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
      setPixel(x, y, 'X');
    }
  }
}

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
      setPixel(x, y, paddleColour);
    }
  }
}

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
      setPixel(x, y, ballColour);

    }
  }
}

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
      setPixel(x, y, 'X');
    }
  }
}

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

void detectCollisions(void)
{
  float impact;
  if (ballVelocityX < 0 && ballX >= lPaddleX && ballX <= lPaddleX + paddleWidth &&
      ballY + ballSize > lPaddleY && ballY < lPaddleY + paddleHeight)
  {
    if (ballVelocityX < 0)
    {
      ballVelocityX *= -1;
    }
    impact = (ballY + ballSize / 2.0f - (lPaddleY + paddleHeight / 2.0f)) / (paddleHeight / 2.0f);
    ballVelocityY = (ballSpeed * impact);
  }
  else if (ballVelocityX > 0 && ballX <= rPaddleX && ballX + ballSize >= rPaddleX &&
           ballY + ballSize > rPaddleY && ballY < rPaddleY + paddleHeight)
  {
    if (ballVelocityX > 0)
    {
      ballVelocityX *= -1;
    }
    impact = (ballY + ballSize / 2.0f - (rPaddleY + paddleHeight / 2.0f)) / (paddleHeight / 2.0f);
    ballVelocityY = (ballSpeed * impact);
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

void displayScores(void)
{
  drawDigit(lScore, ((panelWidth / 2) - (3 * netWidth)), 2);
  drawDigit(rScore, ((panelWidth / 2) + (netWidth)), 2);
}

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

void drawDigit(int digit, int startingX, int startingY)
{
  if (digit < 0 || digit > 9) return;

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
            setPixel(x, y, 'X');
          }
        }
      }
    }
  }

  void drawCharacter(char character, int startingX, int startingY)
  {
    int index = 7; // Unknown characters render as spaces.
    switch (character)
    {
      default:
      case ' ':
      index = 7;
      break;
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

  static inline float bound(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
  }

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

  void updatePaddlePositions(void)
  {
    int rawLeft = getRawPaddleInput(0);
    int rawRight = getRawPaddleInput(1);

    lPaddleY = convertInputToPaddlePosition(rawLeft);
    rPaddleY = convertInputToPaddlePosition(rawRight);
  }

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
        detectCollisions();
        updateBall();

        updateDisplay();
      }
      else if (gameMode == 2 && ((lServe && inputCheck(0.4, 0.6, 0)) || (!lServe && inputCheck(0.4, 0.6, 1))))
      {
        updateDisplay();
        gameMode = 1;
      }
      else {
        updateDisplay();
      }
    }
  }

  void winScreen(void)
  {

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
      winCycle = (winCycle + 1) % 7;
      textColour = coloursCycle[(winCycle)%7];
      textBackgroundColour = coloursCycle[(winCycle+2)%7];
      borderColour = coloursCycle[(winCycle+1)%7];
      displayWinner(winnerNumber);

    }
    updateDisplay();
  }

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
