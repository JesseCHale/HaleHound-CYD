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
```

# HaleHound-CYD

**Multi-protocol offensive security toolkit for the ESP32 Cheap Yellow Display**

Version **v3.5.5** | By [JesseCHale](https://github.com/JesseCHale) | [HaleHound.com](https://halehound.com)

---

## What Is This

40+ attack modules across WiFi, Bluetooth, SubGHz, 2.4GHz, and NFC — all running on a $7 touchscreen dev board. External CC1101, NRF24L01+PA+LNA, PN532, and GPS modules plug into the CYD's breakout pins. Everything is touch-driven. All radios transmit at maximum power. No safety nets.

---

## Supported Boards

| Board | Build Target | Display | Touch | Status |
|-------|-------------|---------|-------|--------|
| ESP32-2432S028 (2.8") | `esp32-cyd` | ILI9341 240x320 | XPT2046 Resistive | Fully Tested |
| QDtech E32R35T (3.5") | `esp32-e32r35t` | ST7796 320x480 | XPT2046 Resistive | Fully Tested |
| QDtech E32R28T (2.8") | `esp32-e32r28t` | ILI9341 240x320 | XPT2046 Resistive | Fully Tested |
| NM-RF-Hat (2.8") | `esp32-cyd-hat` | ILI9341 240x320 | XPT2046 Resistive | Supported |

All UI scales automatically between 240x320 and 320x480. Pick your board, flash, done.

---

## Hardware

### Required

| Module | What It Does |
|--------|-------------|
| CYD board (any above) | Base platform — ESP32 + touchscreen + SD card |
| CC1101 (HW-863 or E07-433M20S) | SubGHz radio: 300-928 MHz capture, replay, jam, brute force |
| NRF24L01+PA+LNA | 2.4GHz radio: BLE jam, WLAN jam, MouseJack, spectrum analysis |
| PN532 V3 (Elechouse, SPI mode) | NFC/RFID: card scan, read, clone, brute force, emulate |
| GPS (GT-U7 or NEO-6M) | Wardriving, Flock You geolocation, live satellite view |

### Also Required: Independent 3.3V Power for Ebyte Modules

The Ebyte PA modules (E07-433M20S and E01-2G4M27SX) draw too much current for the CYD's onboard 3.3V regulator. You **must** power them from a separate 5V→3.3V buck converter. Tap 5V from USB and step it down to 3.3V independently — do not share the CYD's 3.3V rail or you'll get brownouts, random resets, and failed radio init.

| Part | Example |
|------|---------|
| 5V→3.3V buck converter | AMS1117-3.3 module, MP2307, or any 3.3V reg rated 500mA+ |
| Input | 5V from USB VBUS (before the CYD's regulator) |
| Output | 3.3V to E07 VCC and E01 VCC independently |
| Ground | Common ground with CYD — all GNDs must tie together |

The signal wires (SPI, CS, CE, GDO0, GDO2, TX_EN, RX_EN) still connect directly to ESP32 GPIOs — only VCC gets the independent supply.

### Optional

| Component | Why |
|-----------|-----|
| E07-433M20S PA module | 20dBm amplified SubGHz — serious range upgrade over stock CC1101 |
| 10uF capacitor | Across NRF24 VCC/GND — stops random resets on PA+LNA modules |
| MicroSD card (FAT32) | Loot storage, .sub files, wardriving logs, OTA updates |
| LiPo battery + boost | Portable operation |

---

## Menu Tree

```
HALEHOUND-CYD v3.5.5
│
├── WiFi
│   ├── Packet Monitor         Real-time 802.11 frame capture + graph
│   ├── Beacon Spammer         Flood fake SSIDs
│   ├── WiFi Deauther          Scan → tap → disconnect all clients
│   ├── Probe Sniffer          Capture probes → auto-spawn Evil Twin
│   ├── WiFi Scanner           Scan APs → tap to Deauth or Clone
│   ├── Captive Portal         GARMR Evil Twin credential harvester
│   ├── Station Scanner        Find connected clients + deauth handoff
│   └── Auth Flood             Flood AP with auth frames from random MACs
│
├── Bluetooth
│   ├── BLE Jammer             NRF24 flood on BLE ad channels 37/38/39
│   ├── BLE Spoofer            Multi-platform BLE pairing spam
│   ├── BLE Beacon             Custom iBeacon / Eddystone broadcast
│   ├── BLE Predator           GATT recon → clone device → honeypot trap
│   ├── WhisperPair            CVE-2025-36911 — Fast Pair exploit
│   ├── Airoha RACE            CVE-2025-20700 — link key extraction
│   └── Lunatic Fringe ──┐
│       ├── Tracker Scan  │    Multi-platform BLE tracker scanner
│       ├── AirTag Detect │    Apple FindMy tracker detection
│       ├── Phantom Flood │    Fake FindMy advertisement flood
│       └── AirTag Replay │    Sniff + replay real AirTag identity
│
├── 2.4GHz (NRF24)
│   ├── Scanner                Channel activity across 2400-2525 MHz
│   ├── Spectrum Analyzer      Visual RF spectrum + AP-locked zoom
│   ├── NRF Sniffer            Promiscuous packet capture (Goodspeed)
│   ├── MouseJack              Wireless keyboard keystroke injection
│   ├── WLAN Jammer            Broadband 2.4GHz disruption
│   └── Proto Kill             Multi-protocol 2.4GHz attack suite
│
├── SubGHz (CC1101)
│   ├── Replay Attack          Record + replay RF signals (300-928 MHz)
│   ├── Brute Force            Automated code gen (Princeton/CAME/Nice/PT2262)
│   ├── SubGHz Jammer          Wideband SubGHz disruption
│   ├── Spectrum Analyzer      SubGHz RF spectrum display
│   ├── Saved Profile          Load saved signals from SD
│   ├── Tesla Charge           Open any Tesla charge port (US/EU/BOTH)
│   └── .Sub Read              Flipper .sub file browser + transmitter
│
├── RFID (PN532)
│   ├── Card Scanner           Detect + identify NFC/RFID cards
│   ├── Card Reader            MIFARE sector data dump
│   ├── Card Clone             Clone UID to writable card
│   ├── Key Brute Force        MIFARE key A/B brute force
│   └── Card Emulate           Replay captured card UID
│
├── Jam Detect
│   ├── WiFi Guardian          Detect deauth floods
│   ├── SubGHz Sentinel        Detect SubGHz carrier jamming
│   ├── 2.4GHz Watchdog        Detect broadband 2.4GHz jamming
│   └── Full Spectrum          All bands simultaneously
│
├── SIGINT
│   ├── EAPOL Capture          WPA handshake + PMKID capture
│   ├── Karma Attack           Auto-respond to all probes → portal
│   ├── Wardriving             GPS-tagged AP scanning to SD
│   ├── Saved Captures         Browse captured handshakes
│   ├── IoT Recon              Automated LAN scanner + credential brute
│   ├── Loot                   Unified loot browser (5 categories)
│   └── Flock You              Flock Safety ALPR camera detector
│
├── Tools
│   ├── Serial Monitor         UART passthrough terminal
│   ├── Update Firmware        OTA flash from SD card
│   ├── Touch Calibrate        4-corner calibration tool
│   ├── GPS                    Live satellite view + NMEA data
│   └── Radio Test             Hardware verification + wiring diagrams
│
├── Settings
│   ├── Brightness             Backlight PWM (10-255)
│   ├── Screen Timeout         30s / 1m / 2m / 5m / 10m / Never
│   ├── Swap Colors            BGR ↔ RGB panel toggle
│   ├── Invert Display         Inverted color toggle
│   ├── Color Mode             Default / Colorblind / High Contrast
│   ├── Rotation               0° / 180° / 90° CW / 90° CCW
│   ├── Device Info            Heap, CPU, flash, board name
│   ├── Set PIN                4-digit boot lock
│   └── CC1101 Module          Standard HW-863 ↔ E07 PA module
│
└── About
    └── Firmware info + armed module list
