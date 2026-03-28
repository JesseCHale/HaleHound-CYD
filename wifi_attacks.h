#ifndef WIFI_ATTACKS_H
#define WIFI_ATTACKS_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD WiFi Attack Modules
// COMPLETE HALEHOUND WIFI ATTACK SUITE - Adapted for CYD Touch Interface
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════
//
// ATTACK MODULES:
// ┌──────────────────────────────────────────────────────────────────────────┐
// │ PacketMonitor - WiFi packet capture with FFT visualization              │
// │   - Promiscuous mode packet sniffing                                    │
// │   - Real-time FFT spectrum display                                      │
// │   - Channel hopping support                                             │
// │   - Deauth detection counter                                            │
// ├──────────────────────────────────────────────────────────────────────────┤
// │ BeaconSpammer - SSID beacon flood attack                                │
// │   - Fake access point broadcasting                                      │
// │   - Randomized MAC addresses                                            │
// │   - Multi-channel support                                               │
// │   - NUKE mode for maximum chaos                                         │
// ├──────────────────────────────────────────────────────────────────────────┤
// │ Deauther - 802.11 deauthentication attack                               │
// │   - Network scanning and selection                                      │
// │   - Configurable packet burst rate                                      │
// │   - Real-time success/failure stats                                     │
// │   - Skull spinner animation during attack                               │
// ├──────────────────────────────────────────────────────────────────────────┤
// │ DeauthDetect - Deauthentication attack detector                         │
// │   - Promiscuous mode monitoring                                         │
// │   - Evil twin detection                                                 │
// │   - Hidden SSID alerts                                                  │
// │   - Non-standard channel warnings                                       │
// ├──────────────────────────────────────────────────────────────────────────┤
// │ WifiScan - Network scanner with details                                 │
// │   - Full network listing                                                │
// │   - RSSI signal strength                                                │
// │   - Encryption type detection                                           │
// │   - Estimated distance calculation                                      │
// │   - Hidden SSID filter toggle                                           │
// │   - Connected client discovery (passive promiscuous scan)               │
// ├──────────────────────────────────────────────────────────────────────────┤
// │ CaptivePortal - Credential harvesting portal                            │
// │   - Customizable SSID                                                   │
// │   - On-screen keyboard                                                  │
// │   - Credential storage in EEPROM                                        │
// │   - Multi-platform captive portal detection                             │
// └──────────────────────────────────────────────────────────────────────────┘
//
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <EEPROM.h>
#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// PACKET MONITOR
// ═══════════════════════════════════════════════════════════════════════════

namespace PacketMonitor {

// Initialize packet monitor - call before first use
void setup();

// Main loop function - call repeatedly
void loop();

// Set WiFi channel (1-14)
void setChannel(int channel);

// Get current channel
int getChannel();

// Get packet count since last reset
uint32_t getPacketCount();

// Get deauth count since last reset
uint32_t getDeauthCount();

// Reset counters
void resetCounters();

// Check if user requested exit
bool isExitRequested();

// Cleanup and release WiFi
void cleanup();

}  // namespace PacketMonitor

// ═══════════════════════════════════════════════════════════════════════════
// BEACON SPAMMER
// ═══════════════════════════════════════════════════════════════════════════

namespace BeaconSpammer {

// Spam modes
enum SpamMode {
    SPAM_MODE_NORMAL,     // Controlled spam with UI feedback
    SPAM_MODE_NUKE        // Maximum chaos mode
};

// Initialize beacon spammer
void setup();

// Main loop function
void loop();

// Start spamming
void start();

// Stop spamming
void stop();

// Toggle spam state
void toggle();

// Check if currently spamming
bool isSpamming();

// Set WiFi channel (1-14)
void setChannel(int channel);

// Get current channel
int getChannel();

// Run NUKE mode (exits on button press)
void nukeMode();

// Check if user requested exit
bool isExitRequested();

// Cleanup and release WiFi
void cleanup();

}  // namespace BeaconSpammer

// ═══════════════════════════════════════════════════════════════════════════
// DEAUTHER - 802.11 Deauthentication Attack
// ═══════════════════════════════════════════════════════════════════════════

namespace Deauther {

// Initialize deauther module
void setup();

// Main loop function
void loop();

// Scan for networks
bool scanNetworks();

// Get number of discovered networks
int getNetworkCount();

// Select target by index
void selectTarget(int index);

// Get selected target index (-1 if none)
int getSelectedTarget();

// Start deauth attack on selected target
void startAttack();

// Stop deauth attack
void stopAttack();

// Check if attack is running
bool isAttackRunning();

// Get packet count
uint32_t getPacketCount();

// Get success count
uint32_t getSuccessCount();

// Get success rate (0.0 - 100.0)
float getSuccessRate();

// Set packets per burst (1-100)
void setPacketsPerBurst(int count);

// Get packets per burst
int getPacketsPerBurst();

// Check if user requested exit
bool isExitRequested();

// Set target from WifiScan handoff (skips scan screen)
void setTarget(const char* bssid, const char* ssid, int channel);

// Cleanup and release WiFi
void cleanup();

}  // namespace Deauther

// ═══════════════════════════════════════════════════════════════════════════
// DEAUTH DETECT - Deauthentication Attack Detector
// ═══════════════════════════════════════════════════════════════════════════

