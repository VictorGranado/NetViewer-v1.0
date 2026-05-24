/*
  NetViewer / PocketNet - Device / Utility Real Test Firmware
  -----------------------------------------------------------
  Hardware:
    - ESP32-WROOM-DA
    - 2.4" SSD1309 I2C OLED in portrait mode
    - 3 buttons:
        UP    -> GPIO32 and GND
        DOWN  -> GPIO26 and GND
        MENU  -> GPIO27 and GND

  OLED:
    SDA -> GPIO21
    SCL -> GPIO22

  Locked Device / Utility Modes:
    1. Device Status Mode
    2. Settings Mode

  No buzzer is used in this firmware.

  Required Libraries:
    - U8g2

  Controls:
    Startup:
      MENU = start

    Main Menu:
      UP   = previous mode
      DOWN = next mode
      MENU = enter mode

    Device Status:
      UP/DOWN = switch status page
      MENU    = back

    Settings:
      UP   = previous setting
      DOWN = change selected setting
      MENU = back

  Settings included:
    - OLED brightness
    - Auto refresh
    - Display invert
    - Sleep timeout placeholder
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// --------------------------------------------------
// Pin Mapping
// --------------------------------------------------
#define OLED_SDA 21
#define OLED_SCL 22

#define BTN_UP_PIN    32
#define BTN_DOWN_PIN  26
#define BTN_MENU_PIN  27

// --------------------------------------------------
// OLED setup: SSD1309 portrait
// --------------------------------------------------
#define SCREEN_W 64
#define SCREEN_H 128

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(
  U8G2_R1,
  U8X8_PIN_NONE
);

// --------------------------------------------------
// Button handling
// --------------------------------------------------
bool upWasDown = false;
bool downWasDown = false;
bool menuWasDown = false;

unsigned long lastButtonAction = 0;
const unsigned long BUTTON_GAP_MS = 170;

bool rawUp() {
  return digitalRead(BTN_UP_PIN) == LOW;
}

bool rawDown() {
  return digitalRead(BTN_DOWN_PIN) == LOW;
}

bool rawMenu() {
  return digitalRead(BTN_MENU_PIN) == LOW;
}

bool buttonEdge(bool nowDown, bool &wasDown) {
  if (nowDown && !wasDown && millis() - lastButtonAction > BUTTON_GAP_MS) {
    wasDown = true;
    lastButtonAction = millis();
    return true;
  }

  if (!nowDown) {
    wasDown = false;
  }

  return false;
}

bool upPressed() {
  return buttonEdge(rawUp(), upWasDown);
}

bool downPressed() {
  return buttonEdge(rawDown(), downWasDown);
}

bool menuPressed() {
  return buttonEdge(rawMenu(), menuWasDown);
}

void waitForButtonsReleased() {
  while (rawUp() || rawDown() || rawMenu()) {
    delay(10);
  }

  upWasDown = false;
  downWasDown = false;
  menuWasDown = false;
  lastButtonAction = millis();
}

// --------------------------------------------------
// Menu system
// --------------------------------------------------
enum ScreenState {
  SCREEN_MENU,
  SCREEN_MODE
};

enum UtilityMode {
  MODE_DEVICE_STATUS,
  MODE_SETTINGS
};

ScreenState screenState = SCREEN_MENU;
UtilityMode currentMode = MODE_DEVICE_STATUS;

const char* utilityMenu[] = {
  "Device Stat",
  "Settings"
};

const int UTILITY_MENU_COUNT = sizeof(utilityMenu) / sizeof(utilityMenu[0]);
int menuIndex = 0;

// --------------------------------------------------
// Device Status pages
// --------------------------------------------------
int statusPage = 0;
const int STATUS_PAGE_COUNT = 4;

// --------------------------------------------------
// Settings
// --------------------------------------------------
int settingIndex = 0;
const int SETTING_COUNT = 4;

int brightnessIndex = 2;
const int brightnessValues[] = {40, 90, 160, 255};
const char* brightnessNames[] = {"Low", "Med", "High", "Max"};

bool autoRefresh = true;
bool displayInvert = false;
int sleepTimeoutIndex = 0;
const char* sleepTimeoutNames[] = {"Off", "30s", "60s", "120s"};

// --------------------------------------------------
// UI helpers
// --------------------------------------------------
String fitText(String text, int maxChars) {
  if (text.length() <= maxChars) {
    return text;
  }

  if (maxChars <= 1) {
    return ".";
  }

  return text.substring(0, maxChars - 1) + ".";
}

String formatUptime() {
  unsigned long seconds = millis() / 1000;

  unsigned long h = seconds / 3600;
  unsigned long m = (seconds % 3600) / 60;
  unsigned long s = seconds % 60;

  String out = "";

  if (h < 10) out += "0";
  out += String(h);
  out += ":";

  if (m < 10) out += "0";
  out += String(m);
  out += ":";

  if (s < 10) out += "0";
  out += String(s);

  return out;
}

void drawHeader(const char* title) {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 7, fitText(String(title), 12).c_str());
  u8g2.drawHLine(0, 10, SCREEN_W);
}

void drawFooter(const char* text) {
  u8g2.drawHLine(0, 116, SCREEN_W);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 126, fitText(String(text), 15).c_str());
}

void drawKeyValue(int y, const char* key, String value) {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, y, fitText(String(key), 8).c_str());
  u8g2.drawStr(0, y + 10, fitText(value, 12).c_str());
}

void showMessage(const char* title, const char* line1, const char* line2 = "", const char* line3 = "") {
  u8g2.clearBuffer();

  drawHeader(title);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 32, fitText(String(line1), 12).c_str());
  u8g2.drawStr(0, 46, fitText(String(line2), 12).c_str());
  u8g2.drawStr(0, 60, fitText(String(line3), 12).c_str());

  drawFooter("working...");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Splash / raw button check
// --------------------------------------------------
void drawDeviceIcon(int cx, int cy, int frame) {
  u8g2.drawRFrame(cx - 18, cy - 24, 36, 48, 4);
  u8g2.drawHLine(cx - 10, cy - 16, 20);
  u8g2.drawHLine(cx - 10, cy - 6, 20);
  u8g2.drawHLine(cx - 10, cy + 4, 20);
  u8g2.drawDisc(cx, cy + 16, 2);

  if (frame % 2 == 0) {
    u8g2.drawFrame(cx - 13, cy - 11, 26, 20);
  } else {
    u8g2.drawBox(cx - 12, cy - 10, 24, 18);
    u8g2.setDrawColor(0);
    u8g2.drawStr(cx - 9, cy + 2, "OK");
    u8g2.setDrawColor(1);
  }
}

void splashScreen() {
  unsigned long start = millis();
  int frame = 0;

  while (millis() - start < 2500) {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(1, 18, "Net");
    u8g2.drawStr(1, 34, "View");

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(2, 52, "Device Util");

    drawDeviceIcon(32, 88, frame);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(4, 122, "System Tools");

    u8g2.sendBuffer();

    frame++;
    delay(180);
  }
}

void drawRawButtonCheck() {
  u8g2.clearBuffer();

  drawHeader("Button Check");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 28, "UP:");
  u8g2.drawStr(28, 28, rawUp() ? "ON" : "off");

  u8g2.drawStr(0, 44, "DN:");
  u8g2.drawStr(28, 44, rawDown() ? "ON" : "off");

  u8g2.drawStr(0, 60, "MN:");
  u8g2.drawStr(28, 60, rawMenu() ? "ON" : "off");

  u8g2.drawStr(0, 88, "MENU");
  u8g2.drawStr(0, 102, "to start");

  u8g2.sendBuffer();
}

// --------------------------------------------------
// Menu screen
// --------------------------------------------------
void drawMenu() {
  u8g2.clearBuffer();

  drawHeader("Device Util");

  u8g2.setFont(u8g2_font_5x7_tf);

  for (int i = 0; i < UTILITY_MENU_COUNT; i++) {
    int y = 34 + i * 18;

    if (i == menuIndex) {
      u8g2.drawBox(0, y - 8, SCREEN_W, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(1, y, fitText(String(utilityMenu[i]), 11).c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(1, y, fitText(String(utilityMenu[i]), 11).c_str());
    }
  }

  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 76, "System info");
  u8g2.drawStr(0, 88, "OLED settings");

  drawFooter("UP DN SEL");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Device Status Mode
// --------------------------------------------------
String getChipModelString() {
  #if defined(ESP_ARDUINO_VERSION_MAJOR)
    return String(ESP.getChipModel());
  #else
    return "ESP32";
  #endif
}

void drawStatusPage0() {
  drawHeader("Status 1/4");

  drawKeyValue(24, "Uptime", formatUptime());
  drawKeyValue(48, "Heap", String(ESP.getFreeHeap() / 1024) + " KB");
  drawKeyValue(72, "CPU", String(ESP.getCpuFreqMHz()) + " MHz");
  drawKeyValue(96, "Sketch", String(ESP.getSketchSize() / 1024) + " KB");
}

void drawStatusPage1() {
  drawHeader("Status 2/4");

  drawKeyValue(24, "Chip", getChipModelString());
  drawKeyValue(48, "Cores", String(ESP.getChipCores()));
  drawKeyValue(72, "Rev", String(ESP.getChipRevision()));
  drawKeyValue(96, "SDK", fitText(String(ESP.getSdkVersion()), 12));
}

void drawStatusPage2() {
  drawHeader("Status 3/4");

  drawKeyValue(24, "Flash", String(ESP.getFlashChipSize() / 1024 / 1024) + " MB");
  drawKeyValue(48, "Speed", String(ESP.getFlashChipSpeed() / 1000000) + " MHz");
  drawKeyValue(72, "Free", String(ESP.getFreeSketchSpace() / 1024) + " KB");
  drawKeyValue(96, "Boots", "runtime");
}

void drawStatusPage3() {
  drawHeader("Status 4/4");

  drawKeyValue(24, "UP pin", String(BTN_UP_PIN));
  drawKeyValue(48, "DN pin", String(BTN_DOWN_PIN));
  drawKeyValue(72, "MN pin", String(BTN_MENU_PIN));

  String states = "";
  states += rawUp() ? "U1 " : "U0 ";
  states += rawDown() ? "D1 " : "D0 ";
  states += rawMenu() ? "M1" : "M0";
  drawKeyValue(96, "Buttons", states);
}

void drawDeviceStatus() {
  u8g2.clearBuffer();

  if (statusPage == 0) {
    drawStatusPage0();
  } else if (statusPage == 1) {
    drawStatusPage1();
  } else if (statusPage == 2) {
    drawStatusPage2();
  } else {
    drawStatusPage3();
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Settings Mode
// --------------------------------------------------
String settingName(int index) {
  if (index == 0) return "Brightness";
  if (index == 1) return "Auto Refsh";
  if (index == 2) return "Invert";
  return "Sleep Tmr";
}

String settingValue(int index) {
  if (index == 0) return String(brightnessNames[brightnessIndex]);
  if (index == 1) return autoRefresh ? "ON" : "OFF";
  if (index == 2) return displayInvert ? "ON" : "OFF";
  return String(sleepTimeoutNames[sleepTimeoutIndex]);
}

void applySettings() {
  u8g2.setContrast(brightnessValues[brightnessIndex]);
  u8g2.setDrawColor(1);
  u8g2.setFontMode(1);

  if (displayInvert) {
    u8g2.setDisplayRotation(U8G2_R1);
    // U8g2 has no universal invert method for every display constructor.
    // This setting is represented in software for now.
  }
}

void changeCurrentSetting() {
  if (settingIndex == 0) {
    brightnessIndex++;

    if (brightnessIndex >= 4) {
      brightnessIndex = 0;
    }

    u8g2.setContrast(brightnessValues[brightnessIndex]);
  } else if (settingIndex == 1) {
    autoRefresh = !autoRefresh;
  } else if (settingIndex == 2) {
    displayInvert = !displayInvert;
  } else if (settingIndex == 3) {
    sleepTimeoutIndex++;

    if (sleepTimeoutIndex >= 4) {
      sleepTimeoutIndex = 0;
    }
  }

  applySettings();
}

void drawSettings() {
  u8g2.clearBuffer();

  drawHeader("Settings");

  u8g2.setFont(u8g2_font_5x7_tf);

  int first = settingIndex - 1;

  if (first < 0) {
    first = 0;
  }

  if (first > SETTING_COUNT - 4) {
    first = max(0, SETTING_COUNT - 4);
  }

  for (int i = 0; i < 4; i++) {
    int idx = first + i;

    if (idx >= SETTING_COUNT) {
      break;
    }

    int y = 26 + i * 22;

    if (idx == settingIndex) {
      u8g2.drawBox(0, y - 8, SCREEN_W, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(1, y, fitText(settingName(idx), 11).c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(1, y, fitText(settingName(idx), 11).c_str());
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(8, y + 10, fitText(settingValue(idx), 12).c_str());
    u8g2.setFont(u8g2_font_5x7_tf);
  }

  drawFooter("UP Sel DN Chg");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Draw current mode
// --------------------------------------------------
void drawCurrentMode() {
  switch (currentMode) {
    case MODE_DEVICE_STATUS:
      drawDeviceStatus();
      break;

    case MODE_SETTINGS:
      drawSettings();
      break;
  }
}

// --------------------------------------------------
// Input actions
// --------------------------------------------------
void handleUp() {
  if (screenState == SCREEN_MENU) {
    menuIndex--;

    if (menuIndex < 0) {
      menuIndex = UTILITY_MENU_COUNT - 1;
    }

    drawMenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (currentMode == MODE_DEVICE_STATUS) {
      statusPage--;

      if (statusPage < 0) {
        statusPage = STATUS_PAGE_COUNT - 1;
      }

      drawDeviceStatus();
    } else if (currentMode == MODE_SETTINGS) {
      settingIndex--;

      if (settingIndex < 0) {
        settingIndex = SETTING_COUNT - 1;
      }

      drawSettings();
    }
  }
}

void handleDown() {
  if (screenState == SCREEN_MENU) {
    menuIndex++;

    if (menuIndex >= UTILITY_MENU_COUNT) {
      menuIndex = 0;
    }

    drawMenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (currentMode == MODE_DEVICE_STATUS) {
      statusPage++;

      if (statusPage >= STATUS_PAGE_COUNT) {
        statusPage = 0;
      }

      drawDeviceStatus();
    } else if (currentMode == MODE_SETTINGS) {
      changeCurrentSetting();
      drawSettings();
    }
  }
}

void handleMenu() {
  if (screenState == SCREEN_MENU) {
    currentMode = (UtilityMode)menuIndex;
    screenState = SCREEN_MODE;

    drawCurrentMode();
    return;
  }

  if (screenState == SCREEN_MODE) {
    screenState = SCREEN_MENU;
    drawMenu();
    return;
  }
}

void handleButtons() {
  if (upPressed()) {
    Serial.println("UP");
    handleUp();
  }

  if (downPressed()) {
    Serial.println("DOWN");
    handleDown();
  }

  if (menuPressed()) {
    Serial.println("MENU");
    handleMenu();
  }
}

// --------------------------------------------------
// Background refresh
// --------------------------------------------------
void handleAutoRefresh() {
  static unsigned long lastRefresh = 0;

  if (!autoRefresh) {
    return;
  }

  if (screenState != SCREEN_MODE) {
    return;
  }

  if (currentMode != MODE_DEVICE_STATUS) {
    return;
  }

  if (millis() - lastRefresh < 1000) {
    return;
  }

  lastRefresh = millis();
  drawDeviceStatus();
}

// --------------------------------------------------
// Setup / Loop
// --------------------------------------------------
void setup() {
  Serial.begin(115200);

  pinMode(BTN_UP_PIN, INPUT_PULLUP);
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  pinMode(BTN_MENU_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA, OLED_SCL);

  u8g2.begin();
  u8g2.setContrast(brightnessValues[brightnessIndex]);

  splashScreen();

  // Startup button check. Press MENU to begin.
  while (true) {
    drawRawButtonCheck();

    if (menuPressed()) {
      waitForButtonsReleased();
      break;
    }

    delay(80);
  }

  drawMenu();
}

void loop() {
  handleButtons();
  handleAutoRefresh();
}
