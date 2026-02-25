// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Wardriving Module Implementation
// WiGLE-compatible WiFi + BLE network logging with GPS
// Created: 2026-02-07
// Updated: 2026-02-24 — BLE logging, open network count, WiGLE v1.6
// ═══════════════════════════════════════════════════════════════════════════

#include "wardriving.h"
#include "gps_module.h"
#include "spi_manager.h"
#include "shared.h"
#include "icon.h"
#include <SD.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <esp_wifi.h>

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// MODULE STATE
// ═══════════════════════════════════════════════════════════════════════════

static WardrivingStats stats;
static File logFile;
static bool sdInitialized = false;

// WiFi duplicate detection - store BSSIDs we've seen
static uint8_t seenBSSIDs[WARDRIVING_MAX_NETWORKS][6];
static int seenCount = 0;

// BLE duplicate detection - store BLE MACs we've seen
static uint8_t seenBLEMACs[WARDRIVING_MAX_BLE_DEVICES][6];
static int seenBLECount = 0;

// ═══════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static String macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static String authModeToString(int authMode) {
    switch (authMode) {
        case WIFI_AUTH_OPEN:            return "[OPEN]";
        case WIFI_AUTH_WEP:             return "[WEP]";
        case WIFI_AUTH_WPA_PSK:         return "[WPA_PSK]";
        case WIFI_AUTH_WPA2_PSK:        return "[WPA2_PSK]";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "[WPA_WPA2_PSK]";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "[WPA2_ENTERPRISE]";
        case WIFI_AUTH_WPA3_PSK:        return "[WPA3_PSK]";
        default:                        return "[UNKNOWN]";
    }
}

// WiFi channel to frequency (MHz)
static int channelToFrequency(int channel) {
    // 2.4 GHz band: channels 1-14
    if (channel >= 1 && channel <= 13) {
        return 2412 + (channel - 1) * 5;
    }
    if (channel == 14) {
        return 2484;
    }
    // 5 GHz band common channels
    if (channel >= 36 && channel <= 64) {
        return 5180 + (channel - 36) * 5;
    }
    if (channel >= 100 && channel <= 144) {
        return 5500 + (channel - 100) * 5;
    }
    if (channel >= 149 && channel <= 165) {
        return 5745 + (channel - 149) * 5;
    }
    return 0;
}

static bool isBSSIDSeen(const uint8_t* bssid) {
    for (int i = 0; i < seenCount; i++) {
        if (memcmp(seenBSSIDs[i], bssid, 6) == 0) {
            return true;
        }
    }
    return false;
}

static void addSeenBSSID(const uint8_t* bssid) {
    if (seenCount < WARDRIVING_MAX_NETWORKS) {
        memcpy(seenBSSIDs[seenCount], bssid, 6);
        seenCount++;
    }
}

