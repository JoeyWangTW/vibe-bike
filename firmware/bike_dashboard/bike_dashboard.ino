// bike_dashboard.ino — Vibe Bike Dashboard (VB-004 through VB-009)
// Pulse counter + speed/distance + TFT display + session auto-detect + SD logging.
//
// Board: ESP32-32E with ILI9341V (240x320)
// Wiring:
//   3.3V ─── 10K resistor ─── IO35 ─── bike wire 1
//   GND  ────────────────────────────── bike wire 2
//
// SD Card: VSPI bus (IO5=CS, IO23=MOSI, IO18=SCLK, IO19=MISO)
// Display: HSPI bus (IO15=CS, IO13=MOSI, IO14=SCLK, IO12=MISO)
//
// Build:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/bike_dashboard
//   arduino-cli upload -b esp32:esp32:esp32 -p /dev/cu.usbmodem2101 firmware/bike_dashboard
//   arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

#include <TFT_eSPI.h>
#include <SD.h>
#include <SPI.h>

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
#define PAUSE_TIMEOUT_MS    30000
#define END_TIMEOUT_MS      120000

// ── Display Configuration ────────────────────────────────────
#define TFT_BL_PIN        21
#define DISPLAY_UPDATE_MS 500

// ── SD Card Configuration ────────────────────────────────────
#define SD_CS_PIN         5
#define RAW_LOG_INTERVAL_MS  1000  // Log raw data every 1s during session

// ── Colors ───────────────────────────────────────────────────
#define BG_COLOR          TFT_BLACK
#define TITLE_COLOR       0x04B3
#define LABEL_COLOR       0x7BEF
#define RPM_COLOR         TFT_CYAN
#define SPEED_COLOR       TFT_GREEN
#define DIST_COLOR        TFT_YELLOW
#define TIME_COLOR        0xFD20
#define DIVIDER_COLOR     0x2104
#define STATUS_ACTIVE     TFT_GREEN
#define STATUS_PAUSED     TFT_YELLOW
#define STATUS_ENDED      TFT_RED
#define STATUS_READY      0x7BEF

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
    SESSION_READY,
    SESSION_ACTIVE,
    SESSION_PAUSED,
    SESSION_ENDED
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
unsigned long activeStartTime = 0;
unsigned long accumulatedActiveMs = 0;
unsigned long lastPulseTimeLoop = 0;

// Session stats (for summary)
float maxRpm = 0;
float maxSpeed = 0;
double sumRpm = 0;        // Sum of all RPM samples (for average)
unsigned long rpmSampleCount = 0;  // Number of RPM samples taken

// SD card state
bool sdReady = false;
int sessionNumber = 0;
char sessionFilename[24];  // "/session_NNNN.csv"
unsigned long lastRawLogTime = 0;
bool sessionLogged = false;  // Prevent double-logging

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

unsigned long getActiveTimeMs(unsigned long now) {
    if (sessionState == SESSION_ACTIVE) {
        return accumulatedActiveMs + (now - activeStartTime);
    }
    return accumulatedActiveMs;
}

// ── SD Card Functions ────────────────────────────────────────

// Find next available session number by checking existing files
int findNextSessionNumber() {
    int maxNum = 0;
    File root = SD.open("/");
    if (!root) return 1;

    File entry;
    while ((entry = root.openNextFile())) {
        const char* name = entry.name();
        // Match "session_NNNN.csv" pattern
        if (strncmp(name, "session_", 8) == 0) {
            int num = atoi(name + 8);
            if (num > maxNum) maxNum = num;
        }
        entry.close();
    }
    root.close();
    return maxNum + 1;
}

