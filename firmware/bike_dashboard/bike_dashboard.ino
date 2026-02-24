// bike_dashboard.ino — VB-007: Dashboard UI v1 for Vibe Bike
// Combines pulse counter (VB-004/005) with TFT display (VB-006).
// Shows RPM, speed, distance, and elapsed time on the 2.8" ILI9341V screen.
//
// Board: ESP32-32E with ILI9341V (240x320)
// Wiring:
//   3.3V ─── 10K resistor ─── IO35 ─── bike wire 1
//   GND  ────────────────────────────── bike wire 2
//
// Build:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/bike_dashboard
//   arduino-cli upload -b esp32:esp32:esp32 -p /dev/cu.usbmodem2101 firmware/bike_dashboard
//   arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

#include <TFT_eSPI.h>

// ── Sensor Configuration ─────────────────────────────────────
#define SENSOR_PIN        35
#define DEBOUNCE_MS       50
#define RPM_TIMEOUT_MS    3000
#define MIN_RPM           10.0
#define MAX_RPM           200.0
#define SMOOTHING_SAMPLES 4

// ── Speed & Distance Configuration ───────────────────────────
#define DISTANCE_PER_REV_M  6.3
#define USE_METRIC          true
#define KM_TO_MILES         0.621371

// ── Display Configuration ────────────────────────────────────
#define TFT_BL_PIN        21
#define DISPLAY_UPDATE_MS 500   // 2Hz display refresh

// ── Colors ───────────────────────────────────────────────────
#define BG_COLOR          TFT_BLACK
#define TITLE_COLOR       0x04B3     // Dark teal
#define LABEL_COLOR       0x7BEF     // Grey
#define VALUE_COLOR       TFT_WHITE
#define RPM_COLOR         TFT_CYAN
#define SPEED_COLOR       TFT_GREEN
#define DIST_COLOR        TFT_YELLOW
#define TIME_COLOR        0xFD20     // Orange
#define DIVIDER_COLOR     0x2104     // Dark grey
#define STATUS_ACTIVE     TFT_GREEN
#define STATUS_STOPPED    0x7BEF     // Grey

// ── Layout (240x320 portrait) ────────────────────────────────
// Title bar:     y=0-24
// RPM zone:      y=25-144   (large, centered)
// Divider:       y=145
// Speed/Dist:    y=150-234  (two columns)
// Divider:       y=235
// Time:          y=240-294
// Status bar:    y=295-319

#define TITLE_Y       4
#define RPM_VALUE_Y   45
#define RPM_LABEL_Y   125
#define DIV1_Y        145
#define SPEED_LABEL_Y 152
#define SPEED_VALUE_Y 170
#define SPEED_UNIT_Y  210
#define DIST_LABEL_Y  152
#define DIST_VALUE_Y  170
#define DIST_UNIT_Y   210
#define DIV2_Y        235
#define TIME_LABEL_Y  242
#define TIME_VALUE_Y  260
#define STATUS_Y      302

// Column centers for speed/distance
#define LEFT_COL      60
#define RIGHT_COL     180

TFT_eSPI tft = TFT_eSPI();

// ── ISR Variables ────────────────────────────────────────────
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile unsigned long pulseCount = 0;
volatile bool newPulse = false;

void IRAM_ATTR onPulse() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastPulseTime;
    if (elapsed > DEBOUNCE_MS) {
        pulseInterval = elapsed;
        lastPulseTime = now;
        pulseCount++;
        newPulse = true;
    }
}

// ── RPM Smoothing ────────────────────────────────────────────
float rpmBuffer[SMOOTHING_SAMPLES];
int rpmBufferIndex = 0;
bool rpmBufferFull = false;

void addRpmSample(float rpm) {
    rpmBuffer[rpmBufferIndex] = rpm;
    rpmBufferIndex = (rpmBufferIndex + 1) % SMOOTHING_SAMPLES;
    if (rpmBufferIndex == 0) rpmBufferFull = true;
}

float getSmoothedRpm() {
    int count = rpmBufferFull ? SMOOTHING_SAMPLES : rpmBufferIndex;
    if (count == 0) return 0.0;
    float sum = 0;
    for (int i = 0; i < count; i++) sum += rpmBuffer[i];
    return sum / count;
}

