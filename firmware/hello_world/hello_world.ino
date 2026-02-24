// hello_world.ino — VB-006: Display Hello World on ESP32-32E 2.8" ILI9341V
// Verifies TFT_eSPI config and display wiring are correct.
// Board: ESP32-32E with ILI9341V (240x320)
// Build: arduino-cli compile -b esp32:esp32:esp32 firmware/hello_world

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// Backlight pin
#define TFT_BL_PIN 21

void setup() {
  Serial.begin(115200);
  Serial.println("VB-006: Hello World Display Test");

  // Turn on backlight
  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH);

  // Initialize display
  tft.init();
  tft.setRotation(0);  // Portrait: 240w x 320h
  tft.fillScreen(TFT_BLACK);

  // Title
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);  // Top center
  tft.drawString("VIBE BIKE", 120, 20, 4);

  // Divider line
  tft.drawLine(20, 55, 220, 55, TFT_DARKGREY);

  // Hello World message
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Hello World!", 120, 80, 4);

  // Board info
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);  // Top left
  tft.drawString("Board: ESP32-32E", 20, 130, 2);
  tft.drawString("Display: ILI9341V", 20, 150, 2);
  tft.drawString("Size: 240x320", 20, 170, 2);
  tft.drawString("SPI: HSPI @ 55MHz", 20, 190, 2);

  // Pin mapping
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("Pin Map:", 20, 220, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("CS:15 DC:2 CLK:14", 20, 240, 2);
  tft.drawString("MOSI:13 MISO:12", 20, 260, 2);
  tft.drawString("BL:21 RST:EN", 20, 280, 2);

  // Status
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("Display OK", 120, 305, 2);

  Serial.println("Display initialized. If you see text, TFT_eSPI is configured correctly.");
}

void loop() {
  // Nothing to do — static display test
  delay(1000);
}
