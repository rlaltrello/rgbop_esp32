#pragma once
#include <NimBLEDevice.h>

// RGBop mobile app initially start with bluetooth to ask for wifi credentials 

// ------------------------------------------------------------
// EXTERNS PROVIDED BY main.ino
// ------------------------------------------------------------
extern String currentSSID;
extern String currentPASS;

extern bool provisioningMode;
extern bool newCredentialsReceived;

// ------------------------------------------------------------
// BLE UUIDs (same as your .ino)
// ------------------------------------------------------------
#define PROV_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID_SSID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CHAR_UUID_PASS    "beb5483e-36e1-4688-b7f5-ea07361b26a9"
#define CHAR_UUID_CMD     "beb5483e-36e1-4688-b7f5-ea07361b26aa"

// ------------------------------------------------------------
// CALLBACKS
// ------------------------------------------------------------
class SSIDCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        currentSSID = c->getValue().c_str();
        Serial.print("[BLE] Received SSID: ");
        Serial.println(currentSSID);
    }
};

class PassCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        currentPASS = c->getValue().c_str();
        Serial.print("[BLE] Received Password: ");
        Serial.println(currentPASS);
    }
};

class CmdCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
        Serial.println("[BLE] Command received — triggering WiFi sequence...");
        newCredentialsReceived = true;
    }
};

// ------------------------------------------------------------
// BLE SETUP
// ------------------------------------------------------------
static void setupBLE() {
    Serial.println("[BLE] Initializing NimBLE provisioning...");

    NimBLEDevice::init("RGBop Provisioning");
    NimBLEDevice::setSecurityAuth(true, true, true);

    NimBLEServer* server = NimBLEDevice::createServer();

    NimBLEService* provService = server->createService(PROV_SERVICE_UUID);

    // SSID characteristic
    NimBLECharacteristic* ssidChar =
        provService->createCharacteristic(
            CHAR_UUID_SSID,
            NIMBLE_PROPERTY::WRITE
        );
    ssidChar->setCallbacks(new SSIDCallbacks());

    // PASS characteristic
    NimBLECharacteristic* passChar =
        provService->createCharacteristic(
            CHAR_UUID_PASS,
            NIMBLE_PROPERTY::WRITE
        );
    passChar->setCallbacks(new PassCallbacks());

    // CMD characteristic
    NimBLECharacteristic* cmdChar =
        provService->createCharacteristic(
            CHAR_UUID_CMD,
            NIMBLE_PROPERTY::WRITE
        );
    cmdChar->setCallbacks(new CmdCallbacks());

    provService->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(PROV_SERVICE_UUID);
    adv->start();

    provisioningMode = true;

    Serial.println("[BLE] Provisioning service started.");
}
