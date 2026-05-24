# NetViewer — ESP32 Wireless Utility Console

NetViewer is a minimal handheld wireless utility console built with an **ESP32-WROOM-DA**, a **2.4-inch SSD1309 I2C OLED**, and **three push buttons**. It focuses on Wi-Fi diagnostics, BLE scanning/tracking, local web server tools, and basic device utilities through a compact OLED menu interface.

> This project is intended for passive wireless diagnostics, local network testing, and learning ESP32 Wi-Fi/BLE/web server development.

---

## Features

- Portrait SSD1309 OLED interface
- 3-button navigation
- Animated Wi-Fi splash screen
- Wi-Fi scanning, channel analysis, signal viewing, and visualizer mode
- BLE scanning, proximity tracking, beacon detection, advertiser, and BLE UART
- ESP32-hosted web dashboard, control panel, captive info page, and live monitor
- Device status and OLED settings menu
- Back option inside every submenu for easier navigation

---

## Hardware

| Component | Description |
|---|---|
| ESP32-WROOM-DA | Main controller with Wi-Fi and BLE |
| 2.4-inch SSD1309 OLED | I2C display, used in portrait orientation |
| 3 Push Buttons | Up, Down, Menu/Select |
| Power Circuit | Battery/regulator/switch circuit for portable use |

---

## Pin Mapping

| Function | ESP32 Pin |
|---|---|
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| Button UP | GPIO32 |
| Button DOWN | GPIO26 |
| Button MENU / SELECT | GPIO27 |

Buttons are wired between the GPIO pin and GND. The firmware uses `INPUT_PULLUP`, so:

```text
Released = HIGH
Pressed  = LOW
```

> GPIO25 was originally tested for UP, but GPIO32 was selected because it behaved more reliably while Wi-Fi was active.

---

## Required Libraries

Install these in Arduino IDE:

| Library / Package | Purpose |
|---|---|
| esp32 by Espressif Systems | ESP32 board support, Wi-Fi, BLE, WebServer |
| U8g2 by Oliver Kraus | SSD1309 OLED graphics |
| ESP32Ping | Ping and internet connection testing |

Do **not** install the old external `ESP32_BLE_Arduino` library. Use the BLE library included with the ESP32 board package.

---

## Main Menu

```text
1. Wi-Fi Tools
2. BLE Tools
3. Web Server
4. Device Utility
```

Each submenu includes a **BACK** option to return to the main menu.

---

## Wi-Fi Tools

| Mode | Purpose |
|---|---|
| Wi-Fi Scanner | Lists nearby access points with SSID, RSSI, channel, and security |
| Wi-Fi Visualizer | Draws a simplified channel/signal graph on the OLED |
| Channel Analyzer | Shows 2.4 GHz channel congestion |
| Signal Monitor | Shows RSSI and quality for a selected network |
| Open Networks | Filters and displays open/unsecured networks |
| AP Mode | Starts the ESP32 as a local access point |
| Network Info | Shows STA connection info such as IP, RSSI, and channel |
| Ping Test | Pings a target such as `8.8.8.8` |
| Internet Check | Checks whether internet access is available |
| HTTP Test | Sends a basic HTTP GET request |
| Signal Heat Walk | Saves RSSI points while moving around |

---

## BLE Tools

| Mode | Purpose |
|---|---|
| BLE Scanner | Lists nearby BLE advertisers |
| Proximity Monitor | Estimates near/medium/far using RSSI |
| Beacon Detector | Detects beacon-like advertisements using manufacturer data |
| Signal Tracker | Tracks RSSI stats for a selected BLE device |
| Service Viewer | Shows advertised UUIDs and BLE advertisement details |
| Advertiser Mode | Makes the ESP32 advertise as a BLE device |
| BLE UART Mode | Provides BLE serial-style communication using Nordic UART UUIDs |

---

## Web Server Tools

| Mode | Purpose |
|---|---|
| Start Server | Starts the ESP32 AP and local web server |
| Info | Shows how to connect to the ESP32 dashboard |
| Captive Page | Starts local DNS redirect for a captive-style info page |
| Control Panel | Browser page for simple ESP32 control actions |
| Live Monitor | Browser page that refreshes `/status` data live |

Default AP details:

```text
SSID: NetViewer_AP
Password: 12345678
IP: 192.168.4.1
```

---

## Device Utility

| Mode | Purpose |
|---|---|
| Device Status | Shows uptime, heap, CPU speed, flash info, chip info, and button states |
| Settings | Adjusts OLED brightness, auto-refresh, invert placeholder, and sleep timeout placeholder |

---

## Arduino IDE Setup

Recommended settings:

| Setting | Value |
|---|---|
| Board | ESP32 Dev Module |
| CPU Frequency | 240 MHz |
| Partition Scheme | Huge APP or large app partition |
| Upload Speed | 115200 if high-speed upload fails |

---

## Notes

- Wi-Fi scan, AP mode, BLE scan, and local web server features work without router credentials.
- Ping, Internet Check, HTTP Test, and STA Network Info require the ESP32 to connect to a compatible 2.4 GHz Wi-Fi network.
- Open Wi-Fi networks work well for testing. Captive portal networks may connect but still block internet tests.
- BLE modes use the classic ESP32 BLE library included with the ESP32 board package, not NimBLE.

---

## Safe Use

NetViewer is meant for legal and ethical diagnostics only. It should be used for:

- Scanning visible Wi-Fi networks
- Viewing BLE advertisements
- Testing your own network connectivity
- Hosting your own ESP32 dashboard
- Learning embedded wireless development

Do not use it for unauthorized access, credential capture, phishing, deauthentication, or attacking devices/networks.

---

## Project Status

Current status: **Finished and fully functional**

NetViewer has reached a complete working version. The final firmware merges all four planned menu groups into one stable handheld interface:

Wi-Fi Tools
BLE Tools
Web Server
Device Utility

The project now includes a working SSD1309 portrait OLED interface, a Wi-Fi splash screen, stable three-button navigation, submenu BACK options, and fully integrated Wi-Fi, BLE, web/server, and device utility modes. The final hardware mapping uses GPIO32 for UP, GPIO26 for DOWN, and GPIO27 for MENU/SELECT after testing showed GPIO32 was more reliable during Wi-Fi activity.

The Wi-Fi tools support scanning, channel analysis, signal viewing, open network detection, AP mode, network tests, HTTP testing, and a visualizer mode. The BLE tools support scanning, proximity monitoring, beacon detection, signal tracking, service viewing, advertising, and BLE UART. The web/server tools provide a local dashboard, info page, captive-style page, control panel, and live monitor. The utility menu provides device status and basic OLED settings.


Future improvements may include saved settings, better BLE service discovery, true WebSocket support, scan history, and data export.

Contributors
Victor Stafussi Granado – Embedded Systems Design, Firmware Development, System Integration

License
This project is licensed under the MIT License.
See the LICENSE file for details.
