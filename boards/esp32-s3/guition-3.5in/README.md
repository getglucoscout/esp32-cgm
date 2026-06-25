# ESP32-S3 — GUITION JC3248W535C (3.5")

Glucose dashboard for the GUITION **JC3248W535C**: an ESP32-S3 board with a 3.5"
320×480 capacitive touch display (AXS15231B controller, QSPI), 8 MB OPI PSRAM,
16 MB flash.

## Files

| File | Purpose |
|------|---------|
| `CGM_Dashboard.ino` | main sketch — Wi-Fi, Nightscout polling, UI |
| `secrets.example.h` | template — copy to `secrets.h` and fill in |
| `version.h` | firmware version string |
| `lv_conf.h` | LVGL configuration |
| `lv_port.*`, `esp_bsp.*`, `display.h` | display/board bring-up |
| `esp_lcd_axs15231b.*` | panel driver |
| `esp_lcd_touch.*` | touch driver |
| `partitions.csv` | flash partition table |

## Configure

```sh
cp secrets.example.h secrets.h     # secrets.h is git-ignored
# edit secrets.h: WIFI_SSID, WIFI_PASS, NS_URL, NS_SECRET, OTA_PASSWORD
```

## Build & flash (Arduino)

1. Install the ESP32 Arduino core and the `ArduinoJson`, `JPEGDEC`, and `lvgl`
   (8.4.x) libraries. Copy `lv_conf.h` next to the `lvgl` library folder.
2. Board: **ESP32S3 Dev Module** — PSRAM **OPI**, Flash **16 MB**.
3. Open `CGM_Dashboard.ino` and flash. After the first boot you can also adjust
   Nightscout settings from the on-device config screen.

> Not a medical device — see the [DISCLAIMER](../../../DISCLAIMER.md).
