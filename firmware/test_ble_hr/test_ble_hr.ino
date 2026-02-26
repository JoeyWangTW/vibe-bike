// test_ble_hr.ino — BLE Heart Rate Monitor Test
// Standalone sketch to validate BLE connection to Coospo H808S chest strap.
//
// Scans for devices advertising HR Service (0x180D) or name "H808S",
// connects, subscribes to HR Measurement (0x2A37), prints BPM to serial.
// Auto-reconnects on disconnect.
//
// Board: ESP32-32E
// Build:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/test_ble_hr
//   arduino-cli upload -b esp32:esp32:esp32 -p /dev/cu.usbmodem2101 firmware/test_ble_hr
//   arduino-cli monitor -p /dev/cu.usbmodem2101 -c baudrate=115200

#include <NimBLEDevice.h>

// ── BLE UUIDs ──────────────────────────────────────────────────
static NimBLEUUID hrServiceUUID("180D");
static NimBLEUUID hrCharUUID("2A37");

// ── State ──────────────────────────────────────────────────────
static NimBLEClient* pClient = nullptr;
static bool doConnect = false;
static bool connected = false;
static NimBLEAdvertisedDevice* targetDevice = nullptr;

// ── HR Notification Callback ───────────────────────────────────
// Called on NimBLE task thread when HR data arrives.
// HR Measurement format (Bluetooth spec):
//   Byte 0: Flags — bit 0: 0=HR is uint8 (byte 1), 1=HR is uint16 (bytes 1-2)
//   Byte 1 (or 1-2): Heart rate value
void hrNotifyCallback(NimBLERemoteCharacteristic* pChar,
                      uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint16_t hr;
    if (pData[0] & 0x01) {
        // 16-bit HR
        hr = (length >= 3) ? (pData[1] | (pData[2] << 8)) : pData[1];
    } else {
        // 8-bit HR
        hr = pData[1];
    }

    Serial.printf("HR: %d bpm\n", hr);
}

// ── Client Callbacks ───────────────────────────────────────────
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        connected = true;
        Serial.println("BLE: Connected to HR strap");
    }

    void onDisconnect(NimBLEClient* pClient, int reason) override {
        connected = false;
        Serial.printf("BLE: Disconnected (reason=%d). Will re-scan...\n", reason);
    }
};

static ClientCallbacks clientCB;

// ── Scan Callbacks ─────────────────────────────────────────────
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        String name = String(advertisedDevice->getName().c_str());
        bool hasHRService = advertisedDevice->isAdvertisingService(hrServiceUUID);

        if (hasHRService || name.startsWith("H808S") || name.startsWith("CooSpo")) {
            Serial.printf("BLE: Found HR device: %s [%s]\n",
                          name.c_str(),
                          advertisedDevice->getAddress().toString().c_str());

            // Stop scan and flag for connection
            NimBLEDevice::getScan()->stop();
            targetDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
            doConnect = true;
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        if (!doConnect && !connected) {
            Serial.println("BLE: Scan ended, no HR device found. Restarting scan...");
            NimBLEDevice::getScan()->start(10000);
        }
    }
};

static ScanCallbacks scanCB;

// ── Connect to HR Strap ────────────────────────────────────────
bool connectToHR() {
    if (!targetDevice) return false;

    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&clientCB);
    }

    Serial.printf("BLE: Connecting to %s...\n", targetDevice->getAddress().toString().c_str());

    if (!pClient->connect(targetDevice)) {
        Serial.println("BLE: Connection failed");
        return false;
    }

    // Get HR service
    NimBLERemoteService* pService = pClient->getService(hrServiceUUID);
    if (!pService) {
        Serial.println("BLE: HR service not found");
        pClient->disconnect();
        return false;
    }

    // Get HR measurement characteristic
    NimBLERemoteCharacteristic* pChar = pService->getCharacteristic(hrCharUUID);
    if (!pChar) {
        Serial.println("BLE: HR characteristic not found");
        pClient->disconnect();
        return false;
    }

    // Subscribe to notifications
    if (!pChar->subscribe(true, hrNotifyCallback)) {
        Serial.println("BLE: Failed to subscribe to HR notifications");
        pClient->disconnect();
        return false;
    }

    Serial.println("BLE: Subscribed to HR notifications. Waiting for data...");
    return true;
}

// ── Setup ──────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n=== BLE Heart Rate Test ===");

    NimBLEDevice::init("VibeBike");

    // Configure scan
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(&scanCB);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);

    Serial.println("BLE: Starting scan for HR devices...");
    pScan->start(10000);
}

// ── Loop ───────────────────────────────────────────────────────
void loop() {
    // Handle connection request from scan callback
    if (doConnect) {
        doConnect = false;
        if (!connectToHR()) {
            // Failed — clean up and restart scan
            delete targetDevice;
            targetDevice = nullptr;
            Serial.println("BLE: Restarting scan after failed connection...");
            NimBLEDevice::getScan()->start(10000);
        }
        delete targetDevice;
        targetDevice = nullptr;
    }

    // Auto-reconnect: if disconnected and not scanning, restart scan
    if (!connected && !doConnect && !NimBLEDevice::getScan()->isScanning()) {
        delay(2000);  // Wait before re-scanning
        Serial.println("BLE: Restarting scan...");
        NimBLEDevice::getScan()->start(10000);
    }

    delay(100);
}
