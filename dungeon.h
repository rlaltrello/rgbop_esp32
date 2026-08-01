#ifndef DUNGEON_H
#define DUNGEON_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <Fonts/TomThumb.h>

#define DRAW_DISTANCE 3

struct ItemInfo {
  String name;
  String color;
  bool collectible;
};

class Dungeon {
public:
  enum DungeonState {
    STATE_PLAYING,
    STATE_INVENTORY
  };

private:
  int playerX;
  int playerY;
  int playerDir;  // 0: N, 1: E, 2: S, 3: W

  std::vector<std::vector<uint8_t>> map;
  std::vector<std::vector<String>> itemMap;
  std::map<String, ItemInfo> itemDefinitions;
  std::vector<ItemInfo> playerInventory;

  DungeonState currentState = STATE_PLAYING;

  int mapSizeX = 0;
  int mapSizeY = 0;
  uint16_t wallColor = 0x07E0;  // Default green (RGB565)

  String activeMessage = "";

  const int depthInset[DRAW_DISTANCE + 1] = { 0, 14, 22, 27 };

  const int fwdX[4] = { 0, 1, 0, -1 };
  const int fwdY[4] = { -1, 0, 1, 0 };
  const int rightX[4] = { 1, 0, -1, 0 };
  const int rightY[4] = { 0, 1, 0, -1 };

  bool isWallAt(int x, int y) {
    if (x < 0 || x >= mapSizeX || y < 0 || y >= mapSizeY) return true;
    return map[y][x] == 1;
  }

  uint16_t parseHexColor(const char* hexStr) {
    if (!hexStr) return 0x07E0;
    if (hexStr[0] == '#') hexStr++;
    long number = strtol(hexStr, NULL, 16);
    uint8_t r = (number >> 16) & 0xFF;
    uint8_t g = (number >> 8) & 0xFF;
    uint8_t b = number & 0xFF;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  void checkForItemAtPlayerPos() {
    activeMessage = "";  // Clear text when leaving the previous tile
    if (playerY >= 0 && playerY < itemMap.size() && playerX >= 0 && playerX < itemMap[0].size()) {
      String itemKey = itemMap[playerY][playerX];
      itemKey.trim();
      if (itemKey.length() > 0 && itemKey != " ") {
        if (itemDefinitions.count(itemKey)) {
          ItemInfo info = itemDefinitions[itemKey];
          activeMessage = info.name;
        } else {
          activeMessage = itemKey;
        }
      }
    }
  }

public:
  Dungeon(int startX = 1, int startY = 1, int startDir = 0)
    : playerX(startX), playerY(startY), playerDir(startDir) {

    map = {
      { 1, 1, 1, 1, 1, 1, 1, 1 },
      { 1, 0, 0, 0, 0, 0, 0, 1 },
      { 1, 0, 1, 1, 1, 1, 0, 1 },
      { 1, 0, 1, 1, 0, 0, 0, 1 },
      { 1, 0, 1, 1, 0, 1, 1, 1 },
      { 1, 0, 1, 0, 0, 1, 1, 1 },
      { 1, 0, 1, 1, 0, 1, 1, 1 },
      { 1, 1, 1, 1, 1, 1, 1, 1 }
    };
    mapSizeX = 8;
    mapSizeY = 8;
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

      if (doc["color"].is<const char*>()) {
        wallColor = parseHexColor(doc["color"]);
      }

      // Parse items metadata dictionary
      JsonObject jitems = doc["items"].as<JsonObject>();
      itemDefinitions.clear();
      for (JsonPair kv : jitems) {
        String key = kv.key().c_str();
        JsonObject itemObj = kv.value().as<JsonObject>();
        ItemInfo info;
        info.name = itemObj["name"].as<String>();
        info.color = itemObj["color"].as<String>();
        info.collectible = itemObj["collectible"].as<bool>();
        itemDefinitions[key] = info;
      }

      // Parse map array
      JsonArray jmap = doc["map"].as<JsonArray>();
      if (!jmap.isNull() && jmap.size() > 0) {
        int rows = jmap.size();
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

        map = tempMap;
        mapSizeY = rows;
        mapSizeX = cols;
      }

      // Parse itemMap array
      JsonArray jitemMap = doc["itemMap"].as<JsonArray>();
      if (!jitemMap.isNull() && jitemMap.size() > 0) {
        int rows = jitemMap.size();
        int cols = jitemMap[0].as<JsonArray>().size();

        std::vector<std::vector<String>> tempItemMap;
        tempItemMap.resize(rows);

        for (int i = 0; i < rows; i++) {
          tempItemMap[i].resize(cols);
          JsonArray rowArray = jitemMap[i].as<JsonArray>();
          for (int j = 0; j < cols; j++) {
            tempItemMap[i][j] = rowArray[j].as<String>();
          }
        }

        itemMap = tempItemMap;
      }

      Serial.printf("Map successfully updated: %dx%d\n", mapSizeX, mapSizeY);
      http.end();
      return true;
    } else {
      Serial.print("HTTP GET Error code: ");
      Serial.println(httpResponseCode);
    }

    http.end();
    return false;
  }

