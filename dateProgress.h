// dateProgress.h
#pragma once
#include <time.h>
#include <string>
#include <cmath>
#include <algorithm>

class DateProgressWidget {
private:
    unsigned long lastDrawn = 0;
    unsigned long animStart = 0;

public:
    // --- TEMPLATE FIX ---
    // This allows the function to accept your MatrixGraphics and MatrixFont 
    // directly without needing to know their underlying base classes ahead of time!
    template <typename CtxType, typename FontType>
    void draw(CtxType* ctx, FontType* font, int width, int height, unsigned long nowMs) {
        
        // --- ANIMATION TRIGGER ---
        if (nowMs - lastDrawn > 1000) {
            animStart = nowMs;
        }
        lastDrawn = nowMs;

        double ease = std::min(static_cast<double>(nowMs - animStart) / 1200.0, 1.0);
        ease = 1.0 - std::pow(1.0 - ease, 3); // Cubic ease-out

        // --- TIME MATH ---
        time_t now_c;
        time(&now_c);
        struct tm* parts = localtime(&now_c);

        // Safety check: If NTP hasn't synced (year < 2020), just draw black and return
        if (parts->tm_year < 120) {
            ctx->setFillStyle(0x000000);
            ctx->fillRect(0, 0, width, height);
            return;
        }

        // 1. Day Progress
        struct tm sod_tm = *parts;
        sod_tm.tm_hour = 0;
        sod_tm.tm_min = 0;
        sod_tm.tm_sec = 0;
        time_t startOfDay = mktime(&sod_tm);
        time_t endOfDay = startOfDay + 86400; // + 24 hours
        double dayProgress = static_cast<double>(now_c - startOfDay) / (endOfDay - startOfDay);

        // 2. Month Progress
        struct tm som_tm = sod_tm;
        som_tm.tm_mday = 1;
        time_t startOfMonth = mktime(&som_tm);
        
        struct tm eom_tm = som_tm;
        eom_tm.tm_mon += 1; // mktime auto-normalizes December -> January
        time_t endOfMonth = mktime(&eom_tm);
        double monthProgress = static_cast<double>(now_c - startOfMonth) / (endOfMonth - startOfMonth);

        // 3. Year Progress
        struct tm soy_tm = som_tm;
        soy_tm.tm_mon = 0;
        time_t startOfYear = mktime(&soy_tm);
        
        struct tm eoy_tm = soy_tm;
        eoy_tm.tm_year += 1;
        time_t endOfYear = mktime(&eoy_tm);
        double yearProgress = static_cast<double>(now_c - startOfYear) / (endOfYear - startOfYear);

        // --- DRAWING PHASE ---
        ctx->setFillStyle(0x000000);
        ctx->fillRect(0, 0, width, height);

        // Helper lambda to draw rows
        // Helper lambda to draw rows dynamically across screen width
auto drawBar = [&](const std::string& label, double progress, int y, uint32_t colorHex, uint32_t bgHex) {
    double currentProgress = std::max(0.0, std::min(progress, 1.0)) * ease;

    const int Y_NUDGE = 8;
    int textY = y + Y_NUDGE;

    // 1. Draw Label ("D", "M", "Y")
    ctx->setFillStyle(0xFFFFFF); // White
    font->drawText(ctx, label, 2, textY);

    // 2. Dynamic Bar Positioning based on panel width
    const int barX = 14;
    const int rightMargin = 2; // Keep a 2px pad on the far right
    int barWidth = width - barX - rightMargin; // Automatically becomes 112px on 128px displays!
    if (barWidth < 10) barWidth = 10; // Safety floor

    ctx->setFillStyle(bgHex);
    ctx->fillRect(barX, y, barWidth, 10);

    // 3. Draw Fill
    int fillWidth = std::max(1, static_cast<int>(std::floor(barWidth * currentProgress)));
    if (currentProgress > 0) {
        ctx->setFillStyle(colorHex);
        ctx->fillRect(barX, y, fillWidth, 10);

        // Bright white leading tip
        ctx->setFillStyle(0xFFFFFF);
        ctx->fillRect(barX + fillWidth - 1, y, 1, 10);
    }

    // 4. Percentage Text
    std::string pctText = std::to_string(static_cast<int>(std::floor(currentProgress * 100))) + "%";
    int pctWidth = font->getTextWidth(pctText);

    // Place text inside un-filled area if there's room, otherwise place inside filled area
    if (barWidth - fillWidth > pctWidth + 4) {
        ctx->setFillStyle(0xFFFFFF);
        int textX = barX + fillWidth + 2;
        font->drawText(ctx, pctText, textX, textY);
    } else {
        ctx->setFillStyle(0x000000);
        int textX = barX + fillWidth - pctWidth - 2;
        font->drawText(ctx, pctText, textX, textY);
    }
};

        // Draw the three bars evenly spaced
        drawBar("D", dayProgress, 8, 0xFF0000, 0x330000); // Red
        drawBar("M", monthProgress, 26, 0x00FF00, 0x003300); // Green
        drawBar("Y", yearProgress, 44, 0x00FFFF, 0x003333); // Cyan
    }
};