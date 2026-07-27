#ifndef PLANES_H
#define PLANES_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>

class PlanesWidget {
private:
    String clientId;
    String clientSecret;
    float myLat;
    float myLng;
    float searchRadius;
    String displayLoc = "";
    String displayAlt = "";
    String displaySpd = "";
    String accessToken = "";
    unsigned long tokenExpiryTime = 0;

    // State trackers
    bool hasPlane = false;
    bool apiError = false; // NEW: Tracks if we have an auth or network issue

    // Data for the closest plane
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
        return (R * c) / 1.60934; 
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

    bool authenticate() {
    Serial.println("[PLANES] Requesting OAuth2 token...");
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    http.begin(client, "https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String payload = "grant_type=client_credentials&client_id=" + clientId + "&client_secret=" + clientSecret;
    int httpCode = http.POST(payload);

    if (httpCode == 200) {
        DynamicJsonDocument doc(512);
        deserializeJson(doc, http.getString());
        accessToken = doc["access_token"].as<String>();
        int expiresIn = doc["expires_in"].as<int>();
        tokenExpiryTime = millis() + (expiresIn * 1000) - 60000; // Refresh 1 min early
        Serial.println("[PLANES] Token acquired.");
        http.end();
        return true;
    }
    Serial.printf("[PLANES] Auth Error: %d\n", httpCode);
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
        if (gameManager.isGameModeActive()) return false;
        Serial.println("[PLANES] Fetching states from OpenSky...");

        if (accessToken.isEmpty() || millis() >= tokenExpiryTime) {
        if (!authenticate()) {
            apiError = true;
            return true;
        }
    }

        // NEW: Check for missing credentials immediately and abort if empty
        if (clientId.length() == 0 || clientSecret.length() == 0) {
            Serial.println("[PLANES] Missing OpenSky credentials.");
            apiError = true;
            return true; // Return true so the main rotation loop isn't blocked
        }

        float radius_km = searchRadius * 1.609;
        float lat_offset = (radius_km / 6371.0) * (180.0 / M_PI);
        float lng_offset = (radius_km / 6371.0) * (180.0 / M_PI) / cos(myLat * M_PI / 180.0);
        
        String url = "https://opensky-network.org/api/states/all?lamin=" + String(myLat - lat_offset, 3) + 
                     "&lomin=" + String(myLng - lng_offset, 3) + 
                     "&lamax=" + String(myLat + lat_offset, 3) + 
                     "&lomax=" + String(myLng + lng_offset, 3);
        Serial.println(" URL: " + url);
        bool success = false; // The single-exit variable

        WiFiClientSecure *client = new WiFiClientSecure();
        client->setInsecure(); 
        
        HTTPClient http;
        http.begin(*client, url);

        String authHeader = "Bearer " + accessToken;
        http.addHeader("Authorization", authHeader);

        int httpCode = http.GET();
        Serial.printf("[PLANES] API HTTP Response Code: %d\n", httpCode);

        if (httpCode == 200) {
            apiError = false; // Clear any previous errors!
            
            JsonDocument filter;
            filter["states"][0][1] = true;  
            filter["states"][0][5] = true;  
            filter["states"][0][6] = true;  
            filter["states"][0][7] = true;  
            filter["states"][0][8] = true;  
            filter["states"][0][9] = true;  
            filter["states"][0][10] = true; 
            filter["states"][0][13] = true; 


        String responseBody = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, responseBody);


            if (error) {
                Serial.printf("[PLANES] JSON Parse Failed: %s\n", error.c_str());
            } else {
                JsonArray states = doc["states"];

                int totalPlanesInBox = states.size();
                Serial.printf("[PLANES] Found %d total aircraft in the bounding box.\n", totalPlanesInBox);

                if (totalPlanesInBox == 0) {
                    hasPlane = false;
                    Serial.println("[PLANES] Result: No planes overhead (Empty Array).");
                    success = true; 
                } else {
                    float minDist = 99999.0;
                    int airborneCount = 0;
                    int strictlyInRadiusCount = 0;
                    bool foundAirborne = false;

                    for (JsonVariant state : states) {
                        if (state[8].as<bool>()) continue; 
                        airborneCount++;

                        float p_lng = state[5];
                        float p_lat = state[6];
                        float dist = getHaversineDistance(myLat, myLng, p_lat, p_lng);

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
                                displayLoc = String(closestDist, 1) + "mi " + closestHeading;
                                displayAlt = String(closestAlt) + " ft";
                                displaySpd = String(closestSpeed) + " mph";
                                
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
                    success = true;
                }
            }
        } else {
            // NEW: Handle HTTP errors cleanly
            apiError = true;
            if (httpCode == 401) {
                Serial.println("[PLANES] Auth failed. Check OpenSky username/password.");
            } else {
                Serial.printf("[PLANES] Unexpected API Error. HTTP Code: %d\n", httpCode);
            }
            success = true; // Let the loop continue despite the error
        }

        // UNCONDITIONAL CLEANUP CHOKEPOINT
        http.end();
        delete client; 
        return success;
    }

    void draw(GraphicsContext* ctx, Font* font, int width, int height, unsigned long currentTime) {
        ctx->setFillStyle(0x000000); 
        ctx->fillRect(0, 0, width, height);

        ctx->setFillStyle(0x000044); 
        ctx->fillRect(0, 0, width, 9);

        ctx->setFillStyle(0x00FFFF); 
        ctx->fillRect(0, 9, width, 1);

        font->drawColorText(ctx, "PLANE TRAX", 2, 8, 0x00FFFF);

        // NEW: Intercept the screen and show the error in bright red (RGB565 = 0xF800)
        if (apiError) {
            font->drawColorText(ctx, "CHECK API", 6, 34, 0xFF0000); 
            font->drawColorText(ctx, "SETTINGS", 10, 46, 0xFFFF00);
            return;
        }

        if (!hasPlane) {
            font->drawText(ctx, "NO PLANES", 5, 34);
            font->drawText(ctx, "OVERHEAD", 8, 46);
            return;
        }
        
        ctx->setFillStyle(0x00FF00); 
        font->drawText(ctx, closestCallsign.c_str(), 2, 20);

        font->drawText(ctx, displayLoc.c_str(), 2, 32);
        font->drawText(ctx, displayAlt.c_str(), 2, 44);
        font->drawText(ctx, displaySpd.c_str(), 2, 56);
    }
};

#endif