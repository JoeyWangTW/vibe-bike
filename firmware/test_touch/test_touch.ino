// test_touch.ino — Raw XPT2046 diagnostic for ESP32-32E board
// Bypasses library to read raw ADC values and test pin mapping
//
// Touch pins (from board schematic): MOSI=32, MISO=39, CLK=25, CS=33, IRQ=36
//
// Build:
//   arduino-cli compile -b esp32:esp32:esp32 firmware/test_touch
//   arduino-cli upload -b esp32:esp32:esp32:UploadSpeed=460800 -p /dev/cu.usbserial-1120 firmware/test_touch
//   arduino-cli monitor -p /dev/cu.usbserial-1120 -c baudrate=115200

#include <TFT_eSPI.h>

// Touch pins — try multiple mappings to find the right one
#define T_MOSI 32
#define T_MISO 39
#define T_CLK  25
#define T_CS   33
#define T_IRQ  36

// XPT2046 commands
#define CMD_READ_X 0xD0
#define CMD_READ_Y 0x90
#define CMD_READ_Z1 0xB0
#define CMD_READ_Z2 0xC0

TFT_eSPI tft = TFT_eSPI();

int rawSpiRead(uint8_t mosiPin, uint8_t misoPin, uint8_t clkPin, byte command) {
    int result = 0;
    // Send 8-bit command
    for (int i = 7; i >= 0; i--) {
        digitalWrite(mosiPin, (command >> i) & 1);
        digitalWrite(clkPin, HIGH);
        delayMicroseconds(10);
        digitalWrite(clkPin, LOW);
        delayMicroseconds(10);
    }
    // Read 12-bit result
    for (int i = 11; i >= 0; i--) {
        digitalWrite(clkPin, HIGH);
        delayMicroseconds(10);
        result |= (digitalRead(misoPin) << i);
        digitalWrite(clkPin, LOW);
        delayMicroseconds(10);
    }
    return result;
}

void readRawTouch(uint8_t mosiPin, uint8_t misoPin, uint8_t clkPin, uint8_t csPin,
                  int* rawX, int* rawY, int* rawZ1, int* rawZ2) {
    digitalWrite(csPin, LOW);
    *rawX = rawSpiRead(mosiPin, misoPin, clkPin, CMD_READ_X);
    *rawY = rawSpiRead(mosiPin, misoPin, clkPin, CMD_READ_Y);
    *rawZ1 = rawSpiRead(mosiPin, misoPin, clkPin, CMD_READ_Z1);
    *rawZ2 = rawSpiRead(mosiPin, misoPin, clkPin, CMD_READ_Z2);
    digitalWrite(csPin, HIGH);
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n\n=== XPT2046 RAW DIAGNOSTIC ===");
    Serial.printf("Pins: MOSI=%d MISO=%d CLK=%d CS=%d IRQ=%d\n",
                  T_MOSI, T_MISO, T_CLK, T_CS, T_IRQ);

    // Backlight
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    // Display
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // Set up touch pins manually
    pinMode(T_MOSI, OUTPUT);
    pinMode(T_MISO, INPUT);
    pinMode(T_CLK, OUTPUT);
    pinMode(T_CS, OUTPUT);
    pinMode(T_IRQ, INPUT);
    digitalWrite(T_CS, HIGH);
    digitalWrite(T_CLK, LOW);

    // Draw UI
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("TOUCH DIAGNOSTIC", 120, 5, 2);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Touch screen & watch Serial", 120, 25, 2);
    tft.drawString("Pin mapping test", 120, 45, 2);

    Serial.println("Ready — touch the screen and watch values");
    Serial.println("If all zeros, pin mapping is wrong\n");
}

int loopCount = 0;

void loop() {
    int irqState = digitalRead(T_IRQ);

    // Read raw touch values regardless of IRQ (for diagnosis)
    int rawX, rawY, rawZ1, rawZ2;
    readRawTouch(T_MOSI, T_MISO, T_CLK, T_CS, &rawX, &rawY, &rawZ1, &rawZ2);

    // Only print every 500ms to avoid flooding
    if (loopCount % 5 == 0) {
        Serial.printf("IRQ=%s  rawX=%4d  rawY=%4d  Z1=%4d  Z2=%4d",
                      irqState == LOW ? "TOUCH" : "-----",
                      rawX, rawY, rawZ1, rawZ2);

        if (rawX == 0 && rawY == 0 && rawZ1 == 0 && rawZ2 == 0) {
            Serial.print("  << ALL ZERO - no SPI response");
        }
        Serial.println();

        // Update display
        tft.fillRect(0, 80, 240, 200, TFT_BLACK);

        char buf[40];
        tft.setTextDatum(TL_DATUM);

        sprintf(buf, "IRQ: %s", irqState == LOW ? "TOUCHED" : "not touched");
        tft.setTextColor(irqState == LOW ? TFT_GREEN : TFT_RED, TFT_BLACK);
        tft.drawString(buf, 10, 90, 2);

        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        sprintf(buf, "Raw X: %d", rawX);
        tft.drawString(buf, 10, 120, 4);
        sprintf(buf, "Raw Y: %d", rawY);
        tft.drawString(buf, 10, 155, 4);
        sprintf(buf, "Z1: %d  Z2: %d", rawZ1, rawZ2);
        tft.drawString(buf, 10, 190, 2);

        if (rawX == 0 && rawY == 0) {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.drawString("NO SPI RESPONSE", 10, 220, 2);
            tft.drawString("Check pin mapping!", 10, 240, 2);
        }
    }

    loopCount++;
    delay(100);
}
