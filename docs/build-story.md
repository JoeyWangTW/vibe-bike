# How We Built the Vibe Bike: A Human + Claude Code Story

**What do humans do while agents work? Joey bikes.**

This is the story of how a broken indoor bike became a smart training dashboard — built over five late nights by one person and an AI coding partner. It's a story about vibe coding at its most literal: building a thing that tracks your biking *and* your AI's coding, on a single screen, with Claude Code doing most of the typing.

---

## The Starting Point: A Broken Bike and a Spare Screen

The project began with two things sitting around unused:

1. An indoor exercise bike with a dead monitor. The old display was gone, but there was a 2-pin connector dangling from the frame — a mystery signal from whatever sensor the original monitor used.
2. A "Cheap Yellow Display" (CYD) — a $12 ESP32 dev board with a 2.8" touchscreen, WiFi, Bluetooth, and an SD card slot. The kind of board that sits in a drawer until you find the right project for it.

The idea was simple: mount the screen where the old monitor was, read the bike sensor, and build a dashboard. But the ambition grew. What if it also showed your heart rate? What if it showed how many tokens Claude Code was burning while you pedaled?

The deeper message: while AI agents handle the code, you invest in yourself. A fitness dashboard that also monitors your AI's productivity.

---

## Night 1: The Sprint (Feb 24, 2am)

### Planning with "VP Cadence Cole"

