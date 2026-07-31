#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "MarioClock.h"
#include "morphClock.h"
#include "dateProgress.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "GameModeManager.h"

WebServer server(80);
GameModeManager gameManager(server);


#include <ArduinoJson.h>  // Make sure to install this library in Arduino IDE
#include "weather.h"
#include "textblast.h"
#include "marioFont.h"
#include "isslocation.h"
#include "planes.h"
#include "radar.h"
#include "earthquake.h"
#include "spotify.h"
#include "diags.h"
#include <WebServer.h>
#include "gifEngine.h"
#include "shooter.h"
#include "dungeon.h"
#include "webApi.h"
#include "logo.h"
#include "widgetEngine.h"
#include "configEngine.h"
#include "bleEngine.h"
#include "hardwareEngine.h"
#include "networkEngine.h"
#include <esp_mac.h>

#define SPIRAM_DMA_BUFFER 1
#include "FS.h"
#include <LittleFS.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <NimBLEDevice.h>

// --- RGBop BLE UUIDs ---
#define PROV_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID_SSID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_UUID_PASS "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_UUID_CMD "beb5483e-36e1-4688-b7f5-ea07361b26aa"



// --- DYNAMIC CREDENTIALS & STATE ---
String currentSSID = "";
String currentPASS = "";

//MatrixPanel_I2S_DMA dma_display;
MatrixPanel_I2S_DMA* dma_display = nullptr;

bool provisioningMode = false;
bool newCredentialsReceived = false;


String prefPanelName = "";
bool prefShowGifs = true;
bool prefShowClock = true;
bool prefShowDate = true;
bool prefShowWeather = true;
bool prefShowISS = true;
bool prefShowPlanes = true;
bool prefShowTextBlast = true;
bool prefShowDoodles = true;
bool prefShowEarthquake = true;
bool prefShowSpotify = true;
bool prefSpotifyShowOnPause = true;
bool prefShowDiags = true;
bool prefShowRadar = true;
bool spotifyIsPaused = true;


int prefTextBlastTextScale = 1;
bool prefTextBlastTextCustomMessage = false;
String prefTextBlastText = "";
int prefTextBlastTextColor = 0x00FFFF00;
int prefTextBlastBackgroundColor = 0x00000000;
int prefTextBlastCycles = 1;
float prefTextBlastSpeed = 40.0;

float prefLat = 34.16;
float prefLng = -84.80;

int prefRadarZoomLevel = 7;
RadarTimeFormat prefRadarTimeFormat = RadarTimeFormat::FORMAT_12H;
RadarUnitFormat prefRadarUnitFormat = RadarUnitFormat::MI;

String prefOsUser = "";
String prefOsPass = "";

String prefSpotifyRefreshToken = "";

int actualPanelX = 64;  // extend panel here and in hardware.h is you add another panel
int actualPanelY = 64;

int prefBrightness = 128;
bool prefNightMode = false;
int prefNightStart = 22;
int prefNightEnd = 6;
int prefTransitionTime = 10;

bool currentIsNight = false;

String currentGame = "shooter";


File fsUploadFile;
String gifDir = "/gifs";  // play all GIFs in this directory on the SD card
char filePath[256] = { 0 };
File root;


ShooterGame shooter;
Dungeon dungeon;


#define FILESYSTEM LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true


// globals
WeatherWidget weatherWidget;
DateProgressWidget dateWidget;
MorphClockWidget morphWidget;
TextBlastWidget textBlastWidget;
LogoWidget logoWidget;
IssLocationWidget issWidget;
PlanesWidget planesWidget;
EarthquakeWidget earthquakeWidget;
SpotifyWidget spotifyWidget;
DiagWidget diagWidget;
RadarWidget radarWidget;

bool showMarioNext = true;

uint16_t myBLACK = dma_display->color565(0, 0, 0);
uint16_t myWHITE = dma_display->color565(255, 255, 255);
uint16_t myRED = dma_display->color565(255, 0, 0);
uint16_t myGREEN = dma_display->color565(0, 255, 0);
uint16_t myBLUE = dma_display->color565(0, 0, 255);

File f;

//unsigned long start_tick = 0;

// Instantiate the wrappers globally
CustomMarioFont myMarioFont;



uint16_t applyNightVision(uint16_t color) {
  if (!currentIsNight || color == 0) return color;

  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5) & 0x3F;
  uint8_t b = color & 0x1F;

  uint8_t lum = (r + (g >> 1) + b) / 3;
  lum >>= 1;

  return (lum << 11);
}


