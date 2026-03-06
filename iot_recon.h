#ifndef IOT_RECON_H
#define IOT_RECON_H

// =============================================================================
// HaleHound-CYD IoT Recon Module
// Automated LAN Attack Suite — Subnet Scanner + Credential Brute Force
// Cameras (RTSP/HTTP), MQTT Brokers, Telnet, Modbus
// Created: 2026-03-01
// =============================================================================
//
// ATTACK PIPELINE:
// +------------------------------------------------------------------------+
// | DISCOVER - TCP connect scan: 554,80,8080,23,1883,502,443,34567        |
// | IDENTIFY - Banner grab + fingerprint device type from response         |
// | ATTACK   - Auto-select: camera brute, MQTT dump, telnet creds, modbus |
// | REPORT   - Color-coded kill feed + SD card log                         |
// +------------------------------------------------------------------------+
//
// DUAL-CORE:
//   Core 0 = TCP scanner engine (port scan, brute force, MQTT, telnet)
//   Core 1 = Display + touch (kill feed, stats, Digital Plague animation)
//
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include "cyd_config.h"

// =============================================================================
// MAX LIMITS
// =============================================================================

#define IOT_MAX_DEVICES     48
#define IOT_MAX_PORTS       8
#define IOT_MAX_BANNER_LEN  48
#define IOT_MAX_CRED_USER   16
#define IOT_MAX_CRED_PASS   16
#define IOT_MAX_KILL_LINES  20

// =============================================================================
// ENUMS
// =============================================================================

enum IotDeviceType : uint8_t {
    IOT_UNKNOWN = 0,
    IOT_CAMERA,
    IOT_MQTT_BROKER,
    IOT_TELNET_DEVICE,
    IOT_MODBUS_PLC,
    IOT_HTTP_DEVICE
};

enum IotDeviceStatus : uint8_t {
    IOT_FOUND = 0,
    IOT_TESTING,
    IOT_CRACKED,
    IOT_OPEN,
    IOT_LOCKED
};

enum IotScanPhase : uint8_t {
    IOT_PHASE_CONNECT = 0,  // WiFi connection screen
    IOT_PHASE_DISCOVER,     // TCP port scanning
    IOT_PHASE_IDENTIFY,     // Banner grab + fingerprint
    IOT_PHASE_ATTACK,       // Credential testing
    IOT_PHASE_DONE          // Scan complete
};

// =============================================================================
// STRUCTURES
// =============================================================================

struct IotDevice {
    uint8_t  ip[4];                         // Device IP
    uint8_t  openPorts;                     // Bitmask: bit0=554, bit1=80, bit2=8080, bit3=23, bit4=1883, bit5=502, bit6=443, bit7=34567
    IotDeviceType  type;
    IotDeviceStatus status;
    char     banner[IOT_MAX_BANNER_LEN];    // Fingerprint / model string
    char     credUser[IOT_MAX_CRED_USER];   // Working username (if cracked)
    char     credPass[IOT_MAX_CRED_PASS];   // Working password (if cracked)
    uint8_t  mqttTopics;                    // Number of MQTT topics captured
};

// =============================================================================
// NAMESPACE
// =============================================================================

namespace IotRecon {

// Module lifecycle
void setup();
void loop();
bool isExitRequested();
void cleanup();

}  // namespace IotRecon

#endif // IOT_RECON_H
