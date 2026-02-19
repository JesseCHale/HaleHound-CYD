// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD NRF24 Attack Modules Implementation
// FULL IMPLEMENTATIONS - Ported from ESP32-DIV v2.5 HaleHound Edition
// Created: 2026-02-06
// Updated: 2026-02-07 - Full implementation port
// ═══════════════════════════════════════════════════════════════════════════
//
// NRF24 PIN CONFIGURATION FOR CYD:
//   CE  = GPIO16
//   CSN = GPIO4
//   SCK = GPIO18 (shared SPI)
//   MOSI = GPIO23 (shared SPI)
//   MISO = GPIO19 (shared SPI)
//   VCC = 3.3V (add 10uF capacitor!)
//
// ═══════════════════════════════════════════════════════════════════════════

#include "nrf24_attacks.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "icon.h"
#include <SPI.h>

// Free Fonts are already included via TFT_eSPI when LOAD_GFXFF is enabled
// Available: FreeMonoBold9pt7b, FreeMonoBold12pt7b, FreeMonoBold18pt7b, etc.

// ═══════════════════════════════════════════════════════════════════════════
// NRF24 PIN DEFINITIONS FOR CYD
// ═══════════════════════════════════════════════════════════════════════════

#define NRF_CE   16
#define NRF_CSN  4

// Use default SPI object (VSPI) - shared with SD card, separate CS pins

// NRF24 Register Definitions
#define _NRF24_CONFIG      0x00
#define _NRF24_EN_AA       0x01
#define _NRF24_EN_RXADDR   0x02
#define _NRF24_SETUP_AW    0x03
#define _NRF24_RF_CH       0x05
#define _NRF24_RF_SETUP    0x06
#define _NRF24_STATUS      0x07
#define _NRF24_RPD         0x09
#define _NRF24_RX_ADDR_P0  0x0A
#define _NRF24_RX_ADDR_P1  0x0B
#define _NRF24_RX_ADDR_P2  0x0C
#define _NRF24_RX_ADDR_P3  0x0D
#define _NRF24_RX_ADDR_P4  0x0E
#define _NRF24_RX_ADDR_P5  0x0F

// Promiscuous mode noise-catching addresses (alternating bit patterns)
static const uint8_t noiseAddr0[] = {0x55, 0x55};
static const uint8_t noiseAddr1[] = {0xAA, 0xAA};
static const uint8_t noiseAddr2 = 0xA0;
static const uint8_t noiseAddr3 = 0xAB;
static const uint8_t noiseAddr4 = 0xAC;
static const uint8_t noiseAddr5 = 0xAD;

// ═══════════════════════════════════════════════════════════════════════════
// SHARED ICON BAR FOR NRF24 SCREENS
// ═══════════════════════════════════════════════════════════════════════════

#define NRF_ICON_SIZE 16
#define NRF_ICON_NUM 3

static int nrfIconX[NRF_ICON_NUM] = {170, 210, 10};
static const unsigned char* nrfIcons[NRF_ICON_NUM] = {
    bitmap_icon_undo,      // Calibrate/Reset
    bitmap_icon_start,     // Start/Stop
    bitmap_icon_go_back    // Back
};

static int nrfActiveIcon = -1;
static int nrfAnimState = 0;
static unsigned long nrfLastAnim = 0;

// Draw icon bar with 3 icons - MATCHES ORIGINAL HALEHOUND
static void drawNrfIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < NRF_ICON_NUM; i++) {
        if (nrfIcons[i] != NULL) {
            tft.drawBitmap(nrfIconX[i], 20, nrfIcons[i], NRF_ICON_SIZE, NRF_ICON_SIZE, HALEHOUND_MAGENTA);
        }
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Check icon bar touch and return icon index (0-2) or -1
static int checkNrfIconTouch() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            for (int i = 0; i < NRF_ICON_NUM; i++) {
                if (tx >= nrfIconX[i] - 5 && tx <= nrfIconX[i] + NRF_ICON_SIZE + 5) {
                    if (nrfAnimState == 0) {
                        tft.drawBitmap(nrfIconX[i], 20, nrfIcons[i], NRF_ICON_SIZE, NRF_ICON_SIZE, TFT_BLACK);
                        nrfAnimState = 1;
                        nrfActiveIcon = i;
                        nrfLastAnim = millis();
                    }
                    return i;
                }
            }
        }
    }
    return -1;
}

// Process icon animation and return action (0=calibrate, 1=scan, 2=back, -1=none)
static int processNrfIconAnim() {
    if (nrfAnimState > 0 && millis() - nrfLastAnim >= 50) {
        if (nrfAnimState == 1) {
            tft.drawBitmap(nrfIconX[nrfActiveIcon], 20, nrfIcons[nrfActiveIcon], NRF_ICON_SIZE, NRF_ICON_SIZE, HALEHOUND_MAGENTA);
            nrfAnimState = 2;
            int action = nrfActiveIcon;
            nrfLastAnim = millis();
            return action;
        } else if (nrfAnimState == 2) {
            nrfAnimState = 0;
            nrfActiveIcon = -1;
        }
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// SHARED NRF24 SPI FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static byte nrfGetRegister(byte r) {
    byte c;
    digitalWrite(NRF_CSN, LOW);
    SPI.transfer(r & 0x1F);
    c = SPI.transfer(0);
    digitalWrite(NRF_CSN, HIGH);
    return c;
}

static void nrfSetRegister(byte r, byte v) {
    digitalWrite(NRF_CSN, LOW);
    SPI.transfer((r & 0x1F) | 0x20);
    SPI.transfer(v);
    digitalWrite(NRF_CSN, HIGH);
}

static void nrfSetRegisterMulti(byte r, const byte* data, byte len) {
    digitalWrite(NRF_CSN, LOW);
    SPI.transfer((r & 0x1F) | 0x20);
    for (byte i = 0; i < len; i++) {
        SPI.transfer(data[i]);
    }
    digitalWrite(NRF_CSN, HIGH);
}

static void nrfSetChannel(uint8_t channel) {
    nrfSetRegister(_NRF24_RF_CH, channel);
}

static void nrfPowerUp() {
    nrfSetRegister(_NRF24_CONFIG, nrfGetRegister(_NRF24_CONFIG) | 0x02);
    delayMicroseconds(130);
}

static void nrfPowerDown() {
    nrfSetRegister(_NRF24_CONFIG, nrfGetRegister(_NRF24_CONFIG) & ~0x02);
}

static void nrfEnable() {
    digitalWrite(NRF_CE, HIGH);
}

static void nrfDisable() {
    digitalWrite(NRF_CE, LOW);
}

static void nrfSetRX() {
    nrfSetRegister(_NRF24_CONFIG, nrfGetRegister(_NRF24_CONFIG) | 0x01);
    nrfEnable();
    delayMicroseconds(100);
}

static void nrfSetTX() {
    // PWR_UP=1, PRIM_RX=0 for TX mode
    nrfSetRegister(_NRF24_CONFIG, (nrfGetRegister(_NRF24_CONFIG) | 0x02) & ~0x01);
    delayMicroseconds(150);
}

static bool nrfCarrierDetected() {
    return nrfGetRegister(_NRF24_RPD) & 0x01;
}

// Initialize NRF24 hardware
static bool nrfInit() {
    // Configure NRF24 pins
    pinMode(NRF_CE, OUTPUT);
    pinMode(NRF_CSN, OUTPUT);
    digitalWrite(NRF_CE, LOW);
    digitalWrite(NRF_CSN, HIGH);

    // ALWAYS deselect other SPI devices (CS HIGH) before NRF24 operations
    // CC1101 CS = GPIO 27, SD CS = GPIO 5
    pinMode(27, OUTPUT);
    digitalWrite(27, HIGH);  // Deselect CC1101
    pinMode(5, OUTPUT);
    digitalWrite(5, HIGH);   // Deselect SD card

    // Reset SPI bus with proper settle time between end/begin
    SPI.end();
    delay(10);
    SPI.begin(18, 19, 23);  // NO CS pin - manual control with digitalWrite
    SPI.setDataMode(SPI_MODE0);
    SPI.setFrequency(4000000);  // 4MHz — conservative for reliable detection
    SPI.setBitOrder(MSBFIRST);
    delay(10);

    // Try up to 3 times with increasing delays — some boards need longer settle
    bool found = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) delay(attempt * 100);  // 0ms, 100ms, 200ms

        nrfDisable();
        nrfPowerUp();
        nrfSetRegister(_NRF24_EN_AA, 0x00);       // Disable auto-ack
        nrfSetRegister(_NRF24_RF_SETUP, 0x0F);    // 2Mbps, max power

        byte status = nrfGetRegister(_NRF24_STATUS);
        if (status != 0x00 && status != 0xFF) {
            found = true;
            // Bump to full speed now that we know the chip is alive
            SPI.setFrequency(8000000);
            break;
        }
    }

    return found;
}

// ═══════════════════════════════════════════════════════════════════════════
// SCANNER - 2.4GHz Channel Scanner with Bar Graph
// WiFi-only scanning range: 2400-2484 MHz = NRF channels 0-84
// ═══════════════════════════════════════════════════════════════════════════

