// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Radio Test Tool
// Interactive SPI radio hardware verification (NRF24L01+ and CC1101)
// Tap a radio name to run its test. Results show inline as PASS/FAIL.
// Includes wiring reference and battery voltage check.
// Created: 2026-02-19
// ═══════════════════════════════════════════════════════════════════════════

#include "radio_test.h"
#include "cyd_config.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "icon.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>

extern TFT_eSPI tft;

// Forward declarations for functions defined in HaleHound-CYD.ino
extern void drawStatusBar();
extern void drawInoIconBar();

// ═══════════════════════════════════════════════════════════════════════════
// SCREEN LAYOUT CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════

// Title at Y=60 (drawGlitchTitle)
// NRF24 button:   Y=85..108
// NRF24 status:   Y=110 (two lines: result + troubleshoot hint)
// CC1101 button:  Y=140..163
// CC1101 status:  Y=165 (two lines)
// Wiring button:  Y=200..223
// Battery line:   Y=230
// Hint:           Y=260

#define RT_NRF_BTN_Y     85
#define RT_NRF_BTN_H     23
#define RT_NRF_STATUS_Y  110
#define RT_NRF_HINT_Y    122
#define RT_CC_BTN_Y      140
#define RT_CC_BTN_H      23
#define RT_CC_STATUS_Y   165
#define RT_CC_HINT_Y     177
#define RT_WIRE_BTN_Y    200
#define RT_WIRE_BTN_H    23
#define RT_BATT_Y        230
#define RT_HINT_Y        260
#define RT_BTN_X          20
#define RT_BTN_W         200

// ═══════════════════════════════════════════════════════════════════════════
// DRAWING HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static void drawRadioButton(int y, int h, const char* label, uint16_t color) {
    tft.fillRect(RT_BTN_X, y, RT_BTN_W, h, TFT_BLACK);
    tft.drawRoundRect(RT_BTN_X, y, RT_BTN_W, h, 4, color);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextSize(1);
    int tw = tft.textWidth(label);
    int tx = RT_BTN_X + (RT_BTN_W - tw) / 2;
    int ty = y + (h - 16) / 2;
    tft.setCursor(tx, ty);
    tft.print(label);
}

static void drawStatusLine(int y, const char* text, uint16_t color) {
    tft.fillRect(0, y, SCREEN_WIDTH, 12, TFT_BLACK);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextFont(1);
    tft.setTextSize(1);
    int tw = tft.textWidth(text);
    int tx = (SCREEN_WIDTH - tw) / 2;
    if (tx < 5) tx = 5;
    tft.setCursor(tx, y);
    tft.print(text);
}

static void drawTestingIndicator(int statusY) {
    drawStatusLine(statusY, "Testing...", TFT_YELLOW);
    // Clear troubleshoot hint line too
    tft.fillRect(0, statusY + 12, SCREEN_WIDTH, 12, TFT_BLACK);
}

// ═══════════════════════════════════════════════════════════════════════════
// SPI HELPERS (same proven patterns as runBootDiagnostics)
// ═══════════════════════════════════════════════════════════════════════════

static void deselectAllCS() {
    pinMode(SD_CS, OUTPUT);       digitalWrite(SD_CS, HIGH);
    pinMode(CC1101_CS, OUTPUT);   digitalWrite(CC1101_CS, HIGH);
    pinMode(NRF24_CSN, OUTPUT);   digitalWrite(NRF24_CSN, HIGH);
    pinMode(NRF24_CE, OUTPUT);    digitalWrite(NRF24_CE, LOW);
}

static void spiReset4MHz() {
    SPI.end();
    delay(10);
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI);
    SPI.setFrequency(4000000);
    delay(10);
}

// Raw NRF24 register read (manual CS toggle, no library dependency)
static byte rawNrfRead(byte reg) {
    byte val;
    digitalWrite(NRF24_CSN, LOW);
    delayMicroseconds(5);
    SPI.transfer(reg & 0x1F);    // R_REGISTER command
    val = SPI.transfer(0xFF);
    digitalWrite(NRF24_CSN, HIGH);
    return val;
}