  void reset() {
    playerX = 1;
    playerY = 1;
    playerDir = 0;
    activeMessage = "";
    currentState = STATE_PLAYING;
    playerInventory.clear();
    checkForItemAtPlayerPos();
  }

  void moveForward() {
    int nextX = playerX + fwdX[playerDir];
    int nextY = playerY + fwdY[playerDir];
    if (!isWallAt(nextX, nextY)) {
      playerX = nextX;
      playerY = nextY;
      checkForItemAtPlayerPos();
    }
  }

  void turnLeft() {
    playerDir = (playerDir + 3) % 4;
  }
  void turnRight() {
    playerDir = (playerDir + 1) % 4;
  }
  void turnAround() {
    playerDir = (playerDir + 2) % 4;
  }

  void update(const GameInputState& input) {
    static bool buttonHeld = false;
    static bool aButtonHeld = false;
    static bool bButtonHeld = false;
    static bool anyButtonHeld = false;

    if (currentState == STATE_PLAYING) {
      if (input.up) {
        if (!buttonHeld) {
          moveForward();
          buttonHeld = true;
        }
      } else if (input.left) {
        if (!buttonHeld) {
          turnLeft();
          buttonHeld = true;
        }
      } else if (input.right) {
        if (!buttonHeld) {
          turnRight();
          buttonHeld = true;
        }
      } else if (input.down) {
        if (!buttonHeld) {
          turnAround();
          buttonHeld = true;
        }
      } else {
        buttonHeld = false;
      }

      // Handle 'btnA' button press to collect collectible items
      if (input.btnA) {
        if (!aButtonHeld) {
          if (playerY >= 0 && playerY < itemMap.size() && playerX >= 0 && playerX < itemMap[0].size()) {
            String itemKey = itemMap[playerY][playerX];
            itemKey.trim();
            if (itemKey.length() > 0 && itemKey != " ") {
              if (itemDefinitions.count(itemKey)) {
                ItemInfo info = itemDefinitions[itemKey];
                if (info.collectible) {
                  playerInventory.push_back(info);
                  itemMap[playerY][playerX] = " ";
                  activeMessage = "";  // Clear text when collected
                  Serial.printf("Collected: %s\n", info.name.c_str());
                }
              }
            }
          }
          aButtonHeld = true;
        }
      } else {
        aButtonHeld = false;
      }

      // Handle 'btnB' button press to open inventory
      if (input.btnB) {
        if (!bButtonHeld) {
          currentState = STATE_INVENTORY;
          bButtonHeld = true;
          anyButtonHeld = true;  // Prevent immediate re-trigger on exit
        }
      } else {
        bButtonHeld = false;
      }

    } else if (currentState == STATE_INVENTORY) {
      // Check if any button is pressed to return to the game
      bool anyPressed = input.up || input.down || input.left || input.right || input.btnA || input.btnB;
      if (anyPressed) {
        if (!anyButtonHeld) {
          currentState = STATE_PLAYING;
          anyButtonHeld = true;
        }
      } else {
        anyButtonHeld = false;
      }
    }
  }

  void drawKeySprite(MatrixPanel_I2S_DMA& display, int startX, int startY, uint16_t color) {
    // 9 columns x 5 rows pixel art representation of a key
    const char* keySprite[5] = {
      "011000000",
      "100100000",
      "101111111",
      "100100010",
      "011000000"
    };

    int rows = 5;
    int cols = 9;

    for (int y = 0; y < rows; y++) {
      for (int x = 0; x < cols; x++) {
        if (keySprite[y][x] == '1') {
          display.drawPixel(startX + x, startY + y, color);
        }
      }
    }
  }

  void drawStairDownSprite(MatrixPanel_I2S_DMA& display, int startX, int startY) {
    // 6 columns x 3 rows pixel art representation of a Stairs going down
    const char* keySprite[6] = {
      "MMMM0000000",
      "BBLLLL00000",
      "BBBMMMMM000",
      "BBBBLLLLL00",
      "BBBBBMMMMMM",
      "BBBBBBBBBBB"
    };

    int rows = 6;
    int cols = 11;

    for (int y = 0; y < rows; y++) {
      for (int x = 0; x < cols; x++) {
        if (keySprite[y][x] == 'B') {
          display.drawPixel(startX + x, startY + y, parseHexColor("#555555"));
        }
        if (keySprite[y][x] == 'L') {
          display.drawPixel(startX + x, startY + y, parseHexColor("#CCCCCC"));
        }
        if (keySprite[y][x] == 'M') {
          display.drawPixel(startX + x, startY + y, parseHexColor("#888888"));
        }
      }
    }
  }

