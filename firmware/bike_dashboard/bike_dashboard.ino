// bike_dashboard.ino — Vibe Bike Dashboard (VB-004 through VB-017)
// Pulse counter + speed/distance + TFT display + session auto-detect + SD logging
// + BLE heart rate (Phase 2) + WiFi token tracking (Phase 3).
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
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ── WiFi Configuration ─────────────────────────────────────────
// Set your WiFi credentials here, or leave empty to skip WiFi
#define WIFI_SSID         ""
#define WIFI_PASSWORD     ""
#define WIFI_TIMEOUT_MS   10000

// ── Anthropic API Configuration ────────────────────────────────
// Set your Admin API key here, or leave empty to skip token tracking
#define ANTHROPIC_ADMIN_KEY  ""
#define API_POLL_INTERVAL_MS 60000  // Poll every 60 seconds

// Anthropic pricing (per million tokens) — Claude Sonnet 4
#define INPUT_COST_PER_M   3.00
#define OUTPUT_COST_PER_M  15.00

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
#define COST_COLOR        0x07FF    // Cyan-ish
#define DIVIDER_COLOR     0x2104
#define STATUS_ACTIVE     TFT_GREEN
#define STATUS_PAUSED     TFT_YELLOW
#define STATUS_ENDED      TFT_RED
#define STATUS_READY      0x7BEF
#define DIMMED_COLOR      0x4208    // Gray for disconnected values

// ── Compact Layout (240x320 portrait) ────────────────────────
// Title bar
#define TITLE_Y       4
// RPM section
#define RPM_VALUE_Y   32
#define RPM_LABEL_Y   72
// Divider 1
#define DIV1_Y        90
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
// Tokens / Cost row
#define TOKEN_LABEL_Y 250
#define TOKEN_VALUE_Y 268
#define COST_LABEL_Y  250
#define COST_VALUE_Y  268
// Divider 4
#define DIV4_Y        294
// Status bar
#define STATUS_Y      302
// Columns
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
double sumRpm = 0;
unsigned long rpmSampleCount = 0;

// SD card state
bool sdReady = false;
int sessionNumber = 0;
char sessionFilename[24];
unsigned long lastRawLogTime = 0;
bool sessionLogged = false;

// ── BLE Heart Rate State ─────────────────────────────────────
static NimBLEUUID hrServiceUUID("180D");
static NimBLEUUID hrCharUUID("2A37");
static NimBLEClient* pBLEClient = nullptr;
static volatile uint16_t currentHR = 0;
static volatile bool hrConnected = false;
static bool bleDoConnect = false;
static NimBLEAdvertisedDevice* bleTargetDevice = nullptr;

// HR session stats
uint16_t maxHR = 0;
uint16_t minHR = 0;    // Min non-zero HR during session
double sumHR = 0;
unsigned long hrSampleCount = 0;

// ── WiFi & Token State ───────────────────────────────────────
bool wifiEnabled = false;
bool wifiConnected = false;
bool tokenTrackingEnabled = false;
unsigned long lastApiPollTime = 0;

// Token tracking
unsigned long sessionStartInputTokens = 0;
unsigned long sessionStartOutputTokens = 0;
unsigned long latestInputTokens = 0;
unsigned long latestOutputTokens = 0;
unsigned long sessionTokensDelta = 0;
float sessionCost = 0.0;
bool tokenDataValid = false;
bool initialTokenFetchDone = false;

// Previous display values (for dirty-region updates)
int prevRpmInt = -1;
int prevSpeedTenths = -1;
int prevDistHundredths = -1;
int prevTimeSec = -1;
int prevHR = -1;
bool prevHRConnected = false;
long prevTokensK = -1;
int prevCostCents = -1;
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

// ── BLE Heart Rate ───────────────────────────────────────────

// Notification callback — called on NimBLE task thread
void hrNotifyCallback(NimBLERemoteCharacteristic* pChar,
                      uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint16_t hr;
    if (pData[0] & 0x01) {
        hr = (length >= 3) ? (pData[1] | (pData[2] << 8)) : pData[1];
    } else {
        hr = pData[1];
    }

    currentHR = hr;
    Serial.printf("HR: %d bpm\n", hr);
}

class HRClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        hrConnected = true;
        Serial.println("BLE: Connected to HR strap");
    }

    void onDisconnect(NimBLEClient* pClient, int reason) override {
        hrConnected = false;
        currentHR = 0;
        Serial.printf("BLE: HR strap disconnected (reason=%d)\n", reason);
    }
};

