// bike_dashboard.ino — Vibe Bike Dashboard (VB-004 through VB-017)
// Pulse counter + speed/distance + TFT display + session auto-detect + SD logging
// + BLE heart rate (Phase 2) + WiFi Claude Code stats tracking (Phase 3).
//
// Board: ESP32-32E with ILI9341V (240x320)
// Wiring:
//   3.3V ─── 10K resistor ─── IO35 ─── bike wire 1
//   GND  ────────────────────────────── bike wire 2
//
// SD Card: VSPI bus (IO5=CS, IO23=MOSI, IO18=SCLK, IO19=MISO)
// Display: HSPI bus (IO15=CS, IO13=MOSI, IO14=SCLK, IO12=MISO)
//
// Build (min_spiffs partition needed for BLE+WiFi+HTTPS):
//   arduino-cli compile -b esp32:esp32:esp32:PartitionScheme=min_spiffs firmware/bike_dashboard
//   arduino-cli upload -b esp32:esp32:esp32:PartitionScheme=min_spiffs -p /dev/cu.usbmodem2101 firmware/bike_dashboard
//   arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

#include <TFT_eSPI.h>
#include <SD.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <Preferences.h>
#include "config.h"  // WiFi + stats server + BLE config (copy config.example.h → config.h)

// Fallback for older config.h without BLE setting
#ifndef BLE_HR_DEVICE_NAME
#define BLE_HR_DEVICE_NAME  ""
#endif

// ── WiFi / Stats Settings ────────────────────────────────────
#define WIFI_TIMEOUT_MS   10000
#define STATS_POLL_INTERVAL_MS 30000  // Poll every 30 seconds

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
#define RAW_LOG_INTERVAL_MS  1000

// ── Colors ───────────────────────────────────────────────────
#define BG_COLOR          TFT_BLACK
#define TITLE_COLOR       0x04B3
#define LABEL_COLOR       0x7BEF
#define RPM_COLOR         TFT_CYAN
#define SPEED_COLOR       TFT_GREEN
#define DIST_COLOR        TFT_YELLOW
#define TIME_COLOR        0xFD20
#define HR_COLOR          TFT_RED
#define TOKEN_COLOR       0xF81F    // Magenta
#define MSGS_COLOR        0x07FF    // Cyan-ish
#define DIVIDER_COLOR     0x2104
#define STATUS_ACTIVE     TFT_GREEN
#define STATUS_PAUSED     TFT_YELLOW
#define STATUS_ENDED      TFT_RED
#define STATUS_READY      0x7BEF
#define DIMMED_COLOR      0x4208    // Gray for disconnected values
#define HR_ZONE_IN_COLOR  0x0400    // Dark green background for in-zone
#define HR_ZONE_OUT_COLOR TFT_RED   // Red for out-of-zone flash
#define BUTTON_COLOR      0x4A69    // Gray button fill
#define BUTTON_TEXT_COLOR TFT_WHITE
#define TOGGLE_ON_COLOR   0x07E0    // Green
#define TOGGLE_OFF_COLOR  TFT_RED

// ── Touch Configuration (XPT2046 bit-bang SPI) ───────────────
#define TOUCH_MOSI  32
#define TOUCH_MISO  39
#define TOUCH_CLK   25
#define TOUCH_CS    33
#define TOUCH_IRQ   36
#define TOUCH_DEBOUNCE_MS 300
// Calibration from raw diagnostic (raw ADC → screen pixels)
#define TOUCH_RAW_X_MIN  185
#define TOUCH_RAW_X_MAX  1770
#define TOUCH_RAW_Y_MIN  150
#define TOUCH_RAW_Y_MAX  1840
#define TOUCH_PRESSURE_MIN 50  // Z1 threshold for real touch

// ── Page State ───────────────────────────────────────────────
enum PageState {
    PAGE_DASHBOARD,
    PAGE_HR_ZONE_SETTINGS
};

// ── Compact Layout (240x320 portrait) ────────────────────────
// Title bar
#define TITLE_Y       4
// RPM section (value centered, "RPM" label drawn to the right dynamically)
#define RPM_VALUE_Y   28
// Divider 1
#define DIV1_Y        80
// Speed / Distance row
#define SPEED_LABEL_Y 94
#define SPEED_VALUE_Y 112
#define SPEED_UNIT_Y  148
#define DIST_LABEL_Y  94
#define DIST_VALUE_Y  112
#define DIST_UNIT_Y   148
// Divider 2
#define DIV2_Y        168
// Time / HR row
#define TIME_LABEL_Y  172
#define TIME_VALUE_Y  190
#define HR_LABEL_Y    172
#define HR_VALUE_Y    190
#define HR_UNIT_Y     226
// Divider 3
#define DIV3_Y        246
// Tokens / Messages row
#define TOKEN_LABEL_Y 250
#define TOKEN_VALUE_Y 268
#define MSGS_LABEL_Y  250
#define MSGS_VALUE_Y  268
// Divider 4
#define DIV4_Y        294
// Status bar
#define STATUS_Y      302
// Columns
#define LEFT_COL      60
#define RIGHT_COL     180

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;

// ── Page & Touch State ───────────────────────────────────────
PageState currentPage = PAGE_DASHBOARD;
unsigned long lastTouchTime = 0;

