#ifndef WP_LOOT_VIEWER_H
#define WP_LOOT_VIEWER_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD WhisperPair Loot Viewer
// SD card browser for /wp_loot/ directory (CVE-2025-36911 attack reports)
// Created: 2026-03-10
//
// Browse, inspect, and delete WhisperPair attack loot files.
// NOTE: Does NOT call SD.end() — just deselects CS pin on cleanup.
//       SD.end() destabilizes shared SPI bus (CC1101, NRF24 share VSPI).
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

namespace WPLootViewer {

// Initialize — mount SD, scan directory, draw list
void setup();

// Main loop — handles touch, scrolling, file actions
void loop();

// Check if user requested exit
bool isExitRequested();

// Cleanup — release SD
void cleanup();

}  // namespace WPLootViewer

#endif // WP_LOOT_VIEWER_H
