#pragma once
#include <WiFi.h>

// ------------------------------------------------------------
// EXTERNS PROVIDED BY main.ino & configEngine.h
// ------------------------------------------------------------
extern bool prefShowWeather;
extern bool prefShowISS;
extern bool prefShowPlanes;
extern bool prefShowEarthquake;

extern WeatherWidget weatherWidget;
extern IssLocationWidget issWidget;
extern PlanesWidget planesWidget;
extern EarthquakeWidget earthquakeWidget;

extern void updateBrightness();

// ------------------------------------------------------------
// NETWORK STATE & TIMERS
// ------------------------------------------------------------
bool wifiConnected = false;
bool wsConnected = false;
unsigned long lastWatchdogCheck = 0;

// --- Weather Timers ---
unsigned long lastWeatherFetch = 0;
unsigned long currentWeatherInterval = 0; 
const unsigned long WEATHER_SUCCESS_INTERVAL = 5 * 60 * 1000; // 5 minutes
const unsigned long WEATHER_RETRY_INTERVAL = 30 * 1000;       // 30 seconds

// --- ISS Timers ---
unsigned long lastISSFetch = 0;
unsigned long currentISSInterval = 0;
const unsigned long ISS_SUCCESS_INTERVAL = 3 * 60 * 1000;     // 3 minutes
const unsigned long ISS_RETRY_INTERVAL = 15 * 1000;           // 15 seconds

// --- Planes Timers ---
unsigned long lastPlanesFetch = 0;
unsigned long currentPlanesInterval = 0;
const unsigned long PLANES_SUCCESS_INTERVAL = 30 * 1000;      // 30 seconds
const unsigned long PLANES_RETRY_INTERVAL = 15 * 1000;        // 15 seconds

// --- EarthQuake Timers ---
unsigned long lastEarthquakeFetch = 0;
unsigned long currentEarthquakeInterval = 0;
const unsigned long EARTHQUAKE_SUCCESS_INTERVAL = 3 * 60 * 1000;      // 3 minutes
const unsigned long EARTHQUAKE_RETRY_INTERVAL = 15 * 1000;        // 15 seconds

// ------------------------------------------------------------
// NETWORK MAINTENANCE LOOP
// ------------------------------------------------------------
static void maintainNetwork() {
    unsigned long now = millis();

    // 1. WiFi Watchdog
    if (now - lastWatchdogCheck > 3000) {
        lastWatchdogCheck = now;

        updateBrightness();

        if (WiFi.status() != WL_CONNECTED) {
            if (wifiConnected) {
                Serial.println("[WIFI] Lost! Reconnecting...");
                wifiConnected = false;
                wsConnected = false;
            }
            WiFi.disconnect();
            WiFi.reconnect();
        } else {
            if (!wifiConnected) {
                Serial.println("[WIFI] Reconnected.");
                Serial.println(WiFi.localIP());
                wifiConnected = true;
            }
        }
    }

    // 2. Widget Data Fetches (Only if connected)
    if (WiFi.status() == WL_CONNECTED) {
        
        // --- Check Weather ---
        if (prefShowWeather && (now - lastWeatherFetch >= currentWeatherInterval || lastWeatherFetch == 0)) {
            // Assuming you named the new class method fetchWeatherData()
            if (weatherWidget.fetchWeatherData()) {
                currentWeatherInterval = WEATHER_SUCCESS_INTERVAL; 
            } else {
                currentWeatherInterval = WEATHER_RETRY_INTERVAL;   
            }
            lastWeatherFetch = millis(); 
        }
        
        // --- Check ISS ---
        if (prefShowISS && (now - lastISSFetch >= currentISSInterval || lastISSFetch == 0)) {
            if (issWidget.fetchISSData()) {
                currentISSInterval = ISS_SUCCESS_INTERVAL; 
            } else {
                currentISSInterval = ISS_RETRY_INTERVAL;     
            }
            lastISSFetch = millis();
        }

        // --- Check Planes ---
        if (prefShowPlanes && (now - lastPlanesFetch >= currentPlanesInterval || lastPlanesFetch == 0)) {
            if (planesWidget.fetchPlanesData()) {
                currentPlanesInterval = PLANES_SUCCESS_INTERVAL; 
            } else {
                currentPlanesInterval = PLANES_RETRY_INTERVAL;   
            }
            lastPlanesFetch = millis(); 
        }

                // --- Check Planes ---
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