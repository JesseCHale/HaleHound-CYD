// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Unified Loot Manager
// Central hub for all attack loot on the SD card
// Created: 2026-03-11
//
// Hub screen with 5 categories (Wardriving, EAPOL, WP Loot, IoT Recon,
// Credentials). Wardriving gets a built-in CSV viewer with summary stats.
// EAPOL and WP Loot delegate to their existing viewers. IoT Recon and
// Credentials use a shared scrollable text viewer with color-coded lines.
//
// NOTE: Does NOT call SD.end() — just deselects CS pin on cleanup.
//       SD.end() destabilizes shared SPI bus (CC1101, NRF24 share VSPI).
// ═══════════════════════════════════════════════════════════════════════════

#include "loot_manager.h"
#include "saved_captures.h"
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

namespace LootManager {

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define LM_WD_DIR           "/wardriving"
#define LM_EAPOL_DIR        "/eapol"
#define LM_WPLOOT_DIR       "/wp_loot"
#define LM_IOT_FILE         "/iot_recon.txt"
#define LM_CREDS_FILE       "/creds.txt"

#define LM_HUB_Y            62          // First category row Y
#define LM_HUB_ROW_H        38         // Row height on hub
#define LM_HUB_ROWS         5          // 5 categories

#define LM_WD_MAX_FILES     32         // Max wardriving CSVs
#define LM_WD_NAME_LEN      44         // Filename buffer
#define LM_WD_LIST_Y        74         // File list start Y
#define LM_WD_LIST_BOTTOM   (SCREEN_HEIGHT - 28)
#define LM_WD_ROW_H         24
#define LM_WD_VISIBLE       ((LM_WD_LIST_BOTTOM - LM_WD_LIST_Y) / LM_WD_ROW_H)

#define LM_TV_MAX_LINES     64         // Text viewer max lines
#define LM_TV_LINE_LEN      42         // Chars per line (40 + null + spare)
#define LM_TV_Y_START       64         // First text line Y
#define LM_TV_LINE_H        18         // Line height in text viewer
#define LM_TV_VISIBLE       ((SCREEN_HEIGHT - 40 - LM_TV_Y_START) / LM_TV_LINE_H)

// ═══════════════════════════════════════════════════════════════════════════
// TYPES
// ═══════════════════════════════════════════════════════════════════════════

enum Phase {
    PHASE_HUB,              // Category hub
    PHASE_WD_LIST,          // Wardriving file list
    PHASE_WD_DETAIL,        // Wardriving file summary stats
    PHASE_WD_CONFIRM_DEL,   // Wardriving delete confirmation
    PHASE_EAPOL,            // Delegate to SavedCaptures
    PHASE_WPLOOT,           // Delegate to WPLootViewer
    PHASE_TEXT_VIEW          // Scrollable text viewer (IoT Recon / Creds)
};

enum HubCategory {
    CAT_WARDRIVING = 0,
    CAT_EAPOL,
    CAT_WPLOOT,
    CAT_IOT_RECON,
    CAT_CREDENTIALS
};

struct WDFileEntry {
    char name[LM_WD_NAME_LEN];
    uint32_t size;
};

struct WDStats {
    uint32_t wifiCount;
    uint32_t bleCount;
    uint32_t openCount;
    char firstSeen[20];     // First timestamp in CSV
    char lastSeen[20];      // Last timestamp in CSV
    uint32_t fileSize;
};

struct HubInfo {
    int fileCount;
    uint32_t totalSize;
};

// ═══════════════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════════════

static Phase phase = PHASE_HUB;
static bool exitRequested = false;
static bool sdMounted = false;

// Hub state
static HubInfo hubInfo[LM_HUB_ROWS];

// Wardriving file list (heap-allocated)
static WDFileEntry* wdFiles = nullptr;
static int wdFileCount = 0;
static int wdScrollOffset = 0;
static int wdSelectedIndex = -1;
static WDStats wdStats;

// Text viewer (heap-allocated)
static char (*tvLines)[LM_TV_LINE_LEN] = nullptr;
static uint16_t* tvColors = nullptr;       // Per-line color
static int tvLineCount = 0;
static int tvScrollOffset = 0;
static const char* tvTitle = nullptr;      // Title for text viewer

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static bool mountSD() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    if (SD.begin(SD_CS)) return true;
    SPI.begin(18, 19, 23, SD_CS);
    if (SD.begin(SD_CS, SPI, 4000000)) return true;
    return false;
}

static void formatSize(uint32_t bytes, char* buf, int bufLen) {
    if (bytes < 1024) {
        snprintf(buf, bufLen, "%luB", (unsigned long)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, bufLen, "%.1fKB", bytes / 1024.0f);
    } else {
        snprintf(buf, bufLen, "%.1fMB", bytes / (1024.0f * 1024.0f));
    }
}

