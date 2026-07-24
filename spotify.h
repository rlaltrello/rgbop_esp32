#pragma once
#include <string>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <mbedtls/base64.h> // Native ESP32 Base64 library


class SpotifyWidget {
private:
    std::string songTitle = "NOT PLAYING";
    std::string artistName = "";
    std::string fullDisplayStr = "NOT PLAYING";

    String refreshToken;
    
    // 64x48 RGB24 Buffer (64 * 48 * 3 = 9,216 bytes)
    uint8_t albumArtPixels[64 * 48 * 3];
    bool hasArt = false;
    bool isPlaying = false;
    float progressRatio = 0.0f; // 0.0 to 1.0
    
    // Styling
    uint32_t textColor = 0xFFFFFF;       // Default text: White
    uint32_t spotifyGreen = 0x1DB954;   // Spotify Brand Color
    
    // Cycle Tracking & Scrolling Variables
    int targetCycles = 1; 
    int currentCycles = 0; 
    
    float scrollX = 64.0f;
    int textWidth = 0;
    unsigned long lastUpdateMs = 0;

    String urlEncode(const String& value) {
    String encoded = "";
    const char* hex = "0123456789ABCDEF";

    for (size_t i = 0; i < value.length(); i++) {
        unsigned char c = static_cast<unsigned char>(value[i]);

        bool isAlphaNum =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9');

        if (isAlphaNum || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += char(c);
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }

    return encoded;
}

public:

    void begin(String token) {
        refreshToken = token;
        //Serial.printf("Spotify begin() token length: %u\n", refreshToken.length());
    }

void fetchSpotifyStatus() {
    // 1. Guard check WiFi first
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Spotify fetch aborted: WiFi not connected.");
        return;
    }

    // 2. Guard check token BEFORE touching network objects
    if (refreshToken.length() == 0) {
        Serial.println("Spotify fetch aborted: refresh token empty");
        return;
    }

    Serial.println("Fetching Spotify status from VPS...");

    // 3. Stack allocation (automatically deleted when function ends)
    WiFiClientSecure client;
    client.setInsecure(); 

    String url = "https://rgbop.com/spotify?refresh_token=" + urlEncode(refreshToken);

    HTTPClient http;
    if (http.begin(client, url)) { // Pass stack reference 'client'
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();

            JsonDocument doc; 
            DeserializationError error = deserializeJson(doc, payload);

            if (!error) {
                isPlaying = doc["is_playing"] | false;

                const char* title = doc["title"] | "No Song";
                const char* artist = doc["artist"] | "Unknown Artist";
                songTitle = title;
                artistName = artist;

                if (isPlaying) {
                    fullDisplayStr = songTitle + " - " + artistName;
                } else {
                    fullDisplayStr = "Spotify Paused";
                }

                progressRatio = doc["progress_ratio"] | 0.0f;
                if (progressRatio < 0.0f) progressRatio = 0.0f;
                if (progressRatio > 1.0f) progressRatio = 1.0f;

                if (doc.containsKey("cycles")) {
                    targetCycles = doc["cycles"].as<int>();
                    if (targetCycles < 1) targetCycles = 1; 
                }

                const char* artBase64 = doc["art_rgb24"];
                if (artBase64 && strlen(artBase64) > 0) {
                    size_t outLen = 0;
                    int ret = mbedtls_base64_decode(
                        albumArtPixels, 
                        sizeof(albumArtPixels), 
                        &outLen, 
                        (const unsigned char*)artBase64, 
                        strlen(artBase64)
                    );
                    hasArt = (ret == 0 && outLen == sizeof(albumArtPixels));
                } else {
                    hasArt = false;
                }

                Serial.printf("Spotify: %s | Playing: %s | Has Art: %s\n", 
                              fullDisplayStr.c_str(), 
                              isPlaying ? "YES" : "NO", 
                              hasArt ? "YES" : "NO");
            } else {
                Serial.print("Spotify JSON parse failed: ");
                Serial.println(error.c_str());
            }
        } else {
            String payload = http.getString();
            Serial.printf("Spotify HTTP failed: %d\n", httpCode);
            Serial.println(payload);
        }
        http.end(); // Clean up HTTP client
    }
}

    void resetScroll(int width) {
        scrollX = width;
        lastUpdateMs = 0;
        currentCycles = 0; 
    }

    template <typename CtxType, typename FontType>
    bool draw(CtxType* ctx, FontType* font, int width, int height, unsigned long nowMs) {
        
        // Calculate text width (scale 1 for the 16px bottom banner)
        textWidth = font->getTextWidth(fullDisplayStr, 1);

        // Delta-time scroll calculation
        if (lastUpdateMs == 0) lastUpdateMs = nowMs;
        float dt = (nowMs - lastUpdateMs) / 1000.0f;
        lastUpdateMs = nowMs;
        
        float scrollSpeed = 35.0f; 
        scrollX -= scrollSpeed * dt;

        if (scrollX < -textWidth) {
            scrollX = width; 
            currentCycles++; 
            
            if (currentCycles >= targetCycles) {
                return true; // Signal widget complete to main loop
            }
        }

        // -------------------------------------------------------------
        // 1. Draw 64x48 Album Art Panel (Y = 0 to 47)
        // -------------------------------------------------------------
        if (hasArt) {
            int byteIndex = 0;
            for (int y = 0; y < 48; y++) {
                for (int x = 0; x < 64; x++) {
                    uint8_t r = albumArtPixels[byteIndex++];
                    uint8_t g = albumArtPixels[byteIndex++];
                    uint8_t b = albumArtPixels[byteIndex++];

                    ctx->setPixel(x, y, r, g, b);
                }
            }
        } else {
            // Fallback: Dark background for art area if no art loaded
            ctx->setFillStyle(0x101010);
            ctx->fillRect(0, 0, 64, 48);
        }

        // -------------------------------------------------------------
        // 2. Clear Bottom Banner Area (Y = 48 to 63)
        // -------------------------------------------------------------
        ctx->setFillStyle(0x000000);
        ctx->fillRect(0, 48, 64, 16);

        // -------------------------------------------------------------
        // 3. Draw Scrolling Text in Banner Area
        // -------------------------------------------------------------
        ctx->setFillStyle(textColor);
        
        // Y position accounting for MatrixFont's -7 internal offset
        // (58 - 7 = 51, placing the top of the text safely at Y=51)
        int yPos = 58; 
        font->drawText(ctx, fullDisplayStr, static_cast<int>(scrollX), yPos, 1);

        // -------------------------------------------------------------
        // 4. Draw Progress Bar at very bottom (Y = 62 to 63)
        // -------------------------------------------------------------
        if (isPlaying && progressRatio > 0.0f) {
            float barWidth = 64.0f * progressRatio;
            ctx->setFillStyle(spotifyGreen); 
            ctx->fillRect(0, 62, static_cast<int>(barWidth), 2);
        }

        return false;
    }
};