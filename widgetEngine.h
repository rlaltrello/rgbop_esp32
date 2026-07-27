#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Adafruit_GFX.h>
#include <time.h>

// Externs provided by main.ino
extern MatrixPanel_I2S_DMA* dma_display;
extern WebServer server;
extern bool currentIsNight;

// Widgets provided by main.ino
extern WeatherWidget weatherWidget;
extern DateProgressWidget dateWidget;
extern MorphClockWidget morphWidget;
extern TextBlastWidget textBlastWidget;
extern LogoWidget logoWidget;
extern IssLocationWidget issWidget;
extern PlanesWidget planesWidget;
extern EarthquakeWidget earthquakeWidget;
extern SpotifyWidget spotifyWidget;
extern DiagWidget diagWidget;
extern RadarWidget radarWidget;
extern GFXcanvas16 widgetCanvas;

extern int prefTransitionTime;
extern String prefSpotifyRefreshToken;

// Mario font provided by main.ino
extern CustomMarioFont myMarioFont;

extern int actualPanelX;
extern int actualPanelY;

// ------------------------------------------------------------
// CANVAS + FONT + GRAPHICS WRAPPERS
// ------------------------------------------------------------
GFXcanvas16 widgetCanvas(actualPanelX, actualPanelY);
extern GFXcanvas16 weatherCanvas; 

class MatrixFont : public Font {
public:
    void drawText(GraphicsContext* ctx, const std::string& text, int x, int y) override {
        widgetCanvas.setTextWrap(false);
        widgetCanvas.setTextSize(1);
        widgetCanvas.setCursor(x, y - 7);
        widgetCanvas.setTextColor(0xFFFF);
        widgetCanvas.print(text.c_str());
    }

    
void drawColorText(GraphicsContext* ctx,
                   const std::string& text,
                   int x,
                   int y,
                   uint32_t color) override{
                            widgetCanvas.setTextWrap(false);
        widgetCanvas.setTextSize(1);
        widgetCanvas.setCursor(x, y - 7);
        widgetCanvas.setTextColor(color);
        widgetCanvas.print(text.c_str());

                   }


    int getTextWidth(const std::string& text) override {
        return text.length() * 6;
    }

    void drawText(GraphicsContext* ctx, const std::string& text, int x, int y, int scale) {
        widgetCanvas.setTextWrap(false);
        widgetCanvas.setTextSize(scale);
        widgetCanvas.setCursor(x, y - 7);
        widgetCanvas.setTextColor(0xFFFF);
        widgetCanvas.print(text.c_str());
        widgetCanvas.setTextSize(1);
    }

    int getTextWidth(const std::string& text, int scale) {
        return text.length() * 6 * scale;
    }
};

static MatrixFont widgetFont;

class MatrixGraphics : public GraphicsContext {
private:
    uint16_t fillColor = 0;
    uint16_t strokeColor = 0;
    float curX = 0, curY = 0;

    uint16_t hexTo565(uint32_t color) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

public:
    void setFillStyle(uint32_t color) override { fillColor = hexTo565(color); }
    void setStrokeStyle(uint32_t color) override { strokeColor = hexTo565(color); }
    void setLineWidth(float width) override {}

    void beginPath() override {}
    void closePath() override {}

    void moveTo(float x, float y) override { curX = x; curY = y; }

    void lineTo(float x, float y) override {
        widgetCanvas.drawLine(curX, curY, x, y, strokeColor);
        curX = x; curY = y;
    }

    void arc(float x, float y, float radius, float startAngle, float endAngle) override {
        widgetCanvas.fillCircle(x, y, radius, fillColor);
    }

    void bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) override {
        float sx = curX, sy = curY;
        for (int i = 1; i <= 10; i++) {
            float t = i / 10.0f;
            float u = 1.0f - t;
            float px = u*u*u*sx + 3*u*u*t*cp1x + 3*u*t*t*cp2x + t*t*t*x;
            float py = u*u*u*sy + 3*u*u*t*cp1y + 3*u*t*t*cp2y + t*t*t*y;
            widgetCanvas.drawLine(curX, curY, px, py, strokeColor);
            curX = px; curY = py;
        }
    }

    void quadraticCurveTo(float cpx, float cpy, float x, float y) override {
        float sx = curX, sy = curY;
        for (int i = 1; i <= 10; i++) {
            float t = i / 10.0f;
            float u = 1.0f - t;
            float px = u*u*sx + 2*u*t*cpx + t*t*x;
            float py = u*u*sy + 2*u*t*cpy + t*t*y;
            widgetCanvas.drawLine(curX, curY, px, py, strokeColor);
            curX = px; curY = py;
        }
    }

    void fill() override {}
    void stroke() override {}

    void fillRect(float x, float y, float w, float h) override {
        widgetCanvas.fillRect(x, y, w, h, fillColor);
    }

    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
        uint16_t c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        widgetCanvas.drawPixel(x, y, c);
    }
};

static MatrixGraphics widgetGraphics;

// ------------------------------------------------------------
// NIGHT VISION + CANVAS PUSH
// ------------------------------------------------------------


static void pushCanvasToMatrix() {
    if (currentIsNight) {
        uint16_t* src = widgetCanvas.getBuffer();
        for (int y = 0; y < actualPanelY; y++) {
            for (int x = 0; x < actualPanelX; x++) {
                dma_display->drawPixel(x, y, applyNightVision(src[y * 64 + x]));
            }
        }
    } else {
        dma_display->drawRGBBitmap(0, 0, widgetCanvas.getBuffer(), actualPanelX, actualPanelY);
    }
}

