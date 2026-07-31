#pragma once
#include <string>

extern String prefPanelName;

class DiagWidget {
public:
    // Draw the logo onto the provided GraphicsContext + Font
    template <typename CtxType, typename FontType>
bool draw(CtxType* ctx, FontType* font, int width, int height, unsigned long currentTime) {

int thirdWidth = width / 3;

// Left 1/3 = RED
ctx->setFillStyle(0xFF0000);
ctx->fillRect(0, 0, thirdWidth, height);

// Middle 1/3 = BLUE
ctx->setFillStyle(0x0000FF);
ctx->fillRect(thirdWidth, 0, thirdWidth, height);

// Right 1/3 = GREEN (Fill remaining span to the right boundary)
ctx->setFillStyle(0x00FF00);
ctx->fillRect(2 * thirdWidth, 0, width - (2 * thirdWidth), height);

    // --- 2. Text Setup ---
    const std::string text = std::string(prefPanelName.c_str()) + " - " + 
                         WiFi.getHostname() + " - " + 
                         WiFi.localIP().toString().c_str();
    const uint32_t textColor = 0x00FFFF;   // Cyan
    const uint32_t bgColor   = 0x000000;   // Black
    const int padding = 3;

    int textW = font->getTextWidth(text);
    int textH = 8;  // MatrixFont glyph height

    // --- 3. Scroll Calculation ---
    const unsigned long msPerPixel = 30; // Milliseconds per pixel shift
    int totalDistance = width + textW;  // Distance for 1 complete pass

    // Calculate total pixels shift elapsed
    unsigned long totalPixelsMoved = currentTime / msPerPixel;

    // Exit early if 1 full passes (1 * totalDistance) have completed
    if (totalPixelsMoved >= (unsigned long)(1 * totalDistance)) {
        return true; // Signal completion to caller
    }

    // Compute active X position relative to the current loop pass
    int textX = width - (int)(totalPixelsMoved % totalDistance);

    // Center the background band vertically
    int boxY = (height - (textH + padding * 2)) / 2;

    // --- 4. Draw Full-Width Black Background Band ---
    ctx->setFillStyle(bgColor);
    ctx->fillRect(0, boxY, width, textH + padding * 2);

    // --- 5. Draw Text ---
    ctx->setFillStyle(textColor);
    font->drawText(ctx, text, textX, boxY + padding + 7);

    return false; // Still animating
}
};