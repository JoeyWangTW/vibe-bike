# Project Status

**Last updated:** 2026-02-25

**Current state:** Phase 3 COMPLETE — All 17 stories done. Full vibe bike dashboard with cadence, speed, distance, BLE heart rate, WiFi token tracking, and SD logging.

## Recently Completed

- **Phase 1** (VB-001 to VB-009): Pulse counting, RPM, speed, distance, TFT dashboard, session management, SD logging
- **Phase 2** (VB-010 to VB-013): BLE heart rate from Coospo H808S chest strap
  - VB-010: NimBLE-Arduino 2.3.7 installed, standalone BLE HR test sketch created
  - VB-011: BLE client integrated into dashboard — scan, connect, subscribe, auto-reconnect
  - VB-012: Compact display layout — RPM (font 6), speed/dist, time/HR, tokens/cost, status bar
  - VB-013: HR in SD logging — heart_rate column in raw data, avg/max/min HR in summary
- **Phase 3** (VB-014 to VB-017): WiFi + Anthropic Usage API token tracking
  - VB-014: WiFi setup with auto-reconnect, status indicator on display
  - VB-015: Anthropic Admin API integration — polls every 60s, tracks session token delta
  - VB-016: Token display — session tokens (with K/M suffix) + estimated cost on dashboard
  - VB-017: Combined logging (tokens in SD), docs updated, CLAUDE.md updated

## Technical Notes

- Partition scheme changed to `min_spiffs` (BLE+WiFi+HTTPS needs ~1.4MB flash vs 1.3MB default)
- ArduinoJson 7.4.2 added for API response parsing
- WiFi and token tracking are optional — leave WIFI_SSID and ANTHROPIC_ADMIN_KEY empty to disable
- BLE and WiFi coexist on the ESP32 radio (NimBLE handles time-slicing)

## In Progress

- (All phases complete!)

## Up Next

- Hardware testing: upload firmware, validate BLE HR connection, test WiFi+API
- Physical mounting of display on bike

## Blockers

- (none)
