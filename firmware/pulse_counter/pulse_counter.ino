// Vibe Bike — Pulse Counter + Speed/Distance Firmware (VB-004, VB-005)
// Interrupt-driven cadence, speed, and distance from bike reed switch.
//
// Wiring:
//   3.3V ─── 10K resistor ─── IO35 ─── bike wire 1
//   GND  ────────────────────────────── bike wire 2
//
// Reed switch: HIGH at rest, LOW pulse on magnet pass (1 pulse/revolution).
//
// Compile & upload:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/pulse_counter
//   arduino-cli upload -b esp32:esp32:esp32 -p /dev/cu.usbmodem2101 firmware/pulse_counter
//   arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

// ── Configuration ──────────────────────────────────────────────
#define SENSOR_PIN        35      // IO35 (input-only), external 10K pull-up
#define DEBOUNCE_MS       50      // Ignore pulses within this window (reed bounce)
#define RPM_TIMEOUT_MS    3000    // No pulse for this long → RPM = 0 (stopped)
#define MIN_RPM           10.0    // Below this, consider noise / stopped
#define MAX_RPM           200.0   // Above this, likely bounce artifact
#define SMOOTHING_SAMPLES 4       // Rolling average window for RPM
#define SERIAL_UPDATE_MS  500     // How often to print RPM to serial

// ── Speed & Distance Configuration ─────────────────────────────
// Indoor bike: 1 reed switch pulse = 1 pedal revolution.
// DISTANCE_PER_REV estimates the road-equivalent distance per pedal revolution.
// Typical indoor bike with ~3:1 flywheel ratio and 700c equivalent:
//   1 pedal rev × 3 wheel revs × 2.1m circumference ≈ 6.3m
// Adjust this value to calibrate your specific bike.
#define DISTANCE_PER_REV_M  6.3   // Meters of road-equivalent distance per revolution
#define USE_METRIC          true  // true = km/h + km, false = mph + miles
#define KM_TO_MILES         0.621371

// ── ISR Variables (volatile, accessed from interrupt) ──────────
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile unsigned long pulseCount = 0;
volatile bool newPulse = false;

// ── ISR ────────────────────────────────────────────────────────
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

// ── RPM Smoothing Buffer ──────────────────────────────────────
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
    for (int i = 0; i < count; i++) {
        sum += rpmBuffer[i];
    }
    return sum / count;
}

void clearRpmBuffer() {
    for (int i = 0; i < SMOOTHING_SAMPLES; i++) rpmBuffer[i] = 0;
    rpmBufferIndex = 0;
    rpmBufferFull = false;
}

// ── State ─────────────────────────────────────────────────────
float currentRpm = 0;
float smoothedRpm = 0;
unsigned long lastSerialUpdate = 0;
unsigned long displayPulseCount = 0;

// ── Speed & Distance State ───────────────────────────────────
float currentSpeed = 0;          // Current speed in display units (km/h or mph)
float totalDistanceM = 0;        // Accumulated distance in meters
unsigned long lastDistancePulse = 0;  // Last pulse count used for distance calc

// ── Speed & Distance Helpers ─────────────────────────────────
// Convert RPM to speed in display units
float rpmToSpeed(float rpm) {
    // speed = RPM × distance_per_rev × 60 min/hr ÷ 1000 m/km
    float speedKmh = rpm * DISTANCE_PER_REV_M * 60.0 / 1000.0;
    return USE_METRIC ? speedKmh : speedKmh * KM_TO_MILES;
}

// Get total distance in display units
float getDisplayDistance() {
    float distKm = totalDistanceM / 1000.0;
    return USE_METRIC ? distKm : distKm * KM_TO_MILES;
}

const char* speedUnit() { return USE_METRIC ? "km/h" : "mph"; }
const char* distUnit()  { return USE_METRIC ? "km"   : "mi"; }

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(SENSOR_PIN, INPUT);  // External 10K pull-up to 3.3V
    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), onPulse, FALLING);

    clearRpmBuffer();

    Serial.println();
    Serial.println("╔══════════════════════════════════════╗");
    Serial.println("║   Vibe Bike — Pulse + Speed/Distance ║");
    Serial.println("║   Firmware v1.1 (VB-004 + VB-005)   ║");
    Serial.println("╠══════════════════════════════════════╣");
    Serial.printf( "║ Sensor: IO35 (FALLING interrupt)     ║\n");
    Serial.printf( "║ Distance/rev: %.1f m                  ║\n", DISTANCE_PER_REV_M);
    Serial.printf( "║ Units: %s, %s                    ║\n", speedUnit(), distUnit());
    Serial.println("║ Debounce: 50ms | Timeout: 3s         ║");
    Serial.println("╚══════════════════════════════════════╝");
    Serial.println();
    Serial.println("Waiting for pedaling...");
    Serial.println();
}

// ── Main Loop ─────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // Process new pulse from ISR
    if (newPulse) {
        newPulse = false;

        // Read ISR variables atomically
        noInterrupts();
        unsigned long interval = pulseInterval;
        unsigned long count = pulseCount;
        interrupts();

        displayPulseCount = count;

        // Calculate instantaneous RPM from pulse interval
        if (interval > 0) {
            currentRpm = 60000.0 / interval;
        }

        // Clamp: reject obviously bad values
        if (currentRpm < MIN_RPM) {
            currentRpm = 0;
        } else if (currentRpm > MAX_RPM) {
            // Likely bounce — don't update smoothing buffer
            return;
        }

        // Feed into smoothing buffer
        if (currentRpm > 0) {
            addRpmSample(currentRpm);
        }

        // Accumulate distance: count new pulses since last distance update
        unsigned long newPulses = count - lastDistancePulse;
        if (newPulses > 0 && currentRpm >= MIN_RPM) {
            totalDistanceM += newPulses * DISTANCE_PER_REV_M;
            lastDistancePulse = count;
        }
    }

    // Timeout detection: no pulse for RPM_TIMEOUT_MS → stopped
    noInterrupts();
    unsigned long timeSinceLastPulse = now - lastPulseTime;
    interrupts();

    if (lastPulseTime > 0 && timeSinceLastPulse > RPM_TIMEOUT_MS) {
        currentRpm = 0;
        clearRpmBuffer();
    }

    // Calculate smoothed RPM and speed
    smoothedRpm = (currentRpm > 0) ? getSmoothedRpm() : 0;
    currentSpeed = rpmToSpeed(smoothedRpm);

    // Serial output at fixed interval
    if (now - lastSerialUpdate >= SERIAL_UPDATE_MS) {
        lastSerialUpdate = now;

        float dist = getDisplayDistance();

        if (smoothedRpm > 0) {
            Serial.printf("RPM: %5.1f | Speed: %5.1f %s | Dist: %6.2f %s | Pulses: %lu\n",
                          smoothedRpm, currentSpeed, speedUnit(),
                          dist, distUnit(), displayPulseCount);
        } else if (displayPulseCount > 0) {
            Serial.printf("RPM:   0.0 (stopped) | Speed:   0.0 %s | Dist: %6.2f %s | Pulses: %lu\n",
                          speedUnit(), dist, distUnit(), displayPulseCount);
        }
    }
}
