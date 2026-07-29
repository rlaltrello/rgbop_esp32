#ifndef SHOOTER_H
#define SHOOTER_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Fonts/TomThumb.h>
#include "GameModeManager.h"

enum Direction {
  DIR_UP,
  DIR_UP_RIGHT,
  DIR_RIGHT,
  DIR_DOWN_RIGHT,
  DIR_DOWN,
  DIR_DOWN_LEFT,
  DIR_LEFT,
  DIR_UP_LEFT
};

enum GameState {
  STATE_COUNTDOWN,
  STATE_PLAYING,
  STATE_EXPLODING,
  STATE_GAME_OVER
};

struct Bullet {
  int x = -1;
  int y = -1;
  int dx = 0;
  int dy = 0;
  int distanceTraveled = 0;  // Track how far the bullet has flown
  bool active = false;
};

struct Enemy {
  int x = -1;
  int y = -1;
  int dx = 0;
  int dy = 0;
  bool active = false;
};

struct Particle {
  float x = 0;
  float y = 0;
  float dx = 0;
  float dy = 0;
  uint16_t color = 0;
  int life = 0;
  bool active = false;
};

class ShooterGame {
private:
  static const int PANEL_WIDTH = 64;
  static const int PANEL_HEIGHT = 64;
  static const int MAX_BULLETS = 5;
  static const int MAX_ENEMIES = 10;
  static const int INITIAL_ENEMIES = 3;
  static const int MAX_PARTICLES = 16;
  static const int MAX_BULLET_DISTANCE = 32;

  int activeEnemyCount = INITIAL_ENEMIES;



  int playerX = 31;
  int playerY = 31;
  Direction playerDir = DIR_UP;

  int score = 0;

  GameState currentState = STATE_COUNTDOWN;

  // Countdown Timer
  unsigned long countdownStartMs = 0;
  const unsigned long COUNTDOWN_DURATION_MS = 3000;  // 3 Seconds

  // Explosion / Death Timer
  unsigned long playerDeathTime = 0;
  const unsigned long EXPLOSION_DURATION_MS = 3000;  // 3-second delay before Game Over screen

  Bullet bullets[MAX_BULLETS];
  Enemy enemies[MAX_ENEMIES];
  Particle particles[MAX_PARTICLES];

  GameInputState lastInput;
  bool lastBtnAState = false;
  bool lastBtnBState = false;

  unsigned long lastMoveTime = 0;
  unsigned long lastBulletTime = 0;
  unsigned long lastEnemyMoveTime = 0;
  unsigned long lastParticleTime = 0;
  unsigned long lastFireTime = 0;

  const unsigned long NORMAL_MOVE_INTERVAL_MS = 35;
  const unsigned long BOOST_MOVE_INTERVAL_MS = 18;
  const unsigned long BULLET_INTERVAL_MS = 15;
  const unsigned long ENEMY_MOVE_INTERVAL_MS = 80;
  const unsigned long PARTICLE_INTERVAL_MS = 20;
  const unsigned long FIRE_RATE_LIMIT_MS = 100;

  void spawnEnemy(int index) {
    int edge = random(0, 4);

    switch (edge) {
      case 0:
        enemies[index].x = random(4, PANEL_WIDTH - 4);
        enemies[index].y = -3;
        enemies[index].dx = 0;
        enemies[index].dy = 1;
        break;
      case 1:
        enemies[index].x = PANEL_WIDTH + 3;
        enemies[index].y = random(4, PANEL_HEIGHT - 4);
        enemies[index].dx = -1;
        enemies[index].dy = 0;
        break;
      case 2:
        enemies[index].x = random(4, PANEL_WIDTH - 4);
        enemies[index].y = PANEL_HEIGHT + 3;
        enemies[index].dx = 0;
        enemies[index].dy = -1;
        break;
      case 3:
        enemies[index].x = -3;
        enemies[index].y = random(4, PANEL_HEIGHT - 4);
        enemies[index].dx = 1;
        enemies[index].dy = 0;
        break;
    }

    enemies[index].active = true;
  }

