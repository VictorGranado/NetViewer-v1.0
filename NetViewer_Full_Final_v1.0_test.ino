/*
  NetViewer / PocketNet - FULL MERGED FIRMWARE
  -------------------------------------------
  Hardware:
    ESP32-WROOM-DA
    SSD1309 2.4" I2C OLED, portrait orientation
    UP    -> GPIO32 and GND
    DOWN  -> GPIO26 and GND
    MENU  -> GPIO27 and GND
    OLED SDA -> GPIO21
    OLED SCL -> GPIO22

  Menus merged:
    1. Wi-Fi Tools
    2. BLE Tools
    3. Web / Server
    4. Device / Utility

  Notes:
    - Uses classic ESP32 BLE/Kolban style, not NimBLE.
    - WebSocket Live Monitor is implemented as browser fetch('/status') live monitor.
    - Each submenu includes a BACK option to return home.
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESP32Ping.h>
#include <U8g2lib.h>
#include <Preferences.h>

// Classic ESP32 BLE / Kolban style
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

#define SCREEN_W 64
#define SCREEN_H 128

U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R1, U8X8_PIN_NONE);

// --------------------------------------------------
// Wi-Fi / Web settings
// --------------------------------------------------
// Optional fallback station Wi-Fi credentials.
// Leave blank for normal use. Wi-Fi credentials can be entered from the web portal at /wifi.
const char* STA_SSID = "";
const char* STA_PASS = "";

// Saved Wi-Fi credentials are stored in ESP32 flash using Preferences.
Preferences prefs;
String savedStaSSID = "";
String savedStaPASS = "";

const char* AP_SSID = "NetViewer_AP";
const char* AP_PASS = "12345678";

const char* PING_TARGET = "8.8.8.8";
const char* INTERNET_TEST_HOST = "example.com";
const char* HTTP_TEST_URL = "http://example.com";

IPAddress apIP(192, 168, 4, 1);
IPAddress gatewayIP(192, 168, 4, 1);
IPAddress subnetMask(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;
bool apStarted = false;
bool serverStarted = false;
bool captiveStarted = false;

// --------------------------------------------------
// BLE settings
// --------------------------------------------------
const char* BLE_DEVICE_NAME = "NetViewer_BLE";
const int BLE_SCAN_TIME_SECONDS = 5;

#define UART_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define UART_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define STATUS_SERVICE_UUID "180F"
#define STATUS_CHAR_UUID    "2A19"

BLEServer* bleServer = nullptr;
BLECharacteristic* uartTxCharacteristic = nullptr;
BLECharacteristic* uartRxCharacteristic = nullptr;
bool bleAdvertiserStarted = false;
bool bleUARTStarted = false;
bool bleClientConnected = false;
String bleUartLastRx = "";
unsigned long lastUARTNotify = 0;

// --------------------------------------------------
// Button handling
// --------------------------------------------------
bool upWasDown = false;
bool downWasDown = false;
bool menuWasDown = false;
unsigned long lastButtonAction = 0;
const unsigned long BUTTON_GAP_MS = 170;

bool rawUp()   { return digitalRead(BTN_UP_PIN) == LOW; }
bool rawDown() { return digitalRead(BTN_DOWN_PIN) == LOW; }
bool rawMenu() { return digitalRead(BTN_MENU_PIN) == LOW; }

bool buttonEdge(bool nowDown, bool &wasDown) {
  if (nowDown && !wasDown && millis() - lastButtonAction > BUTTON_GAP_MS) {
    wasDown = true;
    lastButtonAction = millis();
    return true;
  }
  if (!nowDown) wasDown = false;
  return false;
}

bool upPressed()   { return buttonEdge(rawUp(), upWasDown); }
bool downPressed() { return buttonEdge(rawDown(), downWasDown); }
bool menuPressed() { return buttonEdge(rawMenu(), menuWasDown); }

void waitForButtonsReleased() {
  while (rawUp() || rawDown() || rawMenu()) delay(10);
  upWasDown = false;
  downWasDown = false;
  menuWasDown = false;
  lastButtonAction = millis();
}

// --------------------------------------------------
// UI helpers
// --------------------------------------------------
String fitText(String text, int maxChars) {
  if (text.length() <= maxChars) return text;
  if (maxChars <= 1) return ".";
  return text.substring(0, maxChars - 1) + ".";
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

int rssiToQuality(int rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
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

void drawKeyValue(int y, const char* key, String value) {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, y, fitText(String(key), 8).c_str());
  u8g2.drawStr(0, y + 10, fitText(value, 12).c_str());
}

// --------------------------------------------------
// App state / menus
// --------------------------------------------------
enum ScreenState {
  SCREEN_MAIN_MENU,
  SCREEN_WIFI_MENU,
  SCREEN_BLE_MENU,
  SCREEN_WEB_MENU,
  SCREEN_DEVICE_MENU,
  SCREEN_MODE
};

enum ActiveGroup {
  GROUP_NONE,
  GROUP_WIFI,
  GROUP_BLE,
  GROUP_WEB,
  GROUP_DEVICE
};

enum ModeID {
  MODE_NONE,

  // Wi-Fi
  MODE_WIFI_SCANNER,
  MODE_WIFI_VISUALIZER,
  MODE_WIFI_CHANNEL_ANALYZER,
  MODE_WIFI_SIGNAL_MONITOR,
  MODE_WIFI_OPEN_NETWORKS,
  MODE_WIFI_AP_MODE,
  MODE_WIFI_NETWORK_INFO,
  MODE_WIFI_PING_TEST,
  MODE_WIFI_INTERNET_CHECK,
  MODE_WIFI_WEB_REQUEST_TEST,
  MODE_WIFI_HEAT_WALK,

  // BLE
  MODE_BLE_SCANNER,
  MODE_BLE_PROXIMITY,
  MODE_BLE_BEACON_DETECTOR,
  MODE_BLE_SIGNAL_TRACKER,
  MODE_BLE_SERVICE_VIEWER,
  MODE_BLE_ADVERTISER,
  MODE_BLE_UART,

  // Web
  MODE_WEB_START_SERVER,
  MODE_WEB_INFO,
  MODE_WEB_CAPTIVE_PORTAL,
  MODE_WEB_CONTROL_PANEL,
  MODE_WEB_LIVE_MONITOR,

  // Device
  MODE_DEVICE_STATUS,
  MODE_DEVICE_SETTINGS
};

ScreenState screenState = SCREEN_MAIN_MENU;
ActiveGroup activeGroup = GROUP_NONE;
ModeID currentMode = MODE_NONE;

int mainIndex = 0;
int wifiIndex = 0;
int bleIndex = 0;
int webIndex = 0;
int deviceIndex = 0;

const char* mainMenu[] = {
  "Wi-Fi Tools",
  "BLE Tools",
  "Web Server",
  "Device Util"
};
const int MAIN_MENU_COUNT = 4;

const char* wifiMenu[] = {
  "WiFi Scan",
  "Visualizer",
  "Channel Map",
  "Signal Mon",
  "Open Nets",
  "AP Mode",
  "Net Info",
  "Ping Test",
  "Internet",
  "HTTP Test",
  "Heat Walk",
  "BACK"
};
const int WIFI_MENU_COUNT = 12;

const char* bleMenu[] = {
  "BLE Scan",
  "Proximity",
  "Beacon Det",
  "Signal Trk",
  "Svc Viewer",
  "Advertise",
  "BLE UART",
  "BACK"
};
const int BLE_MENU_COUNT = 8;

const char* webMenu[] = {
  "Start Server",
  "Info",
  "Captive Pg",
  "Control Pnl",
  "Live Monitor",
  "BACK"
};
const int WEB_MENU_COUNT = 6;

const char* deviceMenu[] = {
  "Device Stat",
  "Settings",
  "BACK"
};
const int DEVICE_MENU_COUNT = 3;

ModeID wifiModes[] = {
  MODE_WIFI_SCANNER,
  MODE_WIFI_VISUALIZER,
  MODE_WIFI_CHANNEL_ANALYZER,
  MODE_WIFI_SIGNAL_MONITOR,
  MODE_WIFI_OPEN_NETWORKS,
  MODE_WIFI_AP_MODE,
  MODE_WIFI_NETWORK_INFO,
  MODE_WIFI_PING_TEST,
  MODE_WIFI_INTERNET_CHECK,
  MODE_WIFI_WEB_REQUEST_TEST,
  MODE_WIFI_HEAT_WALK,
  MODE_NONE
};

ModeID bleModes[] = {
  MODE_BLE_SCANNER,
  MODE_BLE_PROXIMITY,
  MODE_BLE_BEACON_DETECTOR,
  MODE_BLE_SIGNAL_TRACKER,
  MODE_BLE_SERVICE_VIEWER,
  MODE_BLE_ADVERTISER,
  MODE_BLE_UART,
  MODE_NONE
};

ModeID webModes[] = {
  MODE_WEB_START_SERVER,
  MODE_WEB_INFO,
  MODE_WEB_CAPTIVE_PORTAL,
  MODE_WEB_CONTROL_PANEL,
  MODE_WEB_LIVE_MONITOR,
  MODE_NONE
};

ModeID deviceModes[] = {
  MODE_DEVICE_STATUS,
  MODE_DEVICE_SETTINGS,
  MODE_NONE
};

// --------------------------------------------------
// Splash screen with Wi-Fi symbol
// --------------------------------------------------
void drawWiFiIcon(int x, int y, int step) {
  u8g2.drawDisc(x, y, 2);
  if (step >= 1) u8g2.drawCircle(x, y, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  if (step >= 2) u8g2.drawCircle(x, y, 13, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  if (step >= 3) u8g2.drawCircle(x, y, 19, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
}

void splashScreen() {
  unsigned long start = millis();
  int frame = 0;

  while (millis() - start < 3000) {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(1, 18, "NetViewer");
    u8g2.drawStr(1, 34, "------------------");

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 52, "WiFi BLE Web");

    drawWiFiIcon(32, 88, frame % 4);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(4, 122, "------------------------");

    u8g2.sendBuffer();
    frame++;
    delay(150);
  }
}

// --------------------------------------------------
// Wi-Fi data
// --------------------------------------------------
#define MAX_WIFI_RESULTS 40
struct WiFiEntry {
  String ssid;
  int32_t rssi;
  int32_t channel;
  wifi_auth_mode_t encryption;
  bool hidden;
};

WiFiEntry wifiResults[MAX_WIFI_RESULTS];
int wifiCount = 0;
int selectedWiFi = 0;
unsigned long lastWiFiScanTime = 0;

#define MAX_HEAT_POINTS 24
int heatPoints[MAX_HEAT_POINTS];
int heatPointCount = 0;

String encryptionToString(wifi_auth_mode_t encryption) {
  switch (encryption) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/W2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "W2/W3";
    default: return "UNK";
  }
}

void sortWiFiByRSSI() {
  for (int i = 0; i < wifiCount - 1; i++) {
    for (int j = i + 1; j < wifiCount; j++) {
      if (wifiResults[j].rssi > wifiResults[i].rssi) {
        WiFiEntry temp = wifiResults[i];
        wifiResults[i] = wifiResults[j];
        wifiResults[j] = temp;
      }
    }
  }
}

void performWiFiScan() {
  showMessage("WiFi Scan", "Scanning...", "please wait");

  WiFi.mode(WIFI_AP_STA);
  delay(100);

  int n = WiFi.scanNetworks(false, true);
  wifiCount = min(n, MAX_WIFI_RESULTS);

  for (int i = 0; i < wifiCount; i++) {
    wifiResults[i].ssid = WiFi.SSID(i);
    wifiResults[i].rssi = WiFi.RSSI(i);
    wifiResults[i].channel = WiFi.channel(i);
    wifiResults[i].encryption = WiFi.encryptionType(i);
    wifiResults[i].hidden = wifiResults[i].ssid.length() == 0;
  }

  sortWiFiByRSSI();
  if (selectedWiFi >= wifiCount) selectedWiFi = 0;
  lastWiFiScanTime = millis();
  waitForButtonsReleased();
}

bool wifiModeNeedsScan(int m) {
  return (
    m == MODE_WIFI_SCANNER ||
    m == MODE_WIFI_VISUALIZER ||
    m == MODE_WIFI_CHANNEL_ANALYZER ||
    m == MODE_WIFI_SIGNAL_MONITOR ||
    m == MODE_WIFI_OPEN_NETWORKS ||
    m == MODE_WIFI_HEAT_WALK
  );
}

// --------------------------------------------------
// BLE data and callbacks
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
unsigned long lastBLEScanTime = 0;

// Tracker stats
bool trackerSeen = false;
int trackerCurrentRSSI = -127;
int trackerMinRSSI = 0;
int trackerMaxRSSI = -127;
long trackerSumRSSI = 0;
int trackerSamples = 0;
unsigned long lastTrackerRefresh = 0;

// Service viewer
int serviceViewerPage = 0;
const int SERVICE_VIEWER_PAGE_COUNT = 3;

String rssiToProximity(int rssi) {
  if (rssi >= -55) return "NEAR";
  if (rssi >= -72) return "MEDIUM";
  return "FAR";
}

String knownServiceName(String uuid) {
  uuid.toUpperCase();
  if (uuid.indexOf("180F") >= 0) return "Battery";
  if (uuid.indexOf("180A") >= 0) return "Device Info";
  if (uuid.indexOf("1812") >= 0) return "HID";
  if (uuid.indexOf("180D") >= 0) return "Heart Rate";
  if (uuid.indexOf("181A") >= 0) return "Env Sense";
  if (uuid.indexOf("6E400001") >= 0) return "BLE UART";
  return "Unknown";
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (bleCount >= MAX_BLE_RESULTS) return;

    BLEEntry &e = bleResults[bleCount];
    e.name = advertisedDevice.haveName() ? String(advertisedDevice.getName().c_str()) : "<unknown>";
    e.address = String(advertisedDevice.getAddress().toString().c_str());
    e.rssi = advertisedDevice.getRSSI();

    if (advertisedDevice.haveServiceUUID()) {
      e.serviceUUID = String(advertisedDevice.getServiceUUID().toString().c_str());
      e.hasServiceUUID = true;
    } else {
      e.serviceUUID = "-";
      e.hasServiceUUID = false;
    }

    e.hasManufacturerData = advertisedDevice.haveManufacturerData();
    e.connectableGuess = e.hasServiceUUID;
    bleCount++;
  }
};

MyAdvertisedDeviceCallbacks scanCallbacks;

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
  lastBLEScanTime = millis();
  waitForButtonsReleased();
}

bool bleModeNeedsScan(int m) {
  return (
    m == MODE_BLE_SCANNER ||
    m == MODE_BLE_PROXIMITY ||
    m == MODE_BLE_BEACON_DETECTOR ||
    m == MODE_BLE_SIGNAL_TRACKER ||
    m == MODE_BLE_SERVICE_VIEWER
  );
}

// Tracker refresh callback
String trackerTargetAddress = "";
class TrackerScanCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String addr = String(advertisedDevice.getAddress().toString().c_str());
    if (addr == trackerTargetAddress) {
      trackerSeen = true;
      trackerCurrentRSSI = advertisedDevice.getRSSI();

      if (trackerSamples == 0) {
        trackerMinRSSI = trackerCurrentRSSI;
        trackerMaxRSSI = trackerCurrentRSSI;
      }

      if (trackerCurrentRSSI < trackerMinRSSI) trackerMinRSSI = trackerCurrentRSSI;
      if (trackerCurrentRSSI > trackerMaxRSSI) trackerMaxRSSI = trackerCurrentRSSI;

      trackerSumRSSI += trackerCurrentRSSI;
      trackerSamples++;
    }
  }
};
TrackerScanCallbacks trackerScanCallbacks;

void resetTrackerStats() {
  trackerSeen = false;
  trackerCurrentRSSI = -127;
  trackerMinRSSI = 0;
  trackerMaxRSSI = -127;
  trackerSumRSSI = 0;
  trackerSamples = 0;
}

void refreshTrackerTarget() {
  if (bleCount <= 0) return;
  if (millis() - lastTrackerRefresh < 2500) return;

  lastTrackerRefresh = millis();
  trackerSeen = false;
  trackerTargetAddress = bleResults[selectedBLE].address;

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(&trackerScanCallbacks, true);
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(80);
  pBLEScan->setWindow(60);
  pBLEScan->start(1, false);
  pBLEScan->clearResults();
}

// BLE server callbacks
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { bleClientConnected = true; }
  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
    BLEDevice::startAdvertising();
  }
};

class UartRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    String value = pCharacteristic->getValue();
    if (value.length() > 0) bleUartLastRx = value;
  }
};

MyServerCallbacks serverCallbacks;
UartRxCallbacks uartRxCallbacks;

void startBLEAdvertiserMode() {
  if (bleAdvertiserStarted) return;

  BLEDevice::deinit(false);
  delay(100);
  BLEDevice::init(BLE_DEVICE_NAME);

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
  if (bleUARTStarted) return;

  BLEDevice::deinit(false);
  delay(100);
  BLEDevice::init(BLE_DEVICE_NAME);

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
  if (!bleUARTStarted || !bleClientConnected || uartTxCharacteristic == nullptr) return;
  if (millis() - lastUARTNotify < 1500) return;

  lastUARTNotify = millis();
  String msg = "NetViewer BLE UART uptime=" + String(millis() / 1000) + "s";
  uartTxCharacteristic->setValue(msg.c_str());
  uartTxCharacteristic->notify();
}

// --------------------------------------------------
// Device / settings
// --------------------------------------------------
int statusPage = 0;
const int STATUS_PAGE_COUNT = 4;
int settingIndex = 0;
const int SETTING_COUNT = 4;

int brightnessIndex = 2;
const int brightnessValues[] = {40, 90, 160, 255};
const char* brightnessNames[] = {"Low", "Med", "High", "Max"};
bool autoRefresh = true;
bool displayInvert = false;
int sleepTimeoutIndex = 0;
const char* sleepTimeoutNames[] = {"Off", "30s", "60s", "120s"};

String formatUptime() {
  unsigned long seconds = millis() / 1000;
  unsigned long h = seconds / 3600;
  unsigned long m = (seconds % 3600) / 60;
  unsigned long s = seconds % 60;

  String out = "";
  if (h < 10) out += "0";
  out += String(h) + ":";
  if (m < 10) out += "0";
  out += String(m) + ":";
  if (s < 10) out += "0";
  out += String(s);
  return out;
}

String getChipModelString() {
  return String(ESP.getChipModel());
}

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

void changeCurrentSetting() {
  if (settingIndex == 0) {
    brightnessIndex++;
    if (brightnessIndex >= 4) brightnessIndex = 0;
    u8g2.setContrast(brightnessValues[brightnessIndex]);
  } else if (settingIndex == 1) {
    autoRefresh = !autoRefresh;
  } else if (settingIndex == 2) {
    displayInvert = !displayInvert;
    // Placeholder flag for future full-screen invert support.
  } else if (settingIndex == 3) {
    sleepTimeoutIndex++;
    if (sleepTimeoutIndex >= 4) sleepTimeoutIndex = 0;
  }
}

// --------------------------------------------------
// Web pages and server
// --------------------------------------------------
String currentWebMode = "None";
String oledMessage = "Ready";
bool testFlag = false;
int controlCounter = 0;


// --------------------------------------------------
// Saved Wi-Fi credentials helpers
// --------------------------------------------------
void loadSavedWiFiCredentials() {
  prefs.begin("wifi", true);
  savedStaSSID = prefs.getString("ssid", "");
  savedStaPASS = prefs.getString("pass", "");
  prefs.end();
}

void saveWiFiCredentials(String ssid, String pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  savedStaSSID = ssid;
  savedStaPASS = pass;
}

void clearWiFiCredentials() {
  prefs.begin("wifi", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();

  savedStaSSID = "";
  savedStaPASS = "";
}

String activeStaSSID() {
  if (savedStaSSID.length() > 0) {
    return savedStaSSID;
  }

  return String(STA_SSID);
}

String activeStaPASS() {
  if (savedStaSSID.length() > 0) {
    return savedStaPASS;
  }

  return String(STA_PASS);
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

String htmlHeader(String title) {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>" + title + "</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:#eee;margin:20px;}";
  html += ".card{background:#1d1d1d;border:1px solid #444;border-radius:12px;padding:14px;margin:12px 0;}";
  html += "a,button{background:#222;color:#7cf;border:1px solid #555;border-radius:8px;padding:10px;margin:5px;display:inline-block;text-decoration:none;}";
  html += "table{border-collapse:collapse;width:100%;}td,th{border:1px solid #555;padding:6px;text-align:left;}";
  html += "</style></head><body><h1>NetViewer Web</h1>";
  return html;
}

String navLinks() {
  return "<p><a href='/'>Dashboard</a><a href='/wifi'>Wi-Fi Setup</a><a href='/control'>Control</a><a href='/live'>Live</a><a href='/info'>Info</a><a href='/status'>JSON</a></p>";
}

String htmlFooter() {
  return "<div class='card'><small>NetViewer_AP @ 192.168.4.1</small></div></body></html>";
}

String statusJSON() {
  String json = "{";
  json += "\"device\":\"NetViewer\",";
  json += "\"mode\":\"" + currentWebMode + "\",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"wifiCount\":" + String(wifiCount) + ",";
  json += "\"bleCount\":" + String(bleCount) + ",";
  json += "\"apIP\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"testFlag\":" + String(testFlag ? "true" : "false") + ",";
  json += "\"counter\":" + String(controlCounter) + ",";
  json += "\"staConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"staSSID\":\"" + activeStaSSID() + "\",";
  json += "\"staIP\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"oledMessage\":\"" + oledMessage + "\"";
  json += "}";
  return json;
}

void handleRoot() {
  String html = htmlHeader("NetViewer Dashboard");
  html += navLinks();
  html += "<div class='card'><h2>Server Dashboard</h2><table>";
  html += "<tr><td>AP SSID</td><td>" + String(AP_SSID) + "</td></tr>";
  html += "<tr><td>AP IP</td><td>" + WiFi.softAPIP().toString() + "</td></tr>";
  html += "<tr><td>STA Wi-Fi</td><td>" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Not connected") + "</td></tr>";
  html += "<tr><td>STA SSID</td><td>" + htmlEscape(activeStaSSID()) + "</td></tr>";
  html += "<tr><td>STA IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " sec</td></tr>";
  html += "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
  html += "<tr><td>Clients</td><td>" + String(WiFi.softAPgetStationNum()) + "</td></tr>";
  html += "<tr><td>Wi-Fi APs</td><td>" + String(wifiCount) + "</td></tr>";
  html += "<tr><td>BLE Devices</td><td>" + String(bleCount) + "</td></tr>";
  html += "<tr><td>OLED Message</td><td>" + oledMessage + "</td></tr>";
  html += "</table></div>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleInfo() {
  String html = htmlHeader("NetViewer Info");
  html += navLinks();
  html += "<div class='card'><h2>Captive Portal Info</h2><p>Connect to <b>";
  html += AP_SSID;
  html += "</b>, password <b>";
  html += AP_PASS;
  html += "</b>, then open <b>192.168.4.1</b>.</p></div>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleWiFiSetup() {
  String html = htmlHeader("NetViewer Wi-Fi Setup");
  html += navLinks();

  html += "<div class='card'><h2>Wi-Fi Setup</h2>";
  html += "<p>Enter local 2.4 GHz Wi-Fi credentials here. The ESP32 will save them to flash memory and restart.</p>";
  html += "<form action='/savewifi' method='POST'>";
  html += "<label>SSID</label><br>";
  html += "<input name='ssid' value='" + htmlEscape(activeStaSSID()) + "' style='width:95%;padding:10px;margin:6px 0;'><br>";
  html += "<label>Password</label><br>";
  html += "<input name='pass' type='password' style='width:95%;padding:10px;margin:6px 0;'><br>";
  html += "<button type='submit'>Save Wi-Fi and Restart</button>";
  html += "</form>";
  html += "</div>";

  html += "<div class='card'><h2>Current STA Status</h2><table>";
  html += "<tr><td>Status</td><td>" + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Not connected") + "</td></tr>";
  html += "<tr><td>Saved SSID</td><td>" + htmlEscape(activeStaSSID()) + "</td></tr>";
  html += "<tr><td>STA IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
  String staRssiText = WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "-";
  html += "<tr><td>RSSI</td><td>" + staRssiText + "</td></tr>";
  html += "</table>";
  html += "<p><a href='/clearwifi'>Clear Saved Wi-Fi</a></p>";
  html += "</div>";

  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleSaveWiFi() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Missing SSID");
    return;
  }

  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  ssid.trim();

  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID cannot be blank");
    return;
  }

  saveWiFiCredentials(ssid, pass);

  String html = htmlHeader("Wi-Fi Saved");
  html += "<div class='card'><h2>Wi-Fi Saved</h2>";
  html += "<p>Saved SSID: <b>" + htmlEscape(ssid) + "</b></p>";
  html += "<p>The ESP32 will restart and try to connect using the saved credentials.</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
  delay(1200);
  ESP.restart();
}

void handleClearWiFi() {
  clearWiFiCredentials();

  String html = htmlHeader("Wi-Fi Cleared");
  html += "<div class='card'><h2>Saved Wi-Fi Cleared</h2>";
  html += "<p>The ESP32 will restart. You can reconnect to NetViewer_AP and enter new credentials.</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
  delay(1200);
  ESP.restart();
}

void handleControlPanel() {
  String html = htmlHeader("NetViewer Control");
  html += navLinks();
  html += "<div class='card'><h2>Control Panel</h2>";
  html += "<p><a href='/toggle'>Toggle Test Flag</a></p>";
  html += "<p><a href='/count'>Increment Counter</a></p>";
  html += "<p><a href='/msgready'>OLED: Ready</a></p>";
  html += "<p><a href='/msgweb'>OLED: Web OK</a></p>";
  html += "<p><a href='/msgctrl'>OLED: Control</a></p>";
  html += "<p><a href='/restart' onclick=\"return confirm('Restart ESP32?')\">Restart ESP32</a></p>";
  html += "</div><div class='card'><table>";
  html += "<tr><td>Test Flag</td><td>" + String(testFlag ? "ON" : "OFF") + "</td></tr>";
  html += "<tr><td>Counter</td><td>" + String(controlCounter) + "</td></tr>";
  html += "<tr><td>OLED Message</td><td>" + oledMessage + "</td></tr>";
  html += "</table></div>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleLiveMonitor() {
  String html = htmlHeader("NetViewer Live");
  html += navLinks();
  html += "<div class='card'><h2>Live Monitor</h2><pre id='out'>Loading...</pre></div>";
  html += "<script>";
  html += "async function update(){try{let r=await fetch('/status');let j=await r.json();document.getElementById('out').textContent=JSON.stringify(j,null,2);}catch(e){document.getElementById('out').textContent='Update failed: '+e;}}";
  html += "setInterval(update,1000);update();";
  html += "</script>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleToggle() { testFlag = !testFlag; oledMessage = testFlag ? "Flag ON" : "Flag OFF"; server.sendHeader("Location", "/control"); server.send(303); }
void handleCount() { controlCounter++; oledMessage = "Count " + String(controlCounter); server.sendHeader("Location", "/control"); server.send(303); }
void handleSetMsgReady() { oledMessage = "Ready"; server.sendHeader("Location", "/control"); server.send(303); }
void handleSetMsgWeb() { oledMessage = "Web OK"; server.sendHeader("Location", "/control"); server.send(303); }
void handleSetMsgCtrl() { oledMessage = "Control"; server.sendHeader("Location", "/control"); server.send(303); }
void handleRestart() { server.send(200, "text/html", "<html><body><h1>Restarting...</h1></body></html>"); delay(500); ESP.restart(); }
void handleStatus() { server.send(200, "application/json", statusJSON()); }
void handleNotFound() { server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true); server.send(302, "text/plain", ""); }

void startAP() {
  if (apStarted) return;
  showMessage("AP Mode", "Starting AP", AP_SSID);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, gatewayIP, subnetMask);
  WiFi.softAP(AP_SSID, AP_PASS);
  apStarted = true;
  delay(500);
}

void startWebServer() {
  if (serverStarted) return;

  startAP();
  showMessage("Web Server", "Starting", "routes");

  server.on("/", handleRoot);
  server.on("/info", handleInfo);
  server.on("/wifi", handleWiFiSetup);
  server.on("/savewifi", HTTP_POST, handleSaveWiFi);
  server.on("/clearwifi", handleClearWiFi);
  server.on("/control", handleControlPanel);
  server.on("/live", handleLiveMonitor);
  server.on("/status", handleStatus);
  server.on("/toggle", handleToggle);
  server.on("/count", handleCount);
  server.on("/msgready", handleSetMsgReady);
  server.on("/msgweb", handleSetMsgWeb);
  server.on("/msgctrl", handleSetMsgCtrl);
  server.on("/restart", handleRestart);
  server.onNotFound(handleNotFound);

  server.begin();
  serverStarted = true;
  delay(500);
  waitForButtonsReleased();
}

void startCaptiveDNS() {
  if (captiveStarted) return;
  startWebServer();
  showMessage("Captive", "Starting DNS", "redirect");
  dnsServer.start(53, "*", WiFi.softAPIP());
  captiveStarted = true;
  delay(500);
  waitForButtonsReleased();
}

// --------------------------------------------------
// Draw generic menus
// --------------------------------------------------
void drawMenuList(const char* title, const char* items[], int count, int selected) {
  u8g2.clearBuffer();
  drawHeader(title);
  u8g2.setFont(u8g2_font_5x7_tf);

  int visible = 8;
  int first = selected - 3;
  if (first < 0) first = 0;
  if (first > count - visible) first = max(0, count - visible);

  for (int i = 0; i < visible; i++) {
    int idx = first + i;
    if (idx >= count) break;

    int y = 22 + i * 11;
    if (idx == selected) {
      u8g2.drawBox(0, y - 8, SCREEN_W, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(1, y, fitText(String(items[idx]), 11).c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(1, y, fitText(String(items[idx]), 11).c_str());
    }
  }

  drawFooter("UP DN SEL");
  u8g2.sendBuffer();
}

void drawMainMenu() {
  drawMenuList("NetViewer", mainMenu, MAIN_MENU_COUNT, mainIndex);
}

void drawCurrentSubmenu() {
  if (screenState == SCREEN_WIFI_MENU) drawMenuList("Wi-Fi Tools", wifiMenu, WIFI_MENU_COUNT, wifiIndex);
  else if (screenState == SCREEN_BLE_MENU) drawMenuList("BLE Tools", bleMenu, BLE_MENU_COUNT, bleIndex);
  else if (screenState == SCREEN_WEB_MENU) drawMenuList("Web Tools", webMenu, WEB_MENU_COUNT, webIndex);
  else if (screenState == SCREEN_DEVICE_MENU) drawMenuList("Device Util", deviceMenu, DEVICE_MENU_COUNT, deviceIndex);
}

// --------------------------------------------------
// Wi-Fi mode screens
// --------------------------------------------------
void drawWiFiScanner() {
  u8g2.clearBuffer();
  drawHeader("WiFi Scan");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 30, "No results");
  } else {
    WiFiEntry &e = wifiResults[selectedWiFi];
    u8g2.drawStr(0, 22, ("AP " + String(selectedWiFi + 1)).c_str());
    u8g2.drawStr(32, 22, ("/" + String(wifiCount)).c_str());
    u8g2.drawStr(0, 38, "SSID:");
    u8g2.drawStr(0, 50, fitText(e.hidden ? "<hidden>" : e.ssid, 11).c_str());
    u8g2.drawStr(0, 66, ("R:" + String(e.rssi)).c_str());
    u8g2.drawStr(34, 66, ("C:" + String(e.channel)).c_str());
    u8g2.drawStr(0, 82, encryptionToString(e.encryption).c_str());
    drawSignalBar(96, rssiToQuality(e.rssi));
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

int channelToX(int ch) {
  ch = constrain(ch, 1, 11);
  return map(ch, 1, 11, 3, 60);
}

void drawNetworkHump(int ch, int rssi, bool selected) {
  int cx = channelToX(ch);
  int quality = rssiToQuality(rssi);
  int h = map(quality, 0, 100, 8, 58);
  int baseY = 104;
  int topY = baseY - h;
  int halfW = 9;

  for (int dx = -halfW; dx <= halfW; dx++) {
    float norm = abs(dx) / (float)halfW;
    int y = topY + (int)((baseY - topY) * norm * norm);
    int x = cx + dx;
    if (x >= 0 && x < SCREEN_W) {
      u8g2.drawPixel(x, y);
      if (selected && y + 1 < SCREEN_H) u8g2.drawPixel(x, y + 1);
    }
  }

  if (selected) {
    u8g2.drawVLine(cx, topY, baseY - topY);
  }
}

void drawWiFiVisualizer() {
  u8g2.clearBuffer();
  drawHeader("Visualizer");
  u8g2.setFont(u8g2_font_4x6_tf);

  if (wifiCount <= 0) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 34, "No scan");
    u8g2.drawStr(0, 48, "data yet");
  } else {
    u8g2.drawStr(0, 19, fitText(wifiResults[selectedWiFi].hidden ? "<hidden>" : wifiResults[selectedWiFi].ssid, 14).c_str());
    u8g2.drawStr(0, 29, ("CH" + String(wifiResults[selectedWiFi].channel) + " " + String(wifiResults[selectedWiFi].rssi)).c_str());

    // Draw strongest networks first, up to 10
    int maxDraw = min(wifiCount, 10);
    for (int i = maxDraw - 1; i >= 0; i--) {
      drawNetworkHump(wifiResults[i].channel, wifiResults[i].rssi, i == selectedWiFi);
    }

    u8g2.drawHLine(0, 104, 64);
    u8g2.drawStr(0, 115, "1");
    u8g2.drawStr(29, 115, "6");
    u8g2.drawStr(54, 115, "11");
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawWiFiChannelAnalyzer() {
  int counts[15] = {0};
  for (int i = 0; i < wifiCount; i++) {
    int ch = wifiResults[i].channel;
    if (ch >= 1 && ch <= 14) counts[ch]++;
  }

  u8g2.clearBuffer();
  drawHeader("Channel");
  u8g2.setFont(u8g2_font_5x7_tf);

  int channels[] = {1, 6, 11};
  for (int i = 0; i < 3; i++) {
    int ch = channels[i];
    int y = 27 + i * 27;
    u8g2.drawStr(0, y, ("CH " + String(ch)).c_str());
    u8g2.drawStr(45, y, String(counts[ch]).c_str());
    int barW = min(counts[ch] * 8, 60);
    u8g2.drawFrame(0, y + 6, 62, 8);
    u8g2.drawBox(1, y + 7, barW, 6);
  }
  u8g2.drawStr(0, 108, ("APs:" + String(wifiCount)).c_str());
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWiFiSignalMonitor() {
  u8g2.clearBuffer();
  drawHeader("Signal Mon");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 34, "No scan");
  } else {
    WiFiEntry &e = wifiResults[selectedWiFi];
    int q = rssiToQuality(e.rssi);
    u8g2.drawStr(0, 24, fitText(e.hidden ? "<hidden>" : e.ssid, 11).c_str());
    u8g2.drawStr(0, 42, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 56, ("Q:" + String(q) + "%").c_str());
    drawSignalBar(72, q);
    u8g2.drawStr(0, 96, "Last scan");
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawWiFiOpenNetworks() {
  int openIndexes[MAX_WIFI_RESULTS];
  int openCount = 0;
  for (int i = 0; i < wifiCount; i++) {
    if (wifiResults[i].encryption == WIFI_AUTH_OPEN) openIndexes[openCount++] = i;
  }

  u8g2.clearBuffer();
  drawHeader("Open Nets");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (openCount <= 0) {
    u8g2.drawStr(0, 34, "No open");
    u8g2.drawStr(0, 48, "networks");
  } else {
    int localIndex = selectedWiFi % openCount;
    int idx = openIndexes[localIndex];
    WiFiEntry &e = wifiResults[idx];
    u8g2.drawStr(0, 24, ("Open " + String(localIndex + 1)).c_str());
    u8g2.drawStr(36, 24, ("/" + String(openCount)).c_str());
    u8g2.drawStr(0, 42, fitText(e.hidden ? "<hidden>" : e.ssid, 11).c_str());
    u8g2.drawStr(0, 58, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 72, ("CH:" + String(e.channel)).c_str());
    drawSignalBar(88, rssiToQuality(e.rssi));
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawWiFiAPMode() {
  currentWebMode = "WiFi AP Mode";
  startWebServer();

  u8g2.clearBuffer();
  drawHeader("AP Mode");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 24, "AP Active");
  u8g2.drawStr(0, 42, "SSID:");
  u8g2.drawStr(0, 54, fitText(String(AP_SSID), 11).c_str());
  u8g2.drawStr(0, 74, "IP:");
  u8g2.drawStr(0, 86, "192.168.4.1");
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWiFiNetworkInfo() {
  u8g2.clearBuffer();
  drawHeader("Net Info");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(0, 22, "STA conn");
    u8g2.drawStr(0, 36, fitText(WiFi.SSID(), 11).c_str());
    u8g2.drawStr(0, 54, "IP:");
    u8g2.drawStr(0, 66, fitText(WiFi.localIP().toString(), 12).c_str());
    u8g2.drawStr(0, 84, ("RSSI:" + String(WiFi.RSSI())).c_str());
  } else {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
    u8g2.drawStr(0, 56, "credentials");
    u8g2.drawStr(0, 78, "AP IP:");
    u8g2.drawStr(0, 92, WiFi.softAPIP().toString().c_str());
  }

  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWiFiPingTest() {
  u8g2.clearBuffer();
  drawHeader("Ping Test");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
  } else {
    u8g2.drawStr(0, 28, "Press DN");
    u8g2.drawStr(0, 42, "to run");
    u8g2.drawStr(0, 62, fitText(String(PING_TARGET), 12).c_str());
  }
  drawFooter("DN Run MENU");
  u8g2.sendBuffer();
}

void runPingNow() {
  showMessage("Ping Test", "Running", PING_TARGET);
  bool ok = Ping.ping(PING_TARGET, 3);
  u8g2.clearBuffer();
  drawHeader("Ping Test");
  u8g2.setFont(u8g2_font_5x7_tf);
  if (ok) {
    u8g2.drawStr(0, 30, "Status: OK");
    u8g2.drawStr(0, 48, ("Avg:" + String(Ping.averageTime())).c_str());
    u8g2.drawStr(0, 62, "ms");
  } else {
    u8g2.drawStr(0, 36, "Ping fail");
  }
  drawFooter("MENU Back");
  u8g2.sendBuffer();
  waitForButtonsReleased();
}

void drawWiFiInternetCheck() {
  u8g2.clearBuffer();
  drawHeader("Internet");
  u8g2.setFont(u8g2_font_5x7_tf);
  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
  } else {
    u8g2.drawStr(0, 28, "Press DN");
    u8g2.drawStr(0, 42, "to check");
    u8g2.drawStr(0, 62, fitText(String(INTERNET_TEST_HOST), 12).c_str());
  }
  drawFooter("DN Run MENU");
  u8g2.sendBuffer();
}

void runInternetNow() {
  showMessage("Internet", "Checking", INTERNET_TEST_HOST);
  bool ok = Ping.ping(INTERNET_TEST_HOST, 2);
  u8g2.clearBuffer();
  drawHeader("Internet");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 30, ok ? "Internet" : "No net");
  u8g2.drawStr(0, 46, ok ? "OK" : "or DNS");
  drawFooter("MENU Back");
  u8g2.sendBuffer();
  waitForButtonsReleased();
}

void drawWiFiWebRequestTest() {
  u8g2.clearBuffer();
  drawHeader("HTTP Test");
  u8g2.setFont(u8g2_font_5x7_tf);
  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
  } else {
    u8g2.drawStr(0, 28, "Press DN");
    u8g2.drawStr(0, 42, "to GET");
    u8g2.drawStr(0, 62, "example.com");
  }
  drawFooter("DN Run MENU");
  u8g2.sendBuffer();
}

void runHTTPNow() {
  showMessage("HTTP Test", "GET", "example.com");
  HTTPClient http;
  unsigned long start = millis();
  http.begin(HTTP_TEST_URL);
  int code = http.GET();
  unsigned long elapsed = millis() - start;
  http.end();

  u8g2.clearBuffer();
  drawHeader("HTTP Test");
  u8g2.setFont(u8g2_font_5x7_tf);
  if (code > 0) {
    u8g2.drawStr(0, 30, ("Code:" + String(code)).c_str());
    u8g2.drawStr(0, 48, ("Time:" + String(elapsed)).c_str());
    u8g2.drawStr(0, 62, "ms");
  } else {
    u8g2.drawStr(0, 36, "Request");
    u8g2.drawStr(0, 50, "failed");
  }
  drawFooter("MENU Back");
  u8g2.sendBuffer();
  waitForButtonsReleased();
}

void drawWiFiHeatWalk() {
  u8g2.clearBuffer();
  drawHeader("Heat Walk");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 30, "No scan");
  } else {
    WiFiEntry &e = wifiResults[selectedWiFi];
    u8g2.drawStr(0, 22, "Target:");
    u8g2.drawStr(0, 34, fitText(e.hidden ? "<hidden>" : e.ssid, 11).c_str());
    u8g2.drawStr(0, 52, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 68, ("Pts:" + String(heatPointCount)).c_str());
    if (heatPointCount > 0) u8g2.drawStr(0, 84, ("Last:" + String(heatPoints[heatPointCount - 1])).c_str());
    u8g2.drawStr(0, 102, "DN save");
    u8g2.drawStr(0, 113, "UP target");
  }
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
    u8g2.drawStr(0, 60, "Range:");
    u8g2.drawStr(0, 74, rssiToProximity(e.rssi).c_str());
    drawSignalBar(90, q);
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawBLEBeaconDetector() {
  int beaconIndexes[MAX_BLE_RESULTS];
  int beaconCount = 0;
  for (int i = 0; i < bleCount; i++) if (bleResults[i].hasManufacturerData) beaconIndexes[beaconCount++] = i;

  u8g2.clearBuffer();
  drawHeader("Beacon Det");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (beaconCount <= 0) {
    u8g2.drawStr(0, 30, "No beacon");
    u8g2.drawStr(0, 44, "like adv");
    u8g2.drawStr(0, 68, "MFG data");
    u8g2.drawStr(0, 82, "not found");
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
  if (bleCount > 0) refreshTrackerTarget();

  u8g2.clearBuffer();
  drawHeader("Signal Trk");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (bleCount <= 0) {
    u8g2.drawStr(0, 34, "Scan first");
  } else {
    BLEEntry &e = bleResults[selectedBLE];
    int shownRSSI = trackerSamples > 0 ? trackerCurrentRSSI : e.rssi;
    int avg = trackerSamples > 0 ? trackerSumRSSI / trackerSamples : e.rssi;
    u8g2.drawStr(0, 18, fitText(e.name, 11).c_str());
    u8g2.drawStr(0, 32, trackerSeen ? "Seen: yes" : "Seen: no");
    u8g2.drawStr(0, 46, ("Now:" + String(shownRSSI)).c_str());
    u8g2.drawStr(0, 60, ("Avg:" + String(avg)).c_str());
    u8g2.drawStr(0, 74, ("Min:" + String(trackerMinRSSI)).c_str());
    u8g2.drawStr(0, 88, ("Max:" + String(trackerMaxRSSI)).c_str());
    drawSignalBar(102, rssiToQuality(shownRSSI));
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
        u8g2.drawStr(0, 84, knownServiceName(e.serviceUUID).c_str());
      } else {
        u8g2.drawStr(0, 50, "None adv");
      }
    } else if (serviceViewerPage == 1) {
      u8g2.drawStr(0, 22, "Adv flags");
      u8g2.drawStr(0, 40, e.hasServiceUUID ? "UUID: yes" : "UUID: no");
      u8g2.drawStr(0, 54, e.hasManufacturerData ? "MFG: yes" : "MFG: no");
      u8g2.drawStr(0, 68, e.connectableGuess ? "Conn: maybe" : "Conn: unk");
      u8g2.drawStr(0, 84, ("RSSI:" + String(e.rssi)).c_str());
      drawSignalBar(98, rssiToQuality(e.rssi));
    } else {
      u8g2.drawStr(0, 22, "Identity");
      u8g2.drawStr(0, 38, fitText(e.name, 11).c_str());
      u8g2.drawStr(0, 54, fitText(e.address, 12).c_str());
      u8g2.drawStr(0, 70, fitText(e.address.substring(12), 12).c_str());
      u8g2.drawStr(0, 92, ("Dev " + String(selectedBLE + 1) + "/" + String(bleCount)).c_str());
    }
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
  u8g2.drawStr(0, 54, fitText(String(BLE_DEVICE_NAME), 12).c_str());
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
  u8g2.drawStr(0, 52, fitText(String(BLE_DEVICE_NAME), 12).c_str());
  u8g2.drawStr(0, 70, "Last RX:");
  u8g2.drawStr(0, 84, fitText(bleUartLastRx, 12).c_str());
  u8g2.drawStr(0, 104, "nRF Connect");
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Web mode screens
// --------------------------------------------------
void drawWebStartServer() {
  currentWebMode = "Start Server";
  startWebServer();

  u8g2.clearBuffer();
  drawHeader("Start Server");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 22, "Server ON");
  u8g2.drawStr(0, 38, "SSID:");
  u8g2.drawStr(0, 50, fitText(String(AP_SSID), 11).c_str());
  u8g2.drawStr(0, 68, "IP:");
  u8g2.drawStr(0, 80, "192.168.4.1");
  u8g2.drawStr(0, 100, ("Cli:" + String(WiFi.softAPgetStationNum())).c_str());
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWebInfo() {
  currentWebMode = "Info";
  startWebServer();

  u8g2.clearBuffer();
  drawHeader("Info");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 22, "Connect to:");
  u8g2.drawStr(0, 36, fitText(String(AP_SSID), 11).c_str());
  u8g2.drawStr(0, 56, "Password:");
  u8g2.drawStr(0, 70, fitText(String(AP_PASS), 11).c_str());
  u8g2.drawStr(0, 92, "Open:");
  u8g2.drawStr(0, 106, "192.168.4.1");
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWebCaptive() {
  currentWebMode = "Captive Portal";
  startCaptiveDNS();

  u8g2.clearBuffer();
  drawHeader("Captive Pg");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 22, "DNS redirect");
  u8g2.drawStr(0, 36, captiveStarted ? "ACTIVE" : "OFF");
  u8g2.drawStr(0, 56, "Connect:");
  u8g2.drawStr(0, 70, fitText(String(AP_SSID), 11).c_str());
  u8g2.drawStr(0, 92, "Info page:");
  u8g2.drawStr(0, 106, "/info");
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWebControlPanel() {
  currentWebMode = "Control Panel";
  startWebServer();

  u8g2.clearBuffer();
  drawHeader("Control Pnl");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 22, "Browser:");
  u8g2.drawStr(0, 36, "/control");
  u8g2.drawStr(0, 56, ("Flag:" + String(testFlag ? "ON" : "OFF")).c_str());
  u8g2.drawStr(0, 70, ("Cnt:" + String(controlCounter)).c_str());
  u8g2.drawStr(0, 90, "Msg:");
  u8g2.drawStr(0, 104, fitText(oledMessage, 12).c_str());
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawWebLiveMonitor() {
  currentWebMode = "Live Monitor";
  startWebServer();

  u8g2.clearBuffer();
  drawHeader("Live Monitor");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 22, "Browser:");
  u8g2.drawStr(0, 36, "/live");
  u8g2.drawStr(0, 56, ("Up:" + String(millis() / 1000) + "s").c_str());
  u8g2.drawStr(0, 70, ("Heap:" + String(ESP.getFreeHeap() / 1024)).c_str());
  u8g2.drawStr(0, 84, ("Cli:" + String(WiFi.softAPgetStationNum())).c_str());
  u8g2.drawStr(0, 104, "fetch /status");
  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Device utility screens
// --------------------------------------------------
void drawDeviceStatus() {
  u8g2.clearBuffer();

  if (statusPage == 0) {
    drawHeader("Status 1/4");
    drawKeyValue(24, "Uptime", formatUptime());
    drawKeyValue(48, "Heap", String(ESP.getFreeHeap() / 1024) + " KB");
    drawKeyValue(72, "CPU", String(ESP.getCpuFreqMHz()) + " MHz");
    drawKeyValue(96, "Sketch", String(ESP.getSketchSize() / 1024) + " KB");
  } else if (statusPage == 1) {
    drawHeader("Status 2/4");
    drawKeyValue(24, "Chip", getChipModelString());
    drawKeyValue(48, "Cores", String(ESP.getChipCores()));
    drawKeyValue(72, "Rev", String(ESP.getChipRevision()));
    drawKeyValue(96, "SDK", fitText(String(ESP.getSdkVersion()), 12));
  } else if (statusPage == 2) {
    drawHeader("Status 3/4");
    drawKeyValue(24, "Flash", String(ESP.getFlashChipSize() / 1024 / 1024) + " MB");
    drawKeyValue(48, "Speed", String(ESP.getFlashChipSpeed() / 1000000) + " MHz");
    drawKeyValue(72, "Free", String(ESP.getFreeSketchSpace() / 1024) + " KB");
    drawKeyValue(96, "Boots", "runtime");
  } else {
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

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawDeviceSettings() {
  u8g2.clearBuffer();
  drawHeader("Settings");
  u8g2.setFont(u8g2_font_5x7_tf);

  int first = settingIndex - 1;
  if (first < 0) first = 0;
  if (first > SETTING_COUNT - 4) first = max(0, SETTING_COUNT - 4);

  for (int i = 0; i < 4; i++) {
    int idx = first + i;
    if (idx >= SETTING_COUNT) break;

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
// Current mode drawing
// --------------------------------------------------
void drawCurrentMode() {
  switch (currentMode) {
    case MODE_WIFI_SCANNER: drawWiFiScanner(); break;
    case MODE_WIFI_VISUALIZER: drawWiFiVisualizer(); break;
    case MODE_WIFI_CHANNEL_ANALYZER: drawWiFiChannelAnalyzer(); break;
    case MODE_WIFI_SIGNAL_MONITOR: drawWiFiSignalMonitor(); break;
    case MODE_WIFI_OPEN_NETWORKS: drawWiFiOpenNetworks(); break;
    case MODE_WIFI_AP_MODE: drawWiFiAPMode(); break;
    case MODE_WIFI_NETWORK_INFO: drawWiFiNetworkInfo(); break;
    case MODE_WIFI_PING_TEST: drawWiFiPingTest(); break;
    case MODE_WIFI_INTERNET_CHECK: drawWiFiInternetCheck(); break;
    case MODE_WIFI_WEB_REQUEST_TEST: drawWiFiWebRequestTest(); break;
    case MODE_WIFI_HEAT_WALK: drawWiFiHeatWalk(); break;

    case MODE_BLE_SCANNER: drawBLEScanner(); break;
    case MODE_BLE_PROXIMITY: drawBLEProximity(); break;
    case MODE_BLE_BEACON_DETECTOR: drawBLEBeaconDetector(); break;
    case MODE_BLE_SIGNAL_TRACKER: drawBLESignalTracker(); break;
    case MODE_BLE_SERVICE_VIEWER: drawBLEServiceViewer(); break;
    case MODE_BLE_ADVERTISER: drawBLEAdvertiser(); break;
    case MODE_BLE_UART: drawBLEUART(); break;

    case MODE_WEB_START_SERVER: drawWebStartServer(); break;
    case MODE_WEB_INFO: drawWebInfo(); break;
    case MODE_WEB_CAPTIVE_PORTAL: drawWebCaptive(); break;
    case MODE_WEB_CONTROL_PANEL: drawWebControlPanel(); break;
    case MODE_WEB_LIVE_MONITOR: drawWebLiveMonitor(); break;

    case MODE_DEVICE_STATUS: drawDeviceStatus(); break;
    case MODE_DEVICE_SETTINGS: drawDeviceSettings(); break;

    default: drawMainMenu(); break;
  }
}

// --------------------------------------------------
// Enter mode / navigation
// --------------------------------------------------
void enterMode(int mode, int group) {
  if (mode == MODE_NONE) return;

  currentMode = (ModeID)mode;
  activeGroup = (ActiveGroup)group;
  screenState = SCREEN_MODE;

  if (wifiModeNeedsScan(mode)) performWiFiScan();
  if (bleModeNeedsScan(mode)) performBLEScan();

  if (mode == MODE_BLE_SIGNAL_TRACKER) resetTrackerStats();
  if (mode == MODE_BLE_SERVICE_VIEWER) serviceViewerPage = 0;

  drawCurrentMode();
}

void returnToSubmenu() {
  if (activeGroup == GROUP_WIFI) {
    screenState = SCREEN_WIFI_MENU;
    drawCurrentSubmenu();
  } else if (activeGroup == GROUP_BLE) {
    screenState = SCREEN_BLE_MENU;
    drawCurrentSubmenu();
  } else if (activeGroup == GROUP_WEB) {
    screenState = SCREEN_WEB_MENU;
    drawCurrentSubmenu();
  } else if (activeGroup == GROUP_DEVICE) {
    screenState = SCREEN_DEVICE_MENU;
    drawCurrentSubmenu();
  } else {
    screenState = SCREEN_MAIN_MENU;
    drawMainMenu();
  }
}

void goHome() {
  screenState = SCREEN_MAIN_MENU;
  activeGroup = GROUP_NONE;
  currentMode = MODE_NONE;
  drawMainMenu();
}

// --------------------------------------------------
// Input actions
// --------------------------------------------------
void handleUp() {
  if (screenState == SCREEN_MAIN_MENU) {
    mainIndex--;
    if (mainIndex < 0) mainIndex = MAIN_MENU_COUNT - 1;
    drawMainMenu();
    return;
  }

  if (screenState == SCREEN_WIFI_MENU) {
    wifiIndex--;
    if (wifiIndex < 0) wifiIndex = WIFI_MENU_COUNT - 1;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_BLE_MENU) {
    bleIndex--;
    if (bleIndex < 0) bleIndex = BLE_MENU_COUNT - 1;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_WEB_MENU) {
    webIndex--;
    if (webIndex < 0) webIndex = WEB_MENU_COUNT - 1;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_DEVICE_MENU) {
    deviceIndex--;
    if (deviceIndex < 0) deviceIndex = DEVICE_MENU_COUNT - 1;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (
      currentMode == MODE_WIFI_SCANNER ||
      currentMode == MODE_WIFI_VISUALIZER ||
      currentMode == MODE_WIFI_SIGNAL_MONITOR ||
      currentMode == MODE_WIFI_OPEN_NETWORKS ||
      currentMode == MODE_WIFI_HEAT_WALK
    ) {
      if (wifiCount > 0) {
        selectedWiFi--;
        if (selectedWiFi < 0) selectedWiFi = wifiCount - 1;
      }
      drawCurrentMode();
    } else if (
      currentMode == MODE_BLE_SCANNER ||
      currentMode == MODE_BLE_PROXIMITY ||
      currentMode == MODE_BLE_BEACON_DETECTOR ||
      currentMode == MODE_BLE_SIGNAL_TRACKER
    ) {
      if (bleCount > 0) {
        selectedBLE--;
        if (selectedBLE < 0) selectedBLE = bleCount - 1;
      }
      if (currentMode == MODE_BLE_SIGNAL_TRACKER) resetTrackerStats();
      drawCurrentMode();
    } else if (currentMode == MODE_BLE_SERVICE_VIEWER) {
      serviceViewerPage--;
      if (serviceViewerPage < 0) serviceViewerPage = SERVICE_VIEWER_PAGE_COUNT - 1;
      drawCurrentMode();
    } else if (currentMode == MODE_DEVICE_STATUS) {
      statusPage--;
      if (statusPage < 0) statusPage = STATUS_PAGE_COUNT - 1;
      drawCurrentMode();
    } else if (currentMode == MODE_DEVICE_SETTINGS) {
      settingIndex--;
      if (settingIndex < 0) settingIndex = SETTING_COUNT - 1;
      drawCurrentMode();
    }
  }
}

void handleDown() {
  if (screenState == SCREEN_MAIN_MENU) {
    mainIndex++;
    if (mainIndex >= MAIN_MENU_COUNT) mainIndex = 0;
    drawMainMenu();
    return;
  }

  if (screenState == SCREEN_WIFI_MENU) {
    wifiIndex++;
    if (wifiIndex >= WIFI_MENU_COUNT) wifiIndex = 0;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_BLE_MENU) {
    bleIndex++;
    if (bleIndex >= BLE_MENU_COUNT) bleIndex = 0;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_WEB_MENU) {
    webIndex++;
    if (webIndex >= WEB_MENU_COUNT) webIndex = 0;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_DEVICE_MENU) {
    deviceIndex++;
    if (deviceIndex >= DEVICE_MENU_COUNT) deviceIndex = 0;
    drawCurrentSubmenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (
      currentMode == MODE_WIFI_SCANNER ||
      currentMode == MODE_WIFI_VISUALIZER ||
      currentMode == MODE_WIFI_SIGNAL_MONITOR ||
      currentMode == MODE_WIFI_OPEN_NETWORKS
    ) {
      if (wifiCount > 0) {
        selectedWiFi++;
        if (selectedWiFi >= wifiCount) selectedWiFi = 0;
      }
      drawCurrentMode();
    } else if (currentMode == MODE_WIFI_HEAT_WALK) {
      if (wifiCount > 0 && heatPointCount < MAX_HEAT_POINTS) {
        heatPoints[heatPointCount++] = wifiResults[selectedWiFi].rssi;
      }
      drawCurrentMode();
    } else if (currentMode == MODE_WIFI_PING_TEST && WiFi.status() == WL_CONNECTED) {
      runPingNow();
    } else if (currentMode == MODE_WIFI_INTERNET_CHECK && WiFi.status() == WL_CONNECTED) {
      runInternetNow();
    } else if (currentMode == MODE_WIFI_WEB_REQUEST_TEST && WiFi.status() == WL_CONNECTED) {
      runHTTPNow();
    } else if (
      currentMode == MODE_BLE_SCANNER ||
      currentMode == MODE_BLE_PROXIMITY ||
      currentMode == MODE_BLE_BEACON_DETECTOR ||
      currentMode == MODE_BLE_SIGNAL_TRACKER ||
      currentMode == MODE_BLE_SERVICE_VIEWER
    ) {
      if (bleCount > 0) {
        selectedBLE++;
        if (selectedBLE >= bleCount) selectedBLE = 0;
      }
      if (currentMode == MODE_BLE_SIGNAL_TRACKER) resetTrackerStats();
      drawCurrentMode();
    } else if (
      currentMode == MODE_WEB_START_SERVER ||
      currentMode == MODE_WEB_CONTROL_PANEL ||
      currentMode == MODE_WEB_LIVE_MONITOR
    ) {
      drawCurrentMode();
    } else if (currentMode == MODE_DEVICE_STATUS) {
      statusPage++;
      if (statusPage >= STATUS_PAGE_COUNT) statusPage = 0;
      drawCurrentMode();
    } else if (currentMode == MODE_DEVICE_SETTINGS) {
      changeCurrentSetting();
      drawCurrentMode();
    }
  }
}

void handleMenu() {
  if (screenState == SCREEN_MAIN_MENU) {
    if (mainIndex == 0) {
      screenState = SCREEN_WIFI_MENU;
      drawCurrentSubmenu();
    } else if (mainIndex == 1) {
      screenState = SCREEN_BLE_MENU;
      drawCurrentSubmenu();
    } else if (mainIndex == 2) {
      screenState = SCREEN_WEB_MENU;
      drawCurrentSubmenu();
    } else if (mainIndex == 3) {
      screenState = SCREEN_DEVICE_MENU;
      drawCurrentSubmenu();
    }
    return;
  }

  if (screenState == SCREEN_WIFI_MENU) {
    if (wifiModes[wifiIndex] == MODE_NONE) {
      goHome();
    } else {
      enterMode(wifiModes[wifiIndex], GROUP_WIFI);
    }
    return;
  }

  if (screenState == SCREEN_BLE_MENU) {
    if (bleModes[bleIndex] == MODE_NONE) {
      goHome();
    } else {
      enterMode(bleModes[bleIndex], GROUP_BLE);
    }
    return;
  }

  if (screenState == SCREEN_WEB_MENU) {
    if (webModes[webIndex] == MODE_NONE) {
      goHome();
    } else {
      enterMode(webModes[webIndex], GROUP_WEB);
    }
    return;
  }

  if (screenState == SCREEN_DEVICE_MENU) {
    if (deviceModes[deviceIndex] == MODE_NONE) {
      goHome();
    } else {
      enterMode(deviceModes[deviceIndex], GROUP_DEVICE);
    }
    return;
  }

  if (screenState == SCREEN_MODE) {
    returnToSubmenu();
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
// Background services
// --------------------------------------------------
void connectToStationIfConfigured() {
  loadSavedWiFiCredentials();

  String ssid = activeStaSSID();
  String pass = activeStaPASS();

  if (ssid.length() == 0) {
    return;
  }

  showMessage("STA WiFi", "Connecting", ssid.c_str());

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(500);

  WiFi.begin(ssid.c_str(), pass.c_str());
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
    u8g2.clearBuffer();
    drawHeader("STA WiFi");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 28, "Connecting...");
    u8g2.drawStr(0, 44, fitText(ssid, 12).c_str());
    u8g2.drawStr(0, 62, ("Status:" + String(WiFi.status())).c_str());
    u8g2.drawStr(0, 80, ("Time:" + String((millis() - start) / 1000) + "s").c_str());
    drawFooter("wait...");
    u8g2.sendBuffer();
  }

  u8g2.clearBuffer();
  drawHeader("STA WiFi");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(0, 22, "Connected");
    u8g2.drawStr(0, 38, fitText(WiFi.SSID(), 12).c_str());
    u8g2.drawStr(0, 56, "IP:");
    u8g2.drawStr(0, 68, fitText(WiFi.localIP().toString(), 12).c_str());
    u8g2.drawStr(0, 86, ("RSSI:" + String(WiFi.RSSI())).c_str());
    WiFi.mode(WIFI_AP_STA);
  } else {
    u8g2.drawStr(0, 22, "STA Failed");
    u8g2.drawStr(0, 40, ("Code:" + String(WiFi.status())).c_str());
    u8g2.drawStr(0, 58, "Use portal");
    u8g2.drawStr(0, 72, "/wifi setup");
    WiFi.mode(WIFI_AP_STA);
  }

  drawFooter("continue...");
  u8g2.sendBuffer();
  delay(2000);
  waitForButtonsReleased();
}

void handleBackgroundServices() {
  if (serverStarted) server.handleClient();
  if (captiveStarted) dnsServer.processNextRequest();
  sendUARTStatusIfNeeded();

  static unsigned long lastDisplayRefresh = 0;
  if (screenState == SCREEN_MODE && millis() - lastDisplayRefresh > 1500) {
    lastDisplayRefresh = millis();

    if (
      currentMode == MODE_BLE_ADVERTISER ||
      currentMode == MODE_BLE_UART ||
      currentMode == MODE_BLE_SIGNAL_TRACKER ||
      currentMode == MODE_WEB_START_SERVER ||
      currentMode == MODE_WEB_CONTROL_PANEL ||
      currentMode == MODE_WEB_LIVE_MONITOR ||
      (currentMode == MODE_DEVICE_STATUS && autoRefresh)
    ) {
      drawCurrentMode();
    }
  }
}

// --------------------------------------------------
// Setup / loop
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

  BLEDevice::init(BLE_DEVICE_NAME);
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect(false);

  connectToStationIfConfigured();

  drawMainMenu();
}

void loop() {
  handleButtons();
  handleBackgroundServices();
}