// Count files in a directory, sum their sizes
static HubInfo countDirFiles(const char* dirPath) {
    HubInfo info = {0, 0};
    if (!SD.exists(dirPath)) return info;
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return info;
    }
    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            info.fileCount++;
            info.totalSize += entry.size();
        }
        entry.close();
    }
    dir.close();
    return info;
}

// Count a single file as 1 if it exists, 0 if not
static HubInfo countSingleFile(const char* filePath) {
    HubInfo info = {0, 0};
    if (!SD.exists(filePath)) return info;
    File f = SD.open(filePath, FILE_READ);
    if (f) {
        info.fileCount = 1;
        info.totalSize = f.size();
        f.close();
    }
    return info;
}

// ═══════════════════════════════════════════════════════════════════════════
// LOCAL ICON BAR & BACK BUTTON
// ═══════════════════════════════════════════════════════════════════════════

static void drawLMIconBar() {
    tft.fillRect(0, ICON_BAR_Y, tft.width(), ICON_BAR_H, HALEHOUND_DARK);
    tft.drawBitmap(10, ICON_BAR_Y, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, ICON_BAR_BOTTOM, tft.width(), ICON_BAR_BOTTOM, HALEHOUND_HOTPINK);
}

static bool isLMBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= ICON_BAR_TOUCH_TOP && ty <= ICON_BAR_TOUCH_BOTTOM && tx >= 10 && tx < 30) {
            delay(150);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// HUB SCREEN
// ═══════════════════════════════════════════════════════════════════════════

static void scanHubInfo() {
    spiDeselect();
    hubInfo[CAT_WARDRIVING]  = countDirFiles(LM_WD_DIR);
    hubInfo[CAT_EAPOL]       = countDirFiles(LM_EAPOL_DIR);
    hubInfo[CAT_WPLOOT]      = countDirFiles(LM_WPLOOT_DIR);
    hubInfo[CAT_IOT_RECON]   = countSingleFile(LM_IOT_FILE);
    hubInfo[CAT_CREDENTIALS] = countSingleFile(LM_CREDS_FILE);
}

static const char* hubLabels[LM_HUB_ROWS] = {
    "WARDRIVING",
    "EAPOL",
    "WP LOOT",
    "IOT RECON",
    "CREDENTIALS"
};

static const unsigned char* hubIcons[LM_HUB_ROWS] = {
    bitmap_icon_follow,         // Wardriving
    bitmap_icon_key,            // EAPOL
    bitmap_icon_ble,            // WhisperPair loot
    bitmap_icon_scanner,        // IoT Recon
    bitmap_icon_key,            // Credentials (portal captures)
};

static void drawHub() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawLMIconBar();
    drawGlitchText(55, "LOOT", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);

    for (int i = 0; i < LM_HUB_ROWS; i++) {
        int y = LM_HUB_Y + i * LM_HUB_ROW_H;

        // Row background — subtle alternating
        if (i % 2 == 0) {
            tft.fillRect(2, y, SCREEN_WIDTH - 4, LM_HUB_ROW_H - 2, HALEHOUND_DARK);
        }

        // Icon
        tft.drawBitmap(8, y + 10, hubIcons[i], 16, 16, HALEHOUND_MAGENTA);

        // Category label
        tft.setTextColor(HALEHOUND_MAGENTA);
        tft.drawString(hubLabels[i], 30, y + 6);

        // File count + size
        char infoBuf[24];
        if (hubInfo[i].fileCount == 0) {
            snprintf(infoBuf, sizeof(infoBuf), "empty");
            tft.setTextColor(HALEHOUND_GUNMETAL);
        } else {
            char sizeBuf[10];
            formatSize(hubInfo[i].totalSize, sizeBuf, sizeof(sizeBuf));
            snprintf(infoBuf, sizeof(infoBuf), "%d file%s  %s",
                     hubInfo[i].fileCount,
                     hubInfo[i].fileCount == 1 ? "" : "s",
                     sizeBuf);
            tft.setTextColor(HALEHOUND_HOTPINK);
        }
        tft.drawString(infoBuf, 30, y + 20);

        // Chevron
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.drawString(">", SCREEN_WIDTH - 14, y + 12);
    }

    // Bottom hint
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString("TAP CATEGORY  |  BACK TO EXIT", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 14);
    tft.setTextDatum(TL_DATUM);
}

