#pragma once
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Externs provided by main.ino
extern WebServer server;
extern String gifDir;
extern File fsUploadFile;
extern void syncTimeWithLocation();

// Preferences (extern so main.ino owns the state)
extern bool prefShowGifs;
extern bool prefShowClock;
extern bool prefShowDate;
extern bool prefShowWeather;
extern bool prefShowISS;
extern bool prefShowPlanes;
extern bool prefShowTextBlast;
extern float prefLat;
extern float prefLng;
extern String prefOsUser;
extern String prefOsPass;
extern int prefBrightness;
extern bool prefNightMode;
extern int prefNightStart;
extern int prefNightEnd;
extern int prefTransitionTime;
extern bool prefShowDoodles;

extern void updateBrightness();
extern void saveConfig();

// ------------------------------------------------------------
// ROUTE SETUP
// ------------------------------------------------------------
static void setupWebRoutes() {

    // --- FACTORY RESET ---
    server.on("/api/reset", HTTP_POST, []() {
        server.send(200, "text/plain", "Resetting panel...");
        LittleFS.remove("/config.json");
        delay(1000);
        ESP.restart();
    });

    // --- GET SETTINGS ---
    server.on("/api/settings", HTTP_GET, []() {
        JsonDocument doc;

        doc["gifs"] = prefShowGifs;
        doc["clock"] = prefShowClock;
        doc["date"] = prefShowDate;
        doc["weather"] = prefShowWeather;
        doc["iss"] = prefShowISS;
        doc["planes"] = prefShowPlanes;
        doc["textblast"] = prefShowTextBlast;
        doc["lat"] = prefLat;
        doc["lng"] = prefLng;
        doc["osUser"] = prefOsUser;
        doc["osPass"] = prefOsPass;
        doc["brightness"] = prefBrightness;
        doc["nightMode"] = prefNightMode;
        doc["nightStart"] = prefNightStart;
        doc["nightEnd"] = prefNightEnd;
        doc["transitionTime"] = prefTransitionTime;
        doc["doodles"] = prefShowDoodles;

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // --- SAVE SETTINGS ---
    server.on("/api/settings", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "No payload");
            return;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) {
            server.send(400, "text/plain", "Invalid JSON");
            return;
        }

        if (doc.containsKey("gifs")) prefShowGifs = doc["gifs"];
        if (doc.containsKey("clock")) prefShowClock = doc["clock"];
        if (doc.containsKey("date")) prefShowDate = doc["date"];
        if (doc.containsKey("weather")) prefShowWeather = doc["weather"];
        if (doc.containsKey("iss")) prefShowISS = doc["iss"];
        if (doc.containsKey("planes")) prefShowPlanes = doc["planes"];
        if (doc.containsKey("textblast")) prefShowTextBlast = doc["textblast"];
        if (doc.containsKey("lat")) prefLat = doc["lat"];
        if (doc.containsKey("lng")) prefLng = doc["lng"];
        if (doc.containsKey("osUser")) prefOsUser = doc["osUser"].as<String>();
        if (doc.containsKey("osPass")) prefOsPass = doc["osPass"].as<String>();
        if (doc.containsKey("brightness")) prefBrightness = doc["brightness"];
        if (doc.containsKey("nightMode")) prefNightMode = doc["nightMode"];
        if (doc.containsKey("nightStart")) prefNightStart = doc["nightStart"];
        if (doc.containsKey("nightEnd")) prefNightEnd = doc["nightEnd"];
        if (doc.containsKey("transitionTime")) prefTransitionTime = doc["transitionTime"];
        if (doc.containsKey("doodles")) prefShowDoodles = doc["doodles"];

        updateBrightness();
        syncTimeWithLocation();
        saveConfig();

        server.send(200, "application/json", "{\"status\":\"success\"}");
    });

    // --- DOODLE UPLOAD ---
    server.on("/api/doodle/upload", HTTP_POST,
        []() {
            server.send(200, "application/json", "{\"status\":\"success\"}");
        },
        []() {
            HTTPUpload& upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                // Ensure the doodles directory exists
                if (!LittleFS.exists("/doodles")) {
                    LittleFS.mkdir("/doodles");
                }
                
                String filename = upload.filename;
                if (!filename.startsWith("/")) filename = "/" + filename;
                
                String path = "/doodles" + filename;
                fsUploadFile = LittleFS.open(path, "w");
            }
            else if (upload.status == UPLOAD_FILE_WRITE) {
                if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
            }
            else if (upload.status == UPLOAD_FILE_END) {
                if (fsUploadFile) {
                    fsUploadFile.close();
                    Serial.printf("Saved new doodle: %s (%u bytes)\n", upload.filename.c_str(), upload.totalSize);
                }
            }
        }
    );


    // --- LIST DOODLES ---
    server.on("/api/doodle/list", HTTP_GET, []() {
        File root = LittleFS.open("/doodles");
        if (!root || !root.isDirectory()) {
            server.send(404, "application/json", "{\"error\":\"Doodles directory not found\"}");
            return;
        }

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                arr.add(String(file.name()));
            }
            file = root.openNextFile();
        }
        root.close();

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

 // --- DOWNLOAD DOODLE ---
    server.on("/api/doodle/download", HTTP_GET, []() {
        if (!server.hasArg("name")) {
            server.send(400, "text/plain", "Missing name");
            return;
        }
        
        String path = "/doodles/" + server.arg("name");
        
        // 1. Open the file into a variable
        File file = LittleFS.open(path, "r");
        
        // 2. Check if the file is valid
        if (!file || file.isDirectory()) {
            server.send(404, "text/plain", "File not found");
            return;
        }

        // 3. Pass the file variable to streamFile
        server.streamFile(file, "application/octet-stream");
        
        // 4. Close the file after streaming
        file.close();
    });

