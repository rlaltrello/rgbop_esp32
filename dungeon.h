#ifndef DUNGEON_H
#define DUNGEON_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>


#define MAP_SIZE 8
#define DRAW_DISTANCE 3

class Dungeon {
private:
  int playerX;
  int playerY;
  int playerDir; // 0: N, 1: E, 2: S, 3: W

  uint8_t map[MAP_SIZE][MAP_SIZE] = {
    {1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 1, 1, 1, 0, 1},
    {1, 0, 1, 1, 0, 0, 0, 1},
    {1, 0, 1, 1, 0, 1, 1, 1},
    {1, 0, 1, 0, 0, 1, 1, 1},
    {1, 0, 1, 1, 0, 1, 1, 1},
    {1, 1, 1, 1, 1, 1, 1, 1}
  };

  const int depthInset[DRAW_DISTANCE + 1] = { 0, 14, 22, 27 };

  const int fwdX[4]   = {  0,  1,  0, -1 };
  const int fwdY[4]   = { -1,  0,  1,  0 };
  const int rightX[4] = {  1,  0, -1,  0 };
  const int rightY[4] = {  0,  1,  0, -1 };

  bool isWallAt(int x, int y) {
    if (x < 0 || x >= MAP_SIZE || y < 0 || y >= MAP_SIZE) return true;
    return map[y][x] == 1;
  }

public:
  Dungeon(int startX = 4, int startY = 4, int startDir = 0)
    : playerX(startX), playerY(startY), playerDir(startDir) {}

  void reset() {
    playerX = 4;
    playerY = 4;
    playerDir = 0;
  }

  void moveForward() {
    int nextX = playerX + fwdX[playerDir];
    int nextY = playerY + fwdY[playerDir];
    if (!isWallAt(nextX, nextY)) {
      playerX = nextX;
      playerY = nextY;
    }
  }

  void turnLeft()   { playerDir = (playerDir + 3) % 4; }
  void turnRight()  { playerDir = (playerDir + 1) % 4; }
  void turnAround() { playerDir = (playerDir + 2) % 4; }

  void update(const GameInputState& input) {
    static bool buttonHeld = false;

    if (input.up) {
      if (!buttonHeld) { moveForward(); buttonHeld = true; }
    } else if (input.left) {
      if (!buttonHeld) { turnLeft(); buttonHeld = true; }
    } else if (input.right) {
      if (!buttonHeld) { turnRight(); buttonHeld = true; }
    } else if (input.down) {
      if (!buttonHeld) { turnAround(); buttonHeld = true; }
    } else {
      buttonHeld = false;
    }
  }

  void draw(MatrixPanel_I2S_DMA& display) {
    display.fillScreen(0);
    uint16_t color = display.color565(0, 255, 0); // Red wireframe

    int d;
    bool drawing = true;

    for (d = 0; d < DRAW_DISTANCE && drawing; ++d) {
      int currX = playerX + (fwdX[playerDir] * d);
      int currY = playerY + (fwdY[playerDir] * d);

      int nextX = playerX + (fwdX[playerDir] * (d + 1));
      int nextY = playerY + (fwdY[playerDir] * (d + 1));

      // Calculate coordinates of blocks to the immediate left and right
      int leftX  = currX - rightX[playerDir];
      int leftY  = currY - rightY[playerDir];
      int rightX_pos = currX + rightX[playerDir];
      int rightY_pos = currY + rightY[playerDir];

      // Calculate coordinates of blocks to the left and right ONE STEP AHEAD
      int nextLeftX  = nextX - rightX[playerDir];
      int nextLeftY  = nextY - rightY[playerDir];
      int nextRightX = nextX + rightX[playerDir];
      int nextRightY = nextY + rightY[playerDir];

      bool isLeftSolid = isWallAt(leftX, leftY);
      bool isNextLeftSolid = isWallAt(nextLeftX, nextLeftY);
      bool isRightSolid = isWallAt(rightX_pos, rightY_pos);
      bool isNextRightSolid = isWallAt(nextRightX, nextRightY);

      int boxCurr = depthInset[d];
      int boxNext = depthInset[d + 1];
      int boxHeight = (63 - boxNext) - boxNext + 1; // Height of the vertical seam at depth d+1

      // ==========================================
      // LEFT SIDE RENDERING
      // ==========================================
      if (isLeftSolid) {
        // Draw diagonal perspective lines for a solid wall
        display.drawLine(boxCurr, boxCurr, boxNext, boxNext, color);
        display.drawLine(boxCurr, 63 - boxCurr, boxNext, 63 - boxNext, color);
        
        // If the wall ends after this depth (meaning a turn is coming up), drop a vertical seam
        if (!isNextLeftSolid) {
          display.drawFastVLine(boxNext, boxNext, boxHeight, color);
        }
      } else {
        // LEFT IS OPEN (Turn / Passage)
        // Near corner vertical seam
        display.drawFastVLine(boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        
        // Horizontal recessed lines (Ceiling and Floor of the side passage)
        display.drawLine(boxCurr, boxNext, boxNext, boxNext, color);
        display.drawLine(boxCurr, 63 - boxNext, boxNext, 63 - boxNext, color);
        
        // Far corner vertical seam (The back wall of the turn)
        display.drawFastVLine(boxNext, boxNext, boxHeight, color);
      }

      // ==========================================
      // RIGHT SIDE RENDERING
      // ==========================================
      if (isRightSolid) {
        // Draw diagonal perspective lines for a solid wall
        display.drawLine(63 - boxCurr, boxCurr, 63 - boxNext, boxNext, color);
        display.drawLine(63 - boxCurr, 63 - boxCurr, 63 - boxNext, 63 - boxNext, color);
        
        // If the wall ends after this depth, drop a vertical seam
        if (!isNextRightSolid) {
          display.drawFastVLine(63 - boxNext, boxNext, boxHeight, color);
        }
      } else {
        // RIGHT IS OPEN (Turn / Passage)
        // Near corner vertical seam
        display.drawFastVLine(63 - boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        
        // Horizontal recessed lines (Ceiling and Floor of the side passage)
        display.drawLine(63 - boxCurr, boxNext, 63 - boxNext, boxNext, color);
        display.drawLine(63 - boxCurr, 63 - boxNext, 63 - boxNext, 63 - boxNext, color);
        
        // Far corner vertical seam (The back wall of the turn)
        display.drawFastVLine(63 - boxNext, boxNext, boxHeight, color);
      }

      // Stop rendering further depth if blocked by a wall directly ahead
      if (isWallAt(nextX, nextY)) {
        drawing = false;
      }
    }

    // ==========================================
    // BACK WALL RENDERING
    // ==========================================
    int box = depthInset[d];
    if (isWallAt(playerX + (fwdX[playerDir] * d), playerY + (fwdY[playerDir] * d))) {
      // Draw a solid flat square at the furthest visible depth
      display.drawRect(box, box, 64 - (box * 2), 64 - (box * 2), color);
    } else {
      // Open horizon - Draw horizontal horizon brackets
      display.drawFastHLine(box, box, 64 - (box * 2), color);
      display.drawFastHLine(box, 63 - box, 64 - (box * 2), color);
    }
  }
};

#endif // DUNGEON_H