// ------------------------------------------------------------
// WIDGET SHOW FUNCTIONS
// ------------------------------------------------------------
static void showWeather() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        weatherWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showLogo() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        logoWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY);
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showClock() {
    unsigned long start = millis();
    struct tm timeinfo;

    while (millis() - start < (prefTransitionTime * 1000)) {
        if (getLocalTime(&timeinfo)) {
            int hour = timeinfo.tm_hour;
            int minute = timeinfo.tm_min;
            if (hour == 0) hour = 12;
            else if (hour > 12) hour -= 12;
            MarioClock::drawClockFrame(hour, minute);
        }
        delay(30);
        server.handleClient();
    }
}

static void showMorphClock() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        morphWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

// Static variable to remember where we are in the directory across function calls
static File doodleDir;

static void showDoodles() {
    // 1. Open directory if it's not already open
    if (!doodleDir || !doodleDir.isDirectory()) {
        doodleDir = LittleFS.open("/doodles");
        if (!doodleDir) return; 
    }

    // 2. Try to get the next file
    File file = doodleDir.openNextFile();
    
    // 3. Loop back to start if at the end
    if (!file) {
        doodleDir.close();
        doodleDir = LittleFS.open("/doodles");
        if (!doodleDir) return;
        file = doodleDir.openNextFile();
        if (!file) return; 
    }

    // 4. Accept either 64x64 (8192 bytes) OR 128x64 (16384 bytes)
    size_t fileSize = 0;
    while (file) {
        fileSize = file.size();
        if (!file.isDirectory() && String(file.name()).endsWith(".bin") && 
           (fileSize == 8192 || fileSize == 16384)) {
            break; 
        }
        file.close();
        file = doodleDir.openNextFile();
    }

    // 5. Draw it safely
    if (file) {
        // Clear full canvas first (blanks second panel or extra margins)
        widgetCanvas.fillScreen(0x0000); 

        file.seek(0);

        if (fileSize == 8192) {
            // --------------------------------------------------------
            // Legacy 64x64 Doodle: Center across actualPanelX
            // --------------------------------------------------------
            int offsetX = (actualPanelX - 64) / 2;
            if (offsetX < 0) offsetX = 0;

            uint8_t rowBuffer[128]; // 64 pixels * 2 bytes = 128 bytes

            for (int y = 0; y < 64 && y < actualPanelY; y++) {
                if (file.read(rowBuffer, sizeof(rowBuffer)) != sizeof(rowBuffer)) break;

                for (int x = 0; x < 64; x++) {
                    uint16_t color = (rowBuffer[x * 2] << 8) | rowBuffer[x * 2 + 1];
                    widgetCanvas.drawPixel(offsetX + x, y, color);
                }
            }
        } else if (fileSize == 16384) {
            // --------------------------------------------------------
            // Full 128x64 Doodle
            // --------------------------------------------------------
            uint8_t rowBuffer[256]; // 128 pixels * 2 bytes = 256 bytes

            for (int y = 0; y < actualPanelY; y++) {
                if (file.read(rowBuffer, sizeof(rowBuffer)) != sizeof(rowBuffer)) break;

                for (int x = 0; x < actualPanelX; x++) {
                    uint16_t color = (rowBuffer[x * 2] << 8) | rowBuffer[x * 2 + 1];
                    widgetCanvas.drawPixel(x, y, color);
                }
            }
        }

        // Render to matrix for the transition time
        unsigned long start = millis();
        while (millis() - start < (prefTransitionTime * 1000)) {
            pushCanvasToMatrix();
            delay(30);
            server.handleClient();
        }
        file.close();
    }
}

static void showDateProgress() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        dateWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showTextBlast() {
    textBlastWidget.fetchMessage();
    textBlastWidget.resetScroll(64);

    unsigned long start = millis();
    bool done = false;

    while (!done && millis() - start < 60000) {
        done = textBlastWidget.draw(&widgetGraphics, &widgetFont, &myMarioFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showPlanes() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        planesWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showEarthquakes() {
    unsigned long start = millis();
    bool done = false;
    while (!done && millis() - start < 60000) {
        done = earthquakeWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showRadar() {
    radarWidget.setLocation(prefLat, prefLng);
    radarWidget.setZoomLevel(prefRadarZoomLevel);
    radarWidget.setTimeFormat(prefRadarTimeFormat);
    radarWidget.setUnitFormat(prefRadarUnitFormat);
    radarWidget.setFrameDelay(200); // 200ms per animation step
    radarWidget.syncConfig();
    radarWidget.fetch();
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        radarWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showDiags() {
    unsigned long start = millis();
    bool done = false;

    while (!done && millis() - start < 60000) {
        // Pass elapsed time (millis() - start) instead of raw millis()
        done = diagWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis() - start);
        
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}


static void showSpotify() {
    // 1. Fetch current track, progress, and 64x48 album art from VPS
    
    //spotifyWidget.begin("AQAYtmv8TAhMS2HuwBQVlnH_ahmOIHc9fkVeDmzQgjKGKOgeUeonpYq_ntYSRK0prbSduVTfmTFixUpaWi-qxWPRbO8z9F8rQ5QwV7Wgg9YNVqG2PjNX-T6w-T8PNMo5JW8");
    spotifyWidget.begin(prefSpotifyRefreshToken);
    spotifyWidget.fetchSpotifyStatus();
    // 2. Reset scroll position and cycle counters
    spotifyWidget.resetScroll(64);

    unsigned long start = millis();
    bool done = false;

    // 3. Render loop until configured scroll cycles complete (or 60s timeout)
    while (!done && millis() - start < 60000) {
        // Draw returns true once scrollX passes the text width targetCycles times
        done = spotifyWidget.draw(&widgetGraphics, &widgetFont, actualPanelX, actualPanelY, millis());
        
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showISS() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        issWidget.draw();
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}
