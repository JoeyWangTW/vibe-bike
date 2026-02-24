// bike_dashboard.ino — Vibe Bike Dashboard (VB-004 through VB-008)
// Pulse counter + speed/distance + TFT display + session auto-detect.
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

// ── Session Configuration ────────────────────────────────────
#define PAUSE_TIMEOUT_MS    30000   // 30s no pulses → paused
#define END_TIMEOUT_MS      120000  // 2 min no pulses → ended

// ── Display Configuration ────────────────────────────────────
#define TFT_BL_PIN        21
#define DISPLAY_UPDATE_MS 500   // 2Hz display refresh

// ── Colors ───────────────────────────────────────────────────
#define BG_COLOR          TFT_BLACK
#define TITLE_COLOR       0x04B3     // Dark teal
#define LABEL_COLOR       0x7BEF     // Grey
#define RPM_COLOR         TFT_CYAN
#define SPEED_COLOR       TFT_GREEN
#define DIST_COLOR        TFT_YELLOW
#define TIME_COLOR        0xFD20     // Orange
#define DIVIDER_COLOR     0x2104     // Dark grey
#define STATUS_ACTIVE     TFT_GREEN
#define STATUS_PAUSED     TFT_YELLOW
#define STATUS_ENDED      TFT_RED
#define STATUS_READY      0x7BEF     // Grey

// ── Layout (240x320 portrait) ────────────────────────────────
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
#define LEFT_COL      60
#define RIGHT_COL     180

TFT_eSPI tft = TFT_eSPI();

// ── Session States ───────────────────────────────────────────
enum SessionState {
    SESSION_READY,    // Waiting for first pulse
    SESSION_ACTIVE,   // Pedaling — timer running
    SESSION_PAUSED,   // No pulses for 30s — timer frozen
    SESSION_ENDED     // No pulses for 2 min — session over
};

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

// Session state
SessionState sessionState = SESSION_READY;
unsigned long activeStartTime = 0;     // When current active interval started
unsigned long accumulatedActiveMs = 0;  // Total active time from previous intervals
unsigned long lastPulseTimeLoop = 0;    // Copy of lastPulseTime for loop use

// Previous display values (for dirty-region updates)
int prevRpmInt = -1;
int prevSpeedTenths = -1;
int prevDistHundredths = -1;
int prevTimeSec = -1;
SessionState prevDisplayState = SESSION_READY;
unsigned long prevDisplayPulses = 0;
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

// Get total active time (accumulated + current active interval)
unsigned long getActiveTimeMs(unsigned long now) {
    if (sessionState == SESSION_ACTIVE) {
        return accumulatedActiveMs + (now - activeStartTime);
    }
    return accumulatedActiveMs;
}

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

    // Speed / Distance labels and units
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("SPEED", LEFT_COL, SPEED_LABEL_Y, 2);
    tft.drawString("DISTANCE", RIGHT_COL, DIST_LABEL_Y, 2);
    tft.drawString(speedUnit(), LEFT_COL, SPEED_UNIT_Y, 2);
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
    tft.drawString(buf, 120, RPM_VALUE_Y, 7);
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
    tft.drawString(buf, 120, TIME_VALUE_Y, 6);
}

void updateStatusBar(SessionState state, unsigned long pulses) {
    // Redraw state label on change
    if (state != prevDisplayState || firstDraw) {
        prevDisplayState = state;

        // Clear left side of status area
        tft.fillRect(0, STATUS_Y - 4, 130, 24, BG_COLOR);

        tft.setTextDatum(TL_DATUM);
        switch (state) {
            case SESSION_READY:
                tft.setTextColor(STATUS_READY, BG_COLOR);
                tft.drawString("READY", 14, STATUS_Y, 2);
                break;
            case SESSION_ACTIVE:
                tft.setTextColor(STATUS_ACTIVE, BG_COLOR);
                tft.drawString("PEDALING", 14, STATUS_Y, 2);
                break;
            case SESSION_PAUSED:
                tft.setTextColor(STATUS_PAUSED, BG_COLOR);
                tft.drawString("PAUSED", 14, STATUS_Y, 2);
                break;
            case SESSION_ENDED:
                tft.setTextColor(STATUS_ENDED, BG_COLOR);
                tft.drawString("ENDED", 14, STATUS_Y, 2);
                break;
        }
    }

    // Pulse count (right side, update on change)
    if (pulses != prevDisplayPulses || firstDraw) {
        prevDisplayPulses = pulses;

        // Clear right side
        tft.fillRect(130, STATUS_Y - 4, 110, 24, BG_COLOR);

        char buf[16];
        sprintf(buf, "%lu rev", pulses);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(LABEL_COLOR, BG_COLOR);
        tft.drawString(buf, 226, STATUS_Y, 2);
    }
}

