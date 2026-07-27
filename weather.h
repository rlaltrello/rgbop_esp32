#ifndef WEATHER_H
#define WEATHER_H

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

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
                           uint32_t color) = 0;

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
    void drawSun(GraphicsContext* ctx, int ox) {
        float shimmer = std::sin(animTick * 0.05f) * 2.0f;
        float cx = 32 + ox;

        ctx->setFillStyle(0xFFDD00); // Format: 0xRRGGBB
        ctx->beginPath();
        ctx->arc(cx, 24, 10 + shimmer, 0, M_PI * 2);
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

        ray(cx, 24 - r, cx, 24 - (r + 6));
        ray(cx, 24 + r, cx, 24 + (r + 6));
        ray(cx - r, 24, cx - (r + 6), 24);
        ray(cx + r, 24, cx + (r + 6), 24);

        ray(cx - r * 0.7f, 24 - r * 0.7f, cx - (r + 6), 24 - (r + 6));
        ray(cx + r * 0.7f, 24 - r * 0.7f, cx + (r + 6), 24 - (r + 6));
        ray(cx - r * 0.7f, 24 + r * 0.7f, cx - (r + 6), 24 + (r + 6));
        ray(cx + r * 0.7f, 24 + r * 0.7f, cx + (r + 6), 24 + (r + 6));
    }

    void drawMoon(GraphicsContext* ctx, int ox) {
        float glow = std::sin(animTick * 0.04f) * 2.0f;
        float cx = 32 + ox;

        ctx->setFillStyle(0xDDDDDD);
        ctx->beginPath();
        ctx->arc(cx, 24, 10 + glow, 0, M_PI * 2);
        ctx->fill();

        ctx->setFillStyle(0x001122);
        ctx->beginPath();
        ctx->arc(cx + 4, 20, 10 + glow, 0, M_PI * 2);
        ctx->fill();
    }

    void drawCloud(GraphicsContext* ctx, int ox) {
        float drift = std::sin(animTick * 0.03f) * 2.0f;
        float scale = 0.16f;
        float cloudOx = ox + 1 + drift;
        float oy = 4;

        ctx->setFillStyle(0xD0D0E0);
        ctx->setStrokeStyle(0x555566);

        ctx->beginPath();

        ctx->arc((100 * scale) + cloudOx, (135 * scale) + oy, 60 * scale, M_PI * 0.5, M_PI * 1.5);
        ctx->arc((170 * scale) + cloudOx, (75 * scale) + oy,  70 * scale, M_PI * 1.0, M_PI * 1.85);
        ctx->arc((252 * scale) + cloudOx, (90 * scale) + oy,  50 * scale, M_PI * 1.37, M_PI * 1.91);
        ctx->arc((300 * scale) + cloudOx, (135 * scale) + oy, 60 * scale, M_PI * 1.5, M_PI * 0.5);

        // Draw the bottom flat edge of the cloud
        float rightX = (300 * scale) + cloudOx;
        float leftX = (100 * scale) + cloudOx;
        float baseY = (195 * scale) + oy;

        ctx->moveTo(rightX, baseY);
        ctx->lineTo(leftX, baseY);

        ctx->closePath();
        ctx->fill();

        ctx->setFillStyle(0xD0D0E0); // Cloud body color
        ctx->fillRect(leftX + 1, baseY - 3, (rightX - leftX) - 2, 3);

        ctx->stroke();
    }

    void drawSnow(GraphicsContext* ctx, int ox) {
        drawCloud(ctx, ox);
        int fall = (animTick % 20);

        ctx->setFillStyle(0xFFFFFF);
        ctx->fillRect(ox + 26, 32 + fall, 2, 2);
        ctx->fillRect(ox + 32, 34 + fall, 2, 2);
        ctx->fillRect(ox + 38, 32 + fall, 2, 2);
    }

    void drawRain(GraphicsContext* ctx, int ox) {
        drawCloud(ctx, ox);

        ctx->setStrokeStyle(0x66A8FF);

        int drops[5][3] = {
            {22, 0,  4},
            {30, 8,  6},
            {38, 3,  5},
            {46, 12, 3},
            {54, 6,  5}
        };

        for (int i = 0; i < 5; i++) {
            int dropX = ox + drops[i][0];
            int yOff = drops[i][1];
            int len = drops[i][2];

            int fall = (animTick + yOff) % 24; 
            
            int startY = 32 + fall;
            int endY = startY + len;

            if (startY > 50) continue; 
            if (endY > 50) endY = 50;

            ctx->beginPath();
            ctx->moveTo(dropX, startY);
            ctx->lineTo(dropX - (len / 3), endY); 
            ctx->stroke();
        }
    }

    void drawLightning(GraphicsContext* ctx, int ox) {
        drawCloud(ctx, ox);

        bool flash = (animTick % 30) < 4;
        if (!flash) return;

        ctx->setStrokeStyle(0xFFFF55);

        // Strike 1
        ctx->beginPath();
        ctx->moveTo(ox + 32, 18);
        ctx->lineTo(ox + 40, 30);
        ctx->lineTo(ox + 30, 42);
        ctx->lineTo(ox + 44, 54);
        ctx->lineTo(ox + 34, 66);
        ctx->stroke();

        // Strike 2 (Thickness boost)
        ctx->beginPath();
        ctx->moveTo(ox + 33, 18);
        ctx->lineTo(ox + 41, 30);
        ctx->lineTo(ox + 31, 42);
        ctx->lineTo(ox + 45, 54);
        ctx->lineTo(ox + 35, 66);
        ctx->stroke();
    }

    void drawFog(GraphicsContext* ctx, int ox) {
        float slide = std::sin(animTick * 0.05f) * 4.0f;

        ctx->setFillStyle(0xCCCCCC);
        ctx->fillRect(ox + 16 + slide, 28, 32, 3);
        ctx->fillRect(ox + 12 + slide, 34, 40, 3);
        ctx->fillRect(ox + 20 + slide, 40, 28, 3);
    }

    void drawWind(GraphicsContext* ctx, int ox) {
        float slide = std::sin(animTick * 0.05f) * 4.0f;

        ctx->setStrokeStyle(0xFFFFFF);
        ctx->setLineWidth(3);

        // Top swoosh
        ctx->beginPath();
        ctx->moveTo(ox + 14 + slide, 22);
        ctx->quadraticCurveTo(ox + 30 + slide, 16, ox + 50 + slide, 22);
        ctx->stroke();

        ctx->beginPath();
        ctx->moveTo(ox + 50 + slide, 22);
        ctx->quadraticCurveTo(ox + 54 + slide, 24, ox + 50 + slide, 26);
        ctx->stroke();

        // Middle swoosh
        ctx->beginPath();
        ctx->moveTo(ox + 10 + slide, 30);
        ctx->quadraticCurveTo(ox + 28 + slide, 26, ox + 46 + slide, 30);
        ctx->stroke();

        ctx->beginPath();
        ctx->moveTo(ox + 46 + slide, 30);
        ctx->quadraticCurveTo(ox + 50 + slide, 32, ox + 46 + slide, 34);
        ctx->stroke();

        // Bottom swoosh
        ctx->beginPath();
        ctx->moveTo(ox + 16 + slide, 38);
        ctx->quadraticCurveTo(ox + 32 + slide, 34, ox + 48 + slide, 38);
        ctx->stroke();

        ctx->beginPath();
        ctx->moveTo(ox + 48 + slide, 38);
        ctx->quadraticCurveTo(ox + 52 + slide, 40, ox + 48 + slide, 42);
        ctx->stroke();
    }

