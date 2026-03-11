// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Wardriving Screen
// Full-screen wardriving UI with WiFi + BLE scan, GPS, and SD logging
// Created: 2026-02-16
// Updated: 2026-02-24 — BLE scanning, faster interval, redesigned display
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
#include <BLEDevice.h>
#include <esp_bt.h>

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define WD_SCAN_INTERVAL_MS    1000    // Delay between scan completions
                                        // WiFi scans are async so touch stays responsive
#define WD_DISPLAY_INTERVAL_MS  500    // Update display every 500ms
#define WD_BLINK_INTERVAL_MS    400    // Record indicator blink rate
#define WD_BLE_SCAN_SECONDS       2    // BLE passive scan duration (2s catches most devices)
#define WD_BLE_EVERY_N            3    // BLE scan every Nth cycle (WiFi all other cycles)

// Scan phase — WiFi dominant, BLE every Nth cycle for better coverage at speed
// At 30mph (~44 ft/s) an AP's range is ~5-7s. Need WiFi scan every ~3s to catch it.
#define WD_PHASE_WIFI  0
#define WD_PHASE_BLE   1

// Layout constants (scaled for 2.8"/3.5")
#define WD_FRAME_X       5
#define WD_FRAME_Y       SCALE_Y(62)
#define WD_FRAME_W       (SCREEN_WIDTH - 10)
#define WD_FRAME_H       SCALE_H(52)
#define WD_STATS_Y       SCALE_Y(122)
#define WD_GPS_Y         SCALE_Y(158)
#define WD_SPEED_Y       SCALE_Y(184)
#define WD_FILE_Y        SCALE_Y(200)
#define WD_BTN_X         SCALE_X(40)
#define WD_BTN_Y         SCALE_Y(260)
#define WD_BTN_W         SCALE_W(160)
#define WD_BTN_H         SCALE_H(40)
// Column positions for stats labels/values
#define WD_COL_R_LABEL   SCALE_X(125)
#define WD_COL_R_VAL     SCALE_X(155)
#define WD_COL_L_VAL1    SCALE_X(65)
#define WD_COL_L_VAL2    SCALE_X(30)
#define WD_COL_L_VAL3    SCALE_X(50)
#define WD_COL_L_VAL4    SCALE_X(48)

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
static esp_err_t wdLastScanErr = ESP_OK;  // Track scan errors for TFT display
static uint8_t wdScanPhase = WD_PHASE_WIFI;
static uint32_t wdScanCycle = 0;           // Cycle counter for WiFi/BLE ratio
static unsigned long wdSessionStart = 0;   // millis() when session started

// BLE scan state — volatile callback queue (same pattern as BleSniffer)
static BLEScan* wdBleScan = nullptr;

// BLE scan completion flag — set by callback, polled with timeout
static volatile bool wdBleScanDone = false;

static void wdBleScanCompleteCB(BLEScanResults results) {
    wdBleScanDone = true;
}

// Temp buffer for BLE results — process after scan completes
#define WD_BLE_RESULT_MAX 24
struct WdBleResult {
    uint8_t mac[6];
    int8_t  rssi;
    char    name[17];
    uint8_t mfgData[8];
    uint8_t mfgLen;
};
static WdBleResult wdBleResults[WD_BLE_RESULT_MAX];
static int wdBleResultCount = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR — matches GPS/other screens
// ═══════════════════════════════════════════════════════════════════════════

