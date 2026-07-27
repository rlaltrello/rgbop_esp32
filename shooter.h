#ifndef SHOOTER_H
#define SHOOTER_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "GameModeManager.h"

enum Direction {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT
};

extern int actualPanelX;
extern int actualPanelY;

class ShooterGame {
private:
    const int PANEL_WIDTH = actualPanelX;
    const int PANEL_HEIGHT = actualPanelY;

    int playerX = actualPanelX/2;
    int playerY = actualPanelY/2;
    Direction playerDir = DIR_UP;

    // Bullet state
    int bulletX = -1;
    int bulletY = -1;
    int bulletDX = 0; // Directional movement X (-1, 0, or 1)
    int bulletDY = 0; // Directional movement Y (-1, 0, or 1)

    bool lastFireState = false;

    unsigned long lastMoveTime = 0;
    unsigned long lastBulletTime = 0;

    const unsigned long MOVE_INTERVAL_MS = 30;   // Player speed
    const unsigned long BULLET_INTERVAL_MS = 15; // Bullet speed

public:
    int getX() const { return playerX; }
    int getY() const { return playerY; }
    int getDir() const { return (int)playerDir; }
    int getBulletX() const { return bulletX; }
    int getBulletY() const { return bulletY; }

    void reset() {
        playerX = actualPanelX/2;
        playerY = actualPanelY/2;
        playerDir = DIR_UP;
        bulletX = -1;
        bulletY = -1;
        bulletDX = 0;
        bulletDY = 0;
        lastFireState = false;
    }

    void update(const GameInputState& input) {
        unsigned long now = millis();

        // 1. Handle Movement & Direction Updates
        if (now - lastMoveTime >= MOVE_INTERVAL_MS) {
            if (input.up && playerY > 1) {
                playerY--;
                playerDir = DIR_UP;
            } else if (input.down && playerY < PANEL_HEIGHT - 2) {
                playerY++;
                playerDir = DIR_DOWN;
            } else if (input.left && playerX > 1) {
                playerX--;
                playerDir = DIR_LEFT;
            } else if (input.right && playerX < PANEL_WIDTH - 2) {
                playerX++;
                playerDir = DIR_RIGHT;
            }
            lastMoveTime = now;
        }

        // 2. Handle Fire (Spawn bullet traveling in current ship direction)
        if (input.fire && !lastFireState) {
            if (bulletX < 0 && bulletY < 0) { // Only 1 active bullet at a time
                bulletX = playerX;
                bulletY = playerY;

                switch (playerDir) {
                    case DIR_UP:    bulletDX =  0; bulletDY = -1; bulletY--; break;
                    case DIR_DOWN:  bulletDX =  0; bulletDY =  1; bulletY++; break;
                    case DIR_LEFT:  bulletDX = -1; bulletDY =  0; bulletX--; break;
                    case DIR_RIGHT: bulletDX =  1; bulletDY =  0; bulletX++; break;
                }
            }
        }
        lastFireState = input.fire;

        // 3. Move Active Bullet
        if ((bulletX >= 0 || bulletY >= 0) && (now - lastBulletTime >= BULLET_INTERVAL_MS)) {
            bulletX += bulletDX;
            bulletY += bulletDY;

            // Despawn bullet if it leaves display boundaries
            if (bulletX < 0 || bulletX >= PANEL_WIDTH || bulletY < 0 || bulletY >= PANEL_HEIGHT) {
                bulletX = -1;
                bulletY = -1;
            }
            lastBulletTime = now;
        }
    }

    void draw(MatrixPanel_I2S_DMA& display) {
        // Clear background
        display.fillScreen(display.color565(0, 0, 0));

        uint16_t shipColor = display.color565(0, 255, 0);   // Green
        uint16_t bulletColor = display.color565(255, 255, 0); // Yellow

        // Draw Ship based on Facing Direction (T-shape / Arrowhead)
        switch (playerDir) {
            case DIR_UP:
                display.drawPixel(playerX, playerY - 1, shipColor);     // Nose
                display.drawPixel(playerX - 1, playerY, shipColor);     // Left wing
                display.drawPixel(playerX, playerY, shipColor);         // Center
                display.drawPixel(playerX + 1, playerY, shipColor);     // Right wing
                break;

            case DIR_DOWN:
                display.drawPixel(playerX, playerY + 1, shipColor);     // Nose
                display.drawPixel(playerX - 1, playerY, shipColor);     // Left wing
                display.drawPixel(playerX, playerY, shipColor);         // Center
                display.drawPixel(playerX + 1, playerY, shipColor);     // Right wing
                break;

            case DIR_LEFT:
                display.drawPixel(playerX - 1, playerY, shipColor);     // Nose
                display.drawPixel(playerX, playerY - 1, shipColor);     // Top wing
                display.drawPixel(playerX, playerY, shipColor);         // Center
                display.drawPixel(playerX, playerY + 1, shipColor);     // Bottom wing
                break;

            case DIR_RIGHT:
                display.drawPixel(playerX + 1, playerY, shipColor);     // Nose
                display.drawPixel(playerX, playerY - 1, shipColor);     // Top wing
                display.drawPixel(playerX, playerY, shipColor);         // Center
                display.drawPixel(playerX, playerY + 1, shipColor);     // Bottom wing
                break;
        }

        // Draw Bullet
        if (bulletX >= 0 && bulletY >= 0) {
            display.drawPixel(bulletX, bulletY, bulletColor);
        }
    }
};

#endif