namespace Scanner {

// WiFi-only scanning range
#define SCAN_CHANNELS 85          // 0-84 = 85 channels (2400-2484 MHz)

// Bar graph layout
#define BAR_START_X 10
#define BAR_START_Y 42
#define BAR_WIDTH 220
#define BAR_HEIGHT 210

// WiFi channel positions (NRF24 channel numbers)
#define WIFI_CH1_NRF 12
#define WIFI_CH6_NRF 37
#define WIFI_CH11_NRF 62
#define WIFI_CH13_NRF 72

// Data arrays
static uint8_t bar_peak_levels[SCAN_CHANNELS];
static int backgroundNoise[SCAN_CHANNELS] = {0};
static bool noiseCalibrated = false;
static bool scanner_initialized = false;
static bool scanning = true;
static bool exitRequested = false;
static bool uiDrawn = false;

// Skull signal meter icons and animation
static const unsigned char* scannerSkulls[] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};
static const int numScannerSkulls = 8;
static int scannerSkullFrame = 0;

// Get bar color (teal to hot pink gradient)
static uint16_t getBarColor(int height, int maxHeight) {
    float ratio = (float)height / (float)maxHeight;
    if (ratio > 1.0f) ratio = 1.0f;

    // Teal RGB(0, 207, 255) -> Hot Pink RGB(255, 28, 82)
    uint8_t r = 0 + (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));

    return tft.color565(r, g, b);
}

static void clearBarGraph() {
    memset(bar_peak_levels, 0, sizeof(bar_peak_levels));
}

static void drawScannerFrame() {
    // Y-axis line
    tft.drawFastVLine(BAR_START_X - 2, BAR_START_Y, BAR_HEIGHT, HALEHOUND_MAGENTA);

    // X-axis line
    tft.drawFastHLine(BAR_START_X, BAR_START_Y + BAR_HEIGHT, BAR_WIDTH, HALEHOUND_MAGENTA);

    // WiFi channel markers - vertical dashed lines
    int x1 = BAR_START_X + (WIFI_CH1_NRF * BAR_WIDTH / SCAN_CHANNELS);
    int x6 = BAR_START_X + (WIFI_CH6_NRF * BAR_WIDTH / SCAN_CHANNELS);
    int x11 = BAR_START_X + (WIFI_CH11_NRF * BAR_WIDTH / SCAN_CHANNELS);
    int x13 = BAR_START_X + (WIFI_CH13_NRF * BAR_WIDTH / SCAN_CHANNELS);

    for (int y = BAR_START_Y; y < BAR_START_Y + BAR_HEIGHT; y += 6) {
        tft.drawPixel(x1, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x6, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x11, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x13, y, HALEHOUND_VIOLET);
    }

    // Channel labels
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x1 - 2, BAR_START_Y - 10);
    tft.print("1");
    tft.setCursor(x6 - 2, BAR_START_Y - 10);
    tft.print("6");
    tft.setCursor(x11 - 6, BAR_START_Y - 10);
    tft.print("11");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(x13 - 6, BAR_START_Y - 10);
    tft.print("13");

    // Frequency labels
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(BAR_START_X - 5, BAR_START_Y + BAR_HEIGHT + 4);
    tft.print("2400");
    tft.setCursor(BAR_START_X + BAR_WIDTH/2 - 12, BAR_START_Y + BAR_HEIGHT + 4);
    tft.print("2442");
    tft.setCursor(BAR_START_X + BAR_WIDTH - 28, BAR_START_Y + BAR_HEIGHT + 4);
    tft.print("2484");

    // Divider
    tft.drawFastHLine(0, BAR_START_Y + BAR_HEIGHT + 16, SCREEN_WIDTH, HALEHOUND_HOTPINK);
}

static void drawBarGraph() {
    // Clear bar area
    tft.fillRect(BAR_START_X, BAR_START_Y, BAR_WIDTH, BAR_HEIGHT, TFT_BLACK);

    // Redraw WiFi channel markers
    int x1 = BAR_START_X + (WIFI_CH1_NRF * BAR_WIDTH / SCAN_CHANNELS);
    int x6 = BAR_START_X + (WIFI_CH6_NRF * BAR_WIDTH / SCAN_CHANNELS);
    int x11 = BAR_START_X + (WIFI_CH11_NRF * BAR_WIDTH / SCAN_CHANNELS);
    int x13 = BAR_START_X + (WIFI_CH13_NRF * BAR_WIDTH / SCAN_CHANNELS);

    for (int y = BAR_START_Y; y < BAR_START_Y + BAR_HEIGHT; y += 6) {
        tft.drawPixel(x1, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x6, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x11, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x13, y, HALEHOUND_VIOLET);
    }

    // Track peak
    int peakChannel = 0;
    uint8_t peakLevel = 0;

    // Draw bars
    for (int ch = 0; ch < SCAN_CHANNELS; ch++) {
        uint8_t level = bar_peak_levels[ch];

        if (level > peakLevel) {
            peakLevel = level;
            peakChannel = ch;
        }

        if (level > 0) {
            int x = BAR_START_X + (ch * BAR_WIDTH / SCAN_CHANNELS);
            int barH = (level * BAR_HEIGHT) / 125;
            if (barH > BAR_HEIGHT) barH = BAR_HEIGHT;
            if (barH < 4 && level > 0) barH = 4;

            int barY = BAR_START_Y + BAR_HEIGHT - barH;

            // Gradient bar
            for (int y = 0; y < barH; y++) {
                uint16_t color = getBarColor(y, BAR_HEIGHT);
                tft.drawFastHLine(x, barY + barH - 1 - y, 2, color);
            }
        }
    }

    // Status area - compact layout below graph
    int statusY = BAR_START_Y + BAR_HEIGHT + 6;
    tft.fillRect(0, statusY, SCREEN_WIDTH, tft.height() - statusY, TFT_BLACK);

    // Divider line
    tft.drawFastHLine(0, statusY - 2, SCREEN_WIDTH, HALEHOUND_HOTPINK);

    // Peak frequency - compact above skulls
    int peakFreq = 2400 + peakChannel;
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(85, statusY + 2);
    tft.print("PEAK: ");
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.print(peakFreq);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.print(" MHz");

    // Skull signal meter - row of 8 skulls
    int skullY = statusY + 14;
    int skullStartX = 10;
    int skullSpacing = 28;  // 16px icon + 12px gap

    // How many skulls to light based on signal (0-8)
    int litSkulls = (peakLevel * 8) / 4;
    if (litSkulls > 8) litSkulls = 8;

    for (int i = 0; i < numScannerSkulls; i++) {
        int x = skullStartX + (i * skullSpacing);
        tft.fillRect(x, skullY, 16, 16, HALEHOUND_BLACK);

        if (i < litSkulls && peakLevel > 0) {
            // Animated color wave - teal to pink
            int phase = (scannerSkullFrame + i) % 8;
            uint16_t skullColor;
            if (phase < 4) {
                float ratio = phase / 3.0f;
                uint8_t r = (uint8_t)(ratio * 255);
                uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
                uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
                skullColor = tft.color565(r, g, b);
            } else {
                float ratio = (phase - 4) / 3.0f;
                uint8_t r = 255 - (uint8_t)(ratio * 255);
                uint8_t g = 28 + (uint8_t)(ratio * (207 - 28));
                uint8_t b = 82 + (uint8_t)(ratio * (255 - 82));
                skullColor = tft.color565(r, g, b);
            }
            tft.drawBitmap(x, skullY, scannerSkulls[i], 16, 16, skullColor);
        } else {
            // Unlit skull - gray
            tft.drawBitmap(x, skullY, scannerSkulls[i], 16, 16, HALEHOUND_GUNMETAL);
        }
    }
    scannerSkullFrame++;

    // Percentage at end
    int pct = (peakLevel * 100) / 125;
    if (pct > 100) pct = 100;
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(skullStartX + (numScannerSkulls * skullSpacing) + 2, skullY + 4);
    tft.printf("%d%%", pct);
}

static void calibrateBackgroundNoise() {
    tft.fillRect(10, BAR_START_Y + BAR_HEIGHT + 40, 220, 20, TFT_BLACK);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, BAR_START_Y + BAR_HEIGHT + 40);
    tft.print("Calibrating noise floor...");

    memset(backgroundNoise, 0, sizeof(backgroundNoise));

    int samples = 5;
    for (int s = 0; s < samples; s++) {
        nrfDisable();
        for (int cycles = 0; cycles < 35; cycles++) {
            for (int i = 0; i < SCAN_CHANNELS; i++) {
                nrfSetChannel(i);
                nrfSetRX();  // MUST set PRIM_RX bit to actually receive!
                delayMicroseconds(50);
                nrfDisable();
                if (nrfCarrierDetected()) {
                    backgroundNoise[i]++;
                }
            }
        }
    }

    for (int i = 0; i < SCAN_CHANNELS; i++) {
        backgroundNoise[i] /= samples;
    }

    noiseCalibrated = true;

    tft.fillRect(10, BAR_START_Y + BAR_HEIGHT + 40, 220, 20, TFT_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, BAR_START_Y + BAR_HEIGHT + 40);
    tft.print("Noise floor captured!");
}

