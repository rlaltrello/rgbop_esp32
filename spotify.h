#pragma once
#include <string>
#include <vector>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <mbedtls/base64.h> // Native ESP32 Base64 library

extern int actualPanelX;
extern int actualPanelY;

class SpotifyWidget {
private:
    std::string songTitle = "NOT PLAYING";
    std::string artistName = "";
    std::string fullDisplayStr = "NOT PLAYING";

    String refreshToken;
    
    // Dynamically sized buffer: allocated during begin()
    std::vector<uint8_t> albumArtPixels;
    bool hasArt = false;
    bool isPlaying = false;
    float progressRatio = 0.0f; // 0.0 to 1.0
    
    // Styling
    uint32_t textColor = 0xFFFFFF;       // Default text: White
    uint32_t spotifyGreen = 0x1DB954;   // Spotify Brand Color
    
    // Cycle Tracking & Scrolling Variables
    int targetCycles = 1; 
    int currentCycles = 0; 
    
    float scrollX = 0.0f;
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
        // Ensure vector has a default fallback capacity based on current panel dimensions
        int artHeight = (actualPanelY > 16) ? (actualPanelY - 16) : 48;
        albumArtPixels.resize(actualPanelX * artHeight * 3, 0);  
        scrollX = (float)actualPanelX;
    }

    void fetchSpotifyStatus() {
        if (gameManager.isGameModeActive()) return;
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Spotify fetch aborted: WiFi not connected.");
            return;
        }

        if (refreshToken.length() == 0) {
            Serial.println("Spotify fetch aborted: refresh token empty");
            return;
        }

        Serial.println("Fetching Spotify status from VPS...");

        WiFiClientSecure client;
        client.setInsecure(); 

        String url = "https://rgbop.com/spotify?refresh_token=" + urlEncode(refreshToken);

        HTTPClient http;
        if (http.begin(client, url)) {
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
                        size_t base64Len = strlen(artBase64);
                        // Base64 decodes into roughly 3/4 of the original string size
                        size_t maxDecodedLen = (base64Len * 3) / 4 + 4;

                        if (albumArtPixels.size() < maxDecodedLen) {
                            albumArtPixels.resize(maxDecodedLen);
                        }

                        size_t outLen = 0;
                        int ret = mbedtls_base64_decode(
                          albumArtPixels.data(), 
                          albumArtPixels.size(), 
                          &outLen, 
                          (const unsigned char*)artBase64, 
                          base64Len
                        );

                        // Accept any valid decode that produced data
                        hasArt = (ret == 0 && outLen > 0);
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
            http.end();
        }
    }

    void resetScroll(int width) {
        scrollX = width;
        lastUpdateMs = 0;
        currentCycles = 0; 
    }

    template <typename CtxType, typename FontType>
    bool draw(CtxType* ctx, FontType* font, int width, int height, unsigned long nowMs) {
        
        textWidth = font->getTextWidth(fullDisplayStr, 1);

        if (lastUpdateMs == 0) lastUpdateMs = nowMs;
        float dt = (nowMs - lastUpdateMs) / 1000.0f;
        lastUpdateMs = nowMs;
        
        float scrollSpeed = 35.0f; 
        scrollX -= scrollSpeed * dt;

        if (scrollX < -textWidth) {
            scrollX = width; 
            currentCycles++; 
            
            if (currentCycles >= targetCycles) {
                return true; 
            }
        }

        int artHeight = height - 16; // Save bottom 16px for text + progress bar

// -------------------------------------------------------------
// 1. Draw Centered Album Art Panel
// -------------------------------------------------------------
const int ART_WIDTH = 64;
const int ART_HEIGHT = 48; // artHeight is actualPanelY - 16

// Calculate left margin to center a 64px image on 'width' (128px => offset is 32px)
int offsetX = (width - ART_WIDTH) / 2;
if (offsetX < 0) offsetX = 0;

if (hasArt && !albumArtPixels.empty()) {
    size_t byteIndex = 0;
    size_t totalBytes = albumArtPixels.size();

    for (int y = 0; y < artHeight; y++) {
        for (int x = 0; x < width; x++) {
            // Check if current X position falls inside the centered 64px album art zone
            if (x >= offsetX && x < (offsetX + ART_WIDTH) && y < ART_HEIGHT) {
                if (byteIndex + 2 < totalBytes) {
                    uint8_t r = albumArtPixels[byteIndex++];
                    uint8_t g = albumArtPixels[byteIndex++];
                    uint8_t b = albumArtPixels[byteIndex++];
                    ctx->setPixel(x, y, r, g, b);
                } else {
                    ctx->setPixel(x, y, 0, 0, 0);
                }
            } else {
                // Dark background padding on the left and right sides of the centered art
                ctx->setPixel(x, y, 16, 16, 16); 
            }
        }
    }
} else {
    // Fallback: Dark background for the entire upper area if no art loaded
    ctx->setFillStyle(0x101010);
    ctx->fillRect(0, 0, width, artHeight);
}

        // -------------------------------------------------------------
        // 2. Clear Bottom Banner Area (Y = artHeight to height)
        // -------------------------------------------------------------
        ctx->setFillStyle(0x000000);
        ctx->fillRect(0, artHeight, width, 16);

        // -------------------------------------------------------------
        // 3. Draw Scrolling Text in Banner Area
        // -------------------------------------------------------------
        ctx->setFillStyle(textColor);
        
        // Dynamically compute baseline Y offset (accounts for height variations)
        int yPos = height - 6; 
        font->drawText(ctx, fullDisplayStr, static_cast<int>(scrollX), yPos, 1);

        // -------------------------------------------------------------
        // 4. Draw Progress Bar at very bottom (2px thick)
        // -------------------------------------------------------------
        if (isPlaying && progressRatio > 0.0f) {
            float barWidth = (float)width * progressRatio;
            ctx->setFillStyle(spotifyGreen); 
            ctx->fillRect(0, height - 2, static_cast<int>(barWidth), 2);
        }

        return false;
    }
};