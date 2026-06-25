# common/

Portable, board-agnostic logic shared across boards — the Nightscout client,
glucose model, trend/alarm logic, and reusable UI pieces.

Today most logic still lives inside the S3 board sketch
(`boards/esp32-s3/guition-3.5in/`). As a second board (ESP32-P4) comes online,
the shared parts move here so each board stays a thin adapter (display driver,
pinout, Wi-Fi bring-up) over a common core.