static void scanDisplay() {
    // Persistent channel values (0-125 range) - NOT reset each frame
    static uint8_t channel[SCAN_CHANNELS] = {0};

    if (!scanner_initialized) {
        clearBarGraph();
        drawScannerFrame();
        memset(channel, 0, sizeof(channel));  // Only reset on init
        scanner_initialized = true;
    }

    // Single pass scan with exponential smoothing
    for (int i = 0; i < SCAN_CHANNELS && scanning && !exitRequested; ++i) {
        nrfSetChannel(i);
        nrfSetRX();  // MUST set PRIM_RX bit to actually receive! (not just CE high)
        delayMicroseconds(50);
        nrfDisable();

        int rpd = nrfCarrierDetected() ? 1 : 0;
        // Exponential smoothing: 50% old value + 50% new (scaled to 125)
        channel[i] = (channel[i] + rpd * 125) / 2;
    }

    // Check touch for exit
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty) && tx < 40 && ty >= 20 && ty <= 40) {
        exitRequested = true;
    }

    if (scanning) {
        // Copy smoothed values to display array
        for (int i = 0; i < SCAN_CHANNELS; i++) {
            bar_peak_levels[i] = channel[i];
        }

        drawBarGraph();
    }
}

void scannerSetup() {
    exitRequested = false;
    scanning = true;
    uiDrawn = false;
    scanner_initialized = false;
    nrfAnimState = 0;
    nrfActiveIcon = -1;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Title
    tft.fillRect(0, 20, 160, 16, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(35, 24);
    tft.print("2.4GHz Scanner");

    drawNrfIconBar();

    // Initialize NRF24
    if (!nrfInit()) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setTextSize(2);
        drawCenteredText(100, "NRF24 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check wiring:", HALEHOUND_MAGENTA, 1);
        drawCenteredText(145, "CE=GPIO16 CSN=GPIO4", HALEHOUND_MAGENTA, 1);
        drawCenteredText(160, "Add 10uF cap on VCC!", HALEHOUND_VIOLET, 1);
        return;
    }

    clearBarGraph();
    noiseCalibrated = false;

    #if CYD_DEBUG
    Serial.println("[SCANNER] NRF24 initialized successfully");
    #endif

    scanDisplay();
    uiDrawn = true;
}

void scannerLoop() {
    // Touch uses software bit-banged SPI - no conflict with NRF24 on hardware VSPI

    if (!uiDrawn) {
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }
        return;
    }

    touchButtonsUpdate();

    // Direct touch check - no animation delay
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            // Back icon (x=10)
            if (tx < 40) {
                exitRequested = true;
                return;
            }
            // Calibrate icon (x=170)
            if (tx >= 160 && tx < 200) {
                calibrateBackgroundNoise();
                // Wait for touch release
                while (isTouched()) { delay(10); }
                delay(200);
                // Full reset after calibration - reinit NRF24 and force redraw
                nrfInit();
                scanner_initialized = false;
                scanning = true;
                exitRequested = false;
                clearBarGraph();
                tft.fillRect(BAR_START_X, BAR_START_Y, BAR_WIDTH, BAR_HEIGHT, TFT_BLACK);
                drawScannerFrame();
                return;  // Exit this loop iteration cleanly
            }
            // Refresh icon (x=210)
            if (tx >= 200) {
                // Wait for touch release
                while (isTouched()) { delay(10); }
                delay(200);
                // Full reset - same as calibration
                nrfInit();
                scanner_initialized = false;
                scanning = true;
                exitRequested = false;
                clearBarGraph();
                tft.fillRect(BAR_START_X, BAR_START_Y, BAR_WIDTH, BAR_HEIGHT, TFT_BLACK);
                drawScannerFrame();
                return;
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    scanDisplay();
    delay(5);
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    scanning = false;
    exitRequested = false;
    scanner_initialized = false;
    uiDrawn = false;
    nrfPowerDown();
}

}  // namespace Scanner


// ═══════════════════════════════════════════════════════════════════════════
// ANALYZER - Spectrum Analyzer with Waterfall Display
// ═══════════════════════════════════════════════════════════════════════════

namespace Analyzer {

#define ANA_CHANNELS 85   // Same as scanner - WiFi band 2400-2484 MHz

// Display layout - FULL SCREEN
#define GRAPH_X 2
#define GRAPH_Y 42
#define GRAPH_WIDTH 236
#define GRAPH_HEIGHT 115

#define WATERFALL_Y 162
#define WATERFALL_HEIGHT 126  // 7 rows × 18px each

// Skull waterfall grid
#define SKULL_SIZE 16
#define SKULL_COLS 14         // 236 / 16 = 14 skulls across
#define SKULL_ROWS 7          // 7 rows of skulls
#define SKULL_SPACING_X 17    // 236 / 14 ≈ 17px horizontal spacing
#define SKULL_SPACING_Y 18    // vertical spacing

// WiFi channel positions
#define WIFI_CH1 12
#define WIFI_CH6 37
#define WIFI_CH11 62
#define WIFI_CH13 72

// Data arrays
static uint8_t current_levels[ANA_CHANNELS];
static uint8_t peak_levels[ANA_CHANNELS];
static uint8_t skull_waterfall[SKULL_ROWS][SKULL_COLS];  // Skull-based waterfall
static bool waterfall_initialized = false;
static bool analyzerRunning = true;
static bool exitRequested = false;
static unsigned long lastSkullTime = 0;
static int skullAnimFrame = 0;

// Skull types for waterfall - cycle through all 8
static const unsigned char* skullTypes[] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};
static const int numSkullTypes = 8;

// Skull waterfall color - hot pink (strong) -> electric blue (weak) -> dark gray (none)
static uint16_t getSkullColor(uint8_t level) {
    if (level == 0) return HALEHOUND_GUNMETAL;  // No signal = dark gray

    // BOOST sensitivity - multiply level by 2 for subtle gradient
    int boosted = level * 2;
    if (boosted > 125) boosted = 125;

    float ratio = (float)boosted / 125.0f;

    // Electric Blue RGB(0, 207, 255) -> Hot Pink RGB(255, 28, 82)
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));

    return tft.color565(r, g, b);
}

static void clearSkullWaterfall() {
    memset(skull_waterfall, 0, sizeof(skull_waterfall));
}

static void updateSkullWaterfall() {
    // Shift all rows down by one
    for (int row = SKULL_ROWS - 1; row > 0; row--) {
        for (int col = 0; col < SKULL_COLS; col++) {
            skull_waterfall[row][col] = skull_waterfall[row - 1][col];
        }
    }

    // Average channels into skull columns for top row
    int channelsPerSkull = ANA_CHANNELS / SKULL_COLS;  // 85 / 14 = 6 channels per skull

    for (int col = 0; col < SKULL_COLS; col++) {
        int startCh = col * channelsPerSkull;
        int endCh = startCh + channelsPerSkull;
        if (col == SKULL_COLS - 1) endCh = ANA_CHANNELS;  // Last skull gets remaining

        // Find max signal in this range (more responsive than average)
        uint8_t maxLevel = 0;
        for (int ch = startCh; ch < endCh; ch++) {
            if (peak_levels[ch] > maxLevel) {
                maxLevel = peak_levels[ch];
            }
        }
        skull_waterfall[0][col] = maxLevel;
    }
}

static void drawSkullWaterfall() {
    // Clear waterfall area
    tft.fillRect(GRAPH_X, WATERFALL_Y, GRAPH_WIDTH, WATERFALL_HEIGHT, TFT_BLACK);

    // Draw skull grid with wave animation
    for (int row = 0; row < SKULL_ROWS; row++) {
        int y = WATERFALL_Y + (row * SKULL_SPACING_Y);

        for (int col = 0; col < SKULL_COLS; col++) {
            int x = GRAPH_X + (col * SKULL_SPACING_X);
            uint8_t level = skull_waterfall[row][col];

            uint16_t color;
            if (level == 0) {
                color = HALEHOUND_GUNMETAL;  // No signal = dark gray
            } else {
                // Apply fade based on row (older = dimmer)
                float rowFade = 1.0f - (row * 0.12f);

                // Wave animation phase - creates pulsing color wave
                int phase = (skullAnimFrame + col + row) % 8;
                float waveBoost = (phase < 4) ? (phase / 4.0f) : ((8 - phase) / 4.0f);

                // Combine signal level with wave animation
                float signalRatio = (float)(level * 2) / 125.0f;
                if (signalRatio > 1.0f) signalRatio = 1.0f;

                // Blend: signal determines base, wave adds shimmer
                float finalRatio = (signalRatio * 0.7f) + (waveBoost * 0.3f);
                finalRatio *= rowFade;
                if (finalRatio > 1.0f) finalRatio = 1.0f;

                // Electric Blue RGB(0, 207, 255) -> Hot Pink RGB(255, 28, 82)
                uint8_t r = (uint8_t)(finalRatio * 255);
                uint8_t g = 207 - (uint8_t)(finalRatio * (207 - 28));
                uint8_t b = 255 - (uint8_t)(finalRatio * (255 - 82));
                color = tft.color565(r, g, b);
            }

            // Cycle through all 8 skull types left to right
            const unsigned char* skullIcon = skullTypes[col % numSkullTypes];
            tft.drawBitmap(x, y, skullIcon, SKULL_SIZE, SKULL_SIZE, color);
        }
    }
    skullAnimFrame++;  // Advance animation
}

