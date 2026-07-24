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
extern GFXcanvas16 widgetCanvas;

extern int prefTransitionTime;
extern String prefSpotifyRefreshToken;

// Mario font provided by main.ino
extern CustomMarioFont myMarioFont;

// ------------------------------------------------------------
// CANVAS + FONT + GRAPHICS WRAPPERS
// ------------------------------------------------------------
GFXcanvas16 widgetCanvas(64, 64);
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
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                dma_display->drawPixel(x, y, applyNightVision(src[y * 64 + x]));
            }
        }
    } else {
        dma_display->drawRGBBitmap(0, 0, widgetCanvas.getBuffer(), 64, 64);
    }
}

// ------------------------------------------------------------
// WIDGET SHOW FUNCTIONS
// ------------------------------------------------------------
static void showWeather() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        weatherWidget.draw(&widgetGraphics, &widgetFont, 64, 64, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showLogo() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        logoWidget.draw(&widgetGraphics, &widgetFont, 64, 64);
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
        morphWidget.draw(&widgetGraphics, &widgetFont, 64, 64, millis());
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
        if (!doodleDir) return; // Directory doesn't exist yet
    }

    // 2. Try to get the next file
    File file = doodleDir.openNextFile();
    
    // 3. If we hit the end of the folder, loop back to the beginning
    if (!file) {
        doodleDir.close();
        doodleDir = LittleFS.open("/doodles");
        if (!doodleDir) return;
        file = doodleDir.openNextFile();
        if (!file) return; // Directory is completely empty
    }

    // 4. Seek forward until we find a valid .bin file
    while (file) {
        if (!file.isDirectory() && String(file.name()).endsWith(".bin") && file.size() == 8192) {
            break; // Found a valid doodle!
        }
        file.close();
        file = doodleDir.openNextFile();
    }

    // 5. Draw it
    if (file) {
        widgetCanvas.fillScreen(0x0000); 
        uint8_t buffer[128]; 
        file.seek(0);
        
        for (int y = 0; y < 64; y++) {
            file.read(buffer, 128);
            for (int x = 0; x < 64; x++) {
                uint16_t color = (buffer[x * 2] << 8) | buffer[x * 2 + 1];
                widgetCanvas.drawPixel(x, y, color);
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
        dateWidget.draw(&widgetGraphics, &widgetFont, 64, 64, millis());
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
        done = textBlastWidget.draw(&widgetGraphics, &widgetFont, &myMarioFont, 64, 64, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showPlanes() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        planesWidget.draw(&widgetGraphics, &widgetFont, 64, 64, millis());
        pushCanvasToMatrix();
        delay(30);
        server.handleClient();
    }
}

static void showEarthquakes() {
    unsigned long start = millis();
    while (millis() - start < (prefTransitionTime * 1000)) {
        earthquakeWidget.draw(&widgetGraphics, &widgetFont, 64, 64, millis());
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
        done = spotifyWidget.draw(&widgetGraphics, &widgetFont, 64, 64, millis());
        
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