// ── HR Zone State ────────────────────────────────────────────
bool hrZoneEnabled = false;
uint16_t hrZoneUpper = 160;
uint16_t hrZoneLower = 120;
bool hrZoneFlashState = false;        // Toggles for out-of-zone flash
unsigned long lastZoneFlashTime = 0;

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
double sumRpm = 0;
unsigned long rpmSampleCount = 0;

// SD card state
bool sdReady = false;
int sessionNumber = 0;
char sessionFilename[24];
unsigned long lastRawLogTime = 0;
bool sessionLogged = false;

// ── BLE Heart Rate State ─────────────────────────────────────
static BLEUUID hrServiceUUID("180D");
static BLEUUID hrCharUUID("2A37");
static BLEClient* pBLEClient = nullptr;
static volatile uint16_t currentHR = 0;
static volatile bool hrConnected = false;
static bool bleDoConnect = false;
static BLEAdvertisedDevice* bleTargetDevice = nullptr;
static unsigned long lastBleScanTime = 0;

// HR session stats
uint16_t maxHR = 0;
uint16_t minHR = 0;    // Min non-zero HR during session
double sumHR = 0;
unsigned long hrSampleCount = 0;

// ── WiFi & Stats State ───────────────────────────────────────
bool wifiEnabled = false;
bool wifiConnected = false;
bool statsTrackingEnabled = false;
unsigned long lastStatsPollTime = 0;

// Stats from Claude Code (via local stats server)
unsigned long sessionStartTokens = 0;
unsigned long sessionStartMsgs = 0;
unsigned long latestTokens = 0;
unsigned long latestMsgs = 0;
unsigned long sessionTokensDelta = 0;
unsigned long sessionMsgsDelta = 0;
unsigned long todayTokens = 0;
unsigned long todayMsgs = 0;
bool statsDataValid = false;
bool initialStatsFetchDone = false;

// Previous display values (for dirty-region updates)
int prevRpmInt = -1;
int prevSpeedTenths = -1;
int prevDistHundredths = -1;
int prevTimeSec = -1;
int prevHR = -1;
bool prevHRConnected = false;
long prevTokensK = -1;
long prevMsgs = -1;
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

// ── BLE Heart Rate (built-in ESP32 BLE) ──────────────────────

// Notification callback
static void hrNotifyCallback(BLERemoteCharacteristic* pChar,
                             uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint16_t hr;
    if (pData[0] & 0x01) {
        hr = (length >= 3) ? (pData[1] | (pData[2] << 8)) : pData[1];
    } else {
        hr = pData[1];
    }

    currentHR = hr;
}

class HRClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        hrConnected = true;
        Serial.println("BLE: Connected to HR strap");
    }
    void onDisconnect(BLEClient* pClient) override {
        hrConnected = false;
        currentHR = 0;
        Serial.println("BLE: HR strap disconnected");
    }
};

class HRScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        String name = String(advertisedDevice.getName().c_str());
        bool hasHRService = advertisedDevice.haveServiceUUID() &&
                            advertisedDevice.isAdvertisingService(hrServiceUUID);

        bool nameMatch = (strlen(BLE_HR_DEVICE_NAME) > 0) ?
                          (name.indexOf(BLE_HR_DEVICE_NAME) >= 0) : false;

        if (hasHRService || nameMatch) {
            Serial.printf("BLE: Found HR device: %s [%s]\n",
                          name.c_str(),
                          advertisedDevice.getAddress().toString().c_str());
            advertisedDevice.getScan()->stop();
            bleTargetDevice = new BLEAdvertisedDevice(advertisedDevice);
            bleDoConnect = true;
        }
    }
};

bool connectToHR() {
    if (!bleTargetDevice) return false;

    pBLEClient = BLEDevice::createClient();
    pBLEClient->setClientCallbacks(new HRClientCallbacks());

    Serial.printf("BLE: Connecting to %s...\n",
                  bleTargetDevice->getAddress().toString().c_str());

    if (!pBLEClient->connect(bleTargetDevice)) {
        Serial.println("BLE: Connection failed");
        return false;
    }

    BLERemoteService* pService = pBLEClient->getService(hrServiceUUID);
    if (!pService) {
        Serial.println("BLE: HR service not found");
        pBLEClient->disconnect();
        return false;
    }

    BLERemoteCharacteristic* pChar = pService->getCharacteristic(hrCharUUID);
    if (!pChar) {
        Serial.println("BLE: HR characteristic not found");
        pBLEClient->disconnect();
        return false;
    }

    if (pChar->canNotify()) {
        pChar->registerForNotify(hrNotifyCallback);
        Serial.println("BLE: Subscribed to HR notifications");
    } else {
        Serial.println("BLE: Characteristic doesn't support notify");
        pBLEClient->disconnect();
        return false;
    }

    return true;
}

static HRScanCallbacks hrScanCB;
static bool bleScanActive = false;

void initBLE() {
    BLEDevice::init("VibeBike");

    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&hrScanCB, false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);

    Serial.println("BLE: Starting scan for HR devices...");
    pScan->start(0, nullptr, false);  // Continuous background scan
    bleScanActive = true;
}

