# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0-rc3] - 2026-08-04

### Security
- **OTA transport is now certificate-pinned.** The update manifest and firmware
  download previously skipped TLS verification, so anyone in a position to
  intercept traffic could feed the panel a hostile "update" — remote code
  execution by design of what OTA is. Both fetches now validate against a small
  pinned bundle of the roots the update hosts actually chain to (Let's Encrypt,
  Sectigo/USERTrust, plus one legacy family as rotation insurance), with an SNTP
  clock sync before the first handshake — an unset clock rejects every
  certificate, and a failed sync skips the check rather than downgrading to
  unverified TLS. Glucose fetches to your own configured Nightscout/Dexcom hosts
  are unchanged.

### Changed
- The Home Assistant sensor is now labelled **GMI** — the "estimated A1C"
  wording was retired by the FDA in 2018, and the hosted portal already says
  plain GMI; the panel now agrees with it.

## [1.0.0-rc2] - 2026-07-31

### Added
- **OTA recovery ladder** — a bad over-the-air update can no longer leave a panel
  needing a USB cable. If new firmware boot-loops, the panel automatically rolls back
  to the previous firmware in its other app slot; if that isn't possible it shows a
  "Recovering" screen and auto-pulls a published fix over Wi-Fi (the local OTA push
  still works too). Each healthy boot is marked valid so a good update sticks.
  Proven on hardware before this release: a deliberately crash-looping build was
  pushed over the air and rolled back on its own, and with *both* app slots
  deliberately broken the panel recovered itself from safe mode — no USB either time.

### Changed
- Shipped binaries now bake the project's current home for updates
  (`getglucoscout/esp32-cgm`); the rc1 binaries pointed at the old repository
  location and should be reflashed once, after which OTA works normally again.

## [1.0.0-rc1] - 2026-06-27

First **release candidate**. Adds Home Assistant integration, internet
(over-the-air) updates, and buyer-friendly Wi-Fi setup.

### Added
- **Home Assistant over MQTT** with auto-discovery (glucose, trend, delta,
  estimated A1C), plus a 4th "HA control" page that publishes announce / light /
  snooze / two spare commands for your own automations to act on.
- **Internet OTA updates** — the panel checks a per-board `manifest.json` daily
  (and on demand) and updates itself from the GitHub release; no USB required.
- **Wi-Fi provisioning (captive portal)** so a panel can be set up without
  flashing: it opens a `glucoscout-XXXX` access point on first boot.
- **4-page swipe UI** — dashboard, big-glucose, device info, and HA control;
  long-press still opens Settings.
- On-screen IP address, a factory-reset button, and a `/dbg` diagnostics endpoint.
- A "Home Assistant (MQTT)" card in the web config.

### Changed
- Slimmer firmware: the SD photo-frame was removed and the config page is sent
  more efficiently (about 10% of flash, 17% of RAM).

### Fixed
- Weather sometimes showed blank — open-meteo replies with chunked
  transfer-encoding, which the streaming parser couldn't read; the body is now
  read fully before parsing.
- MQTT never connected (state -2): the broker hostname is now kept in a
  persistent buffer (the client library stored the pointer, not a copy).
- The config page could truncate when some fields were empty.

### Notes
- **Release candidate** (`1.0.0-rc1`). Not a medical device — see
  [DISCLAIMER.md](DISCLAIMER.md).

## [1.0.0-beta.2] - 2026-06-25

### Fixed
- **Long-run stability (crash after several hours).** Root cause was
  internal-heap *fragmentation*, not a leak: each poll allocated a TLS buffer +
  a `DynamicJsonDocument` + the full response `String` in the scarce internal
  heap, and over hours the largest contiguous block shrank below what the next
  allocation needed, tripping the task watchdog. Fixes:
  - JSON documents now allocate in **PSRAM** (ArduinoJson v7 custom allocator).
  - Nightscout/weather responses are **parsed directly from the TLS stream**
    (no large internal response `String`).
  - A **graceful low-memory reboot** triggers a clean ~5 s restart before any
    hard freeze, as a safety net.

## [1.0.0-beta.1] - 2026-06-25

First public **beta** release.

### Added
- ESP32-S3 glucose dashboard for the GUITION JC3248W535C 3.5" panel
  (`boards/esp32-s3/guition-3.5in/`): Nightscout polling, current value + trend
  arrow, recent history graph, and an on-device Wi-Fi/Nightscout config UI.
- Wi-Fi and Nightscout credentials isolated in a git-ignored `secrets.h`, with
  a committed `secrets.example.h` template.
- Hardware reference for the JC3248W535C (`docs/`).
- Project scaffolding: MIT license, medical disclaimer, contribution and
  security policies, issue/PR templates, and CI.

### Notes
- **Beta:** interfaces and layout may change before `1.0.0`. Not a medical
  device — see [DISCLAIMER.md](DISCLAIMER.md).
- ESP32-P4 (GUITION 10.1") support is in progress and not part of this release.

[Unreleased]: https://github.com/getglucoscout/esp32-cgm/compare/v1.0.0-rc1...HEAD
[1.0.0-rc1]: https://github.com/getglucoscout/esp32-cgm/compare/v1.0.0-beta.2...v1.0.0-rc1
[1.0.0-beta.2]: https://github.com/getglucoscout/esp32-cgm/compare/v1.0.0-beta.1...v1.0.0-beta.2
[1.0.0-beta.1]: https://github.com/getglucoscout/esp32-cgm/releases/tag/v1.0.0-beta.1
