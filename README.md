# NetViewer — ESP32 Wireless Utility Console and Proto-Penetration Tool

NetViewer is a minimal handheld wireless utility console built around the **ESP32-WROOM-DA**, a **2.4-inch SSD1309 I2C OLED display**, and **three push buttons**. The project focuses heavily on **Wi-Fi tools**, **BLE tools**, and **local web/server utilities**, while keeping the hardware simple and portable.

The goal is to create a compact device that can scan nearby wireless activity, monitor signal strength, host a local web dashboard, advertise over BLE, and provide useful ESP32 network diagnostics from a small OLED-based interface.

---

## Project Identity

NetViewer is designed as a small field-use wireless console, not a general-purpose cyberdeck or sensor station. It is focused on:

- Wi-Fi discovery and diagnostics
- BLE discovery and tracking
- ESP32-hosted web tools
- Local network testing
- Minimal physical controls
- Clean menu-based OLED interface

---

## Main Features

- SSD1309 OLED graphical interface
- 3-button menu navigation
- Animated Wi-Fi splash screen on boot
- Wi-Fi network scanner
- Wi-Fi channel analyzer
- Wi-Fi signal monitor
- Open network detector
- ESP32 access point mode
- Network information viewer
- Ping test mode
- Internet connection checker
- HTTP/web request tester
- Signal heat walk mode
- BLE scanner
- BLE proximity monitor
- BLE beacon detector
- BLE signal tracker
- BLE service viewer starter mode
- BLE advertiser mode
- BLE UART starter mode
- ESP32 web server dashboard
- Captive portal info page starter
- Local web control panel starter
- WebSocket live monitor placeholder
- Device status mode
- Settings mode

---

## Hardware Scope

| Component | Description |
|---|---|
| ESP32-WROOM-DA | Main microcontroller with Wi-Fi and BLE |
| 2.4-inch OLED Display | I2C OLED using SSD1309 driver |
| 3 Push Buttons | Up, Down, Menu/Select |
| Power Circuit | Battery, charging, regulation, and power switch |
| Optional Battery Divider | For battery voltage monitoring |

---

## Display

The project is designed around an **SSD1309 128x64 I2C OLED display**.

Recommended display library:

```cpp
#include <U8g2lib.h>
#include <Wire.h>
```

OLED constructor used in the starter firmware:

```cpp
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
```

If your display does not show correctly, you may need to test a different U8g2 SSD1309 constructor depending on the exact module variant.

---

## Pin Mapping

| Component | ESP32 Pin |
|---|---|
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| Button Up | GPIO25 |
| Button Down | GPIO26 |
| Button Menu/Select | GPIO27 |
| Optional Battery Monitor | GPIO34 |

### OLED Wiring

| OLED Pin | ESP32 Connection |
|---|---|
| VCC | 3.3V preferred |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

### Button Wiring

Each button should be wired between the ESP32 GPIO pin and GND.

The firmware uses internal pull-up resistors:

```cpp
pinMode(BTN_UP_PIN, INPUT_PULLUP);
pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
pinMode(BTN_MENU_PIN, INPUT_PULLUP);
```

Because of this:

- Released button = HIGH
- Pressed button = LOW

---

## Button Controls

| Button | Menu Action | Mode Action |
|---|---|---|
| Up | Move selection up | Previous item / save point / toggle setting |
| Down | Move selection down | Next item / clear points / adjust setting |
| Menu/Select | Enter selected menu or mode | Return to main menu |

The current firmware uses a simple press-based navigation system. Long-press controls can be added later for shortcuts such as refresh, back, clear, or mode-specific actions.

---

## Locked Mode Scope

### Wi-Fi Tools

| # | Mode | Purpose |
|---|---|---|
| 1 | Wi-Fi Scanner | Scan nearby Wi-Fi access points |
| 2 | Wi-Fi Channel Analyzer | Show channel congestion, especially on 2.4 GHz |
| 3 | Wi-Fi Signal Monitor | Monitor RSSI and quality of a selected network |
| 4 | Open Network Detector | Show only unsecured/open Wi-Fi networks |
| 5 | Wi-Fi Access Point Mode | Start the ESP32 as a local access point |
| 6 | Network Info Mode | Show IP, gateway, RSSI, and connection details |
| 7 | Ping Test Mode | Ping a target IP or host |
| 8 | Internet Check Mode | Test whether the ESP32 has internet access |
| 9 | Web Request Tester | Send a simple HTTP request and display the response status |
| 10 | Signal Heat Walk Mode | Save RSSI readings at different physical locations |