  void draw(MatrixPanel_I2S_DMA& display) {
    display.fillScreen(0);

    // Handle inventory screen rendering
    if (currentState == STATE_INVENTORY) {
      display.setFont(&TomThumb);
      display.setTextSize(1);
      display.setTextColor(display.color565(255, 255, 255));

      display.setCursor(2, 6);
      display.print("INVENTORY");

      int yPos = 16;
      if (playerInventory.empty()) {
        display.setCursor(2, yPos);
        display.print("Empty");
      } else {
        for (const auto& item : playerInventory) {
          // If the item is a key, draw the sprite icon next to it
          if (item.name == "Key" || item.name.indexOf("Key") != -1) {
            drawKeySprite(display, 2, yPos + 1 - 5, parseHexColor(item.color.c_str()));  // key icon
            display.setCursor(13, yPos + 1);                                             // Offset text past the 9px wide icon
          } else {
            display.setCursor(2, yPos);
          }

          display.print(item.name);
          yPos += 10;  // Give a bit more vertical spacing for icons
          if (yPos > 60) break;
        }
      }
      display.flipDMABuffer();
      return;
    }

    // Handle 3D Dungeon view rendering
    uint16_t color = wallColor;

    int d;
    bool drawing = true;

    int prevX = playerX - fwdX[playerDir];
    int prevY = playerY - fwdY[playerDir];
    bool isPrevLeftSolid = isWallAt(prevX - rightX[playerDir], prevY - rightY[playerDir]);
    bool isPrevRightSolid = isWallAt(prevX + rightX[playerDir], prevY + rightY[playerDir]);

    bool leftOccluded = false;
    bool rightOccluded = false;

    for (d = 0; d < DRAW_DISTANCE && drawing; ++d) {
      int currX = playerX + (fwdX[playerDir] * d);
      int currY = playerY + (fwdY[playerDir] * d);

      int nextX = playerX + (fwdX[playerDir] * (d + 1));
      int nextY = playerY + (fwdY[playerDir] * (d + 1));

      int leftX = currX - rightX[playerDir];
      int leftY = currY - rightY[playerDir];
      int rightX_pos = currX + rightX[playerDir];
      int rightY_pos = currY + rightY[playerDir];

      int nextLeftX = nextX - rightX[playerDir];
      int nextLeftY = nextY - rightY[playerDir];
      int nextRightX = nextX + rightX[playerDir];
      int nextRightY = nextY + rightY[playerDir];

      bool isLeftSolid = isWallAt(leftX, leftY);
      bool isNextLeftSolid = isWallAt(nextLeftX, nextLeftY);
      bool isRightSolid = isWallAt(rightX_pos, rightY_pos);
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
          int endX = boxNext - k * tileWidth;
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
          int endX = (63 - boxNext) + k * tileWidth;
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

      isPrevLeftSolid = isLeftSolid;
      isPrevRightSolid = isRightSolid;
    }

    // Back wall rendering
    int box = depthInset[d];
    if (isWallAt(playerX + (fwdX[playerDir] * d), playerY + (fwdY[playerDir] * d))) {
      display.drawRect(box, box, 64 - (box * 2), 64 - (box * 2), color);
    }

    // Render active item message or sprite
    if (activeMessage.length() > 0) {
      String itemKey = "";
      if (playerY >= 0 && playerY < itemMap.size() && playerX >= 0 && playerX < itemMap[0].size()) {
        itemKey = itemMap[playerY][playerX];
        itemKey.trim();
      }

      bool hasSprite = false;
      bool isStairsDown = false;
      String colorStr = "#ffffff";

      if (itemDefinitions.count(itemKey)) {
        ItemInfo info = itemDefinitions[itemKey];
        colorStr = info.color;

        if (info.name.indexOf("Key") != -1) {
          hasSprite = true;
        } else if (info.name.indexOf("Stairs") != -1 && info.name.indexOf("Down") != -1) {
          hasSprite = true;
          isStairsDown = true;
        }
      }

      if (hasSprite) {
        // Center the 9x5 sprite horizontally: (64 - 9) / 2 = 27
        // Position vertically at y = 57
        if (isStairsDown) {
          // Make sure you have your drawStairSpriteDown function ready, or change this to match your function name
          drawStairDownSprite(display, 27, 56);
        } else {
          drawKeySprite(display, 27, 57, parseHexColor(colorStr.c_str()));
        }
      } else {
        // Fallback to text popup for standard items
        display.setFont(&TomThumb);
        display.setTextSize(1);
        display.setTextColor(display.color565(255, 255, 255));
        display.setCursor(2, 60);
        display.print(activeMessage);
      }
    }
    display.flipDMABuffer();
  }
};

#endif  // DUNGEON_H