void clearRpmBuffer() {
    for (int i = 0; i < SMOOTHING_SAMPLES; i++) rpmBuffer[i] = 0;
    rpmBufferIndex = 0;
    rpmBufferFull = false;
}

// ── State ────────────────────────────────────────────────────
float currentRpm = 0;
float smoothedRpm = 0;
float currentSpeed = 0;
float totalDistanceM = 0;
unsigned long lastDistancePulse = 0;
unsigned long displayPulseCount = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long sessionStartTime = 0;
unsigned long elapsedActiveMs = 0;
bool sessionActive = false;

// Previous display values (for dirty-region updates)
int prevRpmInt = -1;
int prevSpeedTenths = -1;
int prevDistHundredths = -1;
int prevTimeSec = -1;
bool prevSessionActive = false;
bool firstDraw = true;

// ── Helpers ──────────────────────────────────────────────────
float rpmToSpeed(float rpm) {
    float speedKmh = rpm * DISTANCE_PER_REV_M * 60.0 / 1000.0;
    return USE_METRIC ? speedKmh : speedKmh * KM_TO_MILES;
}

float getDisplayDistance() {
    float distKm = totalDistanceM / 1000.0;
    return USE_METRIC ? distKm : distKm * KM_TO_MILES;
}

const char* speedUnit() { return USE_METRIC ? "km/h" : "mph"; }
const char* distUnit()  { return USE_METRIC ? "km"   : "mi"; }

// ── Display Drawing ──────────────────────────────────────────

void drawStaticUI() {
    tft.fillScreen(BG_COLOR);

    // Title bar
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TITLE_COLOR, BG_COLOR);
    tft.drawString("VIBE BIKE", 120, TITLE_Y, 2);

    // RPM label
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("RPM", 120, RPM_LABEL_Y, 4);

    // Divider 1
    tft.drawFastHLine(10, DIV1_Y, 220, DIVIDER_COLOR);

    // Speed label
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("SPEED", LEFT_COL, SPEED_LABEL_Y, 2);

    // Distance label
    tft.drawString("DISTANCE", RIGHT_COL, DIST_LABEL_Y, 2);

    // Speed unit
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString(speedUnit(), LEFT_COL, SPEED_UNIT_Y, 2);

    // Distance unit
    tft.drawString(distUnit(), RIGHT_COL, DIST_UNIT_Y, 2);

    // Divider 2
    tft.drawFastHLine(10, DIV2_Y, 220, DIVIDER_COLOR);

    // Time label
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("TIME", 120, TIME_LABEL_Y, 2);

    // Vertical divider between speed and distance
    tft.drawFastVLine(120, DIV1_Y + 2, DIV2_Y - DIV1_Y - 4, DIVIDER_COLOR);
}

void updateRpmDisplay(float rpm) {
    int rpmInt = (int)(rpm + 0.5);
    if (rpmInt == prevRpmInt && !firstDraw) return;
    prevRpmInt = rpmInt;

    char buf[8];
    sprintf(buf, "%3d", rpmInt);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(RPM_COLOR, BG_COLOR);
    tft.drawString(buf, 120, RPM_VALUE_Y, 7);  // Font 7 = 7-segment, large
}

void updateSpeedDisplay(float speed) {
    int speedTenths = (int)(speed * 10 + 0.5);
    if (speedTenths == prevSpeedTenths && !firstDraw) return;
    prevSpeedTenths = speedTenths;

    char buf[8];
    sprintf(buf, "%4.1f", speed);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(SPEED_COLOR, BG_COLOR);
    tft.drawString(buf, LEFT_COL, SPEED_VALUE_Y, 4);
}

void updateDistDisplay(float dist) {
    int distHundredths = (int)(dist * 100 + 0.5);
    if (distHundredths == prevDistHundredths && !firstDraw) return;
    prevDistHundredths = distHundredths;

    char buf[10];
    if (dist < 10.0) {
        sprintf(buf, "%5.2f", dist);
    } else if (dist < 100.0) {
        sprintf(buf, "%5.1f", dist);
    } else {
        sprintf(buf, "%5.0f", dist);
    }

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(DIST_COLOR, BG_COLOR);
    tft.drawString(buf, RIGHT_COL, DIST_VALUE_Y, 4);
}