// Raw NRF24 register write (manual CS toggle)
static void rawNrfWrite(byte reg, byte val) {
    digitalWrite(NRF24_CSN, LOW);
    delayMicroseconds(5);
    SPI.transfer((reg & 0x1F) | 0x20);  // W_REGISTER command
    SPI.transfer(val);
    digitalWrite(NRF24_CSN, HIGH);
}

// ═══════════════════════════════════════════════════════════════════════════
// BATTERY VOLTAGE
// ═══════════════════════════════════════════════════════════════════════════

static void readAndDrawBattery() {
    // GPIO34 = LDR/Battery ADC pin (input only, 12-bit)
    // With 2:1 voltage divider: actual_V = (adc / 4095) * 3.3 * 2
    int raw = analogRead(BATTERY_ADC_PIN);
    float voltage = (raw / 4095.0f) * 3.3f * BATTERY_DIVIDER;

    char msg[48];
    uint16_t color;

    if (raw < 100) {
        // No divider connected — ADC floating or no battery
        snprintf(msg, sizeof(msg), "Battery: no divider (ADC=%d)", raw);
        color = HALEHOUND_GUNMETAL;
    } else if (voltage < 3.3f) {
        snprintf(msg, sizeof(msg), "Battery: %.2fV LOW! (ADC=%d)", voltage, raw);
        color = TFT_RED;
    } else if (voltage < 3.6f) {
        snprintf(msg, sizeof(msg), "Battery: %.2fV warn (ADC=%d)", voltage, raw);
        color = TFT_YELLOW;
    } else {
        snprintf(msg, sizeof(msg), "Battery: %.2fV OK (ADC=%d)", voltage, raw);
        color = TFT_GREEN;
    }

    drawStatusLine(RT_BATT_Y, msg, color);
}

// ═══════════════════════════════════════════════════════════════════════════
// NRF24 TEST — with smart failure diagnostics
// ═══════════════════════════════════════════════════════════════════════════

