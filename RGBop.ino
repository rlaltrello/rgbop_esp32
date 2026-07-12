#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>
#include "MarioClock.h"
#include "morphClock.h"
#include "dateProgress.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h> // Make sure to install this library in Arduino IDE
#include "weather.h"
#include "textblast.h"
#include "marioFont.h"
#include "isslocation.h"
#include "planes.h"
#include <WebServer.h>
#include "gifEngine.h"
#include "webApi.h"
#include "logo.h"
#include "widgetEngine.h"
#include "configEngine.h"

#define SPIRAM_DMA_BUFFER 1
#include "FS.h"
#include <LittleFS.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include <NimBLEDevice.h>



// --- RGBop BLE UUIDs ---
#define PROV_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID_SSID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_UUID_PASS    "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_UUID_CMD     "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// --- DYNAMIC CREDENTIALS & STATE ---
String currentSSID = "";
String currentPASS = "";
bool provisioningMode = false; 
bool newCredentialsReceived = false;

bool prefShowGifs = true;
bool prefShowClock = true;
bool prefShowDate = true;
bool prefShowWeather = true;
bool prefShowISS = true;
bool prefShowPlanes = true;
bool prefShowTextBlast = true;

float prefLat = 34.16;
float prefLng = -84.80;

String prefOsUser = "";
String prefOsPass = "";

int prefBrightness = 128;
bool prefNightMode = false;
int prefNightStart = 22;
int prefNightEnd = 6;

bool currentIsNight = false;

WebServer server(80);
File fsUploadFile;
String gifDir = "/gifs"; // play all GIFs in this directory on the SD card
char filePath[256] = { 0 };
File root;



#define FILESYSTEM LittleFS
#define FORMAT_LITTLEFS_IF_FAILED true

#define PANEL_RES_X 64     // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 64     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1      // Total number of panels chained one to another horizontally only.

// --- VERIFIED WORKING ESP32‑S3 HUB75 PINOUT ---
#define R1_PIN  1
#define G1_PIN  2
#define B1_PIN  3
#define R2_PIN  4
#define G2_PIN  5
#define B2_PIN  6

#define A_PIN   7
#define B_PIN   8
#define C_PIN   9
#define D_PIN   10
#define E_PIN   11

#define LAT_PIN 12
#define OE_PIN  13
#define CLK_PIN 14

// globals
WeatherWidget weatherWidget;
DateProgressWidget dateWidget;
MorphClockWidget morphWidget;
TextBlastWidget textBlastWidget;
LogoWidget logoWidget;

// --- ISS WIDGET GLOBALS ---
IssLocationWidget issWidget;
unsigned long lastISSFetch = 0;
unsigned long currentISSInterval = 0;
const unsigned long ISS_SUCCESS_INTERVAL = 3 * 60 * 1000; // 15 minutes
const unsigned long ISS_RETRY_INTERVAL = 15 * 1000;        // 15 seconds

// --- WEATHER WIDGET GLOBALS ---
unsigned long lastWeatherFetch = 0;
unsigned long currentWeatherInterval = 0; // Starts at 0 to force an immediate fetch
const unsigned long WEATHER_SUCCESS_INTERVAL = 5 * 60 * 1000; // 5 minutes
const unsigned long WEATHER_RETRY_INTERVAL = 30 * 1000;        // 30 seconds

// planes globals
PlanesWidget planesWidget;
unsigned long lastPlanesFetch = 0;
unsigned long currentPlanesInterval = 0;
const unsigned long PLANES_SUCCESS_INTERVAL = 30 * 1000; // OpenSky allows fairly frequent updates, 30s is safe.
const unsigned long PLANES_RETRY_INTERVAL = 15 * 1000;

bool showMarioNext = true;

//MatrixPanel_I2S_DMA dma_display;
MatrixPanel_I2S_DMA *dma_display = nullptr;

bool wifiConnected = false;
bool wsConnected = false;
unsigned long lastWatchdogCheck = 0;

uint16_t myBLACK = dma_display->color565(0, 0, 0);
uint16_t myWHITE = dma_display->color565(255, 255, 255);
uint16_t myRED = dma_display->color565(255, 0, 0);
uint16_t myGREEN = dma_display->color565(0, 255, 0);
uint16_t myBLUE = dma_display->color565(0, 0, 255);

//AnimatedGIF gif;
File f;
//int x_offset, y_offset;

