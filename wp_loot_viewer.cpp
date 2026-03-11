// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD WhisperPair Loot Viewer
// SD card browser for /wp_loot/ directory (CVE-2025-36911 attack reports)
// Created: 2026-03-10
//
// Browse, inspect, serial-dump, and delete WhisperPair loot files.
// Follows SavedCaptures pattern: namespace with setup/loop/isExitRequested/cleanup.
// NOTE: Does NOT call SD.end() — just deselects CS pin on cleanup.
//       SD.end() trashes SPI bus and prevents wpSaveLoot() from working.
// ═══════════════════════════════════════════════════════════════════════════

#include "wp_loot_viewer.h"
#include "spi_manager.h"
#include "touch_buttons.h"
#include "shared.h"
#include "utils.h"
#include "icon.h"
#include "nosifer_font.h"
#include <TFT_eSPI.h>
#include <SD.h>
#include <SPI.h>

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

namespace WPLootViewer {

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define WL_DIR              "/wp_loot"
#define WL_MAX_FILES        16
#define WL_LIST_Y           74       // Top of file list area (below title+separator)
#define WL_LIST_BOTTOM      (SCREEN_HEIGHT - 28)
#define WL_ROW_HEIGHT       24
#define WL_VISIBLE_ROWS     ((WL_LIST_BOTTOM - WL_LIST_Y) / WL_ROW_HEIGHT)

// ═══════════════════════════════════════════════════════════════════════════
// FILE ENTRY — heap-allocated to save DRAM BSS
// ═══════════════════════════════════════════════════════════════════════════

struct WLFileEntry {
    char name[36];      // Filename: "AA-BB-CC-DD-EE-FF_12345.txt" (max ~28 chars)
    uint32_t size;
};

// ═══════════════════════════════════════════════════════════════════════════
// PARSED LOOT — heap-allocated, loaded on detail/view
// ═══════════════════════════════════════════════════════════════════════════

struct WLParsedLoot {
    char target[24];        // "Pixel Buds Pro"
    char bleMac[18];        // "AA:BB:CC:DD:EE:FF"
    int  rssi;
    char modelId[12];       // "0xD99330"
    char brEdr[18];         // BR/EDR address or "N/A"
    // Phase 1
    char strategies[4][24]; // "RAW_KBP: HIT", etc.
    int  strategyCount;
    char kbpResp[48];       // Hex string of KBP response
    // Phase 2
    bool acctKeyWritten;
    char acctKey[48];       // Hex string of account key
    char acctStatus[16];    // "INJECTED" or "Not written"
    // Phase 3
    char passkey[10];       // "Found" or "Absent"
    char additional[10];    // "Found" or "Absent"
    char modelIdGatt[12];   // "0xAABBCC" from GATT read
    char firmware[20];
    bool valid;             // Parsing succeeded
};

// ═══════════════════════════════════════════════════════════════════════════
// STATE — files and loot are heap-allocated to minimize DRAM BSS usage
// ═══════════════════════════════════════════════════════════════════════════

enum Phase {
    PHASE_LIST,         // File list view
    PHASE_DETAIL,       // Single file summary
    PHASE_VIEW,         // Full parsed attack report (2 pages)
    PHASE_CONFIRM_DEL,  // Delete confirmation
    PHASE_EMPTY         // No files found
};

// Packed state struct — avoids per-variable alignment padding (saves ~18 bytes BSS)
static struct {
    WLFileEntry*  files;          // Heap-allocated file array
    WLParsedLoot* pLoot;          // Heap-allocated parsed loot
    int16_t       fileCount;
    int16_t       scrollOffset;
    int16_t       selectedIndex;
    uint8_t       phase;          // Phase enum
    uint8_t       viewPage;
    bool          exitRequested;
    bool          sdMounted;
} wl = { nullptr, nullptr, 0, 0, -1, PHASE_LIST, 0, false, false };

// Convenience aliases — keep all code readable without wl. prefix
#define files           wl.files
#define pLoot           wl.pLoot
#define fileCount       wl.fileCount
#define scrollOffset    wl.scrollOffset
#define selectedIndex   wl.selectedIndex
#define phase           wl.phase
#define viewPage        wl.viewPage
#define exitRequested   wl.exitRequested
#define sdMounted       wl.sdMounted

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static void formatSize(uint32_t bytes, char* buf, int bufLen) {
    if (bytes < 1024) {
        snprintf(buf, bufLen, "%luB", (unsigned long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufLen, "%.1fKB", bytes / 1024.0f);
    } else {
        snprintf(buf, bufLen, "%.1fMB", bytes / (1024.0f * 1024.0f));
    }
}

// Extract display name from filename (strip .txt, replace dashes with colons in MAC portion)
static void getDisplayName(const char* filename, char* out, int outLen) {
    strncpy(out, filename, outLen - 1);
    out[outLen - 1] = '\0';
    // Strip .txt extension
    char* dot = strrchr(out, '.');
    if (dot && strcasecmp(dot, ".txt") == 0) {
        *dot = '\0';
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// LOCAL ICON BAR & BACK BUTTON (module-scoped, not from .ino)
// ═══════════════════════════════════════════════════════════════════════════

static void drawWLIconBar() {
    tft.fillRect(0, ICON_BAR_Y, tft.width(), ICON_BAR_H, HALEHOUND_DARK);
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, tft.width(), ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

// Back button detection is now inline in loop() — matches WhisperPair's pattern
// (single getTouchPoint per frame, no separate function)

// ═══════════════════════════════════════════════════════════════════════════
// SD CARD & DIRECTORY SCANNING
// ═══════════════════════════════════════════════════════════════════════════

static bool mountSD() {
    #if CYD_DEBUG
    Serial.printf("[WPLOOT] mountSD — heap: %u\n", ESP.getFreeHeap());
    #endif

    // Identical to SavedCaptures::mountSD() which is proven working
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    // Use max_files=1 to minimize heap allocation (BLE eats a lot of heap)
    if (SD.begin(SD_CS, SPI, 4000000, "/sd", 1)) {
        #if CYD_DEBUG
        Serial.println("[WPLOOT] SD mount OK (default SPI)");
        #endif
        return true;
    }

    // Retry with explicit SPI init
    SPI.begin(18, 19, 23, SD_CS);
    if (SD.begin(SD_CS, SPI, 4000000, "/sd", 1)) {
        #if CYD_DEBUG
        Serial.println("[WPLOOT] SD mount OK (explicit SPI)");
        #endif
        return true;
    }

    #if CYD_DEBUG
    Serial.printf("[WPLOOT] SD mount FAILED — heap: %u\n", ESP.getFreeHeap());
    #endif
    return false;
}

static int scanDirectory() {
    fileCount = 0;

    if (!SD.exists(WL_DIR)) {
        return 0;
    }

    File dir = SD.open(WL_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    File entry;
    while ((entry = dir.openNextFile()) && fileCount < WL_MAX_FILES) {
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        const char* name = entry.name();
        // Only accept .txt files
        int len = strlen(name);
        if (len < 5 || strcasecmp(name + len - 4, ".txt") != 0) {
            entry.close();
            continue;
        }

        // Strip leading path if present
        const char* basename = strrchr(name, '/');
        if (basename) {
            basename++;
        } else {
            basename = name;
        }

        strncpy(files[fileCount].name, basename, sizeof(files[fileCount].name) - 1);
        files[fileCount].name[sizeof(files[fileCount].name) - 1] = '\0';
        files[fileCount].size = entry.size();
        fileCount++;

        entry.close();
    }

    dir.close();
    return fileCount;
}

// ═══════════════════════════════════════════════════════════════════════════
// LOOT FILE PARSING
// ═══════════════════════════════════════════════════════════════════════════

// Read a line from an open file into buffer. Returns false if EOF.
static bool readLine(File& f, char* buf, int bufLen) {
    int i = 0;
    while (f.available() && i < bufLen - 1) {
        char c = f.read();
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (i > 0 || f.available());
}

// Trim leading whitespace in-place, return pointer to first non-space
static const char* trimLeft(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void parseLootFile(int idx) {
    if (!pLoot) pLoot = (WLParsedLoot*)malloc(sizeof(WLParsedLoot));
    if (!pLoot) return;
    memset(pLoot, 0, sizeof(WLParsedLoot));
    pLoot->valid = false;
    pLoot->rssi = 0;
    pLoot->strategyCount = 0;
    pLoot->acctKeyWritten = false;
    strcpy(pLoot->brEdr, "N/A");
    strcpy(pLoot->passkey, "N/A");
    strcpy(pLoot->additional, "N/A");
    strcpy(pLoot->modelIdGatt, "N/A");
    pLoot->firmware[0] = '\0';
    pLoot->kbpResp[0] = '\0';
    pLoot->acctKey[0] = '\0';
    strcpy(pLoot->acctStatus, "N/A");

    if (idx < 0 || idx >= fileCount) return;

    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", WL_DIR, files[idx].name);

    File f = SD.open(fullPath, FILE_READ);
    if (!f) return;

    char line[128];
    int currentPhase = 0;  // 0=header, 1=phase1, 2=phase2, 3=phase3

    while (readLine(f, line, sizeof(line))) {
        const char* t = trimLeft(line);

        // Target: name
        if (strncmp(t, "Target:", 7) == 0) {
            const char* val = trimLeft(t + 7);
            strncpy(pLoot->target, val, sizeof(pLoot->target) - 1);
            pLoot->target[sizeof(pLoot->target) - 1] = '\0';
            pLoot->valid = true;
        }
        // BLE MAC: AA:BB:CC:DD:EE:FF
        else if (strncmp(t, "BLE MAC:", 8) == 0) {
            const char* val = trimLeft(t + 8);
            strncpy(pLoot->bleMac, val, sizeof(pLoot->bleMac) - 1);
            pLoot->bleMac[sizeof(pLoot->bleMac) - 1] = '\0';
        }
        // RSSI: -42 dBm
        else if (strncmp(t, "RSSI:", 5) == 0) {
            const char* val = trimLeft(t + 5);
            pLoot->rssi = atoi(val);
        }
        // Model: 0xD99330
        else if (strncmp(t, "Model:", 6) == 0) {
            const char* val = trimLeft(t + 6);
            strncpy(pLoot->modelId, val, sizeof(pLoot->modelId) - 1);
            pLoot->modelId[sizeof(pLoot->modelId) - 1] = '\0';
        }
        // BR/EDR Address: XX:XX:XX:XX:XX:XX or "Not extracted"
        else if (strncmp(t, "BR/EDR Address:", 15) == 0) {
            const char* val = trimLeft(t + 15);
            strncpy(pLoot->brEdr, val, sizeof(pLoot->brEdr) - 1);
            pLoot->brEdr[sizeof(pLoot->brEdr) - 1] = '\0';
        }
        // Phase markers
        else if (strncmp(t, "-- Phase 1:", 11) == 0) {
            currentPhase = 1;
        }
        else if (strncmp(t, "-- Phase 2:", 11) == 0) {
            currentPhase = 2;
        }
        else if (strncmp(t, "-- Phase 3:", 11) == 0) {
            currentPhase = 3;
        }
        // KBP Response: hex bytes
        else if (strncmp(t, "KBP Response:", 13) == 0) {
            const char* val = trimLeft(t + 13);
            strncpy(pLoot->kbpResp, val, sizeof(pLoot->kbpResp) - 1);
            pLoot->kbpResp[sizeof(pLoot->kbpResp) - 1] = '\0';
        }
        // Phase 1 strategy lines — "  RAW_KBP: HIT" etc.
        else if (currentPhase == 1 && (*t >= 'A' && *t <= 'Z') && pLoot->strategyCount < 4) {
            strncpy(pLoot->strategies[pLoot->strategyCount], t, sizeof(pLoot->strategies[0]) - 1);
            pLoot->strategies[pLoot->strategyCount][sizeof(pLoot->strategies[0]) - 1] = '\0';
            pLoot->strategyCount++;
        }
        // Phase 2 — Key: hex
        else if (currentPhase == 2 && strncmp(t, "Key:", 4) == 0) {
            const char* val = trimLeft(t + 4);
            strncpy(pLoot->acctKey, val, sizeof(pLoot->acctKey) - 1);
            pLoot->acctKey[sizeof(pLoot->acctKey) - 1] = '\0';
        }
        // Phase 2 — Status: INJECTED / Not written
        else if (currentPhase == 2 && strncmp(t, "Status:", 7) == 0) {
            const char* val = trimLeft(t + 7);
            strncpy(pLoot->acctStatus, val, sizeof(pLoot->acctStatus) - 1);
            pLoot->acctStatus[sizeof(pLoot->acctStatus) - 1] = '\0';
            if (strncmp(val, "INJECTED", 8) == 0) {
                pLoot->acctKeyWritten = true;
            }
        }
        // Phase 3 — Passkey char:
        else if (currentPhase == 3 && strncmp(t, "Passkey char:", 13) == 0) {
            const char* val = trimLeft(t + 13);
            strncpy(pLoot->passkey, val, sizeof(pLoot->passkey) - 1);
            pLoot->passkey[sizeof(pLoot->passkey) - 1] = '\0';
        }
        // Phase 3 — AdditionalData:
        else if (currentPhase == 3 && strncmp(t, "AdditionalData:", 15) == 0) {
            const char* val = trimLeft(t + 15);
            strncpy(pLoot->additional, val, sizeof(pLoot->additional) - 1);
            pLoot->additional[sizeof(pLoot->additional) - 1] = '\0';
        }
        // Phase 3 — Model ID: 0xAABBCC
        else if (currentPhase == 3 && strncmp(t, "Model ID:", 9) == 0) {
            const char* val = trimLeft(t + 9);
            strncpy(pLoot->modelIdGatt, val, sizeof(pLoot->modelIdGatt) - 1);
            pLoot->modelIdGatt[sizeof(pLoot->modelIdGatt) - 1] = '\0';
        }
        // Phase 3 — Firmware:
        else if (currentPhase == 3 && strncmp(t, "Firmware:", 9) == 0) {
            const char* val = trimLeft(t + 9);
            strncpy(pLoot->firmware, val, sizeof(pLoot->firmware) - 1);
            pLoot->firmware[sizeof(pLoot->firmware) - 1] = '\0';
        }
    }

    f.close();
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

static bool deleteFile(int idx) {
    if (idx < 0 || idx >= fileCount) return false;

    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", WL_DIR, files[idx].name);

    if (!SD.remove(fullPath)) return false;

    // Shift remaining files down
    for (int i = idx; i < fileCount - 1; i++) {
        files[i] = files[i + 1];
    }
    fileCount--;

    // Fix selection
    if (selectedIndex >= fileCount) selectedIndex = fileCount - 1;
    if (scrollOffset > 0 && scrollOffset + WL_VISIBLE_ROWS > fileCount) {
        scrollOffset = fileCount - WL_VISIBLE_ROWS;
        if (scrollOffset < 0) scrollOffset = 0;
    }

    return true;
}

static void dumpToSerial(int idx) {
    if (idx < 0 || idx >= fileCount) return;

    WLFileEntry& fe = files[idx];
    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", WL_DIR, fe.name);

    Serial.println();
    Serial.println("════════════════════════════════════════════");
    Serial.printf("  HALEHOUND WP LOOT DUMP: %s\n", fe.name);
    Serial.println("════════════════════════════════════════════");

    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
        Serial.println("ERROR: Cannot open file");
        return;
    }

    // Dump raw contents
    Serial.println();
    while (f.available()) {
        char line[128];
        int i = 0;
        while (f.available() && i < (int)sizeof(line) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c == '\r') continue;
            line[i++] = c;
        }
        line[i] = '\0';
        if (i > 0) Serial.println(line);
    }

    f.close();

    Serial.println();
    Serial.println("════════════════════════════════════════════");
    Serial.printf("SIZE: %lu bytes\n", (unsigned long)fe.size);
    Serial.println("════════════════════════════════════════════");
    Serial.println();
}

// ═══════════════════════════════════════════════════════════════════════════
// DRAWING — LIST VIEW
// ═══════════════════════════════════════════════════════════════════════════

static void drawListHeader() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawWLIconBar();
    drawGlitchText(55, "WP LOOT", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    // Column headers
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("FILE", 6, WL_LIST_Y - 12);
    tft.drawString("SIZE", 205, WL_LIST_Y - 12);

    // Separator line
    tft.drawFastHLine(4, WL_LIST_Y - 2, 232, HALEHOUND_VIOLET);
}

static void drawFileRow(int screenRow, int fileIdx, bool selected) {
    int y = WL_LIST_Y + screenRow * WL_ROW_HEIGHT;

    // Clear row
    tft.fillRect(2, y, SCREEN_WIDTH - 4, WL_ROW_HEIGHT - 1, selected ? HALEHOUND_VIOLET : TFT_BLACK);

    if (fileIdx < 0 || fileIdx >= fileCount) return;

    WLFileEntry& f = files[fileIdx];

    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(selected ? TFT_WHITE : HALEHOUND_MAGENTA);

    // Truncate filename for display
    char displayName[28];
    getDisplayName(f.name, displayName, sizeof(displayName));
    // Further truncate if needed
    if (strlen(displayName) > 26) {
        displayName[26] = '\0';
    }
    tft.drawString(displayName, 6, y + 4);

    // Size
    char sizeBuf[12];
    formatSize(f.size, sizeBuf, sizeof(sizeBuf));
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(sizeBuf, 205, y + 4);
}

static void drawFileList() {
    for (int i = 0; i < WL_VISIBLE_ROWS; i++) {
        int fileIdx = scrollOffset + i;
        if (fileIdx < fileCount) {
            drawFileRow(i, fileIdx, fileIdx == selectedIndex);
        } else {
            int y = WL_LIST_Y + i * WL_ROW_HEIGHT;
            tft.fillRect(2, y, SCREEN_WIDTH - 4, WL_ROW_HEIGHT - 1, TFT_BLACK);
        }
    }

    // Scroll indicators
    if (scrollOffset > 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("^", 228, WL_LIST_Y);
    }
    if (scrollOffset + WL_VISIBLE_ROWS < fileCount) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("v", 228, WL_LIST_BOTTOM - 12);
    }

    // File count at bottom
    tft.fillRect(0, WL_LIST_BOTTOM + 2, tft.width(), 16, TFT_BLACK);
    char countBuf[32];
    snprintf(countBuf, sizeof(countBuf), "%d FILE%s", fileCount, fileCount == 1 ? "" : "S");
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(countBuf, 120, WL_LIST_BOTTOM + 4);
    tft.setTextDatum(TL_DATUM);

    // Bottom bar
    tft.fillRect(0, tft.height() - 20, tft.width(), 20, TFT_BLACK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("TAP FILE TO VIEW  |  BACK TO EXIT", 120, 304);
    tft.setTextDatum(TL_DATUM);
}

static void drawEmptyScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawWLIconBar();
    drawGlitchText(55, "WP LOOT", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    drawGlitchStatus(120, "NO FILES", HALEHOUND_HOTPINK);
    drawCenteredText(160, "No loot found in /wp_loot/", HALEHOUND_GUNMETAL, 1);
    drawCenteredText(180, "Run WhisperPair attack first", HALEHOUND_GUNMETAL, 1);

    if (!sdMounted) {
        drawCenteredText(210, "SD CARD NOT DETECTED", HALEHOUND_HOTPINK, 1);
        // Show heap for debugging — helps diagnose if BLE ate all memory
        char heapBuf[32];
        snprintf(heapBuf, sizeof(heapBuf), "Free heap: %u", ESP.getFreeHeap());
        drawCenteredText(240, heapBuf, HALEHOUND_GUNMETAL, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DRAWING — DETAIL VIEW (summary of one loot file)
// ═══════════════════════════════════════════════════════════════════════════

static void drawDetailView() {
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;
    if (!pLoot) return;

    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawWLIconBar();

    // Target name as title (or filename if target unknown)
    const char* titleStr = pLoot->valid && pLoot->target[0] ? pLoot->target : files[selectedIndex].name;
    char titleBuf[20];
    strncpy(titleBuf, titleStr, 19);
    titleBuf[19] = '\0';
    drawGlitchText(55, titleBuf, &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    // Frame box
    tft.drawRoundRect(10, 64, CONTENT_INNER_W, 100, 4, HALEHOUND_VIOLET);
    tft.drawRoundRect(11, 65, CONTENT_INNER_W - 2, 98, 4, HALEHOUND_VIOLET);

    int y = 72;

    // BLE MAC
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(16, y);
    tft.print("MAC: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->bleMac[0] ? pLoot->bleMac : "?");
    y += 14;

    // Model ID
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(16, y);
    tft.print("Model: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->modelId[0] ? pLoot->modelId : "?");
    y += 14;

    // RSSI
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(16, y);
    tft.print("RSSI: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    if (pLoot->rssi != 0) {
        tft.printf("%d dBm", pLoot->rssi);
    } else {
        tft.print("?");
    }
    y += 14;

    // BR/EDR
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(16, y);
    tft.print("BR/EDR: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->brEdr);
    y += 14;

    // Result summary
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(16, y);
    tft.print("Key: ");
    if (pLoot->acctKeyWritten) {
        tft.setTextColor(0x07E0);  // Green
        tft.print("INJECTED");
    } else if (pLoot->acctKey[0]) {
        tft.setTextColor(0xFFE0);  // Yellow
        tft.print("NOT WRITTEN");
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.print("N/A");
    }
    y += 14;

    // File size
    char sizeBuf[16];
    formatSize(files[selectedIndex].size, sizeBuf, sizeof(sizeBuf));
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(16, y);
    tft.printf("Size: %s", sizeBuf);

    // Three action buttons: VIEW | SERIAL | DELETE at y=180
    int btnW = (SCREEN_WIDTH - 26) / 3;
    int btn1X = 8;
    int btn2X = btn1X + btnW + 3;
    int btn3X = btn2X + btnW + 3;

    // VIEW button
    tft.drawRoundRect(btn1X, 180, btnW, 32, 4, HALEHOUND_MAGENTA);
    tft.drawRoundRect(btn1X + 1, 181, btnW - 2, 30, 4, HALEHOUND_MAGENTA);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.drawString("VIEW", btn1X + btnW / 2, 190);

    // SERIAL button
    tft.drawRoundRect(btn2X, 180, btnW, 32, 4, HALEHOUND_VIOLET);
    tft.drawRoundRect(btn2X + 1, 181, btnW - 2, 30, 4, HALEHOUND_VIOLET);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.drawString("SERIAL", btn2X + btnW / 2, 190);

    // DELETE button
    tft.drawRoundRect(btn3X, 180, btnW, 32, 4, HALEHOUND_HOTPINK);
    tft.drawRoundRect(btn3X + 1, 181, btnW - 2, 30, 4, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("DELETE", btn3X + btnW / 2, 190);

    tft.setTextDatum(TL_DATUM);

    // Bottom hint
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString("BACK ARROW TO RETURN TO LIST", 120, 224);
    tft.setTextDatum(TL_DATUM);
}

static void drawDeleteConfirm() {
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;

    // Overlay confirmation box
    tft.fillRect(20, 120, SCREEN_WIDTH - 40, 80, TFT_BLACK);
    tft.drawRoundRect(20, 120, SCREEN_WIDTH - 40, 80, 4, HALEHOUND_HOTPINK);
    tft.drawRoundRect(21, 121, SCREEN_WIDTH - 42, 78, 4, HALEHOUND_HOTPINK);

    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("DELETE THIS FILE?", 120, 130);

    // Truncated filename
    char nameBuf[28];
    strncpy(nameBuf, files[selectedIndex].name, 27);
    nameBuf[27] = '\0';
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.drawString(nameBuf, 120, 148);

    // YES / NO buttons
    tft.drawRoundRect(35, 168, 70, 24, 3, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("YES", 70, 173);

    tft.drawRoundRect(135, 168, 70, 24, 3, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.drawString("NO", 170, 173);

    tft.setTextDatum(TL_DATUM);
}

// ═══════════════════════════════════════════════════════════════════════════
// DRAWING — VIEW SCREEN (full parsed attack report, 2 pages)
// ═══════════════════════════════════════════════════════════════════════════

static void drawViewPage1() {
    if (!pLoot) return;
    // Page 1: Target info + Phase 1 results
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawWLIconBar();

    drawGlitchText(55, "REPORT 1/2", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    int y = 64;

    // Target info section
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(6, y);
    tft.print("TARGET");
    y += 14;

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, y);
    tft.print(pLoot->target[0] ? pLoot->target : "Unknown");
    y += 12;

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("MAC: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->bleMac[0] ? pLoot->bleMac : "?");
    y += 12;

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("Model: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->modelId[0] ? pLoot->modelId : "?");
    tft.print("  RSSI: ");
    if (pLoot->rssi != 0) {
        tft.printf("%d", pLoot->rssi);
    } else {
        tft.print("?");
    }
    y += 12;

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("BR/EDR: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->brEdr);
    y += 16;

    // Separator
    tft.drawFastHLine(6, y, SCREEN_WIDTH - 12, HALEHOUND_DARK);
    y += 6;

    // Phase 1 section
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(6, y);
    tft.print("PHASE 1: KBP EXPLOIT");
    y += 14;

    for (int i = 0; i < pLoot->strategyCount; i++) {
        // Color by result
        if (strstr(pLoot->strategies[i], "HIT")) {
            tft.setTextColor(0x07E0);  // Green for HIT
        } else if (strstr(pLoot->strategies[i], "MISS")) {
            tft.setTextColor(0xF800);  // Red for MISS
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);  // Gray for N/A
        }
        tft.setCursor(14, y);
        tft.print(pLoot->strategies[i]);
        y += 12;
    }

    if (pLoot->strategyCount == 0) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(14, y);
        tft.print("No strategy data");
        y += 12;
    }

    y += 4;

    // KBP Response
    if (pLoot->kbpResp[0]) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(6, y);
        tft.print("KBP RESPONSE:");
        y += 12;
        tft.setTextColor(HALEHOUND_MAGENTA);
        // Split long hex into two lines if needed
        int respLen = strlen(pLoot->kbpResp);
        if (respLen <= 30) {
            tft.setCursor(10, y);
            tft.print(pLoot->kbpResp);
        } else {
            char part1[32];
            strncpy(part1, pLoot->kbpResp, 30);
            part1[30] = '\0';
            tft.setCursor(10, y);
            tft.print(part1);
            y += 12;
            tft.setCursor(10, y);
            tft.print(pLoot->kbpResp + 30);
        }
    }

    // Bottom: page indicator + nav hint
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString("TAP RIGHT FOR PAGE 2", 120, 304);
    tft.setTextDatum(TL_DATUM);
}

static void drawViewPage2() {
    if (!pLoot) return;
    // Page 2: Phase 2 (account key) + Phase 3 (intel)
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawWLIconBar();

    drawGlitchText(55, "REPORT 2/2", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    int y = 64;

    // Phase 2 section
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(6, y);
    tft.print("PHASE 2: ACCOUNT KEY");
    y += 14;

    // Account key
    if (pLoot->acctKey[0]) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(10, y);
        tft.print("Key:");
        y += 12;
        tft.setTextColor(HALEHOUND_MAGENTA);
        // Split long hex into two lines if needed
        int keyLen = strlen(pLoot->acctKey);
        if (keyLen <= 30) {
            tft.setCursor(14, y);
            tft.print(pLoot->acctKey);
        } else {
            char part1[32];
            strncpy(part1, pLoot->acctKey, 30);
            part1[30] = '\0';
            tft.setCursor(14, y);
            tft.print(part1);
            y += 12;
            tft.setCursor(14, y);
            tft.print(pLoot->acctKey + 30);
        }
        y += 14;
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(10, y);
        tft.print("No account key data");
        y += 14;
    }

    // Status
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("Status: ");
    if (pLoot->acctKeyWritten) {
        tft.setTextColor(0x07E0);  // Green
        tft.print("INJECTED");
    } else {
        tft.setTextColor(0xF800);  // Red
        tft.print(pLoot->acctStatus);
    }
    y += 20;

    // Separator
    tft.drawFastHLine(6, y, SCREEN_WIDTH - 12, HALEHOUND_DARK);
    y += 6;

    // Phase 3 section
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(6, y);
    tft.print("PHASE 3: INTEL");
    y += 14;

    // Passkey
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("Passkey: ");
    if (strcmp(pLoot->passkey, "Found") == 0) {
        tft.setTextColor(0x07E0);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
    }
    tft.print(pLoot->passkey);
    y += 12;

    // AdditionalData
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("AddlData: ");
    if (strcmp(pLoot->additional, "Found") == 0) {
        tft.setTextColor(0x07E0);
    } else {
        tft.setTextColor(HALEHOUND_GUNMETAL);
    }
    tft.print(pLoot->additional);
    y += 12;

    // Model ID from GATT
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("Model ID: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->modelIdGatt);
    y += 12;

    // Firmware
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, y);
    tft.print("Firmware: ");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.print(pLoot->firmware[0] ? pLoot->firmware : "N/A");

    // Bottom: page indicator + nav hint
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString("TAP LEFT FOR PAGE 1  |  BACK", 120, 304);
    tft.setTextDatum(TL_DATUM);
}

static void drawViewScreen() {
    if (viewPage == 0) {
        drawViewPage1();
    } else {
        drawViewPage2();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

static void handleListTouch(int tx, int ty) {
    // Check if touch is in the file list area
    if (ty >= WL_LIST_Y && ty < WL_LIST_BOTTOM && tx >= 2 && tx <= 238) {
        int row = (ty - WL_LIST_Y) / WL_ROW_HEIGHT;
        int fileIdx = scrollOffset + row;

        if (fileIdx >= 0 && fileIdx < fileCount) {
            if (fileIdx == selectedIndex) {
                // Double-tap: open detail view
                parseLootFile(selectedIndex);
                phase = PHASE_DETAIL;
                drawDetailView();
            } else {
                // First tap: select
                int oldSel = selectedIndex;
                selectedIndex = fileIdx;

                // Redraw only changed rows
                if (oldSel >= scrollOffset && oldSel < scrollOffset + WL_VISIBLE_ROWS) {
                    drawFileRow(oldSel - scrollOffset, oldSel, false);
                }
                drawFileRow(fileIdx - scrollOffset, fileIdx, true);
            }
        }
    }

    // Scroll up zone (top 20px of list)
    if (ty >= WL_LIST_Y && ty < WL_LIST_Y + 20 && scrollOffset > 0) {
        scrollOffset--;
        drawFileList();
    }

    // Scroll down zone (bottom 20px of list)
    if (ty > WL_LIST_BOTTOM - 20 && ty <= WL_LIST_BOTTOM && scrollOffset + WL_VISIBLE_ROWS < fileCount) {
        scrollOffset++;
        drawFileList();
    }
}

static void handleDetailTouch(int tx, int ty) {
    int btnW = (SCREEN_WIDTH - 26) / 3;
    int btn1X = 8;
    int btn2X = btn1X + btnW + 3;
    int btn3X = btn2X + btnW + 3;

    // VIEW button
    if (tx >= btn1X && tx <= btn1X + btnW && ty >= 180 && ty <= 212) {
        viewPage = 0;
        phase = PHASE_VIEW;
        drawViewScreen();
        return;
    }

    // SERIAL button
    if (tx >= btn2X && tx <= btn2X + btnW && ty >= 180 && ty <= 212) {
        // Flash button feedback
        tft.fillRoundRect(btn2X + 1, 181, btnW - 2, 30, 4, HALEHOUND_VIOLET);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_BLACK);
        tft.drawString("DUMPING", btn2X + btnW / 2, 190);
        tft.setTextDatum(TL_DATUM);

        dumpToSerial(selectedIndex);

        // Show confirmation
        tft.fillRoundRect(btn2X + 1, 181, btnW - 2, 30, 4, HALEHOUND_MAGENTA);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_BLACK);
        tft.drawString("SENT!", btn2X + btnW / 2, 190);
        tft.setTextDatum(TL_DATUM);
        delay(1000);

        // Redraw normal button
        tft.fillRoundRect(btn2X + 1, 181, btnW - 2, 30, 4, TFT_BLACK);
        tft.drawRoundRect(btn2X, 180, btnW, 32, 4, HALEHOUND_VIOLET);
        tft.drawRoundRect(btn2X + 1, 181, btnW - 2, 30, 4, HALEHOUND_VIOLET);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.drawString("SERIAL", btn2X + btnW / 2, 190);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    // DELETE button
    if (tx >= btn3X && tx <= btn3X + btnW && ty >= 180 && ty <= 212) {
        phase = PHASE_CONFIRM_DEL;
        drawDeleteConfirm();
        return;
    }
}

static void handleConfirmTouch(int tx, int ty) {
    // YES button: x=35-105, y=168-192
    if (tx >= 35 && tx <= 105 && ty >= 168 && ty <= 192) {
        if (deleteFile(selectedIndex)) {
            phase = PHASE_LIST;
            if (fileCount == 0) {
                phase = PHASE_EMPTY;
                drawEmptyScreen();
            } else {
                drawListHeader();
                drawFileList();
            }
        } else {
            // Failed — show error briefly, return to detail
            tft.fillRect(20, 120, SCREEN_WIDTH - 40, 80, TFT_BLACK);
            tft.drawRoundRect(20, 120, SCREEN_WIDTH - 40, 80, 4, HALEHOUND_HOTPINK);
            tft.setTextDatum(TC_DATUM);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.drawString("DELETE FAILED", 120, 150);
            tft.setTextDatum(TL_DATUM);
            delay(1000);
            phase = PHASE_DETAIL;
            drawDetailView();
        }
        return;
    }

    // NO button: x=135-205, y=168-192
    if (tx >= 135 && tx <= 205 && ty >= 168 && ty <= 192) {
        phase = PHASE_DETAIL;
        drawDetailView();
        return;
    }
}

static void handleViewTouch(int tx, int ty) {
    // Left half of screen — previous page
    if (tx < SCREEN_WIDTH / 2 && viewPage > 0) {
        viewPage--;
        drawViewScreen();
        return;
    }

    // Right half of screen — next page
    if (tx >= SCREEN_WIDTH / 2 && viewPage < 1) {
        viewPage++;
        drawViewScreen();
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC INTERFACE
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    exitRequested = false;
    phase = PHASE_LIST;
    fileCount = 0;
    scrollOffset = 0;
    selectedIndex = -1;
    viewPage = 0;

    // Heap-allocate files array (saves ~640 bytes BSS)
    if (!files) files = (WLFileEntry*)malloc(WL_MAX_FILES * sizeof(WLFileEntry));
    if (!files) {
        phase = PHASE_EMPTY;
        drawEmptyScreen();
        return;
    }

    // Mount SD
    sdMounted = mountSD();

    if (!sdMounted) {
        phase = PHASE_EMPTY;
        drawEmptyScreen();
        return;
    }

    // Scan for loot files
    int found = scanDirectory();

    if (found == 0) {
        phase = PHASE_EMPTY;
        drawEmptyScreen();
        return;
    }

    // Draw list view
    drawListHeader();
    drawFileList();
}

void loop() {
    // Single touch read per frame — matches WhisperPair's proven pattern
    static unsigned long lastTap = 0;
    if (millis() - lastTap > 200) {
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // Icon bar back button — top of screen, left side
            if (ty <= 55 && tx < 50) {
                if (phase == PHASE_VIEW) {
                    phase = PHASE_DETAIL;
                    drawDetailView();
                } else if (phase == PHASE_DETAIL || phase == PHASE_CONFIRM_DEL) {
                    phase = PHASE_LIST;
                    if (fileCount == 0) {
                        phase = PHASE_EMPTY;
                        drawEmptyScreen();
                    } else {
                        drawListHeader();
                        drawFileList();
                    }
                } else {
                    // List or empty — exit module
                    exitRequested = true;
                }
                waitForTouchRelease();
                lastTap = millis();
                return;
            }

            // Content area touches — below icon bar
            if (ty > 55) {
                switch (phase) {
                    case PHASE_LIST:
                        handleListTouch(tx, ty);
                        break;
                    case PHASE_DETAIL:
                        handleDetailTouch(tx, ty);
                        break;
                    case PHASE_CONFIRM_DEL:
                        handleConfirmTouch(tx, ty);
                        break;
                    case PHASE_VIEW:
                        handleViewTouch(tx, ty);
                        break;
                    default:
                        break;
                }
            }
            lastTap = millis();
        }
    }

    delay(20);
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    // Do NOT call SD.end() — it trashes the SPI bus state and prevents
    // subsequent SD access by wpSaveLoot() and future loot viewer opens.
    // Just deselect CS to release the bus.
    digitalWrite(SD_CS, HIGH);
    sdMounted = false;
    fileCount = 0;
    selectedIndex = -1;
    scrollOffset = 0;

    // Free heap allocations
    if (files) { free(files); files = nullptr; }
    if (pLoot) { free(pLoot); pLoot = nullptr; }
}

}  // namespace WPLootViewer
