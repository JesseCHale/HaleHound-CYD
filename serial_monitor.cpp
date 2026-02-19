// =============================================================================
// HaleHound-CYD UART Serial Terminal
// Hardware UART passthrough for target device debug ports
// Pattern: Same as gps_module.cpp - standalone functions, own screen loop
// Created: 2026-02-15
// =============================================================================

#include "serial_monitor.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "icon.h"
#include "cyd_config.h"

extern TFT_eSPI tft;

// =============================================================================
// CONSTANTS
// =============================================================================

#define TERM_COLS          40       // Characters per line (240px / 6px font)
#define TERM_ROWS          32       // Visible rows (256px / 8px font)
#define RING_SIZE          64       // Total lines in ring buffer
#define LINE_BUF_SIZE     128       // Incoming line accumulator
#define TERM_Y_START       40       // First terminal row Y position
#define TERM_ROW_HEIGHT     8       // Font size 1 = 8px per row
#define TERM_TEXT_COLOR  0x07FF     // Real cyan for terminal readability
#define STATUS_Y          302       // Status line Y position
#define ICON_SIZE          16       // Icon bitmap size

// =============================================================================
// STATE
// =============================================================================

// Ring buffer - 64 lines x 41 bytes = 2.6KB
static char ringBuffer[RING_SIZE][TERM_COLS + 1];
static int ringHead = 0;
static int ringCount = 0;

// Line accumulator
static char lineBuf[LINE_BUF_SIZE];
static int lineBufPos = 0;

// Config
static const long baudRates[] = {9600, 19200, 38400, 57600, 115200};
static const int numBaudRates = 5;
static int selectedBaudIndex = 4;  // default 115200
static UARTPinMode selectedPin = UART_PIN_P1;

// Runtime
static HardwareSerial monSerial(2);
static bool monPaused = false;
static uint32_t totalBytesRx = 0;

// =============================================================================
// RING BUFFER
// =============================================================================

static void ringPushLine(const char* line) {
    strncpy(ringBuffer[ringHead], line, TERM_COLS);
    ringBuffer[ringHead][TERM_COLS] = '\0';
    ringHead = (ringHead + 1) % RING_SIZE;
    if (ringCount < RING_SIZE) ringCount++;
}

static void ringClear() {
    ringHead = 0;
    ringCount = 0;
    for (int i = 0; i < RING_SIZE; i++) {
        ringBuffer[i][0] = '\0';
    }
}

// Get ring buffer line by visible row index (0 = oldest visible)
static const char* ringGetLine(int visibleRow) {
    if (visibleRow >= ringCount) return "";
    int totalVisible = (ringCount < TERM_ROWS) ? ringCount : TERM_ROWS;
    int startIdx = (ringHead - totalVisible + RING_SIZE) % RING_SIZE;
    int idx = (startIdx + visibleRow) % RING_SIZE;
    return ringBuffer[idx];
}

// =============================================================================
// TERMINAL DRAWING
// =============================================================================

static void redrawTerminal() {
    int totalVisible = (ringCount < TERM_ROWS) ? ringCount : TERM_ROWS;
    tft.setTextSize(1);
    tft.setTextColor(TERM_TEXT_COLOR, TFT_BLACK);

    // Draw visible lines with padding to clear old text
    for (int row = 0; row < totalVisible; row++) {
        tft.setCursor(0, TERM_Y_START + row * TERM_ROW_HEIGHT);
        const char* line = ringGetLine(row);
        tft.print(line);
        int pad = TERM_COLS - strlen(line);
        for (int p = 0; p < pad; p++) tft.print(' ');
    }

    // Clear remaining rows below visible content
    if (totalVisible < TERM_ROWS) {
        int clearY = TERM_Y_START + totalVisible * TERM_ROW_HEIGHT;
        int clearH = (TERM_ROWS - totalVisible) * TERM_ROW_HEIGHT;
        tft.fillRect(0, clearY, tft.width(), clearH, TFT_BLACK);
    }
}