---

### BLE Tools

| # | Mode | Purpose |
|---|---|---|
| 1 | BLE Scanner | Scan nearby BLE advertising devices |
| 2 | BLE Proximity Monitor | Estimate relative proximity using BLE RSSI |
| 3 | BLE Beacon Detector | Detect beacon-like BLE advertisers |
| 4 | BLE Signal Tracker | Track RSSI of a selected BLE device |
| 5 | BLE Service Viewer | Display advertised BLE service UUIDs |
| 6 | BLE Advertiser Mode | Make the ESP32 advertise itself as a BLE device |
| 7 | BLE UART Mode | Use BLE as a wireless serial-style communication channel |

---

### Web / Server Tools

| # | Mode | Purpose |
|---|---|---|
| 1 | ESP32 Web Server Dashboard | Host a web dashboard showing scan results and device status |
| 2 | Web Dashboard Mode | Display access instructions for the web dashboard |
| 3 | Captive Portal Info Page | Start a local captive-portal-style information page |
| 4 | Local Web Server Control Panel | Provide browser-based device controls |
| 5 | WebSocket Live Monitor | Planned live browser update mode |

---

### Device / Utility

| # | Mode | Purpose |
|---|---|---|
| 1 | Device Status Mode | Show ESP32 uptime, heap memory, CPU frequency, and status |
| 2 | Settings Mode | Adjust simple device settings such as brightness and auto-refresh |

---

## Menu Structure

```text
MAIN MENU

1. Wi-Fi Tools
2. BLE Tools
3. Web / Server
4. Device / Utility
```

### Wi-Fi Tools Menu

```text
Wi-Fi Tools

1. Wi-Fi Scanner
2. Channel Analyzer
3. Signal Monitor
4. Open Networks
5. AP Mode
6. Network Info
7. Ping Test
8. Internet Check
9. Web Request Test
10. Signal Heat Walk
```

### BLE Tools Menu

```text
BLE Tools

1. BLE Scanner
2. Proximity Monitor
3. Beacon Detector
4. Signal Tracker
5. Service Viewer
6. Advertiser Mode
7. BLE UART Mode
```

### Web / Server Menu

```text
Web / Server

1. Web Server Dash
2. Dashboard Mode
3. Captive Portal
4. Control Panel
5. WebSocket Monitor
```

### Device / Utility Menu

```text
Device / Utility

1. Device Status
2. Settings
```

---

## Splash Screen

On boot, the firmware displays a **3-second animated Wi-Fi splash screen**.

The animation includes:

- Project name
- Wi-Fi symbol
- Expanding/radiating signal arcs
- Moving signal particles
- Small text label: `WiFi BLE Web`

This gives the project a polished startup sequence while keeping the display simple.

---

## Software Requirements

### Arduino IDE / PlatformIO

This project can be built with either:

- Arduino IDE
- PlatformIO

Arduino IDE is recommended for the first hardware test.

---

## Required Libraries

Install the following libraries:

| Library | Purpose |
|---|---|
| U8g2 | SSD1309 OLED graphics |
| NimBLE-Arduino | BLE scanning, advertising, and BLE UART support |
| ESP32Ping | Ping and connection testing |
| ESPAsyncWebServer | Future async dashboard and WebSocket support |
| AsyncTCP | Required by ESPAsyncWebServer |

The current starter firmware uses the built-in `WebServer` library for the first dashboard implementation, but `ESPAsyncWebServer` and `AsyncTCP` are recommended for the later WebSocket version.

---

## Core Includes

