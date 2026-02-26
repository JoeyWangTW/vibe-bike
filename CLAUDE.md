# Vibe Bike

## Vision

**"What do humans do while agents work? Joey bikes."**

A personal fitness-meets-vibe-coding dashboard. Turns a broken indoor bike into a smart training station that tracks physical activity AND coding productivity on a single ESP32 display. The final form: pedal, monitor heart rate, and watch Claude Code token spend — all on one screen.

## Hardware

- **Indoor bike** — broken monitor removed, 2-pin connector is a **reed switch**, 1 pulse per revolution, no polarity
- **Bike sensor wiring:** Reed switch → IO35 (with 10K pull-up to 3.3V) + GND. Signal: HIGH at rest, LOW pulse on magnet pass.
- **Display/MCU** — 2.8" ESP32-32E with ILI9341V (240x320px), ESP32-D0WD-V3 dual-core @ 240MHz
  - WiFi 802.11b/g/n + Bluetooth 4.2 + BLE
  - Onboard: LCD (SPI), resistive touch, SD card slot, RGB LEDs, audio amp
  - **Free GPIOs:** IO35 (input-only, best for sensor), IO27 (may conflict with SPI)
  - Display docs: https://www.lcdwiki.com/2.8inch_ESP32-32E_Display
- **Heart rate strap** — Coospo H808S BLE chest strap (HR Service UUID 0x180D)

## Project Conventions

### Firmware
- Use **arduino-cli** for builds (NOT PlatformIO)
- FQBN: `esp32:esp32:esp32:PartitionScheme=min_spiffs` (ESP32 core 3.3.7, min_spiffs needed for BLE+WiFi+HTTPS)
- USB port: `/dev/cu.usbserial-1120` (CH340, for upload) — `/dev/cu.usbmodem2101` is native USB (serial monitor only)
- Display library: **TFT_eSPI** 2.5.43 (installed, needs User_Setup.h configured for ILI9341 SPI pins)
- BLE library: **NimBLE-Arduino** 2.3.7 (lighter than Arduino BLE)
- JSON library: **ArduinoJson** 7.4.2 (for API response parsing)
- Sketches go in `firmware/<sketch-name>/<sketch-name>.ino` (Arduino sketch format)
- Test sketches in `firmware/test_<name>/test_<name>.ino`

### Build Commands
```bash
# Compile (main dashboard — needs min_spiffs for BLE+WiFi+HTTPS)
arduino-cli compile -b esp32:esp32:esp32:PartitionScheme=min_spiffs firmware/bike_dashboard

# Compile (test sketches — default partition is fine)
arduino-cli compile -b esp32:esp32:esp32 firmware/test_<name>

# Upload
arduino-cli upload -b esp32:esp32:esp32:PartitionScheme=min_spiffs -p /dev/cu.usbmodem2101 firmware/bike_dashboard

# Serial monitor
arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

# Install a library
arduino-cli lib install "<library-name>"
```

### Important: Upload may require BOOT button
If upload fails with "Failed to connect to ESP32", hold the BOOT button on the board during upload. The CYD shows up as `/dev/cu.usbmodem2101` (native USB).

### Pin Mapping (ESP32-32E Board)
| Function | Pins |
|----------|------|
| LCD (SPI) | IO15, IO2, IO14, IO13, IO12, IO21 |
| Touch | IO25, IO32, IO39, IO33, IO36 |
| RGB LEDs | IO22, IO16, IO17 |
| SD Card | IO5, IO23, IO18, IO19 |
| Audio | IO4, IO26 |
| Serial | IO3, IO1 |
| **Bike sensor** | **IO35 (input-only, 10K pull-up to 3.3V)** |
| Available | IO27 |

### WiFi & API Configuration
- WiFi credentials: set `WIFI_SSID` and `WIFI_PASSWORD` in `bike_dashboard.ino` (or leave empty to skip WiFi)
- Anthropic Admin API key: set `ANTHROPIC_ADMIN_KEY` in `bike_dashboard.ino` (get from console.anthropic.com > Settings > Admin API Keys)
- Token tracking is optional — dashboard works without WiFi/API key

### Data Format
- Session logs: CSV on SD card, named `session_NNNN.csv` (auto-incrementing)
- Raw data fields: `elapsed_sec,rpm,speed,distance,heart_rate,pulses`
- Session summary includes: duration, distance, avg/max RPM, avg/max speed, avg/max/min HR, tokens_used, estimated_cost

### Phases
- **Phase 1 (complete):** Bike cadence/speed data on screen + SD logging
- **Phase 2 (complete):** BLE heart rate from Coospo H808S chest strap
- **Phase 3 (complete):** WiFi + Anthropic Usage API token tracking on display

## Work Documentation

### Status Updates
- Read `docs/status.md` at the start of every session to understand current project state
- Update `docs/status.md` after completing work with what was done and what's next

### Work Logging
- Append entries to `docs/worklog.md` for every work session
- Format: `## YYYY-MM-DD - Brief description` followed by bullet points of changes

### Inbox
- Check `docs/inbox.md` at the start of every session for action items from co-founder discussions or standups
- Mark items as `[SEEN]` after reading them
- Address action items in your current work session

## Allowed Commands

- `arduino-cli compile` — compile firmware
- `arduino-cli upload` — flash to ESP32
- `arduino-cli monitor` — serial monitor
- `arduino-cli lib install` — install libraries
- `arduino-cli lib list` — list installed libraries
- `arduino-cli board list` — list connected boards
