#pragma once
#include <WiFi.h>
#include <ESPping.h>

// ------------------------------------------------------------
// EXTERNS PROVIDED BY main.ino & configEngine.h
// ------------------------------------------------------------
extern bool prefShowWeather;
extern bool prefShowISS;
extern bool prefShowPlanes;
extern bool prefShowEarthquake;
extern bool prefShowSpotify;

extern WeatherWidget weatherWidget;
extern IssLocationWidget issWidget;
extern PlanesWidget planesWidget;
extern EarthquakeWidget earthquakeWidget;

extern void updateBrightness();

bool wifiConnected = false;
bool wsConnected = false;

// --- Weather Timers ---
static unsigned long lastWeatherFetch = 0;
static unsigned long currentWeatherInterval = 0; 
const unsigned long WEATHER_SUCCESS_INTERVAL = 5 * 60 * 1000;
const unsigned long WEATHER_RETRY_INTERVAL = 30 * 1000;

// --- ISS Timers ---
static unsigned long lastISSFetch = 0;
static unsigned long currentISSInterval = 0;
const unsigned long ISS_SUCCESS_INTERVAL = 3 * 60 * 1000;
const unsigned long ISS_RETRY_INTERVAL = 15 * 1000;

// --- Planes Timers ---
static unsigned long lastPlanesFetch = 0;
static unsigned long currentPlanesInterval = 0;
const unsigned long PLANES_SUCCESS_INTERVAL = 30 * 1000;
const unsigned long PLANES_RETRY_INTERVAL = 15 * 1000;

// --- EarthQuake Timers ---
static unsigned long lastEarthquakeFetch = 0;
static unsigned long currentEarthquakeInterval = 0;
const unsigned long EARTHQUAKE_SUCCESS_INTERVAL = 3 * 60 * 1000;
const unsigned long EARTHQUAKE_RETRY_INTERVAL = 15 * 1000;


inline void checkMyPulse() {
  static unsigned long lastPingCheck = 0;
  const unsigned long PING_INTERVAL = 60000; // Check every 60 seconds
  static int failedPingCount = 0;
  const int MAX_PING_FAILURES = 3;            // Restart after 3 consecutive failed checks

  if (millis() - lastPingCheck >= PING_INTERVAL) {
    lastPingCheck = millis();

    IPAddress gateway = WiFi.gatewayIP();
    
    // Extra safety: Don't ping an invalid gateway IP address
    if (gateway == IPAddress(0, 0, 0, 0)) {
      return; 
    }

    bool pingSuccess = Ping.ping(gateway, 1);

    if (pingSuccess) {
      failedPingCount = 0; 
    } else {
      failedPingCount++;
      Serial.printf("[NET] Gateway ping failed (%d/%d). Router unreachable despite WL_CONNECTED.\n", 
                    failedPingCount, MAX_PING_FAILURES);

      if (failedPingCount >= MAX_PING_FAILURES) {
        Serial.println("[NET] WiFi says connected, but gateway is dead. Rebooting...");
        Serial.flush();
        delay(100);
        ESP.restart();
      }
    }
  }
}

// ------------------------------------------------------------
// NETWORK MAINTENANCE LOOP
// ------------------------------------------------------------
static void maintainNetwork() {
    updateBrightness();
    gameManager.update();
    unsigned long now = millis();

    static unsigned long lastReconnectAttempt = 0;
    
    // Handle true Layer 2/3 drops
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiConnected) {
            Serial.println("[WIFI] Connection lost!");
            wifiConnected = false;
            wsConnected = false;
        }

        if (millis() - lastReconnectAttempt >= 10000) {
            lastReconnectAttempt = millis();
            Serial.println("[WIFI] Attempting reconnect...");
            WiFi.disconnect();
            WiFi.reconnect();
        }
    } else {
        if (!wifiConnected) {
            Serial.println("[WIFI] Reconnected!");
            Serial.print("[WIFI] IP: ");
            Serial.println(WiFi.localIP());
            wifiConnected = true;
        }

        // --- ACTIVE CONNECTION HEALTH CHECK ---
        // WiFi.status() IS WL_CONNECTED here, so we verify if data can actually flow.
        checkMyPulse();

        // --- WIDGET DATA FETCHES ---
        
        // Weather
        if (prefShowWeather && (now - lastWeatherFetch >= currentWeatherInterval || lastWeatherFetch == 0)) {
            if (weatherWidget.fetchWeatherData()) {
                currentWeatherInterval = WEATHER_SUCCESS_INTERVAL; 
            } else {
                currentWeatherInterval = WEATHER_RETRY_INTERVAL;   
            }
            lastWeatherFetch = millis(); 
        }
        
        // ISS
        if (prefShowISS && (now - lastISSFetch >= currentISSInterval || lastISSFetch == 0)) {
            if (issWidget.fetchISSData()) {
                currentISSInterval = ISS_SUCCESS_INTERVAL; 
            } else {
                currentISSInterval = ISS_RETRY_INTERVAL;     
            }
            lastISSFetch = millis();
        }

        // Planes
        if (prefShowPlanes && (now - lastPlanesFetch >= currentPlanesInterval || lastPlanesFetch == 0)) {
            if (planesWidget.fetchPlanesData()) {
                currentPlanesInterval = PLANES_SUCCESS_INTERVAL; 
            } else {
                currentPlanesInterval = PLANES_RETRY_INTERVAL;   
            }
            lastPlanesFetch = millis(); 
        }

        // Earthquake
        if (prefShowEarthquake && (now - lastEarthquakeFetch >= currentEarthquakeInterval || lastEarthquakeFetch == 0)) {
            if (earthquakeWidget.fetchData()) {
                currentEarthquakeInterval = EARTHQUAKE_SUCCESS_INTERVAL; 
            } else {
                currentEarthquakeInterval = EARTHQUAKE_RETRY_INTERVAL;   
            }
            lastEarthquakeFetch = millis(); 
        }
    }
}