#ifndef WEATHER_H
#define WEATHER_H

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
//#include "config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ------------------------------------------------------------
// Interfaces (Implement these in your main application)
// ------------------------------------------------------------
class Font {
public:
    virtual void drawText(class GraphicsContext* ctx, const std::string& text, int x, int y) = 0;

    virtual void drawColorText(GraphicsContext* ctx,
                           const std::string& text,
                           int x,
                           int y,
                           uint16_t color) = 0;

    virtual int getTextWidth(const std::string& text) = 0;
};

class GraphicsContext {
public:
    virtual void setFillStyle(uint32_t color) = 0;
    virtual void setStrokeStyle(uint32_t color) = 0;
    virtual void setLineWidth(float width) = 0;
    
    virtual void beginPath() = 0;
    virtual void closePath() = 0;
    virtual void moveTo(float x, float y) = 0;
    virtual void lineTo(float x, float y) = 0;
    virtual void arc(float x, float y, float radius, float startAngle, float endAngle) = 0;
    virtual void bezierCurveTo(float cp1x, float cp1y, float cp2x, float cp2y, float x, float y) = 0;
    virtual void quadraticCurveTo(float cpx, float cpy, float x, float y) = 0;
    
    virtual void fill() = 0;
    virtual void stroke() = 0;
    virtual void fillRect(float x, float y, float w, float h) = 0;
    
    // Pixel manipulation for background dithering
    virtual void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) = 0;
};

// ------------------------------------------------------------
// Weather Widget Class
// ------------------------------------------------------------
struct WeatherState {
    int temp = 0;
    int code = 0;
    bool isDay = true;
    bool loaded = false;
};

class WeatherWidget {
private:

    WeatherState weatherState;
    uint32_t animTick = 0;

    bool demoMode = false;
    int demoIndex = 0;
    unsigned long demoTimer = 0; // Requires passing current time in ms

    const std::vector<std::string> DEMO_ICONS = {
        "sun", "moon", "cloud", "rain", "snow", "lightning", "fog", "wind"
    };

    static constexpr uint8_t bayer4[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
    };

    uint8_t ditherColor(float base, int x, int y) {
        float level = bayer4[y & 3][x & 3];
        float offset = (level - 7.5f) * 0.5f;
        float v = base + offset;
        return static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, v)));
    }

    // --- Drawing Functions ---
    void drawSun(GraphicsContext* ctx) {
        float shimmer = std::sin(animTick * 0.05f) * 2.0f;

        ctx->setFillStyle(0xFFDD00); // Format: 0xRRGGBB
        ctx->beginPath();
        ctx->arc(32, 24, 10 + shimmer, 0, M_PI * 2);
        ctx->fill();

        ctx->setStrokeStyle(0xFFDD00);
        ctx->setLineWidth(3);

        auto ray = [&](float x1, float y1, float x2, float y2) {
            ctx->beginPath();
            ctx->moveTo(x1, y1);
            ctx->lineTo(x2, y2);
            ctx->stroke();
        };

        float r = 12 + shimmer;

        ray(32, 24 - r, 32, 24 - (r + 6));
        ray(32, 24 + r, 32, 24 + (r + 6));
        ray(32 - r, 24, 32 - (r + 6), 24);
        ray(32 + r, 24, 32 + (r + 6), 24);

        ray(32 - r * 0.7f, 24 - r * 0.7f, 32 - (r + 6), 24 - (r + 6));
        ray(32 + r * 0.7f, 24 - r * 0.7f, 32 + (r + 6), 24 - (r + 6));
        ray(32 - r * 0.7f, 24 + r * 0.7f, 32 - (r + 6), 24 + (r + 6));
        ray(32 + r * 0.7f, 24 + r * 0.7f, 32 + (r + 6), 24 + (r + 6));
    }

    void drawMoon(GraphicsContext* ctx) {
        float glow = std::sin(animTick * 0.04f) * 2.0f;

        ctx->setFillStyle(0xDDDDDD);
        ctx->beginPath();
        ctx->arc(32, 24, 10 + glow, 0, M_PI * 2);
        ctx->fill();

        ctx->setFillStyle(0x001122);
        ctx->beginPath();
        ctx->arc(36, 20, 10 + glow, 0, M_PI * 2);
        ctx->fill();
    }