bool initSD() {
    if (!SD.begin(SD_CS_PIN)) {
        Serial.println("SD: Card not found or failed to mount");
        return false;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD: Card mounted, %llu MB\n", cardSize);

    sessionNumber = findNextSessionNumber();
    Serial.printf("SD: Next session number: %d\n", sessionNumber);
    return true;
}

void startSessionLog() {
    if (!sdReady) return;

    sprintf(sessionFilename, "/session_%04d.csv", sessionNumber);

    File f = SD.open(sessionFilename, FILE_WRITE);
    if (!f) {
        Serial.printf("SD: Failed to create %s\n", sessionFilename);
        sdReady = false;
        return;
    }

    // Raw data header
    f.println("elapsed_sec,rpm,speed,distance,pulses");
    f.close();

    Serial.printf("SD: Logging to %s\n", sessionFilename);
}

void logRawData(unsigned long activeMs) {
    if (!sdReady) return;

    File f = SD.open(sessionFilename, FILE_APPEND);
    if (!f) {
        Serial.println("SD: Failed to append raw data");
        return;
    }

    float dist = getDisplayDistance();
    f.printf("%.1f,%.1f,%.1f,%.2f,%lu\n",
             activeMs / 1000.0, smoothedRpm, currentSpeed, dist, displayPulseCount);
    f.close();
}

void writeSessionSummary() {
    if (!sdReady || sessionLogged) return;
    sessionLogged = true;

    // Calculate averages
    float avgRpm = (rpmSampleCount > 0) ? (sumRpm / rpmSampleCount) : 0;
    float avgSpeed = rpmToSpeed(avgRpm);
    float dist = getDisplayDistance();
    unsigned long durationSec = accumulatedActiveMs / 1000;

    // Append summary to the session file
    File f = SD.open(sessionFilename, FILE_APPEND);
    if (!f) {
        Serial.println("SD: Failed to write summary");
        return;
    }

    f.println();
    f.println("# SESSION SUMMARY");
    f.printf("# duration_sec,%lu\n", durationSec);
    f.printf("# distance,%s,%.2f\n", distUnit(), dist);
    f.printf("# avg_rpm,%.1f\n", avgRpm);
    f.printf("# max_rpm,%.1f\n", maxRpm);
    f.printf("# avg_speed,%s,%.1f\n", speedUnit(), avgSpeed);
    f.printf("# max_speed,%s,%.1f\n", speedUnit(), maxSpeed);
    f.printf("# total_revolutions,%lu\n", displayPulseCount);
    f.close();

    // Also write a summary-only file for easy parsing
    char summaryFile[28];
    sprintf(summaryFile, "/summary_%04d.csv", sessionNumber);
    File sf = SD.open(summaryFile, FILE_WRITE);
    if (sf) {
        sf.println("field,unit,value");
        sf.printf("duration,sec,%lu\n", durationSec);
        sf.printf("distance,%s,%.2f\n", distUnit(), dist);
        sf.printf("avg_rpm,rpm,%.1f\n", avgRpm);
        sf.printf("max_rpm,rpm,%.1f\n", maxRpm);
        sf.printf("avg_speed,%s,%.1f\n", speedUnit(), avgSpeed);
        sf.printf("max_speed,%s,%.1f\n", speedUnit(), maxSpeed);
        sf.printf("revolutions,count,%lu\n", displayPulseCount);
        sf.close();
        Serial.printf("SD: Summary written to %s\n", summaryFile);
    }

    Serial.printf("SD: Session %d saved. Duration: %lus, Dist: %.2f %s, Avg RPM: %.1f\n",
                  sessionNumber, durationSec, dist, distUnit(), avgRpm);
}

// ── Display Drawing ──────────────────────────────────────────

void drawStaticUI() {
    tft.fillScreen(BG_COLOR);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TITLE_COLOR, BG_COLOR);
    tft.drawString("VIBE BIKE", 120, TITLE_Y, 2);

    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("RPM", 120, RPM_LABEL_Y, 4);

    tft.drawFastHLine(10, DIV1_Y, 220, DIVIDER_COLOR);

    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("SPEED", LEFT_COL, SPEED_LABEL_Y, 2);
    tft.drawString("DISTANCE", RIGHT_COL, DIST_LABEL_Y, 2);
    tft.drawString(speedUnit(), LEFT_COL, SPEED_UNIT_Y, 2);
    tft.drawString(distUnit(), RIGHT_COL, DIST_UNIT_Y, 2);

    tft.drawFastHLine(10, DIV2_Y, 220, DIVIDER_COLOR);

    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("TIME", 120, TIME_LABEL_Y, 2);

    tft.drawFastVLine(120, DIV1_Y + 2, DIV2_Y - DIV1_Y - 4, DIVIDER_COLOR);

    // SD card indicator (top right)
    tft.setTextDatum(TR_DATUM);
    if (sdReady) {
        tft.setTextColor(TFT_GREEN, BG_COLOR);
        tft.drawString("SD", 234, TITLE_Y, 2);
    } else {
        tft.setTextColor(TFT_RED, BG_COLOR);
        tft.drawString("--", 234, TITLE_Y, 2);
    }
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
    if (state != prevDisplayState || firstDraw) {
        prevDisplayState = state;

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
                tft.drawString("SAVED", 14, STATUS_Y, 2);
                break;
        }
    }

    if (pulses != prevDisplayPulses || firstDraw) {
        prevDisplayPulses = pulses;

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
                maxRpm = 0;
                maxSpeed = 0;
                sumRpm = 0;
                rpmSampleCount = 0;
                sessionLogged = false;
                startSessionLog();
                Serial.println("Session: READY -> ACTIVE");
            }
            break;

        case SESSION_ACTIVE:
            if (timeSinceLastPulse > PAUSE_TIMEOUT_MS) {
                accumulatedActiveMs += (lastPulseTimeLoop - activeStartTime);
                sessionState = SESSION_PAUSED;
                Serial.println("Session: ACTIVE -> PAUSED");
            }
            break;

        case SESSION_PAUSED:
            if (gotPulse) {
                activeStartTime = now;
                sessionState = SESSION_ACTIVE;
                Serial.println("Session: PAUSED -> ACTIVE (resumed)");
            } else if (timeSinceLastPulse > END_TIMEOUT_MS) {
                sessionState = SESSION_ENDED;
                writeSessionSummary();
                Serial.println("Session: PAUSED -> ENDED");
            }
            break;

        case SESSION_ENDED:
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

    // SD card (uses VSPI, separate from display HSPI)
    sdReady = initSD();

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

    Serial.println("Vibe Bike Dashboard v1.2 — Ready");
    if (!sdReady) Serial.println("WARNING: SD card not available. Session data will not be saved.");
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
            return;
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

    // RPM timeout
    if (lastPulseTimeLoop > 0 && timeSinceLastPulse > RPM_TIMEOUT_MS) {
        currentRpm = 0;
        clearRpmBuffer();
    }

    // Session state machine
    updateSessionState(now, timeSinceLastPulse, gotPulse);

    // Calculate values
    smoothedRpm = (currentRpm > 0) ? getSmoothedRpm() : 0;
    currentSpeed = rpmToSpeed(smoothedRpm);

    // Track session stats (only when actively pedaling)
    if (sessionState == SESSION_ACTIVE && smoothedRpm > 0) {
        sumRpm += smoothedRpm;
        rpmSampleCount++;
        if (smoothedRpm > maxRpm) maxRpm = smoothedRpm;
        if (currentSpeed > maxSpeed) maxSpeed = currentSpeed;
    }

    // Raw data logging to SD (every RAW_LOG_INTERVAL_MS during active/paused session)
    if (sdReady && (sessionState == SESSION_ACTIVE || sessionState == SESSION_PAUSED)) {
        if (now - lastRawLogTime >= RAW_LOG_INTERVAL_MS) {
            lastRawLogTime = now;
            logRawData(getActiveTimeMs(now));
        }
    }

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
