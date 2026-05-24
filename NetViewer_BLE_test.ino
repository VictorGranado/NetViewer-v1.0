/*
  NetViewer / PocketNet - Real BLE Modes Test Firmware
  ----------------------------------------------------
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

  BLE Library:
    Classic ESP32 BLE / Kolban style.
    Do NOT use NimBLE.

  Locked BLE Modes Implemented:
    1. BLE Scanner
    2. BLE Proximity Monitor
    3. BLE Beacon Detector
    4. BLE Signal Tracker
    5. BLE Service Viewer
    6. BLE Advertiser Mode
    7. BLE UART Mode

  Required Libraries:
    - U8g2
    - ESP32 BLE Arduino / built-in ESP32 BLE library

  Controls:
    Startup:
      MENU = start

    BLE Menu:
      UP   = previous mode
      DOWN = next mode
      MENU = enter mode

    BLE Scanner:
      UP/DOWN = previous/next device
      MENU    = back

    BLE Proximity:
      UP/DOWN = previous/next device
      MENU    = back

    BLE Beacon Detector:
      UP/DOWN = previous/next beacon-like device
      MENU    = back

    BLE Signal Tracker:
      UP/DOWN = previous/next device
      MENU    = back

    BLE Service Viewer:
      UP/DOWN = previous/next device
      MENU    = back

    BLE Advertiser:
      MENU = back

    BLE UART:
      MENU = back
*/

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// BLE classic / Kolban
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

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
// BLE settings
// --------------------------------------------------
const char* DEVICE_NAME = "NetViewer_BLE";

const int BLE_SCAN_TIME_SECONDS = 5;

// Nordic UART Service UUIDs
#define UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Simple custom advertiser/service UUID
#define STATUS_SERVICE_UUID "180F"
#define STATUS_CHAR_UUID    "2A19"

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
// BLE scan data
// --------------------------------------------------
#define MAX_BLE_RESULTS 35

struct BLEEntry {
  String name;
  String address;
  int rssi;
  String serviceUUID;
  bool hasServiceUUID;
  bool hasManufacturerData;
  bool connectableGuess;
};

BLEEntry bleResults[MAX_BLE_RESULTS];

int bleCount = 0;
int selectedBLE = 0;

unsigned long lastScanTime = 0;

// --------------------------------------------------
// Improved BLE Signal Tracker state
// --------------------------------------------------
bool trackerInitialized = false;
String trackerAddress = "";
String trackerName = "";
int trackerRSSI = -127;
int trackerMinRSSI = 0;
int trackerMaxRSSI = -127;
long trackerRSSISum = 0;
int trackerSamples = 0;
bool trackerFoundLastScan = false;
unsigned long lastTrackerScan = 0;
const unsigned long TRACKER_REFRESH_MS = 2200;

// Used by the scan callback during tracker refresh scans.
// This prevents tracker refreshes from polluting the normal BLE device list.
bool trackerScanMode = false;
String trackerTargetAddress = "";
int trackerTempRSSI = -127;
bool trackerTempFound = false;

// --------------------------------------------------
// Improved BLE Service Viewer state
// --------------------------------------------------
int serviceViewerPage = 0;
const int SERVICE_VIEWER_PAGE_COUNT = 3;

// --------------------------------------------------
// BLE UART state
// --------------------------------------------------
BLEServer* bleServer = nullptr;
BLECharacteristic* uartTxCharacteristic = nullptr;
BLECharacteristic* uartRxCharacteristic = nullptr;

bool bleServerStarted = false;
bool bleAdvertiserStarted = false;
bool bleUARTStarted = false;
bool bleClientConnected = false;

String bleUartLastRx = "";
unsigned long lastUARTNotify = 0;

// --------------------------------------------------
// Menu system
// --------------------------------------------------
enum ScreenState {
  SCREEN_MENU,
  SCREEN_MODE
};

enum BLEMode {
  MODE_BLE_SCANNER,
  MODE_BLE_PROXIMITY,
  MODE_BLE_BEACON_DETECTOR,
  MODE_BLE_SIGNAL_TRACKER,
  MODE_BLE_SERVICE_VIEWER,
  MODE_BLE_ADVERTISER,
  MODE_BLE_UART
};

ScreenState screenState = SCREEN_MENU;
BLEMode currentMode = MODE_BLE_SCANNER;

