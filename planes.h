#ifndef PLANES_H
#define PLANES_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

class PlanesWidget {
private:
    String clientId;
    String clientSecret;
    float myLat;
    float myLng;
    float searchRadius;
    String bearerToken = "";

    // Data for the closest plane
    bool hasPlane = false;
    String closestCallsign = "";
    String closestHeading = "";
    float closestDist = 0.0;
    int closestAlt = 0;
    int closestSpeed = 0;

    float getHaversineDistance(float lat1, float lon1, float lat2, float lon2) {
        float R = 6371.0; 
        float dLat = (lat2 - lat1) * M_PI / 180.0;
        float dLon = (lon2 - lon1) * M_PI / 180.0;
        lat1 = lat1 * M_PI / 180.0;
        lat2 = lat2 * M_PI / 180.0;

        float a = pow(sin(dLat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dLon / 2), 2);
        float c = 2 * atan2(sqrt(a), sqrt(1 - a));
        return (R * c) / 1.60934; // Return miles
    }

    String getHeadingString(float value) {
        if (value < 11.25) return "N";
        if (value < 33.75) return "NNE";
        if (value < 56.25) return "NE";
        if (value < 78.75) return "ENE";
        if (value < 101.25) return "E";
        if (value < 123.75) return "ESE";
        if (value < 146.25) return "SE";
        if (value < 168.75) return "SSE";
        if (value < 191.25) return "S";
        if (value < 213.75) return "SSW";
        if (value < 236.25) return "SW";
        if (value < 258.75) return "WSW";
        if (value < 281.25) return "W";
        if (value < 303.75) return "WNW";
        if (value < 326.25) return "NW";
        if (value < 348.75) return "NNW";
        return "N";
    }

    bool fetchToken() {
        Serial.println("[PLANES] Requesting new OAuth2 Token...");
        HTTPClient http;
        http.begin("https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        String body = "grant_type=client_credentials&client_id=" + clientId + "&client_secret=" + clientSecret;
        
        int httpCode = http.POST(body);
        if (httpCode == 200) {
            JsonDocument doc;
            deserializeJson(doc, http.getStream());
            bearerToken = doc["access_token"].as<String>();
            Serial.println("[PLANES] Token successfully acquired!");
            http.end();
            return true;
        }
        
        Serial.printf("[PLANES] Token fetch failed. HTTP %d\n", httpCode);
        http.end();
        return false;
    }

public:
    void begin(String id, String secret, float lat, float lng, float radiusMiles) {
        clientId = id;
        clientSecret = secret;
        myLat = lat;
        myLng = lng;
        searchRadius = radiusMiles;
    }

    bool fetchPlanesData() {
        if (bearerToken == "") {
            if (!fetchToken()) return false;
        }

        Serial.println("[PLANES] Fetching states from OpenSky...");

        float radius_km = searchRadius * 1.609;
        float lat_offset = (radius_km / 6371.0) * (180.0 / M_PI);
        float lng_offset = (radius_km / 6371.0) * (180.0 / M_PI) / cos(myLat * M_PI / 180.0);
        
        String url = "https://opensky-network.org/api/states/all?lamin=" + String(myLat - lat_offset, 3) + 
                     "&lomin=" + String(myLng - lng_offset, 3) + 
                     "&lamax=" + String(myLat + lat_offset, 3) + 
                     "&lomax=" + String(myLng + lng_offset, 3);

        HTTPClient http;
        http.begin(url);
        http.addHeader("Authorization", "Bearer " + bearerToken);
        http.setTimeout(10000); // 10 second timeout for large payloads

        int httpCode = http.GET();
        Serial.printf("[PLANES] API HTTP Response Code: %d\n", httpCode);

        if (httpCode == 200) {
            // Apply a filter so we only parse the specific array indices we need.
            // This prevents the ESP32 from running out of RAM in busy airspaces.
            JsonDocument filter;
            filter["states"][0][1] = true;  // callsign
            filter["states"][0][5] = true;  // lng
            filter["states"][0][6] = true;  // lat
            filter["states"][0][7] = true;  // baro_alt
            filter["states"][0][8] = true;  // on_ground
            filter["states"][0][9] = true;  // velocity
            filter["states"][0][10] = true; // heading
            filter["states"][0][13] = true; // geo_alt

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));

            if (error) {
                Serial.printf("[PLANES] JSON Parse Failed: %s\n", error.c_str());
                Serial.printf("[PLANES] Free Heap: %d bytes\n", ESP.getFreeHeap());
                http.end();
                return false;
            }

            JsonArray states = doc["states"];
            int totalPlanesInBox = states.size();
            Serial.printf("[PLANES] Found %d total aircraft in the bounding box.\n", totalPlanesInBox);

            if (totalPlanesInBox == 0) {
                hasPlane = false;
                Serial.println("[PLANES] Result: No planes overhead (Empty Array).");
                http.end();
                return true; 
            }

            float minDist = 99999.0;
            int airborneCount = 0;
            int strictlyInRadiusCount = 0;
            bool foundAirborne = false;

            for (JsonVariant state : states) {
                // Check index 8: Is it on the ground?
                if (state[8].as<bool>()) continue; 
                airborneCount++;

                float p_lng = state[5];
                float p_lat = state[6];
                float dist = getHaversineDistance(myLat, myLng, p_lat, p_lng);

                // The bounding box is a square, the radius is a circle.
                // Discard planes that are in the corners of the square.
                if (dist <= searchRadius) {
                    strictlyInRadiusCount++;

                    if (dist < minDist) {
                        minDist = dist;
                        closestCallsign = state[1].as<String>();
                        closestCallsign.trim();
                        closestHeading = getHeadingString(state[10].as<float>());
                        
                        float alt_m = state[13].isNull() ? state[7].as<float>() : state[13].as<float>();
                        closestAlt = round(alt_m * 3.28084);
                        closestSpeed = round(state[9].as<float>() * 2.23694);
                        
                        foundAirborne = true;
                    }
                }
            }

            Serial.printf("[PLANES] After culling: %d were airborne, %d were strictly inside the %.1f mile circular radius.\n", airborneCount, strictlyInRadiusCount, searchRadius);

            hasPlane = foundAirborne;
            closestDist = minDist;
            
            if(hasPlane) {
                Serial.printf("[PLANES] WINNER: %s at %.1f mi, Heading %s, %d ft\n", closestCallsign.c_str(), closestDist, closestHeading.c_str(), closestAlt);
            } else {
                Serial.println("[PLANES] Result: No airborne planes within exact circular radius.");
            }

            http.end();
            return true;

        } else if (httpCode == 401) {
            Serial.println("[PLANES] Token unauthorized/expired. Resetting...");
            bearerToken = ""; 
        } else {
             Serial.printf("[PLANES] Unexpected API Error. HTTP Code: %d\n", httpCode);
        }

