#pragma once
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ------------------------------------------------------------
// EXTERNS PROVIDED BY main.ino
// ------------------------------------------------------------
extern MatrixPanel_I2S_DMA* dma_display;

// Night mode flag (used by configEngine)
extern bool currentIsNight;

// ------------------------------------------------------------
// PANEL CONSTANTS
// ------------------------------------------------------------
#define PANEL_RES_X 64
#define PANEL_RES_Y 64
#define PANEL_CHAIN 1

// ------------------------------------------------------------
// VERIFIED ESP32‑S3 HUB75 PINOUT
// ------------------------------------------------------------
#define R1_PIN  1
#define G1_PIN  2
#define B1_PIN  3
#define R2_PIN  4
#define G2_PIN  5
#define B2_PIN  6

#define A_PIN   7
#define B_PIN   8
#define C_PIN   9
#define D_PIN   10
#define E_PIN   11

#define LAT_PIN 12
#define OE_PIN  13
#define CLK_PIN 14

// ------------------------------------------------------------
// HARDWARE INITIALIZATION
// ------------------------------------------------------------
static void setupMatrixHardware() {
    Serial.println("[HARDWARE] Initializing HUB75 panel...");

    HUB75_I2S_CFG mxconfig(
        PANEL_RES_X,
        PANEL_RES_Y,
        PANEL_CHAIN
    );

    // GPIO mapping
    mxconfig.gpio.r1 = R1_PIN;
    mxconfig.gpio.g1 = G1_PIN;
    mxconfig.gpio.b1 = B1_PIN;
    mxconfig.gpio.r2 = R2_PIN;
    mxconfig.gpio.g2 = G2_PIN;
    mxconfig.gpio.b2 = B2_PIN;

    mxconfig.gpio.a  = A_PIN;
    mxconfig.gpio.b  = B_PIN;
    mxconfig.gpio.c  = C_PIN;
    mxconfig.gpio.d  = D_PIN;
    mxconfig.gpio.e  = E_PIN;

    mxconfig.gpio.lat = LAT_PIN;
    mxconfig.gpio.oe  = OE_PIN;
    mxconfig.gpio.clk = CLK_PIN;

    mxconfig.driver = HUB75_I2S_CFG::FM6124;
    mxconfig.clkphase = false;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    mxconfig.latch_blanking = 4; 

    // Allocate the display
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    if (!dma_display) {
        Serial.println("[HARDWARE] ERROR: Failed to allocate DMA display!");
        return;
    }

    dma_display->begin();
    dma_display->setBrightness8(128);   // default brightness
    currentIsNight = false;

    dma_display->clearScreen();

    // Boot diagnostics pixel
    dma_display->drawPixel(0, 0, dma_display->color565(0, 0, 255));

    Serial.println("[HARDWARE] HUB75 panel initialized.");
}
// ------------------------------------------------------------
// DIAGNOSTIC PIXEL DRAWING
// ------------------------------------------------------------
static void drawDiagnostics() {
    if (dma_display == nullptr) return;

    dma_display->clearScreen();

    // Pixel (0,0) - Panel Initialized / Loop Alive (Blue)
    dma_display->drawPixel(0, 0, dma_display->color565(0, 0, 255));

    // Pixel (1,0) - WiFi State (Green = Connected, Red = Disconnected)
    extern bool wifiConnected;
    dma_display->drawPixel(
        1, 0,
        wifiConnected
            ? dma_display->color565(0, 255, 0)
            : dma_display->color565(255, 0, 0)
    );
}