// --- DELETE SPECIFIC DOODLE ---
    server.on("/api/doodle/delete", HTTP_POST, []() {
        if (!server.hasArg("name")) {
            server.send(400, "application/json", "{\"error\":\"Missing filename\"}");
            return;
        }

        String filename = server.arg("name");
        String path = "/doodles/" + filename;

        // 1. Force close any potential hanging upload file handles
        if (fsUploadFile) {
            fsUploadFile.close();
            Serial.println("[API] Force closed pending upload handle.");
        }

        // 2. Perform the deletion
        if (LittleFS.exists(path)) {
            if (LittleFS.remove(path)) {
                Serial.printf("[API] Successfully deleted: %s\n", path.c_str());
                server.send(200, "application/json", "{\"status\":\"success\"}");
            } else {
                // If it fails again, it means the LittleFS driver has a locked handle
                Serial.println("[API] LittleFS lock detected.");
                server.send(500, "application/json", "{\"error\":\"File is locked by system\"}");
            }
        } else {
            server.send(404, "application/json", "{\"error\":\"File not found\"}");
        }
    });

    // --- CLEAR ALL DOODLES ---
server.on("/api/doodle/clear", HTTP_POST, []() {
    File root = LittleFS.open("/doodles");
    if (!root || !root.isDirectory()) {
        server.send(404, "application/json", "{\"error\":\"Directory not found\"}");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        String fileName = file.name();
        String path = "/doodles/" + fileName;
        file.close(); // Close before deleting
        LittleFS.remove(path);
        file = root.openNextFile();
    }
    root.close();
    
    Serial.println("[API] Doodles directory cleared.");
    server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Directory cleared\"}");
});

    // --- LIST GIFS ---
    server.on("/api/gifs", HTTP_GET, []() {
        File root = LittleFS.open(gifDir);
        if (!root) {
            server.send(500, "application/json", "{\"error\":\"Failed to open directory\"}");
            return;
        }

        JsonDocument doc;
        JsonArray arr = doc["gifs"].to<JsonArray>();

        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String name = file.name();
                JsonObject o = arr.add<JsonObject>();
                o["name"] = name;
                o["size"] = file.size();
                o["enabled"] = !name.startsWith("_");
            }
            file = root.openNextFile();
        }

        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });

    // --- DELETE GIF ---
    server.on("/api/gifs/delete", HTTP_POST, []() {
        if (!server.hasArg("name")) {
            server.send(400, "application/json", "{\"error\":\"Missing filename\"}");
            return;
        }

        String filename = server.arg("name");
        String path = gifDir + "/" + filename;

        if (LittleFS.remove(path)) {
            server.send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            server.send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
        }
    });

    // --- TOGGLE GIF ENABLE ---
    server.on("/api/gifs/toggle", HTTP_POST, []() {
        if (!server.hasArg("name") || !server.hasArg("enabled")) {
            server.send(400, "application/json", "{\"error\":\"Missing args\"}");
            return;
        }

        String oldName = server.arg("name");
        bool enable = (server.arg("enabled") == "true");
        String newName = oldName;

        if (enable && oldName.startsWith("_")) newName = oldName.substring(1);
        if (!enable && !oldName.startsWith("_")) newName = "_" + oldName;

        if (newName != oldName) {
            LittleFS.rename(gifDir + "/" + oldName, gifDir + "/" + newName);
        }

        server.send(200, "application/json", "{\"status\":\"success\"}");
    });

    // --- GIF UPLOAD ---
    server.on("/api/gifs/upload", HTTP_POST,
        []() {
            server.send(200, "application/json", "{\"status\":\"success\"}");
        },
        []() {
            HTTPUpload& upload = server.upload();

            if (upload.status == UPLOAD_FILE_START) {
                String filename = upload.filename;
                if (!filename.startsWith("/")) filename = "/" + filename;
                String path = gifDir + filename;

                fsUploadFile = LittleFS.open(path, "w");
            }
            else if (upload.status == UPLOAD_FILE_WRITE) {
                if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
            }
            else if (upload.status == UPLOAD_FILE_END) {
                if (fsUploadFile) fsUploadFile.close();
            }
        }
    );

    // --- STATIC GIF SERVING ---
    server.serveStatic("/gifs", LittleFS, "/gifs");

    server.begin();
}
