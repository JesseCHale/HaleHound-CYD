// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Wardriving Screen
// Full-screen wardriving UI with scan, log, and GPS status
// Created: 2026-02-16
// ═══════════════════════════════════════════════════════════════════════════

#include "wardriving_screen.h"
#include "wardriving.h"
#include "gps_module.h"
#include "spi_manager.h"
#include "touch_buttons.h"
#include "shared.h"
#include "utils.h"
#include "icon.h"
#include "nosifer_font.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define WD_SCAN_INTERVAL_MS   5000    // WiFi scan every 5 seconds
#define WD_DISPLAY_INTERVAL_MS 500    // Update display every 500ms
#define WD_BLINK_INTERVAL_MS   400    // Record indicator blink rate

// Layout constants
#define WD_FRAME_X       5
#define WD_FRAME_Y       62
#define WD_FRAME_W       230
#define WD_FRAME_H       52
#define WD_STATS_Y       122
#define WD_GPS_Y         170
#define WD_FILE_Y        218
#define WD_BTN_X         40
#define WD_BTN_Y         260
#define WD_BTN_W         160
#define WD_BTN_H         40

// ═══════════════════════════════════════════════════════════════════════════
// MODULE STATE
// ═══════════════════════════════════════════════════════════════════════════

static bool wdExitRequested = false;
static bool wdScanning = false;
static unsigned long wdLastScan = 0;
static unsigned long wdLastDisplay = 0;
static unsigned long wdLastBlink = 0;
static bool wdBlinkState = false;
static uint32_t wdScanCount = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR — matches GPS/other screens
// ═══════════════════════════════════════════════════════════════════════════

static void drawWDIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static bool isWDBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 30) {
            delay(150);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// START/STOP BUTTON
// ═══════════════════════════════════════════════════════════════════════════

static void drawStartStopButton(bool active) {
    // Clear button area
    tft.fillRect(WD_BTN_X - 2, WD_BTN_Y - 2, WD_BTN_W + 4, WD_BTN_H + 4, HALEHOUND_BLACK);

    if (active) {
        // STOP button — hotpink border, hotpink text
        tft.drawRoundRect(WD_BTN_X, WD_BTN_Y, WD_BTN_W, WD_BTN_H, 8, HALEHOUND_HOTPINK);
        tft.drawRoundRect(WD_BTN_X + 1, WD_BTN_Y + 1, WD_BTN_W - 2, WD_BTN_H - 2, 7, HALEHOUND_HOTPINK);
        tft.setFreeFont(&Nosifer_Regular10pt7b);
        tft.setTextColor(HALEHOUND_HOTPINK);
        int16_t tw = tft.textWidth("STOP");
        tft.setCursor(WD_BTN_X + (WD_BTN_W - tw) / 2, WD_BTN_Y + 28);
        tft.print("STOP");
        tft.setFreeFont(NULL);
    } else {
        // START button — cyan border, cyan text
        tft.drawRoundRect(WD_BTN_X, WD_BTN_Y, WD_BTN_W, WD_BTN_H, 8, HALEHOUND_MAGENTA);
        tft.drawRoundRect(WD_BTN_X + 1, WD_BTN_Y + 1, WD_BTN_W - 2, WD_BTN_H - 2, 7, HALEHOUND_MAGENTA);
        tft.setFreeFont(&Nosifer_Regular10pt7b);
        tft.setTextColor(HALEHOUND_MAGENTA);
        int16_t tw = tft.textWidth("START");
        tft.setCursor(WD_BTN_X + (WD_BTN_W - tw) / 2, WD_BTN_Y + 28);
        tft.print("START");
        tft.setFreeFont(NULL);
    }
}

static bool isStartStopTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (tx >= WD_BTN_X && tx <= WD_BTN_X + WD_BTN_W &&
            ty >= WD_BTN_Y && ty <= WD_BTN_Y + WD_BTN_H) {
            delay(200);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// INITIAL SCREEN DRAW
// ═══════════════════════════════════════════════════════════════════════════

static void drawWDScreen() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawWDIconBar();

    // Glitch title — Nosifer font
    drawGlitchText(55, "WARDRIVING", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    // Main stats frame
    tft.drawRoundRect(WD_FRAME_X, WD_FRAME_Y, WD_FRAME_W, WD_FRAME_H, 6, HALEHOUND_VIOLET);
    tft.drawRoundRect(WD_FRAME_X + 1, WD_FRAME_Y + 1, WD_FRAME_W - 2, WD_FRAME_H - 2, 5, HALEHOUND_GUNMETAL);

    // Stats labels
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_STATS_Y);
    tft.print("NETWORKS");
    tft.setCursor(125, WD_STATS_Y);
    tft.print("DUPES");
    tft.setCursor(10, WD_STATS_Y + 18);
    tft.print("SCANS");
    tft.setCursor(125, WD_STATS_Y + 18);
    tft.print("STATUS");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_STATS_Y + 36, WD_FRAME_X + WD_FRAME_W, WD_STATS_Y + 36, HALEHOUND_HOTPINK);

    // GPS section labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_GPS_Y);
    tft.print("GPS");
    tft.setCursor(125, WD_GPS_Y);
    tft.print("SATS");
    tft.setCursor(10, WD_GPS_Y + 18);
    tft.print("LAT");
    tft.setCursor(125, WD_GPS_Y + 18);
    tft.print("LON");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_GPS_Y + 36, WD_FRAME_X + WD_FRAME_W, WD_GPS_Y + 36, HALEHOUND_HOTPINK);

    // File section label
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_FILE_Y);
    tft.print("SD FILE");

    // Draw button
    drawStartStopButton(false);
}