  void triggerExplosion(int x, int y, MatrixPanel_I2S_DMA* display) {
    uint16_t colors[4] = {
      display ? display->color565(255, 255, 255) : 0xFFFF,
      display ? display->color565(255, 200, 0) : 0xFFE0,
      display ? display->color565(255, 100, 0) : 0xFC00,
      display ? display->color565(200, 0, 0) : 0xF800
    };

    for (int i = 0; i < 12; i++) {
      particles[i].x = x;
      particles[i].y = y;

      float angle = (i / 12.0f) * 2.0f * 3.14159f;
      float speed = random(8, 20) / 10.0f;

      particles[i].dx = cos(angle) * speed;
      particles[i].dy = sin(angle) * speed;
      particles[i].color = colors[random(0, 4)];
      particles[i].life = random(15, 30);
      particles[i].active = true;
    }
  }

public:
void reset() {
    playerX = 31;
    playerY = 31;
    playerDir = DIR_UP;
    score = 0;
    activeEnemyCount = INITIAL_ENEMIES;
    currentState = STATE_COUNTDOWN;
    countdownStartMs = millis();
    playerDeathTime = 0;

    for (int i = 0; i < MAX_BULLETS; i++) bullets[i] = Bullet();
    for (int i = 0; i < MAX_PARTICLES; i++) particles[i] = Particle();
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i] = Enemy();

    // Spawn starting batch
    for (int i = 0; i < activeEnemyCount; i++) {
      spawnEnemy(i);
    }

