#ifndef DUNGEON_H
#define DUNGEON_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>

#define DRAW_DISTANCE 3

class Dungeon {
private:
  int playerX;
  int playerY;
  int playerDir; // 0: N, 1: E, 2: S, 3: W

  std::vector<std::vector<uint8_t>> map;
  int mapSizeX = 0;
  int mapSizeY = 0;

  const int depthInset[DRAW_DISTANCE + 1] = { 0, 14, 22, 27 };

  const int fwdX[4]   = {  0,  1,  0, -1 };
  const int fwdY[4]   = { -1,  0,  1,  0 };
  const int rightX[4] = {  1,  0, -1,  0 };
  const int rightY[4] = {  0,  1,  0, -1 };

  bool isWallAt(int x, int y) {
    if (x < 0 || x >= mapSizeX || y < 0 || y >= mapSizeY) return true;
    return map[y][x] == 1;
  }

public:
  Dungeon(int startX = 4, int startY = 4, int startDir = 0)
    : playerX(startX), playerY(startY), playerDir(startDir) {
    
    // Default fallback map (9x9) if API load fails or isn't called yet
    map = {
      {1, 1, 1, 1, 1, 1, 1, 1, 1},
      {1, 0, 0, 0, 0, 0, 0, 0, 1},
      {1, 0, 1, 0, 1, 1, 1, 0, 1},
      {1, 0, 0, 0, 1, 0, 0, 0, 1},
      {1, 0, 1, 1, 1, 0, 1, 1, 1},
      {1, 0, 1, 0, 0, 0, 1, 1, 1},
      {1, 0, 1, 1, 0, 1, 1, 1, 1},
      {1, 1, 1, 1, 1, 1, 1, 1, 1},
      {1, 1, 1, 1, 1, 1, 1, 1, 1}
    };
    mapSizeX = 9;
    mapSizeY = 9;
  }