static HRClientCallbacks bleCB;

class HRScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        String name = String(advertisedDevice->getName().c_str());
        bool hasHRService = advertisedDevice->isAdvertisingService(hrServiceUUID);

        if (hasHRService || name.startsWith("H808S") || name.startsWith("CooSpo")) {
            Serial.printf("BLE: Found HR device: %s [%s]\n",
                          name.c_str(),
                          advertisedDevice->getAddress().toString().c_str());
            NimBLEDevice::getScan()->stop();
            bleTargetDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
            bleDoConnect = true;
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        if (!bleDoConnect && !hrConnected) {
            Serial.println("BLE: Scan ended, restarting...");
            NimBLEDevice::getScan()->start(10000);
        }
    }
};

static HRScanCallbacks bleScanCB;

bool connectToHR() {
    if (!bleTargetDevice) return false;

    if (!pBLEClient) {
        pBLEClient = NimBLEDevice::createClient();
        pBLEClient->setClientCallbacks(&bleCB);
    }

    Serial.printf("BLE: Connecting to %s...\n", bleTargetDevice->getAddress().toString().c_str());

    if (!pBLEClient->connect(bleTargetDevice)) {
        Serial.println("BLE: Connection failed");
        return false;
    }

    NimBLERemoteService* pService = pBLEClient->getService(hrServiceUUID);
    if (!pService) {
        Serial.println("BLE: HR service not found");
        pBLEClient->disconnect();
        return false;
    }

    NimBLERemoteCharacteristic* pChar = pService->getCharacteristic(hrCharUUID);
    if (!pChar) {
        Serial.println("BLE: HR characteristic not found");
        pBLEClient->disconnect();
        return false;
    }

    if (!pChar->subscribe(true, hrNotifyCallback)) {
        Serial.println("BLE: Failed to subscribe");
        pBLEClient->disconnect();
        return false;
    }

    Serial.println("BLE: Subscribed to HR notifications");
    return true;
}

void initBLE() {
    NimBLEDevice::init("VibeBike");

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&bleScanCB);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);

    Serial.println("BLE: Starting scan for HR devices...");
    pScan->start(10000);
}

