#ifndef SHARED_H
#define SHARED_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Shared Definitions
// Color palette and shared state variables
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════

#include "cyd_config.h"

// ═══════════════════════════════════════════════════════════════════════════
// HALEHOUND COLOR PALETTE
// Jesse's Custom: Red/Purple/Pink theme (no yellow/orange)
// Theme colors are extern — runtime-adjustable for colorblind modes
// ═══════════════════════════════════════════════════════════════════════════

// Theme colors — changed by applyColorMode() at runtime
extern uint16_t HALEHOUND_MAGENTA;   // Primary (selected items)
extern uint16_t HALEHOUND_HOTPINK;   // Accents
extern uint16_t HALEHOUND_BRIGHT;    // Highlights
extern uint16_t HALEHOUND_VIOLET;    // Accent color
extern uint16_t HALEHOUND_CYAN;      // Text color
extern uint16_t HALEHOUND_GREEN;     // Secondary accent

// Fixed colors — never change with color mode
const uint16_t HALEHOUND_DARK = 0x2841;     // #2B080A - Dark backgrounds
const uint16_t HALEHOUND_BLACK = 0x0000;    // #000000 - Pure black
const uint16_t HALEHOUND_GUNMETAL = 0x18E3; // #1C1C1C - Gunmetal gray

// ═══════════════════════════════════════════════════════════════════════════
// LEGACY COLOR MAPPINGS (for compatibility with original code)
// Macros so they follow theme color changes at runtime
// ═══════════════════════════════════════════════════════════════════════════

#define SHREDDY_TEAL    HALEHOUND_MAGENTA
#define SHREDDY_PINK    HALEHOUND_MAGENTA
#define SHREDDY_BLACK   HALEHOUND_BLACK
#define SHREDDY_BLUE    HALEHOUND_MAGENTA
#define SHREDDY_PURPLE  HALEHOUND_VIOLET
#define SHREDDY_GREEN   HALEHOUND_GREEN
#define SHREDDY_GUNMETAL HALEHOUND_GUNMETAL

// Standard colors
#define ORANGE          HALEHOUND_MAGENTA
const uint16_t GRAY = 0x8410;
#define BLUE            HALEHOUND_MAGENTA
const uint16_t RED = 0xF800;
#define GREEN           HALEHOUND_GREEN
const uint16_t BLACK = 0x0000;
const uint16_t WHITE = 0xFFFF;
const uint16_t LIGHT_GRAY = 0xC618;
const uint16_t DARK_GRAY = HALEHOUND_GUNMETAL;

// TFT color defines
#define TFT_DARKBLUE  0x3166
#define TFT_LIGHTBLUE HALEHOUND_MAGENTA
#define TFTWHITE     0xFFFF
#define TFT_GRAY      0x8410
#define SELECTED_ICON_COLOR HALEHOUND_MAGENTA

// ═══════════════════════════════════════════════════════════════════════════
// MENU STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════

extern bool in_sub_menu;
extern bool feature_active;
extern bool submenu_initialized;
extern bool is_main_menu;
extern bool feature_exit_requested;

// ═══════════════════════════════════════════════════════════════════════════
// CYD-SPECIFIC STATE
// ═══════════════════════════════════════════════════════════════════════════

extern bool gps_enabled;
extern bool gps_has_fix;

// ═══════════════════════════════════════════════════════════════════════════
// FUNCTION DECLARATIONS
// ═══════════════════════════════════════════════════════════════════════════

void displaySubmenu();

// ═══════════════════════════════════════════════════════════════════════════
// SCREEN DIMENSIONS (from cyd_config.h)
// ═══════════════════════════════════════════════════════════════════════════

#define SCREEN_WIDTH  CYD_SCREEN_WIDTH
#define SCREEN_HEIGHT CYD_SCREEN_HEIGHT

// ═══════════════════════════════════════════════════════════════════════════
// STATUS BAR HEIGHT (for GPS indicator, battery, etc.)
// ═══════════════════════════════════════════════════════════════════════════

#define STATUS_BAR_HEIGHT 0

#endif // SHARED_H