void syncTimeWithLocation() {
  Serial.println("[TIME] Fetching timezone for current coordinates...");

  // We use standard WiFiClient for HTTP (no 'S' since your API is port 8084)
  WiFiClient client;
  HTTPClient http;

  // Build the URL using your config variables
  String url = "http://laltrello.com:8084/api/timezone?lat=" + String(prefLat, 4) + "&lon=" + String(prefLng, 4);

  http.begin(client, url);
  http.setTimeout(5000);  // 5-second timeout so the ESP doesn't freeze if your server is unreachable

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc.containsKey("posix")) {
      String posix = doc["posix"].as<String>();
      Serial.printf("[TIME] API Success! Timezone set to: %s\n", posix.c_str());

      // Set the timezone and sync with NTP servers
      configTzTime(posix.c_str(), "pool.ntp.org", "time.nist.gov");
      http.end();
      return;
    } else {
      Serial.println("[TIME] JSON parse failed or missing 'posix' key.");
    }
  } else {
    Serial.printf("[TIME] API call failed, HTTP Code: %d\n", httpCode);
  }

  http.end();

  // --- FALLBACK ---
  // If the API fails or isn't set, default to EST/EDT so the clock doesn't stay at 1970
  Serial.println("[TIME] Falling back to default EST5EDT.");
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
}

char mdnsHost[20];  // "rgbop-" + 6 hex chars + '\0'

void buildHostName() {

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);  // unique per board STA interface
  snprintf(mdnsHost, sizeof(mdnsHost),
           "rgbop-%02X%02X%02X", mac[3], mac[4], mac[5]);
}


/************************* Arduino Sketch Setup and Loop() *******************************/
void setup() {
  Serial.begin(115200);

  setupMatrixHardware();

  // Now perform the boot delay for the Serial Monitor
  delay(2000);

  // 3. --- MOUNT FILESYSTEM ---
  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
    Serial.println("LittleFS Mount Failed");
    return;
  }

  // Calculate and print space
  size_t totalBytes = LittleFS.totalBytes();
  size_t usedBytes = LittleFS.usedBytes();
  Serial.println("===== LittleFS Space =====");
  Serial.printf("Total space: %u bytes\n", totalBytes);
  Serial.printf("Used space: %u bytes\n", usedBytes);
  Serial.printf("Free space: %u bytes\n", totalBytes - usedBytes);
  Serial.println("==========================");

  // 4. --- WIFI & PROVISIONING SETUP ---
  Serial.println("Starting WiFi Sequence...");
  WiFi.mode(WIFI_STA);

  bool networkFound = false;

  // Attempt to load from LittleFS
  if (loadConfig()) {
    Serial.println("Loaded credentials from config.json. Attempting connection...");
    WiFi.begin(currentSSID.c_str(), currentPASS.c_str());
    WiFi.setSleep(false);
    // 1. DISABLE Wi-Fi Power Saving (Prevents the idle sleep/wake delay)
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 2. Set maximum Wi-Fi TX power (Ensures stable packet delivery during movement)
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    // Wait up to 10 seconds for a connection
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      Serial.print(".");
      drawDiagnostics();
      retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      networkFound = true;
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
    }
  }

  // If we couldn't connect, spin up the BLE Server
  if (!networkFound) {
    Serial.println("\n[WIFI] Failed or no credentials. Entering BLE Provisioning Mode.");
    provisioningMode = true;

    setupBLE();

    Serial.println("BLE Advertising Started. Waiting for Flutter app...");
  } else {
    Serial.println("\n[WIFI] Connected!");
    wifiConnected = true;
    drawDiagnostics();
    delay(1000);

    setupWebRoutes();

    // Bind shooter.reset() to game start event
gameManager.onGameStart([](const String& gameName) {
    if (dma_display != nullptr) {
        dma_display->clearScreen();
    }

    if (gameName == "dungeon") {
        dungeon.loadMap("http://rgbop.com:8084/api/dungeon/map/1");
        dungeon.reset();
    } else {
        shooter.reset();
    }
});
    // Initialize Game Manager routes & WebSocket
    gameManager.begin();

    buildHostName();

    if (MDNS.begin(mdnsHost)) {
      Serial.printf("[mDNS] Responder started: %s.local\n", mdnsHost);
      MDNS.addService("http", "tcp", 80);
    } else {
      Serial.println("[mDNS] Failed to start responder");
    }
  }

  syncTimeWithLocation();



  //planesWidget.begin(OPENSKY_CLIENT_ID, OPENSKY_CLIENT_SECRET, MY_LAT, MY_LNG, 20.0);
  if (prefShowPlanes) planesWidget.begin(prefOsUser, prefOsPass, prefLat, prefLng, 20.0);

  //if (prefShowSpotify) spotifyWidget.begin("060f37a1928a4b189785df59c90fda15","5546d8579cda460cb02c2c5b15f744e2","AQAYtmv8TAhMS2HuwBQVlnH_ahmOIHc9fkVeDmzQgjKGKOgeUeonpYq_ntYSRK0prbSduVTfmTFixUpaWi-qxWPRbO8z9F8rQ5QwV7Wgg9YNVqG2PjNX-T6w-T8PNMo5JW8");
  if (prefShowSpotify) spotifyWidget.begin(prefSpotifyRefreshToken);
  if (prefShowEarthquake) earthquakeWidget.begin();

  // 7. --- START GIF ENGINE ---
  gif.begin(LITTLE_ENDIAN_PIXELS);
}


