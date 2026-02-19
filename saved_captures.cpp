// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Saved Captures Browser
// SD card file browser for /eapol/ directory (.hc22000, .pcap)
// Created: 2026-02-16
//
// Browse, inspect, and delete captured EAPOL handshake and PMKID files.
// NOTE: Does NOT call SD.end() — just deselects CS pin on cleanup.
//       SD.end() destabilizes shared SPI bus (CC1101, NRF24 share VSPI).
// ═══════════════════════════════════════════════════════════════════════════

#include "saved_captures.h"
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

namespace SavedCaptures {

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define SC_DIR              "/eapol"
#define SC_MAX_FILES        32
#define SC_LIST_Y           74       // Top of file list area (below title+separator)
#define SC_LIST_BOTTOM      292      // Bottom of file list area
#define SC_ROW_HEIGHT       24       // Height per file row
#define SC_VISIBLE_ROWS     ((SC_LIST_BOTTOM - SC_LIST_Y) / SC_ROW_HEIGHT)  // ~7 rows
#define SC_BLINK_MS         500

// ═══════════════════════════════════════════════════════════════════════════
// FILE ENTRY
// ═══════════════════════════════════════════════════════════════════════════

enum FileType {
    FT_HC22000,
    FT_PCAP,
    FT_UNKNOWN
};

struct FileEntry {
    char name[48];      // Filename only (no path)
    uint32_t size;      // File size in bytes
    FileType type;
};

// ═══════════════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════════════

enum Phase {
    PHASE_LIST,         // File list view
    PHASE_DETAIL,       // Single file detail view
    PHASE_VIEW,         // Parsed file content display
    PHASE_CONFIRM_DEL,  // Delete confirmation
    PHASE_EMPTY         // No files found
};

static Phase phase = PHASE_LIST;
static bool exitRequested = false;
static bool sdMounted = false;

// File list
static FileEntry files[SC_MAX_FILES];
static int fileCount = 0;
static int scrollOffset = 0;
static int selectedIndex = -1;

// Detail view
static char detailLine1[80];    // First .hc22000 hash line or PCAP info
static char detailLine2[80];    // Size / type info

// Parsed view content
#define SC_VIEW_LINES 12
static char viewLines[SC_VIEW_LINES][42];  // 40 chars per line max
static int viewLineCount = 0;

// UI state
static unsigned long lastBlink = 0;
static bool blinkState = false;

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static FileType classifyFile(const char* name) {
    int len = strlen(name);
    if (len > 8 && strcasecmp(name + len - 8, ".hc22000") == 0) return FT_HC22000;
    if (len > 5 && strcasecmp(name + len - 5, ".pcap") == 0) return FT_PCAP;
    return FT_UNKNOWN;
}

static const char* fileTypeStr(FileType t) {
    switch (t) {
        case FT_HC22000: return "HC22000";
        case FT_PCAP:    return "PCAP";
        default:         return "???";
    }
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

// ═══════════════════════════════════════════════════════════════════════════
// LOCAL ICON BAR & BACK BUTTON (module-scoped, not from .ino)
// ═══════════════════════════════════════════════════════════════════════════

static void drawSCIconBar() {
    tft.fillRect(0, 20, tft.width(), 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, tft.width(), 36, HALEHOUND_HOTPINK);
}

static bool isSCBackTapped() {
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
        if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 30) {
            delay(150);
            return true;
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// SD CARD & DIRECTORY SCANNING
// ═══════════════════════════════════════════════════════════════════════════

static bool mountSD() {
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (SD.begin(SD_CS)) return true;

    // Retry with explicit SPI
    SPI.begin(18, 19, 23, SD_CS);
    if (SD.begin(SD_CS, SPI, 4000000)) return true;

    return false;
}

static int scanDirectory() {
    fileCount = 0;

    if (!SD.exists(SC_DIR)) {
        return 0;
    }

    File dir = SD.open(SC_DIR);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return 0;
    }

    File entry;
    while ((entry = dir.openNextFile()) && fileCount < SC_MAX_FILES) {
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        const char* name = entry.name();
        FileType ft = classifyFile(name);

        if (ft == FT_UNKNOWN) {
            entry.close();
            continue;
        }

        // Strip leading path if present (some SD libs include full path)
        const char* basename = strrchr(name, '/');
        if (basename) {
            basename++;  // Skip the '/'
        } else {
            basename = name;
        }

        strncpy(files[fileCount].name, basename, sizeof(files[fileCount].name) - 1);
        files[fileCount].name[sizeof(files[fileCount].name) - 1] = '\0';
        files[fileCount].size = entry.size();
        files[fileCount].type = ft;
        fileCount++;

        entry.close();
    }

    dir.close();
    return fileCount;
}

// ═══════════════════════════════════════════════════════════════════════════
// FILE DETAIL LOADING
// ═══════════════════════════════════════════════════════════════════════════

static void loadFileDetail(int idx) {
    if (idx < 0 || idx >= fileCount) return;

    FileEntry& f = files[idx];
    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", SC_DIR, f.name);

    detailLine1[0] = '\0';
    detailLine2[0] = '\0';

    char sizeBuf[16];
    formatSize(f.size, sizeBuf, sizeof(sizeBuf));

    if (f.type == FT_HC22000) {
        // Read first line of .hc22000 file to show hash type
        File hf = SD.open(fullPath, FILE_READ);
        if (hf) {
            char line[128];
            int i = 0;
            while (hf.available() && i < (int)sizeof(line) - 1) {
                char c = hf.read();
                if (c == '\n' || c == '\r') break;
                line[i++] = c;
            }
            line[i] = '\0';
            hf.close();

            // Parse WPA*TT* — TT=01 is PMKID, TT=02 is handshake
            if (strncmp(line, "WPA*01*", 7) == 0) {
                snprintf(detailLine1, sizeof(detailLine1), "TYPE: PMKID");
            } else if (strncmp(line, "WPA*02*", 7) == 0) {
                snprintf(detailLine1, sizeof(detailLine1), "TYPE: 4-WAY HANDSHAKE");
            } else {
                snprintf(detailLine1, sizeof(detailLine1), "TYPE: UNKNOWN FORMAT");
            }
        } else {
            snprintf(detailLine1, sizeof(detailLine1), "ERROR: CANNOT READ");
        }
        snprintf(detailLine2, sizeof(detailLine2), "SIZE: %s  |  HASHCAT READY", sizeBuf);

    } else if (f.type == FT_PCAP) {
        // Read PCAP header to verify magic and count estimate
        File pf = SD.open(fullPath, FILE_READ);
        if (pf && pf.size() >= 24) {
            uint32_t magic = 0;
            pf.read((uint8_t*)&magic, 4);
            pf.close();

            if (magic == 0xa1b2c3d4 || magic == 0xd4c3b2a1) {
                // Estimate frame count: (fileSize - 24) / ~150 avg frame
                uint32_t dataBytes = f.size > 24 ? f.size - 24 : 0;
                uint32_t estFrames = dataBytes / 150;
                snprintf(detailLine1, sizeof(detailLine1), "VALID PCAP  |  ~%lu FRAMES", (unsigned long)estFrames);
            } else {
                snprintf(detailLine1, sizeof(detailLine1), "INVALID PCAP MAGIC");
            }
        } else {
            if (pf) pf.close();
            snprintf(detailLine1, sizeof(detailLine1), "ERROR: TOO SMALL / UNREADABLE");
        }
        snprintf(detailLine2, sizeof(detailLine2), "SIZE: %s  |  RAW CAPTURE", sizeBuf);
    }
}

static bool deleteFile(int idx) {
    if (idx < 0 || idx >= fileCount) return false;

    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", SC_DIR, files[idx].name);

    if (!SD.remove(fullPath)) return false;

    // Shift remaining files down
    for (int i = idx; i < fileCount - 1; i++) {
        files[i] = files[i + 1];
    }
    fileCount--;

    // Fix selection
    if (selectedIndex >= fileCount) selectedIndex = fileCount - 1;
    if (scrollOffset > 0 && scrollOffset + SC_VISIBLE_ROWS > fileCount) {
        scrollOffset = fileCount - SC_VISIBLE_ROWS;
        if (scrollOffset < 0) scrollOffset = 0;
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// PARSE FILE FOR VIEW SCREEN
// ═══════════════════════════════════════════════════════════════════════════

// Hex pair to byte
static uint8_t hexPairToByte(char hi, char lo) {
    uint8_t val = 0;
    if (hi >= '0' && hi <= '9') val = (hi - '0') << 4;
    else if (hi >= 'a' && hi <= 'f') val = (hi - 'a' + 10) << 4;
    else if (hi >= 'A' && hi <= 'F') val = (hi - 'A' + 10) << 4;
    if (lo >= '0' && lo <= '9') val |= (lo - '0');
    else if (lo >= 'a' && lo <= 'f') val |= (lo - 'a' + 10);
    else if (lo >= 'A' && lo <= 'F') val |= (lo - 'A' + 10);
    return val;
}

// Format 12-char hex string as XX:XX:XX:XX:XX:XX
static void hexToMAC(const char* hex, char* out) {
    if (strlen(hex) < 12) { strcpy(out, "??:??:??:??:??:??"); return; }
    snprintf(out, 18, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
             hex[0], hex[1], hex[2], hex[3], hex[4], hex[5],
             hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
}

// Decode hex-encoded SSID to ASCII
static void hexToSSID(const char* hex, char* out, int outLen) {
    int hexLen = strlen(hex);
    int ssidLen = hexLen / 2;
    if (ssidLen >= outLen) ssidLen = outLen - 1;
    for (int i = 0; i < ssidLen; i++) {
        out[i] = (char)hexPairToByte(hex[i * 2], hex[i * 2 + 1]);
    }
    out[ssidLen] = '\0';
}

static void parseHC22000ForView() {
    viewLineCount = 0;
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;

    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", SC_DIR, files[selectedIndex].name);

    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
        snprintf(viewLines[viewLineCount++], 42, "ERROR: CANNOT OPEN FILE");
        return;
    }

    // Read first line (hash can be very long)
    char line[600];
    int i = 0;
    while (f.available() && i < (int)sizeof(line) - 1) {
        char c = f.read();
        if (c == '\n' || c == '\r') break;
        line[i++] = c;
    }
    line[i] = '\0';
    f.close();

    // Manual * tokenizer (strtok skips empty fields)
    char* fields[10];
    int fieldCount = 0;
    fields[0] = line;
    for (int j = 0; line[j] && fieldCount < 9; j++) {
        if (line[j] == '*') {
            line[j] = '\0';
            fields[++fieldCount] = line + j + 1;
        }
    }
    fieldCount++;

    if (fieldCount < 6) {
        snprintf(viewLines[viewLineCount++], 42, "INVALID HASH FORMAT");
        return;
    }

    // fields: [0]=WPA [1]=type [2]=PMKID/MIC [3]=APMAC [4]=STAMAC [5]=SSIDHEX
    if (strcmp(fields[1], "01") == 0) {
        snprintf(viewLines[viewLineCount++], 42, "TYPE: PMKID");
    } else if (strcmp(fields[1], "02") == 0) {
        snprintf(viewLines[viewLineCount++], 42, "TYPE: 4-WAY HANDSHAKE");
    } else {
        snprintf(viewLines[viewLineCount++], 42, "TYPE: UNKNOWN (%s)", fields[1]);
    }

    // Blank separator
    viewLines[viewLineCount][0] = '\0';
    viewLineCount++;

    // PMKID or MIC (32 hex chars — show in 2 rows of 16)
    const char* label = (strcmp(fields[1], "01") == 0) ? "PMKID:" : "MIC:";
    snprintf(viewLines[viewLineCount++], 42, "%s", label);
    int hashLen = strlen(fields[2]);
    if (hashLen > 0) {
        // First 32 chars
        char chunk[33];
        strncpy(chunk, fields[2], 32);
        chunk[32] = '\0';
        snprintf(viewLines[viewLineCount++], 42, " %s", chunk);
    }

    // AP MAC
    if (strlen(fields[3]) >= 12) {
        char mac[18];
        hexToMAC(fields[3], mac);
        snprintf(viewLines[viewLineCount++], 42, "AP:  %s", mac);
    }

    // STA MAC
    if (strlen(fields[4]) >= 12) {
        char mac[18];
        hexToMAC(fields[4], mac);
        snprintf(viewLines[viewLineCount++], 42, "STA: %s", mac);
    }

    // SSID (decoded from hex)
    if (strlen(fields[5]) > 0) {
        char ssid[33];
        hexToSSID(fields[5], ssid, sizeof(ssid));
        snprintf(viewLines[viewLineCount++], 42, "SSID: %s", ssid);
    }

    // Blank + hashcat command
    viewLines[viewLineCount][0] = '\0';
    viewLineCount++;
    snprintf(viewLines[viewLineCount++], 42, "hashcat -m 22000 <file>");
}

static void parsePCAPForView() {
    viewLineCount = 0;
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;

    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", SC_DIR, files[selectedIndex].name);

    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
        snprintf(viewLines[viewLineCount++], 42, "ERROR: CANNOT OPEN FILE");
        return;
    }

    if (f.size() < 24) {
        snprintf(viewLines[viewLineCount++], 42, "FILE TOO SMALL");
        f.close();
        return;
    }

    // Read global header
    uint32_t magic;
    f.read((uint8_t*)&magic, 4);
    bool valid = (magic == 0xa1b2c3d4 || magic == 0xd4c3b2a1);
    snprintf(viewLines[viewLineCount++], 42, "MAGIC: %s", valid ? "0xA1B2C3D4 OK" : "INVALID");

    if (!valid) { f.close(); return; }

    // Skip rest of global header
    f.seek(24);

    char sizeBuf[16];
    formatSize(files[selectedIndex].size, sizeBuf, sizeof(sizeBuf));
    snprintf(viewLines[viewLineCount++], 42, "SIZE: %s", sizeBuf);

    // Blank separator
    viewLines[viewLineCount][0] = '\0';
    viewLineCount++;

    snprintf(viewLines[viewLineCount++], 42, "FRAMES:");

    // Walk packets
    int frameNum = 0;
    while (f.available() >= 16 && viewLineCount < SC_VIEW_LINES) {
        uint32_t tsSec, tsUsec, inclLen, origLen;
        f.read((uint8_t*)&tsSec, 4);
        f.read((uint8_t*)&tsUsec, 4);
        f.read((uint8_t*)&inclLen, 4);
        f.read((uint8_t*)&origLen, 4);

        if (inclLen > 2500 || (uint32_t)f.available() < inclLen) break;

        // Read first 2 bytes for frame control
        const char* frameType = "UNKNOWN";
        if (inclLen >= 2) {
            uint8_t fc[2];
            f.read(fc, 2);

            uint8_t type = (fc[0] >> 2) & 0x03;
            uint8_t subtype = (fc[0] >> 4) & 0x0F;

            if (type == 0 && subtype == 0)       frameType = "ASSOC REQ";
            else if (type == 0 && subtype == 1)  frameType = "ASSOC RSP";
            else if (type == 0 && subtype == 4)  frameType = "PROBE REQ";
            else if (type == 0 && subtype == 5)  frameType = "PROBE RSP";
            else if (type == 0 && subtype == 8)  frameType = "BEACON";
            else if (type == 0 && subtype == 11) frameType = "AUTH";
            else if (type == 0 && subtype == 12) frameType = "DEAUTH";
            else if (type == 2)                  frameType = "DATA/EAPOL";

            f.seek(f.position() + inclLen - 2);
        } else {
            f.seek(f.position() + inclLen);
        }

        snprintf(viewLines[viewLineCount++], 42, " %d: %s (%luB)",
                 frameNum + 1, frameType, (unsigned long)origLen);
        frameNum++;
    }

    f.close();
}

// ═══════════════════════════════════════════════════════════════════════════
// SERIAL DUMP
// ═══════════════════════════════════════════════════════════════════════════

static void dumpToSerial(int idx) {
    if (idx < 0 || idx >= fileCount) return;

    FileEntry& fe = files[idx];
    char fullPath[80];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", SC_DIR, fe.name);

    Serial.println();
    Serial.println("════════════════════════════════════════════");
    Serial.printf("  HALEHOUND CAPTURE DUMP: %s\n", fe.name);
    Serial.println("════════════════════════════════════════════");

    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
        Serial.println("ERROR: Cannot open file");
        return;
    }

    if (fe.type == FT_HC22000) {
        // Dump raw hash lines — ready for hashcat
        Serial.println("FORMAT: hashcat .hc22000 (mode 22000)");
        Serial.println("────────────────────────────────────────────");
        Serial.println();

        int lineNum = 0;
        while (f.available()) {
            char line[600];
            int i = 0;
            while (f.available() && i < (int)sizeof(line) - 1) {
                char c = f.read();
                if (c == '\n' || c == '\r') {
                    if (i > 0) break;  // End of line
                    continue;  // Skip empty lines
                }
                line[i++] = c;
            }
            if (i == 0) continue;
            line[i] = '\0';
            lineNum++;

            Serial.println(line);
        }

        Serial.println();
        Serial.println("────────────────────────────────────────────");
        Serial.printf("HASHES: %d line(s)\n", lineNum);
        Serial.println("CRACK:  hashcat -m 22000 <file> <wordlist>");
        Serial.println("        hashcat -m 22000 -r best64.rule <file> <wordlist>");

    } else if (fe.type == FT_PCAP) {
        // PCAP frame summary
        Serial.println("FORMAT: PCAP (IEEE 802.11)");
        Serial.println("────────────────────────────────────────────");
        Serial.printf("SIZE: %lu bytes\n", (unsigned long)fe.size);

        if (f.size() >= 24) {
            uint32_t magic;
            f.read((uint8_t*)&magic, 4);
            Serial.printf("MAGIC: 0x%08X %s\n", magic,
                          (magic == 0xa1b2c3d4 || magic == 0xd4c3b2a1) ? "VALID" : "INVALID");
            f.seek(24);

            int frameNum = 0;
            while (f.available() >= 16) {
                uint32_t tsSec, tsUsec, inclLen, origLen;
                f.read((uint8_t*)&tsSec, 4);
                f.read((uint8_t*)&tsUsec, 4);
                f.read((uint8_t*)&inclLen, 4);
                f.read((uint8_t*)&origLen, 4);

                if (inclLen > 2500 || (uint32_t)f.available() < inclLen) break;

                Serial.printf("FRAME %d: %lu bytes @ %lu.%06lu\n",
                              frameNum + 1, (unsigned long)origLen,
                              (unsigned long)tsSec, (unsigned long)tsUsec);

                f.seek(f.position() + inclLen);
                frameNum++;
            }
            Serial.printf("TOTAL: %d frames\n", frameNum);
        }
        Serial.println("────────────────────────────────────────────");
        Serial.println("OPEN WITH: wireshark <file>.pcap");
        Serial.println("NOTE: Transfer SD card to computer for full PCAP");
    }

    f.close();

    Serial.println("════════════════════════════════════════════");
    Serial.println();
}

// ═══════════════════════════════════════════════════════════════════════════
// DRAWING — LIST VIEW
// ═══════════════════════════════════════════════════════════════════════════

static void drawListHeader() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawSCIconBar();
    drawGlitchText(55, "CAPTURES", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    // Column headers
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("FILE", 6, SC_LIST_Y - 12);
    tft.drawString("TYPE", 160, SC_LIST_Y - 12);
    tft.drawString("SIZE", 205, SC_LIST_Y - 12);

    // Separator line
    tft.drawFastHLine(4, SC_LIST_Y - 2, 232, HALEHOUND_VIOLET);
}

static void drawFileRow(int screenRow, int fileIdx, bool selected) {
    int y = SC_LIST_Y + screenRow * SC_ROW_HEIGHT;

    // Clear row
    tft.fillRect(2, y, 236, SC_ROW_HEIGHT - 1, selected ? HALEHOUND_VIOLET : TFT_BLACK);

    if (fileIdx < 0 || fileIdx >= fileCount) return;

    FileEntry& f = files[fileIdx];

    // Filename (truncate to fit)
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);
    tft.setTextColor(selected ? TFT_WHITE : HALEHOUND_MAGENTA);

    // Truncate filename for display
    char displayName[22];
    strncpy(displayName, f.name, 21);
    displayName[21] = '\0';
    // Strip extension for cleaner display
    char* dot = strrchr(displayName, '.');
    if (dot && (strcasecmp(dot, ".hc22000") == 0 || strcasecmp(dot, ".pcap") == 0)) {
        *dot = '\0';
    }
    tft.drawString(displayName, 6, y + 4);

    // Type badge
    uint16_t badgeColor = (f.type == FT_HC22000) ? HALEHOUND_HOTPINK : HALEHOUND_VIOLET;
    tft.setTextColor(badgeColor);
    tft.drawString(fileTypeStr(f.type), 160, y + 4);

    // Size
    char sizeBuf[12];
    formatSize(f.size, sizeBuf, sizeof(sizeBuf));
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(sizeBuf, 205, y + 4);
}

static void drawFileList() {
    int visible = SC_VISIBLE_ROWS;
    if (visible > fileCount) visible = fileCount;

    for (int i = 0; i < SC_VISIBLE_ROWS; i++) {
        int fileIdx = scrollOffset + i;
        if (fileIdx < fileCount) {
            drawFileRow(i, fileIdx, fileIdx == selectedIndex);
        } else {
            // Clear empty row
            int y = SC_LIST_Y + i * SC_ROW_HEIGHT;
            tft.fillRect(2, y, 236, SC_ROW_HEIGHT - 1, TFT_BLACK);
        }
    }

    // Scroll indicators
    if (scrollOffset > 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("^", 228, SC_LIST_Y);
    }
    if (scrollOffset + SC_VISIBLE_ROWS < fileCount) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("v", 228, SC_LIST_BOTTOM - 12);
    }

