// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD GPS Module Implementation
// GT-U7 (UBLOX 7) GPS Support with TinyGPSPlus
// Created: 2026-02-07
// Updated: 2026-02-14 — P1 connector, HardwareSerial, screen redesign
// ═══════════════════════════════════════════════════════════════════════════

#include "gps_module.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "icon.h"
#include <TinyGPSPlus.h>

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// GPS OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

static TinyGPSPlus gps;
static HardwareSerial gpsSerial(2);     // UART2 — pin determined by auto-scan
static GPSData currentData;
static bool gpsInitialized = false;
static unsigned long lastUpdateTime = 0;
static unsigned long lastDisplayUpdate = 0;
static int gpsActivePin = -1;           // Which GPIO ended up working
static int gpsActiveBaud = 9600;        // Which baud rate worked

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR
// ═══════════════════════════════════════════════════════════════════════════

static void drawGPSIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Check if back icon tapped (y=20-36, x=10-26) - MATCHES isInoBackTapped()
static bool isGPSBackTapped() {
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
// GPS SCREEN - REDESIGNED LAYOUT
//
// y=0-19:   Status bar
// y=20-36:  Icon bar (DARK bg, back icon)
// y=38-58:  Glitch title "GPS TRACKER"
// y=62-114: Coordinate frame (FreeFont lat/lon)
// y=120-148: Info grid (ALT/SPD/HDG/SAT)
// y=154:    Separator
// y=160-172: Date / Time
// y=178:    Separator
// y=184-212: Status box (color-coded)
// y=220-248: Diagnostics (NMEA stats + age)
// ═══════════════════════════════════════════════════════════════════════════

static void drawGPSScreen() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawGPSIconBar();

    // Glitch title - chromatic aberration effect
    drawGlitchText(55, "GPS TRACKER", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    // Coordinate frame
    tft.drawRoundRect(5, 62, 230, 52, 6, HALEHOUND_VIOLET);
    tft.drawRoundRect(6, 63, 228, 50, 5, HALEHOUND_GUNMETAL);

    // Info grid labels
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(8, 122);
    tft.print("ALT");
    tft.setCursor(125, 122);
    tft.print("SPD");
    tft.setCursor(8, 140);
    tft.print("HDG");
    tft.setCursor(125, 140);
    tft.print("SAT");

    // Separator
    tft.drawLine(5, 156, 235, 156, HALEHOUND_HOTPINK);

    // Date/Time labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(8, 164);
    tft.print("DATE");
    tft.setCursor(125, 164);
    tft.print("TIME");

    // Separator
    tft.drawLine(5, 180, 235, 180, HALEHOUND_HOTPINK);

    // Status box frame
    tft.drawRoundRect(5, 186, 230, 28, 4, HALEHOUND_VIOLET);

    // Diagnostic section labels
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(8, 222);
    tft.print("NMEA");
    tft.setCursor(8, 236);
    tft.print("PIN");
    tft.setCursor(8, 250);
    tft.print("AGE");
}

static void updateGPSValues() {
    char buf[48];

    // ── Coordinate frame (clear interior, redraw values) ──
    tft.fillRect(8, 65, 224, 46, TFT_BLACK);

    if (currentData.valid) {
        // Latitude — FreeFont inside frame
        snprintf(buf, sizeof(buf), "%.6f %c",
                 fabs(currentData.latitude),
                 currentData.latitude >= 0 ? 'N' : 'S');
        tft.setFreeFont(&FreeMono9pt7b);
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(12, 84);
        tft.print(buf);

        // Longitude — FreeFont inside frame
        snprintf(buf, sizeof(buf), "%.6f %c",
                 fabs(currentData.longitude),
                 currentData.longitude >= 0 ? 'E' : 'W');
        tft.setCursor(12, 104);
        tft.print(buf);
        tft.setFreeFont(NULL);
    } else {
        // No fix — centered waiting text
        tft.setFreeFont(&FreeMono9pt7b);
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(28, 92);
        tft.print("-- waiting --");
        tft.setFreeFont(NULL);
    }

    // ── Info grid values ──
    tft.setTextSize(1);

    // ALT
    tft.fillRect(30, 122, 88, 10, TFT_BLACK);
    tft.setTextColor(currentData.valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(30, 122);
    if (currentData.valid) {
        snprintf(buf, sizeof(buf), "%.1fm", currentData.altitude);
        tft.print(buf);
    } else {
        tft.print("---");
    }

    // SPD
    tft.fillRect(150, 122, 85, 10, TFT_BLACK);
    tft.setTextColor(currentData.valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(150, 122);
    if (currentData.valid) {
        snprintf(buf, sizeof(buf), "%.1f km/h", currentData.speed);
        tft.print(buf);
    } else {
        tft.print("---");
    }

    // HDG
    tft.fillRect(30, 140, 88, 10, TFT_BLACK);
    tft.setTextColor(currentData.valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(30, 140);
    if (currentData.valid) {
        snprintf(buf, sizeof(buf), "%.1f deg", currentData.course);
        tft.print(buf);
    } else {
        tft.print("---");
    }

    // SAT
    tft.fillRect(150, 140, 85, 10, TFT_BLACK);
    tft.setTextColor(currentData.satellites > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(150, 140);
    snprintf(buf, sizeof(buf), "%d", currentData.satellites);
    tft.print(buf);

    // ── Date / Time ──
    tft.fillRect(34, 164, 88, 10, TFT_BLACK);
    tft.fillRect(155, 164, 80, 10, TFT_BLACK);

    if (currentData.valid && currentData.year > 2000) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 currentData.year, currentData.month, currentData.day);
        tft.setCursor(34, 164);
        tft.print(buf);

        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 currentData.hour, currentData.minute, currentData.second);
        tft.setCursor(155, 164);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(34, 164);
        tft.print("----/--/--");
        tft.setCursor(155, 164);
        tft.print("--:--:--");
    }

    // ── Status box (color-coded) ──
    uint32_t chars = gps.charsProcessed();

    tft.fillRoundRect(6, 187, 228, 26, 3, HALEHOUND_DARK);
    tft.setTextSize(1);

    if (chars == 0) {
        // RED — no data from GPS module at all
        drawCenteredText(196, "NO DATA - Check wiring", 0xF800, 1);
    } else if (!currentData.valid) {
        if (currentData.satellites > 0) {
            // HOTPINK — seeing satellites but no fix yet
            snprintf(buf, sizeof(buf), "SEARCHING  %d sats", currentData.satellites);
            drawCenteredText(196, buf, HALEHOUND_HOTPINK, 1);
        } else {
            // VIOLET — getting NMEA but no satellites
            drawCenteredText(196, "NO FIX - Need sky view", HALEHOUND_VIOLET, 1);
        }
    } else {
        if (currentData.satellites >= 4) {
            // GREEN — full 3D fix
            snprintf(buf, sizeof(buf), "3D FIX  %d sats  LOCKED", currentData.satellites);
            drawCenteredText(196, buf, 0x07E0, 1);
        } else {
            // BRIGHT — 2D fix (no altitude)
            snprintf(buf, sizeof(buf), "2D FIX  %d sats", currentData.satellites);
            drawCenteredText(196, buf, HALEHOUND_BRIGHT, 1);
        }
    }

    // ── Diagnostics (NMEA y=222, PIN y=236, AGE y=250) ──
    tft.fillRect(35, 222, 200, 10, TFT_BLACK);
    tft.fillRect(30, 236, 200, 10, TFT_BLACK);
    tft.fillRect(30, 250, 200, 10, TFT_BLACK);

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setTextSize(1);

    // NMEA stats
    snprintf(buf, sizeof(buf), "%lu chars  %lu ok  %lu fail",
             (unsigned long)gps.charsProcessed(),
             (unsigned long)gps.sentencesWithFix(),
             (unsigned long)gps.failedChecksum());
    tft.setCursor(35, 222);
    tft.print(buf);

    // Active pin/baud
    if (gpsActivePin >= 0) {
        snprintf(buf, sizeof(buf), "GPIO%d @ %d", gpsActivePin, gpsActiveBaud);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    tft.setCursor(30, 236);
    tft.print(buf);

    // Fix age
    if (currentData.valid) {
        snprintf(buf, sizeof(buf), "%lums", (unsigned long)currentData.age);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    tft.setCursor(30, 250);
    tft.print(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

// Try a specific pin/baud combo, return chars received in timeoutMs
static uint32_t tryGPSPin(int pin, int baud, int timeoutMs) {
    gpsSerial.end();
    delay(50);
    gpsSerial.begin(baud, SERIAL_8N1, pin, -1);
    delay(50);

    // Drain any garbage
    while (gpsSerial.available()) gpsSerial.read();

    uint32_t charsBefore = gps.charsProcessed();
    unsigned long start = millis();

    while (millis() - start < (unsigned long)timeoutMs) {
        while (gpsSerial.available() > 0) {
            gps.encode(gpsSerial.read());
        }
        delay(5);
    }

    return gps.charsProcessed() - charsBefore;
}

void gpsSetup() {
    if (gpsInitialized) return;

    memset(&currentData, 0, sizeof(currentData));
    currentData.valid = false;

    // ── Auto-scan: try multiple pins and baud rates ──
    // Show scanning screen
    tft.fillRect(0, 60, SCREEN_WIDTH, 200, TFT_BLACK);
    drawGlitchText(55, "GPS TRACKER", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    drawCenteredText(80, "SCANNING GPS...", HALEHOUND_HOTPINK, 2);

    // Pin/baud combos to try — GPIO3 (P1 connector) first
    struct ScanEntry { int pin; int baud; const char* label; };
    ScanEntry scans[] = {
        { 3,  9600,  "P1 RX (GPIO3) @ 9600"   },
        { 3,  38400, "P1 RX (GPIO3) @ 38400"  },
        { 26, 9600,  "GPIO26 (spk) @ 9600"    },
        { 26, 38400, "GPIO26 (spk) @ 38400"   },
        { 1,  9600,  "P1 TX (GPIO1) @ 9600"   },
    };
    int numScans = 5;

    gpsActivePin = -1;
    gpsActiveBaud = 9600;

    for (int i = 0; i < numScans; i++) {
        // Show current attempt
        tft.fillRect(0, 110, SCREEN_WIDTH, 60, TFT_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 115);
        tft.printf("Try %d/%d: %s", i + 1, numScans, scans[i].label);

        // Progress bar
        int barW = (SCREEN_WIDTH - 20) * (i + 1) / numScans;
        tft.fillRect(10, 135, SCREEN_WIDTH - 20, 8, HALEHOUND_DARK);
        tft.fillRect(10, 135, barW, 8, HALEHOUND_HOTPINK);

        uint32_t chars = tryGPSPin(scans[i].pin, scans[i].baud, 2500);

        // Show result for this attempt
        tft.setCursor(10, 150);
        if (chars > 10) {
            tft.setTextColor(0x07E0);  // Green
            tft.printf("FOUND! %lu chars", (unsigned long)chars);

            gpsActivePin = scans[i].pin;
            gpsActiveBaud = scans[i].baud;

            delay(1000);
            break;
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.printf("No data (%lu chars)", (unsigned long)chars);
        }
    }

    // Show final result
    tft.fillRect(0, 170, SCREEN_WIDTH, 40, TFT_BLACK);
    if (gpsActivePin >= 0) {
        char resultBuf[40];
        snprintf(resultBuf, sizeof(resultBuf), "LOCKED: GPIO%d @ %d", gpsActivePin, gpsActiveBaud);
        drawCenteredText(180, resultBuf, 0x07E0, 1);
    } else {
        drawCenteredText(175, "NO GPS FOUND", 0xF800, 2);
        drawCenteredText(200, "Check wiring & power", HALEHOUND_GUNMETAL, 1);
        // Default to GPS_RX_PIN so screen still shows diagnostics
        gpsSerial.end();
        gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);
        gpsActivePin = GPS_RX_PIN;
        gpsActiveBaud = GPS_BAUD;
    }

    delay(1500);
    gpsInitialized = true;
}

void gpsUpdate() {
    // Read all available GPS data from UART2
    while (gpsSerial.available() > 0) {
        char c = gpsSerial.read();
        gps.encode(c);
    }

    // Update data structure
    if (gps.location.isUpdated()) {
        currentData.valid = gps.location.isValid();
        currentData.latitude = gps.location.lat();
        currentData.longitude = gps.location.lng();
        currentData.age = gps.location.age();
        lastUpdateTime = millis();
    }

    if (gps.altitude.isUpdated()) {
        currentData.altitude = gps.altitude.meters();
    }

    if (gps.speed.isUpdated()) {
        currentData.speed = gps.speed.kmph();
    }

    if (gps.course.isUpdated()) {
        currentData.course = gps.course.deg();
    }

    if (gps.satellites.isUpdated()) {
        currentData.satellites = gps.satellites.value();
    }

    if (gps.date.isUpdated()) {
        currentData.year = gps.date.year();
        currentData.month = gps.date.month();
        currentData.day = gps.date.day();
    }

    if (gps.time.isUpdated()) {
        currentData.hour = gps.time.hour();
        currentData.minute = gps.time.minute();
        currentData.second = gps.time.second();
    }

    // Mark as invalid if data is stale
    if (millis() - lastUpdateTime > GPS_TIMEOUT_MS) {
        currentData.valid = false;
    }

    // Periodic debug output to serial monitor
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 5000) {
        Serial.printf("[GPS] Chars:%lu  Fix:%lu  Fail:%lu  Sats:%d  Valid:%d\n",
                      (unsigned long)gps.charsProcessed(),
                      (unsigned long)gps.sentencesWithFix(),
                      (unsigned long)gps.failedChecksum(),
                      currentData.satellites,
                      currentData.valid ? 1 : 0);
        lastDebug = millis();
    }
}

void gpsScreen() {
    // Release UART0 so UART2 can claim GPIO pins without matrix conflict
    Serial.end();
    delay(50);

    // Initialize GPS if needed
    if (!gpsInitialized) {
        gpsSetup();
    } else {
        // Re-entry: restart UART2 on the pin found during scan
        gpsSerial.begin(gpsActiveBaud, SERIAL_8N1, gpsActivePin, -1);
    }

    // Draw initial screen
    drawGPSScreen();
    updateGPSValues();

    // Main loop
    bool exitRequested = false;
    lastDisplayUpdate = millis();

    while (!exitRequested) {
        // Update GPS data
        gpsUpdate();

        // Update display periodically
        if (millis() - lastDisplayUpdate >= GPS_UPDATE_INTERVAL_MS) {
            updateGPSValues();
            lastDisplayUpdate = millis();
        }

        // Handle touch input
        touchButtonsUpdate();

        // Check for back button tap
        if (isGPSBackTapped()) {
            exitRequested = true;
        }

        // Check hardware buttons
        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }

        delay(10);
    }

    // Restore debug serial
    gpsSerial.end();
    delay(50);
    Serial.begin(115200);
}

bool gpsHasFix() {
    return currentData.valid;
}

GPSData gpsGetData() {
    return currentData;
}

String gpsGetLocationString() {
    if (!currentData.valid) {
        return "0.000000,0.000000";
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.6f,%.6f",
             currentData.latitude, currentData.longitude);
    return String(buffer);
}

String gpsGetTimestamp() {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             currentData.year, currentData.month, currentData.day,
             currentData.hour, currentData.minute, currentData.second);
    return String(buffer);
}

bool gpsIsFresh() {
    return (millis() - lastUpdateTime) < GPS_TIMEOUT_MS;
}

GPSStatus gpsGetStatus() {
    if (!gpsInitialized || gps.charsProcessed() < 10) {
        return GPS_NO_MODULE;
    }
    if (!gps.location.isValid()) {
        return GPS_SEARCHING;
    }
    if (gps.altitude.isValid()) {
        return GPS_FIX_3D;
    }
    return GPS_FIX_2D;
}

uint8_t gpsGetSatellites() {
    return currentData.satellites;
}
