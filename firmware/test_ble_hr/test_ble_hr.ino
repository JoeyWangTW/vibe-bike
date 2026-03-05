// test_ble_hr.ino — BLE Heart Rate Monitor Test (built-in ESP32 BLE)
// Scans for HR strap "808S 0023713", connects, subscribes to HR notifications.
//
// Board: ESP32-32E
// Build:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/test_ble_hr
//   arduino-cli upload -b esp32:esp32:esp32:UploadSpeed=460800 -p /dev/cu.usbserial-1120 firmware/test_ble_hr
//   arduino-cli monitor -p /dev/cu.usbserial-1120 -c baudrate=115200

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>

// ── BLE UUIDs ──────────────────────────────────────────────────
static BLEUUID hrServiceUUID("180D");
static BLEUUID hrCharUUID("2A37");

// ── State ──────────────────────────────────────────────────────
static BLEClient* pClient = nullptr;
static BLEAdvertisedDevice* targetDevice = nullptr;
static bool doConnect = false;
static bool connected = false;
static int scanRound = 0;

// ── HR Notification Callback ───────────────────────────────────
static void hrNotifyCallback(BLERemoteCharacteristic* pChar,
                             uint8_t* pData, size_t length, bool isNotify) {
    if (length < 2) return;

    uint16_t hr;
    if (pData[0] & 0x01) {
        hr = (length >= 3) ? (pData[1] | (pData[2] << 8)) : pData[1];
    } else {
        hr = pData[1];
    }

    Serial.printf("HR: %d bpm\n", hr);
}

// ── Client Callbacks ───────────────────────────────────────────
class ClientCB : public BLEClientCallbacks {
    void onConnect(BLEClient* pClient) override {
        connected = true;
        Serial.println("BLE: Connected!");
    }
    void onDisconnect(BLEClient* pClient) override {
        connected = false;
        Serial.println("BLE: Disconnected");
    }
};

// ── Scan Callbacks ─────────────────────────────────────────────
class ScanCB : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        String name = String(advertisedDevice.getName().c_str());
        bool hasHR = advertisedDevice.haveServiceUUID() &&
                     advertisedDevice.isAdvertisingService(hrServiceUUID);

        Serial.printf("  Found: \"%s\" [%s] RSSI:%d HR:%s\n",
                      name.c_str(),
                      advertisedDevice.getAddress().toString().c_str(),
                      advertisedDevice.getRSSI(),
                      hasHR ? "YES" : "no");

        // Match by HR service or name containing "808"
        if (hasHR || name.indexOf("808") >= 0) {
            Serial.println("  >>> MATCH!");
            advertisedDevice.getScan()->stop();
            targetDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
        }
    }
};

// ── Connect and subscribe ──────────────────────────────────────
bool connectToHR() {
    if (!targetDevice) return false;

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCB());

    Serial.printf("BLE: Connecting to %s...\n",
                  targetDevice->getAddress().toString().c_str());

    if (!pClient->connect(targetDevice)) {
        Serial.println("BLE: Connection FAILED");
        return false;
    }

    Serial.println("BLE: Connected, discovering services...");

    BLERemoteService* pSvc = pClient->getService(hrServiceUUID);
    if (!pSvc) {
        Serial.println("BLE: HR service (0x180D) not found");
        pClient->disconnect();
        return false;
    }

    BLERemoteCharacteristic* pChr = pSvc->getCharacteristic(hrCharUUID);
    if (!pChr) {
        Serial.println("BLE: HR characteristic (0x2A37) not found");
        pClient->disconnect();
        return false;
    }

    if (pChr->canNotify()) {
        pChr->registerForNotify(hrNotifyCallback);
        Serial.println("BLE: Subscribed to HR notifications!");
    } else {
        Serial.println("BLE: Characteristic doesn't support notify");
        pClient->disconnect();
        return false;
    }

    return true;
}

// ── Setup ──────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== BLE Heart Rate Test (built-in BLE) ===");
    Serial.println("Scanning for HR strap...\n");

    BLEDevice::init("VibeBike");
}

// ── Loop ───────────────────────────────────────────────────────
void loop() {
    if (connected) {
        delay(1000);
        return;
    }

    if (doConnect) {
        doConnect = false;
        if (connectToHR()) {
            Serial.println("\n==> Streaming HR data:");
            return;
        }
        Serial.println("BLE: Will retry scan...");
        delete targetDevice;
        targetDevice = nullptr;
    }

    scanRound++;
    Serial.printf("\n--- Scan #%d (10 sec) ---\n", scanRound);

    BLEScan* pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new ScanCB());
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    pScan->start(10, false);
    pScan->clearResults();

    delay(2000);
}