```

---

## Feature Highlights

### WiFi

All WiFi attacks use the ESP32's built-in radio — no external hardware. APSTA mode: STA scans, AP injects raw frames. Max TX at +20.5 dBm.

- **Deauther** — Scan, tap a target, flood deauth frames. WiFi Scanner has tap-to-deauth built in.
- **Probe Sniffer → Evil Twin** — Capture what devices are looking for, then become that network. Auto-spawns GARMR captive portal with the probed SSID.
- **Auth Flood** — Random MAC auth frame flood. Exhausts AP client tables. 85-bar equalizer shows attack rhythm.
- **Captive Portal (GARMR)** — Fake AP + DNS hijack + credential harvest page. Works standalone or chained from Probe Sniffer / WiFi Scanner.

### Bluetooth

All BLE uses the ESP32's built-in Bluetooth. Proper WiFi↔BLE radio teardown handled automatically.

- **BLE Predator** — Three phases: SCAN (discover + threat classify), RECON (GATT enumerate all services/chars), HONEYPOT (clone as connectable server, capture credentials on WRITE). Loot saved to SD.
- **Airoha RACE** — CVE-2025-20700/20701/20702. Unauthenticated BLE GATT access to Airoha chipsets (Sony XM4/XM5/XM6, Marshall, JBL, Jabra, etc). Extracts Bluetooth link keys, BD_ADDR, firmware version, flash memory. No pairing required.
- **WhisperPair** — CVE-2025-36911. Probes Google Fast Pair devices for unauthorized pairing vulnerability.
- **Lunatic Fringe** — Hub for tracker detection and attacks. Scans for AirTags, Samsung SmartTags, Tile, Chipolo, Google FMDN. Phantom Flood spams fake FindMy trackers. AirTag Replay clones real AirTag identities.

### 2.4GHz (NRF24)

External NRF24L01+PA+LNA required. All modes at RF24_PA_MAX (+20 dBm with PA module).

- **NRF Sniffer** — Travis Goodspeed promiscuous mode. Captures raw packets from wireless keyboards, mice, drones. Tap a captured address → auto-populates MouseJack.
- **MouseJack** — Keystroke injection into Logitech Unifying, Dell, Microsoft wireless keyboards. HID++ packets, pre-built payloads (reverse shell, WiFi exfil, custom string).
- **WLAN Jammer / Proto Kill** — Broadband 2.4GHz disruption. Affects WiFi, BLE, Zigbee, wireless peripherals, drones.

### SubGHz (CC1101)

External CC1101 required. All TX at setPA(12) max power. Optional E07-433M20S PA module for 20dBm amplified output.

- **Replay Attack** — Record and replay SubGHz signals. RSSI gating, drain loop, repeat validation. Save profiles to SD.
- **Brute Force** — Automated code generation with de Bruijn sequences. Princeton, CAME, Nice FLO, PT2262.
- **Tesla Charge** — Opens the charge port on any Tesla. Static 43-byte OOK payload, zero authentication, zero rolling code. Works on every Tesla ever made. US (315 MHz), EU (433.92 MHz), or both.
- **.Sub Read** — Browse and transmit Flipper Zero .sub files from SD card. Supports RAW, Princeton, CAME, Nice FLO across full CC1101 frequency range. No .sub files required — shows empty state if folder is missing. Drop files in `/subghz/` when you have them.

### RFID (PN532)

External PN532 V3 in SPI mode. Scan, read, clone, brute force, and emulate MIFARE Classic cards.

### Jam Detect

Defensive modules. WiFi Guardian catches deauth floods, SubGHz Sentinel detects carrier jamming, 2.4GHz Watchdog spots broadband disruption, Full Spectrum monitors all bands at once.

### SIGINT

- **EAPOL Capture** — WPA 4-way handshake + PMKID extraction. Deauth to force reauth, capture, save in hashcat format.
- **Karma Attack** — Auto-respond to every probe request, chain into captive portal for credential harvest.
- **IoT Recon** — Connect to WiFi, scan the subnet, fingerprint services (HTTP, RTSP, Telnet, MQTT, Modbus, XMEye), brute force default credentials. Dual-core: networking on Core 0, UI on Core 1. Drop custom creds in `/creds.txt` on SD.
- **Flock You** — Passive detection of Flock Safety ALPR cameras and Raven/ShotSpotter sensors via BLE fingerprinting. 22 OUI prefixes, 8 Raven GATT service UUIDs, firmware version estimation. GPS-tagged saves to SD.
- **Loot** — Unified browser for all captured data: wardriving CSVs, EAPOL handshakes, WhisperPair/BLE Predator loot, IoT Recon reports, credentials.

### VALHALLA Protocol

Every offensive module is gated behind a liability disclaimer. Accept to unlock offensive tools, decline to enter **Blue Team mode** — defensive and passive modules only. Blue Team mode persists across reboots. The VALHALLA/BLUE TEAM banner on the home screen shows your current mode.

---

## Wiring

All external radios share the VSPI bus (GPIO 18/19/23) with the built-in SD card. Only one device talks at a time — the SPI manager handles bus arbitration automatically. The Radio Test module (Tools > Radio Test) also has 4-page visual wiring diagrams built into the firmware.

**Note:** CiferTech's original firmware had CC1101 TX/RX pins swapped. HaleHound corrects this — GDO0 is TX (to radio), GDO2 is RX (from radio).

### CC1101 SubGHz Radio

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

**E32R28T / E32R35T:** CC1101 CS moves to **GPIO 21** (GPIO 27 is backlight on 3.5"). All other pins identical.

### NRF24L01+PA+LNA

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

**E32R28T / E32R35T:** NRF24 CSN moves to **GPIO 26** (GPIO 4 used for CC1101 PA TX_EN). CE and IRQ stay the same.

**10uF capacitor** between VCC/GND at the NRF24 module if you get random resets.

### PN532 NFC/RFID

```
┌─────────────────┐              ┌──────────────────┐
│    PN532 V3      │              │    CYD ESP32      │
│   (Elechouse)    │              │                   │
├─────────────────┤              ├──────────────────┤
│ VCC ─────────────┼──────────────┤ 3.3V (CN1 hdr)    │
│ GND ─────────────┼──────────────┤ GND  (CN1 hdr)    │
│ SCK ─────────────┼──────────────┤ GPIO 18 (VSPI)    │
│ MOSI ────────────┼──────────────┤ GPIO 23 (VSPI)    │
│ MISO ────────────┼──────────────┤ GPIO 19 (VSPI)    │
│ SS ──────────────┼──────────────┤ GPIO 17 (was RGB B)│
└─────────────────┘              └──────────────────┘
```

**DIP Switches:** CH1=OFF, CH2=ON for SPI mode.

### GPS Module

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

**USB Conflict:** GPIO 3 is shared with USB serial RX. Firmware calls Serial.end() during GPS, restores on exit.

### E07-433M20S PA Module (Optional Amplified SubGHz)

```
┌─────────────────┐              ┌──────────────────┐
│ E07-433M20S      │              │  E32R28T/E32R35T  │
│ (CC1101 + PA)    │              │    ESP32          │
├─────────────────┤              ├──────────────────┤
│ VCC ─────────────┼── 3.3V BUCK ─┤ (NOT CYD 3.3V!)  │
│ GND ─────────────┼──────────────┤ GND (common)      │
│ SCK ─────────────┼──────────────┤ GPIO 18 (VSPI)    │
│ MOSI ────────────┼──────────────┤ GPIO 23 (VSPI)    │
│ MISO ────────────┼──────────────┤ GPIO 19 (VSPI)    │
│ CS ──────────────┼──────────────┤ GPIO 21            │
│ GDO0 (TX) ───────┼──────────────┤ GPIO 22            │
│ GDO2 (RX) ───────┼──────────────┤ GPIO 35            │
│ TX_EN ───────────┼──────────────┤ GPIO 4             │
│ RX_EN ───────────┼──────────────┤ GPIO 0             │
└─────────────────┘              └──────────────────┘
```

**Must be powered from an independent 5V→3.3V buck converter** — not the CYD's 3.3V rail. The PA module draws too much current. Same applies to the NRF24 E01-2G4M27SX. Enable PA mode in Settings > CC1101 Module. E32R28T/E32R35T only.

---

## SD Card

FAT32 formatted MicroSD. Nothing is required — every module handles missing folders gracefully.

```
/sd/
├── subghz/         .sub files for Sub Read (organize into subfolders)
├── eapol/          EAPOL/PMKID captures
├── wardriving/     GPS-tagged AP logs
├── wp_loot/        WhisperPair + BLE Predator loot
├── loot/           Flock You detection logs
├── creds.txt       Custom credentials for IoT Recon (optional)
├── iot_recon.txt   IoT Recon attack reports
└── firmware/       OTA update .bin files
```

---

## Flash — Pre-Compiled Binaries

Pre-built firmware is in the `flash_package/` folder. One binary per board.

### Easiest Method

Flash `HaleHound-<board>-FULL.bin` at address `0x0`.

### Web Flasher (No Install)

1. Open [esp.huhn.me](https://esp.huhn.me) in Chrome/Edge/Opera
2. Connect → select your CYD serial port
3. Set address `0x0`, select the FULL.bin for your board
4. Program → power cycle

### esptool

```bash
esptool.py --chip esp32 --baud 115200 write_flash 0x0 HaleHound-CYD-FULL.bin
```

### Four-File Method (Fallback)

If single-file gives a black screen:

| Address | File |
|---------|------|
| `0x1000` | `bootloader.bin` |
| `0x8000` | `partitions.bin` |
| `0xe000` | `boot_app0.bin` |
| `0x10000` | `HaleHound-<board>.bin` |

### First Boot

Touch calibration runs automatically on first boot. Tap the 4 corner crosshairs. If display orientation is wrong, fix it in Settings > Rotation — no reflash needed.

### CH340 Driver

CYD boards use CH340 USB serial. Install if your computer doesn't see the board:
- **Windows:** [CH341SER.EXE](https://www.wch-ic.com/downloads/CH341SER_EXE.html)
- **macOS:** [CH341SER_MAC.ZIP](https://www.wch-ic.com/downloads/CH341SER_MAC_ZIP.html)
- **Linux:** Built into kernel 5.x+

---

## Build from Source

```bash
# Install PlatformIO, plug in CYD, then:

