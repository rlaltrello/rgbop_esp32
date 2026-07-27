#pragma once
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// External objects provided by main.ino
extern MatrixPanel_I2S_DMA* dma_display;
extern WebServer server;
extern bool currentIsNight;

// Night vision helper (still defined in main.ino)
extern uint16_t applyNightVision(uint16_t color);

// ------------------------------------------------------------
// GIF ENGINE STATE
// ------------------------------------------------------------
static AnimatedGIF gif;
static File gifFile;
static unsigned long gifStartTick = 0;
static int x_offset = 0;
static int y_offset = 0;

// ------------------------------------------------------------
// FILE HANDLERS
// ------------------------------------------------------------
static void* GIFOpenFile(const char* fname, int32_t* pSize) {
    gifFile = LittleFS.open(fname);
    if (!gifFile) return nullptr;
    *pSize = gifFile.size();
    return (void*)&gifFile;
}

static void GIFCloseFile(void* pHandle) {
    File* f = static_cast<File*>(pHandle);
    if (f) f->close();
}

static int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
    File* f = static_cast<File*>(pFile->fHandle);
    int32_t remaining = pFile->iSize - pFile->iPos;

    if (remaining < iLen) iLen = remaining;
    if (iLen <= 0) return 0;

    int32_t bytesRead = f->read(pBuf, iLen);
    pFile->iPos = f->position();
    return bytesRead;
}

static int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition) {
    File* f = static_cast<File*>(pFile->fHandle);
    f->seek(iPosition);
    pFile->iPos = f->position();
    return pFile->iPos;
}

// ------------------------------------------------------------
// DRAW CALLBACK
// ------------------------------------------------------------
static void GIFDraw(GIFDRAW* pDraw) {
    uint8_t* src = pDraw->pPixels;
    uint16_t* palette = pDraw->pPalette;

    int y = y_offset + pDraw->iY + pDraw->y;
    int width = pDraw->iWidth;

    // Clip vertically if out of bounds
    if (y < 0 || y >= dma_display->height()) return;

    if (pDraw->ucDisposalMethod == 2) {
        for (int x = 0; x < width; x++) {
            if (src[x] == pDraw->ucTransparent)
                src[x] = pDraw->ucBackground;
        }
        pDraw->ucHasTransparency = 0;
    }

    if (pDraw->ucHasTransparency) {
        uint8_t transparent = pDraw->ucTransparent;
        uint8_t* end = src + pDraw->iWidth;
        int x = 0;

        while (x < pDraw->iWidth) {
            uint16_t temp[320];
            int count = 0;

            while (src < end && *src != transparent) {
                temp[count++] = palette[*src++];
            }

            for (int i = 0; i < count; i++) {
                int drawX = x_offset + x + i;
                if (drawX >= 0 && drawX < dma_display->width()) {
                    dma_display->drawPixel(drawX, y, applyNightVision(temp[i]));
                }
            }
            x += count;

            count = 0;
            while (src < end && *src == transparent) {
                src++;
                count++;
            }
            x += count;
        }
    } else {
        for (int x = 0; x < width; x++) {
            int drawX = x_offset + x;
            if (drawX >= 0 && drawX < dma_display->width()) {
                dma_display->drawPixel(drawX, y, applyNightVision(palette[*src]));
            }
            src++;
        }
    }
}

// ------------------------------------------------------------
// PUBLIC API
// ------------------------------------------------------------
static void playGIF(const char* path, uint32_t durationMs = 10000) {
    gifStartTick = millis();

    if (!gif.open(path, GIFOpenFile, GIFCloseFile, GIFReadFile, GIFSeekFile, GIFDraw)) {
        Serial.printf("[GIF] Failed to open %s\n", path);
        return;
    }

    x_offset = (dma_display->width() - gif.getCanvasWidth()) / 2;
    if (x_offset < 0) x_offset = 0;

    y_offset = (dma_display->height() - gif.getCanvasHeight()) / 2;
    if (y_offset < 0) y_offset = 0;

    // ------------------------------------------------------------
    // Clear entire display buffer (both panels) before starting
    // ------------------------------------------------------------
    dma_display->fillScreen(0);
    dma_display->flipDMABuffer(); // Clear back buffer if double buffering is enabled
    dma_display->fillScreen(0);

    while (millis() - gifStartTick < durationMs) {
        int result = gif.playFrame(true, nullptr);
        dma_display->flipDMABuffer();
        server.handleClient();

        if (result == 0) gif.reset();
    }

    gif.close();
}