void handleBLE() {
    // Handle pending connection from scan callback
    if (bleDoConnect) {
        bleDoConnect = false;
        if (!connectToHR()) {
            delete bleTargetDevice;
            bleTargetDevice = nullptr;
            NimBLEDevice::getScan()->start(10000);
        } else {
            delete bleTargetDevice;
            bleTargetDevice = nullptr;
        }
    }

    // Auto-reconnect if disconnected and not scanning
    if (!hrConnected && !bleDoConnect && !NimBLEDevice::getScan()->isScanning()) {
        NimBLEDevice::getScan()->start(10000);
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

    // Enable token tracking if API key is set
    if (strlen(ANTHROPIC_ADMIN_KEY) > 0) {
        tokenTrackingEnabled = true;
        Serial.println("WiFi: Token tracking enabled");
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

// ── Anthropic Usage API ──────────────────────────────────────

// Get today's date as YYYY-MM-DD string
void getTodayDate(char* buf, size_t len) {
    // Use compile date as fallback; in production, NTP would be better
    // For now, use the API response timestamps
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);

    if (timeinfo.tm_year > 100) {  // Valid time (after 2000)
        strftime(buf, len, "%Y-%m-%d", &timeinfo);
    } else {
        // Fallback: use a broad date range
        strncpy(buf, "2026-02-25", len);
    }
}

bool fetchTokenUsage(unsigned long* inputTokens, unsigned long* outputTokens) {
    if (!wifiConnected || !tokenTrackingEnabled) return false;

    WiFiClientSecure client;
    client.setInsecure();  // Skip cert verification (acceptable for personal project)

    HTTPClient http;

    char startDate[16], endDate[16];
    getTodayDate(startDate, sizeof(startDate));

    // End date = tomorrow (API uses exclusive end)
    // Simple approach: just use same date for both, API returns today's data
    strncpy(endDate, startDate, sizeof(endDate));

    // Build URL
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.anthropic.com/v1/organizations/usage?start_date=%s&end_date=%s",
             startDate, endDate);

    http.begin(client, url);
    http.addHeader("x-api-key", ANTHROPIC_ADMIN_KEY);
    http.addHeader("anthropic-version", "2023-06-01");

    int httpCode = http.GET();

    if (httpCode != 200) {
        Serial.printf("API: HTTP %d\n", httpCode);
        if (httpCode > 0) {
            String body = http.getString();
            Serial.printf("API: %s\n", body.substring(0, 200).c_str());
        }
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    // Parse JSON response
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("API: JSON parse error: %s\n", err.c_str());
        return false;
    }

    // Sum up all input and output tokens from the response
    unsigned long totalInput = 0;
    unsigned long totalOutput = 0;

    // The usage endpoint returns data array
    JsonArray data = doc["data"].as<JsonArray>();
    if (data) {
        for (JsonObject item : data) {
            totalInput += item["input_tokens"].as<unsigned long>();
            totalOutput += item["output_tokens"].as<unsigned long>();
        }
    } else {
        // Single object response
        totalInput = doc["input_tokens"] | 0UL;
        totalOutput = doc["output_tokens"] | 0UL;
    }

    *inputTokens = totalInput;
    *outputTokens = totalOutput;

    Serial.printf("API: Tokens today — input: %lu, output: %lu\n", totalInput, totalOutput);
    return true;
}

void handleTokenTracking(unsigned long now) {
    if (!tokenTrackingEnabled || !wifiConnected) return;

    // Poll at interval
    if (now - lastApiPollTime < API_POLL_INTERVAL_MS && lastApiPollTime > 0) return;
    lastApiPollTime = now;

    unsigned long input, output;
    if (!fetchTokenUsage(&input, &output)) return;

    latestInputTokens = input;
    latestOutputTokens = output;
    tokenDataValid = true;

    // On first fetch during a session, record baseline
    if (!initialTokenFetchDone && sessionState == SESSION_ACTIVE) {
        sessionStartInputTokens = input;
        sessionStartOutputTokens = output;
        initialTokenFetchDone = true;
        Serial.printf("API: Session baseline — input: %lu, output: %lu\n", input, output);
    }

    // Calculate session delta
    if (initialTokenFetchDone) {
        unsigned long deltaInput = latestInputTokens - sessionStartInputTokens;
        unsigned long deltaOutput = latestOutputTokens - sessionStartOutputTokens;
        sessionTokensDelta = deltaInput + deltaOutput;
        sessionCost = (deltaInput * INPUT_COST_PER_M + deltaOutput * OUTPUT_COST_PER_M) / 1000000.0;
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
    if (tokenDataValid && initialTokenFetchDone) {
        f.printf("# tokens_used,%lu\n", sessionTokensDelta);
        f.printf("# estimated_cost,%.2f\n", sessionCost);
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
        if (tokenDataValid && initialTokenFetchDone) {
            sf.printf("tokens_used,count,%lu\n", sessionTokensDelta);
            sf.printf("estimated_cost,usd,%.2f\n", sessionCost);
        }
        sf.close();
        Serial.printf("SD: Summary written to %s\n", summaryFile);
    }

    Serial.printf("SD: Session %d saved. Duration: %lus, Dist: %.2f %s, Avg RPM: %.1f, Avg HR: %.0f\n",
                  sessionNumber, durationSec, dist, distUnit(), avgRpm, avgHR);
}

// ── Display Drawing ──────────────────────────────────────────

void drawStaticUI() {
    tft.fillScreen(BG_COLOR);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TITLE_COLOR, BG_COLOR);
    tft.drawString("VIBE BIKE", 120, TITLE_Y, 2);

    // RPM label
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("RPM", 120, RPM_LABEL_Y, 2);

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
    tft.drawString("HR", RIGHT_COL, HR_LABEL_Y, 2);
    tft.drawString("bpm", RIGHT_COL, HR_UNIT_Y, 2);

    // Vertical divider between time/HR
    tft.drawFastVLine(120, DIV2_Y + 2, DIV3_Y - DIV2_Y - 4, DIVIDER_COLOR);

    // Divider 3
    tft.drawFastHLine(10, DIV3_Y, 220, DIVIDER_COLOR);

    // Tokens / Cost labels
    tft.setTextColor(LABEL_COLOR, BG_COLOR);
    tft.drawString("TOKENS", LEFT_COL, TOKEN_LABEL_Y, 2);
    tft.drawString("COST", RIGHT_COL, COST_LABEL_Y, 2);

    // Vertical divider between tokens/cost
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

    // WiFi indicator
    if (wifiEnabled) {
        tft.setTextColor(wifiConnected ? TFT_GREEN : TFT_RED, BG_COLOR);
        tft.drawString("W", 210, TITLE_Y, 2);
    }

    // BLE indicator
    tft.setTextColor(DIMMED_COLOR, BG_COLOR);
    tft.drawString("B", 196, TITLE_Y, 2);
}

void updateBLEIndicator() {
    tft.setTextDatum(TR_DATUM);
    if (hrConnected) {
        tft.setTextColor(TFT_GREEN, BG_COLOR);
    } else {
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
    }
    tft.drawString("B", 196, TITLE_Y, 2);
}

void updateWiFiIndicator() {
    if (!wifiEnabled) return;
    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(wifiConnected ? TFT_GREEN : TFT_RED, BG_COLOR);
    tft.drawString("W", 210, TITLE_Y, 2);
}

void updateRpmDisplay(float rpm) {
    int rpmInt = (int)(rpm + 0.5);
    if (rpmInt == prevRpmInt && !firstDraw) return;

    // Clear previous value area (font 6)
    int prevWidth = tft.textWidth("000", 6);
    tft.fillRect(120 - prevWidth / 2, RPM_VALUE_Y, prevWidth, tft.fontHeight(6), BG_COLOR);

    prevRpmInt = rpmInt;

    char buf[8];
    sprintf(buf, "%d", rpmInt);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(RPM_COLOR, BG_COLOR);
    tft.drawString(buf, 120, RPM_VALUE_Y, 6);
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

    if (hrInt == prevHR && conn == prevHRConnected && !firstDraw) return;
    prevHR = hrInt;
    prevHRConnected = conn;

    // Clear previous value
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
    // Format tokens with K suffix
    long tokensK = (long)(sessionTokensDelta / 1000);
    int costCents = (int)(sessionCost * 100 + 0.5);

    if (tokensK == prevTokensK && costCents == prevCostCents && !firstDraw) return;
    prevTokensK = tokensK;
    prevCostCents = costCents;

    char buf[12];

    // Clear token value area
    int prevWidth = tft.textWidth("999.9K", 4);
    tft.fillRect(LEFT_COL - prevWidth / 2, TOKEN_VALUE_Y, prevWidth, tft.fontHeight(4), BG_COLOR);

    if (!tokenTrackingEnabled || !wifiConnected) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("--", LEFT_COL, TOKEN_VALUE_Y, 4);
    } else if (!tokenDataValid) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("...", LEFT_COL, TOKEN_VALUE_Y, 4);
    } else {
        if (sessionTokensDelta >= 1000000) {
            sprintf(buf, "%.1fM", sessionTokensDelta / 1000000.0);
        } else if (sessionTokensDelta >= 1000) {
            sprintf(buf, "%.1fK", sessionTokensDelta / 1000.0);
        } else {
            sprintf(buf, "%lu", sessionTokensDelta);
        }
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TOKEN_COLOR, BG_COLOR);
        tft.drawString(buf, LEFT_COL, TOKEN_VALUE_Y, 4);
    }

    // Clear cost value area
    prevWidth = tft.textWidth("$99.99", 4);
    tft.fillRect(RIGHT_COL - prevWidth / 2, COST_VALUE_Y, prevWidth, tft.fontHeight(4), BG_COLOR);

    if (!tokenTrackingEnabled || !wifiConnected) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("--", RIGHT_COL, COST_VALUE_Y, 4);
    } else if (!tokenDataValid) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(DIMMED_COLOR, BG_COLOR);
        tft.drawString("...", RIGHT_COL, COST_VALUE_Y, 4);
    } else {
        sprintf(buf, "$%.2f", sessionCost);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(COST_COLOR, BG_COLOR);
        tft.drawString(buf, RIGHT_COL, COST_VALUE_Y, 4);
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
                sessionCost = 0;
                initialTokenFetchDone = false;
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

    Serial.println("Vibe Bike Dashboard v2.0 — Ready (Phase 2+3)");
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

    // Handle token tracking (API polling)
    handleTokenTracking(now);

    // Raw data logging to SD
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
        updateHRDisplay(hr);
        updateTokenDisplay();
        updateStatusBar(sessionState, displayPulseCount);
        updateBLEIndicator();
        updateWiFiIndicator();
    }
}