// ═══════════════════════════════════════════════════════════════════════════
// UPDATE DISPLAY VALUES
// ═══════════════════════════════════════════════════════════════════════════

static void updateWDValues() {
    WardrivingStats stats = wardrivingGetStats();
    GPSData gpsData = gpsGetData();
    char buf[48];

    tft.setTextSize(1);

    // ── Main stats frame values ──
    tft.fillRect(WD_FRAME_X + 3, WD_FRAME_Y + 3, WD_FRAME_W - 6, WD_FRAME_H - 6, HALEHOUND_BLACK);

    if (stats.active) {
        // Big network count in frame — centered, Nosifer
        tft.setFreeFont(&Nosifer_Regular12pt7b);
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats.newNetworks);
        int16_t tw = tft.textWidth(buf);
        tft.setCursor(WD_FRAME_X + (WD_FRAME_W - tw) / 2, WD_FRAME_Y + 38);
        tft.print(buf);
        tft.setFreeFont(NULL);
    } else {
        tft.setFreeFont(&FreeMono9pt7b);
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(WD_FRAME_X + 40, WD_FRAME_Y + 35);
        tft.print("-- idle --");
        tft.setFreeFont(NULL);
    }

    // NETWORKS value
    tft.setTextSize(1);
    tft.fillRect(65, WD_STATS_Y, 55, 10, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(65, WD_STATS_Y);
    tft.print(stats.newNetworks);

    // DUPES value
    tft.fillRect(165, WD_STATS_Y, 65, 10, HALEHOUND_BLACK);
    tft.setCursor(165, WD_STATS_Y);
    tft.print(stats.duplicates);

    // SCANS value
    tft.fillRect(50, WD_STATS_Y + 18, 65, 10, HALEHOUND_BLACK);
    tft.setCursor(50, WD_STATS_Y + 18);
    tft.print(wdScanCount);

    // STATUS value
    tft.fillRect(170, WD_STATS_Y + 18, 65, 10, HALEHOUND_BLACK);
    tft.setCursor(170, WD_STATS_Y + 18);
    if (stats.active) {
        // Blinking record indicator
        if (wdBlinkState) {
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.print("REC");
            tft.fillCircle(200, WD_STATS_Y + 22, 3, HALEHOUND_HOTPINK);
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.print("REC");
            tft.fillCircle(200, WD_STATS_Y + 22, 3, HALEHOUND_GUNMETAL);
        }
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("IDLE");
    }

    // ── GPS values ──

    // GPS fix status
    tft.fillRect(30, WD_GPS_Y, 85, 10, HALEHOUND_BLACK);
    tft.setCursor(30, WD_GPS_Y);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.print("FIX OK");
    } else {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("NO FIX");
    }

    // SATS value
    tft.fillRect(160, WD_GPS_Y, 50, 10, HALEHOUND_BLACK);
    tft.setTextColor(gpsData.satellites > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(160, WD_GPS_Y);
    tft.print(gpsData.satellites);

    // LAT value
    tft.fillRect(30, WD_GPS_Y + 18, 90, 10, HALEHOUND_BLACK);
    tft.setCursor(30, WD_GPS_Y + 18);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.latitude);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // LON value
    tft.fillRect(150, WD_GPS_Y + 18, 85, 10, HALEHOUND_BLACK);
    tft.setCursor(150, WD_GPS_Y + 18);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.longitude);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // ── SD File ──
    tft.fillRect(10, WD_FILE_Y + 14, 220, 10, HALEHOUND_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, WD_FILE_Y + 14);
    if (stats.active && stats.currentFile.length() > 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        // Show just the filename, not full path
        int lastSlash = stats.currentFile.lastIndexOf('/');
        if (lastSlash >= 0) {
            tft.print(stats.currentFile.substring(lastSlash + 1));
        } else {
            tft.print(stats.currentFile);
        }
    } else if (stats.sdCardReady) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("SD ready -- tap START");
    } else {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("NO SD CARD");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// WIFI SCAN FOR WARDRIVING
// ═══════════════════════════════════════════════════════════════════════════

static void wdRunScan() {
    // Use ESP-IDF scan (not Arduino WiFi.scan) to avoid conflicts
    wifi_scan_config_t scanConfig;
    memset(&scanConfig, 0, sizeof(scanConfig));
    scanConfig.ssid = NULL;
    scanConfig.bssid = NULL;
    scanConfig.channel = 0;         // All channels
    scanConfig.show_hidden = true;
    scanConfig.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanConfig.scan_time.active.min = 100;
    scanConfig.scan_time.active.max = 300;

    esp_err_t err = esp_wifi_scan_start(&scanConfig, true);  // Blocking scan
    if (err != ESP_OK) {
        Serial.printf("[WARDRIVING] Scan failed: %d\n", err);
        return;
    }

    uint16_t apCount = 0;
    esp_wifi_scan_get_ap_num(&apCount);

    if (apCount == 0) {
        esp_wifi_scan_get_ap_records(&apCount, NULL);
        return;
    }

    // Cap at reasonable number
    if (apCount > 64) apCount = 64;

    wifi_ap_record_t* apRecords = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * apCount);
    if (!apRecords) {
        esp_wifi_scan_get_ap_records(&apCount, NULL);
        return;
    }

    esp_wifi_scan_get_ap_records(&apCount, apRecords);

    // Log through wardriving backend
    wardrivingLogScan(apRecords, apCount);

    free(apRecords);
    wdScanCount++;
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN SCREEN FUNCTION
// ═══════════════════════════════════════════════════════════════════════════

void wardrivingScreen() {
    // Reset state
    wdExitRequested = false;
    wdScanning = false;
    wdLastScan = 0;
    wdLastDisplay = 0;
    wdLastBlink = 0;
    wdBlinkState = false;
    wdScanCount = 0;

    // Init WiFi in STA mode for scanning
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Feed GPS parser
    gpsUpdate();

    // Init SD card through wardriving backend
    wardrivingInit();

    // Draw initial screen
    drawWDScreen();
    updateWDValues();

    // Main loop
    while (!wdExitRequested) {
        // Feed GPS parser
        gpsUpdate();

        // Handle touch
        touchButtonsUpdate();

        // Check back button
        if (isWDBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            wdExitRequested = true;
            break;
        }

        // Check start/stop button
        if (isStartStopTapped()) {
            if (wdScanning) {
                // Stop
                wardrivingStop();
                wdScanning = false;
                drawStartStopButton(false);
            } else {
                // Start
                if (wardrivingStart()) {
                    wdScanning = true;
                    wdScanCount = 0;
                    drawStartStopButton(true);
                    // Run first scan immediately
                    wdRunScan();
                    wdLastScan = millis();
                } else {
                    // SD card failed — flash error
                    tft.fillRect(10, WD_FILE_Y + 14, 220, 10, HALEHOUND_BLACK);
                    tft.setTextColor(HALEHOUND_HOTPINK);
                    tft.setCursor(10, WD_FILE_Y + 14);
                    tft.print("SD CARD ERROR!");
                }
            }
        }

        // Periodic WiFi scan
        if (wdScanning && millis() - wdLastScan >= WD_SCAN_INTERVAL_MS) {
            wdRunScan();
            wdLastScan = millis();
        }

        // Blink timer
        if (millis() - wdLastBlink >= WD_BLINK_INTERVAL_MS) {
            wdBlinkState = !wdBlinkState;
            wdLastBlink = millis();
        }

        // Update display
        if (millis() - wdLastDisplay >= WD_DISPLAY_INTERVAL_MS) {
            updateWDValues();
            wdLastDisplay = millis();
        }

        delay(10);
    }

    // Cleanup
    if (wdScanning) {
        wardrivingStop();
        wdScanning = false;
    }

    // Kill WiFi
    esp_wifi_stop();
}