static bool isBLEMACSeen(const uint8_t* mac) {
    for (int i = 0; i < seenBLECount; i++) {
        if (memcmp(seenBLEMACs[i], mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

static void addSeenBLEMAC(const uint8_t* mac) {
    if (seenBLECount < WARDRIVING_MAX_BLE_DEVICES) {
        memcpy(seenBLEMACs[seenBLECount], mac, 6);
        seenBLECount++;
    }
}

static String generateFilename() {
    // Generate filename with timestamp if GPS available, otherwise sequential
    GPSData gpsData = gpsGetData();

    if (gpsData.valid && gpsData.year > 2020) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s/%s%04d%02d%02d_%02d%02d%02d.csv",
                 WARDRIVING_LOG_DIR, WARDRIVING_FILE_PREFIX,
                 gpsData.year, gpsData.month, gpsData.day,
                 gpsData.hour, gpsData.minute, gpsData.second);
        return String(buf);
    } else {
        // Find next available file number
        for (int i = 1; i <= 999; i++) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s/%s%03d.csv",
                     WARDRIVING_LOG_DIR, WARDRIVING_FILE_PREFIX, i);
            if (!SD.exists(buf)) {
                return String(buf);
            }
        }
        return String(WARDRIVING_LOG_DIR "/halehound_overflow.csv");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

bool wardrivingInit() {
    if (sdInitialized) return stats.sdCardReady;

    Serial.println("[WARDRIVING] Initializing SD card...");

    // Set CS pin as output and deselect
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Try simple SD.begin() - let SD library handle SPI
    if (!SD.begin(SD_CS)) {
        Serial.println("[WARDRIVING] SD.begin(CS) failed, trying with explicit SPI...");

        // Try with explicit VSPI pins
        SPI.begin(18, 19, 23, SD_CS);
        if (!SD.begin(SD_CS, SPI, 4000000)) {
            Serial.println("[WARDRIVING] SD card init failed!");
            stats.sdCardReady = false;
            sdInitialized = true;
            return false;
        }
    }

    Serial.println("[WARDRIVING] SD card initialized OK");

    // Create wardriving directory if needed
    if (!SD.exists(WARDRIVING_LOG_DIR)) {
        SD.mkdir(WARDRIVING_LOG_DIR);
    }

    stats.sdCardReady = true;
    sdInitialized = true;
    Serial.println("[WARDRIVING] SD card ready");
    return true;
}

bool wardrivingSDReady() {
    return stats.sdCardReady;
}

bool wardrivingStart() {
    if (!stats.sdCardReady) {
        if (!wardrivingInit()) {
            return false;
        }
    }

    // Reset stats
    stats.networksLogged = 0;
    stats.newNetworks = 0;
    stats.duplicates = 0;
    stats.openNetworks = 0;
    stats.bleDevicesLogged = 0;
    stats.newBleDevices = 0;
    stats.bleDuplicates = 0;
    seenCount = 0;
    seenBLECount = 0;

    // Generate new filename
    stats.currentFile = generateFilename();

    // Deselect other SPI devices before SD access
    spiDeselect();

    // Open file and write WiGLE v1.6 header
    logFile = SD.open(stats.currentFile, FILE_WRITE);
    if (!logFile) {
        Serial.println("[WARDRIVING] Failed to create log file");
        return false;
    }

    // WiGLE v1.6 CSV header — pre-header line + column header
    logFile.println("WigleWifi-1.6,appRelease=HaleHound-CYD,model=ESP32-CYD,release=2.9.0,device=HaleHound,display=CYD,board=ESP32,brand=JesseCHale,star=Sol,body=3,subBody=0");
    logFile.println("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type");
    logFile.flush();

    stats.active = true;
    Serial.println("[WARDRIVING] Session started: " + stats.currentFile);
    return true;
}

void wardrivingStop() {
    if (logFile) {
        logFile.close();
    }
    stats.active = false;
    Serial.printf("[WARDRIVING] Session stopped. WiFi: %lu  BLE: %lu\n",
                  (unsigned long)stats.newNetworks, (unsigned long)stats.newBleDevices);
}

bool wardrivingIsActive() {
    return stats.active;
}

WardrivingStats wardrivingGetStats() {
    stats.gpsReady = gpsHasFix();
    return stats;
}

bool wardrivingLogNetwork(
    const uint8_t* bssid,
    const char* ssid,
    int rssi,
    int channel,
    int authMode
) {
    if (!stats.active || !logFile) {
        return false;
    }

    // Check for duplicate
    if (isBSSIDSeen(bssid)) {
        stats.duplicates++;
        return false;
    }

    // Track open networks
    if (authMode == WIFI_AUTH_OPEN) {
        stats.openNetworks++;
    }

    // Get GPS data
    GPSData gpsData = gpsGetData();

    // Build CSV line — WiGLE v1.6 format
    // MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type
    String line = macToString(bssid);
    line += ",";

    // Escape SSID (replace commas and quotes)
    String escapedSSID = ssid;
    escapedSSID.replace("\"", "\"\"");
    if (escapedSSID.indexOf(',') >= 0 || escapedSSID.indexOf('"') >= 0) {
        line += "\"" + escapedSSID + "\"";
    } else {
        line += escapedSSID;
    }
    line += ",";

    line += authModeToString(authMode);
    line += ",";

    // Timestamp
    if (gpsData.valid && gpsData.year > 2020) {
        char timestamp[24];
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
                 gpsData.year, gpsData.month, gpsData.day,
                 gpsData.hour, gpsData.minute, gpsData.second);
        line += timestamp;
    } else {
        line += "0000-00-00 00:00:00";
    }
    line += ",";

    // Channel
    line += String(channel);
    line += ",";

    // Frequency (MHz)
    int freq = channelToFrequency(channel);
    if (freq > 0) {
        line += String(freq);
    }
    line += ",";

    // RSSI
    line += String(rssi);
    line += ",";

    // GPS coordinates
    if (gpsData.valid) {
        char lat[16], lon[16], alt[16];
        snprintf(lat, sizeof(lat), "%.6f", gpsData.latitude);
        snprintf(lon, sizeof(lon), "%.6f", gpsData.longitude);
        snprintf(alt, sizeof(alt), "%.1f", gpsData.altitude);
        line += lat;
        line += ",";
        line += lon;
        line += ",";
        line += alt;
        line += ",";
        line += "10";  // Accuracy estimate in meters
    } else {
        line += "0.0,0.0,0.0,0";
    }
    line += ",";

    // RCOIs — empty for WiFi
    line += ",";

    // MfgrId — empty for WiFi
    line += ",";

    line += "WIFI";

    // Deselect other SPI devices before SD write
    spiDeselect();

    // Write to file
    logFile.println(line);
    logFile.flush();

    // Track this BSSID
    addSeenBSSID(bssid);
    stats.networksLogged++;
    stats.newNetworks++;

    return true;
}

bool wardrivingLogBleDevice(
    const uint8_t* mac,
    const char* name,
    int rssi,
    const uint8_t* mfgData,
    uint8_t mfgDataLen
) {
    if (!stats.active || !logFile) {
        return false;
    }

    // Check for duplicate
    if (isBLEMACSeen(mac)) {
        stats.bleDuplicates++;
        return false;
    }

    // Get GPS data
    GPSData gpsData = gpsGetData();

    // Build CSV line — WiGLE v1.6 format for BLE
    // MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,Lat,Lon,Alt,Acc,RCOIs,MfgrId,Type
    String line = macToString(mac);
    line += ",";

    // SSID = BLE device name (escape if needed)
    if (name && name[0] != '\0') {
        String escapedName = name;
        escapedName.replace("\"", "\"\"");
        if (escapedName.indexOf(',') >= 0 || escapedName.indexOf('"') >= 0) {
            line += "\"" + escapedName + "\"";
        } else {
            line += escapedName;
        }
    }
    line += ",";

    // AuthMode for BLE
    line += "[LE]";
    line += ",";

    // Timestamp
    if (gpsData.valid && gpsData.year > 2020) {
        char timestamp[24];
        snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d",
                 gpsData.year, gpsData.month, gpsData.day,
                 gpsData.hour, gpsData.minute, gpsData.second);
        line += timestamp;
    } else {
        line += "0000-00-00 00:00:00";
    }
    line += ",";

    // Channel — 0 for BLE
    line += "0";
    line += ",";

    // Frequency — empty for BLE
    line += ",";

    // RSSI
    line += String(rssi);
    line += ",";

    // GPS coordinates
    if (gpsData.valid) {
        char lat[16], lon[16], alt[16];
        snprintf(lat, sizeof(lat), "%.6f", gpsData.latitude);
        snprintf(lon, sizeof(lon), "%.6f", gpsData.longitude);
        snprintf(alt, sizeof(alt), "%.1f", gpsData.altitude);
        line += lat;
        line += ",";
        line += lon;
        line += ",";
        line += alt;
        line += ",";
        line += "10";
    } else {
        line += "0.0,0.0,0.0,0";
    }
    line += ",";

    // RCOIs — empty for BLE
    line += ",";

    // MfgrId — first 2 bytes of manufacturer data = company ID (little-endian)
    if (mfgData && mfgDataLen >= 2) {
        uint16_t companyId = mfgData[0] | (mfgData[1] << 8);
        char mfgBuf[8];
        snprintf(mfgBuf, sizeof(mfgBuf), "0x%04X", companyId);
        line += mfgBuf;
    }
    line += ",";

    line += "BLE";

    // Deselect other SPI devices before SD write
    spiDeselect();

    // Write to file
    logFile.println(line);
    logFile.flush();

    // Track this BLE MAC
    addSeenBLEMAC(mac);
    stats.bleDevicesLogged++;
    stats.newBleDevices++;

    return true;
}

