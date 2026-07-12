#ifndef ISSLOCATION_H
#define ISSLOCATION_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Fonts/TomThumb.h>

extern GFXcanvas16 widgetCanvas;

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
        
        // 1. Create a dedicated client to manage the sockets safely
        WiFiClient client; 
        HTTPClient http;
        bool issSuccess = false;
        
        for (int i = 0; i < 3; i++) {
            // 2. Pass the client into the begin function!
            http.begin(client, "http://api.open-notify.org/iss-now.json");
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
                client.stop(); // <-- FORCE SOCKET CLOSE
                break;
            } else {
                Serial.printf("ISS API failed (HTTP %d). Retry %d/3...\n", httpCode, i + 1);
                http.end();
                client.stop(); // <-- FORCE SOCKET CLOSE
                delay(1000); 
            }
        }

        if (!issSuccess) {
            Serial.println("Failed to retrieve ISS coordinates.");
            return false;
        }

        String geoUrl = "http://api.geonames.org/findNearbyPlaceNameJSON?username=rlaltrel&lat=" + currentLat + "&lng=" + currentLon;
        http.begin(client, geoUrl); // <-- Pass client
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
        client.stop(); // <-- FORCE SOCKET CLOSE

        if (!onLand) {
            String oceanUrl = "http://api.geonames.org/oceanJSON?username=rlaltrel&lat=" + currentLat + "&lng=" + currentLon;
            http.begin(client, oceanUrl); // <-- Pass client
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
            client.stop(); // <-- FORCE SOCKET CLOSE
        }

        // Strip out the UTF-8 garbage before wrapping!
        locationName = sanitizeText(locationName);
        
        wrappedLocation = wrapText(locationName, 10);
        
        Serial.printf("ISS Location Updated: %s\n", locationName.c_str());
        return true; 
    }

    void draw() {
        widgetCanvas.fillScreen(0x0000); 

        // 1. TOP SECTION: "ISS" Blue Badge
        widgetCanvas.fillRect(0, 0, 24, 11, hexTo565(0x2D38BF));
        widgetCanvas.setFont(); 
        widgetCanvas.setTextWrap(false);
        widgetCanvas.setTextSize(1);
        widgetCanvas.setTextColor(0x0000); 
        widgetCanvas.setCursor(3, 2); 
        widgetCanvas.print("ISS");

        // First Divider Line
        widgetCanvas.drawLine(0, 13, 64, 13, hexTo565(0x222222));

        // 2. MIDDLE SECTION: Coordinates
        widgetCanvas.setFont(&TomThumb);
        widgetCanvas.setTextColor(hexTo565(0x888888));
        
        widgetCanvas.setCursor(1, 20); 
        widgetCanvas.print("Lat: ");
        widgetCanvas.print(currentLat);
        
        widgetCanvas.setCursor(1, 27);
        widgetCanvas.print("Lon: ");
        widgetCanvas.print(currentLon);

        // Second Divider Line
        widgetCanvas.drawLine(0, 31, 64, 31, hexTo565(0x222222));

        // 3. BOTTOM SECTION: Location Text (Fixing the multi-line left margin issue)
        widgetCanvas.setFont(); 
        widgetCanvas.setTextColor(locationColor);
        widgetCanvas.setTextWrap(false); 

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
            widgetCanvas.setCursor(1, currentY);        
            widgetCanvas.print(line);
            currentY += lineHeight; // Drop down 8 pixels for the next line
        }
    }
};

#endif