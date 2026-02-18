// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Touch Button Implementation
// Replaces PCF8574 I2C button expander with touchscreen zones
// Uses SOFTWARE BIT-BANGED SPI (CYD28_TouchscreenR by Piotr Zapart / hexefx.com)
// to avoid VSPI conflict with NRF24/CC1101 radios. Proven working approach.
// Created: 2026-02-06
// Updated: 2026-02-11 - Switched to Piotr Zapart's CYD28_TouchscreenR library
// ═══════════════════════════════════════════════════════════════════════════

#include "touch_buttons.h"
#include "icon.h"
#include "shared.h"
#include "CYD28_TouchscreenR.h"

// ═══════════════════════════════════════════════════════════════════════════
// XPT2046 TOUCH CONTROLLER - SOFTWARE BIT-BANGED SPI
// Uses GPIO bit-banging instead of hardware SPI to avoid conflict with
// NRF24/CC1101 radios which use hardware VSPI (pins 18,19,23).
// Touch pins: CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36
// ═══════════════════════════════════════════════════════════════════════════

// CYD28 touch controller instance (Piotr Zapart / hexefx.com, MIT license)
// Uses software SPI when begin() is called without SPI parameter
// Portrait mode (240x320) to match TFT rotation 0
CYD28_TouchR touch(CYD_SCREEN_WIDTH, CYD_SCREEN_HEIGHT);

// ═══════════════════════════════════════════════════════════════════════════
// INTERNAL STATE
// ═══════════════════════════════════════════════════════════════════════════

static ButtonState buttonStates[BTN_COUNT];
static uint32_t buttonPressTime[BTN_COUNT];
static uint32_t lastUpdateTime = 0;
static uint32_t lastRepeatTime = 0;
static ButtonID lastButton = BTN_NONE;
static ButtonEvent currentEvent;
static bool touchFeedbackEnabled = false;
static bool initialized = false;

// Touch calibration values (raw XPT2046 values are 0-4095)
// These map raw touch to screen coordinates
// Default values for 2.8" CYD - may need adjustment
static uint16_t calMinX = 200;
static uint16_t calMaxX = 3800;
static uint16_t calMinY = 200;
static uint16_t calMaxY = 3800;

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH ZONE DEFINITIONS (from cyd_config.h)
// ═══════════════════════════════════════════════════════════════════════════

struct TouchZone {
    ButtonID id;
    uint16_t x1, y1, x2, y2;
};

static const TouchZone touchZones[] = {
    { BTN_UP,     TOUCH_BTN_UP_X1,   TOUCH_BTN_UP_Y1,   TOUCH_BTN_UP_X2,   TOUCH_BTN_UP_Y2 },
    { BTN_DOWN,   TOUCH_BTN_DOWN_X1, TOUCH_BTN_DOWN_Y1, TOUCH_BTN_DOWN_X2, TOUCH_BTN_DOWN_Y2 },
    { BTN_SELECT, TOUCH_BTN_SEL_X1,  TOUCH_BTN_SEL_Y1,  TOUCH_BTN_SEL_X2,  TOUCH_BTN_SEL_Y2 },
    { BTN_BACK,   TOUCH_BTN_BACK_X1, TOUCH_BTN_BACK_Y1, TOUCH_BTN_BACK_X2, TOUCH_BTN_BACK_Y2 },
};

static const int NUM_TOUCH_ZONES = sizeof(touchZones) / sizeof(TouchZone);

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