    lastInput = GameInputState();
    lastBtnAState = false;
    lastBtnBState = false;
    lastFireTime = 0;
  }

  void update(const GameInputState& input) {
    unsigned long now = millis();

    // Edge detection for button presses
    bool btnAPressed = input.btnA && !lastBtnAState;
    bool btnBPressed = input.btnB && !lastBtnBState;

    // --- 1. COUNTDOWN STATE (3-Second Startup Delay) ---
    if (currentState == STATE_COUNTDOWN) {
      if (now - countdownStartMs >= COUNTDOWN_DURATION_MS) {
        currentState = STATE_PLAYING;
      } else {
        lastBtnAState = input.btnA;
        lastBtnBState = input.btnB;
        lastInput = input;
        return;  // Freeze entity updates until countdown finishes
      }
    }

    // --- GAME OVER STATE ---
    if (currentState == STATE_GAME_OVER) {
      // 1. Pressing Button A or B restarts game
      if (btnAPressed || btnBPressed) {
        reset();
        return;
      }

      // 2. Auto-exit back to widgets after 15 seconds of sitting on GAME OVER screen
      if (now - playerDeathTime >= (EXPLOSION_DURATION_MS + 15000)) {  // 3s explosion + 15s idle
        gameManager.exitGameMode();                                    // Drops back to clock/weather rotation!
        return;
      }

      lastBtnAState = input.btnA;
      lastBtnBState = input.btnB;
      lastInput = input;
      return;
    }

    // --- 3. EXPLODING STATE (3-Second Delay before Game Over) ---
    if (currentState == STATE_EXPLODING) {
      if (now - playerDeathTime >= EXPLOSION_DURATION_MS) {
        currentState = STATE_GAME_OVER;
      }
    }

    // --- 4. ACTIVE PLAYING STATE ---
    if (currentState == STATE_PLAYING) {
      unsigned long moveInterval = input.btnB ? BOOST_MOVE_INTERVAL_MS : NORMAL_MOVE_INTERVAL_MS;
      bool canMove = (now - lastMoveTime >= moveInterval);

      int moveX = 0;
      int moveY = 0;

      if (input.up) moveY--;
      if (input.down) moveY++;
      if (input.left) moveX--;
      if (input.right) moveX++;

      bool dirChanged = (input.up != lastInput.up) || (input.down != lastInput.down) || (input.left != lastInput.left) || (input.right != lastInput.right);

      if ((moveX != 0 || moveY != 0) && (canMove || dirChanged)) {
        // Apply movement with screen wrapping
        playerX += moveX;
        playerY += moveY;

        // Wrap X axis (0 to 63)
        if (playerX < 0) playerX = PANEL_WIDTH - 1;
        else if (playerX >= PANEL_WIDTH) playerX = 0;

        // Wrap Y axis (0 to 63)
        if (playerY < 0) playerY = PANEL_HEIGHT - 1;
        else if (playerY >= PANEL_HEIGHT) playerY = 0;

        // Update direction facing based on stick input
        if (moveY < 0 && moveX == 0) playerDir = DIR_UP;
        else if (moveY < 0 && moveX > 0) playerDir = DIR_UP_RIGHT;
        else if (moveY == 0 && moveX > 0) playerDir = DIR_RIGHT;
        else if (moveY > 0 && moveX > 0) playerDir = DIR_DOWN_RIGHT;
        else if (moveY > 0 && moveX == 0) playerDir = DIR_DOWN;
        else if (moveY > 0 && moveX < 0) playerDir = DIR_DOWN_LEFT;
        else if (moveY == 0 && moveX < 0) playerDir = DIR_LEFT;
        else if (moveY < 0 && moveX < 0) playerDir = DIR_UP_LEFT;

        lastMoveTime = now;
      }

      // Primary Fire
      if (btnAPressed && (now - lastFireTime >= FIRE_RATE_LIMIT_MS)) {
        for (int i = 0; i < MAX_BULLETS; i++) {
          if (!bullets[i].active) {
            bullets[i].x = playerX;
            bullets[i].y = playerY;
            bullets[i].distanceTraveled = 0;  // Reset distance
            bullets[i].active = true;

            switch (playerDir) {
              case DIR_UP:
                bullets[i].dx = 0;
                bullets[i].dy = -1;
                break;
              case DIR_UP_RIGHT:
                bullets[i].dx = 1;
                bullets[i].dy = -1;
                break;
              case DIR_RIGHT:
                bullets[i].dx = 1;
                bullets[i].dy = 0;
                break;
              case DIR_DOWN_RIGHT:
                bullets[i].dx = 1;
                bullets[i].dy = 1;
                break;
              case DIR_DOWN:
                bullets[i].dx = 0;
                bullets[i].dy = 1;
                break;
              case DIR_DOWN_LEFT:
                bullets[i].dx = -1;
                bullets[i].dy = 1;
                break;
              case DIR_LEFT:
                bullets[i].dx = -1;
                bullets[i].dy = 0;
                break;
              case DIR_UP_LEFT:
                bullets[i].dx = -1;
                bullets[i].dy = -1;
                break;
            }

            bullets[i].x += bullets[i].dx;
            bullets[i].y += bullets[i].dy;

            lastFireTime = now;
            break;
          }
        }
      }
    }

    lastBtnAState = input.btnA;
    lastBtnBState = input.btnB;
    lastInput = input;

    // --- 5. BULLET UPDATES ---
    if (now - lastBulletTime >= BULLET_INTERVAL_MS) {
      for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
          // Advance bullet position
          bullets[i].x += bullets[i].dx;
          bullets[i].y += bullets[i].dy;
          bullets[i].distanceTraveled++;

          // Wrap X axis
          if (bullets[i].x < 0) bullets[i].x = PANEL_WIDTH - 1;
          else if (bullets[i].x >= PANEL_WIDTH) bullets[i].x = 0;

          // Wrap Y axis
          if (bullets[i].y < 0) bullets[i].y = PANEL_HEIGHT - 1;
          else if (bullets[i].y >= PANEL_HEIGHT) bullets[i].y = 0;

          // Deactivate bullet if it exceeds max travel distance
          if (bullets[i].distanceTraveled >= MAX_BULLET_DISTANCE) {
            bullets[i].active = false;
          }
        }
      }
      lastBulletTime = now;
    }

    // --- 6. ENEMY UPDATES ---
    if (now - lastEnemyMoveTime >= ENEMY_MOVE_INTERVAL_MS) {
      for (int i = 0; i < activeEnemyCount; i++) {
        if (enemies[i].active) {
          enemies[i].x += enemies[i].dx;
          enemies[i].y += enemies[i].dy;

          // Wrap enemies cleanly across matrix borders
          if (enemies[i].x < -2) enemies[i].x = PANEL_WIDTH + 2;
          else if (enemies[i].x > PANEL_WIDTH + 2) enemies[i].x = -2;

          if (enemies[i].y < -2) enemies[i].y = PANEL_HEIGHT + 2;
          else if (enemies[i].y > PANEL_HEIGHT + 2) enemies[i].y = -2;
        }
      }
      lastEnemyMoveTime = now;
    }

    // --- 7. PARTICLE UPDATES ---
    if (now - lastParticleTime >= PARTICLE_INTERVAL_MS) {
      for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].active) {
          particles[i].x += particles[i].dx;
          particles[i].y += particles[i].dy;
          particles[i].life--;

          if (particles[i].life <= 0 || particles[i].x < 0 || particles[i].x >= PANEL_WIDTH || particles[i].y < 0 || particles[i].y >= PANEL_HEIGHT) {
            particles[i].active = false;
          }
        }
      }
      lastParticleTime = now;
    }

