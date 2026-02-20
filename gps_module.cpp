// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD GPS Module Implementation
// GT-U7 (UBLOX 7) GPS Support with TinyGPSPlus
// Created: 2026-02-07
// Updated: 2026-02-19 — Tactical instrument panel (compass, speed arc,
//                        sat bars, crosshairs, HDOP, pulsing fix dot)
// ═══════════════════════════════════════════════════════════════════════════

#include "gps_module.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "icon.h"
#include <TinyGPSPlus.h>

#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295f
#endif

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
static unsigned long lastPulseUpdate = 0;
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
// COMPASS DIRECTION HELPER
// ═══════════════════════════════════════════════════════════════════════════

static const char* compassDirection(float heading) {
    if (heading >= 337.5f || heading < 22.5f)  return "N";
    if (heading < 67.5f)  return "NE";
    if (heading < 112.5f) return "E";
    if (heading < 157.5f) return "SE";
    if (heading < 202.5f) return "S";
    if (heading < 247.5f) return "SW";
    if (heading < 292.5f) return "W";
    return "NW";
}

// ═══════════════════════════════════════════════════════════════════════════
// INSTRUMENT: TACTICAL CROSSHAIRS (coordinate frame overlay)
//
// Corner brackets + center cross inside the coordinate frame.
// Drawn each update after the interior is cleared, before text.
// ═══════════════════════════════════════════════════════════════════════════

static void drawCrosshairs() {
    const int x1 = 10, y1 = 66;      // top-left interior
    const int x2 = 230, y2 = 110;    // bottom-right interior
    const int len = 15;               // bracket arm length
    uint16_t color = HALEHOUND_GUNMETAL;

    // Top-left bracket
    tft.drawLine(x1, y1, x1 + len, y1, color);
    tft.drawLine(x1, y1, x1, y1 + len, color);

    // Top-right bracket
    tft.drawLine(x2, y1, x2 - len, y1, color);
    tft.drawLine(x2, y1, x2, y1 + len, color);

    // Bottom-left bracket
    tft.drawLine(x1, y2, x1 + len, y2, color);
    tft.drawLine(x1, y2, x1, y2 - len, color);

    // Bottom-right bracket
    tft.drawLine(x2, y2, x2 - len, y2, color);
    tft.drawLine(x2, y2, x2, y2 - len, color);

    // Center cross (small)
    int cx = 120, cy = 88;
    tft.drawLine(cx - 4, cy, cx + 4, cy, color);
    tft.drawLine(cx, cy - 4, cx, cy + 4, color);
}

// ═══════════════════════════════════════════════════════════════════════════
// INSTRUMENT: COMPASS ROSE
//
// Spinning compass with heading needle, 8 tick marks (N/NE/E/SE/S/SW/W/NW),
// double rim, center dot, and heading + direction text below.
// ═══════════════════════════════════════════════════════════════════════════