static void drawWiFiMarkers() {
    int x1 = GRAPH_X + (WIFI_CH1 * GRAPH_WIDTH / ANA_CHANNELS);
    int x6 = GRAPH_X + (WIFI_CH6 * GRAPH_WIDTH / ANA_CHANNELS);
    int x11 = GRAPH_X + (WIFI_CH11 * GRAPH_WIDTH / ANA_CHANNELS);
    int x13 = GRAPH_X + (WIFI_CH13 * GRAPH_WIDTH / ANA_CHANNELS);

    for (int y = GRAPH_Y; y < GRAPH_Y + GRAPH_HEIGHT; y += 4) {
        tft.drawPixel(x1, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x6, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x11, y, HALEHOUND_HOTPINK);
        tft.drawPixel(x13, y, HALEHOUND_VIOLET);
    }

    // Labels
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x1 - 4, GRAPH_Y - 10);
    tft.print("1");
    tft.setCursor(x6 - 4, GRAPH_Y - 10);
    tft.print("6");
    tft.setCursor(x11 - 8, GRAPH_Y - 10);
    tft.print("11");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(x13 - 8, GRAPH_Y - 10);
    tft.print("13");
}

static void drawAxes() {
    tft.drawLine(GRAPH_X - 2, GRAPH_Y, GRAPH_X - 2, GRAPH_Y + GRAPH_HEIGHT, HALEHOUND_MAGENTA);
    tft.drawLine(GRAPH_X, GRAPH_Y + GRAPH_HEIGHT, GRAPH_X + GRAPH_WIDTH, GRAPH_Y + GRAPH_HEIGHT, HALEHOUND_MAGENTA);

    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(GRAPH_X - 5, GRAPH_Y + GRAPH_HEIGHT + 3);
    tft.print("2400");
    tft.setCursor(GRAPH_X + GRAPH_WIDTH/2 - 15, GRAPH_Y + GRAPH_HEIGHT + 3);
    tft.print("2442");
    tft.setCursor(GRAPH_X + GRAPH_WIDTH - 25, GRAPH_Y + GRAPH_HEIGHT + 3);
    tft.print("2484");

    tft.drawLine(0, WATERFALL_Y - 2, SCREEN_WIDTH, WATERFALL_Y - 2, HALEHOUND_HOTPINK);
}

// Get bar color - matches Scanner style (teal to hot pink gradient)
static uint16_t getAnalyzerBarColor(int height, int maxHeight) {
    float ratio = (float)height / (float)maxHeight;
    if (ratio > 1.0f) ratio = 1.0f;

    // Teal RGB(0, 207, 255) -> Hot Pink RGB(255, 28, 82)
    uint8_t r = 0 + (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));

    return tft.color565(r, g, b);
}

static void drawSpectrum() {
    tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, TFT_BLACK);
    drawWiFiMarkers();

    for (int i = 0; i < ANA_CHANNELS; i++) {
        int x = GRAPH_X + (i * GRAPH_WIDTH / ANA_CHANNELS);

        // Use peak_levels for sticky bars - SAME SCALING AS SCANNER
        int barH = (peak_levels[i] * GRAPH_HEIGHT) / 125;
        if (barH > GRAPH_HEIGHT) barH = GRAPH_HEIGHT;
        if (barH < 4 && peak_levels[i] > 0) barH = 4;

        if (barH > 0) {
            int barY = GRAPH_Y + GRAPH_HEIGHT - barH;

            // Gradient bar - MATCHES SCANNER STYLE
            for (int y = 0; y < barH; y++) {
                uint16_t color = getAnalyzerBarColor(y, GRAPH_HEIGHT);
                tft.drawFastHLine(x, barY + barH - 1 - y, 2, color);
            }
        }
    }
    // Skulls drawn separately in loop for performance
}

static void scanAllChannels() {
    // Persistent channel values - same smoothing as Scanner
    static uint8_t channel[ANA_CHANNELS] = {0};

    // Single pass scan with exponential smoothing - MATCHES SCANNER
    for (int ch = 0; ch < ANA_CHANNELS && analyzerRunning && !exitRequested; ch++) {
        nrfSetChannel(ch);
        nrfSetRX();
        delayMicroseconds(50);
        nrfDisable();

        int rpd = nrfCarrierDetected() ? 1 : 0;
        // Exponential smoothing: 50% old + 50% new (scaled to 125) - SAME AS SCANNER
        channel[ch] = (channel[ch] + rpd * 125) / 2;
    }

    // Check touch for exit
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty) && tx < 40 && ty >= 20 && ty <= 40) {
        exitRequested = true;
    }

    // Copy smoothed values to peak_levels for display
    for (int i = 0; i < ANA_CHANNELS; i++) {
        peak_levels[i] = channel[i];
    }
}

static void resetPeaks() {
    memset(peak_levels, 0, sizeof(peak_levels));
    clearSkullWaterfall();
}

static void drawStatusArea() {
    tft.fillRect(0, 300, SCREEN_WIDTH, 20, TFT_BLACK);

    int peakCh = 0;
    uint8_t peakVal = 0;
    for (int i = 0; i < ANA_CHANNELS; i++) {
        if (current_levels[i] > peakVal) {
            peakVal = current_levels[i];
            peakCh = i;
        }
    }

    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 305);
    tft.printf("Peak:%dMHz Lv:%d", 2400 + peakCh, peakVal);
}

void analyzerSetup() {
    exitRequested = false;
    analyzerRunning = true;
    nrfAnimState = 0;
    nrfActiveIcon = -1;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Title
    tft.fillRect(0, 20, 200, 16, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(25, 24);
    tft.print("2.4GHz SPECTRUM ANALYZER");

    drawNrfIconBar();

    if (!nrfInit()) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setTextSize(2);
        drawCenteredText(100, "NRF24 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check wiring:", HALEHOUND_MAGENTA, 1);
        drawCenteredText(145, "CE=GPIO16 CSN=GPIO4", HALEHOUND_MAGENTA, 1);
        return;
    }

    waterfall_initialized = true;
    memset(current_levels, 0, sizeof(current_levels));
    memset(peak_levels, 0, sizeof(peak_levels));
    clearSkullWaterfall();

    drawAxes();
    lastSkullTime = millis();
    skullAnimFrame = 0;

    #if CYD_DEBUG
    Serial.println("[ANALYZER] NRF24 initialized successfully");
    #endif
}

void analyzerLoop() {
    // Touch uses software bit-banged SPI - no conflict with NRF24 on hardware VSPI

    if (!waterfall_initialized) {
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }
        return;
    }

    touchButtonsUpdate();

    // Direct touch check - no animation delay
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            // Back icon (x=10)
            if (tx < 40) {
                exitRequested = true;
                return;
            }
            // Reset peaks icon (x=170)
            if (tx >= 160 && tx < 200) {
                // Wait for touch release
                while (isTouched()) { delay(10); }
                delay(200);
                // Reset and redraw
                resetPeaks();
                tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_WIDTH, GRAPH_HEIGHT, TFT_BLACK);
                tft.fillRect(GRAPH_X, WATERFALL_Y, GRAPH_WIDTH, WATERFALL_HEIGHT, TFT_BLACK);
                drawAxes();
                return;
            }
            // Start/Stop icon (x=210)
            if (tx >= 200) {
                // Wait for touch release
                while (isTouched()) { delay(10); }
                delay(200);
                // Toggle scanning
                analyzerRunning = !analyzerRunning;
                return;
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    scanAllChannels();

    // Bars draw every frame - smooth like scanner
    drawSpectrum();

    // Skulls draw every 100ms - saves performance
    if (millis() - lastSkullTime >= 100) {
        updateSkullWaterfall();
        drawSkullWaterfall();
        drawStatusArea();
        lastSkullTime = millis();
    }
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    analyzerRunning = false;
    exitRequested = false;
    waterfall_initialized = false;
    nrfPowerDown();
}

}  // namespace Analyzer


// ═══════════════════════════════════════════════════════════════════════════
// WLAN JAMMER - WiFi Channel Jammer
// ═══════════════════════════════════════════════════════════════════════════

