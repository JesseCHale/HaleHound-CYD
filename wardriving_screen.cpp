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

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define WD_SCAN_INTERVAL_MS    2500    // WiFi scan every 2.5 seconds
#define WD_DISPLAY_INTERVAL_MS  500    // Update display every 500ms
#define WD_BLINK_INTERVAL_MS    400    // Record indicator blink rate
#define WD_BLE_SCAN_SECONDS       3    // BLE passive scan duration

// Scan phase — alternates between WiFi and BLE each cycle
#define WD_PHASE_WIFI  0
#define WD_PHASE_BLE   1

// Layout constants
#define WD_FRAME_X       5
#define WD_FRAME_Y       62
#define WD_FRAME_W       230
#define WD_FRAME_H       52
#define WD_STATS_Y       122
#define WD_GPS_Y         158
#define WD_SPEED_Y       184
#define WD_FILE_Y        200
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
static esp_err_t wdLastScanErr = ESP_OK;  // Track scan errors for TFT display
static uint8_t wdScanPhase = WD_PHASE_WIFI;
static unsigned long wdSessionStart = 0;   // millis() when session started

// BLE scan state — volatile callback queue (same pattern as BleSniffer)
static BLEScan* wdBleScan = nullptr;

// Temp buffer for BLE results — process after scan completes
#define WD_BLE_RESULT_MAX 32
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

    // Row 1: NETWORKS / OPEN
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_STATS_Y);
    tft.print("NETWORKS");
    tft.setCursor(125, WD_STATS_Y);
    tft.print("OPEN");

    // Row 2: BLE / DUPES
    tft.setCursor(10, WD_STATS_Y + 10);
    tft.print("BLE");
    tft.setCursor(125, WD_STATS_Y + 10);
    tft.print("DUPES");

    // Row 3: SCANS / STATUS
    tft.setCursor(10, WD_STATS_Y + 20);
    tft.print("SCANS");
    tft.setCursor(125, WD_STATS_Y + 20);
    tft.print("STATUS");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_STATS_Y + 33, WD_FRAME_X + WD_FRAME_W, WD_STATS_Y + 33, HALEHOUND_HOTPINK);

    // GPS section labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_GPS_Y);
    tft.print("GPS");
    tft.setCursor(125, WD_GPS_Y);
    tft.print("SATS");
    tft.setCursor(10, WD_GPS_Y + 12);
    tft.print("LAT");
    tft.setCursor(125, WD_GPS_Y + 12);
    tft.print("LON");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_GPS_Y + 24, WD_FRAME_X + WD_FRAME_W, WD_GPS_Y + 24, HALEHOUND_HOTPINK);

    // Speed / Time labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, WD_SPEED_Y);
    tft.print("SPEED");
    tft.setCursor(125, WD_SPEED_Y);
    tft.print("TIME");

    // Separator
    tft.drawLine(WD_FRAME_X, WD_SPEED_Y + 12, WD_FRAME_X + WD_FRAME_W, WD_SPEED_Y + 12, HALEHOUND_HOTPINK);

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

    // ── Row 1: NETWORKS value / OPEN value ──
    tft.setTextSize(1);

    // NETWORKS value
    tft.fillRect(65, WD_STATS_Y, 55, 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(65, WD_STATS_Y);
    tft.print(stats.newNetworks);

    // OPEN value
    tft.fillRect(155, WD_STATS_Y, 75, 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? (stats.openNetworks > 0 ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA) : HALEHOUND_GUNMETAL);
    tft.setCursor(155, WD_STATS_Y);
    tft.print(stats.openNetworks);

    // ── Row 2: BLE value / DUPES value ──

    // BLE value
    tft.fillRect(30, WD_STATS_Y + 10, 85, 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.newBleDevices > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(30, WD_STATS_Y + 10);
    tft.print(stats.newBleDevices);

    // DUPES value (WiFi + BLE combined)
    tft.fillRect(165, WD_STATS_Y + 10, 65, 8, HALEHOUND_BLACK);
    tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(165, WD_STATS_Y + 10);
    tft.print(stats.duplicates + stats.bleDuplicates);

    // ── Row 3: SCANS value / STATUS value ──

    // SCANS value
    tft.fillRect(50, WD_STATS_Y + 20, 65, 8, HALEHOUND_BLACK);
    tft.setCursor(50, WD_STATS_Y + 20);
    if (wdLastScanErr != ESP_OK) {
        // Show error code in red so it's visible on TFT
        tft.setTextColor(0xF800);
        tft.printf("E:0x%X", wdLastScanErr);
    } else {
        tft.setTextColor(stats.active ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
        tft.print(wdScanCount);
    }

    // STATUS value — shows current scan phase
    tft.fillRect(170, WD_STATS_Y + 20, 65, 8, HALEHOUND_BLACK);
    tft.setCursor(170, WD_STATS_Y + 20);
    if (stats.active) {
        if (wdBlinkState) {
            tft.setTextColor(HALEHOUND_HOTPINK);
            if (wdScanPhase == WD_PHASE_BLE) {
                tft.print("BLE");
            } else {
                tft.print("WIFI");
            }
            tft.fillCircle(205, WD_STATS_Y + 24, 3, HALEHOUND_HOTPINK);
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            if (wdScanPhase == WD_PHASE_BLE) {
                tft.print("BLE");
            } else {
                tft.print("WIFI");
            }
            tft.fillCircle(205, WD_STATS_Y + 24, 3, HALEHOUND_GUNMETAL);
        }
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("IDLE");
    }

    // ── GPS values ──

    // GPS fix status
    tft.fillRect(30, WD_GPS_Y, 85, 8, HALEHOUND_BLACK);
    tft.setCursor(30, WD_GPS_Y);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.print("FIX OK");
    } else {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("NO FIX");
    }

    // SATS value
    tft.fillRect(155, WD_GPS_Y, 50, 8, HALEHOUND_BLACK);
    tft.setTextColor(gpsData.satellites > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(155, WD_GPS_Y);
    tft.print(gpsData.satellites);

    // LAT value
    tft.fillRect(30, WD_GPS_Y + 12, 90, 8, HALEHOUND_BLACK);
    tft.setCursor(30, WD_GPS_Y + 12);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.latitude);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // LON value
    tft.fillRect(150, WD_GPS_Y + 12, 85, 8, HALEHOUND_BLACK);
    tft.setCursor(150, WD_GPS_Y + 12);
    if (gpsData.valid) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.4f", gpsData.longitude);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // ── Speed / Time values ──

    // SPEED value (km/h from GPS)
    tft.fillRect(48, WD_SPEED_Y, 70, 8, HALEHOUND_BLACK);
    tft.setCursor(48, WD_SPEED_Y);
    if (gpsData.valid && gpsData.speed >= 0.0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%.1f km/h", gpsData.speed);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("---");
    }

    // TIME value (session elapsed)
    tft.fillRect(155, WD_SPEED_Y, 80, 8, HALEHOUND_BLACK);
    tft.setCursor(155, WD_SPEED_Y);
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
    tft.fillRect(10, WD_FILE_Y + 14, 220, 8, HALEHOUND_BLACK);
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
    // MUST use Arduino WiFi.scanNetworks() — NOT raw esp_wifi_scan_start().
    // Arduino's internal SCAN_DONE event handler (_scanDone in WiFiScan.cpp:114)
    // calls esp_wifi_scan_get_ap_records() which CONSUMES the ESP-IDF scan buffer.
    // If we use raw esp_wifi_scan_start(), by the time we call esp_wifi_scan_get_ap_num()
    // the buffer is already empty — Arduino ate the results. WiFi.scanNetworks() stores
    // the results internally so we can read them through WiFi.SSID(i) etc.
    int n = WiFi.scanNetworks(false, true);  // blocking, show hidden

    if (n == WIFI_SCAN_FAILED) {
        wdLastScanErr = ESP_FAIL;
        wdScanCount++;
        return;
    }

    if (n == WIFI_SCAN_RUNNING) {
        // Shouldn't happen in blocking mode, but handle gracefully
        wdScanCount++;
        return;
    }

    wdLastScanErr = ESP_OK;

    if (n == 0) {
        WiFi.scanDelete();
        wdScanCount++;
        return;
    }

    // Feed GPS before logging so coordinates are fresh
    gpsUpdate();

    // Log each network through wardriving backend
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

    // Free Arduino's internal scan result buffer
    WiFi.scanDelete();
    wdScanCount++;
}

// ═══════════════════════════════════════════════════════════════════════════
// BLE SCAN FOR WARDRIVING
// WiFi and BLE share the ESP32 radio — cannot run both at once.
// Pattern: tear down WiFi → init BLE → passive scan → tear down BLE → restart WiFi
// Based on proven BleSniffer pattern from bluetooth_attacks.cpp
// ═══════════════════════════════════════════════════════════════════════════

static void wdRunBleScan() {
    Serial.println("[WARDRIVING] BLE scan phase starting...");

    // Step 1: Tear down WiFi to free the radio
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Step 2: Init BLE
    BLEDevice::init("");
    delay(150);  // Race condition fix — BLE controller needs time to settle

    wdBleScan = BLEDevice::getScan();
    if (!wdBleScan) {
        Serial.println("[WARDRIVING] BLE getScan() returned NULL — skipping BLE phase");
        BLEDevice::deinit(false);
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        delay(100);
        return;
    }

    // Step 3: Configure passive scan (no SCAN_REQ sent — stealth)
    wdBleScan->setActiveScan(false);
    wdBleScan->setInterval(100);
    wdBleScan->setWindow(99);

    // Step 4: Run blocking scan for WD_BLE_SCAN_SECONDS
    BLEScanResults foundDevices = wdBleScan->start(WD_BLE_SCAN_SECONDS, false);

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

    // Step 7: Restart WiFi for next WiFi scan phase
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

    // Force clean WiFi state — previous module may have used raw esp_wifi_stop()
    // which desyncs Arduino's _esp_wifi_started flag. WiFi.mode(WIFI_OFF) resets it.
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Init WiFi in STA mode for scanning
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(200);

    // Initialize GPS — same sequence as gpsScreen() so it works on first entry
    // Serial.end() frees GPIO 3 so UART2 can claim it without pin matrix conflict
    Serial.end();
    delay(50);
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
                // Stop
                wardrivingStop();
                wdScanning = false;
                wdSessionStart = 0;
                drawStartStopButton(false);
            } else {
                // Start
                if (wardrivingStart()) {
                    wdScanning = true;
                    wdScanCount = 0;
                    wdScanPhase = WD_PHASE_WIFI;
                    wdSessionStart = millis();
                    drawStartStopButton(true);
                    // Run first WiFi scan immediately
                    wdRunScan();
                    wdLastScan = millis();
                } else {
                    // SD card failed — flash error
                    tft.fillRect(10, WD_FILE_Y + 14, 220, 8, HALEHOUND_BLACK);
                    tft.setTextColor(HALEHOUND_HOTPINK);
                    tft.setCursor(10, WD_FILE_Y + 14);
                    tft.print("SD CARD ERROR!");
                }
            }
        }

        // Periodic scan — alternating WiFi / BLE phases
        if (wdScanning && millis() - wdLastScan >= WD_SCAN_INTERVAL_MS) {
            if (wdScanPhase == WD_PHASE_WIFI) {
                wdRunScan();
                wdScanPhase = WD_PHASE_BLE;
            } else {
                wdRunBleScan();
                wdScanPhase = WD_PHASE_WIFI;
            }
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

    // Safety: make sure BLE is torn down if we exit during a BLE scan phase
    if (wdBleScan) {
        wdBleScan->stop();
        wdBleScan = nullptr;
    }
    BLEDevice::deinit(false);

    // Kill WiFi — MUST use Arduino API to keep _esp_wifi_started flag in sync
    // Raw esp_wifi_stop() desyncs the flag and silently breaks WiFi for all modules after
    WiFi.mode(WIFI_OFF);

    // Stop GPS background and restore Serial
    gpsStopBackground();
}