static void drawWDIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_DARK);
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static bool isWDBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_Y && ty <= ICON_BAR_BOTTOM && tx >= 10 && tx < 30) {
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
    drawGlitchText(SCALE_Y(55), "WARDRIVING", &Nosifer_Regular10pt7b);
    tft.drawLine(0, SCALE_Y(58), SCREEN_WIDTH, SCALE_Y(58), HALEHOUND_HOTPINK);

    // Main stats frame
    tft.drawRoundRect(WD_FRAME_X, WD_FRAME_Y, WD_FRAME_W, WD_FRAME_H, 6, HALEHOUND_VIOLET);
    tft.drawRoundRect(WD_FRAME_X + 1, WD_FRAME_Y + 1, WD_FRAME_W - 2, WD_FRAME_H - 2, 5, HALEHOUND_GUNMETAL);

    // Row 1: NETWORKS / OPEN
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_STATS_Y);
    tft.print("NETWORKS");
    tft.setCursor(WD_COL_R_LABEL, WD_STATS_Y);
    tft.print("OPEN");

    // Row 2: BLE / DUPES
    tft.setCursor(10, WD_STATS_Y + SCALE_H(10));
    tft.print("BLE");
    tft.setCursor(WD_COL_R_LABEL, WD_STATS_Y + SCALE_H(10));
    tft.print("DUPES");

    // Row 3: SCANS / STATUS
    tft.setCursor(10, WD_STATS_Y + SCALE_H(20));
    tft.print("SCANS");
    tft.setCursor(WD_COL_R_LABEL, WD_STATS_Y + SCALE_H(20));
    tft.print("STATUS");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_STATS_Y + SCALE_H(33), WD_FRAME_X + WD_FRAME_W, WD_STATS_Y + SCALE_H(33), HALEHOUND_HOTPINK);

    // GPS section labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_GPS_Y);
    tft.print("GPS");
    tft.setCursor(WD_COL_R_LABEL, WD_GPS_Y);
    tft.print("SATS");
    tft.setCursor(10, WD_GPS_Y + SCALE_H(12));
    tft.print("LAT");
    tft.setCursor(WD_COL_R_LABEL, WD_GPS_Y + SCALE_H(12));
    tft.print("LON");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_GPS_Y + SCALE_H(24), WD_FRAME_X + WD_FRAME_W, WD_GPS_Y + SCALE_H(24), HALEHOUND_HOTPINK);

    // Speed / Time labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_SPEED_Y);
    tft.print("SPEED");
    tft.setCursor(WD_COL_R_LABEL, WD_SPEED_Y);
    tft.print("TIME");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_SPEED_Y + SCALE_H(12), WD_FRAME_X + WD_FRAME_W, WD_SPEED_Y + SCALE_H(12), HALEHOUND_HOTPINK);

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
        tft.setCursor(WD_FRAME_X + (WD_FRAME_W - tw) / 2, WD_FRAME_Y + SCALE_H(38));
        tft.print(buf);
        tft.setFreeFont(NULL);
    } else {
        tft.setFreeFont(&FreeMono9pt7b);
        tft.setTextColor(HALEHOUND_GUNMETAL);
        int16_t idleTw = tft.textWidth("-- idle --");
        tft.setCursor(WD_FRAME_X + (WD_FRAME_W - idleTw) / 2, WD_FRAME_Y + SCALE_H(35));
        tft.print("-- idle --");
        tft.setFreeFont(NULL);
    }

    // ── Row 1: NETWORKS value / OPEN value ──
    tft.setTextSize(1);

    // NETWORKS value
    tft.fillRect(WD_COL_L_VAL1, WD_STATS_Y, SCALE_W(55), 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(WD_COL_L_VAL1, WD_STATS_Y);
    tft.print(stats.newNetworks);

    // OPEN value
    tft.fillRect(WD_COL_R_VAL, WD_STATS_Y, SCALE_W(75), 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? (stats.openNetworks > 0 ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA) : HALEHOUND_GUNMETAL);
    tft.setCursor(WD_COL_R_VAL, WD_STATS_Y);
    tft.print(stats.openNetworks);

    // ── Row 2: BLE value / DUPES value ──

    // BLE value
    tft.fillRect(WD_COL_L_VAL2, WD_STATS_Y + SCALE_H(10), SCALE_W(85), 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.newBleDevices > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(WD_COL_L_VAL2, WD_STATS_Y + SCALE_H(10));
    tft.print(stats.newBleDevices);

    // DUPES value (WiFi + BLE combined)
    tft.fillRect(SCALE_X(165), WD_STATS_Y + SCALE_H(10), SCALE_W(65), 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(SCALE_X(165), WD_STATS_Y + SCALE_H(10));
    tft.print(stats.duplicates + stats.bleDuplicates);

    // ── Row 3: SCANS value / STATUS value ──

    // SCANS value
    tft.fillRect(WD_COL_L_VAL3, WD_STATS_Y + SCALE_H(20), SCALE_W(65), 8, HALEHOUND_BLACK);
    tft.setCursor(WD_COL_L_VAL3, WD_STATS_Y + SCALE_H(20));
    if (wdLastScanErr != ESP_OK) {
        // Show error in red — clear after one successful scan so it doesn't stick
        tft.setTextColor(0xF800);
        tft.print("ERR");
    } else {
        tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
        tft.print(wdScanCount);
    }

    // STATUS value — shows current scan phase (WiFi dominant, BLE every Nth)
    tft.fillRect(SCALE_X(170), WD_STATS_Y + SCALE_H(20), SCALE_W(65), 8, HALEHOUND_BLACK);
    tft.setCursor(SCALE_X(170), WD_STATS_Y + SCALE_H(20));
    if (stats.active) {
        bool isBle = (wdScanCycle % WD_BLE_EVERY_N == (WD_BLE_EVERY_N - 1));
        if (wdBlinkState) {
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.print(isBle ? "BLE" : "WIFI");
            tft.fillCircle(SCALE_X(205), WD_STATS_Y + SCALE_H(24), 3, HALEHOUND_HOTPINK);
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.print(isBle ? "BLE" : "WIFI");
            tft.fillCircle(SCALE_X(205), WD_STATS_Y + SCALE_H(24), 3, HALEHOUND_GUNMETAL);
        }
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("IDLE");
    }

    // ── GPS values ──

    // GPS fix status
    // For wardriving, coordinates from a recent fix are still useful even if the
    // staleness timer set valid=false. Show "STALE" instead of "NO FIX" when we
    // have non-zero coordinates — they're close enough for WiGLE logging.
    bool hasPosition = (gpsData.latitude != 0.0 || gpsData.longitude != 0.0);
    tft.fillRect(WD_COL_L_VAL2, WD_GPS_Y, SCALE_W(85), 8, HALEHOUND_BLACK);
    tft.setCursor(WD_COL_L_VAL2, WD_GPS_Y);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.print("FIX OK");
    } else if (hasPosition) {
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.print("STALE");
    } else {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("NO FIX");
    }

    // SATS value
    tft.fillRect(WD_COL_R_VAL, WD_GPS_Y, SCALE_W(50), 8, HALEHOUND_BLACK);
    tft.setTextColor(gpsData.satellites > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(WD_COL_R_VAL, WD_GPS_Y);
    tft.print(gpsData.satellites);

    // LAT value — show stale coordinates in VIOLET instead of hiding them
    tft.fillRect(WD_COL_L_VAL2, WD_GPS_Y + SCALE_H(12), SCALE_W(90), 8, HALEHOUND_BLACK);
    tft.setCursor(WD_COL_L_VAL2, WD_GPS_Y + SCALE_H(12));
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.latitude);
        tft.print(buf);
    } else if (hasPosition) {
        tft.setTextColor(HALEHOUND_VIOLET);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.latitude);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // LON value — show stale coordinates in VIOLET instead of hiding them
    tft.fillRect(SCALE_X(150), WD_GPS_Y + SCALE_H(12), SCALE_W(85), 8, HALEHOUND_BLACK);
    tft.setCursor(SCALE_X(150), WD_GPS_Y + SCALE_H(12));
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.longitude);
        tft.print(buf);
    } else if (hasPosition) {
        tft.setTextColor(HALEHOUND_VIOLET);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.longitude);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // ── Speed / Time values ──

    // SPEED value (km/h from GPS)
    tft.fillRect(WD_COL_L_VAL4, WD_SPEED_Y, SCALE_W(70), 8, HALEHOUND_BLACK);
    tft.setCursor(WD_COL_L_VAL4, WD_SPEED_Y);
    if (gpsData.valid && gpsData.speed >= 0.0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.1f km/h", gpsData.speed);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // TIME value (session elapsed)
    tft.fillRect(WD_COL_R_VAL, WD_SPEED_Y, SCALE_W(80), 8, HALEHOUND_BLACK);
    tft.setCursor(WD_COL_R_VAL, WD_SPEED_Y);
    if (stats.active && wdSessionStart > 0) {
        unsigned long elapsed = (millis() - wdSessionStart) / 1000;
        unsigned long hrs = elapsed / 3600;
        unsigned long mins = (elapsed % 3600) / 60;
        unsigned long secs = elapsed % 60;
        tft.setTextColor(HALEHOUND_MAGENTA);
        if (hrs > 0) {
            snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu", hrs, mins, secs);
        } else {
            snprintf(buf, sizeof(buf), "%lu:%02lu", mins, secs);
        }
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("0:00");
    }

    // ── SD File ──
    tft.fillRect(10, WD_FILE_Y + SCALE_H(14), SCREEN_WIDTH - 20, 8, HALEHOUND_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, WD_FILE_Y + SCALE_H(14));
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

// WiFi scan with per-channel STOP checks.
// Scans channels 1-13 one at a time (~200ms each). Touch is checked between
// channels so STOP responds within ~200ms instead of blocking for 3+ seconds.
// Returns false if user pressed STOP mid-scan (wdScanning already set false).
static bool wdRunScan() {
    for (uint8_t ch = 1; ch <= 13; ch++) {
        // Scan single channel — blocks ~100-200ms
        int n = WiFi.scanNetworks(false, true, false, 200, ch);

        if (n > 0) {
            gpsUpdate();
            int cap = (n > 64) ? 64 : n;
            for (int i = 0; i < cap; i++) {
                wardrivingLogNetwork(
                    WiFi.BSSID(i),
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i),
                    WiFi.channel(i),
                    WiFi.encryptionType(i)
                );
            }
        }

        WiFi.scanDelete();

        // Check STOP button between channels — ~200ms responsiveness
        touchButtonsUpdate();
        if (isStartStopTapped()) {
            wardrivingStop();
            wdScanning = false;
            wdSessionStart = 0;
            WiFi.disconnect();
            drawStartStopButton(false);
            return false;
        }

        // Feed GPS between channels
        gpsUpdate();
    }

    wdLastScanErr = ESP_OK;
    wdScanCount++;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// BLE SCAN FOR WARDRIVING
// WiFi and BLE share the ESP32 radio — cannot run both at once.
// Pattern: tear down WiFi → init BLE → passive scan → tear down BLE → restart WiFi
// Based on proven BleSniffer pattern from bluetooth_attacks.cpp
// ═══════════════════════════════════════════════════════════════════════════

static bool wdClassicBtReleased = false;

static void wdRunBleScan() {
    Serial.println("[WARDRIVING] BLE scan phase starting...");

    // Step 1: Tear down WiFi to free the radio
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Release Classic BT memory on first BLE cycle — we only use BLE, never Classic.
    // This frees ~28KB of heap that Bluedroid would otherwise reserve for Classic BT.
    // Must be called BEFORE esp_bt_controller_init() (which BLEDevice::init triggers).
    // One-shot: once released, the memory stays free for all subsequent BLE cycles.
    if (!wdClassicBtReleased) {
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        wdClassicBtReleased = true;
    }

    // Step 2: Init BLE
    BLEDevice::init("");
    delay(150);  // Race condition fix — BLE controller needs time to settle

    wdBleScan = BLEDevice::getScan();
    if (!wdBleScan) {
        Serial.println("[WARDRIVING] BLE getScan() returned NULL — skipping BLE phase");
        BLEDevice::deinit(false);
        // Restart WiFi so wdRunScan() has STA mode ready
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
        return;
    }

    // Step 3: Configure passive scan (no SCAN_REQ sent — stealth)
    wdBleScan->setActiveScan(false);
    wdBleScan->setInterval(100);
    wdBleScan->setWindow(99);

    // Step 4: Run NON-BLOCKING scan with timeout
    // The blocking BLEScan::start() uses m_semaphoreScanEnd.wait() with NO timeout.
    // If the BLE GAP callback never fires (radio contention after WiFi↔BLE cycling),
    // it hangs forever — freezing the unit. Non-blocking + poll avoids this.
    wdBleScanDone = false;
    wdBleScan->start(WD_BLE_SCAN_SECONDS, wdBleScanCompleteCB, false);

    // Wait with timeout: scan duration + 3 seconds grace period
    // Check STOP button during wait so user can abort immediately
    unsigned long bleStart = millis();
    unsigned long bleTimeout = (WD_BLE_SCAN_SECONDS + 3) * 1000UL;
    while (!wdBleScanDone && (millis() - bleStart < bleTimeout)) {
        gpsUpdate();
        touchButtonsUpdate();
        if (isStartStopTapped()) {
            wdBleScan->stop();
            wdBleScan->clearResults();
            wdBleScan = nullptr;
            BLEDevice::deinit(false);
            WiFi.mode(WIFI_STA);
            WiFi.disconnect();
            delay(100);
            wdScanning = false;
            wdSessionStart = 0;
            drawStartStopButton(false);
            return;
        }
        delay(50);
    }
    if (!wdBleScanDone) {
        Serial.println("[WARDRIVING] BLE scan TIMEOUT — forcing stop");
        wdBleScan->stop();
    }

    BLEScanResults foundDevices = wdBleScan->getResults();

    // Step 5: Process results — copy into temp buffer first
    wdBleResultCount = 0;
    int total = foundDevices.getCount();
    int cap = (total > WD_BLE_RESULT_MAX) ? WD_BLE_RESULT_MAX : total;

    for (int i = 0; i < cap; i++) {
        BLEAdvertisedDevice dev = foundDevices.getDevice(i);
        WdBleResult* r = &wdBleResults[wdBleResultCount];

        // Copy MAC
        const uint8_t* addr = *dev.getAddress().getNative();
        memcpy(r->mac, addr, 6);

        // Copy RSSI
        r->rssi = dev.getRSSI();

        // Copy name
        if (dev.haveName() && dev.getName().length() > 0) {
            strncpy(r->name, dev.getName().c_str(), 16);
            r->name[16] = '\0';
        } else {
            r->name[0] = '\0';
        }

        // Copy manufacturer data (first 8 bytes max)
        if (dev.haveManufacturerData()) {
            std::string mfg = dev.getManufacturerData();
            r->mfgLen = (mfg.length() > 8) ? 8 : mfg.length();
            memcpy(r->mfgData, mfg.data(), r->mfgLen);
        } else {
            r->mfgLen = 0;
        }

        wdBleResultCount++;
    }

    // Step 6: Tear down BLE — MUST use deinit(false) due to library bug
    wdBleScan->stop();
    wdBleScan->clearResults();
    wdBleScan = nullptr;
    BLEDevice::deinit(false);

    // Step 7: Restart WiFi for next scan phase — working pattern (c7ee6ac).
    // WiFi must be back in STA mode before wdRunScan() is called.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Step 8: Feed GPS and log all BLE results to CSV
    gpsUpdate();

    for (int i = 0; i < wdBleResultCount; i++) {
        WdBleResult* r = &wdBleResults[i];
        wardrivingLogBleDevice(
            r->mac,
            r->name,
            r->rssi,
            r->mfgLen > 0 ? r->mfgData : NULL,
            r->mfgLen
        );
    }

    Serial.printf("[WARDRIVING] BLE scan done — %d devices found, %d unique logged\n",
                  wdBleResultCount, wdBleResultCount);
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
    wdLastScanErr = ESP_OK;
    wdScanPhase = WD_PHASE_WIFI;
    wdSessionStart = 0;
    wdBleScan = nullptr;
    wdBleResultCount = 0;

    // ── CRITICAL: Kill Serial FIRST — GPIO 1 (UART0 TX) is wired to GPS RX ──
    // Every Serial.println() sends 115200-baud data to the NEO-6M (9600 baud).
    // The GPS module interprets random bytes as UBX binary commands, including
    // cold restart — killing satellite tracking (6→3→0 sats, never recovers).
    // Serial.end() alone is NOT enough — UART0 TX stays routed to GPIO 1 via
    // IOMUX even after driver deletion. Must physically detach with pinMode(OUTPUT)
    // which calls gpio_hal_iomux_func_sel(PIN_FUNC_GPIO), then drive HIGH.
    Serial.end();
    delay(10);
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);

    // ── DO NOT TOUCH CC1101 PA PINS OR WiFi HERE — BOTH KILL GPS ──
    // Confirmed by isolation testing (2026-03-11, 10+ hour debug session):
    //   Test 1: No PA pins, no WiFi.mode       → SAT:5 held 74+ seconds
    //   Test 2: PA pins restored, no WiFi.mode  → SAT:0 instant death
    //   Test 3: No PA pins, WiFi.mode restored  → SAT:0 instant death
    // Both WiFi.mode() and pinMode() on GPIO 0/4 cause radio/power transients
    // that desensitize the NEO-6M.  WiFi init deferred to START button press.
    // PA pins stay in their current state (CC1101 dormant during wardriving).

    gpsSetup();           // Auto-scans pins/baud on first call, no-op if already initialized

    // Start GPS in background — opens UART2 on the found pin
    gpsStartBackground();

    // Let GPS UART settle and collect a few sentences
    for (int i = 0; i < 50; i++) {
        gpsUpdate();
        delay(10);
    }

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
                // Disconnect WiFi but do NOT call WiFi.mode(WIFI_OFF).
                // WiFi mode transitions cause radio/power transients that kill
                // GPS satellite tracking.  WiFi stays in STA mode (idle).
                wardrivingStop();
                wdScanning = false;
                wdSessionStart = 0;
                WiFi.disconnect();
                drawStartStopButton(false);
            } else {
                // Start
                if (wardrivingStart()) {
                    // Turn WiFi ON now — GPS had quiet RF time to acquire satellites
                    WiFi.mode(WIFI_STA);
                    WiFi.disconnect();
                    delay(200);

                    wdScanning = true;
                    wdScanCount = 0;
                    wdScanCycle = 0;
                    wdScanPhase = WD_PHASE_WIFI;
                    wdSessionStart = millis();
                    drawStartStopButton(true);
                    // Run first WiFi scan immediately (per-channel with STOP checks)
                    wdRunScan();
                    wdLastScan = millis();
                } else {
                    // SD card failed — flash error
                    tft.fillRect(10, WD_FILE_Y + SCALE_H(14), SCREEN_WIDTH - 20, 8, HALEHOUND_BLACK);
                    tft.setTextColor(HALEHOUND_HOTPINK);
                    tft.setCursor(10, WD_FILE_Y + SCALE_H(14));
                    tft.print("SD CARD ERROR!");
                }
            }
        }

        // ── Periodic scan — WiFi per-channel with STOP checks between channels ──
        // Each channel blocks ~200ms max, touch checked between channels.
        // BLE scans have their own internal STOP check in the poll loop.
        if (wdScanning && millis() - wdLastScan >= WD_SCAN_INTERVAL_MS) {
            if (wdScanCycle % WD_BLE_EVERY_N == (WD_BLE_EVERY_N - 1)) {
                wdRunBleScan();
            } else {
                wdRunScan();  // Returns false if user pressed STOP mid-scan
            }
            if (wdScanning) {  // Only advance if we didn't stop
                wdScanCycle++;
                wdLastScan = millis();
            }
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

    // Safety: make sure BLE is torn down if we exit during a BLE scan phase
    if (wdBleScan) {
        wdBleScan->stop();
        wdBleScan = nullptr;
    }
    BLEDevice::deinit(false);

    // Kill WiFi — MUST use Arduino API to keep _esp_wifi_started flag in sync
    // Raw esp_wifi_stop() desyncs the flag and silently breaks WiFi for all modules after
    WiFi.mode(WIFI_OFF);

    // Restore CC1101 PA module pins to their boot state
#if defined(CC1101_TX_EN) && defined(CC1101_RX_EN)
    if (cc1101_pa_module) {
        pinMode(CC1101_TX_EN, OUTPUT);
        digitalWrite(CC1101_TX_EN, LOW);
        pinMode(CC1101_RX_EN, OUTPUT);
        digitalWrite(CC1101_RX_EN, LOW);
        Serial.println("[WARDRIVING] PA module pins restored to OUTPUT LOW");
    }
#endif

    // Stop GPS background and restore Serial
    gpsStopBackground();
}
