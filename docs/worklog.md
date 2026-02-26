# Work Log

## 2026-02-24 - Project created

- Initial project setup
- Created project directory and documentation structure

## 2026-02-24 - VP Cadence Cole Planning Session

- Refined vision: "What do humans do while agents work? Joey bikes."
- Researched ESP32-32E display board specs (lcdwiki.com) — identified IO35 and IO27 as available GPIOs
- Created 3-phase roadmap: bike data (MVP) -> heart rate -> vibe coding dashboard
- Wrote 9 user stories for Phase 1 (VB-001 through VB-009)
- Updated CLAUDE.md with vision, hardware specs, pin mapping, and project conventions
- Updated HQ projects.json with vision statement
- Populated inbox with 5 hardware investigation action items for Joey

## 2026-02-24 - Hardware Investigation Complete

- VB-001: Probed 2-pin connector — confirmed reed switch, 1 pulse per revolution, no wire polarity
- VB-002: GPIO decision — IO35 (input-only) with 10K pull-up resistor to 3.3V
- VB-003: Wiring plan: reed switch wire 1 → IO35, wire 2 → GND (either wire to either pin)
- Created `firmware/test/test_reed_switch.cpp` — hardware verification script
- All hardware stories complete, firmware development unblocked

## 2026-02-24 - Switched to arduino-cli, Ralph Loop launched

- Switched build toolchain from PlatformIO to **arduino-cli** (already installed: ESP32 core 3.3.7, TFT_eSPI 2.5.43)
- Updated CLAUDE.md, prd.json (VB-006), and .claude/settings.json with arduino-cli commands
- Created Arduino sketch at `firmware/test_reed_switch/test_reed_switch.ino` — compiles successfully
- CYD detected at `/dev/cu.usbmodem2101` (may need BOOT button for upload)
- Initiated autonomous work session via `/tst:project-work`
- Stories to complete: 6 (VB-004 through VB-009)
- Starting with: VB-006 — Set up Arduino CLI project and display Hello World

## 2026-02-24 - VB-006: Display Hello World

- Configured TFT_eSPI `User_Setup.h` for ESP32-32E board (was incorrectly set for ESP32-S3)
  - Driver: ILI9341_2_DRIVER, SPI port: HSPI, pins: CS=15, DC=2, SCLK=14, MOSI=13, MISO=12, BL=21
  - SPI frequency: 55MHz, backlight HIGH = on
- Created `firmware/hello_world/hello_world.ino` — display test sketch
  - Shows "VIBE BIKE" title, "Hello World!", board info, and pin mapping
  - Turns on backlight via IO21, portrait orientation (240x320)
- Compiles cleanly: 330KB flash (25%), 22KB RAM (6%)
- Existing pulse_counter sketch still compiles with updated User_Setup.h

## 2026-02-24 - VB-007: Dashboard UI v1

- Created `firmware/bike_dashboard/bike_dashboard.ino` — combines pulse counter + TFT display
- Layout (240x320 portrait):
  - Title bar: "VIBE BIKE" in dark teal
  - RPM: huge 7-segment font, cyan, centered
  - Speed + Distance: two-column layout, green + yellow, font 4
  - Elapsed time: orange, font 6, mm:ss format
  - Status bar: PEDALING/STOPPED/READY + revolution count
- Dirty-region updates: only redraws values that changed (prev value tracking)
- 2Hz display refresh rate matching serial output
- Color scheme: dark background, high-contrast colored values per metric
- Compiles: 334KB flash (25%), 23KB RAM (7%)

## 2026-02-24 - VB-008: Auto-detect session start and stop

- Added session state machine to `firmware/bike_dashboard/bike_dashboard.ino`
  - States: READY → ACTIVE → PAUSED → ENDED
  - READY → ACTIVE: first pulse detected
  - ACTIVE → PAUSED: 30s no pulses (PAUSE_TIMEOUT_MS)
  - PAUSED → ACTIVE: pulse detected (resume)
  - PAUSED → ENDED: 2 min no pulses (END_TIMEOUT_MS)
- Active time tracking: accumulates intervals, excludes paused time
  - `accumulatedActiveMs` stores completed intervals, `activeStartTime` tracks current
- Color-coded status indicator: green PEDALING, yellow PAUSED, red ENDED, grey READY
- RPM timeout (3s) is separate from session pause (30s) — RPM zeroes faster
- Compiles: 335KB flash (25%), 23KB RAM (7%)

