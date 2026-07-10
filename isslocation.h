#ifndef ISSLOCATION_H
#define ISSLOCATION_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Fonts/TomThumb.h>

extern GFXcanvas16 weatherCanvas; 

class IssLocationWidget {
private:
    String currentLat = "0.0";
    String currentLon = "0.0";
    String locationName = "Fetching...";
    String wrappedLocation = "Fetching..."; 
    uint16_t locationColor = 0xFFFF; 

    uint16_t hexTo565(uint32_t color) {
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }

    // Word wrap based on character counts
    String wrapText(String text, int maxCharsPerLine) {
        String wrappedText = "";
        String currentLine = "";
        
        text.replace('\xA0', ' ');
        text.trim();
        
        int startIdx = 0;
        int spaceIdx = text.indexOf(' ');

        while (startIdx < text.length()) {
            String word;
            if (spaceIdx == -1) {
                word = text.substring(startIdx);
                startIdx = text.length();
            } else {
                word = text.substring(startIdx, spaceIdx);
                startIdx = spaceIdx + 1;
                spaceIdx = text.indexOf(' ', startIdx);
            }

            if (word.length() > maxCharsPerLine) {
                if (currentLine.length() > 0) {
                    if (wrappedText.length() > 0) wrappedText += "\n";
                    wrappedText += currentLine;
                    currentLine = "";
                }
                for (int i = 0; i < word.length(); i += maxCharsPerLine) {
                    if (wrappedText.length() > 0) wrappedText += "\n";
                    wrappedText += word.substring(i, i + maxCharsPerLine);
                }
                continue;
            }

            if (currentLine.length() == 0) {
                currentLine = word;
            } else if (currentLine.length() + 1 + word.length() <= maxCharsPerLine) {
                currentLine += " " + word;
            } else {
                if (wrappedText.length() > 0) wrappedText += "\n";
                wrappedText += currentLine;
                currentLine = word;
            }
        }

        if (currentLine.length() > 0) {
            if (wrappedText.length() > 0) wrappedText += "\n";
            wrappedText += currentLine;
        }

        return wrappedText;
    }

    // Cleans the string of any non-standard ASCII/UTF-8 characters
    String sanitizeText(String text) {
        String clean = "";
        for (int i = 0; i < text.length(); i++) {
            char c = text[i];
            // Only keep standard printable ASCII characters (space through tilde)
            if (c >= 32 && c <= 126) {
                clean += c;
            }
        }
        
        // Clean up any double spaces created by stripping characters
        clean.replace("  ", " ");
        return clean;
    }

public:
    bool fetchISSData() {
        if (WiFi.status() != WL_CONNECTED) return false;
        
        Serial.println("Fetching ISS Location...");
        HTTPClient http;
        bool issSuccess = false;
        
        for (int i = 0; i < 3; i++) {
            http.begin("http://api.open-notify.org/iss-now.json");
            int httpCode = http.GET();
            
            if (httpCode == HTTP_CODE_OK) {
                JsonDocument doc;
                deserializeJson(doc, http.getString());
                const char* lat = doc["iss_position"]["latitude"];
                const char* lon = doc["iss_position"]["longitude"];
                
                currentLat = String(lat).substring(0, 7);
                currentLon = String(lon).substring(0, 7);
                issSuccess = true;
                http.end();
                break;
            } else {
                Serial.printf("ISS API failed (HTTP %d). Retry %d/3...\n", httpCode, i + 1);
                http.end();
                delay(1000); 
            }
        }

        if (!issSuccess) {
            Serial.println("Failed to retrieve ISS coordinates.");
            return false;
        }

        String geoUrl = "http://api.geonames.org/findNearbyPlaceNameJSON?username=rlaltrel&lat=" + currentLat + "&lng=" + currentLon;
        http.begin(geoUrl);
        int httpCode = http.GET();
        bool onLand = false;
        
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            deserializeJson(doc, http.getString());
            JsonArray geonames = doc["geonames"];
            
            if (geonames.size() > 0) {
                String city = geonames[0]["name"] | "";
                String country = geonames[0]["countryName"] | "";
                String countryCode = geonames[0]["countryCode"] | "";
                
                if (countryCode == "US") {
                    country = geonames[0]["adminName1"].as<String>();
                }
                
                locationName = city + " " + country;
                locationName.trim();
                locationColor = hexTo565(0xE29315); 
                onLand = true;
            }
        }
        http.end();

        if (!onLand) {
            String oceanUrl = "http://api.geonames.org/oceanJSON?username=rlaltrel&lat=" + currentLat + "&lng=" + currentLon;
            http.begin(oceanUrl);
            httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                JsonDocument doc;
                deserializeJson(doc, payload);
                
                if (payload.indexOf("afraid") >= 0 || !doc.containsKey("ocean")) {
                    locationName = "Unknown Location";
                    locationColor = hexTo565(0x444444); 
                } else {
                    locationName = doc["ocean"]["name"].as<String>();
                    locationColor = hexTo565(0x33A2FF); 
                }
            }
            http.end();
        }

        // Strip out the UTF-8 garbage before wrapping!
        locationName = sanitizeText(locationName);
        
        wrappedLocation = wrapText(locationName, 10);
        
        Serial.printf("ISS Location Updated: %s\n", locationName.c_str());
        return true; 
    }

    void draw() {
        weatherCanvas.fillScreen(0x0000); 

        // 1. TOP SECTION: "ISS" Blue Badge
        weatherCanvas.fillRect(0, 0, 24, 11, hexTo565(0x2D38BF));
        weatherCanvas.setFont(); 
        weatherCanvas.setTextWrap(false);
        weatherCanvas.setTextSize(1);
        weatherCanvas.setTextColor(0x0000); 
        weatherCanvas.setCursor(3, 2); 
        weatherCanvas.print("ISS");

        // First Divider Line
        weatherCanvas.drawLine(0, 13, 64, 13, hexTo565(0x222222));

        // 2. MIDDLE SECTION: Coordinates
        weatherCanvas.setFont(&TomThumb);
        weatherCanvas.setTextColor(hexTo565(0x888888));
        
        weatherCanvas.setCursor(1, 20); 
        weatherCanvas.print("Lat: ");
        weatherCanvas.print(currentLat);
        
        weatherCanvas.setCursor(1, 27);
        weatherCanvas.print("Lon: ");
        weatherCanvas.print(currentLon);

        // Second Divider Line
        weatherCanvas.drawLine(0, 31, 64, 31, hexTo565(0x222222));

        // 3. BOTTOM SECTION: Location Text (Fixing the multi-line left margin issue)
        weatherCanvas.setFont(); 
        weatherCanvas.setTextColor(locationColor);
        weatherCanvas.setTextWrap(false); 

        int startPos = 0;
        int nextPos = wrappedLocation.indexOf('\n');
        int currentY = 34; // Start at line 1
        int lineHeight = 8; // Standard font height in Adafruit GFX

        // Iterate through each chunk of text divided by our \n characters
        while (startPos < wrappedLocation.length()) {
            String line;
            if (nextPos == -1) {
                line = wrappedLocation.substring(startPos);
                startPos = wrappedLocation.length(); // Break condition
            } else {
                line = wrappedLocation.substring(startPos, nextPos);
                startPos = nextPos + 1;
                nextPos = wrappedLocation.indexOf('\n', startPos);
            }
            
            // Hard enforce the 1-pixel X offset for every single line
            weatherCanvas.setCursor(1, currentY);        
            weatherCanvas.print(line);
            currentY += lineHeight; // Drop down 8 pixels for the next line
        }
    }
};

#endif