// --- 8. BULLET vs ENEMY COLLISIONS ---
if (currentState == STATE_PLAYING) {
  for (int b = 0; b < MAX_BULLETS; b++) {
    if (!bullets[b].active) continue;

    for (int e = 0; e < activeEnemyCount; e++) {
      if (!enemies[e].active) continue;

      int dx = abs(bullets[b].x - enemies[e].x);
      int dy = abs(bullets[b].y - enemies[e].y);

      if (dx > PANEL_WIDTH / 2) dx = PANEL_WIDTH - dx;
      if (dy > PANEL_HEIGHT / 2) dy = PANEL_HEIGHT - dy;

      int dist = dx + dy;
      if (dist <= 2) {
        bullets[b].active = false;
        score += 100;
        spawnEnemy(e);

        // --- SCALE ENEMIES EVERY 1000 POINTS ---
        int desiredEnemies = INITIAL_ENEMIES + (score / 1000);
        if (desiredEnemies > MAX_ENEMIES) desiredEnemies = MAX_ENEMIES;

        // If threshold crossed, activate and spawn the next enemy slot
        if (desiredEnemies > activeEnemyCount) {
          int newIndex = activeEnemyCount;
          activeEnemyCount = desiredEnemies;
          spawnEnemy(newIndex);
        }

        break;
      }
    }
  }

      // --- 9. PLAYER vs ENEMY COLLISIONS ---
      for (int e = 0; e < activeEnemyCount; e++) {
        if (!enemies[e].active) continue;

        // Calculate wrapped shortest distance
        int dx = abs(playerX - enemies[e].x);
        int dy = abs(playerY - enemies[e].y);

        if (dx > PANEL_WIDTH / 2) dx = PANEL_WIDTH - dx;
        if (dy > PANEL_HEIGHT / 2) dy = PANEL_HEIGHT - dy;

        int dist = dx + dy;

        if (dist <= 3) {
          currentState = STATE_EXPLODING;
          playerDeathTime = now;
          triggerExplosion(playerX, playerY, nullptr);
          spawnEnemy(e);
          break;
        }
      }
    }
  }

  void draw(MatrixPanel_I2S_DMA& display) {
    display.fillScreen(display.color565(0, 0, 0));

    uint16_t shipColor = lastInput.btnB ? display.color565(0, 255, 255) : display.color565(0, 255, 0);
    uint16_t bulletColor = display.color565(255, 255, 0);
    uint16_t darkRed = display.color565(128, 0, 0);

    // --- DRAW COUNTDOWN OVERLAY ---
    if (currentState == STATE_COUNTDOWN) {
      // Draw initial ship position
      drawShip(display, shipColor);

      display.setFont(&TomThumb);

      // "GET READY!" Line
      display.setTextColor(display.color565(255, 255, 0));  // Yellow
      const char* readyTxt = "GET READY!";
      int16_t x1, y1;
      uint16_t w1, h1;
      display.getTextBounds(readyTxt, 0, 0, &x1, &y1, &w1, &h1);
      display.setCursor((PANEL_WIDTH - w1) / 2, 22);
      display.print(readyTxt);

      // Countdown Number (3.. 2.. 1)
      unsigned long elapsed = millis() - countdownStartMs;
      int remainingSec = 3 - (elapsed / 1000);
      if (remainingSec < 1) remainingSec = 1;

      char numStr[2];
      snprintf(numStr, sizeof(numStr), "%d", remainingSec);
      display.setTextColor(display.color565(255, 255, 255));  // White

      uint16_t w2, h2;
      display.getTextBounds(numStr, 0, 0, &x1, &y1, &w2, &h2);
      display.setCursor((PANEL_WIDTH - w2) / 2, 34);
      display.print(numStr);

      drawScore(display);
      display.setFont(NULL);
      return;
    }

    // --- DRAW GAME OVER OVERLAY ---
    if (currentState == STATE_GAME_OVER) {
      display.setFont(&TomThumb);
      display.setTextColor(display.color565(255, 0, 0));  // Red GAME OVER

      const char* line1 = "GAME OVER";
      int16_t x1, y1;
      uint16_t w1, h1;
      display.getTextBounds(line1, 0, 0, &x1, &y1, &w1, &h1);
      display.setCursor((PANEL_WIDTH - w1) / 2, 26);
      display.print(line1);

      display.setTextColor(display.color565(204, 85, 0));
      const char* line2 = "PRESS BUTTON";
      uint16_t w2, h2;
      display.getTextBounds(line2, 0, 0, &x1, &y1, &w2, &h2);
      display.setCursor((PANEL_WIDTH - w2) / 2, 38);
      display.print(line2);

      const char* line3 = "TO RESTART";
      uint16_t w3, h3;
      display.getTextBounds(line3, 0, 0, &x1, &y1, &w3, &h3);
      display.setCursor((PANEL_WIDTH - w3) / 2, 46);
      display.print(line3);

      drawScore(display);
      display.setFont(NULL);
      return;
    }

    // --- DRAW SHIP (Only while alive) ---
    if (currentState == STATE_PLAYING) {
      drawShip(display, shipColor);
    }

    // Draw Enemies
    for (int i = 0; i < activeEnemyCount; i++) {
      if (enemies[i].active) {
        int ex = enemies[i].x;
        int ey = enemies[i].y;

        display.drawPixel(ex, ey - 2, darkRed);
        display.drawPixel(ex - 1, ey - 1, darkRed);
        display.drawPixel(ex, ey - 1, darkRed);
        display.drawPixel(ex + 1, ey - 1, darkRed);
        display.drawPixel(ex - 2, ey, darkRed);
        display.drawPixel(ex - 1, ey, darkRed);
        display.drawPixel(ex, ey, darkRed);
        display.drawPixel(ex + 1, ey, darkRed);
        display.drawPixel(ex + 2, ey, darkRed);
        display.drawPixel(ex - 1, ey + 1, darkRed);
        display.drawPixel(ex, ey + 1, darkRed);
        display.drawPixel(ex + 1, ey + 1, darkRed);
        display.drawPixel(ex, ey + 2, darkRed);
      }
    }

    // Draw Bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
      if (bullets[i].active) {
        display.drawPixel(bullets[i].x, bullets[i].y, bulletColor);
      }
    }

    // Draw Explosion Particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
      if (particles[i].active) {
        display.drawPixel((int)particles[i].x, (int)particles[i].y, particles[i].color);
      }
    }

    drawScore(display);
    display.setFont(NULL);
  }