void handleBLE() {
    // Handle pending connection from scan callback
    if (bleDoConnect) {
        bleDoConnect = false;

        BLEDevice::getScan()->stop();
        bleScanActive = false;

        if (connectToHR()) {
            delete bleTargetDevice;
            bleTargetDevice = nullptr;
            return;
        }
        delete bleTargetDevice;
        bleTargetDevice = nullptr;
    }

    // Restart scan if not connected and scan not running
    if (!hrConnected && !bleDoConnect && !bleScanActive) {
        unsigned long now = millis();
        if (now - lastBleScanTime > 5000) {
            lastBleScanTime = now;
            Serial.println("BLE: Restarting scan...");
            BLEScan* pScan = BLEDevice::getScan();
            pScan->clearResults();
            pScan->start(0, nullptr, false);
            bleScanActive = true;
        }
    }
}

// ── WiFi Functions ───────────────────────────────────────────

void initWiFi() {
    if (strlen(WIFI_SSID) == 0) {
        Serial.println("WiFi: No SSID configured, skipping");
        return;
    }

    wifiEnabled = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("WiFi: Connecting to %s", WIFI_SSID);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.printf("WiFi: Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("WiFi: Connection failed (will retry in background)");
    }

    // Enable stats tracking if server IP is set
    if (strlen(STATS_SERVER_IP) > 0) {
        statsTrackingEnabled = true;
        Serial.printf("WiFi: Stats tracking enabled (server: %s:%d)\n",
                      STATS_SERVER_IP, STATS_SERVER_PORT);
    }
}

void handleWiFi() {
    if (!wifiEnabled) return;

    bool nowConnected = (WiFi.status() == WL_CONNECTED);
    if (nowConnected != wifiConnected) {
        wifiConnected = nowConnected;
        if (wifiConnected) {
            Serial.println("WiFi: Reconnected");
        } else {
            Serial.println("WiFi: Disconnected");
        }
    }

    // Reconnect if needed
    if (!wifiConnected) {
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 30000) {
            lastReconnectAttempt = millis();
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }
}

// ── Claude Code Stats (via local HTTP server) ────────────────

