#pragma once
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>

// ------------------------------------------------------------
// GLOBAL PREFERENCES (owned here, extern'd in other modules)
// ------------------------------------------------------------
extern String currentSSID;
extern String currentPASS;

extern bool prefShowGifs;
extern bool prefShowClock;
extern bool prefShowDate;
extern bool prefShowWeather;
extern bool prefShowISS;
extern bool prefShowPlanes;
extern bool prefShowTextBlast;
extern bool prefShowDoodles;
extern bool prefShowEarthquake;
extern bool prefShowSpotify;
extern bool prefShowDiags;
extern bool prefShowRadar;

extern float prefLat;
extern float prefLng;


extern int prefRadarZoomLevel;
extern RadarTimeFormat prefRadarTimeFormat;
extern RadarUnitFormat prefRadarUnitFormat;

extern String prefOsUser;
extern String prefOsPass;

extern String prefSpotifyRefreshToken;

extern int prefBrightness;
extern bool prefNightMode;
extern int prefNightStart;
extern int prefNightEnd;
extern int prefTransitionTime;

extern bool currentIsNight;

// Provided by main.ino
extern MatrixPanel_I2S_DMA* dma_display;

// ------------------------------------------------------------
// LOAD CONFIG
// ------------------------------------------------------------
static bool loadConfig() {
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        Serial.println("[FS] Warning: Failed to open /config.json for reading");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[FS] Error: JSON deserialization failed (%s)\n", err.c_str());
        return false;
    }

    currentSSID = doc["ssid"] | "";
    currentPASS = doc["password"] | "";

    prefShowGifs = doc["gifs"] | true;
    prefShowClock = doc["clock"] | true;
    prefShowDate = doc["date"] | true;
    prefShowWeather = doc["weather"] | true;
    prefShowISS = doc["iss"] | true;
    prefShowPlanes = doc["planes"] | true;
    prefShowTextBlast = doc["textblast"] | true;
    prefShowDoodles = doc["doodles"] | true;
    prefShowEarthquake = doc["earthquake"] | true;
    prefShowSpotify = doc["spotify"] | true;
    prefShowDiags = doc["diags"] | true;
    prefShowRadar = doc["radar"] | true;
 
    prefLat = doc["lat"] | 34.16;
    prefLng = doc["lng"] | -84.80;

    // Flexible Time Format parsing (handles "12H", "FORMAT_12H", "24H", etc.)
    const char* tfStr = doc["radarTimeFormat"] | "FORMAT_12H";
    if (strstr(tfStr, "12H") != NULL) {
        prefRadarTimeFormat = RadarTimeFormat::FORMAT_12H;
    } else if (strstr(tfStr, "24H") != NULL) {
        prefRadarTimeFormat = RadarTimeFormat::FORMAT_24H;
    } else {
        prefRadarTimeFormat = RadarTimeFormat::OFF;
    }

    // Flexible Unit Format parsing (handles "MI", "KM", "OFF")
    const char* ufStr = doc["radarUnitFormat"] | "MI";
    if (strcmp(ufStr, "MI") == 0) {
        prefRadarUnitFormat = RadarUnitFormat::MI;
    } else if (strcmp(ufStr, "KM") == 0) {
        prefRadarUnitFormat = RadarUnitFormat::KM;
    } else {
        prefRadarUnitFormat = RadarUnitFormat::OFF;
    }

    prefRadarZoomLevel = doc["radarZoomLevel"] | 7;

    prefOsUser = doc["osUser"] | "";
    prefOsPass = doc["osPass"] | "";

    prefSpotifyRefreshToken = doc["spotifyRefreshToken"] | "";

    prefBrightness = doc["brightness"] | 128;
    prefNightMode = doc["nightMode"] | false;
    prefNightStart = doc["nightStart"] | 22;
    prefNightEnd = doc["nightEnd"] | 6;
    prefTransitionTime = doc["transitionTime"] | 10;

    // IMPORTANT: Apply loaded preferences directly to the radar widget instance
    radarWidget.setLocation(prefLat, prefLng);
    radarWidget.setZoomLevel(prefRadarZoomLevel);
    radarWidget.setTimeFormat(prefRadarTimeFormat);
    radarWidget.setUnitFormat(prefRadarUnitFormat);

    Serial.println("[FS] Configuration loaded and applied successfully.");
    return true; // Return true because config file loaded successfully
}

