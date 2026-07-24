#pragma once
#include <string>

class DiagWidget {
public:
    // Draw the logo onto the provided GraphicsContext + Font
    template <typename CtxType, typename FontType>
bool draw(CtxType* ctx, FontType* font, int width, int height, unsigned long currentTime) {

    // --- 1. Draw Vertical Stripes ---
    // Left 1/3 = RED
    ctx->setFillStyle(0xFF0000);   // Red
    ctx->fillRect(0, 0, width / 3, height);

    // Middle 1/3 = BLUE
    ctx->setFillStyle(0x0000FF);   // Blue
    ctx->fillRect(width / 3, 0, width / 3, height);

    // Right 1/3 = GREEN
    ctx->setFillStyle(0x00FF00);   // Green
    ctx->fillRect(2 * (width / 3), 0, width / 3, height);

    // --- 2. Text Setup ---
    const std::string text = std::string(WiFi.getHostname()) + " - " + WiFi.localIP().toString().c_str();
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

    // Exit early if 2 full passes (2 * totalDistance) have completed
    if (totalPixelsMoved >= (unsigned long)(2 * totalDistance)) {
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