namespace WLANJammer {

// WiFi channel mapping to NRF24 channels
static const uint8_t WIFI_CH_START[] = {1, 6, 11, 16, 21, 26, 31, 36, 41, 46, 51, 56, 61};
static const uint8_t WIFI_CH_END[] =   {23, 28, 33, 38, 43, 48, 53, 58, 63, 68, 73, 78, 83};
static const uint8_t WIFI_CH_CENTER[] = {12, 17, 22, 27, 32, 37, 42, 47, 52, 57, 62, 67, 72};

#define NUM_WIFI_CHANNELS 13
#define ALL_CHANNELS_MODE 0

// Display constants - INSANE EQUALIZER MODE
#define JAM_GRAPH_X 2
#define JAM_GRAPH_Y 95
#define JAM_GRAPH_WIDTH 236
#define JAM_GRAPH_HEIGHT 140
#define JAM_NUM_BARS 85    // One bar per NRF channel (0-84 = WiFi range)

// Skull row for jammer feedback
#define JAM_SKULL_Y 250
#define JAM_SKULL_NUM 8

static bool jammerActive = false;
static int currentWiFiChannel = ALL_CHANNELS_MODE;
static int currentNRFChannel = 0;
static unsigned long lastHopTime = 0;
static unsigned long lastDisplayTime = 0;
static unsigned long lastScanTime = 0;
static bool exitRequested = false;
static bool uiInitialized = false;

static const int HOP_DELAY_US = 500;
static const int SCAN_INTERVAL_MS = 2000;

static uint8_t signalLevels[13] = {0};
static int jamSkullFrame = 0;
static uint8_t channelHeat[JAM_NUM_BARS] = {0};  // Heat level for each NRF channel - EQUALIZER MODE!

// Skull icons for jammer display
static const unsigned char* jamSkulls[] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};

// Icon bar for jammer - uses start/stop icon
#define JAM_ICON_NUM 3
static int jamIconX[JAM_ICON_NUM] = {90, 170, 10};
static const unsigned char* jamIcons[JAM_ICON_NUM] = {
    bitmap_icon_start,     // Toggle ON/OFF
    bitmap_icon_RIGHT,     // Next channel
    bitmap_icon_go_back    // Back
};

static int jamActiveIcon = -1;
static int jamAnimState = 0;
static unsigned long jamLastAnim = 0;

static void drawJamIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < JAM_ICON_NUM; i++) {
        if (jamIcons[i] != NULL) {
            tft.drawBitmap(jamIconX[i], 20, jamIcons[i], 16, 16, HALEHOUND_MAGENTA);
        }
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static int checkJamIconTouch() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            for (int i = 0; i < JAM_ICON_NUM; i++) {
                if (tx >= jamIconX[i] - 5 && tx <= jamIconX[i] + 21) {
                    if (jamAnimState == 0) {
                        tft.drawBitmap(jamIconX[i], 20, jamIcons[i], 16, 16, TFT_BLACK);
                        jamAnimState = 1;
                        jamActiveIcon = i;
                        jamLastAnim = millis();
                    }
                    return i;
                }
            }
        }
    }
    return -1;
}

static int processJamIconAnim() {
    if (jamAnimState > 0 && millis() - jamLastAnim >= 50) {
        if (jamAnimState == 1) {
            tft.drawBitmap(jamIconX[jamActiveIcon], 20, jamIcons[jamActiveIcon], 16, 16, HALEHOUND_MAGENTA);
            jamAnimState = 2;
            int action = jamActiveIcon;
            jamLastAnim = millis();
            return action;
        } else if (jamAnimState == 2) {
            jamAnimState = 0;
            jamActiveIcon = -1;
        }
    }
    return -1;
}

static void wlanStartCarrier(byte channel) {
    nrfSetTX();                      // Switch to TX mode first!
    nrfSetChannel(channel);
    nrfSetRegister(0x06, 0x9E);      // CONT_WAVE + PLL_LOCK + 0dBm + 2Mbps
    nrfEnable();
}

static void wlanStopCarrier() {
    nrfDisable();
    nrfSetRegister(0x06, 0x0F);  // Normal mode
}

static void drawHeader() {
    tft.fillRect(0, 40, SCREEN_WIDTH, 50, TFT_BLACK);

    drawGlitchText(55, "WLAN JAMMER", &Nosifer_Regular10pt7b);

    if (jammerActive) {
        drawGlitchStatus(72, "JAMMING", HALEHOUND_HOTPINK);
    } else {
        drawGlitchStatus(72, "STANDBY", HALEHOUND_GUNMETAL);
    }

    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 70);
    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        tft.print("Mode: ALL WiFi Channels (1-13)");
    } else {
        int freq = 2407 + (currentWiFiChannel * 5);
        tft.printf("Mode: WiFi Ch %d (%d MHz)", currentWiFiChannel, freq);
    }

    tft.drawLine(0, 85, SCREEN_WIDTH, 85, HALEHOUND_HOTPINK);
}

static void drawChannelDisplay() {
    int channelY = 90;
    tft.fillRect(JAM_GRAPH_X, channelY, JAM_GRAPH_WIDTH, 15, TFT_BLACK);

    tft.setTextSize(1);
    for (int ch = 1; ch <= 13; ch++) {
        int x = JAM_GRAPH_X + ((ch - 1) * JAM_GRAPH_WIDTH / 13);

        if (currentWiFiChannel == ALL_CHANNELS_MODE || currentWiFiChannel == ch) {
            if (jammerActive) {
                tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
            } else {
                tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
            }
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        }

        tft.setCursor(x + 2, channelY);
        if (ch < 10) {
            tft.printf(" %d", ch);
        } else {
            tft.printf("%d", ch);
        }
    }
}

// Forward declaration
static void drawJammerWiFiMarkers();

// Update channel heat levels - EQUALIZER MODE (85 bars!)
static void updateChannelHeat() {
    if (!jammerActive) {
        // Decay all channels when not jamming
        for (int i = 0; i < JAM_NUM_BARS; i++) {
            if (channelHeat[i] > 0) {
                channelHeat[i] = channelHeat[i] / 2;  // Fast decay when stopped
            }
        }
        return;
    }

    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        // ALL CHANNELS MODE - INSANE EQUALIZER
        // All bars dance because we're jamming EVERYTHING
        for (int i = 0; i < JAM_NUM_BARS; i++) {
            int dist = abs(i - currentNRFChannel);

            if (i == currentNRFChannel) {
                // Direct hit - MAX HEAT
                channelHeat[i] = 125;
            } else if (dist <= 6) {
                // Splash zone - strong heat with falloff
                int splash = 110 - (dist * 12);
                channelHeat[i] = (channelHeat[i] + splash) / 2;
            } else {
                // Background chaos - all bars dance randomly
                // Base 50-80 with random variation = visible activity everywhere
                int chaos = 50 + random(40);
                channelHeat[i] = (channelHeat[i] + chaos) / 2;
            }
        }
    } else {
        // SINGLE CHANNEL MODE - focused attack
        int startCh = WIFI_CH_START[currentWiFiChannel - 1];
        int endCh = WIFI_CH_END[currentWiFiChannel - 1];

        for (int i = 0; i < JAM_NUM_BARS; i++) {
            bool isTargeted = (i >= startCh && i <= endCh);
            bool isCurrentChannel = (i == currentNRFChannel);

            if (isCurrentChannel) {
                // Currently being hit - MAX HEAT
                channelHeat[i] = 125;
            } else if (isTargeted) {
                // In target range - keep warm with variation
                int baseHeat = 50 + random(30);
                int dist = abs(i - currentNRFChannel);
                int neighborBoost = (dist <= 2) ? (30 - dist * 10) : 0;
                int targetHeat = baseHeat + neighborBoost;
                channelHeat[i] = (channelHeat[i] + targetHeat) / 2;
            } else {
                // Not targeted - decay
                if (channelHeat[i] > 0) {
                    channelHeat[i] = (channelHeat[i] * 3) / 4;
                }
            }
        }
    }
}

