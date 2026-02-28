```
              _                                                   _
         .k$$$$$g,                                           ,g$$$$$k.
      .k$$$$$$$$$$$a.                                     .a$$$$$$$$$$$k.
    .J$$$$$?'   `?$?^?,                                 ,?^?$?`   `?$$$$$L.
   JS$$SI!a,  _.JS$   ?,                               ,?   $SL._  ,a$!IS$$SL
  k$$$SI!:?$$$$$$$$$xu$$j                              j$$ux$$$$$$$$$?:!IS$$$k
 :I$$SI:J$$?*"$$$$4^?*?:                               :?*?^4$$$$"*?$$L:iIS$$I:
 :IS$$SiJ?`  _.'$?`/'   ':                           :'    '/'?$'._  `?LiS$$SI:
  ?ISSik? _        ',    `                              .    ,'       _ ?kiSSI?
    ?i$?` _   k$        .                               :.        $k   _ `?$i?
      '?I:-?z$$I   _._.'                                  ._._   I$$z?-:I?'
     '*?- '?$$a louSxuS?                               ?xuSxuol a$$?' -?*'
           i$$$$$$$$$$$S                               S$$$$$$$$$$$i
              ?$$$?-                                       -?$$$?

     ██░ ██  ▄▄▄       ██▓    ▓█████  ██░ ██  ▒█████   █    ██  ███▄    █ ▓█████▄
    ▓██░ ██▒▒████▄    ▓██▒    ▓█   ▀ ▓██░ ██▒▒██▒  ██▒ ██  ▓██▒ ██ ▀█   █ ▒██▀ ██▌
    ▒██▀▀██░▒██  ▀█▄  ▒██░    ▒███   ▒██▀▀██░▒██░  ██▒▓██  ▒██░▓██  ▀█ ██▒░██   █▌
    ░▓█ ░██ ░██▄▄▄▄██ ▒██░    ▒▓█  ▄ ░▓█ ░██ ▒██   ██░▓▓█  ░██░▓██▒  ▐▌██▒░▓█▄   ▌
    ░▓█▒░██▓ ▓█   ▓██▒░██████▒░▒████▒░▓█▒░██▓░ ████▓▒░▒▒█████▓ ▒██░   ▓██░░▒████▓
     ▒ ░░▒░▒ ▒▒   ▓▒█░░ ▒░▓  ░░░ ▒░ ░ ▒ ░░▒░▒░ ▒░▒░▒░ ░▒▓▒ ▒ ▒ ░ ▒░   ▒ ▒  ▒▒▓  ▒
     ▒ ░▒░ ░  ▒   ▒▒ ░░ ░ ▒  ░ ░ ░  ░ ▒ ░▒░ ░  ░ ▒ ▒░ ░░▒░ ░ ░ ░ ░░   ░ ▒░ ░ ▒  ▒
     ░  ░░ ░  ░   ▒     ░ ░      ░    ░  ░░ ░░ ░ ░ ▒   ░░░ ░ ░    ░   ░ ░  ░ ░  ░
     ░  ░  ░      ░  ░    ░  ░   ░  ░ ░  ░  ░    ░ ░     ░              ░    ░

              _                                                   _
         .k$$$$$g,                                           ,g$$$$$k.
      .k$$$$$$$$$$$a.                                     .a$$$$$$$$$$$k.
    .J$$$$$?'   `?$?^?,                                 ,?^?$?`   `?$$$$$L.
   JS$$SI!a,  _.JS$   ?,                               ,?   $SL._  ,a$!IS$$SL
  k$$$SI!:?$$$$$$$$$xu$$j                              j$$ux$$$$$$$$$?:!IS$$$k
 :I$$SI:J$$?*"$$$$4^?*?:                              :?*?^4$$$$"*?$$L:iIS$$I:
 :IS$$SiJ?`  _.'$?`/'  ':                            :'    '/'?$'._  `?LiS$$SI:
  ?ISSik? _        ',  .                                .    ,'       _ ?kiSSI?
    ?i$?` _  k$        .:                              :.        $k   _ `?$i?
      '?I:-?z$$I   _._.'                                  ._._   I$$z?-:I?'
     '*?- '?$$a louSxuS?                               ?xuSxuol a$$?' -?*'
           i$$$$$$$$$$$S                               S$$$$$$$$$$$i
              ?$$$?-                                       -?$$$?
```

# HaleHound-CYD

**ESP32-DIV HaleHound Edition for Cheap Yellow Display**

