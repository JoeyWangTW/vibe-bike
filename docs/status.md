# Project Status

**Last updated:** 2026-03-01

**Current state:** Phase 3 COMPLETE + HR Zone feature. Full vibe bike dashboard with cadence, speed, distance, BLE heart rate, Claude Code stats tracking, SD logging, and touchscreen HR zone settings.

## Recently Completed

- **HR Zone feature** — touchscreen-enabled heart rate zone settings
  - XPT2046_Bitbang touch on software SPI (IO32/39/25/33, IRQ on IO36)
  - Page system: dashboard ↔ HR zone settings (tap HR block to open)
  - Settings: upper/lower limit +/- (5 bpm steps), on/off toggle, back button
  - Zone indicator: green background in-zone, flashing red out-of-zone
  - NVS persistence via Preferences library (survives reboots)
  - Flash: 1892KB (96% of min_spiffs), 74KB headroom
- **Phase 1** (VB-001 to VB-009): Pulse counting, RPM, speed, distance, TFT dashboard, session management, SD logging
- **Phase 2** (VB-010 to VB-013): BLE heart rate from Coospo H808S chest strap
- **Phase 3** (VB-014 to VB-017): WiFi + Claude Code stats tracking

## Technical Notes

- Partition scheme: `min_spiffs` (BLE+WiFi needs ~1.8MB flash vs 1.3MB default)
- BLE: Uses **built-in ESP32 BLE** (NOT NimBLE — NimBLE 2.3.7 scan returns 0 devices on this board)
- HR strap: Coospo H808S, advertises as "808S 0023713", BLE address `de:5c:38:c5:83:1d`
- Stats tracking reads `~/.claude/stats-cache.json` via local Python HTTP bridge (not Anthropic Admin API, which tracks API billing not Max subscription)
- WiFi and stats tracking are optional — leave `WIFI_SSID` and `STATS_SERVER_IP` empty to disable
- Upload baud: 460800 (921600 causes "chip stopped responding")

## In Progress

- (Nothing currently in progress)

## Up Next

- Upload and test touch calibration (first boot will prompt calibration via Serial)
- Test HR zone settings page on device
- Physical mounting of display on bike
- Test full end-to-end: pedal + HR strap + stats server + HR zones

## Blockers

- (none)
