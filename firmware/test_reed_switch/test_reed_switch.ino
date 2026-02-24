// Vibe Bike — Reed Switch Hardware Test
// Flash this to verify IO35 reads clean pulses from the bike sensor.
//
// Wiring:
//   3.3V ─── 10K resistor ─── IO35 ─── bike wire 1
//   GND  ────────────────────────────── bike wire 2
//
// Expected: IO35 is HIGH at rest, drops to LOW when magnet passes reed switch.
// Serial monitor (115200 baud) prints each pulse with timestamp and interval.
//
// Compile & upload:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/test_reed_switch
//   arduino-cli upload -b esp32:esp32:esp32 -p /dev/cu.usbmodem2101 firmware/test_reed_switch
//   arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

#define SENSOR_PIN 35
#define DEBOUNCE_MS 50

volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile bool newPulse = false;

void IRAM_ATTR onPulse() {
    unsigned long now = millis();
    if (now - lastPulseTime > DEBOUNCE_MS) {
        pulseInterval = now - lastPulseTime;
        lastPulseTime = now;
        newPulse = true;
    }
}

unsigned long pulseCount = 0;

void setup() {
    Serial.begin(115200);
    pinMode(SENSOR_PIN, INPUT);  // 10K external pull-up to 3.3V
    attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), onPulse, FALLING);

    Serial.println("=== Vibe Bike Reed Switch Test ===");
    Serial.println("Waiting for pulses on IO35...");
    Serial.println("Pedal the bike — you should see one pulse per revolution.");
    Serial.println();
}

void loop() {
    if (newPulse) {
        newPulse = false;
        pulseCount++;

        float rpm = (pulseInterval > 0) ? 60000.0 / pulseInterval : 0;

        Serial.printf("Pulse #%lu | interval: %lu ms | RPM: %.1f\n",
                       pulseCount, pulseInterval, rpm);
    }

    // Also print raw pin state every 500ms for debugging
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 500) {
        lastDebug = millis();
        Serial.printf("  [debug] IO35 = %s | total pulses: %lu\n",
                       digitalRead(SENSOR_PIN) ? "HIGH" : "LOW", pulseCount);
    }
}