## 2026-02-24 - VB-009: Log session data to SD card

- Added SD card support to `firmware/bike_dashboard/bike_dashboard.ino`
  - SD card on VSPI bus (CS=IO5, MOSI=IO23, SCLK=IO18, MISO=IO19), separate from display HSPI
  - Graceful degradation: dashboard works without SD card, shows "SD" or "--" indicator
- Session files: `session_NNNN.csv` with auto-incrementing session numbers
  - Raw data: elapsed_sec, rpm, speed, distance, pulses — logged every 1s during session
  - Summary appended at end: duration, distance, avg/max RPM, avg/max speed, total revolutions
- Separate summary file: `summary_NNNN.csv` for easy parsing (field, unit, value format)
- Stats tracking: maxRpm, maxSpeed, sumRpm, rpmSampleCount for averages
- Status bar shows "SAVED" (red) when session ends and data is written
- Compiles: 381KB flash (29%), 23KB RAM (7%)
- Note: Uses sequential session numbers (not YYYY-MM-DD) — no RTC/WiFi yet

## 2026-02-24 - Phase 1 Complete!

- All 9 user stories (VB-001 through VB-009) are now passing
- Main production sketch: `firmware/bike_dashboard/bike_dashboard.ino`
- Capabilities: pulse counting, RPM smoothing, speed/distance, TFT dashboard, session management, SD logging

## 2026-02-25 - Phase 2 & 3: BLE Heart Rate + Token Tracking

- **VB-010:** Installed NimBLE-Arduino 2.3.7, created `firmware/test_ble_hr/test_ble_hr.ino`
  - Standalone BLE HR test: scans for H808S/CooSpo or HR Service UUID 0x180D
  - Connects, subscribes to HR Measurement (0x2A37), prints BPM to serial
  - Auto-reconnect on disconnect (restart scan)
  - Compiles: 602KB flash (45%), 36KB RAM (10%)
- **VB-011:** Integrated BLE HR into `bike_dashboard.ino`
  - NimBLE scan/connect runs in background (FreeRTOS task thread)
  - Notification callback updates `volatile currentHR` — same pattern as pulse ISR
  - `hrConnected` state flag, auto-reconnect via scan restart on disconnect
  - Parses HR measurement flags (8-bit vs 16-bit format)
- **VB-012:** Redesigned display — compact all-in-one layout
  - RPM: shrunk from font 7 to font 6 (saves ~50px vertical)
  - Speed/Distance row: labels + values + units (font 4)
  - Time/HR row: elapsed time + heart rate with bpm unit
  - Tokens/Cost row: session token delta + estimated cost (Phase 3)
  - Status bar: session state + revolution count
  - Status indicators: SD, WiFi (W), BLE (B) in title bar
- **VB-013:** Added HR to SD card logging
  - Raw data: `elapsed_sec,rpm,speed,distance,heart_rate,pulses`
  - Session summary: avg_hr, max_hr, min_hr fields added
  - HR=0 when strap not connected (doesn't skip logging)
- **VB-014:** WiFi setup on ESP32
  - Credentials via `WIFI_SSID`/`WIFI_PASSWORD` defines (leave empty to skip)
  - Connects at boot with 10s timeout, auto-reconnect every 30s
  - WiFi status indicator (W) in title bar — green/red
  - WiFi + BLE coexist via NimBLE radio time-slicing
- **VB-015:** Anthropic Usage API integration
  - Calls `GET /v1/organizations/usage` with Admin API key
  - WiFiClientSecure with HTTPS (insecure cert for personal project)
  - ArduinoJson 7.4.2 for response parsing
  - Tracks session token delta: baseline on first pedal, poll every 60s
  - Calculates estimated cost from input/output token pricing
- **VB-016:** Token display on dashboard
  - Session tokens with K/M suffix (e.g., "45.2K", "1.3M")
  - Estimated cost (e.g., "$1.87")
  - Grayed out when WiFi disconnected, "..." while loading
- **VB-017:** Combined session logging + docs update
  - tokens_used and estimated_cost added to session summary CSV
  - Updated CLAUDE.md: partition scheme, WiFi config, phase status
  - Updated docs/status.md, docs/worklog.md
- **Build notes:**
  - Partition scheme changed from default to `min_spiffs` (1.4MB / 1.9MB = 70%)
  - FQBN: `esp32:esp32:esp32:PartitionScheme=min_spiffs`
  - Total flash: 1390KB (70%), RAM: 60KB (18%)
