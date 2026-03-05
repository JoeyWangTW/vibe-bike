// Minimal BLE scan test — uses ESP32 built-in BLE (NOT NimBLE)
// Tests whether the BLE radio hardware works at all.

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

BLEScan* pBLEScan;
int scanRound = 0;

class MyCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.printf("  Device: \"%s\" [%s] RSSI:%d\n",
                      advertisedDevice.getName().c_str(),
                      advertisedDevice.getAddress().toString().c_str(),
                      advertisedDevice.getRSSI());
    }
};

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Minimal BLE Scan Test (built-in library) ===\n");

    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
}

void loop() {
    scanRound++;
    Serial.printf("--- Scan #%d (10 sec) ---\n", scanRound);

    BLEScanResults* results = pBLEScan->start(10, false);
    Serial.printf("Total found: %d\n\n", results->getCount());
    pBLEScan->clearResults();

    delay(2000);
}
