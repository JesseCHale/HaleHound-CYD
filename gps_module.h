#ifndef GPS_MODULE_H
#define GPS_MODULE_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD GPS Module
// NEO-6M GPS Support with TinyGPSPlus
// Created: 2026-02-07
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// GPS CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define GPS_UPDATE_INTERVAL_MS  1000    // Update display every 1 second
#define GPS_TIMEOUT_MS          30000   // Consider GPS stale after 30 seconds
                                        // Wardriving WiFi scans block GPS for 1-3s each cycle;
                                        // 5s was too aggressive and killed valid between scans

// ═══════════════════════════════════════════════════════════════════════════
// GPS STATUS ENUM
// ═══════════════════════════════════════════════════════════════════════════

enum GPSStatus {
    GPS_NO_MODULE,      // GPS not detected / not responding
    GPS_SEARCHING,      // GPS active but no fix yet
    GPS_FIX_2D,         // 2D fix (lat/lng only, no altitude)
    GPS_FIX_3D          // 3D fix (full position with altitude)
};

// ═══════════════════════════════════════════════════════════════════════════
// GPS DATA STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════

struct GPSData {
    bool valid;             // GPS has fix
    double latitude;        // Decimal degrees
    double longitude;       // Decimal degrees
    double altitude;        // Meters
    double speed;           // km/h
    double course;          // Degrees (0-360)
    int satellites;         // Number of satellites in view
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    uint32_t age;           // Age of last fix in ms
    double hdop;            // Horizontal dilution of precision
};

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

// Initialize GPS module - call once at startup
void gpsSetup();

// ═══════════════════════════════════════════════════════════════════════════
// GPS SCREEN FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

// Enter GPS screen (draws UI, runs loop)
void gpsScreen();

// Update GPS data from serial (call frequently)
void gpsUpdate();

// Check if GPS has valid fix
bool gpsHasFix();

// Get current GPS data
GPSData gpsGetData();

// ═══════════════════════════════════════════════════════════════════════════
// WARDRIVING SUPPORT
// ═══════════════════════════════════════════════════════════════════════════

// Get formatted location string for logging
// Format: "lat,lon" with 6 decimal places
String gpsGetLocationString();

// Get formatted timestamp from GPS
// Format: "YYYY-MM-DD HH:MM:SS"
String gpsGetTimestamp();

// Check if GPS data is fresh (within timeout)
bool gpsIsFresh();

// Get GPS status (NO_MODULE, SEARCHING, FIX_2D, FIX_3D)
GPSStatus gpsGetStatus();

// Get number of satellites in view
uint8_t gpsGetSatellites();

// ═══════════════════════════════════════════════════════════════════════════
// BACKGROUND GPS (for wardriving — no screen, no scan)
// ═══════════════════════════════════════════════════════════════════════════

// Start GPS in background mode — kills Serial to free GPIO 3, opens UART2
// Call gpsUpdate() periodically to feed the parser
void gpsStartBackground();

// Stop GPS background mode — closes UART2, restores Serial
void gpsStopBackground();

// ═══════════════════════════════════════════════════════════════════════════
// DIAGNOSTICS — TinyGPSPlus counters for debugging data flow
// ═══════════════════════════════════════════════════════════════════════════

// Total chars read from UART2 (is data flowing?)
uint32_t gpsCharsProcessed();

// Total NMEA sentences that had valid fix (is GPS holding fix?)
uint32_t gpsSentencesWithFix();

// Total sentences with bad checksum (UART overflow / corruption?)
uint32_t gpsFailedChecksums();

// Milliseconds since last valid location update (staleness)
uint32_t gpsTimeSinceLastUpdate();

// '$' characters seen (should match sentence count if data is NMEA)
uint32_t gpsDollarsSeen();

// Sentences that passed checksum (valid NMEA sentences parsed)
uint32_t gpsPassedChecksums();

// Raw data preview — last 32 chars from UART2 (for debugging bad data)
const char* gpsRawDataPreview();

// Direct TinyGPSPlus satellite value — bypasses currentData
int32_t gpsRawSatValue();

// How many times gps.satellites.isUpdated() fired in gpsUpdate()
uint32_t gpsSatUpdateCount();

#endif // GPS_MODULE_H
