/*
  NetViewer / PocketNet - Real Web/Server Modes Test Firmware
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

  Locked Web / Server Modes Implemented:
    1. ESP32 Web Server Dashboard
    2. Web Dashboard Mode
    3. Captive Portal Info Page
    4. Local Web Server Control Panel
    5. WebSocket Live Monitor

  Important:
    This version does NOT use ESPAsyncWebServer or AsyncTCP.
    The "WebSocket Live Monitor" mode is implemented as a live browser monitor
    using JavaScript fetch() from /status every second. This avoids extra
    library conflicts while still testing real live web/server behavior.

  Required Libraries:
    - U8g2
    - Built-in ESP32 libraries:
        WiFi.h
        WebServer.h
        DNSServer.h

  Browser:
    Connect phone/laptop to:
      SSID: NetViewer_AP
      PASS: 12345678

    Open:
      http://192.168.4.1
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
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
// AP / Server Settings
// --------------------------------------------------
const char* AP_SSID = "NetViewer_AP";
const char* AP_PASS = "12345678";

IPAddress apIP(192, 168, 4, 1);
IPAddress gatewayIP(192, 168, 4, 1);
IPAddress subnetMask(255, 255, 255, 0);

WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

bool apStarted = false;
bool serverStarted = false;
bool captiveStarted = false;

// --------------------------------------------------
// Device / Web state
// --------------------------------------------------
String currentWebMode = "None";
String oledMessage = "Ready";
bool testFlag = false;
int controlCounter = 0;
unsigned long serverStartMillis = 0;

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

enum WebMode {
  MODE_SERVER_DASHBOARD,
  MODE_WEB_DASHBOARD,
  MODE_CAPTIVE_PORTAL,
  MODE_CONTROL_PANEL,
  MODE_LIVE_MONITOR
};

ScreenState screenState = SCREEN_MENU;
WebMode currentMode = MODE_SERVER_DASHBOARD;

const char* webMenu[] = {
  "Start Server",
  "Info",
  "Captive Pg",
  "Control Pnl",
  "Live Monitor"
};

const int WEB_MENU_COUNT = sizeof(webMenu) / sizeof(webMenu[0]);
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
void drawServerIcon(int cx, int cy, int frame) {
  u8g2.drawFrame(cx - 18, cy - 18, 36, 12);
  u8g2.drawFrame(cx - 18, cy - 3, 36, 12);
  u8g2.drawFrame(cx - 18, cy + 12, 36, 12);

  u8g2.drawDisc(cx - 12, cy - 12, 1);
  u8g2.drawDisc(cx - 12, cy + 3, 1);
  u8g2.drawDisc(cx - 12, cy + 18, 1);

  if (frame % 2 == 0) {
    u8g2.drawDisc(cx + 12, cy - 12, 1);
    u8g2.drawDisc(cx + 12, cy + 3, 1);
    u8g2.drawDisc(cx + 12, cy + 18, 1);
  }

  int r = 24 + (frame % 4) * 3;
  u8g2.drawCircle(cx, cy + 3, r);
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
    u8g2.drawStr(4, 52, "Web Server");

    drawServerIcon(32, 82, frame);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(5, 122, "AP Dashboard");

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
// Web page helpers
// --------------------------------------------------
String htmlHeader(String title) {
  String html;
  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>" + title + "</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:#eee;margin:20px;}";
  html += ".card{background:#1d1d1d;border:1px solid #444;border-radius:12px;padding:14px;margin:12px 0;}";
  html += "a,button{background:#222;color:#7cf;border:1px solid #555;border-radius:8px;padding:10px;margin:5px;display:inline-block;text-decoration:none;}";
  html += "button{font-size:16px;}";
  html += "table{border-collapse:collapse;width:100%;}";
  html += "td,th{border:1px solid #555;padding:6px;text-align:left;}";
  html += ".ok{color:#8f8;}.warn{color:#ff8;}";
  html += "</style>";
  html += "</head><body>";
  html += "<h1>NetViewer Web</h1>";
  html += "<p>ESP32 Web / Server Test</p>";
  return html;
}

String navLinks() {
  String html;
  html += "<p>";
  html += "<a href='/'>Dashboard</a>";
  html += "<a href='/control'>Control</a>";
  html += "<a href='/live'>Live</a>";
  html += "<a href='/info'>Info</a>";
  html += "<a href='/status'>JSON</a>";
  html += "</p>";
  return html;
}

String htmlFooter() {
  String html;
  html += "<div class='card'>";
  html += "<small>NetViewer_AP @ 192.168.4.1</small>";
  html += "</div>";
  html += "</body></html>";
  return html;
}

String statusJSON() {
  String json = "{";
  json += "\"device\":\"NetViewer\",";
  json += "\"mode\":\"" + currentWebMode + "\",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"clients\":" + String(WiFi.softAPgetStationNum()) + ",";
  json += "\"apIP\":\"" + WiFi.softAPIP().toString() + "\",";
  json += "\"ssid\":\"" + String(AP_SSID) + "\",";
  json += "\"testFlag\":" + String(testFlag ? "true" : "false") + ",";
  json += "\"counter\":" + String(controlCounter) + ",";
  json += "\"oledMessage\":\"" + oledMessage + "\",";
  json += "\"buttonUp\":" + String(rawUp() ? "true" : "false") + ",";
  json += "\"buttonDown\":" + String(rawDown() ? "true" : "false") + ",";
  json += "\"buttonMenu\":" + String(rawMenu() ? "true" : "false");
  json += "}";
  return json;
}

// --------------------------------------------------
// Web routes
// --------------------------------------------------
void handleRoot() {
  String html = htmlHeader("NetViewer Dashboard");
  html += navLinks();

  html += "<div class='card'>";
  html += "<h2>Server Dashboard</h2>";
  html += "<table>";
  html += "<tr><th>Item</th><th>Value</th></tr>";
  html += "<tr><td>AP SSID</td><td>" + String(AP_SSID) + "</td></tr>";
  html += "<tr><td>AP IP</td><td>" + WiFi.softAPIP().toString() + "</td></tr>";
  html += "<tr><td>Uptime</td><td>" + String(millis() / 1000) + " sec</td></tr>";
  html += "<tr><td>Free Heap</td><td>" + String(ESP.getFreeHeap()) + " bytes</td></tr>";
  html += "<tr><td>Clients</td><td>" + String(WiFi.softAPgetStationNum()) + "</td></tr>";
  html += "<tr><td>OLED Message</td><td>" + oledMessage + "</td></tr>";
  html += "</table>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Mode Links</h2>";
  html += "<p>Use the links above to test dashboard, control panel, and live monitor behavior.</p>";
  html += "</div>";

  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleInfo() {
  String html = htmlHeader("NetViewer Info");
  html += navLinks();

  html += "<div class='card'>";
  html += "<h2>Captive Portal Info Page</h2>";
  html += "<p>This is the local info page hosted by the ESP32.</p>";
  html += "<p>When captive portal DNS mode is active, many devices will redirect web requests back to this page.</p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Connection Steps</h2>";
  html += "<ol>";
  html += "<li>Connect to Wi-Fi network <b>" + String(AP_SSID) + "</b>.</li>";
  html += "<li>Use password <b>" + String(AP_PASS) + "</b>.</li>";
  html += "<li>Open <b>192.168.4.1</b> in a browser.</li>";
  html += "</ol>";
  html += "</div>";

  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleControlPanel() {
  String html = htmlHeader("NetViewer Control");
  html += navLinks();

  html += "<div class='card'>";
  html += "<h2>Local Web Control Panel</h2>";
  html += "<p>Control ESP32 test states from your browser.</p>";
  html += "<p><a href='/toggle'>Toggle Test Flag</a></p>";
  html += "<p><a href='/count'>Increment Counter</a></p>";
  html += "<p><a href='/msgready'>OLED: Ready</a></p>";
  html += "<p><a href='/msgweb'>OLED: Web OK</a></p>";
  html += "<p><a href='/msgctrl'>OLED: Control</a></p>";
  html += "<p><a href='/restart' onclick=\"return confirm('Restart ESP32?')\">Restart ESP32</a></p>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h2>Current State</h2>";
  html += "<table>";
  html += "<tr><td>Test Flag</td><td>" + String(testFlag ? "ON" : "OFF") + "</td></tr>";
  html += "<tr><td>Counter</td><td>" + String(controlCounter) + "</td></tr>";
  html += "<tr><td>OLED Message</td><td>" + oledMessage + "</td></tr>";
  html += "</table>";
  html += "</div>";

  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleLiveMonitor() {
  String html = htmlHeader("NetViewer Live");
  html += navLinks();

  html += "<div class='card'>";
  html += "<h2>Live Monitor</h2>";
  html += "<p>This page updates every second using JavaScript fetch from <b>/status</b>.</p>";
  html += "<pre id='out'>Loading...</pre>";
  html += "</div>";

  html += "<script>";
  html += "async function update(){";
  html += "try{";
  html += "let r=await fetch('/status');";
  html += "let j=await r.json();";
  html += "document.getElementById('out').textContent=JSON.stringify(j,null,2);";
  html += "}catch(e){document.getElementById('out').textContent='Update failed: '+e;}";
  html += "}";
  html += "setInterval(update,1000);update();";
  html += "</script>";

  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleToggle() {
  testFlag = !testFlag;
  oledMessage = testFlag ? "Flag ON" : "Flag OFF";
  server.sendHeader("Location", "/control");
  server.send(303);
}

void handleCount() {
  controlCounter++;
  oledMessage = "Count " + String(controlCounter);
  server.sendHeader("Location", "/control");
  server.send(303);
}

void handleSetMsgReady() {
  oledMessage = "Ready";
  server.sendHeader("Location", "/control");
  server.send(303);
}

void handleSetMsgWeb() {
  oledMessage = "Web OK";
  server.sendHeader("Location", "/control");
  server.send(303);
}

void handleSetMsgCtrl() {
  oledMessage = "Control";
  server.sendHeader("Location", "/control");
  server.send(303);
}

void handleRestart() {
  server.send(200, "text/html", "<html><body><h1>Restarting...</h1></body></html>");
  delay(500);
  ESP.restart();
}

void handleStatus() {
  server.send(200, "application/json", statusJSON());
}

void handleNotFound() {
  // Captive portal style redirect.
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}

// --------------------------------------------------
// AP / Server startup
// --------------------------------------------------
void startAP() {
  if (apStarted) {
    return;
  }

  showMessage("AP Mode", "Starting AP", AP_SSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, gatewayIP, subnetMask);
  WiFi.softAP(AP_SSID, AP_PASS);

  apStarted = true;

  delay(500);
}

void startWebServer() {
  if (serverStarted) {
    return;
  }

  startAP();

  showMessage("Web Server", "Starting", "routes");

  server.on("/", handleRoot);
  server.on("/info", handleInfo);
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
  serverStartMillis = millis();

  delay(500);
  waitForButtonsReleased();
}

void startCaptiveDNS() {
  if (captiveStarted) {
    return;
  }

  startWebServer();

  showMessage("Captive", "Starting DNS", "redirect");

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  captiveStarted = true;

  delay(500);
  waitForButtonsReleased();
}

// --------------------------------------------------
// Menu screen
// --------------------------------------------------
void drawMenu() {
  u8g2.clearBuffer();

  drawHeader("Web Tools");

  u8g2.setFont(u8g2_font_5x7_tf);

  for (int i = 0; i < WEB_MENU_COUNT; i++) {
    int y = 26 + i * 15;

    if (i == menuIndex) {
      u8g2.drawBox(0, y - 8, SCREEN_W, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(1, y, fitText(String(webMenu[i]), 11).c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(1, y, fitText(String(webMenu[i]), 11).c_str());
    }
  }

  drawFooter("UP DN SEL");
  u8g2.sendBuffer();
}

// --------------------------------------------------
// Mode screens
// --------------------------------------------------
void drawServerDashboardMode() {
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

void drawWebDashboardMode() {
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

void drawCaptivePortalMode() {
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

void drawControlPanelMode() {
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

void drawLiveMonitorMode() {
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

void drawCurrentMode() {
  switch (currentMode) {
    case MODE_SERVER_DASHBOARD:
      drawServerDashboardMode();
      break;

    case MODE_WEB_DASHBOARD:
      drawWebDashboardMode();
      break;

    case MODE_CAPTIVE_PORTAL:
      drawCaptivePortalMode();
      break;

    case MODE_CONTROL_PANEL:
      drawControlPanelMode();
      break;

    case MODE_LIVE_MONITOR:
      drawLiveMonitorMode();
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
      menuIndex = WEB_MENU_COUNT - 1;
    }

    drawMenu();
    return;
  }

  // In mode screens, UP is currently reserved for future mode-specific actions.
}

void handleDown() {
  if (screenState == SCREEN_MENU) {
    menuIndex++;

    if (menuIndex >= WEB_MENU_COUNT) {
      menuIndex = 0;
    }

    drawMenu();
    return;
  }

  // In mode screens, DOWN refreshes the current mode/status screen.
  if (screenState == SCREEN_MODE) {
    drawCurrentMode();
  }
}

void handleMenu() {
  if (screenState == SCREEN_MENU) {
    currentMode = (WebMode)menuIndex;
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
// Background services
// --------------------------------------------------
void handleBackgroundServices() {
  if (serverStarted) {
    server.handleClient();
  }

  if (captiveStarted) {
    dnsServer.processNextRequest();
  }
}

void handleDisplayRefresh() {
  static unsigned long lastRefresh = 0;

  if (screenState != SCREEN_MODE) {
    return;
  }

  if (millis() - lastRefresh < 1500) {
    return;
  }

  lastRefresh = millis();

  if (
    currentMode == MODE_SERVER_DASHBOARD ||
    currentMode == MODE_CONTROL_PANEL ||
    currentMode == MODE_LIVE_MONITOR
  ) {
    drawCurrentMode();
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
  handleDisplayRefresh();
}
