# Inbox

Action items from co-founder discussions, standups, and meetings appear here.
Mark items as `[SEEN]` after reading them.

---

## From VP Cadence Cole Planning Session (2026-02-24)

- [x] [DONE] Probe the bike's 2-pin connector with a multimeter. Result: reed switch, 1 pulse/rev, no polarity.
- [ ] Power up the ESP32 display board and verify it works. Try a simple TFT_eSPI example sketch.
- [ ] [SEEN] Test IO35 with a simple digitalRead — confirmed plan: IO35 with 10K pull-up. Use `firmware/test/test_reed_switch.cpp` to verify.
- [ ] Measure the physical mounting: how will the display attach where the old monitor was? Any 3D printing needed or can you use zip ties / velcro for now?
- [ ] Check your chest HR strap model — confirm it supports standard BLE Heart Rate Service (UUID 0x180D). Most Garmin/Polar/Wahoo straps do.