static void drawCompass(float heading, bool valid) {
    const int cx = 40;
    const int cy = 146;
    const int r = 22;

    // Clear compass area
    tft.fillRect(4, 118, 74, 64, TFT_BLACK);

    uint16_t rimColor = valid ? HALEHOUND_VIOLET : HALEHOUND_GUNMETAL;
    uint16_t needleColor = valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;

    // Double rim
    tft.drawCircle(cx, cy, r, rimColor);
    tft.drawCircle(cx, cy, r + 1, HALEHOUND_GUNMETAL);

    // 8 tick marks at compass points (N=0, NE=45, E=90, etc.)
    // Uses heading convention: 0=N(up), clockwise
    // Screen: x += sin(angle), y -= cos(angle)
    for (int i = 0; i < 8; i++) {
        float a = i * 45.0f * DEG_TO_RAD;
        int tickLen = (i % 2 == 0) ? 5 : 3;  // cardinal=longer, intercardinal=shorter
        uint16_t tickColor = (i == 0) ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;

        int ox = cx + (int)(sin(a) * (float)r);
        int oy = cy - (int)(cos(a) * (float)r);
        int ix = cx + (int)(sin(a) * (float)(r - tickLen));
        int iy = cy - (int)(cos(a) * (float)(r - tickLen));

        tft.drawLine(ix, iy, ox, oy, tickColor);
    }

    // "N" label above compass
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(cx - 3, cy - r - 10);
    tft.print("N");

    // Heading needle (thick — 3 parallel lines)
    float rad = heading * DEG_TO_RAD;
    int tipX = cx + (int)(sin(rad) * (float)(r - 5));
    int tipY = cy - (int)(cos(rad) * (float)(r - 5));

    tft.drawLine(cx, cy, tipX, tipY, needleColor);
    tft.drawLine(cx + 1, cy, tipX + 1, tipY, needleColor);
    tft.drawLine(cx - 1, cy, tipX - 1, tipY, needleColor);

    // Tail (shorter, opposite direction)
    int tailX = cx - (int)(sin(rad) * (float)(r / 3));
    int tailY = cy + (int)(cos(rad) * (float)(r / 3));
    tft.drawLine(cx, cy, tailX, tailY, HALEHOUND_GUNMETAL);

    // Center dot (ring style)
    tft.fillCircle(cx, cy, 3, HALEHOUND_HOTPINK);
    tft.fillCircle(cx, cy, 1, HALEHOUND_DARK);

    // Heading text + compass direction below
    char buf[12];
    if (valid) {
        snprintf(buf, sizeof(buf), "%.0f %s", heading, compassDirection(heading));
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    tft.setTextColor(valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setTextSize(1);
    int tw = strlen(buf) * 6;
    tft.setCursor(cx - tw / 2, 176);
    tft.print(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// INSTRUMENT: SPEED ARC GAUGE
//
// 270-degree arc (gap at bottom). Fills left-to-right with color gradient:
// magenta → hotpink → red. Speed value displayed inside the arc.
// Max speed: 120 km/h.
// ═══════════════════════════════════════════════════════════════════════════

static void drawSpeedArc(float speed, bool valid) {
    const int cx = 120;
    const int cy = 152;
    const int outerR = 22;
    const int innerR = 16;
    const float maxSpeed = 120.0f;
    const int totalSweep = 270;

    // Clear speed area
    tft.fillRect(82, 118, 76, 64, TFT_BLACK);

    int fillSteps = 0;
    if (valid && speed > 0.5f) {
        fillSteps = (int)((speed / maxSpeed) * (float)totalSweep);
        if (fillSteps > totalSweep) fillSteps = totalSweep;
    }

    // Draw arc: sweep 270 degrees
    // Step 0 = 225° math (lower-left), step 270 = -45° math (lower-right)
    // Goes counterclockwise through top (standard speedometer sweep)
    for (int step = 0; step <= totalSweep; step += 3) {
        float angleDeg = 225.0f - (float)step;
        float rad = angleDeg * DEG_TO_RAD;

        int ix = cx + (int)(cos(rad) * (float)innerR);
        int iy = cy - (int)(sin(rad) * (float)innerR);
        int ox = cx + (int)(cos(rad) * (float)outerR);
        int oy = cy - (int)(sin(rad) * (float)outerR);

        uint16_t color;
        if (step <= fillSteps && valid) {
            // Color gradient based on position in arc
            float frac = (float)step / (float)totalSweep;
            if (frac < 0.5f)       color = HALEHOUND_MAGENTA;
            else if (frac < 0.75f) color = HALEHOUND_HOTPINK;
            else                   color = 0xF800;  // Red at high speed
        } else {
            color = HALEHOUND_DARK;  // Unfilled background
        }

        tft.drawLine(ix, iy, ox, oy, color);
    }

    // Speed value inside arc center
    char buf[10];
    if (valid) {
        if (speed < 10.0f) snprintf(buf, sizeof(buf), "%.1f", speed);
        else               snprintf(buf, sizeof(buf), "%.0f", speed);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    tft.setTextColor(valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setTextSize(1);
    int tw = strlen(buf) * 6;
    tft.setCursor(cx - tw / 2, cy - 3);
    tft.print(buf);

    // "km/h" label below arc
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(cx - 12, 176);
    tft.print("km/h");
}

// ═══════════════════════════════════════════════════════════════════════════
// 16-BIT COLOR INTERPOLATION (565 format)
// ═══════════════════════════════════════════════════════════════════════════

static uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t) {
    int r1 = (c1 >> 11) & 0x1F;
    int g1 = (c1 >> 5)  & 0x3F;
    int b1 =  c1        & 0x1F;
    int r2 = (c2 >> 11) & 0x1F;
    int g2 = (c2 >> 5)  & 0x3F;
    int b2 =  c2        & 0x1F;
    int r = r1 + (int)((float)(r2 - r1) * t);
    int g = g1 + (int)((float)(g2 - g1) * t);
    int b = b1 + (int)((float)(b2 - b1) * t);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// ═══════════════════════════════════════════════════════════════════════════
// INSTRUMENT: SATELLITE SIGNAL BARS
//
// 5 increasing-height bars (cell signal style). Each bar has a vertical
// gradient from HALEHOUND_DARK (bottom) to its target HaleHound color
// (top). Gradient per bar: VIOLET → VIOLET → MAGENTA → HOTPINK → BRIGHT.
// ═══════════════════════════════════════════════════════════════════════════

static void drawSatBars(int satellites) {
    const int barW = 10;
    const int gap = 3;
    const int startX = 168;
    const int bottomY = 168;
    const int barHeights[] = {8, 14, 20, 26, 32};
    const int thresholds[] = {1, 3, 5, 7, 9};

    // HaleHound gradient target — each bar fades from DARK to this color
    uint16_t barColors[] = {
        HALEHOUND_VIOLET,
        HALEHOUND_VIOLET,
        HALEHOUND_MAGENTA,
        HALEHOUND_HOTPINK,
        HALEHOUND_BRIGHT
    };

    // Clear satellite area
    tft.fillRect(162, 118, 74, 64, TFT_BLACK);

    // "SAT" label at top
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(188, 120);
    tft.print("SAT");

    for (int i = 0; i < 5; i++) {
        int x = startX + i * (barW + gap);
        int h = barHeights[i];
        int y = bottomY - h;

        if (satellites >= thresholds[i]) {
            // Gradient fill: dark at bottom, bright at top
            for (int row = 0; row < h; row++) {
                // row 0 = top (bright), row h-1 = bottom (dark)
                float t = 1.0f - (float)row / (float)(h > 1 ? h - 1 : 1);
                uint16_t color = lerpColor565(HALEHOUND_DARK, barColors[i], t);
                tft.fillRect(x, y + row, barW, 1, color);
            }
        } else {
            tft.drawRect(x, y, barW, h, HALEHOUND_GUNMETAL);
        }
    }

    // Satellite count below bars
    char buf[6];
    snprintf(buf, sizeof(buf), "%d", satellites);
    int tw = strlen(buf) * 6;
    tft.setTextColor(satellites > 0 ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(startX + 25 - tw / 2, 176);
    tft.print(buf);
}

// ═══════════════════════════════════════════════════════════════════════════
// INSTRUMENT: PULSING FIX INDICATOR
//
// Breathing circle that oscillates in size. Updates at 150ms intervals
// for smooth animation (faster than the 1-second main display update).
//
// Green  = 3D fix locked
// Magenta = searching / 2D fix
// Red    = no GPS data (static, no pulse)
// ═══════════════════════════════════════════════════════════════════════════

static void drawSkullIndicator(bool hasFix, bool hasData) {
    const int sx = 214;     // Right side of status box (skull top-left x)
    const int sy = 226;     // Skull top-left y (centered in 28px box)

    // Clear skull area (use HALEHOUND_DARK to match status box interior)
    tft.fillRect(sx - 1, sy - 1, 18, 18, HALEHOUND_DARK);

    if (!hasData) {
        // Dim ghost skull — no GPS data at all
        tft.drawBitmap(sx, sy, bitmap_icon_skull_tools, 16, 16, HALEHOUND_GUNMETAL);
        return;
    }

    if (hasFix) {
        // Solid skull — LOCKED ON (steady, confident, no blinking)
        tft.drawBitmap(sx, sy, bitmap_icon_skull_tools, 16, 16, HALEHOUND_MAGENTA);
    } else {
        // Pulsing skull — searching (breathes between HOTPINK and DARK)
        bool pulseOn = (millis() / 300) % 2;
        uint16_t skullColor = pulseOn ? HALEHOUND_HOTPINK : HALEHOUND_DARK;
        tft.drawBitmap(sx, sy, bitmap_icon_skull_tools, 16, 16, skullColor);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// GPS SCREEN — TACTICAL INSTRUMENT LAYOUT
//
// y=0-19:    Status bar
// y=20-36:   Icon bar (DARK bg, back icon)
// y=38-58:   Glitch title "GPS TRACKER"
// y=62-114:  Coordinate frame with TACTICAL CROSSHAIRS
// y=118-182: INSTRUMENT PANEL (compass | speed arc | sat bars)
// y=186-196: ALT + HDOP info row
// y=198:     Separator
// y=202-212: Date + Time
// y=214:     Separator
// y=218-246: Status box + PULSING FIX DOT
// y=250-278: Diagnostics (NMEA / PIN / AGE)
// ═══════════════════════════════════════════════════════════════════════════

static void drawGPSScreen() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawGPSIconBar();

    // Glitch title — chromatic aberration effect
    drawGlitchText(55, "GPS TRACKER", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    // Coordinate frame (double border)
    tft.drawRoundRect(5, 62, 230, 52, 6, HALEHOUND_VIOLET);
    tft.drawRoundRect(6, 63, 228, 50, 5, HALEHOUND_GUNMETAL);

    // Instrument panel area: drawn dynamically in updateGPSValues()

    // ALT + HDOP labels
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(8, 188);
    tft.print("ALT");
    tft.setCursor(130, 188);
    tft.print("ACC");

    // Separator
    tft.drawLine(5, 200, 235, 200, HALEHOUND_HOTPINK);

    // Date/Time labels
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(8, 204);
    tft.print("DATE");
    tft.setCursor(130, 204);
    tft.print("TIME");

    // Separator
    tft.drawLine(5, 216, 235, 216, HALEHOUND_HOTPINK);

    // Status box frame
    tft.drawRoundRect(5, 220, 230, 28, 4, HALEHOUND_VIOLET);

    // Diagnostic section labels
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(8, 254);
    tft.print("NMEA");
    tft.setCursor(8, 266);
    tft.print("PIN");
    tft.setCursor(8, 278);
    tft.print("AGE");
}

// ═══════════════════════════════════════════════════════════════════════════
// UPDATE GPS VALUES — Called every 1 second
//
// Redraws all dynamic content: coordinates with crosshairs, all three
// instruments, ALT/HDOP values, date/time, status box, diagnostics.
// ═══════════════════════════════════════════════════════════════════════════

static void updateGPSValues() {
    char buf[48];

    // ── Coordinate frame (clear interior, draw crosshairs, then values) ──
    tft.fillRect(8, 65, 224, 46, TFT_BLACK);
    drawCrosshairs();

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

    // ── Instrument panel (compass, speed arc, satellite bars) ──
    drawCompass(currentData.course, currentData.valid);
    drawSpeedArc(currentData.speed, currentData.valid);
    drawSatBars(currentData.satellites);

    // ── ALT + HDOP values ──
    tft.setTextSize(1);

    // ALT value
    tft.fillRect(30, 188, 90, 10, TFT_BLACK);
    tft.setTextColor(currentData.valid ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(30, 188);
    if (currentData.valid) {
        snprintf(buf, sizeof(buf), "%.1fm", currentData.altitude);
        tft.print(buf);
    } else {
        tft.print("---");
    }

    // Accuracy in feet (HDOP × 2.5m × 3.28084 ft/m)
    tft.fillRect(152, 188, 83, 10, TFT_BLACK);
    if (currentData.hdop > 0.01 && currentData.valid) {
        float accFeet = currentData.hdop * 2.5f * 3.28084f;
        uint16_t accColor;
        if (accFeet < 16.0f)       accColor = HALEHOUND_BRIGHT;    // Tight — excellent
        else if (accFeet < 33.0f)  accColor = HALEHOUND_HOTPINK;   // Decent
        else                       accColor = 0xF800;              // Red — poor
        tft.setTextColor(accColor);
        if (accFeet < 100.0f)
            snprintf(buf, sizeof(buf), "~%.0fft", accFeet);
        else
            snprintf(buf, sizeof(buf), ">100ft");
        tft.setCursor(152, 188);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(152, 188);
        tft.print("---");
    }

    // ── Date / Time ──
    tft.fillRect(34, 204, 90, 10, TFT_BLACK);
    tft.fillRect(160, 204, 75, 10, TFT_BLACK);

    if (currentData.valid && currentData.year > 2000) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                 currentData.year, currentData.month, currentData.day);
        tft.setCursor(34, 204);
        tft.print(buf);

        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 currentData.hour, currentData.minute, currentData.second);
        tft.setCursor(160, 204);
        tft.print(buf);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(34, 204);
        tft.print("----/--/--");
        tft.setCursor(160, 204);
        tft.print("--:--:--");
    }

    // ── Status box (color-coded) ──
    uint32_t chars = gps.charsProcessed();

    tft.fillRoundRect(6, 221, 228, 26, 3, HALEHOUND_DARK);
    tft.setTextSize(1);

    if (chars == 0) {
        // RED — no data from GPS module at all
        drawCenteredText(230, "NO DATA - Check wiring", 0xF800, 1);
    } else if (!currentData.valid) {
        if (currentData.satellites > 0) {
            // HOTPINK — seeing satellites but no fix yet
            snprintf(buf, sizeof(buf), "SEARCHING  %d sats", currentData.satellites);
            drawCenteredText(230, buf, HALEHOUND_HOTPINK, 1);
        } else {
            // VIOLET — getting NMEA but no satellites
            drawCenteredText(230, "NO FIX - Need sky view", HALEHOUND_VIOLET, 1);
        }
    } else {
        if (currentData.satellites >= 4) {
            // GREEN — full 3D fix
            snprintf(buf, sizeof(buf), "3D FIX  %d sats  LOCKED", currentData.satellites);
            drawCenteredText(230, buf, 0x07E0, 1);
        } else {
            // BRIGHT — 2D fix (no altitude)
            snprintf(buf, sizeof(buf), "2D FIX  %d sats", currentData.satellites);
            drawCenteredText(230, buf, HALEHOUND_BRIGHT, 1);
        }
    }

    // ── Pulsing fix dot (inside status box, right side) ──
    bool hasData = (chars > 0);
    drawSkullIndicator(currentData.valid, hasData);

    // ── Diagnostics (NMEA y=254, PIN y=266, AGE y=278) ──
    tft.fillRect(35, 254, 200, 10, TFT_BLACK);
    tft.fillRect(30, 266, 200, 10, TFT_BLACK);
    tft.fillRect(30, 278, 200, 10, TFT_BLACK);

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setTextSize(1);

    // NMEA stats
    snprintf(buf, sizeof(buf), "%lu chars  %lu ok  %lu fail",
             (unsigned long)gps.charsProcessed(),
             (unsigned long)gps.sentencesWithFix(),
             (unsigned long)gps.failedChecksum());
    tft.setCursor(35, 254);
    tft.print(buf);

    // Active pin/baud
    if (gpsActivePin >= 0) {
        snprintf(buf, sizeof(buf), "GPIO%d @ %d", gpsActivePin, gpsActiveBaud);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    tft.setCursor(30, 266);
    tft.print(buf);

    // Fix age
    if (currentData.valid) {
        snprintf(buf, sizeof(buf), "%lums", (unsigned long)currentData.age);
    } else {
        snprintf(buf, sizeof(buf), "---");
    }
    tft.setCursor(30, 278);
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

    if (gps.hdop.isUpdated()) {
        currentData.hdop = gps.hdop.value() / 100.0;
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
        Serial.printf("[GPS] Chars:%lu  Fix:%lu  Fail:%lu  Sats:%d  Valid:%d  HDOP:%.1f\n",
                      (unsigned long)gps.charsProcessed(),
                      (unsigned long)gps.sentencesWithFix(),
                      (unsigned long)gps.failedChecksum(),
                      currentData.satellites,
                      currentData.valid ? 1 : 0,
                      currentData.hdop);
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
    lastPulseUpdate = millis();

    while (!exitRequested) {
        // Update GPS data
        gpsUpdate();

        // Full display update every 1 second
        if (millis() - lastDisplayUpdate >= GPS_UPDATE_INTERVAL_MS) {
            updateGPSValues();
            lastDisplayUpdate = millis();
        }

        // Pulsing fix dot — smooth animation at 150ms intervals
        if (millis() - lastPulseUpdate >= 150) {
            uint32_t chars = gps.charsProcessed();
            bool hasData = (chars > 0);
            drawSkullIndicator(currentData.valid, hasData);
            lastPulseUpdate = millis();
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

// ═══════════════════════════════════════════════════════════════════════════
// BACKGROUND GPS — for wardriving and other modules that need live GPS
// without the full GPS screen UI
// ═══════════════════════════════════════════════════════════════════════════

void gpsStartBackground() {
    // Kill UART0 (Serial) to free GPIO 3 for GPS UART2
    Serial.end();
    delay(50);

    if (gpsInitialized && gpsActivePin >= 0) {
        // GPS was scanned before — reopen UART2 on the known working pin
        gpsSerial.begin(gpsActiveBaud, SERIAL_8N1, gpsActivePin, -1);
    } else {
        // Never scanned — use default pin (GPIO 3 P1 connector @ 9600)
        gpsActivePin = GPS_RX_PIN;
        gpsActiveBaud = GPS_BAUD;
        gpsSerial.begin(gpsActiveBaud, SERIAL_8N1, gpsActivePin, -1);
        gpsInitialized = true;
    }

    // Drain any garbage from buffer
    while (gpsSerial.available()) gpsSerial.read();
}

void gpsStopBackground() {
    gpsSerial.end();
    delay(50);
    Serial.begin(115200);
}