        http.end();
        return false;
    }

void draw(GraphicsContext* ctx, Font* font, int width, int height, unsigned long currentTime) {
        // 1. Clear the entire background to black
        ctx->setFillStyle(0x000000); 
        ctx->fillRect(0, 0, width, height);

        // 2. Draw the Persistent Header Background (Dark Blue)
        // 0x000044 is a nice deep dark blue in hex
        ctx->setFillStyle(0x000044); 
        ctx->fillRect(0, 0, width, 9);

        // 3. Draw the Cyan dividing line at Y=9
        ctx->setFillStyle(0x00FFFF); // Cyan
        ctx->fillRect(0, 9, width, 1);

        // 4. Draw the Header Text
        // Because your font wrapper defaults to white, we'll draw this manually 
        // to get the cyan color you requested.
        extern GFXcanvas16 weatherCanvas; // Access the canvas from the main sketch
        weatherCanvas.setTextWrap(false); 
        weatherCanvas.setTextSize(1);
        weatherCanvas.setCursor(2, 1); 
        weatherCanvas.setTextColor(0x07FF); // 0x07FF is RGB565 for Cyan
        weatherCanvas.print("PLANE TRAX");     // Adjust this word based on your simulator testing!

        // 5. Draw the Dynamic Content
        if (!hasPlane) {
            // Shifted down slightly to account for the header
            font->drawText(ctx, "NO PLANES", 5, 34);
            font->drawText(ctx, "OVERHEAD", 8, 46);
            return;
        }

        // --- Data Layout for 64x64 Matrix (Shifted down below the line) ---
        
        // Callsign (Y=20)
        ctx->setFillStyle(0x00FF00); 
        font->drawText(ctx, closestCallsign.c_str(), 2, 20);

        // Distance & Heading (Y=32)
        String locStr = String(closestDist, 1) + "mi " + closestHeading;
        font->drawText(ctx, locStr.c_str(), 2, 32);

        // Altitude (Y=44)
        String altStr = String(closestAlt) + " ft";
        font->drawText(ctx, altStr.c_str(), 2, 44);

        // Speed (Y=56)
        String spdStr = String(closestSpeed) + " mph";
        font->drawText(ctx, spdStr.c_str(), 2, 56);
    }
};

#endif