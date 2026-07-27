// radar.h
#pragma once
#include <time.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

enum class RadarTimeFormat { OFF, FORMAT_12H, FORMAT_24H };
enum class RadarUnitFormat { OFF, KM, MI };

extern float prefLat;
extern float prefLng;
extern int prefRadarZoomLevel;
extern RadarTimeFormat prefRadarTimeFormat;
extern RadarUnitFormat prefRadarUnitFormat;

// --- BASE64 & RGB24 DECODER HELPERS ---
namespace RadarUtils {
    inline std::vector<uint8_t> base64Decode(const std::string& input) {
        static const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";
        std::vector<uint8_t> out;
        out.reserve((input.size() * 3) / 4);
        
        int val = 0, valb = -8;
        for (unsigned char c : input) {
            if (c == '=') break;
            auto pos = base64_chars.find(c);
            if (pos == std::string::npos) continue;
            val = (val << 6) + static_cast<int>(pos);
            valb += 6;
            if (valb >= 0) {
                out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return out;
    }

    // Automatically handles 3-byte (RGB) and 4-byte (RGBA) raw byte streams
    inline std::vector<uint32_t> rgb24ToUint32(const std::vector<uint8_t>& rawBytes) {
        std::vector<uint32_t> pixels;
        if (rawBytes.empty()) return pixels;

        // Determine byte stride: 4 bytes (RGBA) if raw buffer >= 16384 bytes, else 3 (RGB)
        size_t stride = (rawBytes.size() >= 16384) ? 4 : 3;
        pixels.reserve(rawBytes.size() / stride);

        for (size_t i = 0; i + (stride - 1) < rawBytes.size(); i += stride) {
            uint32_t r = rawBytes[i];
            uint32_t g = rawBytes[i + 1];
            uint32_t b = rawBytes[i + 2];
            // alpha is rawBytes[i + 3] (ignored)

            uint32_t color = (r << 16) | (g << 8) | b;
            pixels.push_back(color);
        }
        return pixels;
    }
}

struct RadarFrame {
    time_t timestamp = 0;
    // 64x64 array of 0xRRGGBB colors
    std::vector<uint32_t> pixelData; 
};

class RadarWidget {
private:
    double latitude = prefLat;
    double longitude = prefLng;
    int zoomLevel = prefRadarZoomLevel;
    int frameDelayMs = 250;
    RadarTimeFormat timeFormat = prefRadarTimeFormat;
    RadarUnitFormat unitFormat = prefRadarUnitFormat;

    std::vector<RadarFrame> frames;
    size_t currentFrameIndex = 0;
    unsigned long lastFrameTimeMs = 0;

    static constexpr double ZOOM_M_PER_PX[13] = {
        156543.0339, 78271.51696, 39135.75848, 19567.87924,
        9783.939620,  4891.969810,  2445.984905, 1222.992452,
         611.4962263,  305.7481131,  152.8740566,   76.43702829,
          38.21851414
    };

public:
void syncConfig() {
        latitude = prefLat;
        longitude = prefLng;
        zoomLevel = prefRadarZoomLevel;
        timeFormat = prefRadarTimeFormat;
        unitFormat = prefRadarUnitFormat;
    }
bool fetch() {
        if (gameManager.isGameModeActive()) return false;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[Radar] Error: WiFi not connected");
            return false;
        }

        WiFiClientSecure client;
        client.setInsecure(); // Skip SSL cert verification

        HTTPClient http;

        char url[160];
        snprintf(url, sizeof(url), 
                 "https://rgbop.com/api/radar?lat=%.4f&lon=%.4f&zoom=%d", 
                 latitude, longitude, zoomLevel);

        //Serial.printf("\n[Radar] Requesting: %s\n", url);
        //Serial.printf("[Radar] Free Heap before request: %u bytes\n", ESP.getFreeHeap());

        if (!http.begin(client, url)) {
            Serial.println("[Radar] Error: HTTP begin failed");
            return false;
        }


        http.setTimeout(10000);
        int httpCode = http.GET();

        Serial.printf("[Radar] HTTP Response Code: %d\n", httpCode);

        if (httpCode != HTTP_CODE_OK) {
            Serial.printf("[Radar] Error: HTTP GET failed (%s)\n", http.errorToString(httpCode).c_str());
            http.end();
            return false;
        }

        // Buffer response string
        String payload = http.getString();
        http.end();

        Serial.printf("[Radar] Payload received: %u bytes\n", payload.length());

        // Parse JSON
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.printf("[Radar] Error: JSON deserialization failed (%s)\n", error.c_str());
            return false;
        }

        clearFrames();

        // 1. Array Response: [{"timestamp":..., "art_rgb24":"..."}, ...]
        if (doc.is<JsonArray>()) {
            JsonArray frameArray = doc.as<JsonArray>();
            for (JsonObject frameObj : frameArray) {
                time_t ts = frameObj["timestamp"] | 0;
                const char* b64Data = frameObj["art_rgb24"] | "";
                if (strlen(b64Data) > 0) {
                    addBase64Frame(ts, std::string(b64Data));
                }
            }
        } 
        // 2. Object Response
        else if (doc.is<JsonObject>()) {
            JsonObject root = doc.as<JsonObject>();

            // Single Frame Object: {"timestamp":..., "art_rgb24":"..."}
            if (root.containsKey("art_rgb24")) {
                time_t ts = root["timestamp"] | 0;
                const char* b64Data = root["art_rgb24"] | "";
                if (strlen(b64Data) > 0) {
                    addBase64Frame(ts, std::string(b64Data));
                }
            } 
            // Wrapped Array Object: {"frames": [{...}]}
            else if (root.containsKey("frames") && root["frames"].is<JsonArray>()) {
                for (JsonObject frameObj : root["frames"].as<JsonArray>()) {
                    time_t ts = frameObj["timestamp"] | 0;
                    const char* b64Data = frameObj["art_rgb24"] | "";
                    if (strlen(b64Data) > 0) {
                        addBase64Frame(ts, std::string(b64Data));
                    }
                }
            }
        }

        Serial.printf("[Radar] Successfully ingested %u frame(s). Free Heap: %u bytes\n", 
                      frames.size(), ESP.getFreeHeap());

        return !frames.empty();
    }