class SSIDCallbacks: public NimBLECharacteristicCallbacks {
    // Notice the new NimBLEConnInfo parameter and the 'override' keyword
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
        currentSSID = pCharacteristic->getValue().c_str();
        Serial.print("\n[BLE] YAY! Received SSID: ");
        Serial.println(currentSSID);
    }
};

class PassCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
        currentPASS = pCharacteristic->getValue().c_str();
        Serial.print("[BLE] YAY! Received Password: ");
        Serial.println(currentPASS);
    }
};

class CmdCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {
        Serial.println("[BLE] YAY! Command Byte Received! Triggering WiFi sequence...");
        newCredentialsReceived = true;
    }
};



unsigned long start_tick = 0;



// ------------------------------------------------------------
// Diagnostic Pixel Drawing
// ------------------------------------------------------------
void drawDiagnostics() {
    if (dma_display == nullptr) return; // Safety check

    dma_display->clearScreen();

    // Pixel (0,0) - Panel Initialized / Loop Alive (Blue)
    dma_display->drawPixel(0, 0, dma_display->color565(0, 0, 255));

    // Pixel (1,0) - WiFi State (Green = Connected, Red = Disconnected)
    dma_display->drawPixel(1, 0,
        wifiConnected ? dma_display->color565(0, 255, 0)
                      : dma_display->color565(255, 0, 0));
}
// Create an invisible 64x64 16-bit canvas
GFXcanvas16 weatherCanvas(64, 64);


// Instantiate the wrappers globally
CustomMarioFont myMarioFont;