void touchButtonsSetup() {
    // Initialize button states
    for (int i = 0; i < BTN_COUNT; i++) {
        buttonStates[i] = BTN_STATE_IDLE;
        buttonPressTime[i] = 0;
    }

    // Setup hardware BOOT button (GPIO0)
    pinMode(BOOT_BUTTON, INPUT_PULLUP);

    // Initialize touch controller using SOFTWARE BIT-BANGED SPI
    // This avoids conflict with NRF24/CC1101 on hardware VSPI
    // Calling begin() without SPI parameter = software SPI mode
    touch.begin();
    touch.setRotation(1);  // Rotation 1 - direct mapping

    // Clear current event
    currentEvent.button = BTN_NONE;
    currentEvent.state = BTN_STATE_IDLE;

    initialized = true;

    #if CYD_DEBUG
    Serial.println("[TOUCH] CYD28_TouchR initialized with SOFTWARE SPI");
    Serial.println("[TOUCH] Pins - CLK:25 MOSI:32 MISO:39 CS:33 IRQ:36");
    Serial.println("[TOUCH] BOOT button on GPIO " + String(BOOT_BUTTON));
    Serial.println("[TOUCH] Touch zones defined: " + String(NUM_TOUCH_ZONES));
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// VISUAL TOUCH TEST - Shows raw values on screen for debugging
// ═══════════════════════════════════════════════════════════════════════════

void runTouchTest() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 5);
    tft.println("TOUCH TEST 2.8\"");
    tft.setTextSize(1);
    tft.setCursor(10, 30);
    tft.println("Touch corners to see raw values");
    tft.setCursor(10, 45);
    tft.println("BOOT=exit, dots use OUR mapping");

    // Draw corner markers
    tft.fillCircle(10, 80, 5, TFT_RED);      // Top-left marker
    tft.fillCircle(230, 80, 5, TFT_GREEN);   // Top-right marker
    tft.fillCircle(10, 310, 5, TFT_BLUE);    // Bottom-left marker
    tft.fillCircle(230, 310, 5, TFT_YELLOW); // Bottom-right marker

    tft.drawRect(0, 60, 240, 260, TFT_CYAN); // Touch area box

    while (digitalRead(BOOT_BUTTON) == HIGH) {
        CYD28_TS_Point p = touch.getPointRaw();

        // Clear info area at very bottom
        tft.fillRect(0, 0, 240, 55, TFT_BLACK);

        tft.setTextSize(1);
        tft.setTextColor(TFT_YELLOW);
        tft.setCursor(5, 5);
        tft.printf("RAW X:%4d Y:%4d Z:%4d", p.x, p.y, p.z);

        if (p.z > 100) {
            // OUR OWN DIRECT MAPPING - based on actual measured values
            // X and Y are SWAPPED! rawY controls screenX, rawX controls screenY
            // Calibrated from corner measurements: rawX 1050->Y80, rawX 3600->Y310
            int screenX = map(p.y, 3780, 350, 0, 239);   // rawY -> screenX (inverted)
            int screenY = map(p.x, 150, 3700, 0, 319);   // rawX -> screenY (extrapolated)

            // Clamp to screen
            screenX = constrain(screenX, 0, 239);
            screenY = constrain(screenY, 0, 319);

            tft.setTextColor(TFT_GREEN);
            tft.setCursor(5, 18);
            tft.printf("SCREEN X:%3d Y:%3d", screenX, screenY);

            tft.setCursor(5, 35);
            tft.setTextColor(TFT_MAGENTA);
            tft.print("TOUCHED! Drawing dot...");

            // Draw dot with OUR mapping
            tft.fillCircle(screenX, screenY, 4, TFT_MAGENTA);
        } else {
            tft.setTextColor(TFT_RED);
            tft.setCursor(5, 18);
            tft.print("NO TOUCH - tap screen");
        }

        delay(30);
    }

    while (digitalRead(BOOT_BUTTON) == LOW) delay(10);
    tft.fillScreen(TFT_BLACK);
}

// ═══════════════════════════════════════════════════════════════════════════
// SPI BUS MANAGEMENT - NO LONGER NEEDED
// Software bit-banged SPI doesn't conflict with hardware VSPI
// ═══════════════════════════════════════════════════════════════════════════

void touchReinitSPI() {
    // No-op - software SPI doesn't need reinitialization
    // Kept for API compatibility
}

// ═══════════════════════════════════════════════════════════════════════════
// RAW TOUCH FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

bool isTouched() {
    return touch.touched();
}

