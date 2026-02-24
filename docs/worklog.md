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