  bool loadMap(const char* url) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected. Skipping map load.");
      return false;
    }

    HTTPClient http;
    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Map API Response received.");

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.c_str());
        http.end();
        return false;
      }

      // Supports both a raw 2D array [[...]] or an object wrapper like {"map": [[...]]}
      JsonArray jmap;
      if (doc.is<JsonArray>()) {
        jmap = doc.as<JsonArray>();
      } else if (doc["map"].is<JsonArray>()) {
        jmap = doc["map"].as<JsonArray>();
      } else {
        Serial.println("Invalid JSON structure: No map array found.");
        http.end();
        return false;
      }

      int rows = jmap.size();
      if (rows > 0) {
        int cols = jmap[0].as<JsonArray>().size();
        
        std::vector<std::vector<uint8_t>> tempMap;
        tempMap.resize(rows);
        
        for (int i = 0; i < rows; i++) {
          tempMap[i].resize(cols);
          JsonArray rowArray = jmap[i].as<JsonArray>();
          for (int j = 0; j < cols; j++) {
            tempMap[i][j] = rowArray[j].as<uint8_t>();
          }
        }

        // Commit new map data and dimensions
        map = tempMap;
        mapSizeY = rows;
        mapSizeX = cols;

        Serial.printf("Map successfully updated: %dx%d\n", mapSizeX, mapSizeY);
        http.end();
        return true;
      }
    } else {
      Serial.print("HTTP GET Error code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
    return false;
  }

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
    uint16_t color = display.color565(0, 255, 0); // Green wireframe

    int d;
    bool drawing = true;

    int prevX = playerX - fwdX[playerDir];
    int prevY = playerY - fwdY[playerDir];
    bool isPrevLeftSolid  = isWallAt(prevX - rightX[playerDir], prevY - rightY[playerDir]);
    bool isPrevRightSolid = isWallAt(prevX + rightX[playerDir], prevY + rightY[playerDir]);

    bool leftOccluded = false;
    bool rightOccluded = false;

    for (d = 0; d < DRAW_DISTANCE && drawing; ++d) {
      int currX = playerX + (fwdX[playerDir] * d);
      int currY = playerY + (fwdY[playerDir] * d);

      int nextX = playerX + (fwdX[playerDir] * (d + 1));
      int nextY = playerY + (fwdY[playerDir] * (d + 1));

      int leftX      = currX - rightX[playerDir];
      int leftY      = currY - rightY[playerDir];
      int rightX_pos = currX + rightX[playerDir];
      int rightY_pos = currY + rightY[playerDir];

      int nextLeftX  = nextX - rightX[playerDir];
      int nextLeftY  = nextY - rightY[playerDir];
      int nextRightX = nextX + rightX[playerDir];
      int nextRightY = nextY + rightY[playerDir];

      bool isLeftSolid      = isWallAt(leftX, leftY);
      bool isNextLeftSolid  = isWallAt(nextLeftX, nextLeftY);
      bool isRightSolid     = isWallAt(rightX_pos, rightY_pos);
      bool isNextRightSolid = isWallAt(nextRightX, nextRightY);

      int boxCurr = depthInset[d];
      int boxNext = depthInset[d + 1];
      int boxHeight = (63 - boxNext) - boxNext + 1;
      int tileWidth = (63 - boxNext) - boxNext;

      if (isLeftSolid) leftOccluded = true;
      if (isRightSolid) rightOccluded = true;

      // Left side rendering
      if (isLeftSolid) {
        display.drawLine(boxCurr, boxCurr, boxNext, boxNext, color);
        display.drawLine(boxCurr, 63 - boxCurr, boxNext, 63 - boxNext, color);
        
        if (!isNextLeftSolid) {
          display.drawFastVLine(boxNext, boxNext, boxHeight, color);
        }
        if (!isPrevLeftSolid) {
          display.drawFastVLine(boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        }
      } else {
        if (isPrevLeftSolid && isNextLeftSolid) {
          display.drawLine(boxCurr, boxNext, boxNext, boxNext, color);
          display.drawLine(boxCurr, 63 - boxNext, boxNext, 63 - boxNext, color);
        }
        if (isNextLeftSolid) {
          display.drawFastVLine(boxNext, boxNext, boxHeight, color);
        }
        if (isPrevLeftSolid) {
          display.drawFastVLine(boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        }
      }

      // Right side rendering
      if (isRightSolid) {
        display.drawLine(63 - boxCurr, boxCurr, 63 - boxNext, boxNext, color);
        display.drawLine(63 - boxCurr, 63 - boxCurr, 63 - boxNext, 63 - boxNext, color);
        
        if (!isNextRightSolid) {
          display.drawFastVLine(63 - boxNext, boxNext, boxHeight, color);
        }
        if (!isPrevRightSolid) {
          display.drawFastVLine(63 - boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        }
      } else {
        if (isPrevRightSolid && isNextRightSolid) {
          display.drawLine(63 - boxCurr, boxNext, 63 - boxNext, boxNext, color);
          display.drawLine(63 - boxCurr, 63 - boxNext, 63 - boxNext, 63 - boxNext, color);
        }
        if (isNextRightSolid) {
          display.drawFastVLine(63 - boxNext, boxNext, boxHeight, color);
        }
        if (isPrevRightSolid) {
          display.drawFastVLine(63 - boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        }
      }

      // Lateral scan Left
      if (!leftOccluded) {
        for (int k = 1; k <= 3; ++k) {
          int startX = boxNext - (k - 1) * tileWidth;
          int endX   = boxNext - k * tileWidth;
          if (startX <= 0) break;

          int cx = currX - k * rightX[playerDir];
          int cy = currY - k * rightY[playerDir];
          int nx = nextX - k * rightX[playerDir];
          int ny = nextY - k * rightY[playerDir];

          if (isWallAt(cx, cy)) break;

          if (isWallAt(nx, ny)) {
            display.drawLine(startX, boxNext, endX, boxNext, color);
            display.drawLine(startX, 63 - boxNext, endX, 63 - boxNext, color);

            int nnx = nx - rightX[playerDir];
            int nny = ny - rightY[playerDir];
            if (!isWallAt(nnx, nny)) {
              display.drawFastVLine(endX, boxNext, boxHeight, color);
            }
          }
        }
      }

      // Lateral scan Right
      if (!rightOccluded) {
        for (int k = 1; k <= 3; ++k) {
          int startX = (63 - boxNext) + (k - 1) * tileWidth;
          int endX   = (63 - boxNext) + k * tileWidth;
          if (startX >= 63) break;

          int cx = currX + k * rightX[playerDir];
          int cy = currY + k * rightY[playerDir];
          int nx = nextX + k * rightX[playerDir];
          int ny = nextY + k * rightY[playerDir];

          if (isWallAt(cx, cy)) break;

          if (isWallAt(nx, ny)) {
            display.drawLine(startX, boxNext, endX, boxNext, color);
            display.drawLine(startX, 63 - boxNext, endX, 63 - boxNext, color);

            int nnx = nx + rightX[playerDir];
            int nny = ny + rightY[playerDir];
            if (!isWallAt(nnx, nny)) {
              display.drawFastVLine(endX, boxNext, boxHeight, color);
            }
          }
        }
      }

      if (isWallAt(nextX, nextY)) {
        drawing = false;
      }

      isPrevLeftSolid  = isLeftSolid;
      isPrevRightSolid = isRightSolid;
    }

    // Back wall rendering
    int box = depthInset[d];
    if (isWallAt(playerX + (fwdX[playerDir] * d), playerY + (fwdY[playerDir] * d))) {
      display.drawRect(box, box, 64 - (box * 2), 64 - (box * 2), color);
    } 
  }
};

#endif // DUNGEON_H