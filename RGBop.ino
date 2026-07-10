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

#define SPIRAM_DMA_BUFFER 1
#include "FS.h"
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include <NimBLEDevice.h>
#include "logo.h"


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

WebServer server(80);
File fsUploadFile;

String gifDir = "/gifs"; // play all GIFs in this directory on the SD card
char filePath[256] = { 0 };
File root, gifFile;



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

AnimatedGIF gif;
File f;
int x_offset, y_offset;

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

// --- PREFERENCES & SETTINGS ---
bool prefShowGifs = true;
bool prefShowClock = true;
bool prefShowDate = true;
bool prefShowWeather = true;
bool prefShowISS = true;
bool prefShowPlanes = true;
bool prefShowTextBlast = true;
float prefLat = 34.16;   // Defaulting nearby for initial boot
float prefLng = -84.80;  
String prefOsUser = "";
String prefOsPass = "";

// --- WEB SERVER ROUTES ---
void setupWebRoutes() {
    // 1. Factory Reset
    server.on("/api/reset", HTTP_POST, []() {
        Serial.println("[API] Factory reset requested!");
        server.send(200, "text/plain", "Resetting panel...");
        LittleFS.remove("/config.json");
        delay(1000); 
        ESP.restart();
    });

    // 2. Get Current Settings
    server.on("/api/settings", HTTP_GET, []() {
        JsonDocument doc;
        doc["gifs"] = prefShowGifs;
        doc["clock"] = prefShowClock;
        doc["date"] = prefShowDate;
        doc["weather"] = prefShowWeather;
        doc["iss"] = prefShowISS;
        doc["planes"] = prefShowPlanes;
        doc["textblast"] = prefShowTextBlast;
        doc["lat"] = prefLat;
        doc["lng"] = prefLng;
        doc["osUser"] = prefOsUser;
        doc["osPass"] = prefOsPass;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // 3. Save New Settings
    server.on("/api/settings", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "No payload");
            return;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, server.arg("plain"));
        if (error) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        if (doc.containsKey("gifs")) prefShowGifs = doc["gifs"];
        if (doc.containsKey("clock")) prefShowClock = doc["clock"];
        if (doc.containsKey("date")) prefShowDate = doc["date"];
        if (doc.containsKey("weather")) prefShowWeather = doc["weather"];
        if (doc.containsKey("iss")) prefShowISS = doc["iss"];
        if (doc.containsKey("planes")) prefShowPlanes = doc["planes"];
        if (doc.containsKey("textblast")) prefShowTextBlast = doc["textblast"];
        if (doc.containsKey("lat")) prefLat = doc["lat"];
        if (doc.containsKey("lng")) prefLng = doc["lng"];
        if (doc.containsKey("osUser")) prefOsUser = doc["osUser"].as<String>();
        if (doc.containsKey("osPass")) prefOsPass = doc["osPass"].as<String>();

        // Call the save function using the new globals
        saveConfig(); 

        // Live-update any widgets that rely on these keys/location
        planesWidget.begin(prefOsUser, prefOsPass, prefLat, prefLng, 20.0);

        server.send(200, "application/json", "{\"status\":\"success\"}");
        Serial.println("[API] Settings updated and saved!");
    });
    
    // 4. List all GIFs
    server.on("/api/gifs", HTTP_GET, []() {
        File root = FILESYSTEM.open(gifDir);
        if (!root) {
            server.send(500, "application/json", "{\"error\":\"Failed to open directory\"}");
            return;
        }

        JsonDocument doc;
        JsonArray array = doc["gifs"].to<JsonArray>();
        
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String fileName = String(file.name());
                JsonObject gifObj = array.add<JsonObject>();
                gifObj["name"] = String(file.name());
                gifObj["size"] = file.size();
                gifObj["enabled"] = !fileName.startsWith("_");
            }
            file = root.openNextFile();
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // 5. Delete a GIF
    server.on("/api/gifs/delete", HTTP_POST, []() {
        if (!server.hasArg("name")) {
            server.send(400, "application/json", "{\"error\":\"Missing filename\"}");
            return;
        }
        
        String filename = server.arg("name");
        // Ensure the path is strictly within the /gifs directory
        String path = gifDir + "/" + filename;
        
        if (FILESYSTEM.remove(path)) {
            Serial.printf("[FS] Deleted: %s\n", path.c_str());
            server.send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            server.send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
        }
    });

    server.on("/api/gifs/toggle", HTTP_POST, []() {
        if (!server.hasArg("name") || !server.hasArg("enabled")) {
            server.send(400, "application/json", "{\"error\":\"Missing args\"}");
            return;
        }
        
        String oldName = server.arg("name");
        bool enable = (server.arg("enabled") == "true");
        String newName = oldName;
        
        if (enable && oldName.startsWith("_")) {
            newName = oldName.substring(1); // Remove the underscore
        } else if (!enable && !oldName.startsWith("_")) {
            newName = "_" + oldName; // Add the underscore
        }

        if (oldName != newName) {
            FILESYSTEM.rename(gifDir + "/" + oldName, gifDir + "/" + newName);
            Serial.printf("[FS] Renamed %s to %s\n", oldName.c_str(), newName.c_str());
        }
        
        server.send(200, "application/json", "{\"status\":\"success\"}");
    });

    // 6. Upload a GIF (Requires a special two-part handler for streaming)
    server.on("/api/gifs/upload", HTTP_POST, 
        []() {
            // Part 1: The final HTTP response sent after the upload finishes
            server.send(200, "application/json", "{\"status\":\"success\"}");
        }, 
        []() {
            // Part 2: The chunk-by-chunk stream handler
            HTTPUpload& upload = server.upload();
            
            if (upload.status == UPLOAD_FILE_START) {
                String filename = upload.filename;
                if (!filename.startsWith("/")) filename = "/" + filename;
                String path = gifDir + filename;
                
                Serial.printf("[Upload] Starting: %s\n", path.c_str());
                fsUploadFile = FILESYSTEM.open(path, "w");
                
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (fsUploadFile) {
                    fsUploadFile.write(upload.buf, upload.currentSize);
                }
            } else if (upload.status == UPLOAD_FILE_END) {
                if (fsUploadFile) {
                    fsUploadFile.close();
                    Serial.printf("[Upload] Finished: %s, Size: %u bytes\n", upload.filename.c_str(), upload.totalSize);
                }
            }
        }
    );
    // 7. Serve the actual GIF files so the app can preview them
    server.serveStatic("/gifs", FILESYSTEM, "/gifs");

    server.begin();
    Serial.println("[WEB] HTTP server and API routes started on port 80");
}