static void scrollAndDrawLine(const char* text) {
    if (monPaused) {
        // Still buffer the data even when paused
        ringPushLine(text);
        return;
    }

    ringPushLine(text);

    if (ringCount <= TERM_ROWS) {
        // Screen not full yet - just draw the new line at bottom
        int row = ringCount - 1;
        tft.setTextSize(1);
        tft.setTextColor(TERM_TEXT_COLOR, TFT_BLACK);
        tft.setCursor(0, TERM_Y_START + row * TERM_ROW_HEIGHT);
        tft.print(text);
    } else {
        // Screen full - full redraw to scroll
        redrawTerminal();
    }
}

// =============================================================================
// STATUS LINE
// =============================================================================

static void updateStatusLine() {
    tft.fillRect(0, STATUS_Y, tft.width(), 16, TFT_BLACK);
    tft.setTextSize(1);

    // RX byte count - left side
    char buf[24];
    if (totalBytesRx < 10000) {
        snprintf(buf, sizeof(buf), "RX: %lu B", (unsigned long)totalBytesRx);
    } else {
        snprintf(buf, sizeof(buf), "RX: %luK", (unsigned long)(totalBytesRx / 1024));
    }
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(4, STATUS_Y + 4);
    tft.print(buf);

    // LIVE/PAUSED - right side
    if (monPaused) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(192, STATUS_Y + 4);
        tft.print("PAUSED");
    } else {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(204, STATUS_Y + 4);
        tft.print("LIVE");
    }
}

// =============================================================================
// BYTE PROCESSING
// =============================================================================

static void flushLineBuf() {
    if (lineBufPos > 0) {
        lineBuf[lineBufPos] = '\0';
        scrollAndDrawLine(lineBuf);
        lineBufPos = 0;
    }
}

static void processIncomingByte(uint8_t b) {
    totalBytesRx++;

    if (b == '\n') {
        flushLineBuf();
        return;
    }

    if (b == '\r') {
        return;  // Ignore carriage return
    }

    // Printable characters pass through, non-printable replaced with dot
    if (b >= 0x20 && b <= 0x7E) {
        lineBuf[lineBufPos++] = (char)b;
    } else {
        lineBuf[lineBufPos++] = '.';
    }

    // Force wrap at column limit
    if (lineBufPos >= TERM_COLS) {
        lineBuf[TERM_COLS] = '\0';
        scrollAndDrawLine(lineBuf);
        lineBufPos = 0;
    }
}

// =============================================================================
// UART LIFECYCLE
// =============================================================================

static void startUART() {
    long baud = baudRates[selectedBaudIndex];
    int rxPin, txPin;

    if (selectedPin == UART_PIN_P1) {
        rxPin = UART_MON_P1_RX;
        txPin = UART_MON_P1_TX;
        // Release UART0 from GPIO3/1 so UART2 can use them
        Serial.end();
        delay(50);
    } else {
        rxPin = UART_MON_SPK_RX;
        txPin = -1;  // RX only
        // No need to release Serial - GPIO26 is independent
    }

    monSerial.begin(baud, SERIAL_8N1, rxPin, txPin);
    delay(50);
}

static void stopUART() {
    monSerial.end();
    delay(50);
    // Restore debug serial on UART0
    Serial.begin(115200);
}

// =============================================================================
// CONFIG SCREEN
// =============================================================================

static void drawBaudSelector() {
    // Clear baud area
    tft.fillRect(20, 85, 200, 40, TFT_BLACK);

    // Rounded rect border
    tft.drawRoundRect(20, 85, 200, 40, 6, HALEHOUND_MAGENTA);

    // Baud rate value centered
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", baudRates[selectedBaudIndex]);
    tft.setTextSize(2);
    tft.setTextColor(TERM_TEXT_COLOR, TFT_BLACK);
    int textW = strlen(buf) * 12;  // Size 2 = 12px per char
    tft.setCursor((tft.width() - textW) / 2, 95);
    tft.print(buf);

    // Tap arrows
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(28, 100);
    tft.print("<");
    tft.setCursor(208, 100);
    tft.print(">");
}

