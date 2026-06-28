# JC3248W535C — ESP32-S3 Board Reference Guide

## Board Specs

| Spec | Detail |
|---|---|
| Model | JC3248W535C_I |
| Brand | GUITION |
| MCU | ESP32-S3, dual-core, up to 240MHz |
| Display | 3.5" IPS, 480×320, capacitive touch |
| RAM | 8MB PSRAM (OPI) |
| Flash | 16MB |
| Wireless | WiFi + Bluetooth |
| Power | USB-C + JST 1.25 2P LiPo battery connector |
| Extras | TF card slot, battery switch, expansion headers |
| IDE Support | Arduino, LVGL, Scratch 3.0, Mixly |

---

## Arduino IDE Board Settings

Go to **Tools** and set exactly:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| PSRAM | **OPI PSRAM** ⚠️ |
| Flash Mode | QIO 80MHz |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| Upload Speed | 921600 |
| USB CDC On Boot | **Enabled** ⚠️ |
| CPU Frequency | 240MHz |
| Port | COM3 (may vary) |

> ⚠️ **OPI PSRAM** and **USB CDC On Boot = Enabled** are critical — wrong values will cause boot loops or compile failures.

---

## Required Libraries

Install via **Sketch → Include Library → Manage Libraries**:

| Search | Install | By |
|---|---|---|
| `LVGL` | lvgl | Gabor Kiss-Vamosi |
| `LovyanGFX` | LovyanGFX | lovyan03 |
| `ArduinoJson` | ArduinoJson | Benoit Blanchon |

---

## ESP32 Board Package

1. Go to **File → Preferences**
2. Add to "Additional boards manager URLs":
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **esp32 by Espressif Systems**

---

## Driver Download (USB not detected)

If the board doesn't show up on a COM port, install the USB-to-UART driver:
- https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers

---

## Official Example Code Download

Seller-provided examples with board-specific driver configs:
```
http://pan.jczn1688.com/directlink/1/HMI%20display/JC3248W535EN.zip
```

Useful sketches inside the ZIP:
- `DEMO_LVGL.ino` — best starting point, LVGL UI demo
- `DEMO_PIC.ino` — image display demo
- `DEMO_MJPEG.ino` — video playback demo

---

## How to Flash

1. Connect via USB-C
2. **Hold BOOT** button on the board
3. Press and release **RESET**
4. Release **BOOT**
5. Click **Upload** in Arduino IDE
6. After upload completes, press **RESET** once

---

## Common Issues

| Problem | Fix |
|---|---|
| Port not showing up | Install CP210x/CH340 driver (link above) |
| Stuck in boot loop | PSRAM must be set to **OPI** |
| White screen after flash | Wrong display driver or LVGL config |
| Upload fails | Hold BOOT before clicking upload |
| Display freezes during WiFi/HTTP | Move HTTP calls to a FreeRTOS task on Core 0 (see CGM Dashboard code) |

---

## CGM Dashboard Project

### Features
- Nightscout glucose readings (every 60 seconds)
- Open-Meteo weather — your configured location (every 5 minutes)
- Live clock + date
- WiFi signal strength
- Color-coded glucose (green/orange/red)

### Key Config (`cgm_dashboard.ino`)
```cpp
#define WIFI_SSID        "YOUR_WIFI_SSID"
#define WIFI_PASS        "YOUR_WIFI_PASSWORD"
#define NS_URL           "https://your-nightscout.example.com"
#define NS_SECRET        "YOUR_NIGHTSCOUT_API_SECRET"
#define OWM_LAT          "40.7128"   // example only — set your own
#define OWM_LON          "-74.0060"
#define NS_UPDATE_MS     60000UL         // Nightscout every 60s
#define WEATHER_UPDATE_MS (5 * 60000UL) // Weather every 5 min
#define TZ_OFFSET        (-5 * 3600)    // EST
#define TZ_DST           3600
```

### Architecture Fix (no more freezing)
HTTP fetches run on **Core 0** via FreeRTOS task.
LVGL runs on **Core 1** (Arduino loop) uninterrupted.
A mutex protects shared glucose/weather data between cores.

```cpp
// In setup():
xTaskCreatePinnedToCore(fetchTask, "fetchTask", 8192, NULL, 1, NULL, 0);
```

---

## Glucose Color Coding

| Range | Color |
|---|---|
| Below 70 | 🔴 Red (low) |
| 70–79 | 🟠 Orange (caution) |
| 80–140 | 🟢 Green (in range) |
| 141–180 | 🟠 Orange (high) |
| Above 180 | 🔴 Red (very high) |

---

## Dexcom G7 BLE Notes (future project)

- G7 has multiple BLE connection slots (Slot 1 = Receiver, Slot 2 = Dexcom App, **Slot 3 = secondary/ESP32**)
- G7 uses encrypted BLE pairing — needs the 6-digit PIN from the sensor applicator
- xDrip+ source has the G7 BLE protocol reverse-engineered — portable to ESP32
- Use `ESP32 BLE Arduino` library (built into ESP32 board package)