    // File count at bottom
    tft.fillRect(0, SC_LIST_BOTTOM + 2, tft.width(), 16, TFT_BLACK);
    char countBuf[32];
    snprintf(countBuf, sizeof(countBuf), "%d FILE%s", fileCount, fileCount == 1 ? "" : "S");
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(countBuf, 120, SC_LIST_BOTTOM + 4);
    tft.setTextDatum(TL_DATUM);

    // Bottom bar: TAP TO VIEW
    tft.fillRect(0, tft.height() - 20, tft.width(), 20, TFT_BLACK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("TAP FILE TO VIEW  |  BACK TO EXIT", 120, 304);
    tft.setTextDatum(TL_DATUM);
}

static void drawEmptyScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawSCIconBar();
    drawGlitchText(55, "CAPTURES", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    drawGlitchStatus(120, "NO FILES", HALEHOUND_HOTPINK);
    drawCenteredText(160, "No captures found in /eapol/", HALEHOUND_GUNMETAL, 1);
    drawCenteredText(180, "Run EAPOL Capture first", HALEHOUND_GUNMETAL, 1);

    // SD card status
    if (!sdMounted) {
        drawCenteredText(210, "SD CARD NOT DETECTED", HALEHOUND_HOTPINK, 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DRAWING — DETAIL VIEW
// ═══════════════════════════════════════════════════════════════════════════

static void drawDetailView() {
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;

    FileEntry& f = files[selectedIndex];

    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawSCIconBar();

    // File name as title — 10pt Nosifer, strip extension
    char titleBuf[20];
    strncpy(titleBuf, f.name, 19);
    titleBuf[19] = '\0';
    // Strip known extensions for cleaner title
    char* dot = strrchr(titleBuf, '.');
    if (dot && (strcasecmp(dot, ".hc22000") == 0 || strcasecmp(dot, ".pcap") == 0)) {
        *dot = '\0';
    }
    drawGlitchText(55, titleBuf, &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, tft.width(), 58, HALEHOUND_HOTPINK);

    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    // Frame box
    tft.drawRoundRect(10, 64, 220, 100, 4, HALEHOUND_VIOLET);
    tft.drawRoundRect(11, 65, 218, 98, 4, HALEHOUND_VIOLET);

    // Detail line 1
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.drawString(detailLine1, 120, 75);

    // Detail line 2
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(detailLine2, 120, 95);

    // Full path
    char pathBuf[64];
    snprintf(pathBuf, sizeof(pathBuf), "%s/%s", SC_DIR, f.name);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString(pathBuf, 120, 118);

    // Type-specific info
    if (f.type == FT_HC22000) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.drawString("HASHCAT -m 22000", 120, 140);
    } else {
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.drawString("WIRESHARK / TSHARK", 120, 140);
    }

    tft.setTextDatum(TL_DATUM);

    // Three action buttons: VIEW | SERIAL | DELETE
    // Row at y=180
    // VIEW button
    tft.drawRoundRect(8, 180, 70, 32, 4, HALEHOUND_MAGENTA);
    tft.drawRoundRect(9, 181, 68, 30, 4, HALEHOUND_MAGENTA);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.drawString("VIEW", 43, 190);

    // SERIAL button
    tft.drawRoundRect(85, 180, 70, 32, 4, HALEHOUND_VIOLET);
    tft.drawRoundRect(86, 181, 68, 30, 4, HALEHOUND_VIOLET);
    tft.setTextColor(HALEHOUND_VIOLET);
    tft.drawString("SERIAL", 120, 190);

    // DELETE button
    tft.drawRoundRect(162, 180, 70, 32, 4, HALEHOUND_HOTPINK);
    tft.drawRoundRect(163, 181, 68, 30, 4, HALEHOUND_HOTPINK);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.drawString("DELETE", 197, 190);

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
    tft.fillRect(20, 120, 200, 80, TFT_BLACK);
    tft.drawRoundRect(20, 120, 200, 80, 4, HALEHOUND_HOTPINK);
    tft.drawRoundRect(21, 121, 198, 78, 4, HALEHOUND_HOTPINK);

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
// DRAWING — VIEW SCREEN (parsed file content)
// ═══════════════════════════════════════════════════════════════════════════

static void drawViewScreen() {
    if (selectedIndex < 0 || selectedIndex >= fileCount) return;

    tft.fillScreen(TFT_BLACK);
    drawStatusBar();
    drawSCIconBar();

    // Title
    drawGlitchTitle(75, "HASH DETAIL");

    // Frame box
    tft.drawRoundRect(5, 82, 230, viewLineCount * 16 + 16, 4, HALEHOUND_VIOLET);

    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(NULL);
    tft.setTextSize(1);

    // Draw parsed lines
    for (int i = 0; i < viewLineCount && i < SC_VIEW_LINES; i++) {
        int y = 90 + (i * 16);

        if (viewLines[i][0] == '\0') continue;  // Skip blank separators

        // Color by content
        if (strncmp(viewLines[i], "TYPE:", 5) == 0) {
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else if (strncmp(viewLines[i], "PMKID:", 6) == 0 ||
                   strncmp(viewLines[i], "MIC:", 4) == 0) {
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else if (viewLines[i][0] == ' ' && (viewLines[i][1] >= '0' && viewLines[i][1] <= '9' ||
                   viewLines[i][1] >= 'a' && viewLines[i][1] <= 'f')) {
            tft.setTextColor(HALEHOUND_MAGENTA);  // Hex data
        } else if (strncmp(viewLines[i], "AP:", 3) == 0 ||
                   strncmp(viewLines[i], "STA:", 4) == 0) {
            tft.setTextColor(HALEHOUND_MAGENTA);
        } else if (strncmp(viewLines[i], "SSID:", 5) == 0) {
            tft.setTextColor(HALEHOUND_MAGENTA);
        } else if (strncmp(viewLines[i], "hashcat", 7) == 0 ||
                   strncmp(viewLines[i], "CRACK:", 6) == 0) {
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else if (strncmp(viewLines[i], "FRAMES:", 7) == 0 ||
                   strncmp(viewLines[i], "MAGIC:", 6) == 0 ||
                   strncmp(viewLines[i], "SIZE:", 5) == 0) {
            tft.setTextColor(HALEHOUND_HOTPINK);
        } else {
            tft.setTextColor(HALEHOUND_GUNMETAL);
        }

        tft.drawString(viewLines[i], 12, y);
    }

    // Bottom hint
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.drawString("BACK TO RETURN", 120, 304);
    tft.setTextDatum(TL_DATUM);
}

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH HANDLING
// ═══════════════════════════════════════════════════════════════════════════

static void handleListTouch(int tx, int ty) {
    // Check if touch is in the file list area
    if (ty >= SC_LIST_Y && ty < SC_LIST_BOTTOM && tx >= 2 && tx <= 238) {
        int row = (ty - SC_LIST_Y) / SC_ROW_HEIGHT;
        int fileIdx = scrollOffset + row;

        if (fileIdx >= 0 && fileIdx < fileCount) {
            if (fileIdx == selectedIndex) {
                // Double-tap: open detail view
                loadFileDetail(selectedIndex);
                phase = PHASE_DETAIL;
                drawDetailView();
            } else {
                // First tap: select
                int oldSel = selectedIndex;
                selectedIndex = fileIdx;

                // Redraw only changed rows
                if (oldSel >= scrollOffset && oldSel < scrollOffset + SC_VISIBLE_ROWS) {
                    drawFileRow(oldSel - scrollOffset, oldSel, false);
                }
                drawFileRow(fileIdx - scrollOffset, fileIdx, true);
            }
        }
    }

    // Scroll up zone (top 20px of list)
    if (ty >= SC_LIST_Y && ty < SC_LIST_Y + 20 && scrollOffset > 0) {
        scrollOffset--;
        drawFileList();
    }

    // Scroll down zone (bottom 20px of list)
    if (ty > SC_LIST_BOTTOM - 20 && ty <= SC_LIST_BOTTOM && scrollOffset + SC_VISIBLE_ROWS < fileCount) {
        scrollOffset++;
        drawFileList();
    }
}

static void handleDetailTouch(int tx, int ty) {
    // VIEW button: x=8-78, y=180-212
    if (tx >= 8 && tx <= 78 && ty >= 180 && ty <= 212) {
        // Parse file and show view screen
        if (files[selectedIndex].type == FT_HC22000) {
            parseHC22000ForView();
        } else {
            parsePCAPForView();
        }
        phase = PHASE_VIEW;
        drawViewScreen();
        return;
    }

    // SERIAL button: x=85-155, y=180-212
    if (tx >= 85 && tx <= 155 && ty >= 180 && ty <= 212) {
        // Flash button feedback
        tft.fillRoundRect(86, 181, 68, 30, 4, HALEHOUND_VIOLET);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_BLACK);
        tft.drawString("DUMPING", 120, 190);
        tft.setTextDatum(TL_DATUM);

        // Dump to serial
        dumpToSerial(selectedIndex);

        // Show confirmation
        tft.fillRoundRect(86, 181, 68, 30, 4, HALEHOUND_MAGENTA);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TFT_BLACK);
        tft.drawString("SENT!", 120, 190);
        tft.setTextDatum(TL_DATUM);
        delay(1000);

        // Redraw normal button
        tft.fillRoundRect(86, 181, 68, 30, 4, TFT_BLACK);
        tft.drawRoundRect(85, 180, 70, 32, 4, HALEHOUND_VIOLET);
        tft.drawRoundRect(86, 181, 68, 30, 4, HALEHOUND_VIOLET);
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(HALEHOUND_VIOLET);
        tft.drawString("SERIAL", 120, 190);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    // DELETE button: x=162-232, y=180-212
    if (tx >= 162 && tx <= 232 && ty >= 180 && ty <= 212) {
        phase = PHASE_CONFIRM_DEL;
        drawDeleteConfirm();
        return;
    }
}

static void handleConfirmTouch(int tx, int ty) {
    // YES button: x=35-105, y=168-192
    if (tx >= 35 && tx <= 105 && ty >= 168 && ty <= 192) {
        if (deleteFile(selectedIndex)) {
            // Success — return to list
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
            tft.fillRect(20, 120, 200, 80, TFT_BLACK);
            tft.drawRoundRect(20, 120, 200, 80, 4, HALEHOUND_HOTPINK);
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

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC INTERFACE
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    exitRequested = false;
    phase = PHASE_LIST;
    fileCount = 0;
    scrollOffset = 0;
    selectedIndex = -1;
    detailLine1[0] = '\0';
    detailLine2[0] = '\0';

    // Mount SD
    sdMounted = mountSD();

    if (!sdMounted) {
        phase = PHASE_EMPTY;
        drawEmptyScreen();
        return;
    }

    // Scan for capture files
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
    // Touch input
    touchButtonsUpdate();

    // Back button (icon bar touch or hardware buttons)
    if (isSCBackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (phase == PHASE_VIEW) {
            // View → back to detail
            phase = PHASE_DETAIL;
            drawDetailView();
            delay(200);
            return;
        }
        if (phase == PHASE_DETAIL || phase == PHASE_CONFIRM_DEL) {
            // Detail/confirm → back to list
            phase = PHASE_LIST;
            if (fileCount == 0) {
                phase = PHASE_EMPTY;
                drawEmptyScreen();
            } else {
                drawListHeader();
                drawFileList();
            }
            delay(200);
            return;
        }
        // In list or empty view — exit module
        exitRequested = true;
        return;
    }

    // Touch handling for file selection and actions
    uint16_t tx, ty;
    if (getTouchPoint(&tx, &ty)) {
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
            default:
                break;
        }

        // Debounce — brief delay after touch
        delay(200);
    }

    delay(20);
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    // Do NOT call SD.end() — it destabilizes the shared SPI bus
    // (CC1101 and NRF24 share VSPI with SD card)
    // Just deselect the SD CS pin to release the bus
    digitalWrite(SD_CS, HIGH);
    sdMounted = false;
    fileCount = 0;
    selectedIndex = -1;
    scrollOffset = 0;
}

}  // namespace SavedCaptures