void drawCloud(GraphicsContext* ctx) {
        float drift = std::sin(animTick * 0.03f) * 2.0f;
        float scale = 0.16f;
        float ox = 1 + drift;
        float oy = 4;

        ctx->setFillStyle(0xD0D0E0);
        ctx->setStrokeStyle(0x555566);

        ctx->beginPath();

        ctx->arc((100 * scale) + ox, (135 * scale) + oy, 60 * scale, M_PI * 0.5, M_PI * 1.5);
        ctx->arc((170 * scale) + ox, (75 * scale) + oy,  70 * scale, M_PI * 1.0, M_PI * 1.85);
        ctx->arc((252 * scale) + ox, (90 * scale) + oy,  50 * scale, M_PI * 1.37, M_PI * 1.91);
        ctx->arc((300 * scale) + ox, (135 * scale) + oy, 60 * scale, M_PI * 1.5, M_PI * 0.5);

// ------------------------------------------------------------
        // Draw the bottom flat edge of the cloud
        // ------------------------------------------------------------
        float rightX = (300 * scale) + ox;
        float leftX = (100 * scale) + ox;
        float baseY = (195 * scale) + oy;

        // 1. Close the path with just ONE line for the dark outline
        ctx->moveTo(rightX, baseY);
        ctx->lineTo(leftX, baseY);

        ctx->closePath();
        ctx->fill();

        // 2. Thicken the INSIDE of the cloud by drawing a light-grey rectangle 
        // just above the bottom outline. This fills in the gaps with "cloud" color!
        ctx->setFillStyle(0xD0D0E0); // Cloud body color
        ctx->fillRect(leftX + 1, baseY - 3, (rightX - leftX) - 2, 3);

        // 3. Finally, draw the dark outline so it crisp up the edges
        ctx->stroke();

    }

    void drawSnow(GraphicsContext* ctx) {
        drawCloud(ctx);
        int fall = (animTick % 20);

        ctx->setFillStyle(0xFFFFFF);
        ctx->fillRect(26, 32 + fall, 2, 2);
        ctx->fillRect(32, 34 + fall, 2, 2);
        ctx->fillRect(38, 32 + fall, 2, 2);
    }

void drawRain(GraphicsContext* ctx) {
        drawCloud(ctx); // Draw the cloud in the background

        ctx->setStrokeStyle(0x66A8FF); // Crisp rain blue

        // REIMAGINED RAIN: Array of 5 staggered raindrops
        // Format: { X position, Animation Offset, Length }
        int drops[5][3] = {
            {22, 0,  4},
            {30, 8,  6},
            {38, 3,  5},
            {46, 12, 3},
            {54, 6,  5}
        };

        for (int i = 0; i < 5; i++) {
            int dropX = drops[i][0];
            int yOff = drops[i][1];
            int len = drops[i][2];

            // Use animTick to make them fall endlessly (loops every 24 ticks)
            int fall = (animTick + yOff) % 24; 
            
            int startY = 32 + fall;
            int endY = startY + len;

            // Clip the rain so it doesn't draw over the temperature text
            if (startY > 50) continue; 
            if (endY > 50) endY = 50;

            ctx->beginPath();
            ctx->moveTo(dropX, startY);
            // Draw the line with a slight leftward slant for a "wind" effect
            ctx->lineTo(dropX - (len / 3), endY); 
            ctx->stroke();
        }
    }

void drawLightning(GraphicsContext* ctx) {
        drawCloud(ctx);

        bool flash = (animTick % 30) < 4;
        if (!flash) return;

        ctx->setStrokeStyle(0xFFFF55); // Lightning Yellow

        // Strike 1: The original coordinates
        ctx->beginPath();
        ctx->moveTo(32, 18);
        ctx->lineTo(40, 30);
        ctx->lineTo(30, 42);
        ctx->lineTo(44, 54);
        ctx->lineTo(34, 66);
        ctx->stroke();

        // Strike 2: Shifted +1 on the X-axis to double the thickness!
        ctx->beginPath();
        ctx->moveTo(33, 18);
        ctx->lineTo(41, 30);
        ctx->lineTo(31, 42);
        ctx->lineTo(45, 54);
        ctx->lineTo(35, 66);
        ctx->stroke();
    }

    void drawFog(GraphicsContext* ctx) {
        float slide = std::sin(animTick * 0.05f) * 4.0f;

        ctx->setFillStyle(0xCCCCCC);
        ctx->fillRect(16 + slide, 28, 32, 3);
        ctx->fillRect(12 + slide, 34, 40, 3);
        ctx->fillRect(20 + slide, 40, 28, 3);
    }

    void drawWind(GraphicsContext* ctx) {
        float slide = std::sin(animTick * 0.05f) * 4.0f;

        ctx->setStrokeStyle(0xFFFFFF);
        ctx->setLineWidth(3);
        // Note: You may need to handle lineCap = 'round' in your native Context

        // Top swoosh
        ctx->beginPath();
        ctx->moveTo(14 + slide, 22);
        ctx->quadraticCurveTo(30 + slide, 16, 50 + slide, 22);
        ctx->stroke();

        ctx->beginPath();
        ctx->moveTo(50 + slide, 22);
        ctx->quadraticCurveTo(54 + slide, 24, 50 + slide, 26);
        ctx->stroke();

        // Middle swoosh
        ctx->beginPath();
        ctx->moveTo(10 + slide, 30);
        ctx->quadraticCurveTo(28 + slide, 26, 46 + slide, 30);
        ctx->stroke();

        ctx->beginPath();
        ctx->moveTo(46 + slide, 30);
        ctx->quadraticCurveTo(50 + slide, 32, 46 + slide, 34);
        ctx->stroke();

        // Bottom swoosh
        ctx->beginPath();
        ctx->moveTo(16 + slide, 38);
        ctx->quadraticCurveTo(32 + slide, 34, 48 + slide, 38);
        ctx->stroke();

        ctx->beginPath();
        ctx->moveTo(48 + slide, 38);
        ctx->quadraticCurveTo(52 + slide, 40, 48 + slide, 42);
        ctx->stroke();
    }