The firmware uses:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESP32Ping.h>
#include <U8g2lib.h>
#include <NimBLEDevice.h>
```

---

## Board Setup

Recommended Arduino IDE settings:

| Setting | Recommended Value |
|---|---|
| Board | ESP32 Dev Module |
| USB CDC On Boot | Disabled unless needed |
| CPU Frequency | 240 MHz |
| Flash Frequency | 80 MHz |
| Flash Mode | QIO or DIO |
| Partition Scheme | Huge APP or large app partition |
| Upload Speed | 921600 or 115200 if upload fails |

If the sketch becomes large because of Wi-Fi, BLE, display, and web features, use a larger app partition scheme.

---

## Current Firmware Status

The first firmware version is a full project framework. Some modes are functional now, while others are starter implementations prepared for future expansion.

### Functional / Starter-Functional

- OLED display initialization
- Button navigation
- Splash screen animation
- Main menu and submenus
- Wi-Fi scan
- Wi-Fi channel analyzer
- Open network detector
- Wi-Fi signal monitor
- ESP32 access point mode
- Basic web dashboard
- Captive portal redirect starter
- Network info screen
- Ping test
- Internet check
- HTTP request test
- Signal heat walk point saving
- BLE scan
- BLE proximity monitor
- BLE beacon-like detection
- BLE signal tracker
- BLE advertised service viewer
- BLE advertiser starter
- BLE UART starter
- Device status
- Settings screen

### Planned Refinements

- Better Wi-Fi scan refresh controls
- Long-press button actions
- Persistent settings storage
- Better battery calibration
- Real BLE service connection and characteristic browsing
- More complete BLE UART command system
- Full browser-based control panel
- Async WebSocket live updates
- CSV or JSON export
- Optional SD logging
- Saved known network list
- Signal history graphing
- Improved captive portal compatibility

---

## Wi-Fi Mode Details

### Wi-Fi Scanner

Scans nearby access points and displays:

- SSID
- RSSI
- Channel
- Security type
- Hidden network status

The scan results are sorted strongest-first.

---

### Wi-Fi Channel Analyzer

Counts nearby networks by Wi-Fi channel.

The starter firmware focuses on common 2.4 GHz channels:

- Channel 1
- Channel 6
- Channel 11

This helps identify which router channels are more congested.

---

### Wi-Fi Signal Monitor

Uses scan results to display the RSSI of a selected network.

Shows:

- Selected SSID
- RSSI in dBm
- Signal quality percentage
- Basic signal bar

---

### Open Network Detector

Filters the Wi-Fi scan results and only displays networks using open authentication.

This is a passive diagnostic mode and does not connect to or attack networks.

---

### Wi-Fi Access Point Mode

Starts the ESP32 as a local Wi-Fi access point.

Default starter values:

```cpp
const char* AP_SSID = "PocketNet_AP";
const char* AP_PASS = "12345678";
```

Default AP address:

```text
192.168.4.1
```

---

### Network Info Mode

Displays connection information when the ESP32 is connected to a Wi-Fi network.

Shows:

- SSID
- Local IP
- Gateway IP
- RSSI

---

### Ping Test Mode

Uses the ESP32Ping library to ping a target.

Default target:

```cpp
const char* PING_TARGET = "8.8.8.8";
```

---

### Internet Check Mode

Checks whether the ESP32 has a working internet path.

Default test host:

```cpp
const char* INTERNET_TEST_HOST = "example.com";
```

---

### Web Request Tester

Sends a simple HTTP GET request.

Default URL:

```cpp
const char* HTTP_TEST_URL = "http://example.com";
```

Displays:

- HTTP status code
- Request time
- Failure state if not connected

---

### Signal Heat Walk Mode

A manual signal mapping mode.

Workflow:

1. Select a Wi-Fi network.
2. Walk to a location.
3. Press Up to save the current RSSI point.
4. Move to another location.
5. Save another point.
6. Press Down to clear saved points.

This can be expanded later into a graph or exportable signal map.

---

## BLE Mode Details

### BLE Scanner

Scans BLE advertising devices.

Displays:

- Device name
- MAC address
- RSSI
- Advertised UUID when available

---

### BLE Proximity Monitor

Uses RSSI to estimate relative proximity.

Example categories:

- Near
- Medium
- Far

This is not true distance measurement. BLE RSSI changes with walls, antenna orientation, interference, and device transmit power.

---

### BLE Beacon Detector

Detects beacon-like devices by checking for manufacturer data in BLE advertisements.

This can later be expanded to specifically identify:

- iBeacon-like advertisements
- Eddystone-like advertisements
- Repeated BLE broadcasters

---

### BLE Signal Tracker

Tracks the RSSI of one selected BLE device and displays a signal bar.

---

### BLE Service Viewer

The starter version displays advertised service UUIDs.

Future version goal:

- Connect to a BLE device
- Discover services
- Discover characteristics
- Display readable/writable/notify properties

---

### BLE Advertiser Mode

Makes the ESP32 advertise as a BLE device named:

```cpp
PocketNet
```

The starter version includes a simple BLE status/battery-style service.

---

### BLE UART Mode

Creates a BLE UART-style communication service using Nordic UART Service UUIDs.

This can be tested using apps such as:

- nRF Connect
- Serial Bluetooth Terminal
- LightBlue

Future command ideas:

```text
scan_wifi
scan_ble
status
ap_on
ap_off
restart
```

---

## Web / Server Mode Details

### ESP32 Web Server Dashboard

Hosts a local webpage that displays:

- Device name
- Uptime
- Free heap memory
- Wi-Fi scan results
- BLE scan results
- Status links

Starter dashboard path:

```text
/
```

Status endpoint:

```text
/status
```

---

### Web Dashboard Mode

Displays instructions on the OLED for connecting to the ESP32 dashboard.

Typical flow:

1. Open Wi-Fi settings on phone/laptop.
2. Connect to `PocketNet_AP`.
3. Open `192.168.4.1` in a browser.

---

### Captive Portal Info Page

Starts DNS redirection so unknown web requests redirect to the ESP32 dashboard.

This is intended for a local info page and device control page.

---

### Local Web Server Control Panel

Planned browser-based control panel.

Future controls:

- Start Wi-Fi scan
- Start BLE scan
- Toggle BLE advertiser
- Toggle settings
- View logs
- Restart device
- Download scan data

---

### WebSocket Live Monitor

Planned live browser dashboard mode.

Future live data:

- Wi-Fi RSSI
- BLE device count
- Uptime
- Heap memory
- Battery voltage
- Current mode

---

## Device / Utility Details

### Device Status Mode

Displays:

- Device name
- Uptime
- Free heap memory
- CPU frequency

Possible future additions:

- Flash size
- Chip model
- SDK version
- Wi-Fi MAC
- BLE MAC
- Battery voltage

---

### Settings Mode

Starter settings include:

- Auto-refresh toggle
- Screen brightness toggle

Future settings:

- Scan duration
- AP SSID
- AP password
- Button long-press behavior
- Display timeout
- BLE scan mode
- Web dashboard enable/disable
- Saved Wi-Fi target

---

## Power System Notes

The power circuit can be designed around a rechargeable Li-ion/LiPo battery.

Typical options:

| Module | Purpose |
|---|---|
| TP4056 | Li-ion charging |
| Protection circuit | Battery protection |
| Boost converter | Boost battery voltage to 5V if needed |
| 3.3V regulator | Stable ESP32 power |
| Master switch | Main power control |
| Voltage divider | Battery voltage sensing |

For ESP32 stability, make sure the power circuit can provide enough current during Wi-Fi and BLE activity. ESP32 current spikes can cause resets if the regulator is weak.

---

## Optional Battery Monitor

GPIO34 can be used as an ADC input for battery voltage.

Because ESP32 ADC pins can only read limited voltage, use a voltage divider.

Example:

```text
Battery + ---- R1 ---- GPIO34 ---- R2 ---- GND
```

The starter firmware has placeholder calibration:

```cpp
float voltage = (raw / 4095.0) * 3.3 * 2.0;
```

Adjust the multiplier depending on the resistor divider values.

---

## Safe Use Notes

PocketNet is intended as a passive wireless utility and local diagnostic tool.

This project should focus on:

- Scanning visible Wi-Fi networks
- Viewing BLE advertisements
- Monitoring signal strength
- Hosting your own local web interface
- Testing your own network connectivity

This project should not include:

- Wi-Fi deauthentication attacks
- Credential capture
- Phishing portals
- Unauthorized network access
- BLE attacks against devices you do not own or have permission to test

---

## Suggested Repository Structure

```text
PocketNet/
├── README.md
├── firmware/
│   └── PocketNet/
│       └── PocketNet.ino
├── docs/
│   ├── hardware.md
│   ├── pin-map.md
│   ├── modes.md
│   └── setup.md
├── images/
│   ├── prototype.jpg
│   ├── wiring-diagram.png
│   └── oled-ui-preview.png
└── extras/
    └── notes.md
