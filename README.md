# Vibe Bike

**What do humans do while agents work? They bike.**

Turn a broken indoor bike into a smart training station. Track cadence, speed, heart rate, and Claude Code token spend — all on one ESP32 display.

Built as a vibe coding project with Claude Code.

## Features

- **Cadence & speed** from the bike's built-in reed switch sensor
- **Distance tracking** with session auto-detect (start/stop)
- **BLE heart rate** from any Bluetooth heart rate strap
- **Claude Code stats** — live output tokens and message count from your coding sessions
- **SD card logging** — CSV session logs with full ride data
- **3D-printable mount** to attach the display to the bike

## Hardware

| Part | Notes |
|------|-------|
| ESP32 CYD (Cheap Yellow Display) | 2.8" ESP32-32E with ILI9341, 240x320px. [Amazon](https://www.amazon.com/dp/B0G1M5XHDL) (or cheaper on AliExpress, search "ESP32-2432S028") |
| Indoor bike with reed switch | Most indoor bikes have a 2-pin magnetic reed switch for cadence. If yours doesn't, any hall effect sensor works |
| BLE heart rate strap | Any strap that advertises BLE Heart Rate Service (UUID 0x180D). Tested with Coospo H808S |
| 10K resistor | Pull-up for the reed switch signal line |
| Micro SD card | For session logging (optional) |
| 3D-printed mount | See `3d-model/` — slots onto a metal plate bracket on the bike |

### Wiring

The reed switch connects to GPIO 35 with a pull-up resistor:

```
3.3V ─── 10K resistor ─── IO35 ─── bike wire 1
GND  ────────────────────────────── bike wire 2
```

No polarity — the reed switch is just an open/close contact.

## Setup

### 1. Install arduino-cli

```bash
# macOS
brew install arduino-cli

# Or see https://arduino.github.io/arduino-cli/installation/
```

### 2. Install ESP32 board support

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.7
```

### 3. Install libraries

```bash
arduino-cli lib install "TFT_eSPI@2.5.43"
arduino-cli lib install "ArduinoJson@7.4.2"
```

The TFT_eSPI library needs its `User_Setup.h` configured for the CYD's ILI9341 SPI pins. See the [display docs](https://www.lcdwiki.com/2.8inch_ESP32-32E_Display) for pin mapping.

### 4. Configure

```bash
cd firmware/bike_dashboard
cp config.example.h config.h
```

Edit `config.h`:

```c
// WiFi (optional — leave empty to skip)
#define WIFI_SSID         "your-wifi"
#define WIFI_PASSWORD     "your-password"

// Claude Code stats server (optional — leave empty to skip)
#define STATS_SERVER_IP   "192.168.1.100"
#define STATS_SERVER_PORT  8888

// BLE heart rate strap name (optional — leave empty to match any HR device)
#define BLE_HR_DEVICE_NAME  "808"
```

All three sections are optional. The dashboard works standalone with just the bike sensor.

### 5. Compile and upload

```bash
# Compile (min_spiffs partition needed for BLE + WiFi)
arduino-cli compile -b esp32:esp32:esp32:PartitionScheme=min_spiffs firmware/bike_dashboard

# Upload (460800 baud — default 921600 fails on CYD boards)
arduino-cli upload -b esp32:esp32:esp32:PartitionScheme=min_spiffs,UploadSpeed=460800 -p /dev/cu.usbserial-1120 firmware/bike_dashboard
```

Your serial port will differ — run `arduino-cli board list` to find it. The CYD uses a CH340 USB-serial chip.

If upload fails with "Failed to connect to ESP32", hold the **BOOT** button on the board while uploading.

### 6. Claude Code stats server (optional)

To show live Claude Code token usage on the display:

```bash
python3 scripts/stats_server.py
```

This reads your local Claude Code session data and serves it over HTTP. The ESP32 polls it every 30 seconds. The server prints the IP and port to put in your `config.h`.

## 3D-Printable Mount

The `3d-model/` directory has OpenSCAD source and ready-to-print STL files:

- **`bike_mount.scad`** — Tray-style case. The board slides in from the front, 4 L-bracket corner spacers hold it at the right depth with space behind for a battery. A slot in the back wall friction-fits onto a metal plate bracket on the bike. USB-C accessible from the bottom.
- **`front_cap.scad`** — Snap-on faceplate with a screen cutout. Covers the PCB edges, shows only the display.

Print with the open face pointing up. No supports needed.

## Project Structure

```
firmware/
  bike_dashboard/         # Main dashboard firmware
    bike_dashboard.ino
    config.example.h      # Template — copy to config.h
  test_ble_hr/            # BLE heart rate test sketch
  test_ble_scan/          # BLE scan test sketch
  test_reed_switch/       # Reed switch test sketch
  hello_world/            # Display test sketch

scripts/
  stats_server.py         # Claude Code stats HTTP bridge

3d-model/
  bike_mount.scad         # Display mount (OpenSCAD source)
  bike_mount.stl          # Ready to print
  front_cap.scad          # Front cap (OpenSCAD source)
  front_cap.stl           # Ready to print
```

## License

MIT