bool fetchWeatherData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Weather fetch aborted: WiFi not connected.");
        return false;
    }

    Serial.println("Fetching weather data via HTTP...");
    
    WiFiClient client;
    HTTPClient http;
    bool success = false;

    String weatherUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(MY_LAT, 4) + 
                        "&longitude=" + String(MY_LNG, 4) + 
                        "&current_weather=true&temperature_unit=fahrenheit";
    
    Serial.println("Weather URL: " + weatherUrl); // Good for debugging!
    
    http.begin(client, weatherUrl);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            JsonDocument doc; 
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                int temp = doc["current_weather"]["temperature"];
                int code = doc["current_weather"]["weathercode"];
                int isDay = doc["current_weather"]["is_day"];
                
                weatherWidget.updateWeather(temp, code, isDay == 1);
                Serial.printf("Weather updated: %dF, Code: %d\n", temp, code);
                success = true; // We successfully parsed the data!
            } else {
                Serial.print("JSON Parse failed: ");
                Serial.println(error.c_str());
            }
        } else {
             Serial.printf("HTTP request failed, server returned code: %d\n", httpCode);
        }
    } else {
        Serial.printf("HTTP connection failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return success;
}

uint16_t applyNightVision(uint16_t color) {
    if (!currentIsNight || color == 0) return color;

    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;

    uint8_t lum = (r + (g >> 1) + b) / 3;
    lum >>= 1;

    return (lum << 11);
}


void maintainNetwork() {
    unsigned long now = millis();

    // 1. WiFi Watchdog
    if (now - lastWatchdogCheck > 3000) {
        lastWatchdogCheck = now;

        updateBrightness();

        if (WiFi.status() != WL_CONNECTED) {
            if (wifiConnected) {
                Serial.println("[WIFI] Lost! Reconnecting...");
                wifiConnected = false;
                wsConnected = false;
            }
            WiFi.disconnect();
            WiFi.reconnect();
        } else {
            if (!wifiConnected) {
                Serial.println("[WIFI] Reconnected.");
                Serial.println(WiFi.localIP());
                wifiConnected = true;
            }
        }
    }

    // 2. Weather & ISS Fetches (Only if connected)
    if (WiFi.status() == WL_CONNECTED) {
        
        // --- Check Weather ---
        if (prefShowWeather && (now - lastWeatherFetch >= currentWeatherInterval || lastWeatherFetch == 0)) {
            if (fetchWeatherData()) {
                currentWeatherInterval = WEATHER_SUCCESS_INTERVAL; // Success: Wait 15 mins
            } else {
                currentWeatherInterval = WEATHER_RETRY_INTERVAL;   // Failure: Retry in 30 secs
            }
            lastWeatherFetch = millis(); 
        }
        
        // --- Check ISS ---
        if (prefShowISS && (now - lastISSFetch >= currentISSInterval || lastISSFetch == 0)) {
            if (issWidget.fetchISSData()) {
                currentISSInterval = ISS_SUCCESS_INTERVAL; // Success: Wait 3 minutes
            } else {
                currentISSInterval = ISS_RETRY_INTERVAL;     // Failure: Retry in 15 seconds
            }
            lastISSFetch = millis();
        }
        // --- Check Planes ---
        if (prefShowPlanes && (now - lastPlanesFetch >= currentPlanesInterval || lastPlanesFetch == 0)) {
            if (planesWidget.fetchPlanesData()) {
                currentPlanesInterval = PLANES_SUCCESS_INTERVAL; 
            } else {
                currentPlanesInterval = PLANES_RETRY_INTERVAL;   
            }
            lastPlanesFetch = millis(); 
        }
    }
}

/************************* Arduino Sketch Setup and Loop() *******************************/
void setup() {
  Serial.begin(115200);

  // 1. --- INITIALIZE LED PANEL FIRST ---
  // This allows us to use the panel for boot diagnostics!
  HUB75_I2S_CFG mxconfig(
    PANEL_RES_X,   // module width
    PANEL_RES_Y,   // module height
    PANEL_CHAIN    // Chain of panels - Horizontal width only.
  );

  mxconfig.gpio.r1 = R1_PIN;
  mxconfig.gpio.g1 = G1_PIN;
  mxconfig.gpio.b1 = B1_PIN;
  mxconfig.gpio.r2 = R2_PIN;
  mxconfig.gpio.g2 = G2_PIN;
  mxconfig.gpio.b2 = B2_PIN;

  mxconfig.gpio.a  = A_PIN;
  mxconfig.gpio.b  = B_PIN;
  mxconfig.gpio.c  = C_PIN;
  mxconfig.gpio.d  = D_PIN;
  mxconfig.gpio.e  = E_PIN;

  mxconfig.gpio.lat = LAT_PIN;
  mxconfig.gpio.oe  = OE_PIN;
  mxconfig.gpio.clk = CLK_PIN;

  // ESP32‑S3 specific settings
  mxconfig.driver = HUB75_I2S_CFG::FM6124;
  mxconfig.clkphase = false;
  mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
  mxconfig.latch_blanking = 4; 

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  dma_display->setBrightness8(128); 
  dma_display->clearScreen(); // Ensure screen is completely black

  // 2. --- SHOW INITIAL BOOT DIAGNOSTICS ---
  // (Will show Blue Pixel, Red Pixel)
  drawDiagnostics();

  // Now perform the boot delay for the Serial Monitor
  delay(2000);

  // 3. --- MOUNT FILESYSTEM ---
  if(!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)){
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
      
      NimBLEDevice::init("RGBop-Setup");
      NimBLEServer *pServer = NimBLEDevice::createServer();
      
      // Create the Services and Characteristics
      NimBLEService *pService = pServer->createService(PROV_SERVICE_UUID);
      
      NimBLECharacteristic *pSSIDChar = pService->createCharacteristic(
          CHAR_UUID_SSID,
          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
      );
      pSSIDChar->setCallbacks(new SSIDCallbacks());

      NimBLECharacteristic *pPassChar = pService->createCharacteristic(
          CHAR_UUID_PASS,
          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
      );
      pPassChar->setCallbacks(new PassCallbacks());

      NimBLECharacteristic *pCmdChar = pService->createCharacteristic(
          CHAR_UUID_CMD,
          NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
      );
      pCmdChar->setCallbacks(new CmdCallbacks());
      
      pService->start(); 
      
      NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
      pAdvertising->addServiceUUID(PROV_SERVICE_UUID);
      pAdvertising->start();
      
      Serial.println("BLE Advertising Started. Waiting for Flutter app...");
        } else {
      Serial.println("\n[WIFI] Connected!");
      wifiConnected = true;
      drawDiagnostics();
      delay(1000);
      
      setupWebRoutes();
      // -------------------------------------------
      if (MDNS.begin("rgbop")) {
          Serial.println("[mDNS] Responder started. I am now rgbop.local!");
          MDNS.addService("http", "tcp", 80); // Helps Flutter find the web server
      }
  }

  // 5. --- CONFIGURE TIME ---
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  Serial.println("Time configured via NTP.");
  
  //planesWidget.begin(OPENSKY_CLIENT_ID, OPENSKY_CLIENT_SECRET, MY_LAT, MY_LNG, 20.0);
  if (prefShowPlanes) planesWidget.begin(prefOsUser, prefOsPass, prefLat, prefLng, 20.0);

  // 7. --- START GIF ENGINE ---  
  gif.begin(LITTLE_ENDIAN_PIXELS);
}