void wardrivingLogScan(void* apListPtr, int count) {
    if (!stats.active) return;

    // Update GPS before logging
    gpsUpdate();

    wifi_ap_record_t* apList = (wifi_ap_record_t*)apListPtr;

    for (int i = 0; i < count; i++) {
        wardrivingLogNetwork(
            apList[i].bssid,
            (const char*)apList[i].ssid,
            apList[i].rssi,
            apList[i].primary,
            apList[i].authmode
        );
    }
}

void wardrivingDrawStatus(int x, int y) {
    tft.setTextSize(1);

    if (!stats.active) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(x, y);
        tft.print("WD: OFF");
        return;
    }

    // Active status
    if (stats.gpsReady) {
        tft.setTextColor(HALEHOUND_MAGENTA);
    } else {
        tft.setTextColor(HALEHOUND_HOTPINK);
    }

    tft.setCursor(x, y);
    tft.print("WD:");
    tft.print(stats.newNetworks);
    tft.print("+");
    tft.print(stats.newBleDevices);

    if (!stats.gpsReady) {
        tft.print(" !GPS");
    }
}

void wardrivingDrawIndicator(int x, int y) {
    if (stats.active) {
        // Blinking indicator when active
        static bool blink = false;
        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 500) {
            blink = !blink;
            lastBlink = millis();
        }

        if (blink) {
            tft.fillCircle(x + 3, y + 3, 3, stats.gpsReady ? HALEHOUND_MAGENTA : HALEHOUND_HOTPINK);
        } else {
            tft.fillCircle(x + 3, y + 3, 3, HALEHOUND_DARK);
        }
        tft.drawCircle(x + 3, y + 3, 3, HALEHOUND_MAGENTA);
    }
}
