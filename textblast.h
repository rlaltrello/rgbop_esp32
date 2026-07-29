// textblast.h
#pragma once
#include <string>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

extern int prefTextBlastTextScale;
extern bool prefTextBlastTextCustomMessage;
extern String prefTextBlastText;
extern int prefTextBlastTextColor;
extern int prefTextBlastBackgroundColor;
extern int prefTextBlastCycles;
extern float prefTextBlastSpeed;

class TextBlastWidget {
private:
    std::string currentMessage = "LOADING...";
    std::string currentFont = "normal"; 
    uint32_t currentColor = 0xFFFF00;     // Default text: Yellow
    uint32_t currentBackground = 0x000000; // Default background: Black
    int currentScale = 1;
    
    // Cycle Tracking Variables
    int targetCycles = 1; 
    int currentCycles = 0; 
    
    float scrollX = 128.0;
    int textWidth = 0;
    unsigned long lastUpdateMs = 0;

public:
    void fetchMessage() {
        if (gameManager.isGameModeActive()) return;
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Fetching TextBlast message...");
            WiFiClient client;
            HTTPClient http;
            
            http.begin(client, "http://laltrello.com:8084/");
            int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                
                JsonDocument doc; 
                DeserializationError error = deserializeJson(doc, payload);
                
                if (!error) {
                    const char* msg = doc["message"];
                    if (msg) currentMessage = msg;
                    
                    const char* fontStr = doc["font"];
                    if (fontStr) currentFont = fontStr;
                    
                    // Parse Text Color
                    const char* colorStr = doc["color"];
                    if (colorStr && colorStr[0] == '#') {
                        currentColor = strtoul(colorStr + 1, NULL, 16); 
                    }

                    // --- Parse Background Color ---
                    const char* bgStr = doc["background"];
                    if (bgStr && bgStr[0] == '#') {
                        currentBackground = strtoul(bgStr + 1, NULL, 16); 
                    }

                    // Parse Scale
                    if (doc.containsKey("scale")) {
                        currentScale = doc["scale"].as<int>();
                        if (currentScale < 1) currentScale = 1; 
                    }

                    // Parse Cycles
                    if (doc.containsKey("cycles")) {
                        targetCycles = doc["cycles"].as<int>();
                        if (targetCycles < 1) targetCycles = 1; 
                    }

                    if (prefTextBlastTextCustomMessage) {
                        currentMessage = prefTextBlastText.c_str();
                    }
                    
                    Serial.printf("TextBlast: %s | Color: 0x%06X | BG: 0x%06X | Font: %s | Scale: %d | Cycles: %d\n", 
                                  currentMessage.c_str(), prefTextBlastTextColor, prefTextBlastBackgroundColor, currentFont.c_str(), prefTextBlastTextScale, prefTextBlastCycles);
                } else {
                    Serial.print("TextBlast JSON parse failed: ");
                    Serial.println(error.c_str());
                }
            } else {
                Serial.printf("TextBlast HTTP failed: %d\n", httpCode);
            }
            http.end();
        } else {
            Serial.println("TextBlast fetch aborted: WiFi not connected.");
        }
    }

    void resetScroll(int width) {
        scrollX = width;
        lastUpdateMs = 0;
        currentCycles = 0; 
    }

    template <typename CtxType, typename FontType, typename MarioFontType>
    bool draw(CtxType* ctx, FontType* normalFont, MarioFontType* marioFont, int width, int height, unsigned long nowMs) {
        
        if (currentFont == "mario") {
            textWidth = marioFont->getTextWidth(currentMessage, prefTextBlastTextScale);
        } else {
            textWidth = normalFont->getTextWidth(currentMessage, prefTextBlastTextScale);
        }

        if (lastUpdateMs == 0) lastUpdateMs = nowMs;
        float dt = (nowMs - lastUpdateMs) / 1000.0f;
        lastUpdateMs = nowMs;
        
        //float scrollSpeed = 40.0f; 
        scrollX -= prefTextBlastSpeed * dt;

        if (scrollX < -textWidth) {
            scrollX = width; 
            currentCycles++; 
            
            if (currentCycles >= prefTextBlastCycles) {
                return true; 
            }
        }

        // --- Draw Background dynamically ---
        ctx->setFillStyle(prefTextBlastBackgroundColor);
        ctx->fillRect(0, 0, width, height);
        
        // Set Text Color
        ctx->setFillStyle(prefTextBlastTextColor);

        // --- Dynamic Vertical Centering Math ---
        // Center of the 64px display is Y=32. We adjust based on font height (7 or 8px) * scale.
        if (currentFont == "mario") {
            // Mario glyphs have a -6 Y offset (meaning they draw upwards from the cursor).
            // To center it, the cursor needs to be pushed down proportionally as it scales up.
            int yPos = 32 + static_cast<int>(2.5 * prefTextBlastTextScale);
            marioFont->drawText(ctx, currentMessage, static_cast<int>(scrollX), yPos, prefTextBlastTextScale);
        } else {
            // The Adafruit font draws top-down. The MatrixFont wrapper subtracts 7 internally.
            // True center = 32 - (total_height / 2). Total height = 8 * scale.
            int yPos = 39 - (4 * prefTextBlastTextScale);
            normalFont->drawText(ctx, currentMessage, static_cast<int>(scrollX), yPos, prefTextBlastTextScale);
        }

        return false;
    }
};