bool getTouchPoint(uint16_t *x, uint16_t *y) {
    if (!touch.touched()) {
        return false;
    }

    // Use raw values and OUR calibrated mapping (X/Y swapped, rawY inverted)
    // Calibrated for 2.8" CYD: rawY 3780->X0, rawY 350->X239, rawX 150->Y0, rawX 3700->Y319
    CYD28_TS_Point p = touch.getPointRaw();

    // Check if pressure is sufficient
    if (p.z < TOUCH_MIN_PRESSURE) {
        return false;
    }

    // Apply our calibrated mapping (X and Y are swapped in raw values)
    int16_t screenX = map(p.y, 3780, 350, 0, 239);   // rawY -> screenX (inverted)
    int16_t screenY = map(p.x, 150, 3700, 0, 319);   // rawX -> screenY

    // Clamp to screen bounds
    if (screenX < 0) screenX = 0;
    if (screenX >= CYD_SCREEN_WIDTH) screenX = CYD_SCREEN_WIDTH - 1;
    if (screenY < 0) screenY = 0;
    if (screenY >= CYD_SCREEN_HEIGHT) screenY = CYD_SCREEN_HEIGHT - 1;

    *x = (uint16_t)screenX;
    *y = (uint16_t)screenY;

    return true;
}

ButtonID getTouchZone(uint16_t x, uint16_t y) {
    for (int i = 0; i < NUM_TOUCH_ZONES; i++) {
        if (x >= touchZones[i].x1 && x <= touchZones[i].x2 &&
            y >= touchZones[i].y1 && y <= touchZones[i].y2) {
            return touchZones[i].id;
        }
    }
    return BTN_NONE;
}

// Get screen X coordinate (returns -1 if not touched)
int getTouchX() {
    if (!touch.touched()) return -1;

    CYD28_TS_Point p = touch.getPointRaw();
    if (p.z < TOUCH_MIN_PRESSURE) return -1;

    // Apply our calibrated mapping (rawY -> screenX, inverted)
    int screenX = map(p.y, 3780, 350, 0, 239);
    screenX = constrain(screenX, 0, CYD_SCREEN_WIDTH - 1);

    return screenX;
}

// Get screen Y coordinate (returns -1 if not touched)
int getTouchY() {
    if (!touch.touched()) return -1;

    CYD28_TS_Point p = touch.getPointRaw();
    if (p.z < TOUCH_MIN_PRESSURE) return -1;

    // Apply our calibrated mapping (rawX -> screenY)
    int screenY = map(p.x, 150, 3700, 0, 319);
    screenY = constrain(screenY, 0, CYD_SCREEN_HEIGHT - 1);

    return screenY;
}

// Get which menu item was tapped (-1 if none or not touched)
int getTouchedMenuItem(int startY, int itemHeight, int itemCount) {
    if (!touch.touched()) return -1;

    CYD28_TS_Point p = touch.getPointRaw();
    if (p.z < TOUCH_MIN_PRESSURE) return -1;

    // Apply our calibrated mapping (rawX -> screenY)
    int screenY = map(p.x, 150, 3700, 0, 319);

    // Check if touch is in menu area
    if (screenY < startY) return -1;

    int item = (screenY - startY) / itemHeight;

    if (item >= 0 && item < itemCount) {
        return item;
    }

    return -1;
}

// Back button position - MATCHES ORIGINAL ESP32-DIV (icon at x=10, y=20)
#define BACK_ICON_X  10
#define BACK_ICON_Y  20
#define BACK_ICON_SIZE 16

// Draw visible BACK button - bitmap icon at x=10 like original ESP32-DIV
void drawBackButton() {
    tft.drawBitmap(BACK_ICON_X, BACK_ICON_Y, bitmap_icon_go_back, BACK_ICON_SIZE, BACK_ICON_SIZE, HALEHOUND_CYAN);
}

// Check if BACK button tapped - checks icon area x=10-26, y=20-36
bool isBackButtonTapped() {
    static uint32_t lastTap = 0;

    if (!touch.touched()) return false;
    if (millis() - lastTap < 300) return false;

    CYD28_TS_Point p = touch.getPointRaw();
    if (p.z < TOUCH_MIN_PRESSURE) return false;

    // Apply our calibrated mapping (X/Y swapped)
    int screenX = map(p.y, 3780, 350, 0, 239);
    int screenY = map(p.x, 150, 3700, 0, 319);

    // Check icon area at x=10-26, y=20-36 (16x16 icon)
    if (screenX >= BACK_ICON_X && screenX <= BACK_ICON_X + BACK_ICON_SIZE &&
        screenY >= BACK_ICON_Y && screenY <= BACK_ICON_Y + BACK_ICON_SIZE) {
        lastTap = millis();
        return true;
    }

    return false;
}