static void drawPinSelector() {
    // Clear pin area
    tft.fillRect(10, 150, 220, 35, TFT_BLACK);

    // P1 DUPLEX button
    if (selectedPin == UART_PIN_P1) {
        tft.fillRoundRect(12, 152, 100, 30, 6, HALEHOUND_MAGENTA);
        tft.setTextColor(TFT_BLACK);
    } else {
        tft.drawRoundRect(12, 152, 100, 30, 6, HALEHOUND_GUNMETAL);
        tft.setTextColor(HALEHOUND_GUNMETAL);
    }
    tft.setTextSize(1);
    tft.setCursor(24, 162);
    tft.print("P1 DUPLEX");

    // SPK RX button
    if (selectedPin == UART_PIN_SPEAKER) {
        tft.fillRoundRect(128, 152, 100, 30, 6, HALEHOUND_MAGENTA);
        tft.setTextColor(TFT_BLACK);
    } else {
        tft.drawRoundRect(128, 152, 100, 30, 6, HALEHOUND_GUNMETAL);
        tft.setTextColor(HALEHOUND_GUNMETAL);
    }
    tft.setTextSize(1);
    tft.setCursor(148, 162);
    tft.print("SPK RX");
}

static void drawStartButton() {
    tft.fillRoundRect(40, 200, 160, 40, 8, TFT_GREEN);
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK);
    tft.setCursor(72, 210);
    tft.print("START");
}

static void drawWiringHint() {
    tft.fillRect(0, 250, tft.width(), 30, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    if (selectedPin == UART_PIN_P1) {
        tft.setCursor(30, 255);
        tft.print("P1: RX=GPIO3  TX=GPIO1");
        tft.setCursor(30, 267);
        tft.print("Connect target TX->RX");
    } else {
        tft.setCursor(40, 255);
        tft.print("SPK: RX=GPIO26 (only)");
        tft.setCursor(35, 267);
        tft.print("Connect target TX->RX");
    }
}

static void drawConfigScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    // Icon bar - back only
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Glitch title
    drawGlitchTitle(48, "UART TERM");

    // Baud rate section
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(85, 72);
    tft.print("BAUD RATE");
    drawBaudSelector();

    // Pin select section
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(80, 138);
    tft.print("PIN SELECT");
    drawPinSelector();

    // Start button
    drawStartButton();

    // Wiring hints
    drawWiringHint();
}

// Returns: 0=nothing, 1=START, -1=back
static int handleConfigTouch() {
    uint16_t tx, ty;
    if (!getTouchPoint(&tx, &ty)) return 0;

    // Back icon (y=20-36, x=10-26)
    if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 26) {
        delay(150);
        return -1;
    }

    // Baud selector area (y=85-125, x=20-220)
    if (ty >= 85 && ty <= 125 && tx >= 20 && tx <= 220) {
        selectedBaudIndex = (selectedBaudIndex + 1) % numBaudRates;
        drawBaudSelector();
        delay(200);
        return 0;
    }

    // P1 DUPLEX button (y=150-185, x=12-112)
    if (ty >= 150 && ty <= 185 && tx >= 12 && tx <= 112) {
        if (selectedPin != UART_PIN_P1) {
            selectedPin = UART_PIN_P1;
            drawPinSelector();
            drawWiringHint();
        }
        delay(200);
        return 0;
    }

    // SPK RX button (y=150-185, x=128-228)
    if (ty >= 150 && ty <= 185 && tx >= 128 && tx <= 228) {
        if (selectedPin != UART_PIN_SPEAKER) {
            selectedPin = UART_PIN_SPEAKER;
            drawPinSelector();
            drawWiringHint();
        }
        delay(200);
        return 0;
    }

    // START button (bottom area, x=40-200)
    if (ty >= (tft.height() - 120) && ty <= (tft.height() - 80) && tx >= 40 && tx <= 200) {
        delay(150);
        return 1;
    }

    return 0;
}

// =============================================================================
// TERMINAL SCREEN
// =============================================================================