void drawBluetoothWaiting() {
  // Clear the screen so it doesn't smear
  dma_display->clearScreen();

  // Calculate a breathing brightness using a sine wave based on time
  // This pulses between roughly 50 and 255 brightness
  uint8_t breath = (sin(millis() / 300.0) * 100) + 155;
  uint16_t btColor = dma_display->color565(0, 0, breath);  // Classic Blue, pulsing

  // Center coordinates for a 64x64 panel
  int cx = 32;
  int cy = 32;
  int size = 12;  // Scales the icon

  // Trace the classic Bluetooth rune
  // 1. Bottom Left to Center
  dma_display->drawLine(cx - size, cy + size, cx, cy, btColor);
  // 2. Center to Top Right
  dma_display->drawLine(cx, cy, cx + size, cy - size, btColor);
  // 3. Top Right to Top Center
  dma_display->drawLine(cx + size, cy - size, cx, cy - (size * 2), btColor);
  // 4. Top Center down to Bottom Center (The spine)
  dma_display->drawLine(cx, cy - (size * 2), cx, cy + (size * 2), btColor);
  // 5. Bottom Center to Bottom Right
  dma_display->drawLine(cx, cy + (size * 2), cx + size, cy + size, btColor);
  // 6. Bottom Right to Center
  dma_display->drawLine(cx + size, cy + size, cx, cy, btColor);
  // 7. Center to Top Left
  dma_display->drawLine(cx, cy, cx - size, cy - size, btColor);

  // Optional: Draw a subtle bounding box or text
  // myFont.drawText(&myGraphics, "SETUP", 18, 60);
}

void updateGameLogic(const GameInputState& input) {
  // 1. Read Movement Axes & Action Buttons
  bool moveUp = input.up;
  bool moveDown = input.down;
  bool moveLeft = input.left;
  bool moveRight = input.right;
  bool fireBtn = input.btnA;   // Primary Fire (Button A)
  bool boostBtn = input.btnB;  // Turbo Dash (Button B)

  // 2. Handle 8-Way Diagonal Combinations
  int dx = 0;
  int dy = 0;

  if (moveUp) dy--;
  if (moveDown) dy++;
  if (moveLeft) dx--;
  if (moveRight) dx++;

  // 3. Trigger Primary Fire
  if (fireBtn) {
    /* Spawn projectile in facing direction */
  }

  // 4. Trigger Secondary Action / Speed Boost
  if (boostBtn) {
    /* Apply movement speed multiplier or special ability */
  }
}

// ------------------------------------------------------------
// HELPER: BLE Provisioning Mode
// ------------------------------------------------------------
void handleProvisioning() {
  if (newCredentialsReceived) {
    newCredentialsReceived = false;

    dma_display->clearScreen();
    dma_display->fillRect(0, 0, 64, 64, dma_display->color565(200, 100, 0));
    delay(100);

    Serial.println("Attempting new connection...");
    WiFi.disconnect();
    WiFi.begin(currentSSID.c_str(), currentPASS.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20) {
      delay(500);
      retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connection Successful!");
      saveConfig();

      setupWebRoutes();
      buildHostName();
      if (MDNS.begin(mdnsHost)) {
        Serial.printf("[mDNS] Responder started: %s.local\n", mdnsHost);
        MDNS.addService("http", "tcp", 80);
      } else {
        Serial.println("[mDNS] Failed to start responder");
      }

      dma_display->clearScreen();
      dma_display->fillRect(0, 0, 64, 64, dma_display->color565(0, 255, 0));
      delay(2000);

      NimBLEDevice::deinit(true);
      Serial.println("[BLE] Bluetooth stack completely wiped from RAM.");
      provisioningMode = false;
      wifiConnected = true;

      configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
    } else {
      Serial.println("Connection Failed. Staying in BLE mode.");
      dma_display->clearScreen();
      dma_display->fillRect(0, 0, 64, 64, dma_display->color565(255, 0, 0));
      delay(2000);
      NimBLEDevice::getAdvertising()->start();
      Serial.println("[BLE] Beacon restarted! Ready for another try.");
    }
  }

  drawBluetoothWaiting();
  delay(30);
}

