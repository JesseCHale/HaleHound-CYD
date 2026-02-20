// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Main Firmware
// ESP32 Cheap Yellow Display Edition
// Matches ESP32-DIV HaleHound v2.7.0 EXACTLY
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════
//
// Hardware:
//   - CYD ESP32-2432S028 (2.8") or ESP32-3248S035 (3.5")
//   - CC1101 SubGHz Radio (Optional - shows stub if not available)
//   - NRF24L01+PA+LNA 2.4GHz Radio (Optional - shows stub if not available)
//   - LiPo Battery + USB-C Boost Converter
//
// Based on ESP32-DIV HaleHound Edition by Jesse (JesseCHale)
// GitHub: github.com/JesseCHale/ESP32-DIV
//
// ═══════════════════════════════════════════════════════════════════════════

#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <BLEDevice.h>

// HaleHound-CYD modules
#include "cyd_config.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "spi_manager.h"
#include "subconfig.h"
#include "nrf24_config.h"
#include "icon.h"
#include "skull_bg.h"

// Attack modules
#include "wifi_attacks.h"
#include "bluetooth_attacks.h"
#include "subghz_attacks.h"
#include "nrf24_attacks.h"
#include "gps_module.h"
#include "serial_monitor.h"
#include "firmware_update.h"
#include "wardriving_screen.h"
#include "eapol_capture.h"
#include "karma_attack.h"
#include "saved_captures.h"
#include "radio_test.h"

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

TFT_eSPI tft = TFT_eSPI();

// Menu state - matches original ESP32-DIV
int current_menu_index = 0;
int current_submenu_index = 0;
bool in_sub_menu = false;
bool feature_active = false;
bool submenu_initialized = false;
bool is_main_menu = false;
bool feature_exit_requested = false;

int last_menu_index = -1;
int last_submenu_index = -1;
bool menu_initialized = false;

unsigned long last_interaction_time = 0;

// ═══════════════════════════════════════════════════════════════════════════
// MENU DEFINITIONS - EXACT MATCH TO ORIGINAL ESP32-DIV
// ═══════════════════════════════════════════════════════════════════════════

const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "Bluetooth",
    "2.4GHz",
    "SubGHz",
    "SIGINT",
    "Tools",
    "Setting",
    "About"
};

const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_skull_wifi,
    bitmap_icon_skull_bluetooth,
    bitmap_icon_skull_jammer,
    bitmap_icon_skull_subghz,
    bitmap_icon_skull_ir,
    bitmap_icon_skull_tools,
    bitmap_icon_skull_setting,
    bitmap_icon_skull_about
};

// WiFi Submenu - 9 items
const int NUM_SUBMENU_ITEMS = 9;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Sniffer",
    "WiFi Scanner",
    "Captive Portal",
    "Station Scanner",
    "Auth Flood",
    "Back to Main Menu"
};

const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_skull_wifi,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_graph,
    bitmap_icon_nuke,
    bitmap_icon_go_back
};

// Bluetooth Submenu - 8 items
const int bluetooth_NUM_SUBMENU_ITEMS = 8;
const char *bluetooth_submenu_items[bluetooth_NUM_SUBMENU_ITEMS] = {
    "BLE Jammer",
    "BLE Spoofer",
    "BLE Beacon",
    "Sniffer",
    "BLE Scanner",
    "WhisperPair",
    "AirTag Detect",
    "Back to Main Menu"
};

const unsigned char *bluetooth_submenu_icons[bluetooth_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_ble_jammer,
    bitmap_icon_spoofer,
    bitmap_icon_signal,
    bitmap_icon_analyzer,
    bitmap_icon_graph,
    bitmap_icon_eye,
    bitmap_icon_apple,
    bitmap_icon_go_back
};

// NRF24 2.4GHz Submenu - 5 items
const int nrf_NUM_SUBMENU_ITEMS = 5;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Spectrum Analyzer",
    "WLAN Jammer",
    "Proto Kill",
    "Back to Main Menu"
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,
    bitmap_icon_analyzer,
    bitmap_icon_wifi_jammer,
    bitmap_icon_skull_jammer,  // Proto Kill - skull jammer icon
    bitmap_icon_go_back
};

// SubGHz Submenu - 5 items
const int subghz_NUM_SUBMENU_ITEMS = 6;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "Brute Force",
    "SubGHz Jammer",
    "Spectrum Analyzer",
    "Saved Profile",
    "Back to Main Menu"
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,
    bitmap_icon_skull_subghz,
    bitmap_icon_no_signal,
    bitmap_icon_analyzer,
    bitmap_icon_list,
    bitmap_icon_go_back
};

// SIGINT Submenu - 5 items
const int sigint_NUM_SUBMENU_ITEMS = 5;
const char *sigint_submenu_items[sigint_NUM_SUBMENU_ITEMS] = {
    "EAPOL Capture",
    "Karma Attack",
    "Wardriving",
    "Saved Captures",
    "Back to Main Menu"
};

const unsigned char *sigint_submenu_icons[sigint_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_key,
    bitmap_icon_spoofer,
    bitmap_icon_follow,
    bitmap_icon_floppy2,
    bitmap_icon_go_back
};

// Tools Submenu - 6 items
const int tools_NUM_SUBMENU_ITEMS = 6;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Monitor",
    "Update Firmware",
    "Touch Calibrate",
    "GPS",
    "Radio Test",
    "Back to Main Menu"
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,
    bitmap_icon_follow,
    bitmap_icon_stat,
    bitmap_icon_antenna,
    bitmap_icon_signal,
    bitmap_icon_go_back
};

// Settings Submenu - 8 items
const int settings_NUM_SUBMENU_ITEMS = 8;
const char *settings_submenu_items[settings_NUM_SUBMENU_ITEMS] = {
    "Brightness",
    "Screen Timeout",
    "Swap Colors",
    "Invert Display",
    "Color Mode",
    "Rotation",
    "Device Info",
    "Back to Main Menu"
};

const unsigned char *settings_submenu_icons[settings_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_led,
    bitmap_icon_eye2,
    bitmap_icon_led,
    bitmap_icon_led,
    bitmap_icon_led,
    bitmap_icon_follow,
    bitmap_icon_stat,
    bitmap_icon_go_back
};

// About Submenu - 1 item
const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

// Active submenu pointers
const char **active_submenu_items = nullptr;
int active_submenu_size = 0;
const unsigned char **active_submenu_icons = nullptr;

// Settings
int brightness_level = 255;
int screen_timeout_seconds = 60;
bool screen_asleep = false;
bool color_order_rgb = false;  // false = BGR (default), true = RGB (swapped panels)
bool display_inverted = false; // false = normal, true = inverted (for 2USB/inverted panels)
uint8_t color_mode = 0;       // 0 = Default, 1 = Colorblind, 2 = High Contrast
uint8_t screen_rotation = 0;  // 0 = Standard (USB down), 2 = Flipped 180 (USB up)

// Timeout option tables
const int timeoutOptions[] = {30, 60, 120, 300, 600, 0};
const char* timeoutLabels[] = {"30 SEC", "1 MIN", "2 MIN", "5 MIN", "10 MIN", "NEVER"};
const int numTimeoutOptions = 6;

// ═══════════════════════════════════════════════════════════════════════════
// MENU LAYOUT CONSTANTS - MATCHES ORIGINAL
// ═══════════════════════════════════════════════════════════════════════════

const int COLUMN_WIDTH = 120;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 30;
const int Y_SPACING = 75;

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR HELPER - MATCHES ORIGINAL HALEHOUND
// ═══════════════════════════════════════════════════════════════════════════

#define INO_ICON_SIZE 16