public:
    WeatherWidget() {}

    // Expose state so your HTTP client can update it natively (e.g. WiFiClient/ArduinoJson)
    void updateWeather(int temp, int code, bool isDay) {
        weatherState.temp = temp;
        weatherState.code = code;
        weatherState.isDay = isDay;
        weatherState.loaded = true;
    }
    
    void setDemoMode(bool enable) {
        demoMode = enable;
    }

    // Call this in your main display loop
    void draw(GraphicsContext* ctx, Font* font, int width, int height, unsigned long currentTimeMs) {
        animTick++;

        // Draw Dithered Background
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint8_t r = ditherColor(0x00, x, y);
                uint8_t g = ditherColor(0x11, x, y);
                uint8_t b = ditherColor(0x22, x, y);
                ctx->setPixel(x, y, r, g, b);
            }
        }

        if (demoMode) {
            if (currentTimeMs - demoTimer >= 5000) {
                demoIndex = (demoIndex + 1) % DEMO_ICONS.size();
                demoTimer = currentTimeMs;
            }

            const std::string& icon = DEMO_ICONS[demoIndex];

            if (icon == "sun") drawSun(ctx);
            else if (icon == "moon") drawMoon(ctx);
            else if (icon == "cloud") drawCloud(ctx);
            else if (icon == "rain") drawRain(ctx);
            else if (icon == "snow") drawSnow(ctx);
            else if (icon == "lightning") drawLightning(ctx);
            else if (icon == "fog") drawFog(ctx);
            else if (icon == "wind") drawWind(ctx);

            ctx->setFillStyle(0xFFFFFF);
            std::string tempString = std::to_string(weatherState.temp) + "F";
            int textWidth = font->getTextWidth(tempString);
            int xOffset = ((width - textWidth) / 2) + 1;
            font->drawText(ctx, tempString, xOffset, 55);

            return;
        }

        if (!weatherState.loaded) {
            ctx->setFillStyle(0xFFFFFF);
            font->drawText(ctx, "LOADING", 10, 35);
            return;
        }

        int code = weatherState.code;

        if (code <= 1) {
            weatherState.isDay ? drawSun(ctx) : drawMoon(ctx);
        } else if (code <= 3) {
            drawCloud(ctx);
        } else if (code <= 45) {
            drawFog(ctx);
        } else if (code <= 55) {
            drawRain(ctx);
        } else if (code <= 65) {
            drawRain(ctx);
        } else if (code <= 75) {
            drawSnow(ctx);
        } else if (code <= 95) {
            drawLightning(ctx);
        } else {
            drawCloud(ctx);
        }

        ctx->setFillStyle(0xFFFFFF);
        std::string tempString = std::to_string(weatherState.temp) + "F";
        int textWidth = font->getTextWidth(tempString);
        int xOffset = ((width - textWidth) / 2) + 1;
        font->drawText(ctx, tempString, xOffset, 55);
    }
    bool fetchWeatherData() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Weather fetch aborted: WiFi not connected.");
        return false;
    }

    Serial.println("Fetching weather data via HTTP...");
    
    WiFiClient client;
    HTTPClient http;
    bool success = false;
    extern float prefLat;
    extern float prefLng;

    String weatherUrl = "http://api.open-meteo.com/v1/forecast?latitude=" + String(prefLat, 4) + 
                        "&longitude=" + String(prefLng, 4) + 
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
                
                updateWeather(temp, code, isDay == 1);
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
};

#endif // WEATHER_H