static inline bool hasNonGifWidgetsEnabled() {
  return prefShowClock || prefShowDate || prefShowTextBlast || prefShowWeather || prefShowISS || prefShowPlanes || prefShowDoodles || prefShowEarthquake || prefShowSpotify || prefShowDiags || prefShowRadar;
}

// ------------------------------------------------------------
// Widget Rotation
// ------------------------------------------------------------
void runWidgetRotation() {
  bool anyWidgetActive = hasNonGifWidgetsEnabled();

  if (anyWidgetActive) {
    if (gameManager.isGameModeActive()) return;  // <-- Bail immediately

    if (prefShowClock) {
      if (showMarioNext) showClock();
      else showMorphClock();
      showMarioNext = !showMarioNext;
    }

    if (gameManager.isGameModeActive()) return;  // <-- Bail check between widgets
    if (prefShowDate) showDateProgress();

    if (gameManager.isGameModeActive()) return;
    if (prefShowTextBlast) showTextBlast();

    if (gameManager.isGameModeActive()) return;
    if (prefShowWeather) showWeather();

    if (gameManager.isGameModeActive()) return;
    if (prefShowISS) showISS();

    if (gameManager.isGameModeActive()) return;
    if (prefShowPlanes) showPlanes();

    if (gameManager.isGameModeActive()) return;
    if (prefShowEarthquake) showEarthquakes();

    if (gameManager.isGameModeActive()) return;
    if (prefShowSpotify) showSpotify();

    if (gameManager.isGameModeActive()) return;
    if (prefShowDoodles) showDoodles();

    if (gameManager.isGameModeActive()) return;
    if (prefShowDiags) showDiags();

    if (gameManager.isGameModeActive()) return;
    if (prefShowRadar) showRadar();
  } else {
    if (!gameManager.isGameModeActive()) showLogo();
  }
}

// ------------------------------------------------------------
// HELPER: GIF Sequence
// ------------------------------------------------------------
void playGifSequence() {
  root = FILESYSTEM.open(gifDir);
  if (!root) return;

  File currentFile = root.openNextFile();
  while (currentFile) {
    // INSTANT BAIL OUT FOR GAME MODE
    if (gameManager.isGameModeActive()) {
      currentFile.close();
      break;
    }

    if (!currentFile.isDirectory()) {
      if (String(currentFile.name()).startsWith("_")) {
        currentFile.close();
        currentFile = root.openNextFile();
        continue;
      }

      maintainNetwork();

      // Check again right after network servicing
      if (gameManager.isGameModeActive()) {
        currentFile.close();
        break;
      }

      memset(filePath, 0x0, sizeof(filePath));
      strcpy(filePath, currentFile.path());

      if (prefShowGifs && !gameManager.isGameModeActive()) {
        playGIF(filePath);  // Ensure playGIF calls maintainNetwork() internally!
      } else {
        currentFile.close();
        break;
      }

      if (hasNonGifWidgetsEnabled() && !gameManager.isGameModeActive()) {
        runWidgetRotation();
      }
    }
    currentFile.close();
    currentFile = root.openNextFile();
  }
  root.close();
}
unsigned long lastRenderTime = 0;
const unsigned long RENDER_INTERVAL_MS = 16;  // ~60 FPS cap

void loop() {
  server.handleClient();
  gameManager.update();

//  PRIORITY: Game Mode Execution
  if (gameManager.isGameModeActive()) {
    GameInputState input = gameManager.getInputState();

    // Query active game directly from gameManager
    if (gameManager.getActiveGameName() == "dungeon") {
      dungeon.update(input);
    } else {
      shooter.update(input);
    }

    // Throttle MATRIX draws to ~60 FPS
    unsigned long now = millis();
    if (now - lastRenderTime >= RENDER_INTERVAL_MS) {
      if (gameManager.getActiveGameName() == "dungeon") {
        dungeon.draw(*dma_display);
      } else {
        shooter.draw(*dma_display);
      }
      lastRenderTime = now;
    }
    yield();
    return;
  }

  // If waiting for BLE credentials, block the rest of the loop
  if (provisioningMode) {
    handleProvisioning();
    return;
  }




  //  Play GIFs and interleave widgets if enabled
  if (prefShowGifs) {
    playGifSequence();
  }
  // 3. Otherwise, just maintain network and run widgets
  else {
    maintainNetwork();
    runWidgetRotation();
  }
}