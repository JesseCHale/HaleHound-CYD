#ifndef TOUCH_BUTTONS_H
#define TOUCH_BUTTONS_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Touch Button Handler
// Replaces PCF8574 I2C button expander with touchscreen zones
// Uses SOFTWARE BIT-BANGED SPI (CYD28_TouchscreenR by Piotr Zapart / hexefx.com)
// to avoid conflict with NRF24/CC1101 on hardware VSPI
// Created: 2026-02-06
// Updated: 2026-02-11 - Switched to Piotr Zapart's CYD28_TouchscreenR library
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <SPI.h>
#include "CYD28_TouchscreenR.h"
#include <TFT_eSPI.h>
#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// BUTTON IDENTIFIERS
// ═══════════════════════════════════════════════════════════════════════════

enum ButtonID {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_SELECT,
    BTN_BACK,
    BTN_BOOT,       // Hardware BOOT button (GPIO0)
    BTN_COUNT       // Total number of buttons
};

// ═══════════════════════════════════════════════════════════════════════════
// BUTTON STATES
// ═══════════════════════════════════════════════════════════════════════════

enum ButtonState {
    BTN_STATE_IDLE = 0,     // Not pressed
    BTN_STATE_PRESSED,      // Just pressed this frame
    BTN_STATE_HELD,         // Being held down
    BTN_STATE_RELEASED      // Just released this frame
};

// ═══════════════════════════════════════════════════════════════════════════
// BUTTON EVENT STRUCTURE
// ═══════════════════════════════════════════════════════════════════════════

struct ButtonEvent {
    ButtonID button;
    ButtonState state;
    uint16_t x;             // Touch X coordinate (if touch button)
    uint16_t y;             // Touch Y coordinate (if touch button)
    uint32_t pressTime;     // When button was first pressed
    uint32_t holdTime;      // How long button has been held
};

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define TOUCH_DEBOUNCE_MS       50      // Minimum time between presses
#define TOUCH_HOLD_THRESHOLD_MS 500     // Time before press becomes hold
#define TOUCH_REPEAT_MS         150     // Repeat rate when holding
#define TOUCH_MIN_PRESSURE      400     // XPT2046 Z_THRESHOLD from library

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL TFT OBJECT (must be defined in main sketch)
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

// Initialize touch button system - call in setup()
void touchButtonsSetup();

// ═══════════════════════════════════════════════════════════════════════════
// CORE FUNCTIONS - Call in loop()
// ═══════════════════════════════════════════════════════════════════════════

// Update button states - call once per loop iteration
void touchButtonsUpdate();

// Get the current event (if any)
ButtonEvent touchButtonsGetEvent();

// ═══════════════════════════════════════════════════════════════════════════
// SIMPLE BUTTON CHECKS - For direct polling
// ═══════════════════════════════════════════════════════════════════════════

// Check if a specific button was just pressed (single shot)
bool buttonPressed(ButtonID btn);

// Check if a specific button is currently held
bool buttonHeld(ButtonID btn);

// Check if a specific button was just released
bool buttonReleased(ButtonID btn);

// Check if any button is currently pressed/held
bool anyButtonPressed();

// Get the currently pressed button (BTN_NONE if none)
ButtonID getCurrentButton();

// ═══════════════════════════════════════════════════════════════════════════
// PCF8574 COMPATIBILITY LAYER
// These functions mimic the old PCF8574 button interface
// ═══════════════════════════════════════════════════════════════════════════

// Returns true if UP is pressed (was PCF8574 P0)
bool isUpPressed();

// Returns true if DOWN is pressed (was PCF8574 P1)
bool isDownPressed();

// Returns true if LEFT is pressed (was PCF8574 P2)
bool isLeftPressed();

// Returns true if RIGHT is pressed (was PCF8574 P3)
bool isRightPressed();

// Returns true if SELECT/OK is pressed (was PCF8574 P4)
bool isSelectPressed();

// Returns true if BACK/CANCEL is pressed (was PCF8574 P5)
bool isBackPressed();

// Compatibility: Read all buttons as bitmask (like PCF8574 read8())
// Bit 0 = UP, Bit 1 = DOWN, Bit 2 = LEFT, Bit 3 = RIGHT
// Bit 4 = SELECT, Bit 5 = BACK, Bit 6 = BOOT
// Returns inverted (0 = pressed) to match PCF8574 behavior
uint8_t readButtonMask();

// ═══════════════════════════════════════════════════════════════════════════
// MENU NAVIGATION HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Wait for any button press (blocking)
ButtonID waitForButton();

// Wait for any button press with timeout (returns BTN_NONE on timeout)
ButtonID waitForButtonTimeout(uint32_t timeoutMs);

// Wait for button release (useful after detecting press)
void waitForRelease();

// Clear any pending button events
void clearButtonEvents();

// ═══════════════════════════════════════════════════════════════════════════
// VISUAL FEEDBACK (Optional)
// ═══════════════════════════════════════════════════════════════════════════

// Enable/disable visual feedback when touch zones are pressed
void setTouchFeedback(bool enabled);

// Draw touch button zones on screen (for debugging/visualization)
void drawTouchZones(uint16_t color);

// Draw touch button labels
void drawTouchLabels(uint16_t color);

// ═══════════════════════════════════════════════════════════════════════════
// RAW TOUCH ACCESS
// ═══════════════════════════════════════════════════════════════════════════

// Check if screen is currently being touched
bool isTouched();

// Get raw touch coordinates (returns false if not touched)
bool getTouchPoint(uint16_t *x, uint16_t *y);

// Get which zone a point falls into
ButtonID getTouchZone(uint16_t x, uint16_t y);

// Get raw touch coordinates (screen-mapped)
int getTouchX();
int getTouchY();

// Get which menu item was tapped (-1 if none)
// startY = Y position of first menu item
// itemHeight = height of each menu item
// itemCount = number of menu items
int getTouchedMenuItem(int startY, int itemHeight, int itemCount);

// Draw a visible BACK button at top-left (call after drawing screen)
void drawBackButton();

// Check if the BACK button was tapped
bool isBackButtonTapped();

// Check if touch is within a rectangular area
bool isTouchInArea(int x, int y, int w, int h);

// Check if BOOT button (GPIO0) is pressed
bool isBootButtonPressed();

// Alias for touchButtonsSetup() for compatibility
inline void initButtons() { touchButtonsSetup(); }

// Alias for touchButtonsUpdate() for compatibility
inline void updateButtons() { touchButtonsUpdate(); }

// ═══════════════════════════════════════════════════════════════════════════
// SPI BUS MANAGEMENT (for VSPI conflict with NRF24/CC1101)
// ═══════════════════════════════════════════════════════════════════════════

// Reinitialize touch SPI after NRF24/radio operations
// Call this BEFORE checking touch when radios have used the SPI bus
void touchReinitSPI();

// ═══════════════════════════════════════════════════════════════════════════
// CALIBRATION (if needed)
// ═══════════════════════════════════════════════════════════════════════════

// Set touch calibration values
void setTouchCalibration(uint16_t minX, uint16_t maxX, uint16_t minY, uint16_t maxY);

// Run interactive touch calibration
void runTouchCalibration();

// Run visual touch test (shows raw values on screen)
void runTouchTest();

// ═══════════════════════════════════════════════════════════════════════════
// DEBUG
// ═══════════════════════════════════════════════════════════════════════════

// Get button name as string
String getButtonName(ButtonID btn);

// Print touch debug info to Serial
void printTouchDebug();

#endif // TOUCH_BUTTONS_H