    RadarWidget() = default;

    // --- CONFIGURATION SETTERS ---
    void setLocation(double lat, double lng) { latitude = lat; longitude = lng; }
    void setZoomLevel(int zoom) { zoomLevel = std::max(0, std::min(zoom, 12)); }
    void setFrameDelay(int delayMs) { frameDelayMs = std::max(50, delayMs); }
    void setTimeFormat(RadarTimeFormat fmt) { timeFormat = fmt; }
    void setUnitFormat(RadarUnitFormat fmt) { unitFormat = fmt; }

    // --- FRAME INGESTION ---
    void clearFrames() {
        frames.clear();
        currentFrameIndex = 0;
    }

    // Ingest directly from Node API base64 payload
    void addBase64Frame(time_t timestamp, const std::string& base64Rgb24) {
        if (base64Rgb24.empty()) return;

        std::vector<uint8_t> rawBytes = RadarUtils::base64Decode(base64Rgb24);
        std::vector<uint32_t> pixels = RadarUtils::rgb24ToUint32(rawBytes);

        frames.push_back({timestamp, pixels});
    }

    // Ingest raw uint32 vector directly
    void addFrame(time_t timestamp, const std::vector<uint32_t>& pixels) {
        frames.push_back({timestamp, pixels});
    }

    void setFrames(const std::vector<RadarFrame>& newFrames) {
        frames = newFrames;
        currentFrameIndex = 0;
    }

    void getZoomCoverage(int& kmOut, int& miOut) const {
    // Adjust ground resolution for latitude in radians
    double latRad = latitude * M_PI / 180.0;
    double mPerPx = ZOOM_M_PER_PX[zoomLevel] * cos(latRad);
    
    // Calculate actual coverage for 64 pixels width
    double kmTotal = (64.0 * mPerPx) / 1000.0;
    
    kmOut = static_cast<int>(round(kmTotal));
    miOut = static_cast<int>(round(kmTotal * 0.621371));
}

    std::string formatTime(time_t ts) const {
        if (timeFormat == RadarTimeFormat::OFF || ts == 0) return "";
        struct tm* parts = localtime(&ts);
        if (!parts) return "";
        char buf[32];
        if (timeFormat == RadarTimeFormat::FORMAT_12H) {
            strftime(buf, sizeof(buf), "%I:%M %p", parts);
            if (buf[0] == '0') return std::string(buf + 1);
        } else {
            strftime(buf, sizeof(buf), "%H:%M", parts);
        }
        return std::string(buf);
    }

    // --- MAIN DRAW METHOD ---
    template <typename CtxType, typename FontType>
    void draw(CtxType* ctx, FontType* font, int width, int height, unsigned long nowMs) {

        // 1. Clear Screen
        ctx->setFillStyle(0x000000);
        ctx->fillRect(0, 0, width, height);

        // 2. Advance Animation Frame (if multiple frames loaded)
        if (!frames.empty()) {
            if (nowMs - lastFrameTimeMs >= static_cast<unsigned long>(frameDelayMs)) {
                currentFrameIndex = (currentFrameIndex + 1) % frames.size();
                lastFrameTimeMs = nowMs;
            }

            // 3. DRAW RADAR MAP PIXELS
            const auto& frame = frames[currentFrameIndex];
            if (!frame.pixelData.empty()) {
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        size_t idx = y * width + x;
                        if (idx < frame.pixelData.size()) {
                            uint32_t color = frame.pixelData[idx];
                            // Only draw non-black pixels
                            if (color != 0x000000) {
                                ctx->setFillStyle(color);
                                ctx->fillRect(x, y, 1, 1);
                            }
                        }
                    }
                }
            }
        }

        // 4. Draw Center Red Target Dot (3x3)
        int centerX = width / 2;
        int centerY = height / 2;
        ctx->setFillStyle(0xFF0000); // Red
        ctx->fillRect(centerX - 1, centerY - 1, 3, 3);

        // 5. Draw Time Overlay Banner
        if (timeFormat != RadarTimeFormat::OFF && !frames.empty()) {
            std::string timeText = formatTime(frames[currentFrameIndex].timestamp);
            if (!timeText.empty()) {
                ctx->setFillStyle(0x000000);
                ctx->fillRect(0, 0, width, 8); // Expanded from 7 to 8
                ctx->setFillStyle(0xFFFFFF);
                font->drawText(ctx, timeText, 2, 7); // Shifted Y down from 6 to 7
            }
        }

        // 6. Draw Distance Unit Banner
        if (unitFormat != RadarUnitFormat::OFF) {
            int km = 0, mi = 0;
            getZoomCoverage(km, mi);
            std::string unitText = (unitFormat == RadarUnitFormat::KM)
                ? (std::to_string(km) + " km")
                : (std::to_string(mi) + " mi");

            int barY = height - 8; // Expanded from 7 to 8
            ctx->setFillStyle(0x000000);
            ctx->fillRect(0, barY, width, 8);
            ctx->setFillStyle(0xFFFFFF);
            font->drawText(ctx, unitText, 2, height - 1);
        }
    }
};