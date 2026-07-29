#ifndef GAME_MODE_MANAGER_H
#define GAME_MODE_MANAGER_H

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <functional>

#define BTN_BIT_UP    0
#define BTN_BIT_DOWN  1
#define BTN_BIT_LEFT  2
#define BTN_BIT_RIGHT 3
#define BTN_BIT_A     4
#define BTN_BIT_B     5

struct GameInputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool btnA = false;
    bool btnB = false;
    uint8_t rawBitmask = 0;
    uint8_t lastSeq = 0;
    unsigned long lastPacketMs = 0;
};

class GameModeManager {
private:
    WebServer& _server;
    WebSocketsServer _webSocket;

    bool _hasLoggedStall = false;
    bool _isGameModeActive = false;
    GameInputState _currentState;

    const unsigned long WATCHDOG_TIMEOUT_MS = 250;
    const unsigned long DISCONNECT_GRACE_MS = 800;

    bool _pendingDisconnectExit = false;
    unsigned long _disconnectAtMs = 0;

    std::function<void()> _onGameStartCallback = nullptr;
    std::function<void()> _onGameExitCallback  = nullptr;

    void resetInputState() {
        _currentState = GameInputState();
    }

    uint8_t connectedClientCount() {
        uint8_t count = 0;
        for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
            if (_webSocket.clientIsConnected(i)) {
                count++;
            }
        }
        return count;
    }

    void parseBinaryInputPacket(uint8_t* payload, size_t length) {
        if (length < 2) return;

        uint8_t seq = payload[0];
        uint8_t buttons = payload[1];

        uint8_t seqDiff = (uint8_t)(seq - _currentState.lastSeq);
        if (_currentState.lastPacketMs > 0 && seqDiff > 10 && seqDiff < 200) {
            _currentState.lastSeq = seq;
            _currentState.lastPacketMs = millis();
            return;
        }

        _currentState.rawBitmask = buttons;
        _currentState.up    = (buttons & (1 << BTN_BIT_UP)) != 0;
        _currentState.down  = (buttons & (1 << BTN_BIT_DOWN)) != 0;
        _currentState.left  = (buttons & (1 << BTN_BIT_LEFT)) != 0;
        _currentState.right = (buttons & (1 << BTN_BIT_RIGHT)) != 0;
        _currentState.btnA  = (buttons & (1 << BTN_BIT_A)) != 0;
        _currentState.btnB  = (buttons & (1 << BTN_BIT_B)) != 0;

        _currentState.lastSeq = seq;
        _currentState.lastPacketMs = millis();
        _hasLoggedStall = false;
    }

    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                Serial.printf("[WS] Client #%u Disconnected\n", num);
                if (_isGameModeActive && connectedClientCount() == 0) {
                    _pendingDisconnectExit = true;
                    _disconnectAtMs = millis();
                }
                break;

            case WStype_CONNECTED:
                Serial.printf("[WS] Client #%u Connected\n", num);
                for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
                    if (i != num) {
                        _webSocket.disconnect(i);
                    }
                }
                break;

            case WStype_BIN:
                parseBinaryInputPacket(payload, length);
                break;

            default:
                break;
        }
    }

public:
    GameModeManager(WebServer& server)
        : _server(server), _webSocket(81) {}

    // --- PUBLIC METHOD FOR MANUAL OR TIMER EXITS ---
    void exitGameMode() {
        if (!_isGameModeActive) return;
        _isGameModeActive = false;
        _pendingDisconnectExit = false;
        resetInputState();
        Serial.println("[GAME] Exited Game Mode");
        if (_onGameExitCallback) {
            _onGameExitCallback();
        }
    }

    void onGameStart(std::function<void()> callback) {
        _onGameStartCallback = callback;
    }

    void onGameExit(std::function<void()> callback) {
        _onGameExitCallback = callback;
    }

    void begin() {
        _webSocket.begin();
        _webSocket.enableHeartbeat(0, 0, 0); 

        _webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
            this->webSocketEvent(num, type, payload, length);
        });

        _server.on("/api/game/start", HTTP_POST, [this]() {
            _webSocket.disconnect();
            
            _isGameModeActive = true;
            _pendingDisconnectExit = false;
            resetInputState();
            _currentState.lastPacketMs = millis();

            if (_onGameStartCallback) {
                _onGameStartCallback();
            }

            Serial.println("[GAME] Explicitly re-entered Game Mode");
            _server.sendHeader("Connection", "close");
            _server.send(200, "application/json", "{\"status\":\"ok\",\"mode\":\"game\"}");
        });

        _server.on("/api/game/stop", HTTP_POST, [this]() {
            exitGameMode();
            _webSocket.disconnect(); 
            _server.sendHeader("Connection", "close");
            _server.send(200, "application/json", "{\"status\":\"ok\",\"mode\":\"normal\"}");
        });
    }

    void update() {
        _webSocket.loop();

        if (!_isGameModeActive) return;

        unsigned long now = millis();

        if (_pendingDisconnectExit) {
            if (connectedClientCount() > 0) {
                _pendingDisconnectExit = false;
            } else if (now - _disconnectAtMs > DISCONNECT_GRACE_MS) {
                Serial.println("[GAME] All WS clients lost. Returning to matrix rotation.");
                exitGameMode();
                return;
            }
        }

        if (!_pendingDisconnectExit && connectedClientCount() == 0) {
            _pendingDisconnectExit = true;
            _disconnectAtMs = now;
        }

        if (_currentState.lastPacketMs > 0 && (now - _currentState.lastPacketMs > WATCHDOG_TIMEOUT_MS)) {
            if (_currentState.rawBitmask != 0) {
                resetInputState();
            }
        }
    }

    bool isGameModeActive() const { return _isGameModeActive; }
    GameInputState getInputState() { return _currentState; }
};

#endif