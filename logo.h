#pragma once
#include <string>

class LogoWidget {
public:
    // Draw the logo onto the provided GraphicsContext + Font
    template <typename CtxType, typename FontType>
    void draw(CtxType* ctx, FontType* font, int width, int height) {

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
        const std::string text = "RGBop";
        const uint32_t textColor = 0x00FFFF;   // Cyan
        const uint32_t bgColor   = 0x000000;   // Black
        const int padding = 3;

        // Compute text width using your MatrixFont wrapper
        int textW = font->getTextWidth(text);
        int textH = 8;  // Your MatrixFont draws 8px tall glyphs

        // Center the text box
        int boxX = (width  - (textW + padding * 2)) / 2;
        int boxY = (height - (textH + padding * 2)) / 2;

        // --- 3. Draw Black Background Box ---
        ctx->setFillStyle(bgColor);
        ctx->fillRect(boxX, boxY, textW + padding * 2, textH + padding * 2);

        // --- 4. Draw Text ---
        ctx->setFillStyle(textColor);
        font->drawText(ctx, text, boxX + padding, boxY + padding + 7);
        // (MatrixFont subtracts 7 internally, so +7 aligns baseline)
    }
};