static void drawJammerDisplay() {
    // Update heat levels first
    updateChannelHeat();

    // Clear display area
    tft.fillRect(JAM_GRAPH_X, JAM_GRAPH_Y, JAM_GRAPH_WIDTH, JAM_GRAPH_HEIGHT, TFT_BLACK);

    // Draw frame
    tft.drawRect(JAM_GRAPH_X - 1, JAM_GRAPH_Y - 1, JAM_GRAPH_WIDTH + 2, JAM_GRAPH_HEIGHT + 2, HALEHOUND_MAGENTA);

    int maxBarH = JAM_GRAPH_HEIGHT - 25;

    if (!jammerActive) {
        // Check if any heat remains (for decay animation)
        bool hasHeat = false;
        for (int i = 0; i < JAM_NUM_BARS; i++) {
            if (channelHeat[i] > 3) { hasHeat = true; break; }
        }

        if (!hasHeat) {
            // Fully stopped - show standby bars
            for (int i = 0; i < JAM_NUM_BARS; i++) {
                int x = JAM_GRAPH_X + (i * JAM_GRAPH_WIDTH / JAM_NUM_BARS);
                int barH = 8 + (i % 5) * 2;  // Slight variation
                int barY = JAM_GRAPH_Y + JAM_GRAPH_HEIGHT - barH - 10;
                tft.drawFastVLine(x, barY, barH, HALEHOUND_GUNMETAL);
                tft.drawFastVLine(x + 1, barY, barH, HALEHOUND_GUNMETAL);
            }

            // Standby text
            tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(JAM_GRAPH_X + 85, JAM_GRAPH_Y + 5);
            tft.print("STANDBY");

            // WiFi channel markers
            drawJammerWiFiMarkers();
            return;
        }
    }

    // DRAW THE EQUALIZER - 85 skinny bars of FIRE!
    for (int i = 0; i < JAM_NUM_BARS; i++) {
        int x = JAM_GRAPH_X + (i * JAM_GRAPH_WIDTH / JAM_NUM_BARS);
        uint8_t heat = channelHeat[i];

        // Bar height based on heat - MORE AGGRESSIVE scaling
        int barH = (heat * maxBarH) / 100;  // Taller bars!
        if (barH > maxBarH) barH = maxBarH;
        if (barH < 8) barH = 8;  // Higher minimum

        int barY = JAM_GRAPH_Y + JAM_GRAPH_HEIGHT - barH - 8;

        // Color based on heat - vibrant gradient from cyan to hot pink
        for (int y = 0; y < barH; y++) {
            float heightRatio = (float)y / (float)barH;
            float heatRatio = (float)heat / 125.0f;

            // More aggressive color gradient
            float ratio = heightRatio * (0.3f + heatRatio * 0.7f);
            if (ratio > 1.0f) ratio = 1.0f;

            // Cyan (0, 207, 255) -> Hot Pink (255, 28, 82)
            uint8_t r = (uint8_t)(ratio * 255);
            uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
            uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
            uint16_t color = tft.color565(r, g, b);

            tft.drawFastHLine(x, barY + barH - 1 - y, 2, color);
        }

        // Add glow effect at base for hot bars
        if (heat > 80) {
            tft.drawFastHLine(x, barY + barH, 2, HALEHOUND_HOTPINK);
            tft.drawFastHLine(x, barY + barH + 1, 2, tft.color565(128, 14, 41));
        }
    }

    // WiFi channel markers
    drawJammerWiFiMarkers();

    // Current frequency display
    if (jammerActive) {
        tft.fillRect(JAM_GRAPH_X + 50, JAM_GRAPH_Y + 2, 140, 12, TFT_BLACK);
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(JAM_GRAPH_X + 55, JAM_GRAPH_Y + 3);
        tft.printf(">>> %d MHz <<<", 2400 + currentNRFChannel);
    }
}

// Draw WiFi channel markers below the equalizer
static void drawJammerWiFiMarkers() {
    int markerY = JAM_GRAPH_Y + JAM_GRAPH_HEIGHT - 8;

    // Draw markers for channels 1, 6, 11, 13
    int x1 = JAM_GRAPH_X + (12 * JAM_GRAPH_WIDTH / JAM_NUM_BARS);
    int x6 = JAM_GRAPH_X + (37 * JAM_GRAPH_WIDTH / JAM_NUM_BARS);
    int x11 = JAM_GRAPH_X + (62 * JAM_GRAPH_WIDTH / JAM_NUM_BARS);
    int x13 = JAM_GRAPH_X + (72 * JAM_GRAPH_WIDTH / JAM_NUM_BARS);

    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x1 - 2, markerY);
    tft.print("1");
    tft.setCursor(x6 - 2, markerY);
    tft.print("6");
    tft.setCursor(x11 - 4, markerY);
    tft.print("11");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(x13 - 4, markerY);
    tft.print("13");
}

static void drawJammerSkulls() {
    // Skull row at bottom - visual feedback
    int skullStartX = 10;
    int skullSpacing = 28;

    for (int i = 0; i < JAM_SKULL_NUM; i++) {
        int x = skullStartX + (i * skullSpacing);
        tft.fillRect(x, JAM_SKULL_Y, 16, 16, TFT_BLACK);

        uint16_t color;
        if (jammerActive) {
            // Animated wave when jamming - hot pink to cyan
            int phase = (jamSkullFrame + i) % 8;
            if (phase < 4) {
                float ratio = phase / 3.0f;
                uint8_t r = (uint8_t)(ratio * 255);
                uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
                uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
                color = tft.color565(r, g, b);
            } else {
                float ratio = (phase - 4) / 3.0f;
                uint8_t r = 255 - (uint8_t)(ratio * 255);
                uint8_t g = 28 + (uint8_t)(ratio * (207 - 28));
                uint8_t b = 82 + (uint8_t)(ratio * (255 - 82));
                color = tft.color565(r, g, b);
            }
        } else {
            color = HALEHOUND_GUNMETAL;  // Gray when inactive
        }

        tft.drawBitmap(x, JAM_SKULL_Y, jamSkulls[i], 16, 16, color);
    }

    // Status text next to skulls
    tft.fillRect(skullStartX + (JAM_SKULL_NUM * skullSpacing), JAM_SKULL_Y, 50, 16, TFT_BLACK);
    tft.setTextColor(jammerActive ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(skullStartX + (JAM_SKULL_NUM * skullSpacing) + 5, JAM_SKULL_Y + 4);
    tft.print(jammerActive ? "TX!" : "OFF");

    jamSkullFrame++;
}

static void startJamming() {
    byte status = nrfGetRegister(0x07);
    if (status == 0x00 || status == 0xFF) {
        nrfDisable();
        nrfPowerUp();
        nrfSetRegister(0x01, 0x0);
        nrfSetRegister(0x06, 0x0F);

        status = nrfGetRegister(0x07);
        if (status == 0x00 || status == 0xFF) {
            tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
            tft.setCursor(10, 150);
            tft.print("ERROR: NRF24 not found!");
            return;
        }
    }

    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        currentNRFChannel = WIFI_CH_START[0];
    } else {
        currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
    }

    wlanStartCarrier(currentNRFChannel);
    jammerActive = true;
    lastHopTime = micros();
}

static void stopJamming() {
    wlanStopCarrier();
    nrfPowerDown();
    jammerActive = false;
}

static void hopChannel() {
    if (!jammerActive) return;

    if (currentWiFiChannel == ALL_CHANNELS_MODE) {
        currentNRFChannel++;
        if (currentNRFChannel > 83) {
            currentNRFChannel = 1;
        }
    } else {
        currentNRFChannel++;
        if (currentNRFChannel > WIFI_CH_END[currentWiFiChannel - 1]) {
            currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
        }
    }

    nrfSetChannel(currentNRFChannel);
}

void wlanjammerSetup() {
    exitRequested = false;
    jammerActive = false;
    currentWiFiChannel = ALL_CHANNELS_MODE;
    currentNRFChannel = 0;
    jamAnimState = 0;
    jamActiveIcon = -1;
    jamSkullFrame = 0;
    memset(channelHeat, 0, sizeof(channelHeat));

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawJamIconBar();

    if (!nrfInit()) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setTextSize(2);
        drawCenteredText(100, "NRF24 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check wiring:", HALEHOUND_MAGENTA, 1);
        drawCenteredText(145, "CE=GPIO16 CSN=GPIO4", HALEHOUND_MAGENTA, 1);
        uiInitialized = false;
        return;
    }

    memset(signalLevels, 0, sizeof(signalLevels));

    drawHeader();
    drawChannelDisplay();
    drawJammerDisplay();
    drawJammerSkulls();

    lastDisplayTime = millis();
    lastScanTime = millis();
    uiInitialized = true;

    #if CYD_DEBUG
    Serial.println("[WLANJAMMER] NRF24 initialized successfully");
    #endif
}

void wlanjammerLoop() {
    if (!uiInitialized) {
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }
        return;
    }

    touchButtonsUpdate();

    // Direct touch check with touch release handling
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            // Back icon (x=10)
            if (tx < 40) {
                if (jammerActive) stopJamming();
                exitRequested = true;
                return;
            }
            // Toggle icon (x=90)
            if (tx >= 80 && tx < 120) {
                // Wait for touch release
                while (isTouched()) { delay(10); }
                delay(200);
                if (jammerActive) stopJamming(); else startJamming();
                drawHeader();
                drawChannelDisplay();
                return;
            }
            // Next channel icon (x=170)
            if (tx >= 160 && tx < 200) {
                // Wait for touch release
                while (isTouched()) { delay(10); }
                delay(200);
                currentWiFiChannel++;
                if (currentWiFiChannel > 13) currentWiFiChannel = ALL_CHANNELS_MODE;
                if (jammerActive) {
                    if (currentWiFiChannel == ALL_CHANNELS_MODE) currentNRFChannel = 1;
                    else currentNRFChannel = WIFI_CH_START[currentWiFiChannel - 1];
                }
                drawHeader();
                drawChannelDisplay();
                return;
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (jammerActive) stopJamming();
        exitRequested = true;
        return;
    }

    // Rapid channel hopping
    if (jammerActive) {
        if (micros() - lastHopTime >= HOP_DELAY_US) {
            hopChannel();
            lastHopTime = micros();
        }
    }

    // Update display
    if (millis() - lastDisplayTime >= 80) {
        drawJammerDisplay();
        drawJammerSkulls();
        lastDisplayTime = millis();
    }
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    if (jammerActive) {
        stopJamming();
    }
    exitRequested = false;
    uiInitialized = false;
    nrfPowerDown();
}

}  // namespace WLANJammer


// ═══════════════════════════════════════════════════════════════════════════
// PROTO KILL - Multi-Protocol Jammer
// ═══════════════════════════════════════════════════════════════════════════