// Draw simple icon bar with back icon - MATCHES ORIGINAL HALEHOUND
void drawInoIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, INO_ICON_SIZE, INO_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Check if back icon was tapped (y=20-36, x=10-26)
bool isInoBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 26) {
            delay(150);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// UPDATE ACTIVE SUBMENU
// ═══════════════════════════════════════════════════════════════════════════

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0: // WiFi
            active_submenu_items = submenu_items;
            active_submenu_size = NUM_SUBMENU_ITEMS;
            active_submenu_icons = wifi_submenu_icons;
            break;
        case 1: // Bluetooth
            active_submenu_items = bluetooth_submenu_items;
            active_submenu_size = bluetooth_NUM_SUBMENU_ITEMS;
            active_submenu_icons = bluetooth_submenu_icons;
            break;
        case 2: // 2.4GHz (NRF)
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 3: // SubGHz
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 4: // SIGINT
            active_submenu_items = sigint_submenu_items;
            active_submenu_size = sigint_NUM_SUBMENU_ITEMS;
            active_submenu_icons = sigint_submenu_icons;
            break;
        case 5: // Tools
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            break;
        case 6: // Settings
            active_submenu_items = settings_submenu_items;
            active_submenu_size = settings_NUM_SUBMENU_ITEMS;
            active_submenu_icons = settings_submenu_icons;
            break;
        case 7: // About
            active_submenu_items = about_submenu_items;
            active_submenu_size = about_NUM_SUBMENU_ITEMS;
            active_submenu_icons = about_submenu_icons;
            break;
        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY SUBMENU - MATCHES ORIGINAL STYLE
// ═══════════════════════════════════════════════════════════════════════════

void displaySubmenu() {
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);
    tft.setTextSize(1);

    if (!submenu_initialized) {
        tft.fillScreen(TFT_BLACK);

        for (int i = 0; i < active_submenu_size; i++) {
            int yPos = 30 + i * 30;
            if (i == active_submenu_size - 1) yPos += 10;

            tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
            tft.drawBitmap(10, yPos, active_submenu_icons[i], 16, 16, HALEHOUND_MAGENTA);
            tft.setCursor(30, yPos);
            if (i < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[i]);
        }

        submenu_initialized = true;
        last_submenu_index = -1;
    }

    // Highlight current selection
    if (last_submenu_index != current_submenu_index) {
        // Unhighlight previous
        if (last_submenu_index >= 0) {
            int prev_yPos = 30 + last_submenu_index * 30;
            if (last_submenu_index == active_submenu_size - 1) prev_yPos += 10;

            tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
            tft.drawBitmap(10, prev_yPos, active_submenu_icons[last_submenu_index], 16, 16, HALEHOUND_MAGENTA);
            tft.setCursor(30, prev_yPos);
            if (last_submenu_index < active_submenu_size - 1) {
                tft.print("| ");
            }
            tft.print(active_submenu_items[last_submenu_index]);
        }

        // Highlight current
        int new_yPos = 30 + current_submenu_index * 30;
        if (current_submenu_index == active_submenu_size - 1) new_yPos += 10;

        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        tft.drawBitmap(10, new_yPos, active_submenu_icons[current_submenu_index], 16, 16, HALEHOUND_HOTPINK);
        tft.setCursor(30, new_yPos);
        if (current_submenu_index < active_submenu_size - 1) {
            tft.print("| ");
        }
        tft.print(active_submenu_items[current_submenu_index]);

        last_submenu_index = current_submenu_index;
    }

    drawStatusBar();
}

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY MAIN MENU - MATCHES ORIGINAL WITH SKULL BACKGROUND
// ═══════════════════════════════════════════════════════════════════════════

void displayMenu() {
    submenu_initialized = false;
    last_submenu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        // Black background with skull in magenta
        tft.fillScreen(TFT_BLACK);

        // Flaming skulls watermark - pushed down behind menu
        tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x2945);  // Dark cyan watermark

        // Draw menu buttons
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            // Button - icon and text only, no border
            tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[i], 16, 16, HALEHOUND_MAGENTA);

            tft.setTextColor(HALEHOUND_MAGENTA);
            int textWidth = 6 * strlen(menu_items[i]);
            int textX = x_position + (100 - textWidth) / 2;
            int textY = y_position + 30;
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    // Highlight current selection
    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) {
                // Deselected - redraw icon in cyan (no border)
                tft.fillRoundRect(x_position, y_position, 100, 60, 5, TFT_BLACK);
                tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[last_menu_index], 16, 16, HALEHOUND_MAGENTA);
                tft.setTextColor(HALEHOUND_MAGENTA);
                int textWidth = 6 * strlen(menu_items[last_menu_index]);
                int textX = x_position + (100 - textWidth) / 2;
                int textY = y_position + 30;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        // Highlight current
        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        // Selected button - hot pink icon and text (no border)
        tft.drawBitmap(x_position + 42, y_position + 10, bitmap_icons[current_menu_index], 16, 16, HALEHOUND_HOTPINK);
        tft.setTextColor(HALEHOUND_HOTPINK);
        int textWidth = 6 * strlen(menu_items[current_menu_index]);
        int textX = x_position + (100 - textWidth) / 2;
        int textY = y_position + 30;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
    }

    drawStatusBar();
}

// ═══════════════════════════════════════════════════════════════════════════
// FEATURE RUNNER HELPER
// ═══════════════════════════════════════════════════════════════════════════

void returnToSubmenu() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    last_interaction_time = millis();
    displaySubmenu();
    delay(200);
}

void returnToMainMenu() {
    in_sub_menu = false;
    feature_active = false;
    feature_exit_requested = false;
    menu_initialized = false;
    last_interaction_time = millis();
    displayMenu();
    is_main_menu = false;
}