// Check if touch is within a rectangular area
bool isTouchInArea(int x, int y, int w, int h) {
    static uint32_t lastTap = 0;

    if (!touch.touched()) return false;
    if (millis() - lastTap < 200) return false;

    CYD28_TS_Point p = touch.getPointRaw();
    if (p.z < TOUCH_MIN_PRESSURE) return false;

    // Apply our calibrated mapping (X/Y swapped)
    int screenX = map(p.y, 3780, 350, 0, 239);
    int screenY = map(p.x, 150, 3700, 0, 319);

    if (screenX >= x && screenX <= x + w &&
        screenY >= y && screenY <= y + h) {
        lastTap = millis();
        return true;
    }

    return false;
}

// Check if BOOT button (GPIO0) is pressed
bool isBootButtonPressed() {
    return (digitalRead(BOOT_BUTTON) == LOW);
}

// ═══════════════════════════════════════════════════════════════════════════
// CORE UPDATE FUNCTION
// ═══════════════════════════════════════════════════════════════════════════

void touchButtonsUpdate() {
    if (!initialized) return;

    uint32_t now = millis();

    // Debounce check
    if (now - lastUpdateTime < TOUCH_DEBOUNCE_MS) {
        return;
    }
    lastUpdateTime = now;

    // Clear event from previous frame
    currentEvent.button = BTN_NONE;
    currentEvent.state = BTN_STATE_IDLE;

    // Check touch screen
    uint16_t touchX, touchY;
    ButtonID touchedButton = BTN_NONE;

    if (getTouchPoint(&touchX, &touchY)) {
        touchedButton = getTouchZone(touchX, touchY);
        currentEvent.x = touchX;
        currentEvent.y = touchY;
    }

    // Check hardware BOOT button
    if (digitalRead(BOOT_BUTTON) == LOW) {
        touchedButton = BTN_BOOT;
    }

    // Update button states
    for (int i = 1; i < BTN_COUNT; i++) {  // Skip BTN_NONE (index 0)
        ButtonID btn = (ButtonID)i;
        bool isPressed = (btn == touchedButton);

        switch (buttonStates[i]) {
            case BTN_STATE_IDLE:
                if (isPressed) {
                    buttonStates[i] = BTN_STATE_PRESSED;
                    buttonPressTime[i] = now;
                    currentEvent.button = btn;
                    currentEvent.state = BTN_STATE_PRESSED;
                    currentEvent.pressTime = now;
                    currentEvent.holdTime = 0;
                    lastButton = btn;
                }
                break;

            case BTN_STATE_PRESSED:
                if (isPressed) {
                    // Transition to held after threshold
                    if (now - buttonPressTime[i] > TOUCH_HOLD_THRESHOLD_MS) {
                        buttonStates[i] = BTN_STATE_HELD;
                        currentEvent.button = btn;
                        currentEvent.state = BTN_STATE_HELD;
                        currentEvent.holdTime = now - buttonPressTime[i];
                    }
                } else {
                    // Released quickly - normal press
                    buttonStates[i] = BTN_STATE_RELEASED;
                    currentEvent.button = btn;
                    currentEvent.state = BTN_STATE_RELEASED;
                    currentEvent.holdTime = now - buttonPressTime[i];
                }
                break;

            case BTN_STATE_HELD:
                if (isPressed) {
                    // Still held - generate repeat events
                    if (now - lastRepeatTime > TOUCH_REPEAT_MS) {
                        currentEvent.button = btn;
                        currentEvent.state = BTN_STATE_HELD;
                        currentEvent.holdTime = now - buttonPressTime[i];
                        lastRepeatTime = now;
                    }
                } else {
                    // Released after hold
                    buttonStates[i] = BTN_STATE_RELEASED;
                    currentEvent.button = btn;
                    currentEvent.state = BTN_STATE_RELEASED;
                    currentEvent.holdTime = now - buttonPressTime[i];
                }
                break;

            case BTN_STATE_RELEASED:
                // Always transition back to idle
                buttonStates[i] = BTN_STATE_IDLE;
                if (btn == lastButton) {
                    lastButton = BTN_NONE;
                }
                break;
        }
    }
}

ButtonEvent touchButtonsGetEvent() {
    return currentEvent;
}