public:
    WeatherWidget() {}

    void updateWeather(int temp, int code, bool isDay) {
        weatherState.temp = temp;
        weatherState.code = code;
        weatherState.isDay = isDay;
        weatherState.loaded = true;
    }
    
    void setDemoMode(bool enable) {
        demoMode = enable;
    }

    void draw(GraphicsContext* ctx, Font* font, int width, int height, unsigned long currentTimeMs) {
        animTick++;

        // Calculate dynamic horizontal centering offset for art
        int artOffsetX = (width - 64) / 2;
        if (artOffsetX < 0) artOffsetX = 0;

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

            if (icon == "sun") drawSun(ctx, artOffsetX);
            else if (icon == "moon") drawMoon(ctx, artOffsetX);
            else if (icon == "cloud") drawCloud(ctx, artOffsetX);
            else if (icon == "rain") drawRain(ctx, artOffsetX);
            else if (icon == "snow") drawSnow(ctx, artOffsetX);
            else if (icon == "lightning") drawLightning(ctx, artOffsetX);
            else if (icon == "fog") drawFog(ctx, artOffsetX);
            else if (icon == "wind") drawWind(ctx, artOffsetX);

            ctx->setFillStyle(0xFFFFFF);
            std::string tempString = std::to_string(weatherState.temp) + "F";
            int textWidth = font->getTextWidth(tempString);
            int xOffset = (width - textWidth) / 2;
            font->drawText(ctx, tempString, xOffset, 55);

            return;
        }

        if (!weatherState.loaded) {
            ctx->setFillStyle(0xFFFFFF);
            std::string loadText = "LOADING";
            int textWidth = font->getTextWidth(loadText);
            font->drawText(ctx, loadText, (width - textWidth) / 2, 35);
            return;
        }

        int code = weatherState.code;

        if (code <= 1) {
            weatherState.isDay ? drawSun(ctx, artOffsetX) : drawMoon(ctx, artOffsetX);
        } else if (code <= 3) {
            drawCloud(ctx, artOffsetX);
        } else if (code <= 45) {
            drawFog(ctx, artOffsetX);
        } else if (code <= 55) {
            drawRain(ctx, artOffsetX);
        } else if (code <= 65) {
            drawRain(ctx, artOffsetX);
        } else if (code <= 75) {
            drawSnow(ctx, artOffsetX);
        } else if (code <= 95) {
            drawLightning(ctx, artOffsetX);
        } else {
            drawCloud(ctx, artOffsetX);
        }

        ctx->setFillStyle(0xFFFFFF);
        std::string tempString = std::to_string(weatherState.temp) + "F";
        int textWidth = font->getTextWidth(tempString);
        int xOffset = (width - textWidth) / 2;
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
        
        Serial.println("Weather URL: " + weatherUrl);
        
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
                    success = true;
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