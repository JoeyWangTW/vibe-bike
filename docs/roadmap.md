# Vibe Bike Roadmap

## Vision

**"What do humans do while agents work? Joey bikes."**

Vibe Bike is a personal fitness-meets-vibe-coding dashboard. It turns a broken indoor bike into a smart training station that tracks physical activity AND coding productivity in one place. The final form is a "vibe coding bike session" — pedal, monitor heart rate, and watch your Claude Code token spend all on a single ESP32 display mounted where the old monitor used to be.

The deeper message: while AI agents handle the code, you invest in yourself. Get healthier, stay sharp, and still see exactly what your agents are burning through.

## Hardware

| Component | Details |
|-----------|---------|
| Indoor bike | Broken monitor removed; 2-pin connector exposed (likely reed switch for cadence/speed) |
| Display/MCU | 2.8" ESP32-32E (ILI9341V), 240x320px, ESP32-D0WD-V3 dual-core @ 240MHz |
| Connectivity | WiFi 802.11b/g/n, Bluetooth 4.2 + BLE |
| Heart rate | Chest strap (BLE-compatible, likely ANT+ too) |
| Free GPIOs | IO27 (if SPI peripheral unused), IO35 (input-only) — very limited |

---

## Phase 1: Bike Data on Screen (MVP)

**Goal:** Read cadence/speed from the bike's 2-pin connector and display it on the ESP32 screen.

**Timeline:** 1-2 weeks

### Milestones

- [ ] **1.1 — Hardware investigation**
  - Probe the 2-pin connector with a multimeter to determine signal type (reed switch pulses vs. analog vs. hall effect)
  - Identify a usable GPIO on the ESP32 board (IO27 or IO35 are candidates)
  - Wire the 2-pin connector to the ESP32 with appropriate pull-up/pull-down resistor

- [ ] **1.2 — Signal capture firmware**
  - Write interrupt-driven pulse counter on ESP32 (Arduino or ESP-IDF)
  - Calculate RPM from pulse timing
  - Estimate speed/distance using assumed wheel circumference

- [ ] **1.3 — Display UI v1**
  - Render a simple dashboard on the ILI9341: current RPM, speed, distance, elapsed time
  - Use TFT_eSPI or LovyanGFX library for rendering
  - Keep it clean — big numbers, easy to read while pedaling

- [ ] **1.4 — Session persistence**
  - Log session data to ESP32's onboard SD card (CSV format)
  - Start/stop session with a simple trigger (e.g., first pedal stroke starts, 30s idle stops)

### Key Risks
- **GPIO scarcity:** The ESP32 board has very few free pins. IO35 is input-only (fine for reading pulses). IO27 may conflict with SPI. Need to verify on actual hardware.
- **Signal type unknown:** The 2-pin connector might be a simple reed switch (most likely), but could also be a hall sensor with specific voltage needs.
- **No physical button:** Starting/stopping sessions may need to be auto-detected from pedal activity.

---

## Phase 2: Heart Rate Monitor

**Goal:** Connect BLE chest strap and add heart rate to the dashboard.

**Timeline:** 1-2 weeks (after Phase 1)

### Milestones

- [ ] **2.1 — BLE heart rate pairing**
  - Implement BLE client on ESP32 to scan and connect to heart rate strap
  - Use standard Heart Rate Service (UUID 0x180D) / Heart Rate Measurement Characteristic (0x2A37)
  - Handle reconnection gracefully

- [ ] **2.2 — Display UI v2**
  - Add heart rate (current BPM) to the dashboard
  - Add heart rate zone indicator (zone 1-5 based on estimated max HR)
  - Show connection status for the HR strap

- [ ] **2.3 — Combined session logging**
  - Merge cadence + HR data into session logs
  - Timestamp alignment between bike pulses and HR readings

### Key Risks
- **BLE + WiFi coexistence:** ESP32 shares the radio between WiFi and BLE. If both are active, performance may degrade. Phase 2 is BLE-only so this is fine, but matters for Phase 3.
- **HR strap compatibility:** Most chest straps support standard BLE HR profile, but some cheaper ones may not.

---

## Phase 3: Vibe Coding Dashboard

**Goal:** Track Claude Code token usage alongside bike + HR data. The ultimate "vibe coding session" view.

**Timeline:** 2-4 weeks (after Phase 2)

### Milestones

- [ ] **3.1 — Claude Code token tracking**
  - Build a lightweight local service (Python or Node) that monitors Claude Code token usage
  - Options: parse Claude Code logs, use API usage endpoints, or intercept billing data
  - Expose a simple HTTP endpoint on the local machine for the ESP32 to poll

- [ ] **3.2 — WiFi data sync**
  - Connect ESP32 to local WiFi
  - Poll the token tracking endpoint periodically (every 30s)
  - Handle WiFi + BLE coexistence (may need to time-slice or alternate)

- [ ] **3.3 — Display UI v3 — Final Form**
  - Combined dashboard: cadence, speed, distance, time, HR, HR zone, tokens used, cost
  - Session summary screen at end of ride
  - Maybe: a fun "vibe score" combining physical effort + code productivity

- [ ] **3.4 — Session history and trends**
  - Daily session logs: duration, distance, avg HR, tokens used
  - Upload session data to a simple local DB or flat files for trend viewing
  - Optional: a simple web dashboard for historical data

### Key Risks
- **Token tracking API:** Claude Code may not expose usage data in a clean, parseable way. May need to get creative with log parsing.
- **WiFi + BLE conflict:** Both use the same radio. May need to alternate connections or use BLE for HR and WiFi in bursts.
- **Scope creep:** This phase could grow endlessly. Keep the MVP tight: just show tokens used in current session.

---

## Future Ideas (Post-V1)

- **Leaderboard / streaks:** Track daily riding streaks and coding output
- **Agent health check:** Show which Claude Code tasks are running while you ride
- **Calorie-to-token ratio:** A fun metric — how many calories per 1K tokens?
- **OTA updates:** Push firmware updates over WiFi so you don't have to dismount
- **3D-printed enclosure:** Mount the display cleanly where the old monitor was

---

## Technical Stack

| Layer | Technology |
|-------|-----------|
| Firmware | Arduino framework on ESP32 (PlatformIO recommended) |
| Display | TFT_eSPI or LovyanGFX library for ILI9341 |
| BLE | ESP32 NimBLE or Arduino BLE library |
| Token tracking | Python/Node local service with HTTP API |
| Data storage | SD card (on-device), local files or SQLite (on host) |
