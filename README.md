# esp32-cgm

[![Release](https://img.shields.io/github/v/release/glucoscout/esp32-cgm?include_prereleases&sort=semver)](https://github.com/glucoscout/esp32-cgm/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![CI](https://github.com/glucoscout/esp32-cgm/actions/workflows/ci.yml/badge.svg)](https://github.com/glucoscout/esp32-cgm/actions/workflows/ci.yml)
[![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-blue.svg)](#supported-hardware)

Open-source firmware that turns inexpensive ESP32 touch-display boards into a
live glucose monitor. It reads from a [Nightscout](https://nightscout.github.io/)
site and shows current value, trend, and recent history on the screen.

> ⚠️ **Not a medical device.** This is a hobbyist/informational display. It is
> **not** for treatment or dosing decisions. No warranty — use at your own risk.
> See [DISCLAIMER.md](DISCLAIMER.md).

## Supported hardware

| Board | Chip | Display | Status |
|-------|------|---------|--------|
| GUITION JC3248W535C | ESP32-S3 | 3.5" 320×480 capacitive | ✅ supported |
| GUITION 10.1" panel | ESP32-P4 | 10.1" + C6 Wi-Fi | 🚧 in progress |

New boards are added under `boards/<chip>/<vendor-board>/`; the portable logic
lives in `common/`, so a new board is an adapter, not a fork.

## Repository layout

```
common/                          shared, board-agnostic logic (planned)
boards/
  esp32-s3/guition-3.5in/        the 3.5" S3 dashboard (this release)
docs/                            hardware references
```

## Quick start

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) with the
   [ESP32 board package](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html),
   plus the `ArduinoJson`, `JPEGDEC`, and `lvgl` (8.4.x) libraries. Copy this
   repo's `lv_conf.h` next to your `lvgl` library folder.
2. In `boards/esp32-s3/guition-3.5in/`, copy `secrets.example.h` to `secrets.h`
   and fill in your Wi-Fi and Nightscout details. `secrets.h` is git-ignored.
3. Open `CGM_Dashboard.ino`, select the ESP32-S3 board, and flash.

## Build from source

See the per-board README in `boards/esp32-s3/guition-3.5in/`. CI builds each
supported board on every change.

## Versioning & releases

This project follows [Semantic Versioning](https://semver.org/). Released
builds are tagged and published on the
[Releases page](https://github.com/glucoscout/esp32-cgm/releases); see
[CHANGELOG.md](CHANGELOG.md) for what changed in each version.

## Contributing

Issues and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).
To report a security or credential issue, see [SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE) © glucoscout. A paid, managed hosting service is available
separately; this firmware is and stays free and open source.