bool fetchStats(unsigned long* outTokens, unsigned long* outMsgs) {
    if (!wifiConnected || !statsTrackingEnabled) return false;

    HTTPClient http;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/stats", STATS_SERVER_IP, STATS_SERVER_PORT);

    http.begin(url);
    http.setTimeout(5000);

    int httpCode = http.GET();

    if (httpCode != 200) {
        Serial.printf("Stats: HTTP %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON: {"date":"...","messages":N,"sessions":N,"toolCalls":N,"outputTokens":N}
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("Stats: JSON error: %s\n", err.c_str());
        return false;
    }

    *outTokens = doc["outputTokens"] | 0UL;
    *outMsgs = doc["messages"] | 0UL;

    Serial.printf("Stats: tokens=%lu msgs=%lu\n", *outTokens, *outMsgs);
    return true;
}

void handleStatsTracking(unsigned long now) {
    if (!statsTrackingEnabled || !wifiConnected) return;

    // Poll at interval
    if (now - lastStatsPollTime < STATS_POLL_INTERVAL_MS && lastStatsPollTime > 0) return;
    lastStatsPollTime = now;

    unsigned long tokens, msgs;
    if (!fetchStats(&tokens, &msgs)) return;

    latestTokens = tokens;
    latestMsgs = msgs;
    todayTokens = tokens;
    todayMsgs = msgs;
    statsDataValid = true;

    // On first fetch during a session, record baseline
    if (!initialStatsFetchDone && sessionState == SESSION_ACTIVE) {
        sessionStartTokens = tokens;
        sessionStartMsgs = msgs;
        initialStatsFetchDone = true;
        Serial.printf("Stats: Session baseline — tokens=%lu msgs=%lu\n", tokens, msgs);
    }

    // Calculate session delta
    if (initialStatsFetchDone) {
        sessionTokensDelta = latestTokens - sessionStartTokens;
        sessionMsgsDelta = latestMsgs - sessionStartMsgs;
    }
}

// ── SD Card Functions ────────────────────────────────────────

int findNextSessionNumber() {
    int maxNum = 0;
    File root = SD.open("/");
    if (!root) return 1;

    File entry;
    while ((entry = root.openNextFile())) {
        const char* name = entry.name();
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

    f.println("elapsed_sec,rpm,speed,distance,heart_rate,pulses");
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
    uint16_t hr = currentHR;
    f.printf("%.1f,%.1f,%.1f,%.2f,%u,%lu\n",
             activeMs / 1000.0, smoothedRpm, currentSpeed, dist, hr, displayPulseCount);
    f.close();
}

void writeSessionSummary() {
    if (!sdReady || sessionLogged) return;
    sessionLogged = true;

    float avgRpm = (rpmSampleCount > 0) ? (sumRpm / rpmSampleCount) : 0;
    float avgSpeed = rpmToSpeed(avgRpm);
    float dist = getDisplayDistance();
    unsigned long durationSec = accumulatedActiveMs / 1000;
    float avgHR = (hrSampleCount > 0) ? (sumHR / hrSampleCount) : 0;

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
    f.printf("# avg_hr,%.0f\n", avgHR);
    f.printf("# max_hr,%u\n", maxHR);
    f.printf("# min_hr,%u\n", minHR);
    if (hrZoneEnabled) {
        f.printf("# hr_zone_lower,%u\n", hrZoneLower);
        f.printf("# hr_zone_upper,%u\n", hrZoneUpper);
    }
    if (statsDataValid && initialStatsFetchDone) {
        f.printf("# tokens_during_ride,%lu\n", sessionTokensDelta);
        f.printf("# msgs_during_ride,%lu\n", sessionMsgsDelta);
    }
    f.close();

    // Summary-only file
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
        sf.printf("avg_hr,bpm,%.0f\n", avgHR);
        sf.printf("max_hr,bpm,%u\n", maxHR);
        sf.printf("min_hr,bpm,%u\n", minHR);
        if (hrZoneEnabled) {
            sf.printf("hr_zone_lower,bpm,%u\n", hrZoneLower);
            sf.printf("hr_zone_upper,bpm,%u\n", hrZoneUpper);
        }
        if (statsDataValid && initialStatsFetchDone) {
            sf.printf("tokens_during_ride,count,%lu\n", sessionTokensDelta);
            sf.printf("msgs_during_ride,count,%lu\n", sessionMsgsDelta);
        }
        sf.close();
        Serial.printf("SD: Summary written to %s\n", summaryFile);
    }

    Serial.printf("SD: Session %d saved. Duration: %lus, Dist: %.2f %s, Avg RPM: %.1f, Avg HR: %.0f\n",
                  sessionNumber, durationSec, dist, distUnit(), avgRpm, avgHR);
}

// ── Icon Drawing ──────────────────────────────────────────────

// Icon positions in title bar
#define HEART_CX    188
#define HEART_CY    12
#define WIFI_CX     210
#define WIFI_DOT_Y  18

void drawHeartIcon(uint16_t color) {
    int cx = HEART_CX, cy = HEART_CY;
    // Two circles for the top bumps
    tft.fillCircle(cx - 3, cy - 1, 3, color);
    tft.fillCircle(cx + 3, cy - 1, 3, color);
    // Triangle for the bottom point
    tft.fillTriangle(cx - 5, cy + 1, cx + 5, cy + 1, cx, cy + 7, color);
}

void drawWiFiIcon(uint16_t color) {
    int cx = WIFI_CX, by = WIFI_DOT_Y;
    // Dot at bottom
    tft.fillCircle(cx, by, 1, color);
    // 3 arcs fanning upward
    for (int ring = 1; ring <= 3; ring++) {
        int r = ring * 3;
        for (int deg = 40; deg <= 140; deg++) {
            float rad = deg * 0.01745329f;
            int x = cx + (int)(r * cosf(rad) + 0.5f);
            int y = by - (int)(r * sinf(rad) + 0.5f);
            tft.drawPixel(x, y, color);
        }
    }
}

void updateBLEIndicator() {
    tft.fillRect(HEART_CX - 7, HEART_CY - 5, 15, 14, BG_COLOR);
    drawHeartIcon(hrConnected ? HR_COLOR : DIMMED_COLOR);
}

void updateWiFiIndicator() {
    if (!wifiEnabled) return;
    tft.fillRect(WIFI_CX - 10, WIFI_DOT_Y - 12, 21, 15, BG_COLOR);
    drawWiFiIcon(wifiConnected ? TFT_GREEN : DIMMED_COLOR);
}

// ── Touch Functions (raw XPT2046 bit-bang SPI) ───────────────

int touchSpiRead(byte command) {
    int result = 0;
    for (int i = 7; i >= 0; i--) {
        digitalWrite(TOUCH_MOSI, (command >> i) & 1);
        digitalWrite(TOUCH_CLK, HIGH);
        delayMicroseconds(10);
        digitalWrite(TOUCH_CLK, LOW);
        delayMicroseconds(10);
    }
    for (int i = 11; i >= 0; i--) {
        digitalWrite(TOUCH_CLK, HIGH);
        delayMicroseconds(10);
        result |= (digitalRead(TOUCH_MISO) << i);
        digitalWrite(TOUCH_CLK, LOW);
        delayMicroseconds(10);
    }
    return result;
}

void initTouch() {
    pinMode(TOUCH_MOSI, OUTPUT);
    pinMode(TOUCH_MISO, INPUT);
    pinMode(TOUCH_CLK, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);
    pinMode(TOUCH_IRQ, INPUT);
    digitalWrite(TOUCH_CS, HIGH);
    digitalWrite(TOUCH_CLK, LOW);
    Serial.println("Touch: Initialized (raw bit-bang SPI)");
}

bool readTouch(int* x, int* y) {
    if (digitalRead(TOUCH_IRQ) != LOW) return false;

    unsigned long now = millis();
    if (now - lastTouchTime < TOUCH_DEBOUNCE_MS) return false;
    lastTouchTime = now;

    // Read raw values (disable interrupts to protect bit-bang timing
    // from BLE/WiFi/pulse ISR corrupting the SPI signals)
    noInterrupts();
    digitalWrite(TOUCH_CS, LOW);
    int rawX = touchSpiRead(0xD0);
    int rawY = touchSpiRead(0x90);
    int rawZ1 = touchSpiRead(0xB0);
    digitalWrite(TOUCH_CS, HIGH);
    interrupts();

    // Check pressure
    if (rawZ1 < TOUCH_PRESSURE_MIN) return false;

    // Map raw ADC to screen coordinates (X is inverted on this panel)
    *x = map(rawX, TOUCH_RAW_X_MAX, TOUCH_RAW_X_MIN, 0, 240);
    *y = map(rawY, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, 320);
    *x = constrain(*x, 0, 240);
    *y = constrain(*y, 0, 320);

    Serial.printf("Touch: x=%d y=%d (raw %d,%d z=%d)\n", *x, *y, rawX, rawY, rawZ1);
    return true;
}

// ── HR Zone Persistence ──────────────────────────────────────

void loadHRZoneSettings() {
    prefs.begin("hrzone", true);  // read-only
    hrZoneEnabled = prefs.getBool("enabled", false);
    hrZoneUpper = prefs.getUShort("upper", 160);
    hrZoneLower = prefs.getUShort("lower", 120);
    prefs.end();
    Serial.printf("HR Zone: loaded — %s, %u-%u bpm\n",
                  hrZoneEnabled ? "ON" : "OFF", hrZoneLower, hrZoneUpper);
}

void saveHRZoneSettings() {
    prefs.begin("hrzone", false);  // read-write
    prefs.putBool("enabled", hrZoneEnabled);
    prefs.putUShort("upper", hrZoneUpper);
    prefs.putUShort("lower", hrZoneLower);
    prefs.end();
    Serial.printf("HR Zone: saved — %s, %u-%u bpm\n",
                  hrZoneEnabled ? "ON" : "OFF", hrZoneLower, hrZoneUpper);
}

// ── HR Zone Settings Page ────────────────────────────────────

void drawButton(int x, int y, int w, int h, const char* label, uint16_t bgColor) {
    tft.fillRoundRect(x, y, w, h, 6, bgColor);
    tft.drawRoundRect(x, y, w, h, 6, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(BUTTON_TEXT_COLOR, bgColor);
    tft.drawString(label, x + w / 2, y + h / 2, 2);
}

void drawToggleButton() {
    int y = 200;
    if (hrZoneEnabled) {
        drawButton(50, y, 140, 40, "ZONE ON", TOGGLE_ON_COLOR);
    } else {
        drawButton(50, y, 140, 40, "ZONE OFF", TOGGLE_OFF_COLOR);
    }
}

void drawHRZoneValue(int centerX, int y, uint16_t value) {
    // Clear value area
    tft.fillRect(centerX - 50, y, 100, 30, BG_COLOR);
    char buf[8];
    sprintf(buf, "%u", value);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, BG_COLOR);
    tft.drawString(buf, centerX, y + 8, 4);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("bpm", centerX + 30, y + 8, 2);
}

void drawHRZoneSettingsPage() {
    tft.fillScreen(BG_COLOR);

    // Title
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HR_COLOR, BG_COLOR);
    tft.drawString("HR ZONE SETTINGS", 120, 10, 2);

    tft.drawFastHLine(10, 30, 220, DIVIDER_COLOR);

    // Upper limit section
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("UPPER LIMIT", 120, 40, 2);

    drawButton(20, 65, 50, 40, "-", BUTTON_COLOR);   // Upper minus
    drawHRZoneValue(120, 65, hrZoneUpper);
    drawButton(170, 65, 50, 40, "+", BUTTON_COLOR);   // Upper plus

    tft.drawFastHLine(10, 115, 220, DIVIDER_COLOR);

    // Lower limit section
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("LOWER LIMIT", 120, 125, 2);

    drawButton(20, 150, 50, 40, "-", BUTTON_COLOR);   // Lower minus
    drawHRZoneValue(120, 150, hrZoneLower);
    drawButton(170, 150, 50, 40, "+", BUTTON_COLOR);   // Lower plus

    tft.drawFastHLine(10, 195, 220, DIVIDER_COLOR);

    // Toggle
    drawToggleButton();

    // Back button
    drawButton(50, 258, 140, 40, "BACK", BUTTON_COLOR);
}