// ═══════════════════════════════════════════════════════════════════════════
// WIFI SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void handleWiFiSubmenuTouch() {
    touchButtonsUpdate();

    // Back button
    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    // Touch on submenu items
    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            // Execute selected item
            if (current_submenu_index == 8) { // Back
                returnToMainMenu();
                return;
            }

            feature_active = true;
            feature_exit_requested = false;

            switch (current_submenu_index) {
                case 0: // Packet Monitor
                    PacketMonitor::setup();
                    while (!feature_exit_requested) {
                        PacketMonitor::loop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    PacketMonitor::cleanup();
                    break;
                case 1: // Beacon Spammer
                    BeaconSpammer::setup();
                    while (!feature_exit_requested) {
                        BeaconSpammer::loop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    BeaconSpammer::cleanup();
                    break;
                case 2: // Deauther
                    Deauther::setup();
                    while (!feature_exit_requested) {
                        Deauther::loop();
                        touchButtonsUpdate();
                        if (Deauther::isExitRequested()) feature_exit_requested = true;
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) { delay(200); feature_exit_requested = true; }
                    }
                    Deauther::cleanup();
                    break;
                case 3: // Probe Sniffer (with Evil Twin spawn)
                    DeauthDetect::setup();
                    while (!feature_exit_requested) {
                        DeauthDetect::loop();
                        touchButtonsUpdate();
                        if (DeauthDetect::isExitRequested()) feature_exit_requested = true;
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    // Check if user wants to spawn Evil Twin
                    if (DeauthDetect::isEvilTwinRequested()) {
                        const char* ssid = DeauthDetect::getSelectedSSID();
                        DeauthDetect::clearEvilTwinRequest();
                        DeauthDetect::cleanup();
                        // Set SSID and launch Captive Portal
                        CaptivePortal::setSSID(ssid);
                        CaptivePortal::setup();
                        feature_exit_requested = false;
                        while (!feature_exit_requested) {
                            CaptivePortal::loop();
                            touchButtonsUpdate();
                            if (CaptivePortal::isExitRequested()) feature_exit_requested = true;
                            if (isBackButtonTapped()) feature_exit_requested = true;
                        }
                        CaptivePortal::cleanup();
                    } else {
                        DeauthDetect::cleanup();
                    }
                    break;
                case 4: // WiFi Scanner v2.0 (with Tap-to-Attack)
                    WifiScan::setup();
                    while (!feature_exit_requested) {
                        WifiScan::loop();
                        if (WifiScan::isExitRequested()) feature_exit_requested = true;
                    }
                    // Check for attack handoff
                    if (WifiScan::isDeauthRequested()) {
                        // Pre-select target in Deauther from WifiScan
                        const char* bssid = WifiScan::getSelectedBSSID();
                        const char* ssid = WifiScan::getSelectedSSID();
                        int channel = WifiScan::getSelectedChannel();
                        WifiScan::clearAttackRequest();
                        WifiScan::cleanup();
                        Deauther::setTarget(bssid, ssid, channel);
                        Deauther::setup();
                        feature_exit_requested = false;
                        while (!feature_exit_requested) {
                            Deauther::loop();
                            touchButtonsUpdate();
                            if (Deauther::isExitRequested()) feature_exit_requested = true;
                            if (isBackButtonTapped()) feature_exit_requested = true;
                        }
                        Deauther::cleanup();
                    } else if (WifiScan::isCloneRequested()) {
                        const char* ssid = WifiScan::getSelectedSSID();
                        WifiScan::clearAttackRequest();
                        WifiScan::cleanup();
                        CaptivePortal::setSSID(ssid);
                        CaptivePortal::setup();
                        feature_exit_requested = false;
                        while (!feature_exit_requested) {
                            CaptivePortal::loop();
                            touchButtonsUpdate();
                            if (CaptivePortal::isExitRequested()) feature_exit_requested = true;
                            if (isBackButtonTapped()) feature_exit_requested = true;
                        }
                        CaptivePortal::cleanup();
                    } else {
                        WifiScan::cleanup();
                    }
                    break;
                case 5: // Captive Portal (GARMR Evil Twin)
                    CaptivePortal::setup();
                    while (!feature_exit_requested) {
                        CaptivePortal::loop();
                        touchButtonsUpdate();
                        if (CaptivePortal::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) { delay(50); if (digitalRead(0) == LOW) feature_exit_requested = true; }
                    }
                    CaptivePortal::cleanup();
                    break;
                case 6: // Station Scanner (with Deauth handoff)
                    StationScan::setup();
                    while (!feature_exit_requested) {
                        StationScan::loop();
                        if (StationScan::isExitRequested()) feature_exit_requested = true;
                    }
                    // Check for deauth handoff
                    if (StationScan::isDeauthRequested()) {
                        // Get selected station MACs and launch deauther
                        // Note: Station Scanner provides raw MACs, Deauther needs broadcast deauth
                        int selCount = StationScan::getSelectedCount();
                        StationScan::clearDeauthRequest();
                        StationScan::cleanup();
                        // For now, launch Deauther in scan mode - user picks network
                        // Future: Add client-targeted deauth to Deauther
                        Deauther::setup();
                        feature_exit_requested = false;
                        while (!feature_exit_requested) {
                            Deauther::loop();
                            touchButtonsUpdate();
                            if (Deauther::isExitRequested()) feature_exit_requested = true;
                            if (isBackButtonTapped()) feature_exit_requested = true;
                        }
                        Deauther::cleanup();
                    } else {
                        StationScan::cleanup();
                    }
                    break;
                case 7: // Auth Flood
                    AuthFlood::setup();
                    while (!feature_exit_requested) {
                        AuthFlood::loop();
                        if (AuthFlood::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) { delay(200); feature_exit_requested = true; }
                    }
                    AuthFlood::cleanup();
                    break;
            }

            returnToSubmenu();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// BLUETOOTH SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void handleBluetoothSubmenuTouch() {
    touchButtonsUpdate();

    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            if (current_submenu_index == 7) { // Back
                returnToMainMenu();
                return;
            }

            feature_active = true;
            feature_exit_requested = false;

            switch (current_submenu_index) {
                case 0: // BLE Jammer
                    BleJammer::setup();
                    while (!feature_exit_requested) {
                        BleJammer::loop();
                        if (BleJammer::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                    }
                    BleJammer::cleanup();
                    break;
                case 1: // BLE Spoofer
                    BleSpoofer::setup();
                    while (!feature_exit_requested) {
                        BleSpoofer::loop();
                        if (BleSpoofer::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                    }
                    BleSpoofer::cleanup();
                    break;
                case 2: // BLE Beacon
                    BleBeacon::setup();
                    while (!feature_exit_requested) {
                        BleBeacon::loop();
                        if (BleBeacon::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                    }
                    BleBeacon::cleanup();
                    break;
                case 3: // Sniffer
                    BleSniffer::setup();
                    while (!feature_exit_requested) {
                        BleSniffer::loop();
                        if (BleSniffer::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                    }
                    BleSniffer::cleanup();
                    break;
                case 4: // BLE Scanner
                    BleScan::setup();
                    while (!feature_exit_requested) {
                        BleScan::loop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    BleScan::cleanup();
                    break;
                case 5: // WhisperPair (CVE-2025-36911)
                    WhisperPair::setup();
                    while (!feature_exit_requested) {
                        WhisperPair::loop();
                        if (WhisperPair::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                    }
                    WhisperPair::cleanup();
                    break;
                case 6: // AirTag Detect
                    AirTagDetect::setup();
                    while (!feature_exit_requested) {
                        AirTagDetect::loop();
                        if (AirTagDetect::isExitRequested()) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                    }
                    AirTagDetect::cleanup();
                    break;
            }

            returnToSubmenu();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// NRF24 2.4GHz SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void handleNRFSubmenuTouch() {
    touchButtonsUpdate();

    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            if (current_submenu_index == 4) { // Back
                returnToMainMenu();
                return;
            }

            feature_active = true;
            feature_exit_requested = false;

            switch (current_submenu_index) {
                case 0: // Scanner
                    Scanner::scannerSetup();
                    while (!feature_exit_requested) {
                        Scanner::scannerLoop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    Scanner::cleanup();
                    break;
                case 1: // Spectrum Analyzer
                    Analyzer::analyzerSetup();
                    while (!feature_exit_requested) {
                        Analyzer::analyzerLoop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    Analyzer::cleanup();
                    break;
                case 2: // WLAN Jammer
                    WLANJammer::wlanjammerSetup();
                    while (!feature_exit_requested) {
                        WLANJammer::wlanjammerLoop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    WLANJammer::cleanup();
                    break;
                case 3: // Proto Kill
                    ProtoKill::prokillSetup();
                    while (!feature_exit_requested) {
                        ProtoKill::prokillLoop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    ProtoKill::cleanup();
                    break;
            }

            returnToSubmenu();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SUBGHZ SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void handleSubGHzSubmenuTouch() {
    touchButtonsUpdate();

    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            if (current_submenu_index == 5) { // Back
                returnToMainMenu();
                return;
            }

            feature_active = true;
            feature_exit_requested = false;

            switch (current_submenu_index) {
                case 0: // Replay Attack
                    ReplayAttack::setup();
                    while (!feature_exit_requested) {
                        ReplayAttack::loop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    ReplayAttack::cleanup();
                    break;
                case 1: // Brute Force
                    SubBrute::setup();
                    while (!feature_exit_requested) {
                        SubBrute::loop();
                        if (SubBrute::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;  // BOOT button direct
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    SubBrute::cleanup();
                    break;
                case 2: // SubGHz Jammer
                    SubJammer::setup();
                    while (!feature_exit_requested) {
                        SubJammer::loop();
                        if (SubJammer::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;  // BOOT button direct
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    SubJammer::cleanup();
                    break;
                case 3: // Spectrum Analyzer
                    SubAnalyzer::setup();
                    while (!feature_exit_requested) {
                        SubAnalyzer::loop();
                        if (SubAnalyzer::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;  // BOOT button direct
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    SubAnalyzer::cleanup();
                    break;
                case 4: // Saved Profile
                    SavedProfile::saveSetup();
                    while (!feature_exit_requested) {
                        SavedProfile::saveLoop();
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    break;
            }

            returnToSubmenu();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SIGINT SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void showSigintPlaceholder(const char* title) {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();
    drawGlitchTitle(75, title);
    drawGlitchStatus(110, "BUILDING...", HALEHOUND_HOTPINK);
    drawCenteredText(150, "Module under construction", HALEHOUND_MAGENTA, 1);

    while (true) {
        touchButtonsUpdate();
        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) break;
        delay(50);
    }
}

void handleSIGINTSubmenuTouch() {
    touchButtonsUpdate();

    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            if (current_submenu_index == 4) { // Back
                returnToMainMenu();
                return;
            }

            feature_active = true;
            feature_exit_requested = false;

            switch (current_submenu_index) {
                case 0: // EAPOL Capture
                    EapolCapture::setup();
                    while (!feature_exit_requested) {
                        EapolCapture::loop();
                        if (EapolCapture::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    EapolCapture::cleanup();
                    break;
                case 1: // Karma Attack
                    KarmaAttack::setup();
                    while (!feature_exit_requested) {
                        KarmaAttack::loop();
                        if (KarmaAttack::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    KarmaAttack::cleanup();
                    break;
                case 2: // Wardriving
                    wardrivingScreen();
                    break;
                case 3: // Saved Captures
                    SavedCaptures::setup();
                    while (!feature_exit_requested) {
                        SavedCaptures::loop();
                        if (SavedCaptures::isExitRequested()) feature_exit_requested = true;
                        if (digitalRead(0) == LOW) feature_exit_requested = true;
                        touchButtonsUpdate();
                        if (isBackButtonTapped()) feature_exit_requested = true;
                    }
                    SavedCaptures::cleanup();
                    break;
            }

            returnToSubmenu();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TOOLS SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void handleToolsSubmenuTouch() {
    touchButtonsUpdate();

    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            if (current_submenu_index == 5) { // Back
                returnToMainMenu();
                return;
            }

            // Touch Calibrate - run calibration routine
            if (current_submenu_index == 2) {
                runTouchCalibration();
                returnToSubmenu();
                break;
            }

            // GPS - launch GPS screen
            if (current_submenu_index == 3) {
                gpsScreen();
                returnToSubmenu();
                break;
            }

            // Radio Test - SPI radio hardware verification
            if (current_submenu_index == 4) {
                radioTestScreen();
                returnToSubmenu();
                break;
            }

            // Serial Monitor - launch UART terminal
            if (current_submenu_index == 0) {
                serialMonitorScreen();
                returnToSubmenu();
                break;
            }

            // Firmware Update - flash .bin from SD card
            if (current_submenu_index == 1) {
                firmwareUpdateScreen();
                returnToSubmenu();
                break;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETTINGS SUBMENU HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void displayBrightnessControl() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();  // Icon bar instead of title bar

    // Title — glitch effect
    drawGlitchTitle(75, "BRIGHTNESS");

    // Draw brightness bar
    tft.drawRect(30, 90, 180, 30, HALEHOUND_MAGENTA);
    int bar_width = map(brightness_level, 0, 255, 0, 176);
    tft.fillRect(32, 92, bar_width, 26, HALEHOUND_MAGENTA);

    // Show percentage
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    int percent = map(brightness_level, 0, 255, 0, 100);
    tft.setCursor(90, 135);
    tft.printf("%d%%", percent);

    // Touch zones
    tft.setTextSize(1);
    tft.fillRect(30, 180, 80, 40, HALEHOUND_DARK);
    tft.drawRect(30, 180, 80, 40, HALEHOUND_MAGENTA);
    tft.setCursor(50, 195);
    tft.print("DARKER");

    tft.fillRect(130, 180, 80, 40, HALEHOUND_DARK);
    tft.drawRect(130, 180, 80, 40, HALEHOUND_MAGENTA);
    tft.setCursor(145, 195);
    tft.print("BRIGHTER");
}

void brightnessControlLoop() {
    displayBrightnessControl();

    while (!feature_exit_requested) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            saveSettings();
            break;
        }

        // Darker button
        if (isTouchInArea(30, 180, 80, 40)) {
            brightness_level = max(10, brightness_level - 25);
            ledcWrite(0, brightness_level);
            displayBrightnessControl();
            delay(150);
        }

        // Brighter button
        if (isTouchInArea(130, 180, 80, 40)) {
            brightness_level = min(255, brightness_level + 25);
            ledcWrite(0, brightness_level);
            displayBrightnessControl();
            delay(150);
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SCREEN TIMEOUT CONTROL
// ═══════════════════════════════════════════════════════════════════════════

int getTimeoutOptionIndex() {
    for (int i = 0; i < numTimeoutOptions; i++) {
        if (timeoutOptions[i] == screen_timeout_seconds) return i;
    }
    return 1;  // default to 1 MIN if not found
}

void displayTimeoutControl() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();

    drawGlitchTitle(75, "TIMEOUT");

    // Current value display
    int idx = getTimeoutOptionIndex();
    tft.drawRoundRect(30, 95, 180, 40, 6, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    int tw = strlen(timeoutLabels[idx]) * 12;
    tft.setCursor((tft.width() - tw) / 2, 105);
    tft.print(timeoutLabels[idx]);

    // Arrow hints
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL, TFT_BLACK);
    tft.setCursor(38, 110);
    tft.print("<");
    tft.setCursor(200, 110);
    tft.print(">");

    // Left / Right buttons
    tft.fillRect(30, 160, 80, 40, HALEHOUND_DARK);
    tft.drawRect(30, 160, 80, 40, HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(50, 175);
    tft.print("SHORTER");

    tft.fillRect(130, 160, 80, 40, HALEHOUND_DARK);
    tft.drawRect(130, 160, 80, 40, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(148, 175);
    tft.print("LONGER");

    // Description
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    if (screen_timeout_seconds == 0) {
        tft.setCursor(40, 230);
        tft.print("Screen stays on always");
    } else {
        tft.setCursor(22, 230);
        tft.print("Screen dims after inactivity");
    }
}

void screenTimeoutControlLoop() {
    displayTimeoutControl();

    while (!feature_exit_requested) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            saveSettings();
            break;
        }

        // Shorter button (left)
        if (isTouchInArea(30, 160, 80, 40)) {
            int idx = getTimeoutOptionIndex();
            if (idx > 0) {
                idx--;
                screen_timeout_seconds = timeoutOptions[idx];
                displayTimeoutControl();
            }
            delay(200);
        }

        // Longer button (right)
        if (isTouchInArea(130, 160, 80, 40)) {
            int idx = getTimeoutOptionIndex();
            if (idx < numTimeoutOptions - 1) {
                idx++;
                screen_timeout_seconds = timeoutOptions[idx];
                displayTimeoutControl();
            }
            delay(200);
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// COLOR SWAP CONTROL (BGR / RGB panel toggle)
// ═══════════════════════════════════════════════════════════════════════════

void displayColorSwap() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();

    drawGlitchTitle(60, "COLORS");

    // Current mode display
    tft.drawRoundRect(20, 95, 200, 50, 6, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    if (color_order_rgb) {
        tft.setCursor(72, 108);
        tft.print("RGB");
    } else {
        tft.setCursor(52, 108);
        tft.print("BGR *");
    }

    // Description
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(20, 160);
    tft.print("If colors look wrong (green");
    tft.setCursor(20, 172);
    tft.print("instead of pink), tap SWAP.");
    tft.setCursor(20, 192);
    tft.print("* = default for most boards");

    // Swap button
    tft.fillRect(50, 225, 140, 45, HALEHOUND_DARK);
    tft.drawRect(50, 225, 140, 45, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(82, 238);
    tft.print("SWAP");
}

void colorSwapLoop() {
    displayColorSwap();

    while (!feature_exit_requested) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            saveSettings();
            break;
        }

        // Swap button
        if (isTouchInArea(50, 225, 140, 45)) {
            color_order_rgb = !color_order_rgb;
            applyColorOrder();
            saveSettings();
            displayColorSwap();
            delay(300);
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// INVERT DISPLAY CONTROL
// ═══════════════════════════════════════════════════════════════════════════

void displayInvertScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();

    drawGlitchTitle(60, "INVERT");

    // Current mode display
    tft.drawRoundRect(20, 95, 200, 50, 6, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    if (display_inverted) {
        tft.setCursor(52, 108);
        tft.print("INVERTED");
    } else {
        tft.setCursor(52, 108);
        tft.print("NORMAL *");
    }

    // Description
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(20, 160);
    tft.print("If colors are washed out or");
    tft.setCursor(20, 172);
    tft.print("inverted, tap TOGGLE.");
    tft.setCursor(20, 192);
    tft.print("* = default for most boards");

    // Toggle button
    tft.fillRect(50, 225, 140, 45, HALEHOUND_DARK);
    tft.drawRect(50, 225, 140, 45, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(65, 238);
    tft.print("TOGGLE");
}

void invertDisplayLoop() {
    displayInvertScreen();

    while (!feature_exit_requested) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            saveSettings();
            break;
        }

        // Toggle button
        if (isTouchInArea(50, 225, 140, 45)) {
            display_inverted = !display_inverted;
            tft.invertDisplay(display_inverted);
            saveSettings();
            displayInvertScreen();
            delay(300);
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// COLOR MODE SELECTOR
// 3 palettes: Default (pink/purple), Colorblind (blue/yellow), Hi-Contrast
// ═══════════════════════════════════════════════════════════════════════════

void colorModeScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();

    drawGlitchTitle(60, "COLORS");

    // Current mode name in rounded rect
    tft.drawRoundRect(20, 95, 200, 50, 6, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
    const char* modeNames[] = {"DEFAULT", "COLORBLIND", "HI-CONTRAST"};
    const char* modeName = (color_mode < 3) ? modeNames[color_mode] : modeNames[0];
    int nameLen = strlen(modeName);
    int nameX = (240 - nameLen * 12) / 2;
    tft.setCursor(nameX, 108);
    tft.print(modeName);

    // Color preview strip — 6 swatches showing the active theme
    int swatchW = 30;
    int swatchH = 20;
    int swatchY = 155;
    int swatchStartX = 10;
    uint16_t swatches[] = {HALEHOUND_MAGENTA, HALEHOUND_HOTPINK, HALEHOUND_BRIGHT,
                           HALEHOUND_VIOLET, HALEHOUND_MAGENTA, HALEHOUND_GREEN};
    for (int i = 0; i < 6; i++) {
        int sx = swatchStartX + i * (swatchW + 7);
        tft.fillRect(sx, swatchY, swatchW, swatchH, swatches[i]);
        tft.drawRect(sx, swatchY, swatchW, swatchH, HALEHOUND_GUNMETAL);
    }

    // Description
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(20, 185);
    tft.print("Tap NEXT to cycle modes.");
    tft.setCursor(20, 197);
    tft.print("Changes apply instantly.");

    // NEXT button
    tft.fillRect(50, 225, 140, 45, HALEHOUND_DARK);
    tft.drawRect(50, 225, 140, 45, HALEHOUND_MAGENTA);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(80, 238);
    tft.print("NEXT");
}

void colorModeLoop() {
    colorModeScreen();

    while (!feature_exit_requested) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            saveSettings();
            break;
        }

        // NEXT button — cycle through 3 modes
        if (isTouchInArea(50, 225, 140, 45)) {
            color_mode = (color_mode + 1) % 3;
            applyColorMode(color_mode);
            saveSettings();
            colorModeScreen();
            delay(300);
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ROTATION CONTROL
// ═══════════════════════════════════════════════════════════════════════════

void displayRotationScreen() {
    int sw = tft.width();
    int sh = tft.height();

    tft.fillScreen(TFT_BLACK);
    tft.drawRect(2, 2, sw - 4, sh - 4, HALEHOUND_HOTPINK);
    tft.drawLine(0, 19, sw, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, sw, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, sw, 36, HALEHOUND_HOTPINK);

    drawGlitchTitle(48, "ROTATION");

    // Current rotation label
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(15, 72);
    tft.print("Current: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    const char* rotNames[] = {"Standard", "Landscape CW", "Flipped 180", "Landscape CCW"};
    tft.print(rotNames[screen_rotation]);

    // 4 rotation options — compact layout
    const uint8_t rotVals[] = {0, 2, 1, 3};
    const char* rotLabels[] = {"Standard", "Flipped 180", "90 CW", "90 CCW"};
    const char* rotDescs[] = {"USB at bottom", "USB at top", "USB at left", "USB at right"};
    int boxW = sw - 40;
    int boxH = 35;
    int startY = 88;
    int gap = 4;

    for (int i = 0; i < 4; i++) {
        int y = startY + i * (boxH + gap);
        uint16_t col = (screen_rotation == rotVals[i]) ? HALEHOUND_HOTPINK : HALEHOUND_MAGENTA;
        tft.drawRect(20, y, boxW, boxH, col);
        tft.setTextColor(col);
        tft.setTextSize(2);
        tft.setCursor(30, y + 4);
        tft.print(rotLabels[i]);
        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(30, y + 22);
        tft.print(rotDescs[i]);
    }

    // Info text
    int infoY = startY + 4 * (boxH + gap) + 4;
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setTextSize(1);
    tft.setCursor(15, infoY);
    tft.print("Touch recal runs after change.");

    tft.setTextColor(TFT_DARKGREY);
    tft.setCursor(15, infoY + 16);
    tft.print("BOOT button = back");
}

static void applyNewRotation(uint8_t newRot) {
    extern bool touch_calibrated;

    screen_rotation = newRot;
    tft.setRotation(newRot);
    applyColorOrder();

    // Invalidate touch calibration — axes change with rotation
    touch_calibrated = false;
    saveSettings();

    // Screen just changed — run calibration immediately
    runTouchCalibration();
}

void rotationControlLoop() {
    displayRotationScreen();

    int sw = tft.width();
    int boxW = sw - 40;
    int boxH = 35;
    int startY = 88;
    int gap = 4;
    const uint8_t rotVals[] = {0, 2, 1, 3};

    while (!feature_exit_requested) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            break;
        }

        // Check all 4 rotation buttons
        for (int i = 0; i < 4; i++) {
            int y = startY + i * (boxH + gap);
            if (isTouchInArea(20, y, boxW, boxH)) {
                if (screen_rotation != rotVals[i]) {
                    applyNewRotation(rotVals[i]);
                    // Recalc dimensions after rotation change
                    sw = tft.width();
                    boxW = sw - 40;
                    displayRotationScreen();
                }
                delay(300);
                break;
            }
        }

        delay(50);
    }
}

// ═══════════════════════════════════════════════════════════════════════════

void drawEggBackground() {
    tft.fillScreen(TFT_BLACK);
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0xA800);
    tft.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, HALEHOUND_HOTPINK);
    tft.drawRect(4, 4, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 8, HALEHOUND_VIOLET);
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

void showEasterEggPage1() {
    drawEggBackground();
    drawGlitchTitle(48, "PR #76");

    tft.setTextSize(1);
    int y = 68;

    // Stats
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(18, y);
    tft.print("8 Features | 17 Fixes | Touch");
    y += 18;

    // Timeline
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y); tft.print("Jan 3:  I submitted PR #76");
    y += 13;
    tft.setCursor(10, y); tft.print("        my +296 lines of FENRIR");
    y += 16;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("Jan 4:  my PR CLOSED w/o merge");
    y += 16;

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y); tft.print("Jan 5:  v1.5.0 released");
    y += 13;
    tft.setCursor(10, y); tft.print("        with MY code inside");
    y += 20;

    // Features submitted
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, y); tft.print("--- FEATURES SUBMITTED ---");
    y += 14;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("2.4GHz Spectrum Analyzer");
    y += 12;
    tft.setCursor(10, y); tft.print("WLAN Jammer (NRF24)");
    y += 12;
    tft.setCursor(10, y); tft.print("Proto Kill Multi-Protocol");
    y += 12;
    tft.setCursor(10, y); tft.print("SubGHz Brute Force");
    y += 12;
    tft.setCursor(10, y); tft.print("BLE Sniffer w/ RSSI");
    y += 12;
    tft.setCursor(10, y); tft.print("Brightness + Screen Timeout");
    y += 12;
    tft.setCursor(10, y); tft.print("Full Touch Support");
    y += 18;

    // Page hint
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(50, 305);
    tft.print("TAP FOR MORE  [1/2]");
}

void showEasterEggPage2() {
    drawEggBackground();
    drawGlitchTitle(48, "RECEIPTS");

    // Bold statement
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 68);
    tft.print("VERBATIM COPIED");

    tft.setTextSize(1);
    int y = 88;

    // Bug fixes
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, y); tft.print("--- 17 BUG FIXES TAKEN ---");
    y += 14;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("Deauth buffer overflow");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(190, y); tft.print("HIGH");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("SubGHz wrong modulation");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(190, y); tft.print("HIGH");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("Global init race condition");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(190, y); tft.print("HIGH");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("Profile delete underflow");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(190, y); tft.print("HIGH");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("Missing header guards");
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(190, y); tft.print("HIGH");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("+10 more cleanup fixes");
    y += 18;

    // Pin fixes
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, y); tft.print("--- HW FIXES WE FOUND ---");
    y += 14;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("GDO0/GDO2 TX/RX SWAPPED");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(170, y); tft.print("FIXED");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("NRF24 pin conflict");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(170, y); tft.print("FIXED");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("V2 pin mappings figured out");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(170, y); tft.print("FIXED");
    y += 12;

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, y); tft.print("SCREEN_HEIGHT 64->320");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(170, y); tft.print("FIXED");
    y += 18;

    // The verdict
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y); tft.print("Attribution given:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(130, y); tft.print("ZERO. NONE.");
    y += 18;

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(18, y);
    tft.print("Remember. I built this.");

    // Page hint
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(68, 305);
    tft.print("BACK TO EXIT  [2/2]");
}

void showEasterEgg() {
    int page = 1;
    showEasterEggPage1();

    while (true) {
        touchButtonsUpdate();

        // Back icon or buttons — exit
        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) break;

        // Tap anywhere else — flip page
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty > 36) {  // Below icon bar
                if (page == 1) {
                    page = 2;
                    showEasterEggPage2();
                } else {
                    page = 1;
                    showEasterEggPage1();
                }
                delay(300);
            }
        }

        delay(50);
    }
}

void displayDeviceInfo() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawInoIconBar();

    drawGlitchTitle(70, "DEV INFO");

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    int y = 75;

    tft.setCursor(10, y); tft.print("Device: HaleHound-CYD");
    y += 18;
    tft.setCursor(10, y); tft.print("Version: v2.7.0 CYD Edition");
    y += 18;
    tft.setCursor(10, y); tft.print("By: HaleHound (JMFH)");
    y += 18;
    tft.setCursor(10, y); tft.print("Based on ESP32-DIV (forked)");
    y += 18;
    tft.setCursor(10, y); tft.printf("Free Heap: %d", ESP.getFreeHeap());
    y += 18;
    tft.setCursor(10, y); tft.printf("CPU Freq: %dMHz", ESP.getCpuFreqMHz());
    y += 18;
    tft.setCursor(10, y); tft.printf("Flash: %dMB", ESP.getFlashChipSize() / 1024 / 1024);
    y += 18;
    tft.setCursor(10, y); tft.print("Board: " CYD_BOARD_NAME);
    y += 18;

    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, y + 15);
    tft.print("GitHub: github.com/JesseCHale");

    // Easter egg: tap "By: HaleHound" line 5 times
    int eggTaps = 0;
    unsigned long lastEggTap = 0;

    while (!feature_exit_requested) {
        touchButtonsUpdate();
        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            feature_exit_requested = true;
            break;
        }

        // Check for taps on "By: HaleHound" line (y=111, height ~14px)
        if (isTouchInArea(10, 107, 200, 18)) {
            unsigned long now = millis();
            if (now - lastEggTap < 800) {
                eggTaps++;
            } else {
                eggTaps = 1;
            }
            lastEggTap = now;

            if (eggTaps >= 5) {
                showEasterEgg();
                eggTaps = 0;
                // Redraw device info after returning
                tft.fillScreen(TFT_BLACK);
                drawStatusBar();
                drawInoIconBar();
                drawGlitchTitle(70, "DEV INFO");

                tft.setTextColor(HALEHOUND_MAGENTA);
                tft.setTextSize(1);
                y = 75;
                tft.setCursor(10, y); tft.print("Device: HaleHound-CYD");
                y += 18;
                tft.setCursor(10, y); tft.print("Version: v2.7.0 CYD Edition");
                y += 18;
                tft.setCursor(10, y); tft.print("By: HaleHound (JMFH)");
                y += 18;
                tft.setCursor(10, y); tft.print("Based on ESP32-DIV (forked)");
                y += 18;
                tft.setCursor(10, y); tft.printf("Free Heap: %d", ESP.getFreeHeap());
                y += 18;
                tft.setCursor(10, y); tft.printf("CPU Freq: %dMHz", ESP.getCpuFreqMHz());
                y += 18;
                tft.setCursor(10, y); tft.printf("Flash: %dMB", ESP.getFlashChipSize() / 1024 / 1024);
                y += 18;
                tft.setCursor(10, y); tft.print("Board: " CYD_BOARD_NAME);
                y += 18;
                tft.setTextColor(HALEHOUND_VIOLET);
                tft.setCursor(10, y + 15);
                tft.print("GitHub: github.com/JesseCHale");
            }
            delay(200);
        }

        delay(50);
    }
}

void handleSettingsSubmenuTouch() {
    touchButtonsUpdate();

    if (isBackButtonTapped()) {
        returnToMainMenu();
        return;
    }

    for (int i = 0; i < active_submenu_size; i++) {
        int yPos = 30 + i * 30;
        if (i == active_submenu_size - 1) yPos += 10;

        if (isTouchInArea(10, yPos, 200, 25)) {
            current_submenu_index = i;
            last_interaction_time = millis();
            displaySubmenu();
            delay(200);

            if (current_submenu_index == 7) { // Back
                returnToMainMenu();
                return;
            }

            feature_active = true;
            feature_exit_requested = false;

            switch (current_submenu_index) {
                case 0: // Brightness
                    brightnessControlLoop();
                    break;
                case 1: // Screen Timeout
                    screenTimeoutControlLoop();
                    break;
                case 2: // Swap Colors
                    colorSwapLoop();
                    break;
                case 3: // Invert Display
                    invertDisplayLoop();
                    break;
                case 4: // Color Mode
                    colorModeLoop();
                    break;
                case 5: // Rotation
                    rotationControlLoop();
                    break;
                case 6: // Device Info
                    displayDeviceInfo();
                    break;
            }

            returnToSubmenu();
            break;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ABOUT PAGE HANDLER
// My full-screen about page — skull watermark, glitch title, armed modules
// ═══════════════════════════════════════════════════════════════════════════

void handleAboutPage() {
    // I draw my full-screen about page — same visual punch as my splash screen
    tft.fillScreen(TFT_BLACK);

    // Skull watermark — dark cyan, same as my splash screen
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x2945);

    // My double border — HaleHound signature
    tft.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, HALEHOUND_VIOLET);
    tft.drawRect(4, 4, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 8, HALEHOUND_MAGENTA);

    // Icon bar with back button
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Glitch title — chromatic aberration effect
    drawGlitchTitle(58, "HALEHOUND");

    // Subtitle
    drawGlitchStatus(80, "CYD Edition", HALEHOUND_MAGENTA);

    // Version centered
    drawCenteredText(90, "v2.7.0", HALEHOUND_VIOLET, 1);

    // Separator
    tft.drawLine(20, 100, SCREEN_WIDTH - 20, 100, HALEHOUND_VIOLET);

    // Armed modules section header
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 106);
    tft.print("ARMED:");

    // Module grid — 2 columns showing what I've got loaded
    int my = 118;
    int col1 = 15;
    int col2 = 125;
    int rowH = 13;

    tft.setTextColor(HALEHOUND_MAGENTA);

    tft.setCursor(col1, my); tft.print("> WiFi");
    tft.setCursor(col2, my); tft.print("> Bluetooth");
    my += rowH;
    tft.setCursor(col1, my); tft.print("> SubGHz");
    tft.setCursor(col2, my); tft.print("> NRF24");
    my += rowH;
    tft.setCursor(col1, my); tft.print("> SIGINT");
    tft.setCursor(col2, my); tft.print("> GPS");
    my += rowH;
    tft.setCursor(col1, my); tft.print("> SD Card");
    tft.setCursor(col2, my); tft.print("> Serial Mon");

    // Separator
    my += rowH + 6;
    tft.drawLine(20, my, SCREEN_WIDTH - 20, my, HALEHOUND_VIOLET);
    my += 8;

    // System section header
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, my);
    tft.print("SYSTEM:");
    my += 14;

    // Live hardware stats
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(col1, my);
    tft.printf("Heap: %d bytes", ESP.getFreeHeap());
    my += 12;
    tft.setCursor(col1, my);
    tft.printf("CPU: %dMHz  Flash: %dMB", ESP.getCpuFreqMHz(), ESP.getFlashChipSize() / 1024 / 1024);
    my += 12;
    tft.setCursor(col1, my);
    tft.print("Board: " CYD_BOARD_NAME);

    // Separator
    my += 18;
    tft.drawLine(20, my, SCREEN_WIDTH - 20, my, HALEHOUND_VIOLET);
    my += 10;

    // Author — this is my firmware
    drawCenteredText(my, "By: JMFH (HaleHound)", HALEHOUND_MAGENTA, 1);
    my += 14;
    drawCenteredText(my, "github.com/JesseCHale", HALEHOUND_VIOLET, 1);

    // Tagline at the bottom
    drawCenteredText(SCREEN_HEIGHT - 18, "I built this.", HALEHOUND_GUNMETAL, 1);

    // My own event loop — I stay here until back is pressed
    while (true) {
        touchButtonsUpdate();

        if (isInoBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            break;
        }

        delay(50);
    }

    returnToMainMenu();
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN BUTTON/TOUCH HANDLER
// ═══════════════════════════════════════════════════════════════════════════

void handleButtons() {
    // Screen sleep check
    if (screen_timeout_seconds > 0 && !screen_asleep) {
        if (millis() - last_interaction_time > (unsigned long)screen_timeout_seconds * 1000) {
            ledcWrite(0, 0);
            screen_asleep = true;
        }
    }

    // Wake on any touch or button press — eat the input
    if (screen_asleep) {
        touchButtonsUpdate();
        if (isTouched() || isBootButtonPressed()) {
            screen_asleep = false;
            ledcWrite(0, brightness_level);
            last_interaction_time = millis();
            delay(300);
        }
        return;
    }

    if (in_sub_menu) {
        switch (current_menu_index) {
            case 0: handleWiFiSubmenuTouch(); break;
            case 1: handleBluetoothSubmenuTouch(); break;
            case 2: handleNRFSubmenuTouch(); break;
            case 3: handleSubGHzSubmenuTouch(); break;
            case 4: handleSIGINTSubmenuTouch(); break;
            case 5: handleToolsSubmenuTouch(); break;
            case 6: handleSettingsSubmenuTouch(); break;
            case 7: handleAboutPage(); break;
            default: break;
        }
    } else {
        // Main menu touch handling
        touchButtonsUpdate();

        // Check touch on menu items
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (isTouchInArea(x_position, y_position, 100, 60)) {
                current_menu_index = i;
                last_interaction_time = millis();
                displayMenu();
                delay(150);

                // Enter submenu
                updateActiveSubmenu();
                if (active_submenu_items && active_submenu_size > 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    submenu_initialized = false;
                    displaySubmenu();
                }
                break;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SPLASH SCREEN
// ═══════════════════════════════════════════════════════════════════════════

void showSplash() {
    tft.fillScreen(HALEHOUND_BLACK);

    // Draw border
    tft.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, HALEHOUND_VIOLET);
    tft.drawRect(4, 4, SCREEN_WIDTH - 8, SCREEN_HEIGHT - 8, HALEHOUND_MAGENTA);

    // Skull splatter watermark - full screen
    tft.drawBitmap(0, 0, skull_bg_bitmap, SKULL_BG_WIDTH, SKULL_BG_HEIGHT, 0x2945);  // Dark cyan watermark (brightened for all panel variants)

    // Title — glitch effect
    drawGlitchTitle(80, "HALEHOUND");

    // Subtitle
    drawGlitchStatus(110, "CYD Edition", HALEHOUND_MAGENTA);

    // Version
    tft.setTextSize(1);
    drawCenteredText(130, "v2.7.0", HALEHOUND_HOTPINK, 1);

    // Board info
    drawCenteredText(140, CYD_BOARD_NAME, HALEHOUND_HOTPINK, 1);

    // Credits
    drawCenteredText(SCREEN_HEIGHT - 40, "by JesseCHale", HALEHOUND_VIOLET, 1);
    drawCenteredText(SCREEN_HEIGHT - 25, "github.com/JesseCHale", HALEHOUND_VIOLET, 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// BOOT DIAGNOSTICS — Shows radio/SPI status on TFT (works without serial)
// ═══════════════════════════════════════════════════════════════════════════

void runBootDiagnostics() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(1);
    int y = 5;

    // Title
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(30, y);
    tft.print("=== BOOT DIAGNOSTICS ===");
    y += 18;

    // SPI bus info
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, y);
    tft.printf("SPI Bus: SCK=%d MISO=%d MOSI=%d", VSPI_SCK, VSPI_MISO, VSPI_MOSI);
    y += 16;

    // ── NRF24 TEST ──────────────────────────────────────────────
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, y);
    tft.printf("NRF24: CE=%d CSN=%d", NRF24_CE, NRF24_CSN);
    y += 12;

    // Deselect all CS pins
    pinMode(SD_CS, OUTPUT);       digitalWrite(SD_CS, HIGH);
    pinMode(CC1101_CS, OUTPUT);   digitalWrite(CC1101_CS, HIGH);
    pinMode(NRF24_CSN, OUTPUT);   digitalWrite(NRF24_CSN, HIGH);
    pinMode(NRF24_CE, OUTPUT);    digitalWrite(NRF24_CE, LOW);

    // Fresh SPI init
    SPI.end();
    delay(10);
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI);
    SPI.setFrequency(4000000);
    delay(10);

    // Try NRF24 STATUS register read — 3 attempts with increasing delays
    bool nrfFound = false;
    byte nrfStatus[3] = {0, 0, 0};
    int nrfDelays[] = {10, 100, 500};

    for (int attempt = 0; attempt < 3; attempt++) {
        delay(nrfDelays[attempt]);

        // NRF24 returns STATUS byte on first SPI transfer of any command
        digitalWrite(NRF24_CSN, LOW);
        delayMicroseconds(5);
        nrfStatus[attempt] = SPI.transfer(0x07);
        SPI.transfer(0xFF);
        digitalWrite(NRF24_CSN, HIGH);

        if (nrfStatus[attempt] != 0x00 && nrfStatus[attempt] != 0xFF) {
            nrfFound = true;
        }
    }

    // Show raw STATUS bytes from all 3 attempts
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, y);
    tft.printf("  RAW: 0x%02X  0x%02X  0x%02X", nrfStatus[0], nrfStatus[1], nrfStatus[2]);
    y += 12;

    tft.setCursor(5, y);
    if (nrfFound) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print("  >> NRF24 DETECTED");
        for (int i = 0; i < 3; i++) {
            if (nrfStatus[i] != 0x00 && nrfStatus[i] != 0xFF) {
                tft.printf(" (try %d +%dms)", i + 1, nrfDelays[i]);
                break;
            }
        }
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.print("  >> NRF24 NOT FOUND");
    }
    y += 18;

    // ── CC1101 TEST ─────────────────────────────────────────────
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, y);
    tft.printf("CC1101: CS=%d GDO0=%d GDO2=%d", CC1101_CS, CC1101_GDO0, CC1101_GDO2);
    y += 12;

    // Reset SPI for ELECHOUSE library
    SPI.end();
    delay(10);

    // Deselect NRF24 before CC1101 test
    pinMode(NRF24_CSN, OUTPUT);
    digitalWrite(NRF24_CSN, HIGH);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    ELECHOUSE_cc1101.setSpiPin(VSPI_SCK, VSPI_MISO, VSPI_MOSI, CC1101_CS);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

    bool cc1101Found = ELECHOUSE_cc1101.getCC1101();

    tft.setCursor(5, y);
    if (cc1101Found) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print("  >> CC1101 DETECTED");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.print("  >> CC1101 NOT FOUND");
    }
    y += 18;

    // ── SD CARD TEST ────────────────────────────────────────────
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(5, y);
    tft.printf("SD Card: CS=%d", SD_CS);
    y += 12;

    SPI.end();
    delay(5);
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI);
    pinMode(NRF24_CSN, OUTPUT);   digitalWrite(NRF24_CSN, HIGH);
    pinMode(CC1101_CS, OUTPUT);   digitalWrite(CC1101_CS, HIGH);

    // Send CMD0 (GO_IDLE) — card present returns 0x01
    digitalWrite(SD_CS, LOW);
    delayMicroseconds(5);
    for (int i = 0; i < 10; i++) SPI.transfer(0xFF);
    SPI.transfer(0x40);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x00);
    SPI.transfer(0x95);
    byte sdResp = SPI.transfer(0xFF);
    byte sdResp2 = SPI.transfer(0xFF);
    digitalWrite(SD_CS, HIGH);

    tft.setCursor(5, y);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.printf("  RESP: 0x%02X 0x%02X", sdResp, sdResp2);
    if (sdResp == 0x01) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.print(" (card OK)");
    }
    y += 18;

    // ── SYSTEM INFO ─────────────────────────────────────────────
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(5, y);
    tft.printf("Heap:%d  CPU:%dMHz  Flash:%dMB",
        ESP.getFreeHeap(), ESP.getCpuFreqMHz(),
        ESP.getFlashChipSize() / 1024 / 1024);
    y += 12;

    uint64_t mac = ESP.getEfuseMac();
    tft.setCursor(5, y);
    tft.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X",
        (uint8_t)(mac), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16),
        (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    y += 18;

    // ── COUNTDOWN ───────────────────────────────────────────────
    bool hasError = !nrfFound || !cc1101Found;
    int waitTime = hasError ? 8 : 3;

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    for (int i = waitTime; i > 0; i--) {
        tft.fillRect(5, y, 230, 12, TFT_BLACK);
        tft.setCursor(5, y);
        if (hasError) {
            tft.printf("CHECK WIRING! Continuing in %d...", i);
        } else {
            tft.printf("All radios OK! Continuing in %d...", i);
        }
        delay(1000);
    }

    // Restore SPI bus to clean state for spiManager
    SPI.end();
    delay(5);
    SPI.begin(VSPI_SCK, VSPI_MISO, VSPI_MOSI);
    pinMode(SD_CS, OUTPUT);       digitalWrite(SD_CS, HIGH);
    pinMode(CC1101_CS, OUTPUT);   digitalWrite(CC1101_CS, HIGH);
    pinMode(NRF24_CSN, OUTPUT);   digitalWrite(NRF24_CSN, HIGH);
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    // Initialize Serial
    Serial.begin(CYD_DEBUG_BAUD);
    delay(500);

    Serial.println();
    Serial.println("===============================================");
    Serial.println("        HALEHOUND-CYD FIRMWARE v2.7.0");
    Serial.println("        " CYD_BOARD_NAME);
    Serial.println("===============================================");
    Serial.println();

    // Initialize display
    tft.init();
    tft.setRotation(0);  // Portrait mode — keeps 240x320 coordinate space
    tft.invertDisplay(false);  // Normalize panel inversion before drawing anything
    tft.fillScreen(HALEHOUND_BLACK);

    // Turn on backlight with PWM
    ledcSetup(0, 5000, 8);
    ledcAttachPin(CYD_TFT_BL, 0);
    ledcWrite(0, brightness_level);

    // Show splash screen
    showSplash();

    // Initialize subsystems
    Serial.println("[INIT] Initializing subsystems...");

    // SPI bus manager
    spiManagerSetup();
    Serial.println("[INIT] SPI Manager OK");

    // Boot diagnostics disabled for normal boot — function kept for second board debugging
    // runBootDiagnostics();

    // Touch buttons
    initButtons();
    Serial.println("[INIT] Touch buttons OK");

    // Touch test available via runTouchTest() if needed for recalibration

    // Load settings from EEPROM (brightness, timeout, color order, rotation, touch cal, color mode)
    loadSettings();
    ledcWrite(0, brightness_level);
    applyColorMode(color_mode);

    // Apply saved rotation — must happen before applyColorOrder (which needs correct MADCTL base)
    if (screen_rotation != 0) {
        tft.setRotation(screen_rotation);
        Serial.printf("[INIT] Rotation set to %d\n", screen_rotation);
    }
    applyColorOrder();
    if (display_inverted) {
        tft.invertDisplay(true);
    }
    Serial.println("[INIT] Settings loaded");

    // Auto-trigger touch calibration on first boot (uncalibrated board)
    {
        extern bool touch_calibrated;
        if (!touch_calibrated) {
            Serial.println("[INIT] No touch calibration found — running first-time calibration");
            runTouchCalibration();
        }
    }

    // Print system info
    Serial.printf("[INFO] Free Heap: %d\n", ESP.getFreeHeap());
    Serial.printf("[INFO] CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("[INFO] Flash Size: %d MB\n", ESP.getFlashChipSize() / 1024 / 1024);

    // Small delay to show splash
    delay(2000);

    // Show main menu
    is_main_menu = true;
    menu_initialized = false;
    displayMenu();
    last_interaction_time = millis();

    Serial.println("[INIT] Setup complete - entering main loop");
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    handleButtons();
    delay(20);
}
