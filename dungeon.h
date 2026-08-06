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

struct ParsedItem {
  String baseKey;
  int wallDir = -1;
  bool isOpen = false;
};

class Dungeon {
public:
  enum DungeonState {
    STATE_LOADING,
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

  DungeonState currentState = STATE_LOADING;

  int mapSizeX = 0;
  int mapSizeY = 0;
  int level = 0;
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

  

// Checks if a door exists on a wall, returning its color and open state by reference
  bool getDoorDataOnWall(int cellX, int cellY, int wallDir, uint16_t& outColor, bool& outIsOpen) {
    // 1. Check current cell
    if (cellY >= 0 && cellY < itemMap.size() && cellX >= 0 && cellX < itemMap[0].size()) {
      String rawKey = itemMap[cellY][cellX];
      rawKey.trim();
      if (rawKey.length() > 0 && rawKey != " ") {
        ParsedItem item = parseItemKey(rawKey);
        if (itemDefinitions.count(item.baseKey)) {
          ItemInfo info = itemDefinitions[item.baseKey];
          if (info.name.indexOf("Door") != -1 || item.baseKey.indexOf("Door") != -1) {
            if (item.wallDir == -1 || item.wallDir == wallDir) {
              outColor = parseHexColor(info.color.c_str());
              outIsOpen = item.isOpen;
              return true;
            }
          }
        }
      }
    }

    // 2. Check adjacent cell
    int adjX = cellX + fwdX[wallDir];
    int adjY = cellY + fwdY[wallDir];
    int oppDir = (wallDir + 2) % 4;

    if (adjY >= 0 && adjY < itemMap.size() && adjX >= 0 && adjX < itemMap[0].size()) {
      String rawKey = itemMap[adjY][adjX];
      rawKey.trim();
      if (rawKey.length() > 0 && rawKey != " ") {
        ParsedItem item = parseItemKey(rawKey);
        if (itemDefinitions.count(item.baseKey)) {
          ItemInfo info = itemDefinitions[item.baseKey];
          if (info.name.indexOf("Door") != -1 || item.baseKey.indexOf("Door") != -1) {
            if (item.wallDir == oppDir) {
              outColor = parseHexColor(info.color.c_str());
              outIsOpen = item.isOpen;
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  // Draws a door swung open into the room using two triangles to form a perspective trapezoid
  void drawOpenFrontDoor(MatrixPanel_I2S_DMA& display, int startX, int endX, int box, uint16_t color) {
    int minX = std::min(startX, endX);
    int maxX = std::max(startX, endX);
    int w = maxX - minX;
    int h = (63 - box) - box + 1;
    if (w <= 0 || h <= 0) return;

    int doorW = w / 3;
    int doorH = (h * 2) / 3;
    int doorX = minX + (w / 3);
    int doorY = (63 - box) - doorH + 1;

    // Swing offset creates the visual of the right edge pushing deeper into the room
    int swingOffset = doorH / 5; 
    int hingeX = doorX; 
    int swingX = doorX + doorW;
    
    // Triangle 1: Left Hinge Top, Left Hinge Bottom, Swung Edge Top
    display.fillTriangle(hingeX, doorY, hingeX, doorY + doorH - 1, swingX, doorY + swingOffset, color);
    // Triangle 2: Left Hinge Bottom, Swung Edge Bottom, Swung Edge Top
    display.fillTriangle(hingeX, doorY + doorH - 1, swingX, doorY + doorH - 1 - swingOffset, swingX, doorY + swingOffset, color);
  }

  // Draws a door on the left or right walls, factoring in linear perspective
  void drawSideDoor(MatrixPanel_I2S_DMA& display, int boxCurr, int boxNext, bool isLeft, uint16_t color) {
    int w = boxNext - boxCurr;
    if (w <= 0) return;

    if (isLeft) {
      int startX = boxCurr + w / 3;
      int endX = boxCurr + (w * 2) / 3;
      for (int x = startX; x <= endX; x++) {
        int yCeil = x;
        int yFloor = 63 - x;
        int h = yFloor - yCeil + 1;
        int doorH = (h * 2) / 3;
        int yDoorTop = yFloor - doorH + 1;
        display.drawFastVLine(x, yDoorTop, doorH, color);
      }
    } else {
      int startX = (63 - boxCurr) - w / 3;
      int endX = (63 - boxCurr) - (w * 2) / 3;
      for (int x = startX; x >= endX; x--) {
        int distFromRight = 63 - x;
        int yCeil = distFromRight;
        int yFloor = 63 - distFromRight; 
        int h = yFloor - yCeil + 1;
        int doorH = (h * 2) / 3;
        int yDoorTop = yFloor - doorH + 1;
        display.drawFastVLine(x, yDoorTop, doorH, color);
      }
    }
  }

  // Draws a flat, front-facing door (for front walls and lateral scans)
  void drawFrontDoor(MatrixPanel_I2S_DMA& display, int startX, int endX, int box, uint16_t color) {
    int minX = std::min(startX, endX);
    int maxX = std::max(startX, endX);
    int w = maxX - minX;
    int h = (63 - box) - box + 1;
    if (w <= 0 || h <= 0) return;

    int doorW = w / 3;
    int doorH = (h * 2) / 3;
    int doorX = minX + (w / 3);
    int doorY = (63 - box) - doorH + 1; // Rests on the floor

    if (doorW > 0 && doorH > 0) {
      display.fillRect(doorX, doorY, doorW, doorH, color);
    }
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

ParsedItem parseItemKey(const String& rawKey) {
  ParsedItem result;
  int firstColon = rawKey.indexOf(':');
  
  if (firstColon != -1) {
    result.baseKey = rawKey.substring(0, firstColon);
    result.baseKey.trim();
    
    int secondColon = rawKey.indexOf(':', firstColon + 1);
    if (secondColon != -1) {
      result.wallDir = rawKey.substring(firstColon + 1, secondColon).toInt();
      String state = rawKey.substring(secondColon + 1);
      if (state == "O") result.isOpen = true;
    } else {
      result.wallDir = rawKey.substring(firstColon + 1).toInt();
    }
  } else {
    result.baseKey = rawKey;
    result.baseKey.trim();
  }
  return result;
}

void checkForItemAtPlayerPos() {
  Serial.printf("Player on level %d at position:%d,%d facing %d\n", level,playerX,playerY,playerDir);
  activeMessage = "";  
  if (playerY >= 0 && playerY < itemMap.size() && playerX >= 0 && playerX < itemMap[0].size()) {
    String rawKey = itemMap[playerY][playerX];
    rawKey.trim();
    
    if (rawKey.length() > 0 && rawKey != " ") {
      ParsedItem parsed = parseItemKey(rawKey);
      
      if (itemDefinitions.count(parsed.baseKey)) {
        ItemInfo info = itemDefinitions[parsed.baseKey];
        activeMessage = info.name;
      } else {
        activeMessage = parsed.baseKey;
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
    level = 1;
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

      if (doc["playerX"].is<int>()) {
        playerX = doc["playerX"];
      }
      if (doc["playerY"].is<int>()) {
        playerY = doc["playerY"];
      }
      if (doc["playerDir"].is<int>()) {
        playerDir = doc["playerDir"];
      }
      if (doc["level"].is<int>()) {
        level = doc["level"];
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

      Serial.printf("Level %d Map successfully updated: %dx%d\n Player Position:%d,%d\n", level, mapSizeX, mapSizeY,playerX,playerY);
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
    currentState = STATE_LOADING;
    playerInventory.clear();
  }

  bool hasKeyForDoor(const ItemInfo& doorInfo) {
  for (auto it = playerInventory.begin(); it != playerInventory.end(); ++it) {
    // Check if the inventory item is a key
    bool isKey = it->name.indexOf("Key") != -1;
    
    // Match by color or by transforming "Door" to "Key" in the name
    bool colorMatch = (it->color == doorInfo.color);
    
    String expectedKeyName = doorInfo.name;
    expectedKeyName.replace("Door", "Key");
    bool nameMatch = (it->name == expectedKeyName);

    if (isKey && (colorMatch || nameMatch)) {
      // Optional: Remove the key from inventory once used (single-use key)
      // playerInventory.erase(it);
      return true;
    }
  }
  return false;
}

  bool isDoorBlocking(int x, int y, int facingDir) {
  if (y >= 0 && y < itemMap.size() && x >= 0 && x < itemMap[0].size()) {
    ParsedItem parsed = parseItemKey(itemMap[y][x]);
    // Check if it's a door and if its designated wall matches the direction you are facing/entering from
    if (parsed.wallDir != -1 && parsed.baseKey.indexOf("Door") != -1) {
      if (parsed.wallDir == facingDir) {
        return true; // Door is blocking this side
      }
    }
  }
  return false;
}

void moveForward() {
  int nextX = playerX + fwdX[playerDir];
  int nextY = playerY + fwdY[playerDir];

  uint16_t doorColor;
  bool isDoorOpen = false;
  bool hasDoor = getDoorDataOnWall(playerX, playerY, playerDir, doorColor, isDoorOpen);

  // If the door is open, clear the wall tile in the map array so player can walk through
  if (hasDoor && isDoorOpen) {
    if (nextY >= 0 && nextY < mapSizeY && nextX >= 0 && nextX < mapSizeX) {
      map[nextY][nextX] = 0;
    }
  }

  // Move forward if target is not a solid wall and not a closed door
  if (!isWallAt(nextX, nextY) && !(hasDoor && !isDoorOpen)) {
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

    if (currentState == STATE_LOADING) {
      char url[160];
      snprintf(url, sizeof(url), 
                 "https://rgbop.com/api/dungeon/map/%d", 
                 level);
      if (loadMap(url)) {
        checkForItemAtPlayerPos();
        currentState = STATE_PLAYING;
      }
    }

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

// Handle 'btnA' button press to interact with doors OR collect items
      if (input.btnA) {
        if (!aButtonHeld) {
          bool actionDone = false;
          int nextX = playerX + fwdX[playerDir];
          int nextY = playerY + fwdY[playerDir];
          int oppDir = (playerDir + 2) % 4;

          // Helper lambda to attempt unlocking a door on a specific cell
auto tryOpenDoor = [&](int tx, int ty, int tdir) -> bool {
  if (ty >= 0 && ty < itemMap.size() && tx >= 0 && tx < itemMap[0].size()) {
    String rawKey = itemMap[ty][tx];
    ParsedItem item = parseItemKey(rawKey);
    
    if (item.wallDir == tdir || item.wallDir == -1) {
      if (!item.isOpen && itemDefinitions.count(item.baseKey)) {
        ItemInfo info = itemDefinitions[item.baseKey];
        
        if (info.name.indexOf("Door") != -1 || item.baseKey.indexOf("Door") != -1) {
          if (hasKeyForDoor(info)) {
            // Mark door as Open
            itemMap[ty][tx] = item.baseKey + ":" + String(item.wallDir) + ":O";
            activeMessage = "Door unlocked!";

            // Clear the wall collision at the doorway target tile
            int doorTargetX = playerX + fwdX[playerDir];
            int doorTargetY = playerY + fwdY[playerDir];
            if (doorTargetY >= 0 && doorTargetY < mapSizeY && doorTargetX >= 0 && doorTargetX < mapSizeX) {
              map[doorTargetY][doorTargetX] = 0;
            }
          } else {
            activeMessage = "Locked!";
          }
          return true;
        }
      }
    }
  }
  return false;
};

          // 1. Try opening door on current tile facing player
          if (tryOpenDoor(playerX, playerY, playerDir)) {
            actionDone = true;
          } 
          // 2. Try opening door on adjacent tile facing player
          else if (tryOpenDoor(nextX, nextY, oppDir)) {
            actionDone = true;
          }

          // 3. If no door was blocking us, check our current tile for items or stairs
          if (!actionDone) {
            if (playerY >= 0 && playerY < itemMap.size() && playerX >= 0 && playerX < itemMap[0].size()) {
              String itemKey = itemMap[playerY][playerX];
              itemKey.trim();
              if (itemKey.length() > 0 && itemKey != " ") {
                ParsedItem currentItem = parseItemKey(itemKey);
                
                if (itemDefinitions.count(currentItem.baseKey)) {
                  ItemInfo info = itemDefinitions[currentItem.baseKey];
                  
                  // Check if player is pressing A on Stairs Down
                  if (info.name.indexOf("Stairs") != -1 && info.name.indexOf("Down") != -1) {
                    level++;                    // Increment the level counter
                    currentState = STATE_LOADING; // Trigger the loading state machine
                    activeMessage = "";         
                    Serial.printf("Descending to Level %d\n", level);
                  }
                  // Otherwise handle normal collectibles
                  else if (info.name.indexOf("Door") == -1 && currentItem.baseKey.indexOf("Door") == -1) {
                    if (info.collectible) {
                      playerInventory.push_back(info);
                      itemMap[playerY][playerX] = " ";
                      activeMessage = "";  
                      Serial.printf("Collected: %s\n", info.name.c_str());
                    }
                  }
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

  // Prevent rendering default map/position while waiting for network load
  if (currentState == STATE_LOADING) {
    display.setFont(&TomThumb);
    display.setTextSize(1);
    display.setTextColor(display.color565(255, 255, 255));
    display.setCursor(2, 32);
    display.print("Loading...");
    display.flipDMABuffer();
    return;
  }

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
          if (item.name == "Key" || item.name.indexOf("Key") != -1) {
            drawKeySprite(display, 2, yPos + 1 - 5, parseHexColor(item.color.c_str()));
            display.setCursor(13, yPos + 1);
          } else {
            display.setCursor(2, yPos);
          }
          display.print(item.name);
          yPos += 10;
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

    int leftDir = (playerDir + 3) % 4;
    int rightDir = (playerDir + 1) % 4;

    int prevX = playerX - fwdX[playerDir];
    int prevY = playerY - fwdY[playerDir];
    
    uint16_t c; bool o;
    bool isPrevLeftSolid = isWallAt(prevX - rightX[playerDir], prevY - rightY[playerDir]) || (getDoorDataOnWall(prevX, prevY, leftDir, c, o) && !o);
    bool isPrevRightSolid = isWallAt(prevX + rightX[playerDir], prevY + rightY[playerDir]) || (getDoorDataOnWall(prevX, prevY, rightDir, c, o) && !o);

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

      uint16_t cL, nL, cR, nR, fC;
      bool oL, noL, oR, noR, fO;
      
      bool hasCurrLeft = getDoorDataOnWall(currX, currY, leftDir, cL, oL);
      bool hasNextLeft = getDoorDataOnWall(nextX, nextY, leftDir, nL, noL);
      bool hasCurrRight = getDoorDataOnWall(currX, currY, rightDir, cR, oR);
      bool hasNextRight = getDoorDataOnWall(nextX, nextY, rightDir, nR, noR);
      bool hasFront = getDoorDataOnWall(currX, currY, playerDir, fC, fO);

      bool isLeftSolid = isWallAt(leftX, leftY) || (hasCurrLeft && !oL);
      bool isNextLeftSolid = isWallAt(nextLeftX, nextLeftY) || (hasNextLeft && !noL);
      bool isRightSolid = isWallAt(rightX_pos, rightY_pos) || (hasCurrRight && !oR);
      bool isNextRightSolid = isWallAt(nextRightX, nextRightY) || (hasNextRight && !noR);

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
        if (!isNextLeftSolid) display.drawFastVLine(boxNext, boxNext, boxHeight, color);
        if (!isPrevLeftSolid) display.drawFastVLine(boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        
        if (hasCurrLeft && !oL) drawSideDoor(display, boxCurr, boxNext, true, cL);
      } else {
        if (isPrevLeftSolid && isNextLeftSolid) {
          display.drawLine(boxCurr, boxNext, boxNext, boxNext, color);
          display.drawLine(boxCurr, 63 - boxNext, boxNext, 63 - boxNext, color);
        }
        if (isNextLeftSolid) display.drawFastVLine(boxNext, boxNext, boxHeight, color);
        if (isPrevLeftSolid) display.drawFastVLine(boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
      }

      // Right side rendering
      if (isRightSolid) {
        display.drawLine(63 - boxCurr, boxCurr, 63 - boxNext, boxNext, color);
        display.drawLine(63 - boxCurr, 63 - boxCurr, 63 - boxNext, 63 - boxNext, color);
        if (!isNextRightSolid) display.drawFastVLine(63 - boxNext, boxNext, boxHeight, color);
        if (!isPrevRightSolid) display.drawFastVLine(63 - boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
        
        if (hasCurrRight && !oR) drawSideDoor(display, boxCurr, boxNext, false, cR);
      } else {
        if (isPrevRightSolid && isNextRightSolid) {
          display.drawLine(63 - boxCurr, boxNext, 63 - boxNext, boxNext, color);
          display.drawLine(63 - boxCurr, 63 - boxNext, 63 - boxNext, 63 - boxNext, color);
        }
        if (isNextRightSolid) display.drawFastVLine(63 - boxNext, boxNext, boxHeight, color);
        if (isPrevRightSolid) display.drawFastVLine(63 - boxCurr, boxCurr, (63 - boxCurr) - boxCurr + 1, color);
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

          uint16_t latFrontColor; bool latFrontOpen;
          bool hasLatFront = getDoorDataOnWall(cx, cy, playerDir, latFrontColor, latFrontOpen);
          
          if (isWallAt(nx, ny) || (hasLatFront && !latFrontOpen)) {
            display.drawLine(startX, boxNext, endX, boxNext, color);
            display.drawLine(startX, 63 - boxNext, endX, 63 - boxNext, color);

            int nnx = nx - rightX[playerDir];
            int nny = ny - rightY[playerDir];
            bool isAdjacentSolid = isWallAt(nnx, nny) || (getDoorDataOnWall(nx, ny, leftDir, c, o) && !o);
            
            if (!isAdjacentSolid) display.drawFastVLine(endX, boxNext, boxHeight, color);
            
            if (hasLatFront) {
              if (latFrontOpen) drawOpenFrontDoor(display, startX, endX, boxNext, latFrontColor);
              else drawFrontDoor(display, startX, endX, boxNext, latFrontColor);
            }
          } else if (hasLatFront && latFrontOpen) {
            // Draw open doors even if the wall behind them isn't solid
            drawOpenFrontDoor(display, startX, endX, boxNext, latFrontColor);
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

          uint16_t latFrontColor; bool latFrontOpen;
          bool hasLatFront = getDoorDataOnWall(cx, cy, playerDir, latFrontColor, latFrontOpen);

          if (isWallAt(nx, ny) || (hasLatFront && !latFrontOpen)) {
            display.drawLine(startX, boxNext, endX, boxNext, color);
            display.drawLine(startX, 63 - boxNext, endX, 63 - boxNext, color);

            int nnx = nx + rightX[playerDir];
            int nny = ny + rightY[playerDir];
            bool isAdjacentSolid = isWallAt(nnx, nny) || (getDoorDataOnWall(nx, ny, rightDir, c, o) && !o);
            
            if (!isAdjacentSolid) display.drawFastVLine(endX, boxNext, boxHeight, color);
            
            if (hasLatFront) {
              if (latFrontOpen) drawOpenFrontDoor(display, startX, endX, boxNext, latFrontColor);
              else drawFrontDoor(display, startX, endX, boxNext, latFrontColor);
            }
          } else if (hasLatFront && latFrontOpen) {
            drawOpenFrontDoor(display, startX, endX, boxNext, latFrontColor);
          }
        }
      }

      // Stop rendering further into depth if a wall or closed door is blocking
      if (isWallAt(nextX, nextY) || (hasFront && !fO)) {
        drawing = false;
      } else if (hasFront && fO) {
        // Draw the open door on the current plane before looping to the next tile
        drawOpenFrontDoor(display, boxNext, 63 - boxNext, boxNext, fC);
      }

      isPrevLeftSolid = isLeftSolid;
      isPrevRightSolid = isRightSolid;
    }

    // Back wall rendering
    int box = depthInset[d];
    int checkX = playerX + (fwdX[playerDir] * (d - 1));
    int checkY = playerY + (fwdY[playerDir] * (d - 1));
    int frontWallX = checkX + fwdX[playerDir];
    int frontWallY = checkY + fwdY[playerDir];

    uint16_t frontDoorBackC; bool frontDoorBackO;
    bool hasFrontDoorBack = getDoorDataOnWall(checkX, checkY, playerDir, frontDoorBackC, frontDoorBackO);
    
    if (isWallAt(frontWallX, frontWallY) || (hasFrontDoorBack && !frontDoorBackO)) {
      display.drawRect(box, box, 64 - (box * 2), 64 - (box * 2), color);
      if (hasFrontDoorBack) {
        if (frontDoorBackO) drawOpenFrontDoor(display, box, 63 - box, box, frontDoorBackC);
        else drawFrontDoor(display, box, 63 - box, box, frontDoorBackC);
      }
    } else if (hasFrontDoorBack && frontDoorBackO) {
      drawOpenFrontDoor(display, box, 63 - box, box, frontDoorBackC);
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
      
      // Parse out the base key to match inventory properly
      ParsedItem parsedCurrent = parseItemKey(itemKey);

      if (itemDefinitions.count(parsedCurrent.baseKey)) {
        ItemInfo info = itemDefinitions[parsedCurrent.baseKey];
        colorStr = info.color;

        if (info.name.indexOf("Key") != -1) {
          hasSprite = true;
        } else if (info.name.indexOf("Stairs") != -1 && info.name.indexOf("Down") != -1) {
          hasSprite = true;
          isStairsDown = true;
        }
      }

      if (hasSprite) {
        if (isStairsDown) {
          drawStairDownSprite(display, 27, 56);
        } else {
          drawKeySprite(display, 27, 57, parseHexColor(colorStr.c_str()));
        }
      } else {
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