// Touch hit-test helpers
bool inRect(int tx, int ty, int x, int y, int w, int h) {
    return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

void handleHRZoneTouch(int tx, int ty) {
    bool changed = false;

    // Upper minus (20, 65, 50, 40)
    if (inRect(tx, ty, 20, 65, 50, 40)) {
        if (hrZoneUpper - 5 >= hrZoneLower + 5) {
            hrZoneUpper -= 5;
            drawHRZoneValue(120, 65, hrZoneUpper);
            changed = true;
        }
    }
    // Upper plus (170, 65, 50, 40)
    else if (inRect(tx, ty, 170, 65, 50, 40)) {
        if (hrZoneUpper + 5 <= 220) {
            hrZoneUpper += 5;
            drawHRZoneValue(120, 65, hrZoneUpper);
            changed = true;
        }
    }
    // Lower minus (20, 150, 50, 40)
    else if (inRect(tx, ty, 20, 150, 50, 40)) {
        if (hrZoneLower - 5 >= 40) {
            hrZoneLower -= 5;
            drawHRZoneValue(120, 150, hrZoneLower);
            changed = true;
        }
    }
    // Lower plus (170, 150, 50, 40)
    else if (inRect(tx, ty, 170, 150, 50, 40)) {
        if (hrZoneLower + 5 <= hrZoneUpper - 5) {
            hrZoneLower += 5;
            drawHRZoneValue(120, 150, hrZoneLower);
            changed = true;
        }
    }
    // Toggle (50, 200, 140, 40)
    else if (inRect(tx, ty, 50, 200, 140, 40)) {
        hrZoneEnabled = !hrZoneEnabled;
        drawToggleButton();
        changed = true;
    }
    // Back (50, 258, 140, 40)
    else if (inRect(tx, ty, 50, 258, 140, 40)) {
        if (changed) saveHRZoneSettings();
        switchToPage(PAGE_DASHBOARD);
        return;
    }

    if (changed) saveHRZoneSettings();
}

void handleDashboardTouch(int tx, int ty) {
    // HR block area: right column, between DIV2 and DIV3
    // X: 120-240, Y: DIV2_Y(168) to DIV3_Y(246)
    if (tx >= 120 && tx <= 240 && ty >= DIV2_Y && ty <= DIV3_Y) {
        switchToPage(PAGE_HR_ZONE_SETTINGS);
    }
}

void handleTouch(int tx, int ty) {
    switch (currentPage) {
        case PAGE_DASHBOARD:
            handleDashboardTouch(tx, ty);
            break;
        case PAGE_HR_ZONE_SETTINGS:
            handleHRZoneTouch(tx, ty);
            break;
    }
}

void switchToPage(PageState page) {
    currentPage = page;
    if (page == PAGE_DASHBOARD) {
        // Reset dirty-region trackers so everything redraws
        prevRpmInt = -1;
        prevSpeedTenths = -1;
        prevDistHundredths = -1;
        prevTimeSec = -1;
        prevHR = -1;
        prevHRConnected = false;
        prevTokensK = -1;
        prevMsgs = -1;
        prevDisplayState = SESSION_READY;
        prevDisplayPulses = 0;
        firstDraw = true;
        drawStaticUI();
        // Force immediate update of all dynamic values
        updateRpmDisplay(smoothedRpm);
        updateSpeedDisplay(currentSpeed);
        updateDistDisplay(getDisplayDistance());
        updateTimeDisplay(getActiveTimeMs(millis()));
        updateHRDisplay(currentHR);
        updateTokenDisplay();
        updateStatusBar(sessionState, displayPulseCount);
        updateBLEIndicator();
        updateWiFiIndicator();
        firstDraw = false;
    } else if (page == PAGE_HR_ZONE_SETTINGS) {
        drawHRZoneSettingsPage();
    }
}

// ── Display Drawing ──────────────────────────────────────────

void drawStaticUI() {
    tft.fillScreen(BG_COLOR);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TITLE_COLOR, BG_COLOR);
    tft.drawString("VIBE BIKE", 120, TITLE_Y, 2);

    // Divider 1
    tft.drawFastHLine(10, DIV1_Y, 220, DIVIDER_COLOR);

    // Speed / Distance labels and units
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("SPEED", LEFT_COL, SPEED_LABEL_Y, 2);
    tft.drawString("DISTANCE", RIGHT_COL, DIST_LABEL_Y, 2);
    tft.drawString(speedUnit(), LEFT_COL, SPEED_UNIT_Y, 2);
    tft.drawString(distUnit(), RIGHT_COL, DIST_UNIT_Y, 2);

    // Vertical divider between speed/distance
    tft.drawFastVLine(120, DIV1_Y + 2, DIV2_Y - DIV1_Y - 4, DIVIDER_COLOR);

    // Divider 2
    tft.drawFastHLine(10, DIV2_Y, 220, DIVIDER_COLOR);

    // Time / HR labels and units
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("TIME", LEFT_COL, TIME_LABEL_Y, 2);
    if (hrZoneEnabled) {
        char zoneLabel[12];
        sprintf(zoneLabel, "%u-%u", hrZoneLower, hrZoneUpper);
        tft.drawString(zoneLabel, RIGHT_COL, HR_LABEL_Y, 2);
    } else {
        tft.drawString("HR", RIGHT_COL, HR_LABEL_Y, 2);
    }
    tft.drawString("bpm", RIGHT_COL, HR_UNIT_Y, 2);

    // Vertical divider between time/HR
    tft.drawFastVLine(120, DIV2_Y + 2, DIV3_Y - DIV2_Y - 4, DIVIDER_COLOR);

    // Divider 3
    tft.drawFastHLine(10, DIV3_Y, 220, DIVIDER_COLOR);

    // Tokens / Messages labels
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("TOKENS", LEFT_COL, TOKEN_LABEL_Y, 2);
    tft.drawString("MSGS", RIGHT_COL, MSGS_LABEL_Y, 2);

    // Vertical divider between tokens/msgs
    tft.drawFastVLine(120, DIV3_Y + 2, DIV4_Y - DIV3_Y - 4, DIVIDER_COLOR);

    // Divider 4
    tft.drawFastHLine(10, DIV4_Y, 220, DIVIDER_COLOR);

    // Status indicators (top right)
    tft.setTextDatum(TR_DATUM);

    // SD indicator
    if (sdReady) {
        tft.setTextColor(TFT_GREEN, BG_COLOR);
        tft.drawString("SD", 234, TITLE_Y, 2);
    } else {
        tft.setTextColor(TFT_RED, BG_COLOR);
        tft.drawString("--", 234, TITLE_Y, 2);
    }

    // BLE heart icon (gray = not connected, red = connected)
    drawHeartIcon(DIMMED_COLOR);

    // WiFi icon (gray = not connected, green = connected)
    if (wifiEnabled) {
        drawWiFiIcon(wifiConnected ? TFT_GREEN : DIMMED_COLOR);
    }
}

