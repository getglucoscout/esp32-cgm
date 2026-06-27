#pragma once
// Copy this file to "secrets.h" (gitignored) and fill in real values.
#define WIFI_SSID    ""            // empty -> panel boots into Wi-Fi setup (captive portal)
#define WIFI_PASS    "your-wifi-password"
#define NS_URL       "https://your-nightscout.example.com"
#define NS_SECRET    "your-nightscout-api-secret"
#define OTA_PASSWORD "your-ota-password"
// Optional Home Assistant MQTT fallback (else set it in the web config):
// #define MQTT_HOST "192.0.2.10"
// #define MQTT_USER "mqtt-user"
// #define MQTT_PASS "mqtt-pass"