// Draw a line of image directly on the LED Matrix
void GIFDraw(GIFDRAW *pDraw)
{
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > dma_display->width())
      iWidth = dma_display->width();

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // current line
    
    s = pDraw->pPixels;
    if (pDraw->ucDisposalMethod == 2) // restore to background color
    {
      for (x=0; x<iWidth; x++)
      {
        if (s[x] == pDraw->ucTransparent)
           s[x] = pDraw->ucBackground;
      }
      pDraw->ucHasTransparency = 0;
    }
    // Apply the new pixels to the main image
    if (pDraw->ucHasTransparency) // if transparency used
    {
      uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
      int x, iCount;
      pEnd = s + pDraw->iWidth;
      x = 0;
      iCount = 0; // count non-transparent pixels
      while(x < pDraw->iWidth)
      {
        c = ucTransparent-1;
        d = usTemp;
        while (c != ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent) // done, stop
          {
            s--; // back up to treat it like transparent
          }
          else // opaque
          {
             *d++ = usPalette[c];
             iCount++;
          }
        } // while looking for opaque pixels
        if (iCount) // any opaque pixels?
        {
          for(int xOffset = 0; xOffset < iCount; xOffset++ ){
            dma_display->drawPixel(x + xOffset, y, usTemp[xOffset]); // 565 Color Format
          }
          x += iCount;
          iCount = 0;
        }
        // no, look for a run of transparent pixels
        c = ucTransparent;
        while (c == ucTransparent && s < pEnd)
        {
          c = *s++;
          if (c == ucTransparent)
             iCount++;
          else
             s--; 
        }
        if (iCount)
        {
          x += iCount; // skip these
          iCount = 0;
        }
      }
    }
    else // does not have transparency
    {
      s = pDraw->pPixels;
      // Translate the 8-bit pixels through the RGB565 palette (already byte reversed)
      for (x=0; x<pDraw->iWidth; x++)
      {
        dma_display->drawPixel(x, y, usPalette[*s++]); // color 565
      }
    }
} /* GIFDraw() */


