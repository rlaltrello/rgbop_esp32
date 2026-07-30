# RGBop ESP32

An ESP32-powered HUB75 RGB matrix information display that rotates through a collection of internet-connected widgets including weather, radar, clocks, aviation tracking, ISS tracking, Spotify status, earthquakes, custom messages, diagnostics, and user-created artwork.

Built for ESP32 and HUB75 RGB LED matrix panels using the ESP32-HUB75-MatrixPanel-I2S-DMA library for high-performance rendering.

---

## Features

### Display Widgets

- Animated clock display
- Morphing digital clock
- Date progress indicator
- Weather conditions
- Weather radar animation
- International Space Station tracking
- Nearby aircraft tracking
- Earthquake alerts
- Spotify "Now Playing"
- Custom scrolling messages
- User doodles and artwork
- System diagnostics
- Logo/splash screen

### Configuration

- Wi-Fi configuration
- Brightness control
- Automatic night mode
- Geographic location settings
- Widget enable/disable controls
- Radar zoom and display options
- Spotify integration
- OpenSky aircraft integration

### Storage

- LittleFS-based persistent storage
- JSON configuration management
- OTA partition layout
- Doodle/artwork file storage

### Display Management

- Configurable widget rotation timing
- Night vision mode
- Automatic brightness switching
- Full-screen HUB75 rendering
- Multiple panel size support

---

## Hardware Requirements

### Required

- ESP32 Development Board
- HUB75 RGB LED Matrix Panel
- 5V Power Supply (adequately sized for panel)
- USB cable for programming

### Recommended

- ESP32-WROOM or ESP32-S3
- 64x64 HUB75 panel
- Dedicated 5V power supply capable of supplying panel current requirements

---

## Software Stack

### Core Libraries

- ESP32 Arduino Framework
- ESP32-HUB75-MatrixPanel-I2S-DMA
- Adafruit GFX
- ArduinoJson
- LittleFS

### Services Used

- Weather data provider
- Weather radar imagery
- OpenSky Network (aircraft tracking)
- Spotify Web API
- ISS location services
- Earthquake monitoring feeds

---

## Widget Rotation

The display cycles through enabled widgets automatically.

Available widgets include:

| Widget | Description |
|----------|-------------|
| Clock | Mario-style clock display |
| Morph Clock | Animated digital clock |
| Date | Year progress indicator |
| Weather | Current weather conditions |
| Radar | Animated weather radar |
| ISS | Live International Space Station tracking |
| Planes | Nearby aircraft display |
| Earthquakes | Recent earthquake data |
| Spotify | Current playback information |
| Text Blast | Scrolling custom messages |
| Doodles | User-created artwork |
| Diagnostics | System information |

---


