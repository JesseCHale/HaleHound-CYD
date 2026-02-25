#ifndef WARDRIVING_H
#define WARDRIVING_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Wardriving Module
// WiGLE-compatible WiFi + BLE network logging with GPS
// Created: 2026-02-07
// Updated: 2026-02-24 — BLE logging, open network count, WiGLE v1.6
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// WARDRIVING CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define WARDRIVING_LOG_DIR          "/wardriving"
#define WARDRIVING_FILE_PREFIX      "halehound_"
#define WARDRIVING_MAX_NETWORKS     500     // Max unique WiFi networks per session
#define WARDRIVING_MAX_BLE_DEVICES  200     // Max unique BLE devices per session (200 x 6 = 1200 bytes)

// ═══════════════════════════════════════════════════════════════════════════
// WARDRIVING STATE
// ═══════════════════════════════════════════════════════════════════════════

struct WardrivingStats {
    bool active;                // Wardriving mode enabled
    bool sdCardReady;           // SD card mounted
    bool gpsReady;              // GPS has fix
    uint32_t networksLogged;    // Total WiFi networks logged this session
    uint32_t newNetworks;       // New (unique) WiFi networks found
    uint32_t duplicates;        // Duplicate WiFi networks skipped
    uint32_t openNetworks;      // WiFi networks with WIFI_AUTH_OPEN
    uint32_t bleDevicesLogged;  // Total BLE devices logged to CSV
    uint32_t newBleDevices;     // Unique BLE devices found
    uint32_t bleDuplicates;     // BLE dupes skipped
    String currentFile;         // Current log filename
};

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

// Initialize SD card for wardriving - call once
bool wardrivingInit();

// Check if SD card is available
bool wardrivingSDReady();

// ═══════════════════════════════════════════════════════════════════════════
// SESSION CONTROL
// ═══════════════════════════════════════════════════════════════════════════

// Start a new wardriving session (creates new log file)
bool wardrivingStart();

// Stop wardriving session (closes log file)
void wardrivingStop();

// Check if wardriving is active
bool wardrivingIsActive();

// Get current session stats
WardrivingStats wardrivingGetStats();

// ═══════════════════════════════════════════════════════════════════════════
// NETWORK LOGGING
// ═══════════════════════════════════════════════════════════════════════════

// Log a single WiFi network (called after scan)
// Returns true if logged (new network), false if duplicate
bool wardrivingLogNetwork(
    const uint8_t* bssid,       // 6-byte MAC address
    const char* ssid,           // Network name
    int rssi,                   // Signal strength
    int channel,                // WiFi channel
    int authMode                // Encryption type
);

// Log all networks from current scan
// Pass the apList and count from WiFi scanner
void wardrivingLogScan(void* apList, int count);

// Log a single BLE device to CSV
// Returns true if logged (new device), false if duplicate
bool wardrivingLogBleDevice(
    const uint8_t* mac,         // 6-byte BLE MAC address
    const char* name,           // Device name (or empty string)
    int rssi,                   // Signal strength
    const uint8_t* mfgData,    // Manufacturer data (or NULL)
    uint8_t mfgDataLen          // Length of manufacturer data
);

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

// Draw wardriving status overlay on scan screen
void wardrivingDrawStatus(int x, int y);

// Draw wardriving indicator in status bar
void wardrivingDrawIndicator(int x, int y);

#endif // WARDRIVING_H
