# Changelog

All notable changes to this project are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

[Unreleased]: https://github.com/glucoscout/esp32-cgm/compare/v1.0.0-beta.1...HEAD
[1.0.0-beta.1]: https://github.com/glucoscout/esp32-cgm/releases/tag/v1.0.0-beta.1