// ------------------------------------------------------------
// SAVE CONFIG
// ------------------------------------------------------------
void saveConfig() {
    JsonDocument doc;

    doc["ssid"] = currentSSID;
    doc["password"] = currentPASS;

    doc["gifs"] = prefShowGifs;
    doc["clock"] = prefShowClock;
    doc["date"] = prefShowDate;
    doc["weather"] = prefShowWeather;
    doc["iss"] = prefShowISS;
    doc["planes"] = prefShowPlanes;
    doc["textblast"] = prefShowTextBlast;
    doc["doodles"] = prefShowDoodles;
    doc["earthquake"] = prefShowEarthquake;
    doc["spotify"] = prefShowSpotify;
    doc["diags"] = prefShowDiags;
    doc["radar"] = prefShowRadar;

    doc["lat"] = prefLat;
    doc["lng"] = prefLng;

    if (prefRadarTimeFormat == RadarTimeFormat::FORMAT_12H)      doc["radarTimeFormat"] = "FORMAT_12H";
    else if (prefRadarTimeFormat == RadarTimeFormat::FORMAT_24H) doc["radarTimeFormat"] = "FORMAT_24H";
    else                                                         doc["radarTimeFormat"] = "OFF";

    if (prefRadarUnitFormat == RadarUnitFormat::MI)      doc["radarUnitFormat"] = "MI";
    else if (prefRadarUnitFormat == RadarUnitFormat::KM) doc["radarUnitFormat"] = "KM";
    else                                                 doc["radarUnitFormat"] = "OFF";

    doc["radarZoomLevel"] = prefRadarZoomLevel;

    doc["osUser"] = prefOsUser;
    doc["osPass"] = prefOsPass;

    doc["spotifyRefreshToken"] = prefSpotifyRefreshToken;

    doc["brightness"] = prefBrightness;
    doc["nightMode"] = prefNightMode;
    doc["nightStart"] = prefNightStart;
    doc["nightEnd"] = prefNightEnd;
    doc["transitionTime"] = prefTransitionTime;

    File file = LittleFS.open("/config.json", "w");
    if (file) {
        serializeJson(doc, file);
        file.close();
        Serial.println("[FS] Configuration saved.");
    }
}

// ------------------------------------------------------------
// UPDATE BRIGHTNESS + NIGHT MODE
// ------------------------------------------------------------
void updateBrightness() {
    if (!prefNightMode) {
        currentIsNight = false;
        dma_display->setBrightness8(prefBrightness);
        return;
    }

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        dma_display->setBrightness8(prefBrightness);
        return;
    }

    int hour = timeinfo.tm_hour;
    bool isNight = false;

    if (prefNightStart < prefNightEnd) {
        isNight = (hour >= prefNightStart && hour < prefNightEnd);
    } else {
        isNight = (hour >= prefNightStart || hour < prefNightEnd);
    }

    currentIsNight = isNight;

    if (isNight) {
        dma_display->setBrightness8(2);
    } else {
        dma_display->setBrightness8(prefBrightness);
    }

      //planesWidget.begin(OPENSKY_CLIENT_ID, OPENSKY_CLIENT_SECRET, MY_LAT, MY_LNG, 20.0);
  if (prefShowPlanes) planesWidget.begin(prefOsUser, prefOsPass, prefLat, prefLng, 20.0);
  if (prefShowSpotify) spotifyWidget.begin (prefSpotifyRefreshToken);
  if (prefShowEarthquake) earthquakeWidget.begin();
}