The first session started with planning. Joey set up the project structure — a PRD with user stories, a roadmap, work docs — and framed the planning as a session with the fictional "VP Cadence Cole." (The VP of Cadence. For a bike project. It's a vibe.)

Five action items went into the inbox, the kind of things Claude Code can't do: probe the 2-pin connector with a multimeter, figure out the GPIO situation on the board, check the heart rate strap model. Hardware investigation, done by hand.

The results: the mystery connector was a reed switch (a magnet on the pedal passes by a sensor once per revolution — the simplest possible signal). IO35 on the ESP32 was free and input-capable. A 10K pull-up resistor and two wires later, the bike was connected.

### Unleashing Ralph

With the hardware wired up, Joey launched "Ralph" — an autonomous agent loop. Ralph is a bash script that feeds a prompt to Claude Code (or Amp), telling it to: read the PRD, find the highest-priority incomplete story, implement it, commit, repeat. One story per iteration, one commit per story.

What happened next was fast. Seven commits in 21 minutes:

| Time | Story | What Happened |
|------|-------|---------------|
| 2:06 | Setup | Project scaffolding, docs, test sketches |
| 2:08 | VB-004 | Interrupt-driven pulse counter with debounce and RPM smoothing |
| 2:12 | VB-005 | Speed and distance calculation from RPM |
| 2:18 | VB-006 | "Hello World" on the display — first pixels lit up |
| 2:21 | VB-007 | Full dashboard UI: big RPM numbers, color-coded metrics |
| 2:23 | VB-008 | Session state machine: auto-detect start/pause/stop |
| 2:27 | VB-009 | SD card logging with CSV session files |

Phase 1 complete. In 21 minutes, Ralph went from "we have a reed switch" to a working bike computer with a color dashboard and data logging. The firmware grew from nothing to 611 lines across that sprint.

### What worked

The Ralph loop was a perfect fit for this kind of work — well-scoped user stories, a clear build/verify cycle (`arduino-cli compile`), and no external dependencies to break. Each story built cleanly on the last. Claude Code could read the previous code, understand the patterns, and extend them.

The key insight: the human did the *hardware* work (probing, wiring, testing signals), then handed off the *firmware* work to the agent. Each doing what they're good at.

---

## Night 2: The Ambitious Commit (Feb 25, 11pm)

### Two Phases in One Shot

The next session went big. Instead of one story at a time, the commit log shows **eight user stories landed in a single commit** — all of Phase 2 (BLE heart rate) and Phase 3 (WiFi + token tracking) together.

This was Claude Code writing nearly 600 new lines: NimBLE for Bluetooth heart rate, WiFiClientSecure for HTTPS to the Anthropic Admin API, ArduinoJson for parsing responses, a complete display redesign to fit all the new metrics, and combined session logging. The firmware nearly doubled from 611 to 1,191 lines.

It compiled. It got committed. On paper, the project was done.

### The choices made

Two key technical decisions were baked into this commit:

1. **NimBLE** for BLE — chosen because it's lighter than the built-in ESP32 BLE library
2. **Anthropic Admin API** for token tracking — call the billing API to see how many tokens Claude Code is using

Both seemed reasonable. Both were wrong.

---

## Night 3: The Reckoning (Feb 25-27, hardware time)

### "NimBLE doesn't work on this board"

When the firmware was actually uploaded to the physical board (460800 baud — the default 921600 crashed the chip, first lesson), the BLE heart rate feature was dead. NimBLE's scan returned zero devices. Direct connect crashed.

Joey switched to the built-in ESP32 BLE library and ran a scan: **42 devices found**, including the heart rate strap, immediately. The Coospo H808S showed up as "808S 0023713" and started sending heart rate data within minutes.

The fix was a full rewrite of the BLE code — ripping out NimBLE and replacing it with the heavier but actually-functional built-in library. This is the kind of problem that only surfaces on real hardware. Claude Code had written perfectly correct NimBLE code; the library just didn't work on this specific ESP32 variant.

**Lesson:** Code that compiles isn't code that works. The gap between "it compiles" and "it runs on the hardware" is where the real debugging lives. An AI can write firmware all day, but someone has to plug it in.

### "The Admin API tracks the wrong thing"

The second surprise was more subtle. The Anthropic Admin API was integrated, HTTPS was working, tokens were being fetched... but the numbers didn't match what Joey was seeing in Claude Code.

It turned out the Admin API tracks **API billing** — tokens consumed by applications calling the API. Joey was on Claude Max (a subscription), where there's no per-token billing. The usage data he wanted — tokens and messages from Claude Code sessions — lives in a completely different place: a local file at `~/.claude/stats-cache.json`.

The pivot was clean:
- Ripped out `WiFiClientSecure` (HTTPS) entirely
- Wrote a 50-line Python script (`stats_server.py`) that reads the local stats file and serves it over HTTP
- Replaced the firmware's HTTPS client with a simple HTTP GET to `http://<mac-ip>:8888/stats`
- Changed the display from TOKENS + COST to TOKENS + MSGS (no per-token cost on Max)

This actually *saved* flash space — HTTPS is expensive on an ESP32. The firmware went from fighting flash limits to having a bit more breathing room (briefly).

### What this phase taught us

These two debugging sessions — BLE library swap and API pivot — were the most *human* parts of the entire project. Claude Code wasn't in the loop for the discovery. Joey was the one staring at a serial monitor showing "0 devices found," the one realizing the API numbers didn't add up. The fixes were collaborative — Joey identified the problems, Claude Code wrote the new implementations — but the diagnosis was all human.

This is also the phase where the CLAUDE.md file became truly valuable. After each discovery, the instructions were updated:

> BLE library: **Built-in ESP32 BLE** (NOT NimBLE — NimBLE 2.3.7 doesn't work on this board, scan returns 0 devices)

> Upload speed: 460800 baud (default 921600 fails on this board)

These "NOT X" annotations are scars. They exist because someone tried X first, and they're written so that the next Claude Code session (or the next developer) doesn't repeat the mistake.

---

## Night 4: Going Physical (Mar 1, 1am)

### 3D-Printed Mount

With the firmware stable, the project shifted to the physical world. Claude Code designed a 3D-printable mount in OpenSCAD — a tray-style case where the board slides in from the front, with L-bracket corner spacers and a slot that friction-fits onto the bike's metal plate bracket. A snap-on front cap covers the PCB edges and shows only the screen.

This was an interesting moment: an AI designing a physical object for a specific bike, based on measurements Joey provided. The mount needed to account for the board dimensions (50x86mm), leave space behind for a battery, keep the USB-C port accessible from the bottom, and attach to the bike's existing bracket.

### Touchscreen HR Zones

The final major feature was touchscreen support. The CYD has a resistive touchscreen (XPT2046), but all three SPI buses were already spoken for (display on HSPI, SD card on VSPI). Claude Code's solution: bit-bang a software SPI interface on the remaining GPIOs.

This unlocked a settings page — tap the heart rate area on the dashboard to open HR zone settings, with +/- buttons for upper and lower limits. The zone indicator flashes the screen border red when you're outside your target heart rate, green when you're in it. Settings persist across reboots via the ESP32's NVS (non-volatile storage).

The firmware hit 1,549 lines and 96% of available flash. Only 74KB of headroom remaining on the ESP32's min_spiffs partition.

---

## The Final Product

What started as a broken bike and a spare screen became:

- **Cadence and speed** from the original reed switch sensor
- **Distance tracking** with automatic session detection
- **BLE heart rate** from a Bluetooth chest strap
- **Claude Code stats** — live tokens and messages from your coding sessions
- **SD card logging** — CSV files with full ride + coding data
- **Touchscreen HR zones** — set targets, get visual feedback
- **3D-printed mount** — designed to slot onto the bike

All running on a $12 ESP32 board with a 2.8" screen, at 96% flash capacity.

---

## What We Learned About Building Together

### The Human-AI Division of Labor

The pattern that emerged wasn't "AI writes all the code" — it was more nuanced:

| Human | AI |
|-------|-----|
| Probed the reed switch with a multimeter | Wrote the interrupt-driven pulse counter |
| Wired GPIO35 with a pull-up resistor | Designed the display layout |
| Discovered NimBLE doesn't work on this board | Rewrote BLE code using the working library |
| Realized the Admin API tracks the wrong data | Built the local stats server + firmware client |
| Gave feedback on RPM label placement | Moved the label |
| Provided bike bracket measurements | Designed the 3D mount in OpenSCAD |
| Tested on hardware, reported bugs | Fixed bugs, iterated on code |

Joey handled everything that required physical access, domain knowledge, or judgment calls. Claude Code handled everything that required writing, refactoring, or maintaining large amounts of code. The handoff points were clean because the project documentation (CLAUDE.md, worklog, PRD) served as a shared memory between sessions.

### Ralph: Autonomous Agent Loops Work for Well-Scoped Tasks

The Ralph loop — an autonomous agent churning through user stories — was remarkably effective for Phase 1. Seven stories in 21 minutes, each building on the last. The key ingredients:

1. **Clear acceptance criteria** in the PRD for each story
2. **A verifiable build step** (`arduino-cli compile`)
3. **Incremental complexity** — each story extended the previous one
4. **No external dependencies** to break between iterations

Where it broke down: Phase 2 and 3 were attempted as one big batch, and the code that came out compiled but didn't run. The agent couldn't test on real hardware, and two of its technology choices (NimBLE, Admin API) were wrong for this specific context.

### CLAUDE.md as Living Documentation

The most valuable artifact might be the CLAUDE.md file itself. It started as an optimistic plan and evolved into hard-won truth:

- **Version 1** (day 1): "NimBLE — lighter than Arduino BLE"
- **Version 2** (after Phase 2+3): "NimBLE-Arduino 2.3.7"
- **Version 3** (after debugging): "Built-in ESP32 BLE (NOT NimBLE — NimBLE 2.3.7 doesn't work on this board, scan returns 0 devices)"

Each version reflects increasing contact with reality. The "NOT X" pattern — explicitly documenting what doesn't work — turned out to be one of the most useful things in the file. It's a form of institutional memory: not just "here's how to do it" but "here's what we already tried that failed."

### The 2am Pattern

Every commit was made between 1am and 3am. This was a night-owl project, built in the quiet hours when the house is asleep and there's nothing to do but ride a bike and watch Claude Code work. There's something fitting about that — the whole premise is "what do you do while agents work?" and the answer, apparently, is "stay up way too late tinkering."

---

## By the Numbers

| Metric | Value |
|--------|-------|
| Total time span | 5 days (3 coding sessions) |
| Commits | 11 |
| Lines of firmware | 1,549 |
| User stories completed | 18 |
| Flash usage | 96% (1,892 KB of 1,966 KB) |
| BLE libraries tried | 2 (NimBLE failed, built-in worked) |
| API approaches tried | 2 (Admin API wrong target, local stats server worked) |
| Major pivots | 2 |
| 3D models designed | 2 (mount + front cap) |
| Test sketches written | 4 (reed switch, BLE HR, BLE scan, touch) |
| Hours of the day commits were made | 1am - 3am, exclusively |

---

*Built by Joey Wang and Claude Code, February-March 2026.*