namespace ProtoKill {

enum OperationMode {
    BLE_MODULE,
    Bluetooth_MODULE,
    WiFi_MODULE,
    VIDEO_TX_MODULE,
    RC_MODULE,
    USB_WIRELESS_MODULE,
    ZIGBEE_MODULE,
    NRF24_MODULE
};

static OperationMode currentMode = WiFi_MODULE;
static bool jammerActive = false;
static bool exitRequested = false;
static bool uiInitialized = false;

// Channel arrays for different protocols
static const byte bluetooth_channels[] = {32, 34, 46, 48, 50, 52, 0, 1, 2, 4, 6, 8, 22, 24, 26, 28, 30, 74, 76, 78, 80};
static const byte ble_channels[] = {2, 26, 80};
static const byte WiFi_channels[] = {
    2, 7, 12, 17, 22,     // WiFi Ch 1 (2412MHz ±11MHz)
    27, 32, 37, 42, 47,   // WiFi Ch 6 (2437MHz ±11MHz)
    52, 57, 62, 67, 72    // WiFi Ch 11 (2462MHz ±11MHz)
};
static const byte usbWireless_channels[] = {40, 50, 60};
static const byte videoTransmitter_channels[] = {70, 75, 80};
static const byte rc_channels[] = {10, 30, 50, 70};
static const byte zigbee_channels[] = {5, 25, 50, 75};
static const byte nrf24_channels[] = {76, 78, 79};

#define PK_LINE_HEIGHT 12
#define PK_MAX_LINES 15

static String pkBuffer[PK_MAX_LINES];
static uint16_t pkBufferColor[PK_MAX_LINES];
static int pkIndex = 0;

// Icon bar for proto kill
#define PK_ICON_NUM 4
static int pkIconX[PK_ICON_NUM] = {50, 130, 170, 10};
static const unsigned char* pkIcons[PK_ICON_NUM] = {
    bitmap_icon_start,     // Toggle ON/OFF
    bitmap_icon_RIGHT,     // Mode +
    bitmap_icon_LEFT,      // Mode -
    bitmap_icon_go_back    // Back
};

static int pkActiveIcon = -1;
static int pkAnimState = 0;
static unsigned long pkLastAnim = 0;

static void drawPkIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < PK_ICON_NUM; i++) {
        if (pkIcons[i] != NULL) {
            tft.drawBitmap(pkIconX[i], 20, pkIcons[i], 16, 16, HALEHOUND_MAGENTA);
        }
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static int checkPkIconTouch() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            for (int i = 0; i < PK_ICON_NUM; i++) {
                if (tx >= pkIconX[i] - 5 && tx <= pkIconX[i] + 21) {
                    if (pkAnimState == 0) {
                        tft.drawBitmap(pkIconX[i], 20, pkIcons[i], 16, 16, TFT_BLACK);
                        pkAnimState = 1;
                        pkActiveIcon = i;
                        pkLastAnim = millis();
                    }
                    return i;
                }
            }
        }
    }
    return -1;
}

