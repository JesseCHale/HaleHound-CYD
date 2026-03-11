#ifndef LOOT_MANAGER_H
#define LOOT_MANAGER_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Unified Loot Manager
// Central hub for all attack loot: Wardriving, EAPOL, WhisperPair, IoT, Creds
// Created: 2026-03-11
//
// Browse wardriving CSVs with summary stats, view IoT Recon reports and
// captured credentials with color-coded text, delegate to existing
// SavedCaptures and WPLootViewer modules for EAPOL and WhisperPair loot.
//
// NOTE: Does NOT call SD.end() — just deselects CS pin on cleanup.
//       SD.end() destabilizes shared SPI bus (CC1101, NRF24 share VSPI).
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>

namespace LootManager {

// Initialize — mount SD, count files per category, draw hub
void setup();

// Main loop — handles touch, phase transitions, sub-viewers
void loop();

// Check if user requested exit
bool isExitRequested();

// Cleanup — release SD, free heap allocations
void cleanup();

}  // namespace LootManager

#endif // LOOT_MANAGER_H