namespace DeauthDetect {
// ═══════════════════════════════════════════════════════════════════════════
// PROBE REQUEST SNIFFER v2.0
// Captures devices broadcasting probe requests for networks
// HaleHound v2.7.0 - Added Evil Twin spawn, Filter View, Save Targets
// ═══════════════════════════════════════════════════════════════════════════

// Initialize probe sniffer module
void setup();

// Main loop function
void loop();

// Start sniffing probe requests
void startScanning();

// Stop sniffing
void stopScanning();

// Check if currently sniffing
bool isScanning();

// Get unique device count (MACs seen)
int getDeviceCount();

// Get unique SSID count (networks probed for)
int getSSIDCount();

// Get total probe request count
int getProbeCount();

// Check if user requested exit
bool isExitRequested();

// Evil Twin handoff - get SSID user selected for spawning
const char* getSelectedSSID();

// Check if user wants to spawn Evil Twin with selected SSID
bool isEvilTwinRequested();

// Clear Evil Twin request after handoff to CaptivePortal
void clearEvilTwinRequest();

// Cleanup and release WiFi
void cleanup();

}  // namespace DeauthDetect

// ═══════════════════════════════════════════════════════════════════════════
// WIFI SCAN v3.0 - Network Scanner with Tap-to-Attack
// Sort, Filter (incl. hidden SSID filter), Signal Bars,
// Connected Client Discovery, Attack Handoff
// ═══════════════════════════════════════════════════════════════════════════

namespace WifiScan {

// Initialize scanner module
void setup();

// Main loop function
void loop();

// Start WiFi scan
void startScan();

// Get number of networks found
int getNetworkCount();

// Get filtered count (after applying filter)
int getFilteredCount();

// Get SSID by index
String getSSID(int index);

// Get BSSID by index
String getBSSID(int index);

// Get RSSI by index
int getRSSI(int index);

// Get channel by index
int getChannel(int index);

// Get encryption type by index
int getEncryption(int index);

// Get encryption type as string
String getEncryptionString(int index);

// Calculate estimated distance (meters)
float getEstimatedDistance(int index);

// Calculate signal quality (0-100%)
float getSignalQuality(int index);

// Check if user requested exit
bool isExitRequested();

// Attack handoff - get selected target info
const char* getSelectedSSID();
const char* getSelectedBSSID();
int getSelectedChannel();

// Check attack type requested
bool isDeauthRequested();
bool isCloneRequested();

// Clear attack request after handoff
void clearAttackRequest();

// Cleanup
void cleanup();

}  // namespace WifiScan

// ═══════════════════════════════════════════════════════════════════════════
// CAPTIVE PORTAL - Credential Harvesting Portal
// ═══════════════════════════════════════════════════════════════════════════

namespace CaptivePortal {

// Credential structure (expanded for multi-stage capture)
struct Credential {
    char email[32];     // 31 chars + null
    char password[24];  // 23 chars + null
    char mfa[8];        // 7 chars + null (MFA code or room number)
};

// Portal template enum
enum PortalTemplate : uint8_t {
    TMPL_WIFI = 0,
    TMPL_GOOGLE,
    TMPL_MICROSOFT,
    TMPL_STARBUCKS,
    TMPL_HOTEL,
    TMPL_AIRPORT,
    TMPL_ATT,
    TMPL_MCDONALDS,
    TMPL_XFINITY,
    TMPL_FIRMWARE_UPDATE,
    TMPL_WIFI_RECONNECT,
    TMPL_COUNT
};

// Screen states
enum Screen {
    SCREEN_MAIN,
    SCREEN_KEYBOARD,
    SCREEN_CRED_LIST,
    SCREEN_PORTAL_ACTIVE
};

// Initialize captive portal module
void setup();

// Main loop function
void loop();

// Start the captive portal attack
void startPortal();

// Stop the captive portal attack
void stopPortal();

// Check if portal is active
bool isPortalActive();

// Set SSID for the fake AP
void setSSID(const char* ssid);

// Get current SSID
const char* getSSID();

// Get credential count
int getCredentialCount();

// Get credential by index
Credential getCredential(int index);

// Delete credential by index
void deleteCredential(int index);

// Clear all credentials
void clearAllCredentials();

// Save SSID to EEPROM
void saveSSID(const char* ssid);

// Load SSID from EEPROM
void loadSSID();

// Check if user requested exit
bool isExitRequested();

// Cleanup
void cleanup();

}  // namespace CaptivePortal

// ═══════════════════════════════════════════════════════════════════════════
// STATION SCANNER - Client/Station Discovery via Probe Requests
// ═══════════════════════════════════════════════════════════════════════════

namespace StationScan {

// Initialize station scanner module
void setup();

// Main loop function
void loop();

// Start scanning for stations
void startScanning();

// Stop scanning
void stopScanning();

// Check if currently scanning
bool isScanning();

// Get unique station count
int getStationCount();

// Get selected station count (for deauth handoff)
int getSelectedCount();

// Check if user requested exit
bool isExitRequested();

// Deauth handoff - check if user wants to deauth selected stations
bool isDeauthRequested();

// Get selected station MAC by index (for deauth handoff)
// Returns pointer to 6-byte MAC array
uint8_t* getSelectedMAC(int index);

// Clear deauth request after handoff
void clearDeauthRequest();

// Cleanup and release WiFi
void cleanup();

}  // namespace StationScan

// ═══════════════════════════════════════════════════════════════════════════
// AUTH FLOOD - 802.11 Authentication Frame Flood Attack
// Overwhelms AP client table with fake auth requests from random MACs
// ═══════════════════════════════════════════════════════════════════════════

namespace AuthFlood {

// Initialize auth flood module
void setup();

// Main loop function
void loop();

// Check if user requested exit
bool isExitRequested();

// Cleanup and release WiFi
void cleanup();

}  // namespace AuthFlood

// ═══════════════════════════════════════════════════════════════════════════
// SHARED WIFI UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

// Initialize WiFi in promiscuous mode
void wifiPromiscuousInit();

// Initialize WiFi in AP mode (for beacon spam)
void wifiAPInit();

// Cleanup WiFi and release resources
void wifiCleanup();

#endif // WIFI_ATTACKS_H