static int processPkIconAnim() {
    if (pkAnimState > 0 && millis() - pkLastAnim >= 50) {
        if (pkAnimState == 1) {
            tft.drawBitmap(pkIconX[pkActiveIcon], 20, pkIcons[pkActiveIcon], 16, 16, HALEHOUND_MAGENTA);
            pkAnimState = 2;
            int action = pkActiveIcon;
            pkLastAnim = millis();
            return action;
        } else if (pkAnimState == 2) {
            pkAnimState = 0;
            pkActiveIcon = -1;
        }
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// PROTO KILL - Complete UI Redesign
// 85-bar equalizer, big protocol display, no diagnostic log clutter
// ═══════════════════════════════════════════════════════════════════════════

static const char* getModeString(OperationMode mode) {
    switch (mode) {
        case BLE_MODULE:          return "BLE";
        case Bluetooth_MODULE:    return "BLUETOOTH";
        case WiFi_MODULE:         return "WIFI";
        case USB_WIRELESS_MODULE: return "USB";
        case VIDEO_TX_MODULE:     return "VIDEO TX";
        case RC_MODULE:           return "RC";
        case ZIGBEE_MODULE:       return "ZIGBEE";
        case NRF24_MODULE:        return "NRF24";
        default:                  return "UNKNOWN";
    }
}

// Start carrier wave on specific channel (proper TX mode)
static void pkStartCarrier(byte channel) {
    nrfSetTX();                       // Switch to TX mode FIRST!
    nrfSetChannel(channel);
    nrfSetRegister(0x06, 0x9E);       // CONT_WAVE + PLL_LOCK + 0dBm + 2Mbps
    nrfEnable();
}

static void pkStopCarrier() {
    nrfDisable();
    nrfSetRegister(0x06, 0x0F);       // Normal mode
}

static void initRadio() {
    if (jammerActive) {
        nrfDisable();
        nrfPowerUp();
        nrfSetRegister(0x01, 0x0);     // Disable auto-ack
        nrfSetTX();                    // Switch to TX mode!
        nrfSetRegister(0x06, 0x9E);    // Constant carrier mode
    } else {
        pkStopCarrier();
        nrfPowerDown();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// EQUALIZER SYSTEM - 85 bars like WLAN Jammer
// ═══════════════════════════════════════════════════════════════════════════

#define PK_NUM_BARS      85
#define PK_GRAPH_X       5
#define PK_GRAPH_Y       160
#define PK_GRAPH_WIDTH   230
#define PK_GRAPH_HEIGHT  155

static uint8_t pkChannelHeat[PK_NUM_BARS] = {0};
static int pkCurrentChannel = 0;
static int pkHitCount = 0;
static unsigned long pkLastUpdate = 0;

// Get channel array and size for current protocol
static void getProtocolChannels(OperationMode mode, const byte** channels, int* numChannels) {
    switch (mode) {
        case BLE_MODULE:
            *channels = ble_channels;
            *numChannels = sizeof(ble_channels);
            break;
        case Bluetooth_MODULE:
            *channels = bluetooth_channels;
            *numChannels = sizeof(bluetooth_channels);
            break;
        case WiFi_MODULE:
            *channels = WiFi_channels;
            *numChannels = sizeof(WiFi_channels);
            break;
        case USB_WIRELESS_MODULE:
            *channels = usbWireless_channels;
            *numChannels = sizeof(usbWireless_channels);
            break;
        case VIDEO_TX_MODULE:
            *channels = videoTransmitter_channels;
            *numChannels = sizeof(videoTransmitter_channels);
            break;
        case RC_MODULE:
            *channels = rc_channels;
            *numChannels = sizeof(rc_channels);
            break;
        case ZIGBEE_MODULE:
            *channels = zigbee_channels;
            *numChannels = sizeof(zigbee_channels);
            break;
        case NRF24_MODULE:
            *channels = nrf24_channels;
            *numChannels = sizeof(nrf24_channels);
            break;
        default:
            *channels = WiFi_channels;
            *numChannels = sizeof(WiFi_channels);
    }
}

// Update heat levels for equalizer
static void updatePkHeat() {
    if (!jammerActive) {
        // Decay when not jamming
        for (int i = 0; i < PK_NUM_BARS; i++) {
            if (pkChannelHeat[i] > 0) {
                pkChannelHeat[i] = pkChannelHeat[i] / 2;
            }
        }
        return;
    }

    // Get channels for current protocol
    const byte* channels;
    int numChannels;
    getProtocolChannels(currentMode, &channels, &numChannels);

    // All protocol channels get activity
    for (int i = 0; i < PK_NUM_BARS; i++) {
        int dist = abs(i - pkCurrentChannel);

        if (i == pkCurrentChannel) {
            // Direct hit - MAX HEAT
            pkChannelHeat[i] = 125;
        } else if (dist <= 5) {
            // Splash zone
            int splash = 100 - (dist * 15);
            pkChannelHeat[i] = (pkChannelHeat[i] + splash) / 2;
        } else {
            // Check if this bar is near any protocol channel
            bool nearTarget = false;
            for (int c = 0; c < numChannels; c++) {
                if (abs(i - channels[c]) <= 3) {
                    nearTarget = true;
                    break;
                }
            }
            if (nearTarget) {
                // Background activity on protocol channels
                int chaos = 40 + random(30);
                pkChannelHeat[i] = (pkChannelHeat[i] + chaos) / 2;
            } else {
                // Decay non-target areas
                if (pkChannelHeat[i] > 0) {
                    pkChannelHeat[i] = (pkChannelHeat[i] * 3) / 4;
                }
            }
        }
    }
}

// Draw protocol channel markers at bottom of equalizer
static void drawPkChannelMarkers() {
    const byte* channels;
    int numChannels;
    getProtocolChannels(currentMode, &channels, &numChannels);

    int markerY = PK_GRAPH_Y + PK_GRAPH_HEIGHT - 8;

    for (int c = 0; c < numChannels; c++) {
        int x = PK_GRAPH_X + (channels[c] * PK_GRAPH_WIDTH / PK_NUM_BARS);
        tft.drawFastVLine(x, markerY, 6, HALEHOUND_MAGENTA);
        tft.drawFastVLine(x + 1, markerY, 6, HALEHOUND_MAGENTA);
    }
}

// Draw the full equalizer display
static void drawPkEqualizer() {
    updatePkHeat();

    // Clear display area
    tft.fillRect(PK_GRAPH_X, PK_GRAPH_Y, PK_GRAPH_WIDTH, PK_GRAPH_HEIGHT, TFT_BLACK);

    // Draw frame
    tft.drawRect(PK_GRAPH_X - 1, PK_GRAPH_Y - 1, PK_GRAPH_WIDTH + 2, PK_GRAPH_HEIGHT + 2, HALEHOUND_MAGENTA);

    int maxBarH = PK_GRAPH_HEIGHT - 20;

    if (!jammerActive) {
        // Check for remaining heat (decay animation)
        bool hasHeat = false;
        for (int i = 0; i < PK_NUM_BARS; i++) {
            if (pkChannelHeat[i] > 3) { hasHeat = true; break; }
        }

        if (!hasHeat) {
            // Standby - show idle bars
            for (int i = 0; i < PK_NUM_BARS; i++) {
                int x = PK_GRAPH_X + (i * PK_GRAPH_WIDTH / PK_NUM_BARS);
                int barH = 6 + (i % 4) * 2;
                int barY = PK_GRAPH_Y + PK_GRAPH_HEIGHT - barH - 12;
                tft.drawFastVLine(x, barY, barH, HALEHOUND_GUNMETAL);
                tft.drawFastVLine(x + 1, barY, barH, HALEHOUND_GUNMETAL);
            }

            // Standby text
            tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(2);
            tft.setCursor(PK_GRAPH_X + 65, PK_GRAPH_Y + 50);
            tft.print("STANDBY");
            tft.setTextSize(1);

            drawPkChannelMarkers();
            return;
        }
    }

    // DRAW THE EQUALIZER - 85 bars of FIRE!
    for (int i = 0; i < PK_NUM_BARS; i++) {
        int x = PK_GRAPH_X + (i * PK_GRAPH_WIDTH / PK_NUM_BARS);
        uint8_t heat = pkChannelHeat[i];

        // Bar height based on heat
        int barH = (heat * maxBarH) / 100;
        if (barH > maxBarH) barH = maxBarH;
        if (barH < 6) barH = 6;

        int barY = PK_GRAPH_Y + PK_GRAPH_HEIGHT - barH - 12;

        // Color gradient from cyan to hot pink
        for (int y = 0; y < barH; y++) {
            float heightRatio = (float)y / (float)barH;
            float heatRatio = (float)heat / 125.0f;
            float ratio = heightRatio * (0.3f + heatRatio * 0.7f);
            if (ratio > 1.0f) ratio = 1.0f;

            // Cyan -> Hot Pink gradient
            uint8_t r = (uint8_t)(ratio * 255);
            uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
            uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
            uint16_t color = tft.color565(r, g, b);

            tft.drawFastHLine(x, barY + barH - 1 - y, 2, color);
        }

        // Glow at base for hot bars
        if (heat > 80) {
            tft.drawFastHLine(x, barY + barH, 2, HALEHOUND_HOTPINK);
            tft.drawFastHLine(x, barY + barH + 1, 2, tft.color565(128, 14, 41));
        }
    }

    // Channel markers
    drawPkChannelMarkers();
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN UI DRAWING - FreeMonoBold fonts for terminal/hacker look
// ═══════════════════════════════════════════════════════════════════════════

// Helper to draw centered FreeFont text
static void pkDrawFreeFont(int y, const char* text, uint16_t color, const GFXfont* font) {
    tft.setFreeFont(font);
    tft.setTextColor(color, TFT_BLACK);

    // Calculate width for centering using textWidth()
    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;

    tft.setCursor(x, y);
    tft.print(text);

    // Reset to default font
    tft.setFreeFont(NULL);
}

static void drawPkMainUI() {
    // Clear main area
    tft.fillRect(0, 38, SCREEN_WIDTH, 122, TFT_BLACK);

    // Title line
    tft.drawLine(0, 38, SCREEN_WIDTH, 38, HALEHOUND_HOTPINK);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(75, 45);
    tft.print("PROTO KILL");
    tft.drawLine(0, 56, SCREEN_WIDTH, 56, HALEHOUND_HOTPINK);

    // Rounded frame for main content
    tft.drawRoundRect(10, 60, 220, 70, 8, HALEHOUND_VIOLET);
    tft.drawRoundRect(11, 61, 218, 68, 7, HALEHOUND_GUNMETAL);

    // Protocol Name - Nosifer18pt with glitch effect
    tft.fillRect(15, 65, 210, 30, TFT_BLACK);
    drawGlitchTitle(90, getModeString(currentMode));

    // Status - Nosifer12pt
    tft.fillRect(15, 100, 210, 25, TFT_BLACK);
    if (jammerActive) {
        drawGlitchStatus(120, "JAMMING", HALEHOUND_HOTPINK);
    } else {
        drawGlitchStatus(120, "STANDBY", HALEHOUND_GUNMETAL);
    }

    // Stats line - default font
    tft.setFreeFont(NULL);
    tft.fillRect(0, 135, SCREEN_WIDTH, 20, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(35, 140);
    tft.printf("CH: %03d", pkCurrentChannel);
    tft.setCursor(130, 140);
    tft.printf("HITS: %d", pkHitCount);

    // Separator before equalizer
    tft.drawLine(0, 155, SCREEN_WIDTH, 155, HALEHOUND_HOTPINK);
}

static void updatePkStats() {
    // Only update stats line (fast partial update)
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);

    // Channel
    tft.fillRect(35, 140, 70, 10, TFT_BLACK);
    tft.setCursor(35, 140);
    tft.printf("CH: %03d", pkCurrentChannel);

    // Hits
    tft.fillRect(130, 140, 100, 10, TFT_BLACK);
    tft.setCursor(130, 140);
    tft.printf("HITS: %d", pkHitCount);
}

static void updatePkStatus() {
    // Clear inside the frame and redraw
    tft.fillRect(15, 65, 210, 60, TFT_BLACK);

    // Protocol Name
    drawGlitchTitle(90, getModeString(currentMode));

    // Status
    if (jammerActive) {
        drawGlitchStatus(120, "JAMMING", HALEHOUND_HOTPINK);
    } else {
        drawGlitchStatus(120, "STANDBY", HALEHOUND_GUNMETAL);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP AND LOOP
// ═══════════════════════════════════════════════════════════════════════════

void prokillSetup() {
    exitRequested = false;
    jammerActive = false;
    currentMode = WiFi_MODULE;
    pkHitCount = 0;
    pkCurrentChannel = 0;

    // Clear heat
    for (int i = 0; i < PK_NUM_BARS; i++) {
        pkChannelHeat[i] = 0;
    }

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawPkIconBar();

    if (!nrfInit()) {
        tft.setTextSize(2);
        drawCenteredText(100, "NRF24 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check wiring:", HALEHOUND_MAGENTA, 1);
        drawCenteredText(145, "CE=GPIO16 CSN=GPIO4", HALEHOUND_MAGENTA, 1);
        uiInitialized = false;
        return;
    }

    drawPkMainUI();
    drawPkEqualizer();

    uiInitialized = true;

    #if CYD_DEBUG
    Serial.println("[PROTOKILL] NRF24 initialized successfully");
    #endif
}

void prokillLoop() {
    if (!uiInitialized) {
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }
        return;
    }

    touchButtonsUpdate();

    // Touch handling
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 40) {
            // Back icon (x=10)
            if (tx < 40) {
                jammerActive = false;
                nrfPowerDown();
                exitRequested = true;
                return;
            }
            // Toggle icon (x=50)
            if (tx >= 40 && tx < 100) {
                jammerActive = !jammerActive;
                if (jammerActive) pkHitCount = 0;
                initRadio();
                updatePkStatus();
                while (isTouched()) { delay(10); }
            }
            // Mode + icon (x=130)
            if (tx >= 100 && tx < 170) {
                currentMode = static_cast<OperationMode>((currentMode + 1) % 8);
                if (jammerActive) initRadio();
                updatePkStatus();
                while (isTouched()) { delay(10); }
            }
            // Mode - icon (x=170)
            if (tx >= 170) {
                currentMode = static_cast<OperationMode>((currentMode == 0) ? 7 : (currentMode - 1));
                if (jammerActive) initRadio();
                updatePkStatus();
                while (isTouched()) { delay(10); }
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        jammerActive = false;
        nrfPowerDown();
        exitRequested = true;
        return;
    }

    // Channel hopping when jamming
    if (jammerActive) {
        const byte* channels;
        int numChannels;
        getProtocolChannels(currentMode, &channels, &numChannels);

        int randomIndex = random(0, numChannels);
        pkCurrentChannel = channels[randomIndex];

        pkStartCarrier(pkCurrentChannel);
        delayMicroseconds(500);

        pkHitCount++;
    }

    // Update display at 30ms intervals
    if (millis() - pkLastUpdate >= 30) {
        pkLastUpdate = millis();
        updatePkStats();
        drawPkEqualizer();
    }
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    jammerActive = false;
    exitRequested = false;
    uiInitialized = false;
    nrfPowerDown();
}

}  // namespace ProtoKill