```

---

## Development Phases

### Phase 1 — Hardware Bring-Up

- Wire ESP32 to SSD1309 OLED
- Test display
- Wire three buttons
- Test button input
- Show splash screen
- Show basic menu

### Phase 2 — Wi-Fi Tools

- Implement Wi-Fi scan
- Sort networks by RSSI
- Add channel analyzer
- Add open network detector
- Add signal monitor
- Add AP mode

### Phase 3 — BLE Tools

- Implement BLE scan
- Add BLE RSSI tracking
- Add beacon detector
- Add BLE advertiser
- Add BLE UART starter

### Phase 4 — Web Tools

- Start ESP32 AP
- Host dashboard
- Display scan results
- Add status endpoint
- Add captive portal behavior
- Add control panel actions

### Phase 5 — Polish

- Improve UI layout
- Add long-press button actions
- Add persistent settings
- Add better signal graphs
- Add scan refresh shortcuts
- Add optional logging/export
- Design enclosure

---

## First Build Checklist

- [ ] Install ESP32 board support in Arduino IDE
- [ ] Install U8g2 library
- [ ] Install NimBLE-Arduino library
- [ ] Install ESP32Ping library
- [ ] Wire SSD1309 OLED to GPIO21/GPIO22
- [ ] Wire buttons to GPIO25/GPIO26/GPIO27 and GND
- [ ] Upload OLED test sketch
- [ ] Upload PocketNet firmware
- [ ] Confirm splash screen displays
- [ ] Confirm menu navigation works
- [ ] Test Wi-Fi scanner
- [ ] Test BLE scanner
- [ ] Test AP mode
- [ ] Open web dashboard from phone or laptop

---

## Known Limitations in Starter Firmware

- WebSocket monitor is currently a placeholder.
- BLE service viewer currently shows advertised service UUIDs, not full connected service discovery.
- BLE UART mode is a starter implementation.
- Web control panel is not fully interactive yet.
- Wi-Fi STA credentials are not stored yet.
- Battery voltage calculation requires hardware-specific calibration.
- Long-press button shortcuts are not implemented yet.
- Some library APIs may need adjustment depending on your installed ESP32 core and NimBLE-Arduino versions.

---

## Future Improvements

- Add saved Wi-Fi credentials
- Add Wi-Fi connection manager
- Add known network finder
- Add scan result history
- Add JSON export
- Add CSV export
- Add optional MicroSD logging
- Add battery percentage estimation
- Add OLED sleep mode
- Add better signal graphing
- Add WebSocket live dashboard
- Add full BLE service/characteristic explorer
- Add BLE UART command parser
- Add settings stored in NVS/preferences
- Add enclosure and button labels
- Add wiring diagram and photos

---

## Example Use Cases

- Check nearby Wi-Fi networks
- See crowded Wi-Fi channels
- Find stronger/weaker Wi-Fi spots
- Monitor BLE devices nearby
- Test BLE advertising
- Use ESP32 as a local dashboard server
- Test basic network connectivity
- Learn ESP32 Wi-Fi, BLE, OLED, and web server programming
- Build a portfolio-ready embedded wireless tool

---

## License

This project can be released under the MIT License.

Suggested license:

```text
MIT License
Copyright (c) 2026 Victor Granado
```

---

## Project Status

Current status: **Firmware framework started**

The first version includes the complete menu structure and starter implementations for all locked modes. The next step is to compile the firmware on the target ESP32 setup, fix any library-specific compile issues, and then refine each mode into a polished final feature.

