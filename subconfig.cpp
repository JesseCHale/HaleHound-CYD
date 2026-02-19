// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD SubGHz Configuration Implementation
// SavedProfile stub implementation
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════

#include "subconfig.h"
#include "shared.h"
#include "touch_buttons.h"
#include "utils.h"
#include "icon.h"

// ═══════════════════════════════════════════════════════════════════════════
// SHARED ICON BAR FOR SUBCONFIG SCREENS
// ═══════════════════════════════════════════════════════════════════════════

#define SC_ICON_SIZE 16

// Draw icon bar with back icon - MATCHES ORIGINAL HALEHOUND
static void drawSubconfigUI() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_GUNMETAL);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, SC_ICON_SIZE, SC_ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

// Check if back icon was tapped
static bool isScBackTapped() {
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
// SAVED PROFILE IMPLEMENTATION - Stub for CYD
// ═══════════════════════════════════════════════════════════════════════════

namespace SavedProfile {

static bool initialized = false;
static bool exitRequested = false;

void saveSetup() {
    if (initialized) return;

    #if CYD_DEBUG
    Serial.println("[SAVEDPROFILE] Initializing...");
    #endif

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawSubconfigUI();  // Icon bar instead of title bar

    // Title
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(2);
    drawCenteredText(50, "SAVED PROFILES", HALEHOUND_HOTPINK, 2);

    // Show stub message
    drawCenteredText(90, "COMING SOON", HALEHOUND_MAGENTA, 2);

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(10, 130);
    tft.print("SubGHz Saved Profiles will");
    tft.setCursor(10, 145);
    tft.print("allow you to save and replay");
    tft.setCursor(10, 160);
    tft.print("captured RF signals.");

    tft.setTextColor(HALEHOUND_VIOLET);
    tft.setCursor(10, 190);
    tft.print("Requires CC1101 radio module");
    tft.setCursor(10, 205);
    tft.print("and SD card for storage.");

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(10, SCREEN_HEIGHT - 25);
    tft.print("Tap BACK icon to return");

    exitRequested = false;
    initialized = true;
}

void saveLoop() {
    if (!initialized) return;

    touchButtonsUpdate();

    if (isScBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
    }
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    initialized = false;
    exitRequested = false;
}

}  // namespace SavedProfile


// ═══════════════════════════════════════════════════════════════════════════
// SUBJAMMER STUB IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace subjammer {

static bool initialized = false;
static bool exitRequested = false;

void subjammerSetup() {
    if (initialized) return;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawSubconfigUI();  // Icon bar instead of title bar

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(2);
    drawCenteredText(50, "SUBGHZ JAMMER", HALEHOUND_HOTPINK, 2);
    drawCenteredText(90, "CC1101 REQUIRED", HALEHOUND_MAGENTA, 2);

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(10, 130);
    tft.print("SubGHz Jammer requires");
    tft.setCursor(10, 145);
    tft.print("CC1101 radio module.");

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(10, SCREEN_HEIGHT - 25);
    tft.print("Tap BACK icon to return");

    exitRequested = false;
    initialized = true;
}

void subjammerLoop() {
    if (!initialized) return;

    touchButtonsUpdate();

    if (isScBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
    }
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    initialized = false;
    exitRequested = false;
}

}  // namespace subjammer


// ═══════════════════════════════════════════════════════════════════════════
// SUBBRUTE STUB IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

namespace subbrute {

static bool initialized = false;
static bool exitRequested = false;

void subBruteSetup() {
    if (initialized) return;

    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawSubconfigUI();  // Icon bar instead of title bar

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextSize(2);
    drawCenteredText(50, "BRUTE FORCE", HALEHOUND_HOTPINK, 2);
    drawCenteredText(90, "CC1101 REQUIRED", HALEHOUND_MAGENTA, 2);

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setTextSize(1);
    tft.setCursor(10, 130);
    tft.print("SubGHz Brute Force requires");
    tft.setCursor(10, 145);
    tft.print("CC1101 radio module.");

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(10, SCREEN_HEIGHT - 25);
    tft.print("Tap BACK icon to return");

    exitRequested = false;
    initialized = true;
}

void subBruteLoop() {
    if (!initialized) return;

    touchButtonsUpdate();

    if (isScBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        exitRequested = true;
    }
}

bool isExitRequested() { return exitRequested; }

void cleanup() {
    initialized = false;
    exitRequested = false;
}

}  // namespace subbrute
