// config.h — Vibe Bike Dashboard Configuration
//
// Copy this file to config.h and fill in your credentials:
//   cp config.example.h config.h
//
// config.h is gitignored and will NOT be committed.

#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi Configuration ─────────────────────────────────────────
// Set your WiFi credentials, or leave empty to skip WiFi
#define WIFI_SSID         ""
#define WIFI_PASSWORD     ""

// ── Claude Code Stats Server ─────────────────────────────────
// Run: python3 scripts/stats_server.py
// It will print the IP and port to put here.
// Leave empty to skip token tracking.
#define STATS_SERVER_IP   ""
#define STATS_SERVER_PORT  8888

// ── BLE Heart Rate Monitor ─────────────────────────────────
// Set the name (or partial name) of your BLE heart rate strap.
// The scan matches if the device name contains this string.
// Leave empty to connect to any device advertising HR service (UUID 0x180D).
#define BLE_HR_DEVICE_NAME  ""

#endif