void updateRpmDisplay(float rpm) {
    int rpmInt = (int)(rpm + 0.5);
    if (rpmInt == prevRpmInt && !firstDraw) return;

    // Clear entire RPM row (value + label area)
    tft.fillRect(0, RPM_VALUE_Y, 240, tft.fontHeight(6), BG_COLOR);

    prevRpmInt = rpmInt;

    char buf[8];
    sprintf(buf, "%d", rpmInt);

    // Draw RPM value right-aligned before center
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(RPM_COLOR, BG_COLOR);
    tft.drawString(buf, 125, RPM_VALUE_Y, 6);

    // Draw "RPM" label to the right, vertically centered with the number
    int labelY = RPM_VALUE_Y + (tft.fontHeight(6) - tft.fontHeight(2)) / 2;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("RPM", 130, labelY, 2);
}

void updateSpeedDisplay(float speed) {
    int speedTenths = (int)(speed * 10 + 0.5);
    if (speedTenths == prevSpeedTenths && !firstDraw) return;

    int prevWidth = tft.textWidth("00.0", 4);
    tft.fillRect(LEFT_COL - prevWidth / 2, SPEED_VALUE_Y, prevWidth, tft.fontHeight(4), BG_COLOR);

    prevSpeedTenths = speedTenths;

    char buf[8];
    sprintf(buf, "%.1f", speed);

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
    tft.drawString(buf, LEFT_COL, TIME_VALUE_Y, 4);
}