static void runNrfTest(int statusY, int hintY) {
    drawTestingIndicator(statusY);

    deselectAllCS();
    spiReset4MHz();

    // Step 1: Read STATUS register — 3 attempts with increasing delays
    bool statusOK = false;
    byte statusVal = 0x00;
    int nrfDelays[] = {10, 100, 500};

    for (int attempt = 0; attempt < 3; attempt++) {
        delay(nrfDelays[attempt]);
        statusVal = rawNrfRead(0x07);  // 0x07 = STATUS register
        if (statusVal != 0x00 && statusVal != 0xFF) {
            statusOK = true;
            break;
        }
    }

    if (!statusOK) {
        char msg[48];
        if (statusVal == 0x00) {
            // Bus reads all zeros — chip not powered or CS not connected
            snprintf(msg, sizeof(msg), "FAIL  STATUS=0x00 (no power?)");
            drawStatusLine(statusY, msg, TFT_RED);
            drawStatusLine(hintY, "Check 3.3V and CSN wire (GPIO 4)", TFT_YELLOW);
        } else {
            // 0xFF = MISO stuck high — no chip pulling line down
            snprintf(msg, sizeof(msg), "FAIL  STATUS=0xFF (MISO stuck)");
            drawStatusLine(statusY, msg, TFT_RED);
            drawStatusLine(hintY, "Check MISO (GPIO 19) and CSN (GPIO 4)", TFT_YELLOW);
        }
        return;
    }

    // Step 2: Write/readback test — write 0x3F to EN_AA, read it back
    rawNrfWrite(0x01, 0x3F);            // Write all-pipes-enabled
    delayMicroseconds(10);
    byte readback = rawNrfRead(0x01);   // Read it back
    rawNrfWrite(0x01, 0x00);            // Restore to disabled (our default)

    if (readback == 0x3F) {
        char msg[48];
        snprintf(msg, sizeof(msg), "PASS  ST=0x%02X WR=0x%02X", statusVal, readback);
        drawStatusLine(statusY, msg, TFT_GREEN);
        tft.fillRect(0, hintY, SCREEN_WIDTH, 12, TFT_BLACK);  // Clear hint
    } else {
        char msg[48];
        snprintf(msg, sizeof(msg), "FAIL  ST=0x%02X WR=0x%02X!=0x3F", statusVal, readback);
        drawStatusLine(statusY, msg, TFT_RED);
        drawStatusLine(hintY, "Check MOSI (GPIO 23) or 3.3V sag", TFT_YELLOW);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CC1101 TEST — with smart failure diagnostics
// ═══════════════════════════════════════════════════════════════════════════

static void runCC1101Test(int statusY, int hintY) {
    drawTestingIndicator(statusY);

    deselectAllCS();

    // Reset SPI for ELECHOUSE library (it does its own SPI.begin)
    SPI.end();
    delay(10);

    // Deselect NRF24 explicitly (ELECHOUSE won't do it)
    pinMode(NRF24_CSN, OUTPUT);   digitalWrite(NRF24_CSN, HIGH);
    pinMode(SD_CS, OUTPUT);       digitalWrite(SD_CS, HIGH);

    // Configure ELECHOUSE with our SPI and GDO pins
    ELECHOUSE_cc1101.setSpiPin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    // Step 1: Check if chip responds on SPI
    bool detected = ELECHOUSE_cc1101.getCC1101();

    if (!detected) {
        drawStatusLine(statusY, "FAIL  No SPI response", TFT_RED);
        drawStatusLine(hintY, "Check CS (GPIO 27) and 3.3V power", TFT_YELLOW);
        return;
    }

    // Step 2: Read VERSION register (0x31) — genuine CC1101 returns 0x14
    byte version = ELECHOUSE_cc1101.SpiReadStatus(0x31);

    if (version == 0x14) {
        char msg[48];
        snprintf(msg, sizeof(msg), "PASS  VER=0x%02X (genuine CC1101)", version);
        drawStatusLine(statusY, msg, TFT_GREEN);
        tft.fillRect(0, hintY, SCREEN_WIDTH, 12, TFT_BLACK);  // Clear hint
    } else if (version > 0x00 && version != 0xFF) {
        char msg[48];
        snprintf(msg, sizeof(msg), "WARN  VER=0x%02X (clone chip?)", version);
        drawStatusLine(statusY, msg, TFT_YELLOW);
        drawStatusLine(hintY, "Works but not genuine TI CC1101", TFT_YELLOW);
    } else {
        char msg[48];
        snprintf(msg, sizeof(msg), "FAIL  VER=0x%02X", version);
        drawStatusLine(statusY, msg, TFT_RED);
        drawStatusLine(hintY, "Check MISO (GPIO 19) solder joint", TFT_YELLOW);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// WIRING DIAGRAMS — Duggie's KiCad layouts as TFT block diagrams
// 4 pages: text reference + NRF24 diagram + GPS diagram + CC1101 diagram
// ═══════════════════════════════════════════════════════════════════════════

static int wiringPage = 0;
#define WIRING_NUM_PAGES 4

// Diagram layout constants
#define DIAG_LEFT_X     5
#define DIAG_LEFT_W     85
#define DIAG_RIGHT_X    150
#define DIAG_RIGHT_W    85
#define DIAG_TRACE_X1   (DIAG_LEFT_X + DIAG_LEFT_W)
#define DIAG_TRACE_X2   DIAG_RIGHT_X

// Draw a single pin row with colored trace line between two chip boxes
static void drawPinTrace(int y, const char* leftPin, const char* rightPin,
                          uint16_t color, bool dashed = false) {
    int traceY = y + 4;

    // Left label (right-aligned inside left box)
    tft.setTextColor(color, TFT_BLACK);
    int lw = strlen(leftPin) * 6;
    tft.setCursor(DIAG_LEFT_X + DIAG_LEFT_W - lw - 8, y);
    tft.print(leftPin);

    // Solder dots at box edges
    tft.fillCircle(DIAG_TRACE_X1, traceY, 2, color);
    tft.fillCircle(DIAG_TRACE_X2, traceY, 2, color);

    // Trace line (2px thick for visibility)
    if (dashed) {
        for (int x = DIAG_TRACE_X1 + 4; x < DIAG_TRACE_X2 - 4; x += 8) {
            tft.drawFastHLine(x, traceY, 4, color);
            tft.drawFastHLine(x, traceY + 1, 4, color);
        }
    } else {
        int len = DIAG_TRACE_X2 - DIAG_TRACE_X1 - 6;
        tft.drawFastHLine(DIAG_TRACE_X1 + 3, traceY, len, color);
        tft.drawFastHLine(DIAG_TRACE_X1 + 3, traceY + 1, len, color);
    }

    // Right label (left-aligned inside right box)
    tft.setCursor(DIAG_RIGHT_X + 8, y);
    tft.print(rightPin);
}

// Draw page navigation footer
static void drawPageNav(int page, int total) {
    tft.setTextFont(1);
    tft.setTextSize(1);

    // Left/right arrows
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(15, 287);
    tft.print("<");
    tft.setCursor(SCREEN_WIDTH - 21, 287);
    tft.print(">");

    // Page number
    char buf[8];
    snprintf(buf, sizeof(buf), "%d/%d", page + 1, total);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    int tw = strlen(buf) * 6;
    tft.setCursor((SCREEN_WIDTH - tw) / 2, 287);
    tft.print(buf);

    // Navigation hint
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(22, 305);
    tft.print("TAP </> = Page  BACK = Exit");
}

// ── Page 0: Text wiring reference (original pin lists) ──
static void drawWiringText() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();
    drawGlitchTitle(60, "WIRING");

    tft.setTextFont(1);
    tft.setTextSize(1);
    int y = 80;
    int lineH = 12;

    // NRF24 section
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print("--- NRF24L01+PA+LNA ---");
    y += lineH + 2;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, y);  tft.print("VCC  = 3.3V     GND = GND");
    y += lineH;
    tft.setCursor(10, y);  tft.printf("CSN  = GPIO %-3d CE  = GPIO %d", NRF24_CSN, NRF24_CE);
    y += lineH;
    tft.setCursor(10, y);  tft.printf("SCK  = GPIO %-3d MOSI= GPIO %d", VSPI_SCK, VSPI_MOSI);
    y += lineH;
    tft.setCursor(10, y);  tft.printf("MISO = GPIO %-3d IRQ = N/C", VSPI_MISO);
    y += lineH;

    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(10, y);  tft.print("IRQ not used (optional)");
    y += lineH;

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, y);  tft.print("TIP: Use clean 3.3V source!");
    y += lineH + 6;

    // CC1101 section
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print("--- CC1101 SubGHz ---");
    y += lineH + 2;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, y);  tft.print("VCC  = 3.3V     GND = GND");
    y += lineH;
    tft.setCursor(10, y);  tft.printf("CS   = GPIO %-3d SCK = GPIO %d", CC1101_CS, VSPI_SCK);
    y += lineH;
    tft.setCursor(10, y);  tft.printf("MOSI = GPIO %-3d MISO= GPIO %d", VSPI_MOSI, VSPI_MISO);
    y += lineH;
    tft.setCursor(10, y);  tft.printf("GDO0 = GPIO %-3d GDO2= GPIO %d", CC1101_GDO0, CC1101_GDO2);
    y += lineH;

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, y);  tft.print("GDO0=TX(out) GDO2=RX(in)");
    y += lineH + 6;

    // Shared SPI note
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print("Both radios + SD share VSPI bus");
    y += lineH;
    tft.setCursor(10, y);
    tft.printf("SD CS = GPIO %d", SD_CS);

    drawPageNav(0, WIRING_NUM_PAGES);
}

