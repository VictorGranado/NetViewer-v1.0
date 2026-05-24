#include <Wire.h>
#include <U8g2lib.h>

// OLED pins
#define OLED_SDA 21
#define OLED_SCL 22

// Button pins
#define BTN_UP_PIN    25
#define BTN_DOWN_PIN  26
#define BTN_MENU_PIN  27

// Portrait SSD1309 OLED
#define SCREEN_W 64
#define SCREEN_H 128

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
  U8G2_R1,
  U8X8_PIN_NONE
);

void drawButtonBox(int y, const char* label, bool pressed) {
  if (pressed) {
    u8g2.drawBox(0, y - 9, SCREEN_W, 13);
    u8g2.setDrawColor(0);
    u8g2.drawStr(4, y, label);
    u8g2.drawStr(42, y, "ON");
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawFrame(0, y - 9, SCREEN_W, 13);
    u8g2.drawStr(4, y, label);
    u8g2.drawStr(38, y, "OFF");
  }
}

void drawScreen() {
  bool upPressed = digitalRead(BTN_UP_PIN) == LOW;
  bool downPressed = digitalRead(BTN_DOWN_PIN) == LOW;
  bool menuPressed = digitalRead(BTN_MENU_PIN) == LOW;

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 7, "Button Test");
  u8g2.drawHLine(0, 10, SCREEN_W);

  drawButtonBox(30, "UP", upPressed);
  drawButtonBox(52, "DOWN", downPressed);
  drawButtonBox(74, "MENU", menuPressed);

  u8g2.drawHLine(0, 94, SCREEN_W);

  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 106, "Pressed = ON");
  u8g2.drawStr(0, 118, "Released= OFF");

  u8g2.sendBuffer();
}

void setup() {
  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_MENU_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);

  u8g2.begin();
  u8g2.setContrast(255);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 32, "PocketNet");
  u8g2.drawStr(0, 46, "Button Test");
  u8g2.drawStr(0, 66, "Starting...");
  u8g2.sendBuffer();

  delay(1000);
}

void loop() {
  drawScreen();
  delay(80);
}