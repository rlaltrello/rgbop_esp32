// morphClock.h
#pragma once
#include <time.h>
#include <cmath>
#include <algorithm>
#include <vector>

class MorphClockWidget {
private:
    // --- STATIC GEOMETRY & MAPS ---
    const int SEGMENTS[10][7] = {
        {1,1,1,1,1,1,0}, // 0
        {0,1,1,0,0,0,0}, // 1
        {1,1,0,1,1,0,1}, // 2
        {1,1,1,1,0,0,1}, // 3
        {0,1,1,0,0,1,1}, // 4
        {1,0,1,1,0,1,1}, // 5
        {1,0,1,1,1,1,1}, // 6
        {1,1,1,0,0,0,0}, // 7
        {1,1,1,1,1,1,1}, // 8
        {1,1,1,1,0,1,1}  // 9
    };

    struct SegGeo {
        int x, y, w, h;
        bool horizontal;
    };

    const SegGeo SEGMENT_GEOMETRY[7] = {
        { 1,  0,  6, 2, true },  // A (0)
        { 7,  1,  2, 6, false }, // B (1)
        { 7,  9,  2, 6, false }, // C (2)
        { 1,  14, 6, 2, true },  // D (3)
        { 0,  9,  2, 6, false }, // E (4)
        { 0,  1,  2, 6, false }, // F (5)
        { 1,  7,  6, 2, true }   // G (6)
    };

    const int SLIDE_PAIRS[4][2] = {
        {5, 1}, // F <-> B 
        {4, 2}, // E <-> C 
        {0, 6}, // A <-> G
        {6, 3}  // G <-> D
    };

    const int FALLBACK_PAIRS[7] = { 6, 5, 4, 6, 2, 1, 0 };

    const int POSITIONS_X[6] = { 2, 12, 22, 32, 42, 52 };
    const int POSITIONS_Y = 24;

    // --- STATE VARIABLES ---
    int lastDigits[6] = {0,0,0,0,0,0};
    int nextDigits[6] = {0,0,0,0,0,0};
    unsigned long morphStarts[6] = {0,0,0,0,0,0}; 
    
    const unsigned long morphDuration = 400;
    const unsigned long staggerDelay = 300;

    bool isFirstRun = true;

    // --- ENUMS & STRUCTS FOR ACTIONS ---
    enum ActionType { A_OFF, A_STAY, A_APPEAR, A_DISAPPEAR, A_SLIDE, A_SLIDE_INTO, A_SLIDE_FROM, A_HANDLED };
    enum Anchor { ANC_CENTER, ANC_LEFT, ANC_RIGHT, ANC_TOP, ANC_BOTTOM };

    struct Action {
        ActionType type = A_OFF;
        int target = -1;
    };