void * GIFOpenFile(const char *fname, int32_t *pSize)
{
  Serial.print("Playing gif: ");
  Serial.println(fname);
  f = FILESYSTEM.open(fname);
  if (f)
  {
    *pSize = f.size();
    return (void *)&f;
  }
  return NULL;
} /* GIFOpenFile() */

void GIFCloseFile(void *pHandle)
{
  File *f = static_cast<File *>(pHandle);
  if (f != NULL)
     f->close();
} /* GIFCloseFile() */

int32_t GIFReadFile(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen)
{
    int32_t iBytesRead;
    iBytesRead = iLen;
    File *f = static_cast<File *>(pFile->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((pFile->iSize - pFile->iPos) < iLen)
       iBytesRead = pFile->iSize - pFile->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
       return 0;
    iBytesRead = (int32_t)f->read(pBuf, iBytesRead);
    pFile->iPos = f->position();
    return iBytesRead;
} /* GIFReadFile() */

int32_t GIFSeekFile(GIFFILE *pFile, int32_t iPosition)
{ 
  int i = micros();
  File *f = static_cast<File *>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  i = micros() - i;
//  Serial.printf("Seek time = %d us\n", i);
  return pFile->iPos;
} /* GIFSeekFile() */

unsigned long start_tick = 0;

void ShowGIF(char *name)
{
  start_tick = millis();
   
  if (gif.open(name, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw))
  {
    x_offset = (dma_display->width() - gif.getCanvasWidth())/2;
    if (x_offset < 0) x_offset = 0;
    y_offset = (dma_display->height() - gif.getCanvasHeight())/2;
    if (y_offset < 0) y_offset = 0;
    Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", gif.getCanvasWidth(), gif.getCanvasHeight());
    Serial.flush();
    
    // Loop continuously until 10,000 milliseconds (10 seconds) have passed
    while ((millis() - start_tick) < 10000) 
    {      
      int frameResult = gif.playFrame(true, NULL); // Play the next frame
      dma_display->flipDMABuffer();
      server.handleClient();
      if (frameResult == 0) 
      { 
        // playFrame returned 0, meaning it hit the end of the GIF
        gif.reset(); // Rewind the GIF back to the first frame to loop it
      }
    }
    
    gif.close();
  }

} /* ShowGIF() */

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

// --- FONT WRAPPER ---
class MatrixFont : public Font {
public:
    // 1. STANDARD VERSION (Satisfies weather.h)
    void drawText(GraphicsContext* ctx, const std::string& text, int x, int y) override {
        weatherCanvas.setTextWrap(false); 
        weatherCanvas.setTextSize(1);
        weatherCanvas.setCursor(x, y - 7); 
        weatherCanvas.setTextColor(0xFFFF); 
        weatherCanvas.print(text.c_str());
    }
    
    int getTextWidth(const std::string& text) override {
        return text.length() * 6; 
    }

    // 2. SCALED VERSION (Used by textblast.h)
    // Notice there is no 'override' keyword here because this is a new, custom function!
    void drawText(GraphicsContext* ctx, const std::string& text, int x, int y, int scale) {
        weatherCanvas.setTextWrap(false); 
        weatherCanvas.setTextSize(scale);   // Native Adafruit scaling
        weatherCanvas.setCursor(x, y - 7); 
        weatherCanvas.setTextColor(0xFFFF); 
        weatherCanvas.print(text.c_str());
        weatherCanvas.setTextSize(1);       // Reset to normal 
    }
    
    int getTextWidth(const std::string& text, int scale) {
        return text.length() * 6 * scale; 
    }
};

// --- GRAPHICS WRAPPER ---
class MatrixGraphics : public GraphicsContext {
private:
    uint16_t fillColor = 0;
    uint16_t strokeColor = 0;
    float curX = 0, curY = 0;

    // Helper to convert 0xRRGGBB to RGB565 locally
    uint16_t hexTo565(uint32_t color) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

public:
    void setFillStyle(uint32_t color) override { fillColor = hexTo565(color); }
    void setStrokeStyle(uint32_t color) override { strokeColor = hexTo565(color); }
    void setLineWidth(float width) override { }
    
    void beginPath() override { }
    void closePath() override { }
    
    void moveTo(float x, float y) override { 
        curX = x; curY = y; 
    }
    
    void lineTo(float x, float y) override {
        weatherCanvas.drawLine(curX, curY, x, y, strokeColor);
        curX = x; curY = y;
    }
    
    void arc(float x, float y, float radius, float startAngle, float endAngle) override {
        weatherCanvas.fillCircle(x, y, radius, fillColor);
    }
    
    void bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) override {
        float startX = curX; float startY = curY;
        for(int i = 1; i <= 10; i++) {
            float t = i / 10.0f;
            float u = 1.0f - t;
            float px = u*u*u*startX + 3*u*u*t*cp1x + 3*u*t*t*cp2x + t*t*t*x;
            float py = u*u*u*startY + 3*u*u*t*cp1y + 3*u*t*t*cp2y + t*t*t*y;
            weatherCanvas.drawLine(curX, curY, px, py, strokeColor);
            curX = px; curY = py;
        }
    }
    
    void quadraticCurveTo(float cpx, float cpy, float x, float y) override {
        float startX = curX; float startY = curY;
        for(int i = 1; i <= 10; i++) {
            float t = i / 10.0f;
            float u = 1.0f - t;
            float px = u*u*startX + 2*u*t*cpx + t*t*x;
            float py = u*u*startY + 2*u*t*cpy + t*t*y;
            weatherCanvas.drawLine(curX, curY, px, py, strokeColor);
            curX = px; curY = py;
        }
    }
    
    void fill() override { }
    void stroke() override { }
    
    void fillRect(float x, float y, float w, float h) override {
        weatherCanvas.fillRect(x, y, w, h, fillColor);
    }
    
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
        // Direct conversion to 565 for the canvas
        uint16_t color565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        weatherCanvas.drawPixel(x, y, color565);
    }
};
// Instantiate the wrappers globally
MatrixFont myFont;
MatrixGraphics myGraphics;
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