void drawBluetoothWaiting() {
    // Clear the screen so it doesn't smear
    dma_display->clearScreen();

    // Calculate a breathing brightness using a sine wave based on time
    // This pulses between roughly 50 and 255 brightness
    uint8_t breath = (sin(millis() / 300.0) * 100) + 155; 
    uint16_t btColor = dma_display->color565(0, 0, breath); // Classic Blue, pulsing

    // Center coordinates for a 64x64 panel
    int cx = 32;
    int cy = 32;
    int size = 12; // Scales the icon

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

void loop() 
{
    //server.handleClient();
    // If we are waiting for BLE credentials, handle that and block the rest of the loop
    if (provisioningMode) {
        if (newCredentialsReceived) {
            newCredentialsReceived = false;
            
            // Give visual feedback that we are trying to connect
            dma_display->clearScreen();
            dma_display->fillRect(0, 0, 64, 64, dma_display->color565(200, 100, 0)); // Orange wait screen
            delay(100);

            Serial.println("Attempting new connection...");
            WiFi.disconnect();
            WiFi.begin(currentSSID.c_str(), currentPASS.c_str());
            
            // Wait up to 10 seconds
            int retries = 0;
            while (WiFi.status() != WL_CONNECTED && retries < 20) {
                delay(500);
                retries++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("Connection Successful!");
                saveConfig(); 

                setupWebRoutes();

                // -------------------------------------------
                if (MDNS.begin("rgbop")) {
                    Serial.println("[mDNS] Responder started. I am now rgbop.local!");
                    MDNS.addService("http", "tcp", 80); // Helps Flutter find the web server
                }
                
                // Flash Green for Success!
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
                // Flash Red for Failure, then go back to pulsing blue
                dma_display->clearScreen();
                dma_display->fillRect(0, 0, 64, 64, dma_display->color565(255, 0, 0));
                delay(2000);
                NimBLEDevice::getAdvertising()->start();
                Serial.println("[BLE] Beacon restarted! Ready for another try.");
            }
        }
        
        // --- DRAW THE WAITING ICON ---
        drawBluetoothWaiting();
        delay(30); // ~30fps for the breathing animation
        
        return; // <-- Stops the rest of the normal widget loop from running!
    }

   if (prefShowGifs) {
       // --- PLAY GIFS ---
       root = FILESYSTEM.open(gifDir);
       if (root)
       {
            gifFile = root.openNextFile();
            while (gifFile)
            {
               if (!gifFile.isDirectory())
                {
                    // --- THE FIX: Skip disabled GIFs ---
                    if (String(gifFile.name()).startsWith("_")) {
                        gifFile.close();
                        gifFile = root.openNextFile();
                        continue;
                    }
                    // --- SERVICE BACKGROUND TASKS ---
                    // This now fires between every single widget rotation!
                    maintainNetwork(); 

                    memset(filePath, 0x0, sizeof(filePath));                
                    strcpy(filePath, gifFile.path());
                
                    if (prefShowGifs) {
                        playGIF(filePath);
                    } else { // someone toggled Gifs off
                        gifFile.close();
                        break;
                    }   
                
                    // --- Alternate the Clocks --- 
                    if (prefShowClock) {
                        if (showMarioNext) {
                            showClock();
                        } else {
                            showMorphClock();
                        }
                        showMarioNext = !showMarioNext; 
                    }
                
                    if (prefShowDate) showDateProgress();  
                    if (prefShowTextBlast) showTextBlast();
                    if (prefShowWeather) showWeather();      
                    if (prefShowISS) showISS();
                    if (prefShowPlanes) showPlanes();
                }
                gifFile.close();
                gifFile = root.openNextFile();
             }
          root.close();
       }
} else { // no GIFs
        maintainNetwork();
        
        //Check if ANY widget is turned on
        bool anyWidgetActive = prefShowClock || prefShowDate || prefShowTextBlast || 
                               prefShowWeather || prefShowISS || prefShowPlanes;

        if (anyWidgetActive) {
            // --- Alternate the Clocks ---  
            if (prefShowClock) {
                if (showMarioNext) {
                    showClock();
                } else {
                    showMorphClock();
                }
                showMarioNext = !showMarioNext; 
            }
            if (prefShowDate) showDateProgress();  
            if (prefShowTextBlast) showTextBlast();
            if (prefShowWeather) showWeather();      
            if (prefShowISS) showISS();
            if (prefShowPlanes) showPlanes();
        } else {
            // ONLY show the logo if absolutely everything else is turned off!
            showLogo();
        }
   }
}