    double ease(double t) {
        return t < 0.5 
            ? 4.0 * t * t * t 
            : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    }

public:
    template <typename CtxType, typename FontType>
    void draw(CtxType* ctx, FontType* font, int width, int height, unsigned long nowMs) {
        
        // --- FETCH TIME ---
        time_t now_c;
        time(&now_c);
        struct tm* parts = localtime(&now_c);

        int hh = parts->tm_hour;
        int mm = parts->tm_min;
        int ss = parts->tm_sec;

        int digits[6] = {
            hh / 10, hh % 10,
            mm / 10, mm % 10,
            ss / 10, ss % 10
        };

        // Initialize instantly on first run so we don't cascade from 00:00:00
        if (isFirstRun) {
            for (int i = 0; i < 6; i++) {
                lastDigits[i] = digits[i];
                nextDigits[i] = digits[i];
            }
            isFirstRun = false;
        }

        // --- CASCADE TRIGGER LOGIC ---
        bool changed = false;
        for (int i = 0; i < 6; i++) {
            if (digits[i] != nextDigits[i]) changed = true;
        }

        if (changed) {
            int cascadeIndex = 0;
            for (int i = 5; i >= 0; i--) {
                if (nextDigits[i] != digits[i]) {
                    lastDigits[i] = nextDigits[i];
                    nextDigits[i] = digits[i];
                    morphStarts[i] = nowMs + (cascadeIndex * staggerDelay);
                    cascadeIndex++;
                }
            }
        }

        // --- DRAW BACKGROUND ---
        ctx->setFillStyle(0x000000);
        ctx->fillRect(0, 0, width, height);

        ctx->setFillStyle(0x00FFAA); // Cyan/Greenish Color

        // --- RENDER LOOP ---
        for (int i = 0; i < 6; i++) {
            const int* last = SEGMENTS[lastDigits[i]];
            const int* next = SEGMENTS[nextDigits[i]];
            
            double rawT = static_cast<double>(nowMs - morphStarts[i]) / morphDuration;
            double t = std::max(0.0, std::min(1.0, rawT));
            double et = ease(t);

            int sx = POSITIONS_X[i];
            int sy = POSITIONS_Y;

            std::vector<int> turnOff;
            std::vector<int> turnOn;
            Action actions[7]; // Defaults to A_OFF

            for (int s = 0; s < 7; s++) {
                if (last[s] && !next[s]) turnOff.push_back(s);
                if (!last[s] && next[s]) turnOn.push_back(s);
                if (last[s] && next[s])  actions[s].type = A_STAY;
            }

            // Step A: Map direct 1-to-1 exchanges
            for (int p = 0; p < 4; p++) {
                int s1 = SLIDE_PAIRS[p][0];
                int s2 = SLIDE_PAIRS[p][1];
                
                bool s1Off = std::find(turnOff.begin(), turnOff.end(), s1) != turnOff.end();
                bool s2On = std::find(turnOn.begin(), turnOn.end(), s2) != turnOn.end();
                bool s2Off = std::find(turnOff.begin(), turnOff.end(), s2) != turnOff.end();
                bool s1On = std::find(turnOn.begin(), turnOn.end(), s1) != turnOn.end();

                if (s1Off && s2On) {
                    actions[s1] = { A_SLIDE, s2 };
                    actions[s2].type = A_HANDLED; 
                } else if (s2Off && s1On) {
                    actions[s2] = { A_SLIDE, s1 };
                    actions[s1].type = A_HANDLED;
                }
            }

            // Step B: Phantom Sliding for orphans turning OFF
            for (int s : turnOff) {
                if (actions[s].type == A_HANDLED || actions[s].type != A_OFF) continue;
                int partner = FALLBACK_PAIRS[s];
                if (next[partner]) {
                    actions[s] = { A_SLIDE_INTO, partner };
                } else {
                    actions[s].type = A_DISAPPEAR; 
                }
            }

            // Step C: Phantom Sliding for orphans turning ON
            for (int s : turnOn) {
                if (actions[s].type == A_HANDLED || actions[s].type != A_OFF) continue;
                int partner = FALLBACK_PAIRS[s];
                if (last[partner] || next[partner]) {
                    actions[s] = { A_SLIDE_FROM, partner };
                } else {
                    actions[s].type = A_APPEAR; 
                }
            }

            // --- EXECUTE ACTIONS ---
            for (int s = 0; s < 7; s++) {
                Action action = actions[s];
                if (action.type == A_OFF || action.type == A_HANDLED) continue;

                SegGeo seg = SEGMENT_GEOMETRY[s];
                int drawX = 0, drawY = 0, drawW = 0, drawH = 0;

                if (action.type == A_STAY) {
                    drawX = sx + seg.x;
                    drawY = sy + seg.y;
                    drawW = seg.w;
                    drawH = seg.h;
                } 
                else if (action.type == A_APPEAR || action.type == A_DISAPPEAR) {
                    double stretch = action.type == A_APPEAR ? et : 1.0 - et;
                    if (stretch <= 0.05) continue;

                    Anchor anchor = ANC_CENTER;
                    const int* refState = action.type == A_APPEAR ? next : last;

                    // Dynamic Anchoring Logic
                    if (seg.horizontal) {
                        bool leftOn = (s == 0 && refState[5]) || (s == 6 && (refState[4] || refState[5])) || (s == 3 && refState[4]);
                        bool rightOn = (s == 0 && refState[1]) || (s == 6 && (refState[2] || refState[1])) || (s == 3 && refState[2]);

                        if (rightOn && !leftOn) anchor = ANC_RIGHT;
                        else if (leftOn && !rightOn) anchor = ANC_LEFT;
                    } else {
                        bool topOn = ((s == 5 || s == 1) && refState[0]) || ((s == 4 || s == 2) && refState[6]);
                        bool bottomOn = ((s == 5 || s == 1) && refState[6]) || ((s == 4 || s == 2) && refState[3]);

                        if (topOn && !bottomOn) anchor = ANC_TOP;
                        else if (bottomOn && !topOn) anchor = ANC_BOTTOM;
                    }

                    // Apply Anchors to Geometry
                    if (seg.horizontal) {
                        drawW = std::max(1, static_cast<int>(std::round(seg.w * stretch)));
                        drawH = seg.h;
                        drawY = sy + seg.y;
                        
                        if (anchor == ANC_LEFT) drawX = sx + seg.x;
                        else if (anchor == ANC_RIGHT) drawX = sx + seg.x + seg.w - drawW;
                        else drawX = sx + seg.x + std::round((seg.w - drawW) / 2.0); 
                    } else {
                        drawW = seg.w;
                        drawH = std::max(1, static_cast<int>(std::round(seg.h * stretch)));
                        drawX = sx + seg.x;
                        
                        if (anchor == ANC_TOP) drawY = sy + seg.y;
                        else if (anchor == ANC_BOTTOM) drawY = sy + seg.y + seg.h - drawH;
                        else drawY = sy + seg.y + std::round((seg.h - drawH) / 2.0);
                    }
                } 
                else if (action.type == A_SLIDE) {
                    SegGeo targetSeg = SEGMENT_GEOMETRY[action.target];
                    drawW = seg.w; 
                    drawH = seg.h;
                    drawX = std::round((sx + seg.x) + ((targetSeg.x - seg.x) * et));
                    drawY = std::round((sy + seg.y) + ((targetSeg.y - seg.y) * et));
                }
                else if (action.type == A_SLIDE_INTO) {
                    SegGeo targetSeg = SEGMENT_GEOMETRY[action.target];
                    drawW = seg.w; 
                    drawH = seg.h;
                    drawX = std::round(sx + seg.x + (targetSeg.x - seg.x) * et);
                    drawY = std::round(sy + seg.y + (targetSeg.y - seg.y) * et);
                }
                else if (action.type == A_SLIDE_FROM) {
                    SegGeo sourceSeg = SEGMENT_GEOMETRY[action.target];
                    drawW = seg.w; 
                    drawH = seg.h;
                    drawX = std::round(sx + sourceSeg.x + (seg.x - sourceSeg.x) * et);
                    drawY = std::round(sy + sourceSeg.y + (seg.y - sourceSeg.y) * et);
                }

                ctx->fillRect(drawX, drawY, drawW, drawH);
            }
        }
    }
};