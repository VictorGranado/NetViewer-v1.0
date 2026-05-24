#include <Wire.h>
#include <U8g2lib.h>

// ESP32 I2C pins
#define OLED_SDA 21
#define OLED_SCL 22

// Portrait screen size after U8G2_R1 rotation
#define SCREEN_W 64
#define SCREEN_H 128

// SSD1309 128x64 I2C OLED in portrait orientation
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
  U8G2_R1,
  U8X8_PIN_NONE
);

void drawSignalBar(int x, int y, int percent) {
  percent = constrain(percent, 0, 100);

  int barW = 58;
  int barH = 8;

  u8g2.drawFrame(x, y, barW, barH);

  int fillWidth = map(percent, 0, 100, 0, barW - 2);
  u8g2.drawBox(x + 1, y + 1, fillWidth, barH - 2);
}

void drawWiFiIcon(int x, int y) {
  u8g2.drawDisc(x, y, 2);

  // Small arc
  u8g2.drawCircle(x, y, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);

  // Medium arc
  u8g2.drawCircle(x, y, 13, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);

  // Large arc
  u8g2.drawCircle(x, y, 19, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
}

void drawHeader(const char* title) {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 7, title);
  u8g2.drawHLine(0, 10, SCREEN_W);
}

void drawFooter(const char* text) {
  u8g2.drawHLine(0, 116, SCREEN_W);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 126, text);
}

void splashScreen() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(1, 16, "Pocket");

  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(12, 32, "Net");

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(7, 48, "ESP32 Tool");

  drawWiFiIcon(32, 88);

  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(8, 122, "WiFi BLE Web");

  u8g2.sendBuffer();
  delay(2000);
}

void drawMainMenu() {
  u8g2.clearBuffer();

  drawHeader("PocketNet");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(2, 24, "> Wi-Fi");
  u8g2.drawStr(2, 38, "  BLE");
  u8g2.drawStr(2, 52, "  Web");
  u8g2.drawStr(2, 66, "  Device");

  u8g2.drawFrame(0, 78, 64, 26);
  u8g2.drawStr(5, 89, "UP/DOWN");
  u8g2.drawStr(5, 100, "MENU=SEL");

  drawFooter("Mock Menu");

  u8g2.sendBuffer();
}

void drawWiFiMockScreen() {
  u8g2.clearBuffer();

  drawHeader("Wi-Fi Scan");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 23, "SSID:");
  u8g2.drawStr(0, 33, "Home_Net");

  u8g2.drawStr(0, 48, "RSSI:");
  u8g2.drawStr(34, 48, "-52");

  u8g2.drawStr(0, 60, "CH:");
  u8g2.drawStr(24, 60, "6");

  u8g2.drawStr(0, 72, "SEC:");
  u8g2.drawStr(28, 72, "WPA2");

  u8g2.drawStr(0, 88, "Quality:");
  drawSignalBar(0, 96, 76);

  drawFooter("UP/DN MENU");

  u8g2.sendBuffer();
}

void drawBLEMockScreen() {
  u8g2.clearBuffer();

  drawHeader("BLE Scan");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 23, "Device:");
  u8g2.drawStr(0, 33, "BLE_Tag");

  u8g2.drawStr(0, 48, "RSSI:");
  u8g2.drawStr(34, 48, "-66");

  u8g2.drawStr(0, 63, "UUID:");
  u8g2.drawStr(0, 73, "180F");

  u8g2.drawStr(0, 88, "Signal:");
  drawSignalBar(0, 96, 55);

  drawFooter("UP/DN MENU");

  u8g2.sendBuffer();
}

void drawWebMockScreen() {
  u8g2.clearBuffer();

  drawHeader("Web Server");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 24, "AP Active");

  u8g2.drawStr(0, 42, "SSID:");
  u8g2.drawStr(0, 52, "PocketNet");

  u8g2.drawStr(0, 70, "Open:");
  u8g2.drawStr(0, 80, "192.168.");
  u8g2.drawStr(0, 90, "4.1");

  drawFooter("Dashboard");

  u8g2.sendBuffer();
}

void setup() {
  Wire.begin(OLED_SDA, OLED_SCL);

  u8g2.begin();
  u8g2.setContrast(255);

  splashScreen();
}

void loop() {
  drawMainMenu();
  delay(2000);

  drawWiFiMockScreen();
  delay(2000);

  drawBLEMockScreen();
  delay(2000);

  drawWebMockScreen();
  delay(2000);
}