private:
  void drawShip(MatrixPanel_I2S_DMA& display, uint16_t shipColor) {
    switch (playerDir) {
      case DIR_UP:
        display.drawPixel(playerX, playerY - 1, shipColor);
        display.drawPixel(playerX - 1, playerY, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX + 1, playerY, shipColor);
        break;
      case DIR_UP_RIGHT:
        display.drawPixel(playerX + 1, playerY - 1, shipColor);
        display.drawPixel(playerX - 1, playerY, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX, playerY + 1, shipColor);
        break;
      case DIR_RIGHT:
        display.drawPixel(playerX + 1, playerY, shipColor);
        display.drawPixel(playerX, playerY - 1, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX, playerY + 1, shipColor);
        break;
      case DIR_DOWN_RIGHT:
        display.drawPixel(playerX + 1, playerY + 1, shipColor);
        display.drawPixel(playerX - 1, playerY, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX, playerY - 1, shipColor);
        break;
      case DIR_DOWN:
        display.drawPixel(playerX, playerY + 1, shipColor);
        display.drawPixel(playerX - 1, playerY, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX + 1, playerY, shipColor);
        break;
      case DIR_DOWN_LEFT:
        display.drawPixel(playerX - 1, playerY + 1, shipColor);
        display.drawPixel(playerX + 1, playerY, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX, playerY - 1, shipColor);
        break;
      case DIR_LEFT:
        display.drawPixel(playerX - 1, playerY, shipColor);
        display.drawPixel(playerX, playerY - 1, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX, playerY + 1, shipColor);
        break;
      case DIR_UP_LEFT:
        display.drawPixel(playerX - 1, playerY - 1, shipColor);
        display.drawPixel(playerX + 1, playerY, shipColor);
        display.drawPixel(playerX, playerY, shipColor);
        display.drawPixel(playerX, playerY + 1, shipColor);
        break;
    }
  }

  void drawScore(MatrixPanel_I2S_DMA& display) {
    display.setFont(&TomThumb);
    display.setTextColor(display.color565(204, 85, 0));

    char scoreStr[10];
    snprintf(scoreStr, sizeof(scoreStr), "%d", score);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(scoreStr, 0, 0, &x1, &y1, &w, &h);

    int textX = PANEL_WIDTH - w - 1;
    int textY = 6;

    display.setCursor(textX, textY);
    display.print(scoreStr);
  }
};

#endif