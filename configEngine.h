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

extern float prefLat;
extern float prefLng;

extern String prefOsUser;
extern String prefOsPass;

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
    if (!file) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) return false;

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

    prefLat = doc["lat"] | 34.16;
    prefLng = doc["lng"] | -84.80;

    prefOsUser = doc["osUser"] | "";
    prefOsPass = doc["osPass"] | "";

    prefBrightness = doc["brightness"] | 128;
    prefNightMode = doc["nightMode"] | false;
    prefNightStart = doc["nightStart"] | 22;
    prefNightEnd = doc["nightEnd"] | 6;
    prefTransitionTime = doc["transitionTime"] | 10;

    return (currentSSID.length() > 0);
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

    doc["lat"] = prefLat;
    doc["lng"] = prefLng;

    doc["osUser"] = prefOsUser;
    doc["osPass"] = prefOsPass;

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
  if (prefShowEarthquake) earthquakeWidget.begin();
}