// ── Page 1: NRF24L01+ block diagram ──
static void drawNrf24Diagram() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();
    drawGlitchTitle(60, "NRF24 WIRING");

    tft.setTextFont(1);
    tft.setTextSize(1);

    int pinSpace = 18;
    int boxY = 82;
    int boxH = 8 * pinSpace + 22;

    // Left box — ESP32
    tft.drawRect(DIAG_LEFT_X, boxY, DIAG_LEFT_W, boxH, HALEHOUND_MAGENTA);
    tft.drawRect(DIAG_LEFT_X + 1, boxY + 1, DIAG_LEFT_W - 2, boxH - 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(DIAG_LEFT_X + 25, boxY + 4);
    tft.print("ESP32");

    // Right box — NRF24L01+
    tft.drawRect(DIAG_RIGHT_X, boxY, DIAG_RIGHT_W, boxH, HALEHOUND_MAGENTA);
    tft.drawRect(DIAG_RIGHT_X + 1, boxY + 1, DIAG_RIGHT_W - 2, boxH - 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(DIAG_RIGHT_X + 8, boxY + 4);
    tft.print("NRF24L01+");

    // Pin traces — color coded like Duggie's KiCad
    int py = boxY + 20;
    drawPinTrace(py, "3.3V",  "VCC",  TFT_RED,            false);  py += pinSpace;
    drawPinTrace(py, "GND",   "GND",  TFT_WHITE,          false);  py += pinSpace;
    drawPinTrace(py, "IO18",  "SCK",  TFT_CYAN,           false);  py += pinSpace;
    drawPinTrace(py, "IO23",  "MOSI", TFT_CYAN,           false);  py += pinSpace;
    drawPinTrace(py, "IO19",  "MISO", TFT_CYAN,           false);  py += pinSpace;
    drawPinTrace(py, "IO4",   "CSN",  HALEHOUND_MAGENTA,  false);  py += pinSpace;
    drawPinTrace(py, "IO16",  "CE",   HALEHOUND_MAGENTA,  false);  py += pinSpace;
    drawPinTrace(py, "IO17",  "IRQ",  HALEHOUND_GUNMETAL, true);

    // Notes
    int noteY = boxY + boxH + 6;
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(5, noteY);
    tft.print("3.3V+GND from CN1 (IO22/IO27 plug)");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(5, noteY + 14);
    tft.print("No cap needed from this source!");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(5, noteY + 28);
    tft.print("Shares VSPI with CC1101 + SD");

    drawPageNav(1, WIRING_NUM_PAGES);
}

// ── Page 2: GPS block diagram ──
static void drawGpsDiagram() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();
    drawGlitchTitle(60, "GPS WIRING");

    tft.setTextFont(1);
    tft.setTextSize(1);

    int pinSpace = 22;
    int boxY = 95;
    int boxH = 4 * pinSpace + 22;

    // Left box — CYD P1 Connector
    tft.drawRect(DIAG_LEFT_X, boxY, DIAG_LEFT_W, boxH, HALEHOUND_MAGENTA);
    tft.drawRect(DIAG_LEFT_X + 1, boxY + 1, DIAG_LEFT_W - 2, boxH - 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(DIAG_LEFT_X + 15, boxY + 4);
    tft.print("CYD  P1");

    // Right box — GT-U7 GPS
    tft.drawRect(DIAG_RIGHT_X, boxY, DIAG_RIGHT_W, boxH, HALEHOUND_MAGENTA);
    tft.drawRect(DIAG_RIGHT_X + 1, boxY + 1, DIAG_RIGHT_W - 2, boxH - 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(DIAG_RIGHT_X + 6, boxY + 4);
    tft.print("GT-U7 GPS");

    // Pin traces
    int py = boxY + 22;
    drawPinTrace(py, "VIN",    "VCC", TFT_RED,            false);  py += pinSpace;
    drawPinTrace(py, "GND",    "GND", TFT_WHITE,          false);  py += pinSpace;
    drawPinTrace(py, "RX IO3", "TX",  TFT_CYAN,           false);  py += pinSpace;
    drawPinTrace(py, "TX IO1", "RX",  TFT_CYAN,          false);

    // Notes
    int noteY = boxY + boxH + 10;
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(5, noteY);
    tft.print("GPIO3 shared with CH340C USB!");
    noteY += 14;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(5, noteY);
    tft.print("Serial.end() before GPS init");

    drawPageNav(2, WIRING_NUM_PAGES);
}

// ── Page 3: CC1101 block diagram ──
static void drawCC1101Diagram() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();
    drawGlitchTitle(60, "CC1101 WIRING");

    tft.setTextFont(1);
    tft.setTextSize(1);

    int pinSpace = 18;
    int boxY = 82;
    int boxH = 8 * pinSpace + 22;

    // Left box — ESP32
    tft.drawRect(DIAG_LEFT_X, boxY, DIAG_LEFT_W, boxH, HALEHOUND_MAGENTA);
    tft.drawRect(DIAG_LEFT_X + 1, boxY + 1, DIAG_LEFT_W - 2, boxH - 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(DIAG_LEFT_X + 25, boxY + 4);
    tft.print("ESP32");

    // Right box — CC1101
    tft.drawRect(DIAG_RIGHT_X, boxY, DIAG_RIGHT_W, boxH, HALEHOUND_MAGENTA);
    tft.drawRect(DIAG_RIGHT_X + 1, boxY + 1, DIAG_RIGHT_W - 2, boxH - 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(DIAG_RIGHT_X + 18, boxY + 4);
    tft.print("CC1101");

    // Pin traces — color coded like Duggie's KiCad
    int py = boxY + 20;
    drawPinTrace(py, "3.3V",  "VCC",     TFT_RED,           false);  py += pinSpace;
    drawPinTrace(py, "GND",   "GND",     TFT_WHITE,         false);  py += pinSpace;
    drawPinTrace(py, "IO27",  "CS",      HALEHOUND_MAGENTA, false);  py += pinSpace;
    drawPinTrace(py, "IO18",  "SCK",     TFT_CYAN,          false);  py += pinSpace;
    drawPinTrace(py, "IO23",  "MOSI",    TFT_CYAN,          false);  py += pinSpace;
    drawPinTrace(py, "IO19",  "MISO",    TFT_CYAN,          false);  py += pinSpace;
    drawPinTrace(py, "IO22",  "GDO0 TX", HALEHOUND_HOTPINK, false);  py += pinSpace;
    drawPinTrace(py, "IO35",  "GDO2 RX", TFT_YELLOW,        false);

    // Notes
    int noteY = boxY + boxH + 6;
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setCursor(5, noteY);
    tft.print("3.3V+GND from CN1 (IO22/IO27 plug)");
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(5, noteY + 14);
    tft.print("No cap needed from this source!");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(5, noteY + 28);
    tft.print("GDO0=TX(out)  GDO2=RX(in)");
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(5, noteY + 42);
    tft.print("CS=CN1  GDO0/GDO2=P3 connector");

    drawPageNav(3, WIRING_NUM_PAGES);
}

// Page dispatcher
static void drawCurrentWiringPage() {
    switch (wiringPage) {
        case 0:  drawWiringText();    break;
        case 1:  drawNrf24Diagram();  break;
        case 2:  drawGpsDiagram();    break;
        case 3:  drawCC1101Diagram(); break;
        default: drawWiringText();    break;
    }
}

// Multi-page wiring viewer with LEFT/RIGHT navigation
static void showWiringScreen() {
    wiringPage = 0;
    drawCurrentWiringPage();

    bool exitWiring = false;
    while (!exitWiring) {
        touchButtonsUpdate();

        if (isBackButtonTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitWiring = true;
            break;
        }

        // Right arrow — tap right side of screen
        if (isTouchInArea(180, 275, 60, 40) || buttonPressed(BTN_RIGHT)) {
            wiringPage = (wiringPage + 1) % WIRING_NUM_PAGES;
            drawCurrentWiringPage();
            delay(250);
        }

        // Left arrow — tap left side of screen
        if (isTouchInArea(0, 275, 60, 40) || buttonPressed(BTN_LEFT)) {
            wiringPage = (wiringPage + WIRING_NUM_PAGES - 1) % WIRING_NUM_PAGES;
            drawCurrentWiringPage();
            delay(250);
        }

        delay(20);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN SCREEN
// ═══════════════════════════════════════════════════════════════════════════

static void drawMainScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();
    drawGlitchTitle(60, "RADIO TEST");

    // NRF24 button and status
    drawRadioButton(RT_NRF_BTN_Y, RT_NRF_BTN_H, "[ NRF24L01+ ]", HALEHOUND_MAGENTA);
    drawStatusLine(RT_NRF_STATUS_Y, "Status: --", HALEHOUND_GUNMETAL);

    // CC1101 button and status
    drawRadioButton(RT_CC_BTN_Y, RT_CC_BTN_H, "[ CC1101 ]", HALEHOUND_MAGENTA);
    drawStatusLine(RT_CC_STATUS_Y, "Status: --", HALEHOUND_GUNMETAL);

    // Wiring reference button
    drawRadioButton(RT_WIRE_BTN_Y, RT_WIRE_BTN_H, "[ WIRING ]", HALEHOUND_HOTPINK);

    // Battery voltage
    readAndDrawBattery();

    // Hint
    drawCenteredText(RT_HINT_Y, "Tap radio to test", HALEHOUND_HOTPINK, 1);
}

void radioTestScreen() {
    drawMainScreen();

    bool exitRequested = false;

    while (!exitRequested) {
        touchButtonsUpdate();

        // Check back button (icon bar tap or hardware BOOT button)
        if (isBackButtonTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
            break;
        }

        // Check NRF24 button tap
        if (isTouchInArea(RT_BTN_X, RT_NRF_BTN_Y, RT_BTN_W, RT_NRF_BTN_H)) {
            drawRadioButton(RT_NRF_BTN_Y, RT_NRF_BTN_H, "[ NRF24L01+ ]", TFT_WHITE);
            delay(100);
            drawRadioButton(RT_NRF_BTN_Y, RT_NRF_BTN_H, "[ NRF24L01+ ]", HALEHOUND_MAGENTA);

            runNrfTest(RT_NRF_STATUS_Y, RT_NRF_HINT_Y);

            // Update hint and refresh battery after SPI activity
            tft.fillRect(0, RT_HINT_Y, SCREEN_WIDTH, 14, TFT_BLACK);
            drawCenteredText(RT_HINT_Y, "Tap again to re-test", HALEHOUND_GUNMETAL, 1);
            readAndDrawBattery();

            delay(300);  // Debounce
        }

        // Check CC1101 button tap
        if (isTouchInArea(RT_BTN_X, RT_CC_BTN_Y, RT_BTN_W, RT_CC_BTN_H)) {
            drawRadioButton(RT_CC_BTN_Y, RT_CC_BTN_H, "[ CC1101 ]", TFT_WHITE);
            delay(100);
            drawRadioButton(RT_CC_BTN_Y, RT_CC_BTN_H, "[ CC1101 ]", HALEHOUND_MAGENTA);

            runCC1101Test(RT_CC_STATUS_Y, RT_CC_HINT_Y);

            tft.fillRect(0, RT_HINT_Y, SCREEN_WIDTH, 14, TFT_BLACK);
            drawCenteredText(RT_HINT_Y, "Tap again to re-test", HALEHOUND_GUNMETAL, 1);
            readAndDrawBattery();

            delay(300);  // Debounce
        }

        // Check WIRING button tap
        if (isTouchInArea(RT_BTN_X, RT_WIRE_BTN_Y, RT_BTN_W, RT_WIRE_BTN_H)) {
            drawRadioButton(RT_WIRE_BTN_Y, RT_WIRE_BTN_H, "[ WIRING ]", TFT_WHITE);
            delay(100);

            showWiringScreen();

            // Redraw main screen when returning from wiring
            drawMainScreen();

            delay(300);  // Debounce
        }

        delay(20);
    }

    // Cleanup — restore SPI bus to clean state for spiManager
    SPI.end();
    delay(5);
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI);
    deselectAllCS();
}