// ═══════════════════════════════════════════════════════════════════════════
// SIMPLE BUTTON CHECKS
// ═══════════════════════════════════════════════════════════════════════════

bool buttonPressed(ButtonID btn) {
    if (btn == BTN_NONE || btn >= BTN_COUNT) return false;
    return buttonStates[btn] == BTN_STATE_PRESSED;
}

bool buttonHeld(ButtonID btn) {
    if (btn == BTN_NONE || btn >= BTN_COUNT) return false;
    return buttonStates[btn] == BTN_STATE_HELD;
}

bool buttonReleased(ButtonID btn) {
    if (btn == BTN_NONE || btn >= BTN_COUNT) return false;
    return buttonStates[btn] == BTN_STATE_RELEASED;
}

bool anyButtonPressed() {
    for (int i = 1; i < BTN_COUNT; i++) {
        if (buttonStates[i] == BTN_STATE_PRESSED ||
            buttonStates[i] == BTN_STATE_HELD) {
            return true;
        }
    }
    return false;
}

ButtonID getCurrentButton() {
    for (int i = 1; i < BTN_COUNT; i++) {
        if (buttonStates[i] == BTN_STATE_PRESSED ||
            buttonStates[i] == BTN_STATE_HELD) {
            return (ButtonID)i;
        }
    }
    return BTN_NONE;
}

// ═══════════════════════════════════════════════════════════════════════════
// PCF8574 COMPATIBILITY LAYER
// ═══════════════════════════════════════════════════════════════════════════

bool isUpPressed() {
    return buttonStates[BTN_UP] == BTN_STATE_PRESSED ||
           buttonStates[BTN_UP] == BTN_STATE_HELD;
}

bool isDownPressed() {
    return buttonStates[BTN_DOWN] == BTN_STATE_PRESSED ||
           buttonStates[BTN_DOWN] == BTN_STATE_HELD;
}

bool isLeftPressed() {
    return buttonStates[BTN_LEFT] == BTN_STATE_PRESSED ||
           buttonStates[BTN_LEFT] == BTN_STATE_HELD;
}

bool isRightPressed() {
    return buttonStates[BTN_RIGHT] == BTN_STATE_PRESSED ||
           buttonStates[BTN_RIGHT] == BTN_STATE_HELD;
}

bool isSelectPressed() {
    return buttonStates[BTN_SELECT] == BTN_STATE_PRESSED ||
           buttonStates[BTN_SELECT] == BTN_STATE_HELD;
}

bool isBackPressed() {
    // Back = tap bottom 40 pixels of screen OR BOOT button
    static uint32_t lastBackTouch = 0;

    if (touch.touched() && millis() - lastBackTouch > 300) {
        CYD28_TS_Point p = touch.getPointRaw();
        if (p.z >= TOUCH_MIN_PRESSURE) {
            // Apply our calibrated mapping (rawX -> screenY)
            int screenY = map(p.x, 150, 3700, 0, 319);
            if (screenY > CYD_SCREEN_HEIGHT - 40) {
                lastBackTouch = millis();
                return true;
            }
        }
    }

    // BOOT button also works as back
    if (digitalRead(BOOT_BUTTON) == LOW) {
        return true;
    }

    return false;
}

uint8_t readButtonMask() {
    // Returns inverted bitmask (0 = pressed) to match PCF8574 behavior
    uint8_t mask = 0xFF;

    if (isUpPressed())     mask &= ~(1 << 0);
    if (isDownPressed())   mask &= ~(1 << 1);
    if (isLeftPressed())   mask &= ~(1 << 2);
    if (isRightPressed())  mask &= ~(1 << 3);
    if (isSelectPressed()) mask &= ~(1 << 4);
    if (isBackPressed())   mask &= ~(1 << 5);
    if (buttonStates[BTN_BOOT] != BTN_STATE_IDLE) mask &= ~(1 << 6);

    return mask;
}

// ═══════════════════════════════════════════════════════════════════════════
// MENU NAVIGATION HELPERS
// ═══════════════════════════════════════════════════════════════════════════

ButtonID waitForButton() {
    clearButtonEvents();

    while (true) {
        touchButtonsUpdate();

        if (currentEvent.state == BTN_STATE_PRESSED) {
            return currentEvent.button;
        }

        delay(10);
    }
}