pio run -e esp32-cyd --target upload          # 2.8" CYD
pio run -e esp32-e32r35t --target upload      # E32R35T 3.5"
pio run -e esp32-e32r28t --target upload      # E32R28T
pio run -e esp32-cyd-hat --target upload      # NM-RF-Hat

# Serial monitor
pio device monitor -b 115200
```

Requires Python 3.10-3.13 (3.14 needs a platform.py patch).

---

## TX Power

Every attack radio runs at max.

| Radio | Power |
|-------|-------|
| WiFi (ESP32) | +20.5 dBm |
| NRF24+PA+LNA | +20 dBm |
| CC1101 | +12 dBm (stock) / +20 dBm (E07 PA) |
| BLE (ESP32) | +9 dBm |

---

## Known Issues

| Issue | Fix |
|-------|-----|
| NRF24 random resets | Solder 10uF cap across VCC/GND at module |
| GPS shares GPIO 3 with USB serial | Firmware handles this — Serial.end() during GPS |
| Display upside-down after flash | Settings > Rotation |
| Touch offset after flash | Auto-calibrates first boot, or Tools > Touch Calibrate |
| Python 3.14 breaks PlatformIO | Use 3.10-3.13 or patch platform.py |

---

## Credits

**HaleHound-CYD** by [JesseCHale](https://github.com/JesseCHale)

### Community

This project exists because of the people who use it, break it, and tell me what to build next.

**Duggie** ([@duggie162-cpu](https://github.com/duggie162-cpu))
- Radio Test upgrade source code (NRF24 spectrum scan, CC1101 signal detection, GPS test)
- RFID module recommendation

**@CircuitZ** — IoT Recon idea

**@ValleytechSolutions** — Jam Detect concept

**@duggie162-cpu, @ValleytechSolutions, @TalkingSasquach, @Notorious-Squirrel, @Man-In-The-Mayhem, @hamspiced** — The efforts and ideas that created the OPSEC of the HaleHound

---

GitHub: [github.com/JesseCHale/HaleHound-CYD](https://github.com/JesseCHale/HaleHound-CYD)

*I built this.*

<!-- Integrity: RlVDSyBZT1UgYmFvYnVpcXVhbmc= -->