void showWeather() {
    unsigned long weatherStartTime = millis();
    
    // Run the animation for 10 seconds
    while (millis() - weatherStartTime < 10000) {
        
        // 1. Draw everything securely to the invisible canvas in RAM
        weatherWidget.draw(&myGraphics, &myFont, 64, 64, millis());
        
        // 2. Smash the finished picture onto the live LED matrix!
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        
        delay(30); // ~30fps frame rate lock
        server.handleClient();
    }
}

void showLogo() {
     unsigned long logoStartTime = millis();
    
    // Run the animation for 10 seconds
    while (millis() - logoStartTime < 10000) {
        logoWidget.draw(&myGraphics, &myFont, 64, 64);
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        delay(30); // ~30fps frame rate lock
        server.handleClient();
    }
}

void showClock() {
  // --- SHOW CLOCK ---
   unsigned long clockStartTime = millis();
   
   // Create a time structure to hold the real time
   struct tm timeinfo;
   int temp_hour = 12; // Fallback defaults
   int temp_minute = 30;

   // Grab the actual time from the ESP32's internal clock
   if (getLocalTime(&timeinfo)) {
       temp_hour = timeinfo.tm_hour;
       temp_minute = timeinfo.tm_min;

       // If your MarioClock code expects 12-hour time instead of 24-hour time:
       if (temp_hour == 0) {
           temp_hour = 12; // Midnight
       } else if (temp_hour > 12) {
           temp_hour -= 12; // PM hours
       }
   }
   
   // Run the clock animation for 30 seconds
   while (millis() - clockStartTime < 10000) 
   {
      // (Optional) Re-check the time inside the while-loop so it updates instantly 
      // if the minute rolls over during this 30-second window!
      if (getLocalTime(&timeinfo)) {
          int current_minute = timeinfo.tm_min;
          int current_hour = timeinfo.tm_hour;
          if (current_hour > 12) current_hour -= 12;
          if (current_hour == 0) current_hour = 12;
          
          MarioClock::drawClockFrame(current_hour, current_minute); 
      } else {
          // Fallback if time fails to fetch
          MarioClock::drawClockFrame(temp_hour, temp_minute); 
      }
      
      delay(30); // Prevent the loop from running too fast
      server.handleClient();
   }
}