ButtonID waitForButtonTimeout(uint32_t timeoutMs) {
    clearButtonEvents();
    uint32_t startTime = millis();

    while (millis() - startTime < timeoutMs) {
        touchButtonsUpdate();

        if (currentEvent.state == BTN_STATE_PRESSED) {
            return currentEvent.button;
        }

        delay(10);
    }

    return BTN_NONE;
}

void waitForRelease() {
    while (anyButtonPressed()) {
        touchButtonsUpdate();
        delay(10);
    }
}

void clearButtonEvents() {
    for (int i = 0; i < BTN_COUNT; i++) {
        buttonStates[i] = BTN_STATE_IDLE;
        buttonPressTime[i] = 0;
    }
    currentEvent.button = BTN_NONE;
    currentEvent.state = BTN_STATE_IDLE;
    lastButton = BTN_NONE;
}

// ═══════════════════════════════════════════════════════════════════════════
// VISUAL FEEDBACK
// ═══════════════════════════════════════════════════════════════════════════

void setTouchFeedback(bool enabled) {
    touchFeedbackEnabled = enabled;
}

void drawTouchZones(uint16_t color) {
    for (int i = 0; i < NUM_TOUCH_ZONES; i++) {
        tft.drawRect(
            touchZones[i].x1,
            touchZones[i].y1,
            touchZones[i].x2 - touchZones[i].x1,
            touchZones[i].y2 - touchZones[i].y1,
            color
        );
    }
}

void drawTouchLabels(uint16_t color) {
    tft.setTextColor(color);
    tft.setTextSize(1);

    for (int i = 0; i < NUM_TOUCH_ZONES; i++) {
        uint16_t centerX = (touchZones[i].x1 + touchZones[i].x2) / 2;
        uint16_t centerY = (touchZones[i].y1 + touchZones[i].y2) / 2;

        String label = getButtonName(touchZones[i].id);
        int16_t textWidth = label.length() * 6;  // Approximate

        tft.setCursor(centerX - textWidth/2, centerY - 4);
        tft.print(label);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// CALIBRATION
// ═══════════════════════════════════════════════════════════════════════════

void setTouchCalibration(uint16_t minX, uint16_t maxX, uint16_t minY, uint16_t maxY) {
    calMinX = minX;
    calMaxX = maxX;
    calMinY = minY;
    calMaxY = maxY;

    #if CYD_DEBUG
    Serial.println("[TOUCH] Calibration set: X(" + String(minX) + "-" + String(maxX) +
                   ") Y(" + String(minY) + "-" + String(maxY) + ")");
    #endif
}

void runTouchCalibration() {
    // 4-point calibration using RAW values (getPointScaled is broken!)
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, CYD_SCREEN_HEIGHT / 2 - 20);
    tft.println("Touch Calibration");
    tft.setCursor(20, CYD_SCREEN_HEIGHT / 2);
    tft.println("Touch the corners");
    tft.setCursor(20, CYD_SCREEN_HEIGHT / 2 + 20);
    tft.println("when prompted");

    delay(2000);

    // Collect calibration points - RAW values
    uint16_t rawPoints[4][2];  // 4 corners, x/y each
    const char* cornerNames[] = {"TOP-LEFT", "TOP-RIGHT", "BOTTOM-LEFT", "BOTTOM-RIGHT"};
    int cornerX[] = {20, CYD_SCREEN_WIDTH - 20, 20, CYD_SCREEN_WIDTH - 20};
    int cornerY[] = {20, 20, CYD_SCREEN_HEIGHT - 20, CYD_SCREEN_HEIGHT - 20};

    for (int i = 0; i < 4; i++) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setCursor(20, CYD_SCREEN_HEIGHT / 2);
        tft.print("Touch ");
        tft.println(cornerNames[i]);

        // Draw crosshair at target
        tft.drawLine(cornerX[i] - 10, cornerY[i], cornerX[i] + 10, cornerY[i], TFT_MAGENTA);
        tft.drawLine(cornerX[i], cornerY[i] - 10, cornerX[i], cornerY[i] + 10, TFT_MAGENTA);

        // Wait for touch
        while (!touch.touched()) {
            delay(10);
        }

        // Get RAW point (NOT getPointScaled - that's BROKEN!)
        CYD28_TS_Point p = touch.getPointRaw();
        rawPoints[i][0] = p.x;
        rawPoints[i][1] = p.y;

        // Show what we captured
        tft.setTextColor(TFT_GREEN);
        tft.setCursor(20, CYD_SCREEN_HEIGHT / 2 + 40);
        tft.printf("Raw X:%d Y:%d", p.x, p.y);

        // Wait for release
        while (touch.touched()) {
            delay(10);
        }

        delay(500);
    }

    // Display all captured raw values
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 20);
    tft.println("RAW VALUES CAPTURED:");
    tft.println();

    for (int i = 0; i < 4; i++) {
        tft.printf("%s: X=%d Y=%d\n", cornerNames[i], rawPoints[i][0], rawPoints[i][1]);
    }

    tft.println();
    tft.setTextColor(TFT_YELLOW);
    tft.println("NOTE: X/Y are SWAPPED on 2.8\" CYD!");
    tft.println("rawY -> screenX (inverted)");
    tft.println("rawX -> screenY");
    tft.println();
    tft.setTextColor(TFT_CYAN);
    tft.println("Use runTouchTest() to verify");
    tft.println("Press BOOT to exit");

    #if CYD_DEBUG
    Serial.println("[TOUCH] Calibration raw values:");
    for (int i = 0; i < 4; i++) {
        Serial.printf("  %s: rawX=%d rawY=%d\n", cornerNames[i], rawPoints[i][0], rawPoints[i][1]);
    }
    #endif

    // Wait for BOOT button
    while (digitalRead(BOOT_BUTTON) == HIGH) {
        delay(10);
    }
    while (digitalRead(BOOT_BUTTON) == LOW) {
        delay(10);
    }

    tft.fillScreen(TFT_BLACK);
}

