#ifndef GAME_MODE_MANAGER_H
#define GAME_MODE_MANAGER_H

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#define BTN_BIT_UP    0
#define BTN_BIT_DOWN  1
#define BTN_BIT_LEFT  2
#define BTN_BIT_RIGHT 3
#define BTN_BIT_FIRE  4

struct GameInputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool fire = false;
    uint8_t rawBitmask = 0;
    uint32_t lastSeq = 0;
    unsigned long lastPacketMs = 0;
};

class GameModeManager {
private:
    WebServer& _server;
    WebSocketsServer _webSocket;
    
    bool _isGameModeActive = false;
    GameInputState _currentState;
    uint32_t _highestSeqReceived = 0;

    // Optional callback when game mode starts
    std::function<void()> _onGameStartCallback = nullptr;
    
    const unsigned long WATCHDOG_TIMEOUT_MS = 250;

    void resetInputState() {
        _currentState = GameInputState();
        _highestSeqReceived = 0;
    }

    void parseInputPacket(uint8_t* payload, size_t length) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        
        if (err) {
           Serial.printf("[WS] JSON Parse Error: %s\n", err.c_str());
           return;
        }

        int version = doc["v"] | 0;
        if (version != 1) return; // Reject incompatible protocol version

        uint8_t buttons = doc["buttons"] | 0;
        uint32_t seq = doc["seq"] | 0;

        // TEMPORARY DEBUG LOG
        Serial.printf("[WS] Seq: %u | Buttons Bitmask: %u\n", seq, buttons);
        
        // Drop out-of-order/stale backlog packets
        if (seq <= _highestSeqReceived && _highestSeqReceived > 0) {
            return;
        }
        _highestSeqReceived = seq;

        _currentState.rawBitmask = buttons;
        _currentState.up    = (buttons & (1 << BTN_BIT_UP)) != 0;
        _currentState.down  = (buttons & (1 << BTN_BIT_DOWN)) != 0;
        _currentState.left  = (buttons & (1 << BTN_BIT_LEFT)) != 0;
        _currentState.right = (buttons & (1 << BTN_BIT_RIGHT)) != 0;
        _currentState.fire  = (buttons & (1 << BTN_BIT_FIRE)) != 0;
        
        _currentState.lastSeq = seq;
        _currentState.lastPacketMs = millis();
    }

    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                Serial.printf("[WS] Client #%u Disconnected\n", num);
                resetInputState();
                break;
            case WStype_CONNECTED:
                Serial.printf("[WS] Client #%u Connected\n", num);
                break;
            case WStype_TEXT:
                if (_isGameModeActive) {
                    parseInputPacket(payload, length);
                }
                break;
            default:
                break;
        }
    }

public:
    // Takes standard WebServer reference matching your webapi.h
    GameModeManager(WebServer& server) 
        : _server(server), _webSocket(81) {} // WebSocket server runs on port 81 (or endpoint /ws/game)

    void onGameStart(std::function<void()> callback) {
        _onGameStartCallback = callback;
    }

    void broadcastState(int x, int y, int dir, int bulletX, int bulletY) {
        if (!_isGameModeActive) return;

        // Keep payload small for minimal network overhead
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"x\":%d,\"y\":%d,\"d\":%d,\"bx\":%d,\"by\":%d}", 
                 x, y, dir, bulletX, bulletY);

        _webSocket.broadcastTXT(buf);
    }

    void begin() {
        // Spin up WebSocket server
        _webSocket.begin();
        _webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
            this->webSocketEvent(num, type, payload, length);
        });

        // HTTP POST /api/game/start
        _server.on("/api/game/start", HTTP_POST, [this]() {
            _isGameModeActive = true;
            resetInputState();
            Serial.println("[GAME] Entered Game Mode");
            _server.send(200, "application/json", "{\"status\":\"ok\",\"mode\":\"game\"}");
        });

        // HTTP POST /api/game/stop
        _server.on("/api/game/stop", HTTP_POST, [this]() {
            _isGameModeActive = false;
            resetInputState();
            _webSocket.disconnect();
            Serial.println("[GAME] Exited Game Mode");
            _server.send(200, "application/json", "{\"status\":\"ok\",\"mode\":\"normal\"}");
        });
    }

    void update() {
        // Keep WebSocket loop processing incoming frames
        _webSocket.loop();

        if (!_isGameModeActive) return;

        // Watchdog check: 250ms with no valid packet -> force neutral state
        if (_currentState.lastPacketMs > 0 && (millis() - _currentState.lastPacketMs > WATCHDOG_TIMEOUT_MS)) {
            if (_currentState.rawBitmask != 0) {
                Serial.println("[GAME] Watchdog triggered: Neutralizing inputs");
                resetInputState();
            }
        }
    }

    bool isGameModeActive() const {
        return _isGameModeActive;
    }

    GameInputState getInputState() {
        return _currentState;
    }
};

#endif