void showMorphClock() {
    unsigned long clockStartTime = millis();
    
    // Run the morphing clock animation for 10 seconds
    while (millis() - clockStartTime < 10000) {
        
        // 1. Draw securely to the invisible canvas in RAM
        morphWidget.draw(&myGraphics, &myFont, 64, 64, millis());
        
        // 2. Push to DMA matrix
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        
        delay(30); // ~30fps frame rate lock
        server.handleClient();
    }
}

void showDateProgress() {
    unsigned long dateStartTime = millis();
    
    // Run the animation for 10 seconds (adjust as desired)
    while (millis() - dateStartTime < 10000) {
        
        // 1. Draw everything securely to the invisible canvas in RAM
        // Pass in the existing myGraphics and myFont wrappers
        dateWidget.draw(&myGraphics, &myFont, 64, 64, millis());
        
        // 2. Smash the finished picture onto the live LED matrix
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        
        delay(30); // ~30fps frame rate lock
        server.handleClient();
    }
}

void showTextBlast() {
    textBlastWidget.fetchMessage();
    textBlastWidget.resetScroll(64);

    unsigned long startTime = millis();
    bool isDone = false;
    
    // Loop until the widget says it's done OR 60 seconds have passed (safety net)
    while (!isDone && (millis() - startTime < 60000)) {
        
        // The draw function will return TRUE when the requested cycles are finished
        isDone = textBlastWidget.draw(&myGraphics, &myFont, &myMarioFont, 64, 64, millis());
        
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        
        delay(30);
        server.handleClient();
    }
}

void showPlanes() {
    unsigned long planesStartTime = millis();
    while (millis() - planesStartTime < 10000) { // Show for 10 seconds
        planesWidget.draw(&myGraphics, &myFont, 64, 64, millis());
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        delay(30);
        server.handleClient();
    }
}

void showISS() {
    unsigned long issStartTime = millis();
    
    // Run the animation for 10 seconds
    while (millis() - issStartTime < 10000) {
        
        // 1. Draw to the invisible canvas
        issWidget.draw();
        
        // 2. Smash onto the live matrix
        dma_display->drawRGBBitmap(0, 0, weatherCanvas.getBuffer(), 64, 64);
        
        delay(30); // ~30fps lock
        server.handleClient();
    }
}

bool loadConfig() {
    File file = FILESYSTEM.open("/config.json", "r");
    if (!file) return false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) return false;

    currentSSID = doc["ssid"] | "";
    currentPASS = doc["password"] | "";
    
    // Load preferences (with safe fallbacks if they don't exist yet)
    prefShowGifs = doc["gifs"] | true;
    prefShowClock = doc["clock"] | true;
    prefShowDate = doc["date"] | true;
    prefShowWeather = doc["weather"] | true;
    prefShowISS = doc["iss"] | true;
    prefShowPlanes = doc["planes"] | true;
    prefShowTextBlast = doc["textblast"] | true;
    prefLat = doc["lat"] | 34.16;
    prefLng = doc["lng"] | -84.80;
    prefOsUser = doc["osUser"] | "";
    prefOsPass = doc["osPass"] | "";
    
    return (currentSSID.length() > 0);
}

void saveConfig() {
    JsonDocument doc;
    doc["ssid"] = currentSSID;
    doc["password"] = currentPASS;

    doc["gifs"] = prefShowGifs;
    doc["clock"] = prefShowClock;
    doc["date"] = prefShowDate;
    doc["weather"] = prefShowWeather;
    doc["iss"] = prefShowISS;
    doc["planes"] = prefShowPlanes;
    doc["textblast"] = prefShowTextBlast;
    doc["lat"] = prefLat;
    doc["lng"] = prefLng;
    doc["osUser"] = prefOsUser;
    doc["osPass"] = prefOsPass;

    File file = FILESYSTEM.open("/config.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("[FS] Full configuration saved to LittleFS.");
    }
}

void maintainNetwork() {
    unsigned long now = millis();

    // 1. WiFi Watchdog
    if (now - lastWatchdogCheck > 3000) {
        lastWatchdogCheck = now;

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
                        ShowGIF(filePath);
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