const char* bleMenu[] = {
  "BLE Scan",
  "Proximity",
  "Beacon Det",
  "Signal Trk",
  "Svc Viewer",
  "Advertise",
  "BLE UART"
};

const int BLE_MENU_COUNT = sizeof(bleMenu) / sizeof(bleMenu[0]);
int menuIndex = 0;

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

int rssiToQuality(int rssi) {
  if (rssi <= -100) {
    return 0;
  }

  if (rssi >= -50) {
    return 100;
  }

  return 2 * (rssi + 100);
}

String rssiToProximity(int rssi) {
  if (rssi >= -55) {
    return "NEAR";
  }

  if (rssi >= -72) {
    return "MEDIUM";
  }

  return "FAR";
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

void drawSignalBar(int y, int percent) {
  percent = constrain(percent, 0, 100);

  u8g2.drawFrame(0, y, 62, 8);

  int fillW = map(percent, 0, 100, 0, 60);
  u8g2.drawBox(1, y + 1, fillW, 6);
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
void drawBLEIcon(int x, int y, int step) {
  // Simple BLE-like glyph
  u8g2.drawLine(x, y - 18, x, y + 18);

  if (step >= 1) {
    u8g2.drawLine(x, y, x + 12, y - 10);
    u8g2.drawLine(x + 12, y - 10, x, y - 18);
  }

  if (step >= 2) {
    u8g2.drawLine(x, y, x + 12, y + 10);
    u8g2.drawLine(x + 12, y + 10, x, y + 18);
  }

  if (step >= 3) {
    u8g2.drawCircle(x, y, 22);
  }
}

void splashScreen() {
  unsigned long start = millis();
  int frame = 0;

  while (millis() - start < 3000) {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(1, 18, "Net");
    u8g2.drawStr(1, 34, "View");

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(7, 52, "BLE Modes");

    drawBLEIcon(30, 88, frame % 4);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(4, 122, "Kolban BLE");

    u8g2.sendBuffer();

    frame++;
    delay(150);
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
// BLE scan callback
// --------------------------------------------------
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String foundAddress = String(advertisedDevice.getAddress().toString().c_str());

    // Tracker refresh mode:
    // only watch for the selected target and do not append to the full scan list.
    if (trackerScanMode) {
      if (foundAddress == trackerTargetAddress) {
        trackerTempRSSI = advertisedDevice.getRSSI();
        trackerTempFound = true;
      }
      return;
    }

    if (bleCount >= MAX_BLE_RESULTS) {
      return;
    }

    BLEEntry &e = bleResults[bleCount];

    if (advertisedDevice.haveName()) {
      e.name = String(advertisedDevice.getName().c_str());
    } else {
      e.name = "<unknown>";
    }

    e.address = foundAddress;
    e.rssi = advertisedDevice.getRSSI();

    if (advertisedDevice.haveServiceUUID()) {
      e.serviceUUID = String(advertisedDevice.getServiceUUID().toString().c_str());
      e.hasServiceUUID = true;
    } else {
      e.serviceUUID = "-";
      e.hasServiceUUID = false;
    }

    e.hasManufacturerData = advertisedDevice.haveManufacturerData();

    // Simple guess: if it advertises services, it may be connectable.
    e.connectableGuess = e.hasServiceUUID;

    bleCount++;
  }
};

MyAdvertisedDeviceCallbacks scanCallbacks;

// --------------------------------------------------
// BLE scan functions
// --------------------------------------------------
void sortBLEByRSSI() {
  for (int i = 0; i < bleCount - 1; i++) {
    for (int j = i + 1; j < bleCount; j++) {
      if (bleResults[j].rssi > bleResults[i].rssi) {
        BLEEntry temp = bleResults[i];
        bleResults[i] = bleResults[j];
        bleResults[j] = temp;
      }
    }
  }
}

void performBLEScan() {
  showMessage("BLE Scan", "Scanning...", "please wait");

  bleCount = 0;
  selectedBLE = 0;

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(&scanCallbacks, true);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(80);

  pBLEScan->start(BLE_SCAN_TIME_SECONDS, false);
  pBLEScan->clearResults();

  sortBLEByRSSI();

  lastScanTime = millis();

  waitForButtonsReleased();
}

void resetTrackerForSelected() {
  if (bleCount <= 0) {
    trackerInitialized = false;
    return;
  }

  BLEEntry &e = bleResults[selectedBLE];

  trackerInitialized = true;
  trackerAddress = e.address;
  trackerName = e.name;
  trackerRSSI = e.rssi;
  trackerMinRSSI = e.rssi;
  trackerMaxRSSI = e.rssi;
  trackerRSSISum = e.rssi;
  trackerSamples = 1;
  trackerFoundLastScan = true;
  lastTrackerScan = 0;
}

void updateTrackerScan() {
  if (bleCount <= 0) {
    return;
  }

  if (!trackerInitialized || trackerAddress != bleResults[selectedBLE].address) {
    resetTrackerForSelected();
  }

  if (millis() - lastTrackerScan < TRACKER_REFRESH_MS) {
    return;
  }

  lastTrackerScan = millis();

  trackerScanMode = true;
  trackerTargetAddress = trackerAddress;
  trackerTempRSSI = -127;
  trackerTempFound = false;

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(&scanCallbacks, true);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(80);
  pBLEScan->setWindow(60);

  // Short blocking refresh scan. This is intentionally short so the UI remains usable.
  pBLEScan->start(1, false);
  pBLEScan->clearResults();

  trackerScanMode = false;

  if (trackerTempFound) {
    trackerRSSI = trackerTempRSSI;
    trackerFoundLastScan = true;

    if (trackerRSSI < trackerMinRSSI) {
      trackerMinRSSI = trackerRSSI;
    }

    if (trackerRSSI > trackerMaxRSSI) {
      trackerMaxRSSI = trackerRSSI;
    }

    trackerRSSISum += trackerRSSI;
    trackerSamples++;
  } else {
    trackerFoundLastScan = false;
  }
}

String knownServiceName(String uuid) {
  uuid.toLowerCase();

  if (uuid.indexOf("180f") >= 0) return "Battery";
  if (uuid.indexOf("180a") >= 0) return "Dev Info";
  if (uuid.indexOf("180d") >= 0) return "Heart Rate";
  if (uuid.indexOf("181a") >= 0) return "Env Sense";
  if (uuid.indexOf("1809") >= 0) return "Health Tmp";
  if (uuid.indexOf("1812") >= 0) return "HID";
  if (uuid.indexOf("feaa") >= 0) return "Eddystone";
  if (uuid.indexOf("6e400001") >= 0) return "BLE UART";

  return "Unknown";
}

// --------------------------------------------------
// BLE server callbacks
// --------------------------------------------------
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleClientConnected = true;
  }

  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
    BLEDevice::startAdvertising();
  }
};

class UartRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();

    if (value.length() > 0) {
      bleUartLastRx = value;
    }
  }
};

MyServerCallbacks serverCallbacks;
UartRxCallbacks uartRxCallbacks;

// --------------------------------------------------
// BLE advertiser / UART modes
// --------------------------------------------------
void startBLEAdvertiserMode() {
  if (bleAdvertiserStarted) {
    return;
  }

  BLEDevice::deinit(false);
  delay(100);
  BLEDevice::init(DEVICE_NAME);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(&serverCallbacks);

  BLEService* statusService = bleServer->createService(STATUS_SERVICE_UUID);

  BLECharacteristic* batteryChar = statusService->createCharacteristic(
    STATUS_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ
  );

  batteryChar->setValue("85");

  statusService->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(STATUS_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  bleAdvertiserStarted = true;
  bleUARTStarted = false;
}

void startBLEUARTMode() {
  if (bleUARTStarted) {
    return;
  }

  BLEDevice::deinit(false);
  delay(100);
  BLEDevice::init(DEVICE_NAME);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(&serverCallbacks);

  BLEService* uartService = bleServer->createService(UART_SERVICE_UUID);

  uartTxCharacteristic = uartService->createCharacteristic(
    UART_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );

  uartTxCharacteristic->addDescriptor(new BLE2902());

  uartRxCharacteristic = uartService->createCharacteristic(
    UART_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );

  uartRxCharacteristic->setCallbacks(&uartRxCallbacks);

  uartService->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(UART_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  bleUARTStarted = true;
  bleAdvertiserStarted = false;
}

void sendUARTStatusIfNeeded() {
  if (!bleUARTStarted || !bleClientConnected || uartTxCharacteristic == nullptr) {
    return;
  }

  if (millis() - lastUARTNotify < 1500) {
    return;
  }

  lastUARTNotify = millis();

  String msg = "NetViewer BLE UART uptime=" + String(millis() / 1000) + "s";
  uartTxCharacteristic->setValue(msg.c_str());
  uartTxCharacteristic->notify();
}

// --------------------------------------------------
// Menu screen
// --------------------------------------------------
void drawMenu() {
  u8g2.clearBuffer();

  drawHeader("BLE Tools");

  u8g2.setFont(u8g2_font_5x7_tf);

  int first = menuIndex - 3;

  if (first < 0) {
    first = 0;
  }

  if (first > BLE_MENU_COUNT - 8) {
    first = max(0, BLE_MENU_COUNT - 8);
  }

  for (int i = 0; i < 7; i++) {
    int idx = first + i;

    if (idx >= BLE_MENU_COUNT) {
      break;
    }

    int y = 24 + i * 12;

    if (idx == menuIndex) {
      u8g2.drawBox(0, y - 8, SCREEN_W, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(1, y, fitText(String(bleMenu[idx]), 11).c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(1, y, fitText(String(bleMenu[idx]), 11).c_str());
    }
  }

  drawFooter("UP DN SEL");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// BLE mode screens
// --------------------------------------------------
void drawBLEScanner() {
  u8g2.clearBuffer();

  drawHeader("BLE Scan");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (bleCount <= 0) {
    u8g2.drawStr(0, 30, "No devices");
    u8g2.drawStr(0, 44, "MENU back");
    u8g2.drawStr(0, 58, "re-enter");
    u8g2.drawStr(0, 72, "to scan");
  } else {
    BLEEntry &e = bleResults[selectedBLE];

    u8g2.drawStr(0, 22, ("Dev " + String(selectedBLE + 1)).c_str());
    u8g2.drawStr(34, 22, ("/" + String(bleCount)).c_str());

    u8g2.drawStr(0, 38, fitText(e.name, 11).c_str());

    u8g2.drawStr(0, 54, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 68, rssiToProximity(e.rssi).c_str());

    u8g2.drawStr(0, 84, fitText(e.address, 12).c_str());

    drawSignalBar(98, rssiToQuality(e.rssi));
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawBLEProximity() {
  u8g2.clearBuffer();

  drawHeader("Proximity");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (bleCount <= 0) {
    u8g2.drawStr(0, 34, "Scan first");
  } else {
    BLEEntry &e = bleResults[selectedBLE];
    int q = rssiToQuality(e.rssi);

    u8g2.drawStr(0, 22, fitText(e.name, 11).c_str());

    u8g2.drawStr(0, 42, ("RSSI:" + String(e.rssi)).c_str());

    String prox = rssiToProximity(e.rssi);
    u8g2.drawStr(0, 60, "Range:");
    u8g2.drawStr(0, 74, prox.c_str());

    drawSignalBar(90, q);
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawBLEBeaconDetector() {
  int beaconIndexes[MAX_BLE_RESULTS];
  int beaconCount = 0;

  for (int i = 0; i < bleCount; i++) {
    if (bleResults[i].hasManufacturerData) {
      beaconIndexes[beaconCount++] = i;
    }
  }

  u8g2.clearBuffer();

  drawHeader("Beacon Det");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (beaconCount <= 0) {
    u8g2.drawStr(0, 30, "No beacon");
    u8g2.drawStr(0, 44, "like adv");
    u8g2.drawStr(0, 68, "Detected by");
    u8g2.drawStr(0, 82, "mfg data");
  } else {
    int localIndex = selectedBLE % beaconCount;
    int idx = beaconIndexes[localIndex];

    BLEEntry &e = bleResults[idx];

    u8g2.drawStr(0, 22, ("Bcn " + String(localIndex + 1)).c_str());
    u8g2.drawStr(34, 22, ("/" + String(beaconCount)).c_str());

    u8g2.drawStr(0, 40, fitText(e.name, 11).c_str());
    u8g2.drawStr(0, 56, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 72, "MFG data");

    drawSignalBar(90, rssiToQuality(e.rssi));
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawBLESignalTracker() {
  updateTrackerScan();

  u8g2.clearBuffer();

  drawHeader("Signal Trk");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (bleCount <= 0) {
    u8g2.drawStr(0, 34, "Scan first");
  } else {
    int q = rssiToQuality(trackerRSSI);
    int avg = trackerSamples > 0 ? trackerRSSISum / trackerSamples : trackerRSSI;

    u8g2.drawStr(0, 20, fitText(trackerName, 11).c_str());

    u8g2.drawStr(0, 35, ("Now:" + String(trackerRSSI)).c_str());
    u8g2.drawStr(0, 48, ("Avg:" + String(avg)).c_str());

    u8g2.drawStr(0, 61, ("Min:" + String(trackerMinRSSI)).c_str());
    u8g2.drawStr(0, 74, ("Max:" + String(trackerMaxRSSI)).c_str());

    drawSignalBar(86, q);

    if (trackerFoundLastScan) {
      u8g2.drawStr(0, 106, "Live OK");
    } else {
      u8g2.drawStr(0, 106, "Not seen");
    }
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawBLEServiceViewer() {
  u8g2.clearBuffer();

  drawHeader("Svc Viewer");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (bleCount <= 0) {
    u8g2.drawStr(0, 34, "Scan first");
  } else {
    BLEEntry &e = bleResults[selectedBLE];

    if (serviceViewerPage == 0) {
      u8g2.drawStr(0, 20, fitText(e.name, 11).c_str());

      u8g2.drawStr(0, 36, "Adv UUID:");

      if (e.hasServiceUUID) {
        u8g2.drawStr(0, 50, fitText(e.serviceUUID, 12).c_str());
        u8g2.drawStr(0, 64, fitText(e.serviceUUID.substring(12), 12).c_str());

        u8g2.drawStr(0, 84, "Type:");
        u8g2.drawStr(0, 98, fitText(knownServiceName(e.serviceUUID), 12).c_str());
      } else {
        u8g2.drawStr(0, 50, "None adv");
        u8g2.drawStr(0, 70, "Some devs");
        u8g2.drawStr(0, 84, "hide UUIDs");
      }
    } else if (serviceViewerPage == 1) {
      u8g2.drawStr(0, 22, "Adv flags");

      u8g2.drawStr(0, 40, e.hasServiceUUID ? "UUID: yes" : "UUID: no");
      u8g2.drawStr(0, 54, e.hasManufacturerData ? "MFG: yes" : "MFG: no");
      u8g2.drawStr(0, 68, e.connectableGuess ? "Conn: maybe" : "Conn: unk");

      u8g2.drawStr(0, 88, ("RSSI:" + String(e.rssi)).c_str());
      drawSignalBar(100, rssiToQuality(e.rssi));
    } else {
      u8g2.drawStr(0, 20, "Address:");
      u8g2.drawStr(0, 34, fitText(e.address, 12).c_str());
      u8g2.drawStr(0, 48, fitText(e.address.substring(12), 12).c_str());

      u8g2.drawStr(0, 68, "Device:");
      u8g2.drawStr(0, 82, fitText(e.name, 12).c_str());

      u8g2.drawStr(0, 102, ("#" + String(selectedBLE + 1) + "/" + String(bleCount)).c_str());
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(45, 114, ("P" + String(serviceViewerPage + 1)).c_str());
  }

  drawFooter("UP Pg DN Dev");
  u8g2.sendBuffer();
}

void drawBLEAdvertiser() {
  startBLEAdvertiserMode();

  u8g2.clearBuffer();

  drawHeader("Advertise");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 24, "Advertising");
  u8g2.drawStr(0, 40, "as:");
  u8g2.drawStr(0, 54, fitText(String(DEVICE_NAME), 12).c_str());

  u8g2.drawStr(0, 76, "Svc:");
  u8g2.drawStr(0, 90, STATUS_SERVICE_UUID);

  u8g2.drawStr(0, 106, bleClientConnected ? "Client: yes" : "Client: no");

  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawBLEUART() {
  startBLEUARTMode();

  u8g2.clearBuffer();

  drawHeader("BLE UART");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 22, bleClientConnected ? "Connected" : "Waiting");

  u8g2.drawStr(0, 40, "Name:");
  u8g2.drawStr(0, 52, fitText(String(DEVICE_NAME), 12).c_str());

  u8g2.drawStr(0, 70, "Last RX:");
  u8g2.drawStr(0, 84, fitText(bleUartLastRx, 12).c_str());

  u8g2.drawStr(0, 104, "nRF Connect");

  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawCurrentMode() {
  switch (currentMode) {
    case MODE_BLE_SCANNER:
      drawBLEScanner();
      break;

    case MODE_BLE_PROXIMITY:
      drawBLEProximity();
      break;

    case MODE_BLE_BEACON_DETECTOR:
      drawBLEBeaconDetector();
      break;

    case MODE_BLE_SIGNAL_TRACKER:
      drawBLESignalTracker();
      break;

    case MODE_BLE_SERVICE_VIEWER:
      drawBLEServiceViewer();
      break;

    case MODE_BLE_ADVERTISER:
      drawBLEAdvertiser();
      break;

    case MODE_BLE_UART:
      drawBLEUART();
      break;
  }
}

// --------------------------------------------------
// Mode helpers/actions
// --------------------------------------------------
bool modeNeedsBLEScan(int mode) {
  return (
    mode == MODE_BLE_SCANNER ||
    mode == MODE_BLE_PROXIMITY ||
    mode == MODE_BLE_BEACON_DETECTOR ||
    mode == MODE_BLE_SIGNAL_TRACKER ||
    mode == MODE_BLE_SERVICE_VIEWER
  );
}

void handleUp() {
  if (screenState == SCREEN_MENU) {
    menuIndex--;

    if (menuIndex < 0) {
      menuIndex = BLE_MENU_COUNT - 1;
    }

    drawMenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (currentMode == MODE_BLE_SERVICE_VIEWER) {
      serviceViewerPage--;

      if (serviceViewerPage < 0) {
        serviceViewerPage = SERVICE_VIEWER_PAGE_COUNT - 1;
      }

      drawCurrentMode();
      return;
    }

    if (
      currentMode == MODE_BLE_SCANNER ||
      currentMode == MODE_BLE_PROXIMITY ||
      currentMode == MODE_BLE_BEACON_DETECTOR ||
      currentMode == MODE_BLE_SIGNAL_TRACKER
    ) {
      if (bleCount > 0) {
        selectedBLE--;

        if (selectedBLE < 0) {
          selectedBLE = bleCount - 1;
        }

        if (currentMode == MODE_BLE_SIGNAL_TRACKER) {
          resetTrackerForSelected();
        }
      }

      drawCurrentMode();
    }
  }
}

void handleDown() {
  if (screenState == SCREEN_MENU) {
    menuIndex++;

    if (menuIndex >= BLE_MENU_COUNT) {
      menuIndex = 0;
    }

    drawMenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (
      currentMode == MODE_BLE_SCANNER ||
      currentMode == MODE_BLE_PROXIMITY ||
      currentMode == MODE_BLE_BEACON_DETECTOR ||
      currentMode == MODE_BLE_SIGNAL_TRACKER ||
      currentMode == MODE_BLE_SERVICE_VIEWER
    ) {
      if (bleCount > 0) {
        selectedBLE++;

        if (selectedBLE >= bleCount) {
          selectedBLE = 0;
        }

        if (currentMode == MODE_BLE_SIGNAL_TRACKER) {
          resetTrackerForSelected();
        }
      }

      drawCurrentMode();
    }
  }
}

void handleMenu() {
  if (screenState == SCREEN_MENU) {
    currentMode = (BLEMode)menuIndex;
    screenState = SCREEN_MODE;

    if (modeNeedsBLEScan(currentMode)) {
      performBLEScan();
    }

    if (currentMode == MODE_BLE_SIGNAL_TRACKER) {
      resetTrackerForSelected();
    }

    if (currentMode == MODE_BLE_SERVICE_VIEWER) {
      serviceViewerPage = 0;
    }

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
// Background updates
// --------------------------------------------------
void handleBLEBackground() {
  sendUARTStatusIfNeeded();

  static unsigned long lastDisplayRefresh = 0;

  if (screenState == SCREEN_MODE) {
    if (
      currentMode == MODE_BLE_ADVERTISER ||
      currentMode == MODE_BLE_UART
    ) {
      if (millis() - lastDisplayRefresh > 1000) {
        drawCurrentMode();
        lastDisplayRefresh = millis();
      }
    }

    if (currentMode == MODE_BLE_SIGNAL_TRACKER) {
      if (millis() - lastDisplayRefresh > 2200) {
        drawCurrentMode();
        lastDisplayRefresh = millis();
      }
    }
  }
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
  u8g2.setContrast(255);

  splashScreen();

  BLEDevice::init(DEVICE_NAME);

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
  handleBLEBackground();
}