static void drawTermIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Icons: back | pause | clear
    tft.drawBitmap(10, 20, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(40, 20, monPaused ? bitmap_icon_eye2 : bitmap_icon_eye, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(70, 20, bitmap_icon_recycle, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);

    // Baud rate text label
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld", baudRates[selectedBaudIndex]);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_DARK);
    tft.setCursor(110, 24);
    tft.print(buf);

    // Pin mode text label
    tft.setCursor(185, 24);
    tft.print(selectedPin == UART_PIN_P1 ? "P1" : "SPK");

    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static void drawTerminalScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawTermIconBar();

    // Separator
    tft.drawLine(0, 38, SCREEN_WIDTH, 38, HALEHOUND_HOTPINK);

    // Terminal area starts clear (black) - lines drawn by scrollAndDrawLine

    // Status line
    updateStatusLine();
}

static bool isTermBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 26) {
            delay(150);
            return true;
        }
    }
    return false;
}

static bool isTermPauseTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 36 && tx >= 40 && tx < 56) {
            delay(150);
            return true;
        }
    }
    return false;
}

static bool isTermClearTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 36 && tx >= 70 && tx < 86) {
            delay(150);
            return true;
        }
    }
    return false;
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

void serialMonitorScreen() {
    // Reset state
    monPaused = false;
    totalBytesRx = 0;
    lineBufPos = 0;
    ringClear();

    // Draw config screen
    drawConfigScreen();

    // -- Config loop --
    bool exitRequested = false;
    bool startRequested = false;

    while (!exitRequested && !startRequested) {
        touchButtonsUpdate();

        int result = handleConfigTouch();
        if (result == -1) exitRequested = true;
        if (result == 1) startRequested = true;

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }

        delay(20);
    }

    if (exitRequested) return;

    // -- Start UART and terminal --
    startUART();
    drawTerminalScreen();

    // Push initial info message
    char initMsg[TERM_COLS + 1];
    snprintf(initMsg, sizeof(initMsg), "UART %ld 8N1 %s",
             baudRates[selectedBaudIndex],
             selectedPin == UART_PIN_P1 ? "P1:GPIO3/1" : "SPK:GPIO26");
    scrollAndDrawLine(initMsg);
    scrollAndDrawLine("Waiting for data...");

    unsigned long lastStatusUpdate = millis();
    unsigned long lastByteTime = 0;

    // -- Terminal loop --
    exitRequested = false;
    while (!exitRequested) {
        // Read incoming bytes (max 256 per frame to keep UI responsive)
        int bytesThisFrame = 0;
        while (monSerial.available() && !monPaused && bytesThisFrame < 256) {
            uint8_t b = monSerial.read();
            processIncomingByte(b);
            bytesThisFrame++;
        }

        // Flush partial line if data stalls (100ms timeout)
        if (lineBufPos > 0 && monSerial.available() == 0) {
            if (lastByteTime == 0) {
                lastByteTime = millis();
            } else if (millis() - lastByteTime > 100) {
                flushLineBuf();
                lastByteTime = 0;
            }
        } else if (monSerial.available() > 0 || bytesThisFrame > 0) {
            lastByteTime = millis();
        }

        // Update status line every 500ms
        if (millis() - lastStatusUpdate >= 500) {
            updateStatusLine();
            lastStatusUpdate = millis();
        }

        // Touch handling
        touchButtonsUpdate();

        if (isTermBackTapped()) {
            exitRequested = true;
        } else if (isTermPauseTapped()) {
            monPaused = !monPaused;
            // Update pause icon
            tft.fillRect(40, 20, ICON_SIZE, ICON_SIZE, HALEHOUND_DARK);
            tft.drawBitmap(40, 20, monPaused ? bitmap_icon_eye2 : bitmap_icon_eye,
                           ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
            if (!monPaused) {
                // Resuming - redraw terminal to show lines buffered while paused
                redrawTerminal();
            }
            updateStatusLine();
        } else if (isTermClearTapped()) {
            ringClear();
            totalBytesRx = 0;
            lineBufPos = 0;
            tft.fillRect(0, TERM_Y_START, tft.width(), TERM_ROWS * TERM_ROW_HEIGHT, TFT_BLACK);
            updateStatusLine();
        }

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }

        delay(5);
    }

    // -- Cleanup --
    flushLineBuf();
    stopUART();
}