// ── Session State Machine ────────────────────────────────────
void updateSessionState(unsigned long now, unsigned long timeSinceLastPulse, bool gotPulse) {
    switch (sessionState) {
        case SESSION_READY:
            if (gotPulse) {
                sessionState = SESSION_ACTIVE;
                activeStartTime = now;
                accumulatedActiveMs = 0;
                Serial.println("Session: READY -> ACTIVE");
            }
            break;

        case SESSION_ACTIVE:
            if (timeSinceLastPulse > PAUSE_TIMEOUT_MS) {
                // Freeze active time at the moment of last pulse
                accumulatedActiveMs += (lastPulseTimeLoop - activeStartTime);
                sessionState = SESSION_PAUSED;
                Serial.println("Session: ACTIVE -> PAUSED");
            }
            break;

        case SESSION_PAUSED:
            if (gotPulse) {
                // Resume — start a new active interval
                activeStartTime = now;
                sessionState = SESSION_ACTIVE;
                Serial.println("Session: PAUSED -> ACTIVE (resumed)");
            } else if (timeSinceLastPulse > END_TIMEOUT_MS) {
                sessionState = SESSION_ENDED;
                Serial.println("Session: PAUSED -> ENDED");
            }
            break;

        case SESSION_ENDED:
            // Terminal state — stays ended
            break;
    }
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
    tft.setRotation(0);
    drawStaticUI();

    // Sensor
    pinMode(SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), onPulse, FALLING);
    clearRpmBuffer();

    // Initial display
    updateRpmDisplay(0);
    updateSpeedDisplay(0);
    updateDistDisplay(0);
    updateTimeDisplay(0);
    updateStatusBar(SESSION_READY, 0);
    firstDraw = false;

    Serial.println("Vibe Bike Dashboard v1.1 — Ready");
}

// ── Main Loop ────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();
    bool gotPulse = false;

    // Process new pulse from ISR
    if (newPulse) {
        newPulse = false;
        gotPulse = true;

        noInterrupts();
        unsigned long interval = pulseInterval;
        unsigned long count = pulseCount;
        interrupts();

        displayPulseCount = count;

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

    // Read last pulse time for state machine
    noInterrupts();
    unsigned long timeSinceLastPulse = now - lastPulseTime;
    lastPulseTimeLoop = lastPulseTime;
    interrupts();

    // RPM timeout (separate from session — RPM zeroes faster than session pauses)
    if (lastPulseTimeLoop > 0 && timeSinceLastPulse > RPM_TIMEOUT_MS) {
        currentRpm = 0;
        clearRpmBuffer();
    }

    // Session state machine
    updateSessionState(now, timeSinceLastPulse, gotPulse);

    // Calculate values
    smoothedRpm = (currentRpm > 0) ? getSmoothedRpm() : 0;
    currentSpeed = rpmToSpeed(smoothedRpm);

    // Update display at fixed interval
    if (now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
        lastDisplayUpdate = now;

        updateRpmDisplay(smoothedRpm);
        updateSpeedDisplay(currentSpeed);
        updateDistDisplay(getDisplayDistance());
        updateTimeDisplay(getActiveTimeMs(now));
        updateStatusBar(sessionState, displayPulseCount);
    }
}