static void handleHubTouch(uint16_t tx, uint16_t ty) {
    for (int i = 0; i < LM_HUB_ROWS; i++) {
        int y = LM_HUB_Y + i * LM_HUB_ROW_H;
        if (ty >= y && ty < y + LM_HUB_ROW_H && tx >= 2 && tx < SCREEN_WIDTH - 2) {
            delay(150);
            switch (i) {
                case CAT_WARDRIVING:
                    phase = PHASE_WD_LIST;
                    break;
                case CAT_EAPOL:
                    phase = PHASE_EAPOL;
                    break;
                case CAT_WPLOOT:
                    phase = PHASE_WPLOOT;
                    break;
                case CAT_IOT_RECON:
                    phase = PHASE_TEXT_VIEW;
                    tvTitle = "IOT RECON";
                    break;
                case CAT_CREDENTIALS:
                    phase = PHASE_TEXT_VIEW;
                    tvTitle = "CREDS";
                    break;
            }
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// WARDRIVING FILE LIST
// ═══════════════════════════════════════════════════════════════════════════

static int scanWDFiles() {
    wdFileCount = 0;
    if (!wdFiles) return 0;
    if (!SD.exists(LM_WD_DIR)) return 0;

    File dir = SD.open(LM_WD_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    File entry;
    while ((entry = dir.openNextFile()) && wdFileCount < LM_WD_MAX_FILES) {
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }
        const char* name = entry.name();
        // Only .csv files
        int len = strlen(name);
        if (len < 5 || strcasecmp(name + len - 4, ".csv") != 0) {
            entry.close();
            continue;
        }
        // Strip leading path
        const char* basename = strrchr(name, '/');
        if (basename) basename++;
        else basename = name;

        strncpy(wdFiles[wdFileCount].name, basename, LM_WD_NAME_LEN - 1);
        wdFiles[wdFileCount].name[LM_WD_NAME_LEN - 1] = '\0';
        wdFiles[wdFileCount].size = entry.size();
        wdFileCount++;
        entry.close();
    }
    dir.close();
    return wdFileCount;
}

static void drawWDListHeader() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawLMIconBar();
    drawGlitchText(55, "WARDRIVING", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("FILE", 6, LM_WD_LIST_Y - 12);
    tft.drawString("SIZE", 190, LM_WD_LIST_Y - 12);
    tft.drawFastHLine(4, LM_WD_LIST_Y - 2, SCREEN_WIDTH - 8, HALEHOUND_VIOLET);
}

static void drawWDFileRow(int screenRow, int fileIdx, bool selected) {
    int y = LM_WD_LIST_Y + screenRow * LM_WD_ROW_H;
    tft.fillRect(2, y, SCREEN_WIDTH - 4, LM_WD_ROW_H - 1, selected ? HALEHOUND_VIOLET : TFT_BLACK);
    if (fileIdx < 0 || fileIdx >= wdFileCount) return;

    WDFileEntry& f = wdFiles[fileIdx];
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(selected ? TFT_WHITE : HALEHOUND_MAGENTA);

    // Display name: strip "halehound_" prefix and ".csv" suffix for cleaner look
    char displayName[24];
    const char* src = f.name;
    if (strncasecmp(src, "halehound_", 10) == 0) src += 10;
    strncpy(displayName, src, 23);
    displayName[23] = '\0';
    char* dot = strrchr(displayName, '.');
    if (dot && strcasecmp(dot, ".csv") == 0) *dot = '\0';
    tft.drawString(displayName, 6, y + 4);

    // Size
    char sizeBuf[12];
    formatSize(f.size, sizeBuf, sizeof(sizeBuf));
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(sizeBuf, 190, y + 4);
}

static void drawWDFileList() {
    for (int i = 0; i < LM_WD_VISIBLE; i++) {
        int fileIdx = wdScrollOffset + i;
        if (fileIdx < wdFileCount) {
            drawWDFileRow(i, fileIdx, fileIdx == wdSelectedIndex);
        } else {
            int y = LM_WD_LIST_Y + i * LM_WD_ROW_H;
            tft.fillRect(2, y, SCREEN_WIDTH - 4, LM_WD_ROW_H - 1, TFT_BLACK);
        }
    }

    // Scroll indicators
    if (wdScrollOffset > 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("^", SCREEN_WIDTH - 12, LM_WD_LIST_Y);
    }
    if (wdScrollOffset + LM_WD_VISIBLE < wdFileCount) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("v", SCREEN_WIDTH - 12, LM_WD_LIST_BOTTOM - 12);
    }

    // File count
    tft.fillRect(0, LM_WD_LIST_BOTTOM + 2, tft.width(), 16, TFT_BLACK);
    char countBuf[32];
    snprintf(countBuf, sizeof(countBuf), "%d FILE%s", wdFileCount, wdFileCount == 1 ? "" : "S");
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(countBuf, SCREEN_WIDTH / 2, LM_WD_LIST_BOTTOM + 4);
    tft.setTextDatum(TL_DATUM);

    // Bottom hint
    tft.fillRect(0, SCREEN_HEIGHT - 20, tft.width(), 20, TFT_BLACK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("TAP FILE TO VIEW  |  BACK TO HUB", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 16);
    tft.setTextDatum(TL_DATUM);
}

static void drawWDEmpty() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawLMIconBar();
    drawGlitchText(55, "WARDRIVING", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);
    drawGlitchStatus(120, "NO FILES", HALEHOUND_HOTPINK);
    drawCenteredText(160, "No CSVs in /wardriving/", HALEHOUND_GUNMETAL, 1);
    drawCenteredText(180, "Run Wardriving first", HALEHOUND_GUNMETAL, 1);
}

static void enterWDList() {
    wdScrollOffset = 0;
    wdSelectedIndex = -1;

    if (!wdFiles) wdFiles = (WDFileEntry*)malloc(LM_WD_MAX_FILES * sizeof(WDFileEntry));
    if (!wdFiles) {
        drawWDEmpty();
        return;
    }

    spiDeselect();
    int found = scanWDFiles();
    if (found == 0) {
        drawWDEmpty();
        return;
    }
    drawWDListHeader();
    drawWDFileList();
}

static void handleWDListTouch(uint16_t tx, uint16_t ty) {
    // Scroll zones
    if (ty >= LM_WD_LIST_Y && ty < LM_WD_LIST_Y + LM_WD_ROW_H && tx >= SCREEN_WIDTH - 20) {
        if (wdScrollOffset > 0) {
            wdScrollOffset--;
            drawWDFileList();
        }
        return;
    }
    if (ty >= LM_WD_LIST_BOTTOM - LM_WD_ROW_H && ty < LM_WD_LIST_BOTTOM && tx >= SCREEN_WIDTH - 20) {
        if (wdScrollOffset + LM_WD_VISIBLE < wdFileCount) {
            wdScrollOffset++;
            drawWDFileList();
        }
        return;
    }

    // File row tap
    if (ty >= LM_WD_LIST_Y && ty < LM_WD_LIST_BOTTOM) {
        int row = (ty - LM_WD_LIST_Y) / LM_WD_ROW_H;
        int fileIdx = wdScrollOffset + row;
        if (fileIdx >= 0 && fileIdx < wdFileCount) {
            wdSelectedIndex = fileIdx;
            phase = PHASE_WD_DETAIL;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// WARDRIVING CSV PARSER — line-by-line, 256-byte stack buffer
// ═══════════════════════════════════════════════════════════════════════════

static void parseWDFile(int idx) {
    if (idx < 0 || idx >= wdFileCount) return;

    memset(&wdStats, 0, sizeof(wdStats));
    wdStats.fileSize = wdFiles[idx].size;
    wdStats.firstSeen[0] = '\0';
    wdStats.lastSeen[0] = '\0';

    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", LM_WD_DIR, wdFiles[idx].name);

    spiDeselect();
    File f = SD.open(fullPath, FILE_READ);
    if (!f) return;

    char lineBuf[256];
    int lineNum = 0;

    while (f.available()) {
        // Read one line
        int i = 0;
        while (f.available() && i < (int)sizeof(lineBuf) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c == '\r') continue;
            lineBuf[i++] = c;
        }
        lineBuf[i] = '\0';
        lineNum++;

        // Skip header lines (WiGLE pre-header + column header)
        if (lineNum <= 2) continue;
        if (i == 0) continue;

        // CSV format: MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,Lat,Lon,Alt,Acc,RCOIs,MfgrId,Type
        // Last field is "WIFI" or "BLE"
        // AuthMode field contains "[OPEN]" for open networks

        // Find Type field (last comma-separated value)
        const char* lastComma = strrchr(lineBuf, ',');
        if (!lastComma) continue;
        const char* typeField = lastComma + 1;

        if (strcasecmp(typeField, "WIFI") == 0) {
            wdStats.wifiCount++;
        } else if (strcasecmp(typeField, "BLE") == 0) {
            wdStats.bleCount++;
        }

        // Check for [OPEN] in AuthMode field (3rd field)
        if (strstr(lineBuf, "[OPEN]")) {
            wdStats.openCount++;
        }

        // Extract timestamp (4th field = FirstSeen)
        // Walk to 3rd comma to get start of timestamp
        int commaCount = 0;
        const char* tsStart = nullptr;
        const char* tsEnd = nullptr;
        for (const char* p = lineBuf; *p; p++) {
            if (*p == ',') {
                commaCount++;
                if (commaCount == 3) tsStart = p + 1;
                if (commaCount == 4) { tsEnd = p; break; }
            }
        }

        if (tsStart && tsEnd && (tsEnd - tsStart) > 0) {
            int tsLen = tsEnd - tsStart;
            if (tsLen > 19) tsLen = 19;

            if (wdStats.firstSeen[0] == '\0') {
                strncpy(wdStats.firstSeen, tsStart, tsLen);
                wdStats.firstSeen[tsLen] = '\0';
            }
            strncpy(wdStats.lastSeen, tsStart, tsLen);
            wdStats.lastSeen[tsLen] = '\0';
        }
    }

    f.close();
}

// ═══════════════════════════════════════════════════════════════════════════
// WARDRIVING DETAIL VIEW
// ═══════════════════════════════════════════════════════════════════════════

static void drawWDDetail() {
    if (wdSelectedIndex < 0 || wdSelectedIndex >= wdFileCount) return;

    parseWDFile(wdSelectedIndex);

    WDFileEntry& f = wdFiles[wdSelectedIndex];

    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawLMIconBar();

    // Title: strip prefix/suffix for clean display
    char titleBuf[20];
    const char* src = f.name;
    if (strncasecmp(src, "halehound_", 10) == 0) src += 10;
    strncpy(titleBuf, src, 19);
    titleBuf[19] = '\0';
    char* dot = strrchr(titleBuf, '.');
    if (dot && strcasecmp(dot, ".csv") == 0) *dot = '\0';
    drawGlitchText(55, titleBuf, &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    // Stats box
    tft.drawRoundRect(10, 64, CONTENT_INNER_W, 120, 4, HALEHOUND_VIOLET);
    tft.drawRoundRect(11, 65, CONTENT_INNER_W - 2, 118, 4, HALEHOUND_VIOLET);

    int ly = 74;

    // WiFi count
    tft.setTextColor(HALEHOUND_MAGENTA);
    char buf[48];
    snprintf(buf, sizeof(buf), "WiFi Networks: %lu", (unsigned long)wdStats.wifiCount);
    tft.drawString(buf, 18, ly);
    ly += 16;

    // BLE count
    tft.setTextColor(HALEHOUND_VIOLET);
    snprintf(buf, sizeof(buf), "BLE Devices:   %lu", (unsigned long)wdStats.bleCount);
    tft.drawString(buf, 18, ly);
    ly += 16;

    // Open networks
    tft.setTextColor(wdStats.openCount > 0 ? HALEHOUND_GREEN : HALEHOUND_GUNMETAL);
    snprintf(buf, sizeof(buf), "Open Networks: %lu", (unsigned long)wdStats.openCount);
    tft.drawString(buf, 18, ly);
    ly += 16;

    // Total entries
    tft.setTextColor(HALEHOUND_HOTPINK);
    snprintf(buf, sizeof(buf), "Total Entries: %lu", (unsigned long)(wdStats.wifiCount + wdStats.bleCount));
    tft.drawString(buf, 18, ly);
    ly += 20;

    // Date range
    if (wdStats.firstSeen[0] != '\0') {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        // Show just the date portion (first 10 chars)
        char dateBuf[32];
        char first10[11] = {0};
        char last10[11] = {0};
        strncpy(first10, wdStats.firstSeen, 10);
        strncpy(last10, wdStats.lastSeen, 10);
        snprintf(dateBuf, sizeof(dateBuf), "%s - %s", first10, last10);
        tft.drawString(dateBuf, 18, ly);
    }
    ly += 16;

    // File size
    char sizeBuf[12];
    formatSize(wdStats.fileSize, sizeBuf, sizeof(sizeBuf));
    tft.setTextColor(HALEHOUND_GUNMETAL);
    snprintf(buf, sizeof(buf), "File Size: %s", sizeBuf);
    tft.drawString(buf, 18, ly);

    // Two action buttons: SERIAL | DELETE
    int btnW = (SCREEN_WIDTH - 20) / 2;
    int btn1X = 8;
    int btn2X = btn1X + btnW + 4;
    int btnY = 196;

    // SERIAL button
    tft.drawRoundRect(btn1X, btnY, btnW, 32, 4, HALEHOUND_VIOLET);
    tft.drawRoundRect(btn1X + 1, btnY + 1, btnW - 2, 30, 4, HALEHOUND_VIOLET);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.drawString("SERIAL", btn1X + btnW / 2, btnY + 10);

    // DELETE button
    tft.drawRoundRect(btn2X, btnY, btnW, 32, 4, HALEHOUND_HOTPINK);
    tft.drawRoundRect(btn2X + 1, btnY + 1, btnW - 2, 30, 4, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("DELETE", btn2X + btnW / 2, btnY + 10);

    tft.setTextDatum(TL_DATUM);

    // Bottom hint
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString("BACK ARROW TO RETURN TO LIST", SCREEN_WIDTH / 2, 240);
    tft.setTextDatum(TL_DATUM);
}

static void serialDumpWDFile() {
    if (wdSelectedIndex < 0 || wdSelectedIndex >= wdFileCount) return;

    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", LM_WD_DIR, wdFiles[wdSelectedIndex].name);

    spiDeselect();
    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
        Serial.println("[LOOT] Cannot open file for serial dump");
        return;
    }

    Serial.println("\n══════════════════════════════════════════");
    Serial.printf("[LOOT] Wardriving dump: %s\n", fullPath);
    Serial.println("══════════════════════════════════════════");

    char buf[256];
    while (f.available()) {
        int i = 0;
        while (f.available() && i < (int)sizeof(buf) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c == '\r') continue;
            buf[i++] = c;
        }
        buf[i] = '\0';
        Serial.println(buf);
    }

    Serial.println("══════════════════════════════════════════\n");
    f.close();
}

static bool deleteWDFile(int idx) {
    if (idx < 0 || idx >= wdFileCount) return false;

    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", LM_WD_DIR, wdFiles[idx].name);

    spiDeselect();
    if (!SD.remove(fullPath)) return false;

    // Shift array
    for (int i = idx; i < wdFileCount - 1; i++) {
        wdFiles[i] = wdFiles[i + 1];
    }
    wdFileCount--;

    // Adjust selected
    if (wdSelectedIndex >= wdFileCount) wdSelectedIndex = wdFileCount - 1;

    return true;
}

static void drawWDDeleteConfirm() {
    if (wdSelectedIndex < 0 || wdSelectedIndex >= wdFileCount) return;

    tft.fillRect(20, 120, SCREEN_WIDTH - 40, 80, TFT_BLACK);
    tft.drawRoundRect(20, 120, SCREEN_WIDTH - 40, 80, 4, HALEHOUND_HOTPINK);
    tft.drawRoundRect(21, 121, SCREEN_WIDTH - 42, 78, 4, HALEHOUND_HOTPINK);

    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("DELETE THIS FILE?", SCREEN_WIDTH / 2, 130);

    char nameBuf[28];
    strncpy(nameBuf, wdFiles[wdSelectedIndex].name, 27);
    nameBuf[27] = '\0';
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(nameBuf, SCREEN_WIDTH / 2, 146);

    // YES button
    tft.drawRoundRect(35, 168, 70, 24, 4, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("YES", 70, 172);

    // NO button
    tft.drawRoundRect(135, 168, 70, 24, 4, HALEHOUND_MAGENTA);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.drawString("NO", 170, 172);

    tft.setTextDatum(TL_DATUM);
}

static void handleWDDetailTouch(uint16_t tx, uint16_t ty) {
    int btnW = (SCREEN_WIDTH - 20) / 2;
    int btn1X = 8;
    int btn2X = btn1X + btnW + 4;
    int btnY = 196;

    // SERIAL button
    if (tx >= btn1X && tx <= btn1X + btnW && ty >= btnY && ty <= btnY + 32) {
        serialDumpWDFile();

        // Flash "SENT!" feedback
        tft.fillRoundRect(btn1X + 1, btnY + 1, btnW - 2, 30, 4, HALEHOUND_VIOLET);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_BLACK);
        tft.drawString("SENT!", btn1X + btnW / 2, btnY + 10);
        tft.setTextDatum(TL_DATUM);
        delay(800);

        // Redraw normal button
        tft.fillRoundRect(btn1X + 1, btnY + 1, btnW - 2, 30, 4, TFT_BLACK);
        tft.drawRoundRect(btn1X, btnY, btnW, 32, 4, HALEHOUND_VIOLET);
        tft.drawRoundRect(btn1X + 1, btnY + 1, btnW - 2, 30, 4, HALEHOUND_VIOLET);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.drawString("SERIAL", btn1X + btnW / 2, btnY + 10);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    // DELETE button
    if (tx >= btn2X && tx <= btn2X + btnW && ty >= btnY && ty <= btnY + 32) {
        phase = PHASE_WD_CONFIRM_DEL;
        drawWDDeleteConfirm();
        return;
    }
}

static void handleWDConfirmTouch(uint16_t tx, uint16_t ty) {
    // YES button: x=35-105, y=168-192
    if (tx >= 35 && tx <= 105 && ty >= 168 && ty <= 192) {
        if (deleteWDFile(wdSelectedIndex)) {
            phase = PHASE_WD_LIST;
            if (wdFileCount == 0) {
                drawWDEmpty();
            } else {
                drawWDListHeader();
                drawWDFileList();
            }
        } else {
            tft.fillRect(20, 120, SCREEN_WIDTH - 40, 80, TFT_BLACK);
            tft.drawRoundRect(20, 120, SCREEN_WIDTH - 40, 80, 4, HALEHOUND_HOTPINK);
            tft.setTextDatum(TC_DATUM);
            tft.setTextColor(HALEHOUND_HOTPINK);
            tft.drawString("DELETE FAILED", SCREEN_WIDTH / 2, 150);
            tft.setTextDatum(TL_DATUM);
            delay(1000);
            phase = PHASE_WD_DETAIL;
            drawWDDetail();
        }
        return;
    }

    // NO button: x=135-205, y=168-192
    if (tx >= 135 && tx <= 205 && ty >= 168 && ty <= 192) {
        phase = PHASE_WD_DETAIL;
        drawWDDetail();
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TEXT VIEWER — scrollable, color-coded, shared by IoT Recon & Creds
// ═══════════════════════════════════════════════════════════════════════════

static uint16_t colorForLine(const char* line) {
    if (strstr(line, "[CRACKED]"))  return HALEHOUND_GREEN;
    if (strstr(line, "[OPEN]"))     return HALEHOUND_HOTPINK;
    if (strstr(line, "[LOCKED]"))   return HALEHOUND_GUNMETAL;
    if (strstr(line, "[PSK]"))      return HALEHOUND_HOTPINK;
    if (strstr(line, "rtsp://"))    return HALEHOUND_VIOLET;
    if (strstr(line, "http://"))    return HALEHOUND_VIOLET;
    if (strstr(line, "https://"))   return HALEHOUND_VIOLET;
    if (strstr(line, "mqtt://"))    return HALEHOUND_VIOLET;
    if (strstr(line, "telnet"))     return HALEHOUND_VIOLET;
    if (strstr(line, "Creds:"))     return HALEHOUND_GREEN;
    if (strstr(line, "==="))        return HALEHOUND_MAGENTA;
    if (line[0] == '|')            return HALEHOUND_MAGENTA;
    return HALEHOUND_CYAN;
}

static void loadTextFile(const char* path) {
    tvLineCount = 0;
    tvScrollOffset = 0;

    if (!tvLines) tvLines = (char (*)[LM_TV_LINE_LEN])malloc(LM_TV_MAX_LINES * LM_TV_LINE_LEN);
    if (!tvColors) tvColors = (uint16_t*)malloc(LM_TV_MAX_LINES * sizeof(uint16_t));
    if (!tvLines || !tvColors) return;

    spiDeselect();
    File f = SD.open(path, FILE_READ);
    if (!f) return;

    char lineBuf[128];
    while (f.available() && tvLineCount < LM_TV_MAX_LINES) {
        int i = 0;
        while (f.available() && i < (int)sizeof(lineBuf) - 1) {
            char c = f.read();
            if (c == '\n') break;
            if (c == '\r') continue;
            lineBuf[i++] = c;
        }
        lineBuf[i] = '\0';

        // Truncate to display width
        strncpy(tvLines[tvLineCount], lineBuf, LM_TV_LINE_LEN - 1);
        tvLines[tvLineCount][LM_TV_LINE_LEN - 1] = '\0';
        tvColors[tvLineCount] = colorForLine(lineBuf);
        tvLineCount++;
    }

    f.close();
}

static void drawTextView() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawLMIconBar();
    drawGlitchText(55, tvTitle ? tvTitle : "VIEW", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextDatum(TL_DATUM);

    int visibleLines = LM_TV_VISIBLE;
    for (int i = 0; i < visibleLines; i++) {
        int lineIdx = tvScrollOffset + i;
        int y = LM_TV_Y_START + i * LM_TV_LINE_H;

        if (lineIdx < tvLineCount) {
            tft.setTextColor(tvColors[lineIdx]);
            tft.drawString(tvLines[lineIdx], 4, y);
        }
    }

    // Scroll indicators
    if (tvScrollOffset > 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("^", SCREEN_WIDTH - 10, LM_TV_Y_START);
    }
    if (tvScrollOffset + visibleLines < tvLineCount) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("v", SCREEN_WIDTH - 10, SCREEN_HEIGHT - 38);
    }

    // Bottom bar
    char countBuf[24];
    snprintf(countBuf, sizeof(countBuf), "%d/%d lines", tvScrollOffset + 1, tvLineCount);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(countBuf, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 14);
    tft.setTextDatum(TL_DATUM);
}

static void handleTextViewTouch(uint16_t tx, uint16_t ty) {
    int visibleLines = LM_TV_VISIBLE;

    // Top 30% = scroll up
    if (ty >= LM_TV_Y_START && ty < LM_TV_Y_START + (SCREEN_HEIGHT - LM_TV_Y_START) * 3 / 10) {
        if (tvScrollOffset > 0) {
            tvScrollOffset -= 3;
            if (tvScrollOffset < 0) tvScrollOffset = 0;
            drawTextView();
        }
        return;
    }

    // Bottom 30% = scroll down
    if (ty > SCREEN_HEIGHT - (SCREEN_HEIGHT - LM_TV_Y_START) * 3 / 10) {
        if (tvScrollOffset + visibleLines < tvLineCount) {
            tvScrollOffset += 3;
            int maxScroll = tvLineCount - visibleLines;
            if (maxScroll < 0) maxScroll = 0;
            if (tvScrollOffset > maxScroll) tvScrollOffset = maxScroll;
            drawTextView();
        }
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// FREE HEAP ALLOCATIONS
// ═══════════════════════════════════════════════════════════════════════════

static void freeWDFiles() {
    if (wdFiles) { free(wdFiles); wdFiles = nullptr; }
    wdFileCount = 0;
    wdSelectedIndex = -1;
    wdScrollOffset = 0;
}

static void freeTextViewer() {
    if (tvLines) { free(tvLines); tvLines = nullptr; }
    if (tvColors) { free(tvColors); tvColors = nullptr; }
    tvLineCount = 0;
    tvScrollOffset = 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC INTERFACE
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    exitRequested = false;
    phase = PHASE_HUB;
    wdFileCount = 0;
    wdScrollOffset = 0;
    wdSelectedIndex = -1;
    tvLineCount = 0;
    tvScrollOffset = 0;

    // Mount SD
    sdMounted = mountSD();

    if (!sdMounted) {
        tft.fillScreen(TFT_BLACK);
        drawStatusBar();
        drawLMIconBar();
        drawGlitchText(55, "LOOT", &Nosifer_Regular10pt7b);
        tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);
        drawGlitchStatus(120, "NO SD", HALEHOUND_HOTPINK);
        drawCenteredText(160, "Insert SD card and retry", HALEHOUND_GUNMETAL, 1);
        return;
    }

    scanHubInfo();
    drawHub();
}

void loop() {
    // Debounce
    static unsigned long lastTap = 0;
    if (millis() - lastTap < 200) {
        delay(20);
        return;
    }

    // Handle delegated viewers (EAPOL, WP Loot) — they run their own loops
    // These are entered from the main .ino loop, not here.

    // Back button — context-dependent
    if (isLMBackTapped()) {
        lastTap = millis();
        switch (phase) {
            case PHASE_HUB:
                exitRequested = true;
                return;
            case PHASE_WD_LIST:
                freeWDFiles();
                phase = PHASE_HUB;
                scanHubInfo();
                drawHub();
                return;
            case PHASE_WD_DETAIL:
                phase = PHASE_WD_LIST;
                drawWDListHeader();
                drawWDFileList();
                return;
            case PHASE_WD_CONFIRM_DEL:
                phase = PHASE_WD_DETAIL;
                drawWDDetail();
                return;
            case PHASE_TEXT_VIEW:
                freeTextViewer();
                phase = PHASE_HUB;
                scanHubInfo();
                drawHub();
                return;
            default:
                exitRequested = true;
                return;
        }
    }

    // Touch input
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        lastTap = millis();

        switch (phase) {
            case PHASE_HUB:
                handleHubTouch(tx, ty);
                // After hub touch, enter the selected phase
                if (phase == PHASE_WD_LIST) {
                    enterWDList();
                } else if (phase == PHASE_EAPOL) {
                    // Will be handled by caller in .ino — set exit so we can delegate
                    // Actually, we handle delegation inline here
                    SavedCaptures::setup();
                    while (true) {
                        SavedCaptures::loop();
                        if (SavedCaptures::isExitRequested()) break;
                        if (IS_BOOT_PRESSED()) break;
                    }
                    SavedCaptures::cleanup();
                    phase = PHASE_HUB;
                    scanHubInfo();
                    drawHub();
                } else if (phase == PHASE_WPLOOT) {
                    WPLootViewer::setup();
                    while (true) {
                        WPLootViewer::loop();
                        if (WPLootViewer::isExitRequested()) break;
                        if (IS_BOOT_PRESSED()) break;
                    }
                    WPLootViewer::cleanup();
                    phase = PHASE_HUB;
                    scanHubInfo();
                    drawHub();
                } else if (phase == PHASE_TEXT_VIEW) {
                    const char* filePath = nullptr;
                    if (tvTitle && strcmp(tvTitle, "IOT RECON") == 0) {
                        filePath = LM_IOT_FILE;
                    } else {
                        filePath = LM_CREDS_FILE;
                    }
                    loadTextFile(filePath);
                    if (tvLineCount == 0) {
                        // No content — show empty message and return to hub
                        tft.fillScreen(TFT_BLACK);
                        drawStatusBar();
                        drawLMIconBar();
                        drawGlitchText(55, tvTitle ? tvTitle : "VIEW", &Nosifer_Regular10pt7b);
                        tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);
                        drawGlitchStatus(120, "NO DATA", HALEHOUND_HOTPINK);
                        drawCenteredText(160, "File is empty or missing", HALEHOUND_GUNMETAL, 1);
                        delay(1500);
                        freeTextViewer();
                        phase = PHASE_HUB;
                        scanHubInfo();
                        drawHub();
                    } else {
                        drawTextView();
                    }
                }
                break;

            case PHASE_WD_LIST:
                handleWDListTouch(tx, ty);
                if (phase == PHASE_WD_DETAIL) {
                    drawWDDetail();
                }
                break;

            case PHASE_WD_DETAIL:
                handleWDDetailTouch(tx, ty);
                break;

            case PHASE_WD_CONFIRM_DEL:
                handleWDConfirmTouch(tx, ty);
                break;

            case PHASE_TEXT_VIEW:
                handleTextViewTouch(tx, ty);
                break;

            default:
                break;
        }
    }

    delay(20);
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    // Do NOT call SD.end() — just deselect CS
    digitalWrite(SD_CS, HIGH);
    sdMounted = false;

    freeWDFiles();
    freeTextViewer();
}

}  // namespace LootManager
