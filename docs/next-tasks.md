# Prioritized Tasks

## Completed
- ~~**VB-001:** Probe 2-pin connector~~ — reed switch, 1 pulse/rev
- ~~**VB-002:** Test GPIO~~ — IO35 confirmed
- ~~**VB-003:** Wire bike sensor~~ — IO35 + 10K pull-up + GND

## Next Up (Phase 1 Firmware)

1. **VB-006:** Set up PlatformIO project, configure TFT_eSPI for the ILI9341 board, flash "Hello World"
2. **VB-004:** Implement interrupt-driven pulse counter (use `test_reed_switch.cpp` to verify hardware first)
3. **VB-005:** Calculate speed and distance from RPM
4. **VB-007:** Build dashboard UI v1 (RPM, speed, distance, time)
5. **VB-008:** Auto-detect session start/stop
6. **VB-009:** Log session data to SD card