// ═══════════════════════════════════════════════════════════════════════════
// DEBUG
// ═══════════════════════════════════════════════════════════════════════════

String getButtonName(ButtonID btn) {
    switch (btn) {
        case BTN_NONE:   return "NONE";
        case BTN_UP:     return "UP";
        case BTN_DOWN:   return "DOWN";
        case BTN_LEFT:   return "LEFT";
        case BTN_RIGHT:  return "RIGHT";
        case BTN_SELECT: return "SELECT";
        case BTN_BACK:   return "BACK";
        case BTN_BOOT:   return "BOOT";
        default:         return "?";
    }
}

void printTouchDebug() {
    #if CYD_DEBUG
    Serial.println("═══════════════════════════════════════");
    Serial.println("         TOUCH DEBUG (XPT2046)");
    Serial.println("═══════════════════════════════════════");

    bool touched = touch.touched();
    Serial.println("Touched:    " + String(touched ? "YES" : "NO"));

    if (touched) {
        CYD28_TS_Point p = touch.getPointScaled();
        Serial.println("Raw X:      " + String(p.x));
        Serial.println("Raw Y:      " + String(p.y));
        Serial.println("Raw Z:      " + String(p.z));

        uint16_t screenX, screenY;
        if (getTouchPoint(&screenX, &screenY)) {
            Serial.println("Screen X:   " + String(screenX));
            Serial.println("Screen Y:   " + String(screenY));
            Serial.println("Zone:       " + getButtonName(getTouchZone(screenX, screenY)));
        }
    }

    Serial.println("BOOT btn:   " + String(digitalRead(BOOT_BUTTON) == LOW ? "PRESSED" : "released"));
    Serial.println("───────────────────────────────────────");
    Serial.println("Calibration:");
    Serial.println("  X: " + String(calMinX) + " - " + String(calMaxX));
    Serial.println("  Y: " + String(calMinY) + " - " + String(calMaxY));
    Serial.println("───────────────────────────────────────");
    Serial.println("Button states:");

    for (int i = 1; i < BTN_COUNT; i++) {
        String state;
        switch (buttonStates[i]) {
            case BTN_STATE_IDLE:     state = "idle"; break;
            case BTN_STATE_PRESSED:  state = "PRESSED"; break;
            case BTN_STATE_HELD:     state = "HELD"; break;
            case BTN_STATE_RELEASED: state = "released"; break;
        }
        Serial.println("  " + getButtonName((ButtonID)i) + ": " + state);
    }

    Serial.println("═══════════════════════════════════════");
    #endif
}