void updateTimeDisplay(unsigned long activeMs) {
    int totalSec = activeMs / 1000;
    if (totalSec == prevTimeSec && !firstDraw) return;
    prevTimeSec = totalSec;

    int mins = totalSec / 60;
    int secs = totalSec % 60;

    char buf[12];
    sprintf(buf, "%02d:%02d", mins, secs);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TIME_COLOR, BG_COLOR);
    tft.drawString(buf, 120, TIME_VALUE_Y, 6);  // Font 6 = medium digits
}

void updateStatusBar(bool active, unsigned long pulses) {
    // Only redraw if state changed
    if (active != prevSessionActive || firstDraw) {
        prevSessionActive = active;

        // Clear status area
        tft.fillRect(0, STATUS_Y - 4, 240, 24, BG_COLOR);

        tft.setTextDatum(TL_DATUM);
        if (active) {
            tft.setTextColor(STATUS_ACTIVE, BG_COLOR);
            tft.drawString("PEDALING", 14, STATUS_Y, 2);
        } else if (pulses > 0) {
            tft.setTextColor(STATUS_STOPPED, BG_COLOR);
            tft.drawString("STOPPED", 14, STATUS_Y, 2);
        } else {
            tft.setTextColor(STATUS_STOPPED, BG_COLOR);
            tft.drawString("READY", 14, STATUS_Y, 2);
        }
    }

    // Pulse count (right-aligned, always update)
    char buf[16];
    sprintf(buf, "%lu rev", pulses);
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString(buf, 226, STATUS_Y, 2);
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);

    // Backlight
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    // Display
    tft.init();
    tft.setRotation(0);  // Portrait 240x320
    drawStaticUI();

    // Sensor
    pinMode(SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), onPulse, FALLING);
    clearRpmBuffer();

    // Initial display values
    updateRpmDisplay(0);
    updateSpeedDisplay(0);
    updateDistDisplay(0);
    updateTimeDisplay(0);
    updateStatusBar(false, 0);
    firstDraw = false;

    Serial.println("Vibe Bike Dashboard v1.0 — Ready");
}

// ── Main Loop ────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // Process new pulse from ISR
    if (newPulse) {
        newPulse = false;

        noInterrupts();
        unsigned long interval = pulseInterval;
        unsigned long count = pulseCount;
        interrupts();

        displayPulseCount = count;

        // Start session timer on first pulse
        if (!sessionActive) {
            sessionActive = true;
            sessionStartTime = now;
        }

        // Calculate instantaneous RPM
        if (interval > 0) {
            currentRpm = 60000.0 / interval;
        }

        // Clamp
        if (currentRpm < MIN_RPM) {
            currentRpm = 0;
        } else if (currentRpm > MAX_RPM) {
            return;  // Bounce — ignore
        }

        // Smoothing
        if (currentRpm > 0) {
            addRpmSample(currentRpm);
        }

        // Distance accumulation
        unsigned long newPulses = count - lastDistancePulse;
        if (newPulses > 0 && currentRpm >= MIN_RPM) {
            totalDistanceM += newPulses * DISTANCE_PER_REV_M;
            lastDistancePulse = count;
        }
    }

    // Timeout detection
    noInterrupts();
    unsigned long timeSinceLastPulse = now - lastPulseTime;
    interrupts();

    if (lastPulseTime > 0 && timeSinceLastPulse > RPM_TIMEOUT_MS) {
        currentRpm = 0;
        clearRpmBuffer();
    }

    // Calculate values
    smoothedRpm = (currentRpm > 0) ? getSmoothedRpm() : 0;
    currentSpeed = rpmToSpeed(smoothedRpm);

    // Track active time (only when pedaling)
    if (sessionActive && smoothedRpm > 0) {
        elapsedActiveMs = now - sessionStartTime;
    }

    // Update display at fixed interval
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
        lastDisplayUpdate = now;

        updateRpmDisplay(smoothedRpm);
        updateSpeedDisplay(currentSpeed);
        updateDistDisplay(getDisplayDistance());
        updateTimeDisplay(elapsedActiveMs);
        updateStatusBar(smoothedRpm > 0, displayPulseCount);
    }
}