void updateHRDisplay(uint16_t hr) {
    int hrInt = (int)hr;
    bool conn = hrConnected;

    // Determine zone background color
    uint16_t bgCol = BG_COLOR;
    if (hrZoneEnabled && conn && hrInt > 0) {
        if (hrInt >= hrZoneLower && hrInt <= hrZoneUpper) {
            bgCol = HR_ZONE_IN_COLOR;  // Green — in zone
        } else {
            // Flash red when out of zone (toggle every 500ms)
            unsigned long now = millis();
            if (now - lastZoneFlashTime >= 500) {
                lastZoneFlashTime = now;
                hrZoneFlashState = !hrZoneFlashState;
            }
            bgCol = hrZoneFlashState ? HR_ZONE_OUT_COLOR : BG_COLOR;
        }
    }

    // Check if zone flash state changed (forces redraw even if HR value unchanged)
    static uint16_t prevBgCol = BG_COLOR;
    if (hrInt == prevHR && conn == prevHRConnected && bgCol == prevBgCol && !firstDraw) return;
    prevHR = hrInt;
    prevHRConnected = conn;
    prevBgCol = bgCol;

    // Draw zone border around HR block (right half between DIV2 and DIV3)
    int bx = 121, by = DIV2_Y + 1, bw = 118, bh = DIV3_Y - DIV2_Y - 2;
    if (hrZoneEnabled && conn && hrInt > 0 && bgCol != BG_COLOR) {
        // 2px colored border
        tft.drawRect(bx, by, bw, bh, bgCol);
        tft.drawRect(bx + 1, by + 1, bw - 2, bh - 2, bgCol);
    } else {
        // Clear border to black
        tft.drawRect(bx, by, bw, bh, BG_COLOR);
        tft.drawRect(bx + 1, by + 1, bw - 2, bh - 2, BG_COLOR);
    }

    // Redraw label and unit (always on black background)
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    if (hrZoneEnabled) {
        char zoneLabel[12];
        sprintf(zoneLabel, "%u-%u", hrZoneLower, hrZoneUpper);
        tft.drawString(zoneLabel, RIGHT_COL, HR_LABEL_Y, 2);
    } else {
        tft.drawString("HR", RIGHT_COL, HR_LABEL_Y, 2);
    }
    tft.drawString("bpm", RIGHT_COL, HR_UNIT_Y, 2);

    // Draw HR value
    // Clear previous value area
    int prevWidth = tft.textWidth("000", 4);
    tft.fillRect(RIGHT_COL - prevWidth / 2, HR_VALUE_Y, prevWidth, tft.fontHeight(4), BG_COLOR);

    char buf[8];
    if (!conn) {
        sprintf(buf, "--");
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString(buf, RIGHT_COL, HR_VALUE_Y, 4);
    } else {
        sprintf(buf, "%d", hrInt);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(HR_COLOR, BG_COLOR);
        tft.drawString(buf, RIGHT_COL, HR_VALUE_Y, 4);
    }
}

