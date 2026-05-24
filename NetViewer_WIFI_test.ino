/*
  NetViewer / PocketNet - Real Wi-Fi Modes Test Firmware
  ------------------------------------------------------
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

  Locked Wi-Fi Modes Implemented:
    1. Wi-Fi Scanner
    2. Wi-Fi Visualizer
    3. Wi-Fi Channel Analyzer
    4. Wi-Fi Signal Monitor
    5. Open Network Detector
    6. Wi-Fi Access Point Mode
    7. Network Info Mode
    8. Ping Test Mode
    9. Internet Check Mode
    10. Web Request Tester
    11. Signal Heat Walk Mode

  Required Libraries:
    - U8g2
    - ESP32Ping

  Notes:
    - Scanner/AP/Channel/Open/Signal/Heat modes work without Wi-Fi credentials.
    - Ping/Internet/HTTP/Network Info need STA Wi-Fi credentials.
    - Add your Wi-Fi credentials below if you want those modes to work.
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESP32Ping.h>
#include <U8g2lib.h>

// --------------------------------------------------
// Pin Mapping
// --------------------------------------------------
#define OLED_SDA 21
#define OLED_SCL 22

// Updated stable button mapping
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
// Wi-Fi configuration
// --------------------------------------------------
// Leave blank if you only want scanner/AP/local dashboard modes.
const char* STA_SSID = "optix_legacy";
const char* STA_PASS = "";

const char* AP_SSID = "NetViewer_AP";
const char* AP_PASS = "12345678";

const char* PING_TARGET = "8.8.8.8";
const char* INTERNET_TEST_HOST = "example.com";
const char* HTTP_TEST_URL = "http://example.com";

// --------------------------------------------------
// Web server / captive-style local dashboard
// --------------------------------------------------
WebServer server(80);
DNSServer dnsServer;

bool apStarted = false;
bool serverStarted = false;
bool dnsStarted = false;

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
// Wi-Fi scan data
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
unsigned long lastScanTime = 0;

// --------------------------------------------------
// Heat Walk data
// --------------------------------------------------
#define MAX_HEAT_POINTS 24

int heatPoints[MAX_HEAT_POINTS];
int heatPointCount = 0;

// --------------------------------------------------
// Menu system
// --------------------------------------------------
enum ScreenState {
  SCREEN_MENU,
  SCREEN_MODE
};

enum WiFiMode {
  MODE_WIFI_SCANNER,
  MODE_WIFI_VISUALIZER,
  MODE_CHANNEL_ANALYZER,
  MODE_SIGNAL_MONITOR,
  MODE_OPEN_NETWORKS,
  MODE_AP_MODE,
  MODE_NETWORK_INFO,
  MODE_PING_TEST,
  MODE_INTERNET_CHECK,
  MODE_WEB_REQUEST_TEST,
  MODE_SIGNAL_HEAT_WALK
};

ScreenState screenState = SCREEN_MENU;
WiFiMode currentMode = MODE_WIFI_SCANNER;

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
  "Heat Walk"
};

const int WIFI_MENU_COUNT = sizeof(wifiMenu) / sizeof(wifiMenu[0]);
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

int rssiToQuality(int rssi) {
  if (rssi <= -100) {
    return 0;
  }

  if (rssi >= -50) {
    return 100;
  }

  return 2 * (rssi + 100);
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
// Splash screen
// --------------------------------------------------
void drawWiFiIcon(int x, int y, int step) {
  u8g2.drawDisc(x, y, 2);

  if (step >= 1) {
    u8g2.drawCircle(x, y, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  }

  if (step >= 2) {
    u8g2.drawCircle(x, y, 13, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  }

  if (step >= 3) {
    u8g2.drawCircle(x, y, 19, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
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
    u8g2.drawStr(4, 52, "WiFi Modes");

    drawWiFiIcon(32, 88, frame % 4);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(4, 122, "UP=GPIO32 OK");

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
// Wi-Fi scanning
// --------------------------------------------------
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

  if (selectedWiFi >= wifiCount) {
    selectedWiFi = 0;
  }

  lastScanTime = millis();

  waitForButtonsReleased();
}

// --------------------------------------------------
// Station connection
// --------------------------------------------------
void connectToStationIfConfigured() {
  if (String(STA_SSID).length() == 0) {
    return;
  }

  showMessage("STA WiFi", "Connecting", STA_SSID);

  WiFi.persistent(false);
  WiFi.setSleep(false);

  // Start clean in station-only mode first.
  // This is more reliable than trying to connect while AP mode is already active.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(500);

  WiFi.begin(STA_SSID, STA_PASS);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);

    u8g2.clearBuffer();

    drawHeader("STA WiFi");

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 28, "Connecting...");
    u8g2.drawStr(0, 44, fitText(String(STA_SSID), 12).c_str());
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

    // After station connects, enable AP+STA so local dashboard can still work.
    WiFi.mode(WIFI_AP_STA);
  } else {
    int statusCode = WiFi.status();

    u8g2.drawStr(0, 22, "STA Failed");
    u8g2.drawStr(0, 40, ("Code:" + String(statusCode)).c_str());

    if (statusCode == WL_NO_SSID_AVAIL) {
      u8g2.drawStr(0, 58, "SSID not");
      u8g2.drawStr(0, 72, "found");
    } else if (statusCode == WL_CONNECT_FAILED) {
      u8g2.drawStr(0, 58, "Bad pass");
      u8g2.drawStr(0, 72, "or auth");
    } else if (statusCode == WL_DISCONNECTED) {
      u8g2.drawStr(0, 58, "Disconnected");
    } else {
      u8g2.drawStr(0, 58, "Check 2.4G");
      u8g2.drawStr(0, 72, "SSID/pass");
    }

    // Still allow scanner/AP modes after failed STA connection.
    WiFi.mode(WIFI_AP_STA);
  }

  drawFooter("continue...");
  u8g2.sendBuffer();

  delay(2500);
  waitForButtonsReleased();
}

// --------------------------------------------------
// Web dashboard
// --------------------------------------------------
String dashboardHTML() {
  String html;

  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>NetViewer WiFi</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:#eee;margin:20px;}";
  html += "table{border-collapse:collapse;width:100%;font-size:14px;}";
  html += "td,th{border:1px solid #555;padding:6px;text-align:left;}";
  html += "a{color:#7cf;}";
  html += "</style></head><body>";

  html += "<h1>NetViewer WiFi</h1>";
  html += "<p>ESP32 Wi-Fi Mode Test</p>";
  html += "<p>Uptime: " + String(millis() / 1000) + " sec</p>";
  html += "<p>Wi-Fi results: " + String(wifiCount) + "</p>";
  html += "<p>Heat points: " + String(heatPointCount) + "</p>";
  html += "<p><a href='/scan'>Run Wi-Fi Scan</a> | <a href='/status'>Status JSON</a></p>";

  html += "<h2>Scan Results</h2>";
  html += "<table><tr><th>#</th><th>SSID</th><th>RSSI</th><th>CH</th><th>SEC</th></tr>";

  for (int i = 0; i < wifiCount; i++) {
    html += "<tr>";
    html += "<td>" + String(i + 1) + "</td>";
    html += "<td>" + String(wifiResults[i].hidden ? "&lt;hidden&gt;" : wifiResults[i].ssid) + "</td>";
    html += "<td>" + String(wifiResults[i].rssi) + "</td>";
    html += "<td>" + String(wifiResults[i].channel) + "</td>";
    html += "<td>" + encryptionToString(wifiResults[i].encryption) + "</td>";
    html += "</tr>";
  }

  html += "</table>";

  html += "<h2>Heat Walk Points</h2><ol>";

  for (int i = 0; i < heatPointCount; i++) {
    html += "<li>" + String(heatPoints[i]) + " dBm</li>";
  }

  html += "</ol>";
  html += "</body></html>";

  return html;
}

void startAPAndServer() {
  WiFi.mode(WIFI_AP_STA);

  if (!apStarted) {
    WiFi.softAP(AP_SSID, AP_PASS);
    apStarted = true;
  }

  if (!dnsStarted) {
    dnsServer.start(53, "*", WiFi.softAPIP());
    dnsStarted = true;
  }

  if (!serverStarted) {
    server.on("/", []() {
      server.send(200, "text/html", dashboardHTML());
    });

    server.on("/scan", []() {
      performWiFiScan();
      server.sendHeader("Location", "/");
      server.send(303);
    });

    server.on("/status", []() {
      String json = "{";
      json += "\"device\":\"NetViewer\",";
      json += "\"wifiCount\":" + String(wifiCount) + ",";
      json += "\"heatPoints\":" + String(heatPointCount) + ",";
      json += "\"uptime\":" + String(millis() / 1000) + ",";
      json += "\"apIP\":\"" + WiFi.softAPIP().toString() + "\"";
      json += "}";

      server.send(200, "application/json", json);
    });

    server.onNotFound([]() {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
      server.send(302, "text/plain", "");
    });

    server.begin();
    serverStarted = true;
  }
}

// --------------------------------------------------
// Menu screen
// --------------------------------------------------
void drawMenu() {
  u8g2.clearBuffer();

  drawHeader("WiFi Tools");

  u8g2.setFont(u8g2_font_5x7_tf);

  int first = menuIndex - 3;

  if (first < 0) {
    first = 0;
  }

  if (first > WIFI_MENU_COUNT - 8) {
    first = max(0, WIFI_MENU_COUNT - 8);
  }

  for (int i = 0; i < 8; i++) {
    int idx = first + i;

    if (idx >= WIFI_MENU_COUNT) {
      break;
    }

    int y = 22 + i * 11;

    if (idx == menuIndex) {
      u8g2.drawBox(0, y - 8, SCREEN_W, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(1, y, fitText(String(wifiMenu[idx]), 11).c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(1, y, fitText(String(wifiMenu[idx]), 11).c_str());
    }
  }

  drawFooter("UP DN SEL");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Mode screens
// --------------------------------------------------
void drawWiFiScanner() {
  u8g2.clearBuffer();
  drawHeader("WiFi Scan");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 30, "No results");
    u8g2.drawStr(0, 44, "MENU back");
    u8g2.drawStr(0, 58, "re-enter");
    u8g2.drawStr(0, 72, "to scan");
  } else {
    WiFiEntry &e = wifiResults[selectedWiFi];

    String ssid = e.hidden ? "<hidden>" : e.ssid;

    u8g2.drawStr(0, 22, ("AP " + String(selectedWiFi + 1)).c_str());
    u8g2.drawStr(32, 22, ("/" + String(wifiCount)).c_str());

    u8g2.drawStr(0, 38, "SSID:");
    u8g2.drawStr(0, 50, fitText(ssid, 11).c_str());

    u8g2.drawStr(0, 66, ("R:" + String(e.rssi)).c_str());
    u8g2.drawStr(34, 66, ("C:" + String(e.channel)).c_str());

    u8g2.drawStr(0, 82, encryptionToString(e.encryption).c_str());

    drawSignalBar(96, rssiToQuality(e.rssi));
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}


// --------------------------------------------------
// Wi-Fi Visualizer Mode
// --------------------------------------------------
// This mode gives a small monochrome "spectrum-style" view of the scan results.
// X-axis = Wi-Fi channel position.
// Y-axis = signal strength.
// Each AP is drawn as a curved hump centered around its channel.
// The selected AP is drawn thicker and its info is shown at the top.
void drawWiFiArc(int centerX, int baseY, int halfWidth, int height, bool selected) {
  int lastX = -1;
  int lastY = -1;

  for (int dx = -halfWidth; dx <= halfWidth; dx++) {
    float t = (float)dx / (float)halfWidth;
    int x = centerX + dx;
    int y = baseY - height + (int)(height * t * t);

    if (x < 0 || x >= SCREEN_W || y < 12 || y >= SCREEN_H) {
      continue;
    }

    if (!selected && (dx % 2 != 0)) {
      continue;
    }

    if (lastX >= 0) {
      u8g2.drawLine(lastX, lastY, x, y);

      if (selected) {
        // Make the selected network easier to see on a monochrome OLED.
        if (y + 1 < baseY) {
          u8g2.drawLine(lastX, lastY + 1, x, y + 1);
        }
      }
    }

    lastX = x;
    lastY = y;
  }
}

void drawWiFiVisualizer() {
  u8g2.clearBuffer();
  drawHeader("WiFi Visual");

  u8g2.setFont(u8g2_font_4x6_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 34, "No scan data");
    u8g2.drawStr(0, 48, "Go back and");
    u8g2.drawStr(0, 62, "re-enter mode");
    drawFooter("MENU Back");
    u8g2.sendBuffer();
    return;
  }

  WiFiEntry &selected = wifiResults[selectedWiFi];

  String ssid = selected.hidden ? "<hidden>" : selected.ssid;
  u8g2.drawStr(0, 18, fitText(ssid, 15).c_str());
  u8g2.drawStr(0, 27, ("R:" + String(selected.rssi)).c_str());
  u8g2.drawStr(34, 27, ("C:" + String(selected.channel)).c_str());

  // Graph area
  const int graphTop = 32;
  const int baseY = 108;

  // Axis lines
  u8g2.drawVLine(2, graphTop, baseY - graphTop);
  u8g2.drawHLine(2, baseY, 61);

  // Light RSSI guide lines
  u8g2.drawHLine(2, 48, 61);  // strong
  u8g2.drawHLine(2, 78, 61);  // medium

  // Channel labels for 2.4 GHz planning
  u8g2.drawStr(2, 115, "1");
  u8g2.drawStr(28, 115, "6");
  u8g2.drawStr(51, 115, "11");

  // Draw strongest APs only to avoid clutter.
  int visibleCount = min(wifiCount, 8);

  // Draw non-selected APs first.
  for (int i = 0; i < visibleCount; i++) {
    if (i == selectedWiFi) {
      continue;
    }

    int ch = constrain((int)wifiResults[i].channel, 1, 13);
    int centerX = map(ch, 1, 13, 5, 60);

    int rssi = constrain((int)wifiResults[i].rssi, -95, -35);
    int height = map(rssi, -95, -35, 8, 68);

    // Approximate 20 MHz width. Kept small because screen is narrow.
    int halfWidth = 7;

    drawWiFiArc(centerX, baseY, halfWidth, height, false);
  }

  // Draw selected AP on top, even if it is outside top 8.
  int ch = constrain((int)selected.channel, 1, 13);
  int centerX = map(ch, 1, 13, 5, 60);

  int rssi = constrain((int)selected.rssi, -95, -35);
  int height = map(rssi, -95, -35, 8, 68);

  drawWiFiArc(centerX, baseY, 9, height, true);

  // Mark selected AP center.
  u8g2.drawDisc(centerX, baseY, 2);

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawChannelAnalyzer() {
  int counts[15] = {0};

  for (int i = 0; i < wifiCount; i++) {
    int ch = wifiResults[i].channel;

    if (ch >= 1 && ch <= 14) {
      counts[ch]++;
    }
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

void drawSignalMonitor() {
  u8g2.clearBuffer();

  drawHeader("Signal Mon");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 34, "No scan");
    u8g2.drawStr(0, 48, "data yet");
  } else {
    WiFiEntry &e = wifiResults[selectedWiFi];
    int q = rssiToQuality(e.rssi);

    u8g2.drawStr(0, 24, fitText(e.hidden ? "<hidden>" : e.ssid, 11).c_str());
    u8g2.drawStr(0, 42, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 56, ("Q:" + String(q) + "%").c_str());

    drawSignalBar(72, q);

    u8g2.drawStr(0, 96, "Last scan");
    u8g2.drawStr(0, 108, "value");
  }

  drawFooter("UP DN Back");
  u8g2.sendBuffer();
}

void drawOpenNetworks() {
  int openIndexes[MAX_WIFI_RESULTS];
  int openCount = 0;

  for (int i = 0; i < wifiCount; i++) {
    if (wifiResults[i].encryption == WIFI_AUTH_OPEN) {
      openIndexes[openCount++] = i;
    }
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

void drawAPMode() {
  startAPAndServer();

  u8g2.clearBuffer();

  drawHeader("AP Mode");

  u8g2.setFont(u8g2_font_5x7_tf);

  u8g2.drawStr(0, 24, "AP Active");

  u8g2.drawStr(0, 42, "SSID:");
  u8g2.drawStr(0, 54, fitText(String(AP_SSID), 11).c_str());

  u8g2.drawStr(0, 74, "IP:");
  u8g2.drawStr(0, 86, "192.168.");
  u8g2.drawStr(0, 98, "4.1");

  drawFooter("MENU Back");
  u8g2.sendBuffer();
}

void drawNetworkInfo() {
  u8g2.clearBuffer();

  drawHeader("Net Info");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() == WL_CONNECTED) {
    u8g2.drawStr(0, 22, "STA conn");
    u8g2.drawStr(0, 36, fitText(WiFi.SSID(), 11).c_str());

    u8g2.drawStr(0, 54, "IP:");
    u8g2.drawStr(0, 66, fitText(WiFi.localIP().toString(), 12).c_str());

    u8g2.drawStr(0, 84, ("RSSI:" + String(WiFi.RSSI())).c_str());
    u8g2.drawStr(0, 98, ("CH:" + String(WiFi.channel())).c_str());
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

void drawPingTest() {
  u8g2.clearBuffer();

  drawHeader("Ping Test");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
    u8g2.drawStr(0, 56, "credentials");
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

void drawInternetCheck() {
  u8g2.clearBuffer();

  drawHeader("Internet");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
    u8g2.drawStr(0, 56, "credentials");
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

  if (ok) {
    u8g2.drawStr(0, 30, "Internet");
    u8g2.drawStr(0, 46, "OK");
  } else {
    u8g2.drawStr(0, 30, "No net");
    u8g2.drawStr(0, 46, "or DNS");
  }

  drawFooter("MENU Back");
  u8g2.sendBuffer();

  waitForButtonsReleased();
}

void drawWebRequestTest() {
  u8g2.clearBuffer();

  drawHeader("HTTP Test");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, "No STA");
    u8g2.drawStr(0, 42, "Add WiFi");
    u8g2.drawStr(0, 56, "credentials");
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

void drawSignalHeatWalk() {
  u8g2.clearBuffer();

  drawHeader("Heat Walk");

  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiCount <= 0) {
    u8g2.drawStr(0, 30, "No scan");
    u8g2.drawStr(0, 44, "data yet");
  } else {
    WiFiEntry &e = wifiResults[selectedWiFi];

    u8g2.drawStr(0, 22, "Target:");
    u8g2.drawStr(0, 34, fitText(e.hidden ? "<hidden>" : e.ssid, 11).c_str());

    u8g2.drawStr(0, 52, ("RSSI:" + String(e.rssi)).c_str());
    u8g2.drawStr(0, 68, ("Pts:" + String(heatPointCount)).c_str());

    if (heatPointCount > 0) {
      u8g2.drawStr(0, 84, ("Last:" + String(heatPoints[heatPointCount - 1])).c_str());
    }

    u8g2.drawStr(0, 102, "DN save");
    u8g2.drawStr(0, 113, "UP target");
  }

  u8g2.sendBuffer();
}

void drawCurrentMode() {
  switch (currentMode) {
    case MODE_WIFI_SCANNER:
      drawWiFiScanner();
      break;

    case MODE_WIFI_VISUALIZER:
      drawWiFiVisualizer();
      break;

    case MODE_CHANNEL_ANALYZER:
      drawChannelAnalyzer();
      break;

    case MODE_SIGNAL_MONITOR:
      drawSignalMonitor();
      break;

    case MODE_OPEN_NETWORKS:
      drawOpenNetworks();
      break;

    case MODE_AP_MODE:
      drawAPMode();
      break;

    case MODE_NETWORK_INFO:
      drawNetworkInfo();
      break;

    case MODE_PING_TEST:
      drawPingTest();
      break;

    case MODE_INTERNET_CHECK:
      drawInternetCheck();
      break;

    case MODE_WEB_REQUEST_TEST:
      drawWebRequestTest();
      break;

    case MODE_SIGNAL_HEAT_WALK:
      drawSignalHeatWalk();
      break;
  }
}

// --------------------------------------------------
// Mode helpers/actions
// --------------------------------------------------
bool modeNeedsScan(int mode) {
  return (
    mode == MODE_WIFI_SCANNER ||
    mode == MODE_WIFI_VISUALIZER ||
    mode == MODE_CHANNEL_ANALYZER ||
    mode == MODE_SIGNAL_MONITOR ||
    mode == MODE_OPEN_NETWORKS ||
    mode == MODE_SIGNAL_HEAT_WALK
  );
}

void handleUp() {
  if (screenState == SCREEN_MENU) {
    menuIndex--;

    if (menuIndex < 0) {
      menuIndex = WIFI_MENU_COUNT - 1;
    }

    drawMenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (
      currentMode == MODE_WIFI_SCANNER ||
      currentMode == MODE_WIFI_VISUALIZER ||
      currentMode == MODE_SIGNAL_MONITOR ||
      currentMode == MODE_OPEN_NETWORKS ||
      currentMode == MODE_SIGNAL_HEAT_WALK
    ) {
      if (wifiCount > 0) {
        selectedWiFi--;

        if (selectedWiFi < 0) {
          selectedWiFi = wifiCount - 1;
        }
      }

      drawCurrentMode();
    }
  }
}

void handleDown() {
  if (screenState == SCREEN_MENU) {
    menuIndex++;

    if (menuIndex >= WIFI_MENU_COUNT) {
      menuIndex = 0;
    }

    drawMenu();
    return;
  }

  if (screenState == SCREEN_MODE) {
    if (
      currentMode == MODE_WIFI_SCANNER ||
      currentMode == MODE_WIFI_VISUALIZER ||
      currentMode == MODE_SIGNAL_MONITOR ||
      currentMode == MODE_OPEN_NETWORKS
    ) {
      if (wifiCount > 0) {
        selectedWiFi++;

        if (selectedWiFi >= wifiCount) {
          selectedWiFi = 0;
        }
      }

      drawCurrentMode();
    } else if (currentMode == MODE_SIGNAL_HEAT_WALK) {
      if (wifiCount > 0 && heatPointCount < MAX_HEAT_POINTS) {
        heatPoints[heatPointCount++] = wifiResults[selectedWiFi].rssi;
      }

      drawCurrentMode();
    } else if (currentMode == MODE_PING_TEST && WiFi.status() == WL_CONNECTED) {
      runPingNow();
    } else if (currentMode == MODE_INTERNET_CHECK && WiFi.status() == WL_CONNECTED) {
      runInternetNow();
    } else if (currentMode == MODE_WEB_REQUEST_TEST && WiFi.status() == WL_CONNECTED) {
      runHTTPNow();
    }
  }
}

void handleMenu() {
  if (screenState == SCREEN_MENU) {
    currentMode = (WiFiMode)menuIndex;
    screenState = SCREEN_MODE;

    if (modeNeedsScan(currentMode)) {
      performWiFiScan();
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
// Background services
// --------------------------------------------------
void handleBackgroundServices() {
  if (serverStarted) {
    server.handleClient();
  }

  if (dnsStarted) {
    dnsServer.processNextRequest();
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

  connectToStationIfConfigured();

  // If station connection failed or credentials are blank, keep AP+STA available
  // so scan/AP/dashboard modes still work.
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP_STA);
  }

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
  handleBackgroundServices();
}