Version **v3.0.0 CYD Edition** | By [JesseCHale](https://github.com/JesseCHale)

---

## Overview

HaleHound-CYD is a multi-protocol offensive security toolkit built for the ESP32-2432S028 "Cheap Yellow Display" (CYD) platform. It ports the full ESP32-DIV HaleHound v2.5.0 firmware to the CYD's 2.8" touchscreen form factor, adding external CC1101 SubGHz, NRF24L01+PA+LNA 2.4GHz, and GPS radios via the CYD's breakout pins.

Every attack module from the original ESP32-DIV is present, plus CYD-exclusive features: full touchscreen navigation, EAPOL/PMKID capture, Karma attacks, wardriving with GPS logging, UART serial monitor for hardware hacking, and OTA firmware updates from SD card.

All radios transmit at maximum power. No safety nets.

---

## Table of Contents

- [Hardware Requirements](#hardware-requirements)
- [Supported Boards](#supported-boards)
- [Complete Wiring Guide](#complete-wiring-guide)
- [Menu Tree](#menu-tree)
- [Attack Modules - Detailed](#attack-modules---detailed)
  - [WiFi Attacks](#wifi-attacks)
  - [Bluetooth Attacks](#bluetooth-attacks)
  - [2.4GHz NRF24 Attacks](#24ghz-nrf24-attacks)
  - [SubGHz CC1101 Attacks](#subghz-cc1101-attacks)
  - [SIGINT Operations](#sigint-operations)
  - [Tools](#tools)
  - [Settings](#settings)
- [Touch Navigation](#touch-navigation)
- [TX Power Configuration](#tx-power-configuration)
- [SD Card Structure](#sd-card-structure)
- [Build and Flash](#build-and-flash)
- [Pin Reference Table](#pin-reference-table)
- [SPI Bus Sharing](#spi-bus-sharing)
- [Known Issues](#known-issues)
- [Project Structure](#project-structure)

---

## Hardware Requirements

### Base Board

| Component | Specification |
|-----------|--------------|
| Board | ESP32-2432S028 (CYD 2.8") |
| MCU | ESP32-WROOM-32 or ESP32-WROOM-32UE |
| Display | 2.8" ILI9341 320x240 TFT |
| Touch | XPT2046 Resistive (separate SPI bus) |
| Flash | 4MB minimum (16MB recommended) |
| USB | CH340C USB-Serial (Micro-USB or USB-C) |
| SD Card | Built-in MicroSD slot (VSPI bus) |
| Power | 5V USB or LiPo + boost converter |

### External Modules (All Required for Full Functionality)

| Module | Model | Purpose |
|--------|-------|---------|
| SubGHz Radio | CC1101 HW-863 (red board) | 300-928 MHz signal capture/replay/jam |
| 2.4GHz Radio | NRF24L01+PA+LNA | WiFi/BLE/Zigbee jamming, MouseJack |
| GPS | GT-U7 or NEO-6M | Wardriving, location logging |

### Optional

| Component | Purpose |
|-----------|---------|
| 10uF capacitor | NRF24 power stability (across VCC/GND at module) |
| MicroSD card | Payload storage, PCAP saves, wardriving logs |
| LiPo battery + boost converter | Portable operation |
| External antenna (u.FL) | Extended range (if using ESP32-WROOM-32UE) |

---

## Supported Boards

| Board | Define | Display | Status |
|-------|--------|---------|--------|
| ESP32-2432S028 (2.8") | `CYD_28` | 240x320 ILI9341 | **Primary - Fully Tested** |
| ESP32-3248S035 (3.5") | `CYD_35` | 320x480 ST7796 | Pin defines ready, untested |

Board selection is in `cyd_config.h` line 14:
```cpp
#define CYD_28    // ESP32-2432S028 - 2.8" 320x240 ILI9341
//#define CYD_35    // ESP32-3248S035 - 3.5" 480x320 ST7796
```

---

## Complete Wiring Guide

### SPI Bus Architecture

The CYD has **two independent SPI buses**. Understanding this is critical:

```
  ┌─────────────────────────────────────────────────────────────┐
  │                    HSPI BUS (Display)                       │
  │  MISO=GPIO12  MOSI=GPIO13  SCLK=GPIO14  CS=GPIO15         │
  │  ┌─────────┐                                               │
  │  │ ILI9341 │  DC=GPIO2  BL=GPIO21  RST=EN                 │
  │  │ Display │                                               │
  │  └─────────┘                                               │
  └─────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────┐
  │              SEPARATE TOUCH SPI (Bit-Banged)                │
  │  CLK=GPIO25  MOSI=GPIO32  MISO=GPIO39  CS=GPIO33           │
  │  ┌──────────┐                                              │
  │  │ XPT2046  │  IRQ=GPIO36                                  │
  │  │  Touch   │                                              │
  │  └──────────┘                                              │
  └─────────────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────────────┐
  │                 VSPI BUS (Shared Radio Bus)                 │
  │  SCK=GPIO18   MOSI=GPIO23   MISO=GPIO19                    │
  │                                                             │
  │  ┌──────────┐  ┌──────────┐  ┌──────────┐                 │
  │  │ SD Card  │  │  CC1101  │  │  NRF24   │                 │
  │  │ CS=GPIO5 │  │ CS=GPIO27│  │ CSN=GPIO4│                 │
  │  │ (built-in│  │ GDO0=G22 │  │ CE=GPIO16│                 │
  │  │  slot)   │  │ GDO2=G35 │  │ IRQ=G17  │                 │
  │  └──────────┘  └──────────┘  └──────────┘                 │
  │                                                             │
  │  IMPORTANT: Only ONE device active at a time!               │
  │  Pull target CS LOW, all others HIGH before SPI transfer.   │
  └─────────────────────────────────────────────────────────────┘
```

### CC1101 SubGHz Radio Wiring

```
┌─────────────────┐              ┌──────────────────┐
│    CC1101        │              │    CYD ESP32      │
│    HW-863        │              │                   │
├─────────────────┤              ├──────────────────┤
│ VCC ─────────────┼──────────────┤ 3.3V              │
│ GND ─────────────┼──────────────┤ GND               │
│ SCK ─────────────┼──────────────┤ GPIO 18 (VSPI)    │
│ MOSI ────────────┼──────────────┤ GPIO 23 (VSPI)    │
│ MISO ────────────┼──────────────┤ GPIO 19 (VSPI)    │
│ CS ──────────────┼──────────────┤ GPIO 27 (CN1 hdr) │
│ GDO0 (TX) ───────┼──────────────┤ GPIO 22 (P3 hdr)  │
│ GDO2 (RX) ───────┼──────────────┤ GPIO 35 (P3 hdr)  │
└─────────────────┘              └──────────────────┘
```

**GDO0/GDO2 Pin Naming Fix:** The original ESP32-DIV firmware (CiferTech) had TX and RX **swapped**. HaleHound corrects this:
- **GDO0** (GPIO 22) = Data going **TO** the CC1101 (TX line)
- **GDO2** (GPIO 35) = Data coming **FROM** the CC1101 (RX line)

GPIO 35 is input-only on ESP32, which is correct for RX.

### NRF24L01+PA+LNA Wiring

```
┌─────────────────┐              ┌──────────────────┐
│  NRF24L01        │              │    CYD ESP32      │
│  +PA+LNA         │              │                   │
├─────────────────┤              ├──────────────────┤
│ VCC ─────────────┼──────────────┤ 3.3V              │
│ GND ─────────────┼──────────────┤ GND               │
│ SCK ─────────────┼──────────────┤ GPIO 18 (VSPI)    │
│ MOSI ────────────┼──────────────┤ GPIO 23 (VSPI)    │
│ MISO ────────────┼──────────────┤ GPIO 19 (VSPI)    │
│ CSN ─────────────┼──────────────┤ GPIO 4  (was RGB R)│
│ CE ──────────────┼──────────────┤ GPIO 16 (was RGB G)│
│ IRQ (optional) ──┼──────────────┤ GPIO 17 (was RGB B)│
└─────────────────┘              └──────────────────┘
```

**Power Note:** The +PA+LNA version draws significant current. If you get random resets or failed init, solder a **10uF capacitor** between VCC and GND directly at the NRF24 module.

**Pin Repurposing:** The CYD's RGB LED pins (GPIO 4, 16, 17) are sacrificed for the NRF24. The RGB LED is disabled in firmware (`CYD_HAS_RGB_LED = 0`).

### GPS Module Wiring

```
┌─────────────────┐              ┌──────────────────┐
│    GT-U7         │              │  CYD P1 Connector │
│    GPS           │              │  (JST header)     │
├─────────────────┤              ├──────────────────┤
│ VCC ─────────────┼──────────────┤ VIN               │
│ GND ─────────────┼──────────────┤ GND               │
│ TX ──────────────┼──────────────┤ RX (GPIO 3)       │
│ RX (not used) ───┼──────────────┤ TX (GPIO 1)       │
└─────────────────┘              └──────────────────┘
```

**USB Conflict:** GPIO 3 is shared with the CH340C USB serial RX. When GPS is active, the firmware calls `Serial.end()` to release GPIO 3 for UART2 remapping. Debug output via `Serial.println()` (TX on GPIO 1) still works. GPS restores normal serial on exit.

### SD Card

The MicroSD slot is **built into the CYD board** on the back. No external wiring needed.
- CS = GPIO 5 (active LOW)
- Shares VSPI bus with radios (GPIO 18/19/23)
- The SPI manager deconflicts access automatically

---

## Menu Tree

```
HALEHOUND-CYD v3.0.0
│
├── WiFi ──────────────────────────────────────────────────
│   ├── Packet Monitor ......... Real-time 802.11 frame capture
│   ├── Beacon Spammer ......... Flood SSIDs (custom or random)
│   ├── WiFi Deauther .......... Target deauth with network scan
│   ├── Probe Sniffer .......... Capture probe requests → Evil Twin
│   ├── WiFi Scanner ........... Scan APs → Tap-to-Deauth/Clone
│   ├── Captive Portal ......... GARMR Evil Twin credential harvest
│   ├── Station Scanner ........ Enumerate connected clients
│   ├── Auth Flood ............. 802.11 auth frame flood attack
│   └── Back to Main Menu
│
├── Bluetooth ─────────────────────────────────────────────
│   ├── BLE Jammer ............. Flood advertisements
│   ├── BLE Spoofer ............ Clone device identities
│   ├── BLE Beacon ............. Broadcast custom BLE beacons
│   ├── Sniffer ................ Passive BLE traffic analysis
│   ├── BLE Scanner ............ Discover nearby BLE devices
│   ├── WhisperPair ............ CVE-2025-36911 Fast Pair scanner
│   ├── AirTag Detect .......... Apple FindMy tracker detection
│   └── Back to Main Menu
│
├── 2.4GHz (NRF24) ───────────────────────────────────────
│   ├── Scanner ................ Channel activity scanner
│   ├── Spectrum Analyzer ...... Visual RF spectrum display
│   ├── WLAN Jammer ............ 2.4GHz broadband disruption
│   ├── Proto Kill ............. Multi-protocol attack suite
│   └── Back to Main Menu
│
├── SubGHz (CC1101) ──────────────────────────────────────
│   ├── Replay Attack .......... Record and replay RF signals
│   ├── Brute Force ............ Automated code generation
│   ├── SubGHz Jammer .......... Wideband SubGHz disruption
│   ├── Spectrum Analyzer ...... SubGHz RF spectrum display
│   ├── Saved Profile .......... Load saved signal profiles
│   └── Back to Main Menu
│
├── SIGINT ────────────────────────────────────────────────
│   ├── EAPOL Capture .......... WPA handshake/PMKID capture
│   ├── Karma Attack ........... Auto-respond to probe requests
│   ├── Wardriving ............. GPS-tagged AP scanning
│   ├── Saved Captures ......... Browse captured handshakes
│   └── Back to Main Menu
│
├── Tools ─────────────────────────────────────────────────
│   ├── Serial Monitor ......... UART passthrough terminal
│   ├── Update Firmware ........ Flash .bin from SD card
│   ├── Touch Calibrate ........ Touchscreen recalibration
│   ├── GPS .................... Live satellite view & NMEA data
│   ├── Radio Test ............. SPI radio hardware verification
│   └── Back to Main Menu
│
├── Settings ──────────────────────────────────────────────
│   ├── Brightness ............. Backlight PWM control
│   ├── Screen Timeout ......... 30s / 1m / 2m / 5m / 10m / Never
│   ├── Swap Colors ............ BGR / RGB panel toggle
│   ├── Rotation ............... Portrait orientation fix (0° / 180° / 90CW / 90CCW)
│   ├── Device Info ............ Hardware stats (+ Easter Egg)
│   └── Back to Main Menu
│
└── About ─────────────────────────────────────────────────
    └── Full-screen about page with armed module list
```

---

## Attack Modules - Detailed

### WiFi Attacks

All WiFi attacks use the ESP32's built-in WiFi radio. No external module needed.

#### Packet Monitor

Puts the WiFi radio into promiscuous mode and displays real-time 802.11 frame statistics. Shows management, data, and control frame counts with a live packet-per-second graph.

```
┌─────────────────────────────┐
│  ATTACK FLOW                │
│                             │
│  WiFi → Promiscuous Mode    │
│       → Frame Callback      │
│       → Classify Frame Type │
│       → Update Counters     │
│       → Draw Live Graph     │
└─────────────────────────────┘
```

#### Beacon Spammer

Floods the airspace with fake WiFi network names. Configurable SSID list or random generation. Uses raw 802.11 frame injection at max TX power.

```
┌─────────────────────────────────────────┐
│  ATTACK FLOW                            │
│                                         │
│  Init WiFi (APSTA mode)                 │
│  Set max TX power (82 = +20.5dBm)      │
│  Disable power save (WIFI_PS_NONE)      │
│  Loop:                                  │
│    Build beacon frame with target SSID  │
│    esp_wifi_80211_tx() on AP interface  │
│    Cycle through SSID list              │
│    Display count on screen              │
└─────────────────────────────────────────┘
```

**Target Scenario:** Confuse client auto-connect, flood nearby WiFi lists, distraction during engagement.

#### WiFi Deauther

Scans for access points, lets you select a target by tapping it, then sends deauthentication frames to disconnect all clients from that AP.

```
┌──────────────────────────────────────────────┐
│  ATTACK FLOW                                 │
│                                              │
│  1. Scan networks (esp_wifi_scan_start)      │
│  2. Display AP list with RSSI bars           │
│  3. User taps target AP                      │
│  4. Switch to target's channel               │
│  5. Build deauth frame (reason code 7)       │
│  6. esp_wifi_80211_tx() via AP interface     │
│  7. Broadcast deauth to FF:FF:FF:FF:FF:FF   │
│  8. Display deauth packet counter            │
│                                              │
│  REQUIRES: WIFI_MODE_APSTA                   │
│  Raw frame TX needs an active AP interface.  │
│  STA does scanning, AP does injection.       │
└──────────────────────────────────────────────┘
```

**WiFi Scanner → Deauth Handoff:** The WiFi Scanner module has a "tap-to-attack" feature. Tap any scanned AP and choose Deauth — the target BSSID, SSID, and channel are passed directly to the Deauther, skipping the scan phase.

#### Probe Sniffer → Evil Twin Chain

Captures probe request frames from nearby devices, revealing the SSIDs they're looking for. Select any probed SSID to instantly spawn a GARMR Evil Twin captive portal impersonating that network.

```
┌────────────────────────────────────────────────┐
│  ATTACK CHAIN                                  │
│                                                │
│  Probe Sniffer                                 │
│    → Capture probe requests (passive)          │
│    → Display SSID + client MAC + RSSI          │
│    → User selects probed SSID                  │
│    ↓                                           │
│  GARMR Captive Portal (auto-spawns)            │
│    → Create fake AP with selected SSID         │
│    → DNS hijack all domains to portal          │
│    → Serve credential harvesting page          │
│    → Captured creds displayed on screen        │
└────────────────────────────────────────────────┘
```

#### WiFi Scanner

Full AP scanner with signal strength bars, channel info, encryption type, and vendor OUI lookup. Tap any result to attack:

```
Tap an AP:
  ├── DEAUTH → Launch deauther pre-targeted at this AP
  └── CLONE  → Launch GARMR captive portal with this SSID
```

#### Captive Portal (GARMR Evil Twin)

Standalone captive portal that creates a fake AP and captures credentials. Serves customizable portal pages from `portal_pages.h`. All DNS queries resolve to the ESP32, forcing a captive portal pop-up on connected clients.

#### Station Scanner

Scans for connected clients (stations) on nearby networks. Shows client MAC, associated AP, and RSSI. Supports deauth handoff to disconnect selected clients.

#### Auth Flood

Overwhelms a target AP's client table by flooding it with 802.11 authentication frames from random MAC addresses. Scan for nearby access points, tap to select a target, then flood.

```
┌──────────────────────────────────────────────┐
│  ATTACK FLOW                                 │
│                                              │
│  1. Scan networks (esp_wifi_scan_start)      │
│  2. Display AP list with RSSI bars           │
│  3. User taps target AP                      │
│  4. Switch to target's channel               │
│  5. Validate TX with test auth frame         │
│  6. Flood auth frames from random MACs       │
│  7. 85-bar equalizer shows attack rhythm     │
│                                              │
│  REQUIRES: WIFI_MODE_APSTA                   │
│  Raw frame TX needs an active AP interface.  │
└──────────────────────────────────────────────┘
```

**Target Scenario:** Force AP to exhaust its association table, causing legitimate clients to be unable to connect. Effective against consumer routers with small client table limits.

---

### Bluetooth Attacks

All BLE attacks use the ESP32's built-in Bluetooth radio. Proper radio teardown between WiFi and BLE modes is handled automatically.

#### BLE Jammer

Floods BLE advertisement channels (37, 38, 39) with garbage data to disrupt nearby BLE connections.

#### BLE Spoofer

Clones the identity of a nearby BLE device. Scan first, select a target, then the ESP32 broadcasts as that device.

#### BLE Beacon

Broadcasts custom BLE beacon advertisements. Can impersonate AirTags, Tiles, or custom iBeacon/Eddystone payloads.

#### BLE Sniffer

Passive BLE traffic analyzer. Displays advertisement data, RSSI, device names, and manufacturer data in real time.

#### BLE Scanner

Discovery tool that lists all nearby BLE devices with their names, MAC addresses, RSSI, and advertised services.

#### WhisperPair — CVE-2025-36911 Fast Pair Vulnerability Scanner

Scans for Google Fast Pair devices and probes for the WhisperPair vulnerability (CVE-2025-36911). Discovers Fast Pair service advertisements, then connects via GATT to check if the Key-Based Pairing characteristic is accessible outside of pairing mode — a condition that allows unauthorized pairing.

```
┌──────────────────────────────────────────────┐
│  ATTACK FLOW                                 │
│                                              │
│  1. BLE scan for Fast Pair service UUID      │
│  2. Filter devices with Fast Pair adverts    │
│  3. Connect to target via GATT               │
│  4. Probe Key-Based Pairing characteristic   │
│  5. Report: VULNERABLE / PATCHED / UNKNOWN   │
│                                              │
│  Passive discovery, active GATT probe.       │
└──────────────────────────────────────────────┘
```

#### AirTag Detect — Apple FindMy Tracker Detection

Passive BLE scanner that detects AirTags, FindMy accessories, and compatible third-party trackers. Filters for Apple manufacturer data (0x4C) with FindMy type bytes (0x12/0x19). Displays MAC address, RSSI proximity bars, battery level estimate, distance calculation, and status byte for each detected tracker. Auto-rescans every 5 seconds with alert flash on new tracker detection. Tracks up to 20 unique devices simultaneously.

```
┌──────────────────────────────────────────────┐
│  DETECTION METHOD                            │
│                                              │
│  1. BLE scan for Apple manufacturer data     │
│  2. Filter: company ID 0x004C (Apple)        │
│  3. Match FindMy type: 0x12 or 0x19          │
│  4. Extract status byte and battery level    │
│  5. Calculate distance from RSSI             │
│  6. Track unique MACs (up to 20)             │
│  7. Alert flash on new tracker detection     │
│  8. Auto-rescan every 5 seconds              │
└──────────────────────────────────────────────┘
```

**BLE Radio Notes:**
- Always use `BLEDevice::deinit(false)` — `deinit(true)` has a bug that breaks reinit
- 150ms delay between `BLEDevice::init()` and `esp_ble_tx_power_set()` to prevent crash
- `esp_wifi_stop()` before BLE init is sufficient — do NOT call `esp_wifi_deinit()`
- All BLE TX at `ESP_PWR_LVL_P9` (maximum)

---

### 2.4GHz NRF24 Attacks

Requires the external NRF24L01+PA+LNA module. All attacks run at `RF24_PA_MAX` power level.

#### Scanner

Scans all 126 NRF24 channels (2400-2525 MHz) and displays active channel activity. Useful for finding active 2.4GHz devices before attacking.

#### Spectrum Analyzer

Real-time visual spectrum display of the 2.4GHz band using the NRF24 as a wideband receiver. Shows signal strength across all channels with a waterfall display.

#### WLAN Jammer

Broadband 2.4GHz disruption. The NRF24+PA+LNA rapidly cycles through channels transmitting noise, disrupting WiFi, Bluetooth, Zigbee, and other 2.4GHz protocols simultaneously.

```
┌──────────────────────────────────────────┐
│  ATTACK FLOW                             │
│                                          │
│  Init NRF24 at RF24_PA_MAX               │
│  Loop across channels 0-125:             │
│    Set channel                           │
│    Transmit random payload               │
│    Hop to next channel (fast sweep)      │
│  Result: Broadband 2.4GHz disruption     │
│                                          │
│  Affects: WiFi, BLE, Zigbee, Z-Wave,    │
│           wireless keyboards/mice,       │
│           baby monitors, drones          │
└──────────────────────────────────────────┘
```

#### Proto Kill

Multi-protocol attack suite that combines channel hopping with protocol-specific disruption patterns. Targets specific 2.4GHz protocols with optimized jamming patterns.

---

### SubGHz CC1101 Attacks

Requires the external CC1101 HW-863 module. All attacks use `setPA(12)` (maximum TX power).

#### Replay Attack

Record a SubGHz signal, then replay it on demand. Works with garage doors, car key fobs (fixed code), gate openers, and other devices operating in the 300-928 MHz range.

```
┌──────────────────────────────────────────────┐
│  ATTACK FLOW                                 │
│                                              │
│  1. Set target frequency (e.g., 433.92 MHz)  │
│  2. Enter RX mode — listen for signals       │
│  3. Signal captured → stored in memory       │
│  4. User triggers replay                     │
│  5. Switch to TX mode                        │
│  6. setPA(12) for max power                  │
│  7. Transmit captured signal                 │
│  8. Optional: save to SD card profile        │
│                                              │
│  Supported modulations: ASK/OOK, 2-FSK      │
│  Common targets: 315MHz, 433.92MHz, 868MHz   │
└──────────────────────────────────────────────┘
```

#### Brute Force

Automated code generation for fixed-code systems. Iterates through possible code combinations and transmits each one. Effective against older garage doors and gate systems with limited code space.

#### SubGHz Jammer

Wideband jamming on configurable SubGHz frequencies. Disrupts garage door openers, car key fobs, alarm systems, and other SubGHz devices within range.

#### Spectrum Analyzer

Real-time SubGHz spectrum display. Sweep across the 300-928 MHz range to identify active frequencies before attacking.

#### Saved Profile

Load and replay previously saved SubGHz signal profiles from the SD card.

---

### SIGINT Operations

Advanced signal intelligence modules for passive and active wireless reconnaissance.

#### EAPOL Capture

Captures WPA/WPA2 4-way handshakes and PMKID from nearby networks. Forces a deauthentication to trigger reauthentication, then captures the EAPOL exchange.

```
┌──────────────────────────────────────────────────┐
│  ATTACK FLOW                                     │
│                                                  │
│  1. Scan for target networks                     │
│  2. User selects target AP                       │
│  3. Set WiFi to target channel                   │
│  4. Enable promiscuous mode                      │
│  5. Send deauth to force reauthentication        │
│  6. Capture EAPOL frames:                        │
│     - Message 1: AP → Client (ANonce + PMKID)    │
│     - Message 2: Client → AP (SNonce)            │
│     - Message 3: AP → Client (GTK)               │
│     - Message 4: Client → AP (Confirmation)      │
│  7. Extract PMKID from Message 1 RSN IE          │
│  8. Save to SD card in hashcat format            │
│  9. Display capture status on screen             │
│                                                  │
│  Output: .hc22000 or .hccapx for offline crack   │
└──────────────────────────────────────────────────┘
```

#### Karma Attack

Automatically responds to all probe requests with matching beacon frames, tricking devices into connecting to the ESP32. Combined with the captive portal for credential harvesting.

```
┌──────────────────────────────────────────────┐
│  ATTACK FLOW                                 │
│                                              │
│  1. Enable promiscuous mode                  │
│  2. Listen for probe request frames          │
│  3. For each probed SSID:                    │
│     → Create matching beacon response        │
│     → Broadcast as that network              │
│  4. Client auto-connects (trusts SSID)       │
│  5. DNS hijack to captive portal             │
│  6. Harvest credentials                      │
│                                              │
│  Captures: "Home_WiFi", "Starbucks",         │
│  "HOTEL_GUEST" — whatever devices remember   │
└──────────────────────────────────────────────┘
```

#### Wardriving

GPS-tagged WiFi scanning. Scans for access points while recording GPS coordinates, creating a log of every network and its physical location. Requires GPS module.

```
┌────────────────────────────────────┐
│  DATA COLLECTED PER AP             │
│                                    │
│  SSID, BSSID, Channel, RSSI,      │
│  Encryption type, Latitude,        │
│  Longitude, Altitude, Timestamp    │
│                                    │
│  Saved to SD card as CSV/JSON      │
└────────────────────────────────────┘
```

#### Saved Captures

Browse and manage previously captured EAPOL handshakes and PMKID hashes stored on the SD card.

---

### Tools

#### Serial Monitor

UART passthrough terminal for hardware hacking. Connect to a target device's debug port and read/send serial data directly from the CYD screen.

- **P1 Connector:** Full duplex via UART0 (GPIO 3 RX / GPIO 1 TX) — shared with USB serial
- **Speaker Pin:** RX only via GPIO 26 — dedicated, no USB conflict
- **Default baud:** 115200 (configurable)

#### Update Firmware

Flash a new firmware `.bin` file directly from the SD card without a computer. Browse SD card, select a `.bin`, confirm, and the ESP32 applies the OTA update and reboots.

#### Touch Calibrate

Interactive 4-corner touchscreen calibration tool. Tap crosshairs at each corner of the screen to compute the raw touch-to-screen coordinate mapping for your specific board. Takes 5-sample averages at each point for accuracy.

**Auto-calibration:** On first boot after flashing (or after an EEPROM reset), the firmware automatically runs touch calibration before showing the main menu. This ensures every board gets a working touch mapping immediately — no need to navigate menus with broken touch.

**EEPROM persistence:** Calibration values are saved to EEPROM and survive power cycles. Recalibrate anytime from Tools > Touch Calibrate. Press BOOT button to cancel calibration.

#### GPS

Live GPS satellite view with NMEA data parsing via TinyGPSPlus. Displays satellite count, fix status, latitude, longitude, altitude, speed, and HDOP. Auto-scans GPIO 3 (P1 connector) at 9600 baud.

#### Radio Test

Interactive SPI hardware verification tool for NRF24L01+ and CC1101 radios. Tests SPI communication by reading chip identification registers (NRF24 CONFIG register 0x08, CC1101 VERSION register 0x14) and provides smart failure diagnostics — distinguishes between wiring issues, dead chips, and clone chip detection. Includes battery voltage readout and 4-page wiring block diagrams with KiCad-style layout showing colored trace lines, solder dots, and chip boxes for NRF24, GPS, and CC1101 connections.

---

### Settings

#### Brightness

Backlight PWM control with DARKER/BRIGHTER touch buttons. Range: 10-255. Saved to EEPROM.

#### Screen Timeout

Auto-dim after inactivity. Options: 30 sec, 1 min, 2 min, 5 min, 10 min, Never. Touch or BOOT button to wake. Saved to EEPROM.

#### Swap Colors

Toggle between BGR (default) and RGB color order for the ILI9341 display. Some CYD board batches have panels wired with swapped color channels, causing reds to appear blue and vice versa. This setting writes the MADCTL register directly and is saved to EEPROM.

#### Rotation

Different CYD manufacturers mount the LCD panel in different orientations. If your display appears upside-down or sideways after flashing, use this setting to correct it. The UI is **portrait only** (taller than wide) — this setting rotates the portrait layout to compensate for how your board's panel is physically mounted.

| Option | Use When |
|--------|----------|
| Standard (0°) | Display is correct — most common |
| Flipped 180° | Display is upside-down |
| 90° CW | Panel is mounted sideways (clockwise) |
| 90° CCW | Panel is mounted sideways (counter-clockwise) |

Touch input automatically recalibrates after every rotation change. The selected rotation is saved to EEPROM and persists across power cycles.

**Note:** 90° CW and 90° CCW exist only to compensate for sideways-mounted LCD panels. They are **not** a landscape mode.

#### Device Info

Hardware information page showing: device name, firmware version, free heap, CPU frequency, flash size, and board type.

**Easter Egg:** Tap "By: HaleHound" five times rapidly to reveal the PR #76 story.

---

## Touch Navigation

The CYD has no physical buttons except BOOT (GPIO 0). All navigation is touchscreen-based.

### Main Menu

The main menu displays 8 categories in a 2x4 grid with skull icons. **Tap any icon** to enter that submenu.

### Submenus

Submenus display as vertical lists. **Tap any item** to launch that module.

### Inside Modules

| Action | Method |
|--------|--------|
| **Exit module** | Tap BACK icon (top-left, below status bar) |
| **Exit module** | Press BOOT button (GPIO 0) |
| **Scroll** | Tap UP zone (top-left) or DOWN zone (bottom-left) |
| **Select** | Tap center of screen |
| **Wake from sleep** | Tap anywhere or press BOOT |

### Touch Zones (2.8" Portrait)

```
┌────────────┬────────────────┬────────────┐
│   UP       │                │   BACK     │  y: 0-60
│  (0-80)    │                │ (160-240)  │
├────────────┤                ├────────────┤
│            │                │            │
│            │    SELECT      │            │
│            │   (80-160,     │            │
│            │   130-190)     │            │
│            │                │            │
├────────────┤                ├────────────┤
│   DOWN     │                │            │  y: 260-320
│  (0-80)    │                │            │
└────────────┴────────────────┴────────────┘
```

---

## TX Power Configuration

Every attack radio is configured for maximum transmission power.

| Radio | Setting | Power Level | Verified |
|-------|---------|-------------|----------|
| WiFi Beacon Spammer | `esp_wifi_set_max_tx_power(82)` + `WIFI_PS_NONE` | +20.5 dBm | Yes |
| WiFi Deauther | `esp_wifi_set_max_tx_power(82)` + `WIFI_PS_NONE` | +20.5 dBm | Yes |
| NRF24 (all modes) | `RF24_PA_MAX` | +20 dBm (with PA+LNA) | Yes |
| CC1101 Replay | `setPA(12)` | +12 dBm | Yes |
| CC1101 Jammer | `setPA(12)` | +12 dBm | Yes |
| CC1101 Brute Force | `setPA(12)` | +12 dBm | Yes |
| BLE (all modes) | `ESP_PWR_LVL_P9` | +9 dBm | Yes |

Passive-only modules (Packet Monitor, Probe Sniffer, Station Scanner, BLE Scanner) do not set TX power as they only receive.

---

## SD Card Structure

Insert a MicroSD card (FAT32 formatted) for data capture and OTA updates.

```
/sd/
├── eapol/             ← EAPOL/PMKID captures
│   ├── target_handshake.hc22000
│   └── ...
├── wardriving/        ← GPS-tagged AP logs
│   └── wardrive_20260215.csv
└── firmware/          ← OTA update binaries
    └── halehound-cyd.bin
```

---

## Pre-Compiled Binaries

Pre-compiled firmware binaries are available in the `flash_package/` folder for users who don't want to build from source.

### Single Binary — All Boards

One firmware binary works on every CYD board regardless of LCD panel orientation. No more guessing which rotation file to download.

| File | Flash Address | Description |
|------|---------------|-------------|
| `HaleHound-CYD-FULL.bin` | `0x0` | Complete image — single file, easiest method |
| `HaleHound-CYD.bin` | `0x10000` | Firmware only — requires 3 shared boot files |

If your display is upside-down or sideways after flashing, go to **Settings > Rotation** and select the correct orientation. Touch recalibrates automatically. No reflashing needed.

### Flash Methods

Two flash methods are available. If one doesn't work, try the other.

| Method | Files | Notes |
|--------|-------|-------|
| **Single File** | `HaleHound-CYD-FULL.bin` at `0x0` | One file, works for most boards |
| **Four File** | `bootloader.bin` + `partitions.bin` + `boot_app0.bin` + `HaleHound-CYD.bin` | Works on ALL boards including DIO/QIO incompatible ones |

### Flash with ESP Web Flasher (No Install)

1. Open [esp.huhn.me](https://esp.huhn.me) in **Chrome**, **Edge**, or **Opera** (Firefox/Safari not supported)
2. Click **Connect** and select your CYD's serial port
3. **Single file method:** Set address `0x0`, select `HaleHound-CYD-FULL.bin`
4. **Four file method:** Add all four entries:
   - `0x1000` → `bootloader.bin`
   - `0x8000` → `partitions.bin`
   - `0xe000` → `boot_app0.bin`
   - `0x10000` → `HaleHound-CYD.bin`
5. Click **Program** and wait for completion
6. Power cycle the CYD (unplug and replug USB)

### Flash with esptool (Command Line)

```bash
# Single file method
esptool.py --chip esp32 --baud 115200 write_flash 0x0 HaleHound-CYD-FULL.bin

# Four file method
esptool.py --chip esp32 --baud 115200 write_flash \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0xe000 boot_app0.bin \
  0x10000 HaleHound-CYD.bin
```

### First Boot

On first boot (or after flashing), the firmware automatically runs touch calibration — tap the 4 corner crosshairs when prompted. If your display orientation is wrong, navigate to **Settings > Rotation** to fix it. The UI is portrait only.

### CH340 USB Driver

CYD boards use the CH340 USB-to-serial chip. Install the driver if your computer doesn't recognize the board:
- **Windows:** [CH341SER.EXE](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
- **macOS:** [CH341SER_MAC.ZIP](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html)
- **Linux:** Built into kernel 5.x+ (no install needed)

### Troubleshooting Pre-Compiled Bins

| Problem | Solution |
|---------|----------|
| Black screen after single-file flash | Use the four-file method instead |
| Display sideways or upside-down | Go to Settings > Rotation and select the correct orientation |
| Touch backwards or offset | Change rotation in Settings, or use Tools > Touch Calibrate |
| "Failed to connect" error | Hold BOOT button while clicking Program |
| Browser doesn't show serial port | Use Chrome/Edge/Opera and install CH340 driver |
| Flashing completes but won't boot | Erase flash first: `esptool.py --chip esp32 erase_flash` then reflash |

See `flash_package/FLASH_INSTRUCTIONS.txt` for complete step-by-step instructions.

---

## Build from Source

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or IDE)
- Python 3.10-3.13 (3.14 requires platform.py patch)
- USB cable connected to CYD

### Build

```bash
# From project root
pio run -e esp32-cyd
```

### Flash

```bash
# Build and upload
pio run -e esp32-cyd --target upload

# If serial port isn't auto-detected:
pio run -e esp32-cyd --target upload --upload-port /dev/cu.usbserial-0001
```

### Serial Monitor

```bash
pio device monitor -b 115200
```

### One-Liner (Build + Flash + Monitor)

```bash
pio run -e esp32-cyd --target upload && pio device monitor -b 115200
```

### Partition Scheme

Uses `huge_app.csv` partition scheme for maximum application space. The firmware is large due to the number of attack modules, icons, and embedded resources.

---

## Pin Reference Table

### Pins In Use

| GPIO | Function | Bus | Connector | Notes |
|------|----------|-----|-----------|-------|
| 0 | BOOT Button | - | PCB | Active LOW, strapping pin |
| 1 | USB Serial TX | UART0 | P1 | Shared with GPS TX (unused) |
| 2 | TFT DC | HSPI | PCB | Data/Command select |
| 3 | GPS RX / USB Serial RX | UART2 | P1 | Shared — Serial.end() workaround |
| 4 | NRF24 CSN | VSPI | Wire | Was RGB Red LED |
| 5 | SD Card CS | VSPI | PCB | Built-in MicroSD slot |
| 12 | TFT MISO | HSPI | PCB | Display data in |
| 13 | TFT MOSI | HSPI | PCB | Display data out |
| 14 | TFT SCLK | HSPI | PCB | Display clock |
| 15 | TFT CS | HSPI | PCB | Display chip select |
| 16 | NRF24 CE | - | Wire | Was RGB Green LED |
| 17 | NRF24 IRQ | - | Wire | Was RGB Blue LED (optional) |
| 18 | VSPI SCK | VSPI | Wire | Shared: SD + CC1101 + NRF24 |
| 19 | VSPI MISO | VSPI | Wire | Shared: SD + CC1101 + NRF24 |
| 21 | TFT Backlight | PWM | PCB | PWM channel 0 |
| 22 | CC1101 GDO0 (TX) | - | P3 | Data TO radio |
| 23 | VSPI MOSI | VSPI | Wire | Shared: SD + CC1101 + NRF24 |
| 25 | Touch CLK | Bit-bang | PCB | XPT2046 touch SPI |
| 26 | Speaker / UART Mon | - | P4 | 8002A amp output (RX-only serial) |
| 27 | CC1101 CS | VSPI | CN1 | CC1101 chip select |
| 32 | Touch MOSI | Bit-bang | PCB | XPT2046 touch SPI |
| 33 | Touch CS | Bit-bang | PCB | XPT2046 touch chip select |
| 34 | LDR / Battery ADC | ADC | PCB | Input only, available |
| 35 | CC1101 GDO2 (RX) | - | P3 | Data FROM radio (input only) |
| 36 | Touch IRQ | - | PCB | XPT2046 touch interrupt |
| 39 | Touch MISO | Bit-bang | PCB | XPT2046 touch SPI (input only) |

### Pins NOT Available

| GPIO | Reason |
|------|--------|
| 6-11 | Connected to SPI flash — **never use** |
| 34, 36, 39 | Input only — no internal pull-up/down |

### Feature Flags

Set in `cyd_config.h`:

| Flag | Default | Meaning |
|------|---------|---------|
| `CYD_HAS_CC1101` | 1 | CC1101 SubGHz radio connected |
| `CYD_HAS_NRF24` | 1 | NRF24L01+PA+LNA connected |
| `CYD_HAS_GPS` | 1 | GPS module connected |
| `CYD_HAS_SDCARD` | 1 | SD card enabled |
| `CYD_HAS_RGB_LED` | 0 | Disabled (pins used for NRF24) |
| `CYD_HAS_SPEAKER` | 0 | Disabled (GPIO 26 used for serial mon) |
| `CYD_HAS_PCF8574` | 0 | No I2C button expander (CYD uses touch) |
| `CYD_HAS_SERIAL_MON` | 1 | UART serial monitor enabled |

---

## SPI Bus Sharing

Three devices share the VSPI bus (GPIO 18/19/23). The `spi_manager` module handles mutual exclusion:

```
┌────────────────────────────────────────────────────┐
│  SPI BUS ARBITRATION                               │
│                                                    │
│  Before using any VSPI device:                     │
│    1. Pull ALL CS pins HIGH (deselect all)         │
│       GPIO 5  (SD)  → HIGH                        │
│       GPIO 27 (CC1101) → HIGH                      │
│       GPIO 4  (NRF24) → HIGH                       │
│    2. Pull target CS pin LOW (select one)          │
│    3. Perform SPI transaction                      │
│    4. Pull target CS HIGH when done                │
│                                                    │
│  RULE: Only ONE device active at a time!           │
│  Violating this corrupts data on all three.        │
└────────────────────────────────────────────────────┘
```

---

## Known Issues

| Issue | Status | Workaround |
|-------|--------|------------|
| GPIO 26 cannot be used for GPS UART | Confirmed | Use GPIO 3 (P1). GPIO 26 feeds through 8002A amp IC |
| GPS shares GPIO 3 with USB serial | By design | Firmware calls Serial.end() during GPS, restores on exit |
| RGB LED unavailable | By design | Pins repurposed for NRF24 CE/CSN/IRQ |
| Speaker unavailable | By design | GPIO 26 repurposed for serial monitor RX |
| Python 3.14 breaks PlatformIO build | Platform bug | Patch `platform.py` or use Python 3.10-3.13 |
| NRF24+PA+LNA random resets | Power issue | Add 10uF capacitor between VCC/GND at module |
| CYD boards have different LCD panel orientations | Hardware variance | Use Settings > Rotation to select the correct portrait orientation |
| Touch mapping varies between CYD boards | Hardware variance | Auto-calibrates on first boot; recalibrate via Tools → Touch Calibrate |
| 3.5" CYD board untested | Pending | Pin defines ready in cyd_config.h, needs validation |

---

## Project Structure

```
HaleHound-CYD/
├── HaleHound-CYD.ino ......... Main firmware, menus, setup/loop
├── cyd_config.h ............... Master pin configuration
├── shared.h ................... Color palette, state variables
├── platformio.ini ............. Build configuration
├── User_Setup.h ............... TFT_eSPI display driver config
│
├── wifi_attacks.cpp/h ......... Packet Mon, Beacon, Deauth, Probe,
│                                WiFi Scan, Captive Portal, Station Scan
├── bluetooth_attacks.cpp/h .... BLE Jammer, Spoofer, Beacon, Sniffer, Scanner
├── nrf24_attacks.cpp/h ........ Scanner, Analyzer, WLAN Jammer, Proto Kill
├── nrf24_config.cpp/h ......... NRF24 initialization and SPI setup
├── subghz_attacks.cpp/h ....... Replay, Brute Force, Jammer, Analyzer
├── subconfig.cpp/h ............ CC1101 initialization and SPI setup
│
├── eapol_capture.cpp/h ........ EAPOL/PMKID handshake capture
├── karma_attack.cpp/h ......... Karma AP auto-respond attack
├── wardriving.cpp/h ........... GPS-tagged AP scan engine
├── wardriving_screen.cpp/h .... Wardriving display and UI
├── saved_captures.cpp/h ....... Browse saved handshakes on SD
│
├── radio_test.cpp/h ........... SPI radio diagnostics + wiring diagrams
├── gps_module.cpp/h ........... GPS setup, NMEA parsing, display
├── gps.h ...................... GPS type definitions
├── serial_monitor.cpp/h ....... UART passthrough terminal
├── firmware_update.cpp/h ...... OTA update from SD card
│
├── touch_buttons.cpp/h ........ Touch input, zones, calibration
├── CYD28_TouchscreenR.cpp/h ... Custom XPT2046 driver (polling mode)
├── spi_manager.cpp/h .......... VSPI bus arbitration
├── utils.cpp/h ................ Glitch text, centered text, helpers
│
├── icon.h ..................... Menu and module icon bitmaps
├── skull_bg.h ................. Skull watermark background bitmap
├── nuke_icon.h ................ NUKE cloud animation icon
├── nosifer_font.h ............. Custom Nosifer font (3 sizes)
├── portal_pages.h ............. Captive portal HTML pages
│
└── .pio/ ...................... PlatformIO build artifacts
    └── libdeps/esp32-cyd/ ..... Auto-downloaded libraries
```

---

## Libraries

Managed by PlatformIO (`lib_deps` in `platformio.ini`):

| Library | Version | Purpose |
|---------|---------|---------|
| TFT_eSPI | ^2.5.43 | ILI9341 display driver |
| ArduinoJson | ^7.0.0 | JSON parsing for configs/profiles |
| TinyGPSPlus | ^1.0.3 | NMEA GPS sentence parsing |
| EspSoftwareSerial | ^8.2.0 | Software serial for GPS |
| rc-switch | ^2.6.4 | SubGHz protocol encoding/decoding |
| arduinoFFT | ^2.0.2 | FFT for spectrum analyzers |
| RF24 | ^1.4.9 | NRF24L01 driver |
| XPT2046_Touchscreen | git | Touch controller driver |
| SmartRC-CC1101-Driver-Lib | ^2.5.7 | CC1101 radio driver |

---

## Credits

**HaleHound-CYD** by [JesseCHale](https://github.com/JesseCHale)

Based on the ESP32-DIV project. HaleHound Edition includes 8 new features, 17 bug fixes, hardware pin corrections (CC1101 TX/RX swap fix), full touchscreen support, SIGINT suite, and the CYD hardware port.

GitHub: [github.com/JesseCHale/HaleHound-CYD](https://github.com/JesseCHale/HaleHound-CYD)

---

*I built this.*