void updateTokenDisplay() {
    // Show delta during ride (tokens/msgs generated while pedaling)
    // Before first pedal or after session ends, show 0
    unsigned long displayTokens = initialStatsFetchDone ? sessionTokensDelta : 0;
    unsigned long displayMsgs = initialStatsFetchDone ? sessionMsgsDelta : 0;

    long tokensK = (long)(displayTokens / 1000);
    long msgs = (long)displayMsgs;

    if (tokensK == prevTokensK && msgs == prevMsgs && !firstDraw) return;
    prevTokensK = tokensK;
    prevMsgs = msgs;

    char buf[12];

    // Clear token value area
    int prevWidth = tft.textWidth("999.9K", 4);
    tft.fillRect(LEFT_COL - prevWidth / 2, TOKEN_VALUE_Y, prevWidth, tft.fontHeight(4), BG_COLOR);

    if (!statsTrackingEnabled || !wifiConnected) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("--", LEFT_COL, TOKEN_VALUE_Y, 4);
    } else if (!statsDataValid) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("...", LEFT_COL, TOKEN_VALUE_Y, 4);
    } else {
        if (displayTokens >= 1000000) {
            sprintf(buf, "%.1fM", displayTokens / 1000000.0);
        } else if (displayTokens >= 1000) {
            sprintf(buf, "%.1fK", displayTokens / 1000.0);
        } else {
            sprintf(buf, "%lu", displayTokens);
        }
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TOKEN_COLOR, BG_COLOR);
        tft.drawString(buf, LEFT_COL, TOKEN_VALUE_Y, 4);
    }

    // Clear msgs value area
    prevWidth = tft.textWidth("9999", 4);
    tft.fillRect(RIGHT_COL - prevWidth / 2, MSGS_VALUE_Y, prevWidth, tft.fontHeight(4), BG_COLOR);

    if (!statsTrackingEnabled || !wifiConnected) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("--", RIGHT_COL, MSGS_VALUE_Y, 4);
    } else if (!statsDataValid) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("...", RIGHT_COL, MSGS_VALUE_Y, 4);
    } else {
        sprintf(buf, "%lu", displayMsgs);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(MSGS_COLOR, BG_COLOR);
        tft.drawString(buf, RIGHT_COL, MSGS_VALUE_Y, 4);
    }
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
                maxHR = 0;
                minHR = 0;
                sumHR = 0;
                hrSampleCount = 0;
                sessionTokensDelta = 0;
                sessionMsgsDelta = 0;
                initialStatsFetchDone = false;
                sessionLogged = false;
                startSessionLog();
                Serial.println("Session: READY -> ACTIVE");
            }
            break;

        case SESSION_ACTIVE:
            if (timeSinceLastPulse > PAUSE_TIMEOUT_MS) {
                unsigned long activeEnd = lastPulseTimeLoop > activeStartTime ? lastPulseTimeLoop : now;
                accumulatedActiveMs += (activeEnd - activeStartTime);
                sessionState = SESSION_PAUSED;
                Serial.printf("Session: ACTIVE -> PAUSED (accumulated: %lu ms)\n", accumulatedActiveMs);
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

    // WiFi (before BLE to establish connection first)
    initWiFi();

    // BLE Heart Rate
    initBLE();

    // Touch
    initTouch();

    // HR Zone settings (from NVS)
    loadHRZoneSettings();

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
    updateHRDisplay(0);
    updateTokenDisplay();
    updateStatusBar(SESSION_READY, 0);
    firstDraw = false;

    Serial.println("Vibe Bike Dashboard v2.1 — Ready (Phase 2+3 + HR Zones)");
    if (!sdReady) Serial.println("WARNING: SD card not available.");
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

        if (interval > 0) {
            currentRpm = 60000.0 / interval;
        }

        if (currentRpm < MIN_RPM) {
            currentRpm = 0;
        } else if (currentRpm > MAX_RPM) {
            return;
        }

        if (currentRpm > 0) {
            addRpmSample(currentRpm);
        }

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

    // Track HR stats during active session
    uint16_t hr = currentHR;
    if (sessionState == SESSION_ACTIVE && hr > 0) {
        sumHR += hr;
        hrSampleCount++;
        if (hr > maxHR) maxHR = hr;
        if (minHR == 0 || hr < minHR) minHR = hr;
    }

    // Handle BLE (connection management)
    handleBLE();

    // Handle WiFi (reconnection)
    handleWiFi();

    // Handle stats tracking (local server polling)
    handleStatsTracking(now);

    // Handle touch input
    int tx, ty;
    if (readTouch(&tx, &ty)) {
        handleTouch(tx, ty);
    }

    // Raw data logging to SD
    if (sdReady && (sessionState == SESSION_ACTIVE || sessionState == SESSION_PAUSED)) {
        if (now - lastRawLogTime >= RAW_LOG_INTERVAL_MS) {
            lastRawLogTime = now;
            logRawData(getActiveTimeMs(now));
        }
    }

    // Update display at fixed interval (only on dashboard page)
    if (currentPage == PAGE_DASHBOARD && now - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
        lastDisplayUpdate = now;

        updateRpmDisplay(smoothedRpm);
        updateSpeedDisplay(currentSpeed);
        updateDistDisplay(getDisplayDistance());
        updateTimeDisplay(getActiveTimeMs(now));
        updateHRDisplay(hr);
        updateTokenDisplay();
        updateStatusBar(sessionState, displayPulseCount);
        updateBLEIndicator();
        updateWiFiIndicator();
    }
}
