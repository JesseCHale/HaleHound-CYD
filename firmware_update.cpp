// =============================================================================
// HaleHound-CYD SD Card Firmware Update
// I flash new firmware.bin from my SD card — no laptop needed
// My pattern: same as my serial_monitor.cpp / gps_module.cpp
// Created: 2026-02-15
// =============================================================================

#include "firmware_update.h"
#include "shared.h"
#include "utils.h"
#include "touch_buttons.h"
#include "icon.h"
#include "cyd_config.h"
#include "spi_manager.h"
#include <SD.h>
#include <SPI.h>
#include <Update.h>

extern TFT_eSPI tft;

// =============================================================================
// CONSTANTS
// =============================================================================

#define MAX_BIN_FILES       16
#define FW_CHUNK_SIZE     4096
#define FW_LIST_VISIBLE      5
#define FW_ITEM_HEIGHT      30
#define FW_LIST_Y_START     85
#define FW_MIN_SIZE     102400    // 100 KB minimum
#define FW_MAX_SIZE    3145728    // 3 MB maximum (partition limit)
#define ESP32_IMAGE_MAGIC  0xE9
#define ICON_SIZE           16

// =============================================================================
// STATE
// =============================================================================

static char binFiles[MAX_BIN_FILES][64];
static uint32_t binSizes[MAX_BIN_FILES];
static int binFileCount = 0;
static int selectedFileIndex = -1;
static int scrollOffset = 0;
static bool fwSDReady = false;

// =============================================================================
// SD CARD INIT (my pattern from wardriving.cpp)
// =============================================================================

static bool initSDCard() {
    Serial.println("[FW_UPDATE] Initializing SD card...");

    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    if (!SD.begin(SD_CS)) {
        Serial.println("[FW_UPDATE] SD.begin(CS) failed, trying explicit SPI...");
        SPI.begin(18, 19, 23, SD_CS);
        if (!SD.begin(SD_CS, SPI, 4000000)) {
            Serial.println("[FW_UPDATE] SD card init failed!");
            return false;
        }
    }

    Serial.println("[FW_UPDATE] SD card initialized OK");
    return true;
}

// =============================================================================
// FILE SCANNING
// =============================================================================

static bool hasBinExtension(const char* name) {
    int len = strlen(name);
    if (len < 5) return false;
    // Case insensitive .bin check
    const char* ext = name + len - 4;
    return (ext[0] == '.' &&
            (ext[1] == 'b' || ext[1] == 'B') &&
            (ext[2] == 'i' || ext[2] == 'I') &&
            (ext[3] == 'n' || ext[3] == 'N'));
}

static void scanDirectory(const char* dirPath) {
    File dir = SD.open(dirPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File entry;
    while ((entry = dir.openNextFile()) && binFileCount < MAX_BIN_FILES) {
        if (!entry.isDirectory()) {
            const char* name = entry.name();
            if (hasBinExtension(name)) {
                uint32_t fsize = entry.size();
                // Build full path
                if (strcmp(dirPath, "/") == 0) {
                    snprintf(binFiles[binFileCount], 64, "/%s", name);
                } else {
                    snprintf(binFiles[binFileCount], 64, "%s/%s", dirPath, name);
                }
                binSizes[binFileCount] = fsize;
                Serial.printf("[FW_UPDATE] Found: %s (%u bytes)\n", binFiles[binFileCount], fsize);
                binFileCount++;
            }
        }
        entry.close();
    }
    dir.close();
}

static void scanBinFiles() {
    binFileCount = 0;
    scanDirectory("/");
    scanDirectory("/firmware");
}

// =============================================================================
// VALIDATION
// =============================================================================

static bool validateBinFile(const char* path, char* errMsg, size_t errLen) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        snprintf(errMsg, errLen, "Cannot open file");
        return false;
    }

    uint32_t fsize = f.size();
    if (fsize < FW_MIN_SIZE) {
        snprintf(errMsg, errLen, "Too small (%u KB)", (unsigned)(fsize / 1024));
        f.close();
        return false;
    }
    if (fsize > FW_MAX_SIZE) {
        snprintf(errMsg, errLen, "Too large (max 3MB)");
        f.close();
        return false;
    }

    uint8_t magic;
    f.read(&magic, 1);
    f.close();

    if (magic != ESP32_IMAGE_MAGIC) {
        snprintf(errMsg, errLen, "Bad magic: 0x%02X (need 0xE9)", magic);
        return false;
    }

    return true;
}

// =============================================================================
// FORMATTING
// =============================================================================

static void formatFileSize(uint32_t bytes, char* buf, size_t bufLen) {
    if (bytes >= 1048576) {
        snprintf(buf, bufLen, "%.2f MB", (float)bytes / 1048576.0f);
    } else if (bytes >= 1024) {
        snprintf(buf, bufLen, "%u KB", (unsigned)(bytes / 1024));
    } else {
        snprintf(buf, bufLen, "%u B", (unsigned)bytes);
    }
}

// Extract just the filename from a full path
static const char* getFileName(const char* path) {
    const char* last = strrchr(path, '/');
    return last ? last + 1 : path;
}

// =============================================================================
// FILE SELECTION SCREEN
// =============================================================================

static void drawFileList() {
    // Clear list area
    tft.fillRect(0, FW_LIST_Y_START, tft.width(), FW_LIST_VISIBLE * FW_ITEM_HEIGHT, TFT_BLACK);

    int visible = (binFileCount - scrollOffset);
    if (visible > FW_LIST_VISIBLE) visible = FW_LIST_VISIBLE;

    for (int i = 0; i < visible; i++) {
        int fileIdx = scrollOffset + i;
        int y = FW_LIST_Y_START + i * FW_ITEM_HEIGHT;

        bool isSelected = (fileIdx == selectedFileIndex);

        // Background
        if (isSelected) {
            tft.fillRoundRect(4, y, 232, FW_ITEM_HEIGHT - 2, 4, HALEHOUND_MAGENTA);
        } else {
            tft.drawRoundRect(4, y, 232, FW_ITEM_HEIGHT - 2, 4, HALEHOUND_GUNMETAL);
        }

        // File name
        tft.setTextSize(1);
        tft.setTextColor(isSelected ? TFT_BLACK : HALEHOUND_HOTPINK);
        tft.setCursor(10, y + 5);
        // Truncate filename to fit
        char nameBuf[28];
        strncpy(nameBuf, getFileName(binFiles[fileIdx]), 27);
        nameBuf[27] = '\0';
        tft.print(nameBuf);

        // File size
        char sizeBuf[16];
        formatFileSize(binSizes[fileIdx], sizeBuf, sizeof(sizeBuf));
        tft.setTextColor(isSelected ? TFT_BLACK : HALEHOUND_GUNMETAL);
        tft.setCursor(10, y + 17);
        tft.print(sizeBuf);
    }

    // Scroll arrows if needed
    if (scrollOffset > 0) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(116, FW_LIST_Y_START - 10);
        tft.setTextSize(1);
        tft.print("^");
    }
    if (scrollOffset + FW_LIST_VISIBLE < binFileCount) {
        tft.setTextColor(HALEHOUND_HOTPINK);
        tft.setCursor(116, FW_LIST_Y_START + FW_LIST_VISIBLE * FW_ITEM_HEIGHT + 2);
        tft.setTextSize(1);
        tft.print("v");
    }
}

static void drawFlashButton(bool enabled) {
    tft.fillRoundRect(50, 250, 140, 36, 8,
                      enabled ? TFT_GREEN : HALEHOUND_GUNMETAL);
    tft.setTextSize(2);
    tft.setTextColor(enabled ? TFT_BLACK : TFT_DARKGREY);
    tft.setCursor(76, 258);
    tft.print("FLASH");
}

static void drawFileSelectScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    // Icon bar - back only
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Glitch title
    drawGlitchTitle(48, "UPDATE FW");

    // SD status line
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_MAGENTA);
    char statusBuf[32];
    snprintf(statusBuf, sizeof(statusBuf), "SD: %d file%s found",
             binFileCount, binFileCount == 1 ? "" : "s");
    int sw = strlen(statusBuf) * 6;
    tft.setCursor((tft.width() - sw) / 2, 68);
    tft.print(statusBuf);

    // File list
    drawFileList();

    // Flash button (disabled initially)
    drawFlashButton(selectedFileIndex >= 0);

    // Hint text
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(52, 298);
    tft.print("Select a .bin file");
}

// Returns: 0=nothing, 1=FLASH, -1=back
static int handleFileSelectTouch() {
    uint16_t tx, ty;
    if (!getTouchPoint(&tx, &ty)) return 0;

    // Back icon (y=20-36, x=10-26)
    if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 26) {
        delay(150);
        return -1;
    }

    // File list taps
    if (ty >= FW_LIST_Y_START && ty < FW_LIST_Y_START + FW_LIST_VISIBLE * FW_ITEM_HEIGHT) {
        int tappedRow = (ty - FW_LIST_Y_START) / FW_ITEM_HEIGHT;
        int tappedIdx = scrollOffset + tappedRow;
        if (tappedIdx < binFileCount) {
            selectedFileIndex = tappedIdx;
            drawFileList();
            drawFlashButton(true);
        }
        delay(200);
        return 0;
    }

    // Scroll up arrow area (above list)
    if (ty >= FW_LIST_Y_START - 15 && ty < FW_LIST_Y_START && scrollOffset > 0) {
        scrollOffset--;
        drawFileList();
        delay(200);
        return 0;
    }

    // Scroll down arrow area (below list)
    if (ty >= FW_LIST_Y_START + FW_LIST_VISIBLE * FW_ITEM_HEIGHT &&
        ty < FW_LIST_Y_START + FW_LIST_VISIBLE * FW_ITEM_HEIGHT + 15 &&
        scrollOffset + FW_LIST_VISIBLE < binFileCount) {
        scrollOffset++;
        drawFileList();
        delay(200);
        return 0;
    }

    // FLASH button (y=250-286, x=50-190)
    if (ty >= 250 && ty <= 286 && tx >= 50 && tx <= 190 && selectedFileIndex >= 0) {
        delay(150);
        return 1;
    }

    return 0;
}

// =============================================================================
// CONFIRM SCREEN
// =============================================================================

static void drawConfirmScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    // Icon bar
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Glitch title
    drawGlitchTitle(48, "CONFIRM");

    // File name
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 85);
    tft.print("File:");
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 98);
    char nameBuf[36];
    strncpy(nameBuf, getFileName(binFiles[selectedFileIndex]), 35);
    nameBuf[35] = '\0';
    tft.print(nameBuf);

    // File size
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 115);
    tft.print("Size:");
    char sizeBuf[16];
    formatFileSize(binSizes[selectedFileIndex], sizeBuf, sizeof(sizeBuf));
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(42, 115);
    tft.print(sizeBuf);

    // Warning
    tft.setTextColor(TFT_RED);
    tft.setTextSize(1);
    tft.setCursor(40, 140);
    tft.print("This will overwrite");
    tft.setCursor(40, 155);
    tft.print("current firmware!");

    // CANCEL button (red)
    tft.fillRoundRect(15, 185, 95, 35, 6, TFT_RED);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(24, 193);
    tft.print("CANCEL");

    // FLASH button (green)
    tft.fillRoundRect(130, 185, 95, 35, 6, TFT_GREEN);
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK);
    tft.setCursor(142, 193);
    tft.print("FLASH");
}

// Returns: 0=nothing, 1=confirm, -1=cancel
static int handleConfirmTouch() {
    uint16_t tx, ty;
    if (!getTouchPoint(&tx, &ty)) return 0;

    // CANCEL button (y=185-220, x=15-110)
    if (ty >= 185 && ty <= 220 && tx >= 15 && tx <= 110) {
        delay(150);
        return -1;
    }

    // FLASH button (y=185-220, x=130-225)
    if (ty >= 185 && ty <= 220 && tx >= 130 && tx <= 225) {
        delay(150);
        return 1;
    }

    return 0;
}

// =============================================================================
// PROGRESS SCREEN
// =============================================================================

static void drawProgressScreen(const char* fileName) {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    // Icon bar (no back - can't cancel mid-flash)
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    // Glitch title
    drawGlitchTitle(48, "FLASHING");

    // File name
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 85);
    tft.print(fileName);

    // Progress bar outline
    tft.drawRect(19, 115, 202, 22, HALEHOUND_MAGENTA);

    // Percentage
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    tft.setCursor(100, 145);
    tft.print("0%");

    // Warning
    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(44, 200);
    tft.print("DO NOT POWER OFF!");
}

static void updateProgressBar(int pct) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    // Fill bar (200px max width)
    int fillW = (200 * pct) / 100;
    if (fillW > 0) {
        tft.fillRect(20, 116, fillW, 20, TFT_GREEN);
    }

    // Percentage text
    tft.fillRect(88, 145, 64, 16, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(HALEHOUND_MAGENTA, TFT_BLACK);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    int tw = strlen(buf) * 12;
    tft.setCursor((tft.width() - tw) / 2, 145);
    tft.print(buf);
}

// =============================================================================
// FLASH UPDATE
// =============================================================================

static bool performFlashUpdate(const char* filepath, char* errMsg, size_t errLen) {
    File file = SD.open(filepath, FILE_READ);
    if (!file) {
        snprintf(errMsg, errLen, "Cannot open file");
        return false;
    }

    size_t fileSize = file.size();
    Serial.printf("[FW_UPDATE] Flashing %s (%u bytes)\n", filepath, (unsigned)fileSize);

    if (!Update.begin(fileSize, U_FLASH)) {
        snprintf(errMsg, errLen, "Update.begin failed");
        Serial.printf("[FW_UPDATE] Update.begin failed: %s\n", Update.errorString());
        file.close();
        return false;
    }

    uint8_t buf[FW_CHUNK_SIZE];
    size_t written = 0;
    int lastPct = -1;

    while (written < fileSize) {
        size_t toRead = fileSize - written;
        if (toRead > FW_CHUNK_SIZE) toRead = FW_CHUNK_SIZE;

        size_t bytesRead = file.read(buf, toRead);
        if (bytesRead == 0) {
            snprintf(errMsg, errLen, "Read error at %u", (unsigned)written);
            Update.abort();
            file.close();
            return false;
        }

        size_t bytesWritten = Update.write(buf, bytesRead);
        if (bytesWritten != bytesRead) {
            snprintf(errMsg, errLen, "Write error at %u", (unsigned)written);
            Serial.printf("[FW_UPDATE] Write failed: %s\n", Update.errorString());
            Update.abort();
            file.close();
            return false;
        }

        written += bytesRead;

        int pct = (int)((written * 100) / fileSize);
        if (pct != lastPct) {
            updateProgressBar(pct);
            lastPct = pct;
        }
    }

    file.close();

    if (!Update.end(true)) {
        snprintf(errMsg, errLen, "Finalize failed");
        Serial.printf("[FW_UPDATE] end() failed: %s\n", Update.errorString());
        return false;
    }

    Serial.println("[FW_UPDATE] Flash complete!");
    return true;
}

// =============================================================================
// RESULT SCREENS
// =============================================================================

static void drawSuccessScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    drawGlitchTitle(48, "COMPLETE");

    tft.setTextSize(1);
    tft.setTextColor(TFT_GREEN);
    tft.setCursor(52, 100);
    tft.print("UPDATE COMPLETE!");

    // Countdown and reboot
    for (int i = 3; i >= 1; i--) {
        tft.fillRect(40, 140, 160, 20, TFT_BLACK);
        tft.setTextSize(1);
        tft.setTextColor(HALEHOUND_HOTPINK, TFT_BLACK);
        char buf[32];
        snprintf(buf, sizeof(buf), "Rebooting in %d...", i);
        int tw = strlen(buf) * 6;
        tft.setCursor((tft.width() - tw) / 2, 145);
        tft.print(buf);
        delay(1000);
    }

    ESP.restart();
}

static void drawErrorScreen(const char* msg) {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    // Icon bar with back
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    drawGlitchTitle(48, "ERROR");

    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(10, 100);
    tft.print("Update failed:");

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(10, 118);
    tft.print(msg);

    // BACK button
    tft.fillRoundRect(60, 200, 120, 36, 8, TFT_RED);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(84, 208);
    tft.print("BACK");
}

// Wait for back tap on error screen
static void waitForErrorBack() {
    while (true) {
        touchButtonsUpdate();

        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            // Back icon
            if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 26) {
                delay(150);
                return;
            }
            // BACK button (y=200-236, x=60-180)
            if (ty >= 200 && ty <= 236 && tx >= 60 && tx <= 180) {
                delay(150);
                return;
            }
        }

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) return;
        delay(20);
    }
}

// =============================================================================
// NO SD / NO FILES SCREENS
// =============================================================================

static void drawNoSDScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    drawGlitchTitle(48, "UPDATE FW");

    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(72, 120);
    tft.print("NO SD CARD");

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(28, 150);
    tft.print("Insert SD card and retry");
}

static void drawNoFilesScreen() {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar();

    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, ICON_SIZE, ICON_SIZE, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);

    drawGlitchTitle(48, "UPDATE FW");

    tft.setTextSize(1);
    tft.setTextColor(TFT_RED);
    tft.setCursor(60, 120);
    tft.print("NO .BIN FILES");

    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(12, 150);
    tft.print("Place firmware.bin on SD");
    tft.setCursor(12, 165);
    tft.print("root or /firmware/ folder");
}

static void waitForBackTap() {
    while (true) {
        touchButtonsUpdate();

        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty >= 20 && ty <= 36 && tx >= 10 && tx < 26) {
                delay(150);
                return;
            }
        }

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) return;
        delay(20);
    }
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

void firmwareUpdateScreen() {
    // Reset state
    selectedFileIndex = -1;
    scrollOffset = 0;
    binFileCount = 0;
    fwSDReady = false;

    // Step 1: Init SD card
    fwSDReady = initSDCard();
    if (!fwSDReady) {
        drawNoSDScreen();
        waitForBackTap();
        SD.end();
        return;
    }

    // Step 2: Scan for .bin files
    scanBinFiles();
    if (binFileCount == 0) {
        drawNoFilesScreen();
        waitForBackTap();
        SD.end();
        return;
    }

    // Step 3: File selection screen
    drawFileSelectScreen();

    // Step 4: Selection loop
    bool exitRequested = false;
    bool flashRequested = false;

    while (!exitRequested && !flashRequested) {
        touchButtonsUpdate();

        int result = handleFileSelectTouch();
        if (result == -1) exitRequested = true;
        if (result == 1) flashRequested = true;

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
            exitRequested = true;
        }

        delay(20);
    }

    if (exitRequested) {
        SD.end();
        return;
    }

    // Step 5: Validate selected file
    char errMsg[64];
    if (!validateBinFile(binFiles[selectedFileIndex], errMsg, sizeof(errMsg))) {
        drawErrorScreen(errMsg);
        waitForErrorBack();
        SD.end();
        return;
    }

    // Step 6: Confirm screen
    drawConfirmScreen();

    bool confirmed = false;
    while (true) {
        touchButtonsUpdate();

        int result = handleConfirmTouch();
        if (result == -1) break;       // Cancel
        if (result == 1) {
            confirmed = true;
            break;
        }

        if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) break;
        delay(20);
    }

    if (!confirmed) {
        // Return to file selection
        drawFileSelectScreen();

        exitRequested = false;
        flashRequested = false;

        while (!exitRequested && !flashRequested) {
            touchButtonsUpdate();

            int result = handleFileSelectTouch();
            if (result == -1) exitRequested = true;
            if (result == 1) {
                // Re-validate and re-confirm
                if (!validateBinFile(binFiles[selectedFileIndex], errMsg, sizeof(errMsg))) {
                    drawErrorScreen(errMsg);
                    waitForErrorBack();
                    SD.end();
                    return;
                }
                drawConfirmScreen();
                while (true) {
                    touchButtonsUpdate();
                    int cr = handleConfirmTouch();
                    if (cr == -1) {
                        drawFileSelectScreen();
                        break;
                    }
                    if (cr == 1) {
                        confirmed = true;
                        break;
                    }
                    if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
                        drawFileSelectScreen();
                        break;
                    }
                    delay(20);
                }
                if (confirmed) flashRequested = true;
            }

            if (buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
                exitRequested = true;
            }

            delay(20);
        }

        if (!confirmed) {
            SD.end();
            return;
        }
    }

    // Step 7: Flash!
    const char* fname = getFileName(binFiles[selectedFileIndex]);
    drawProgressScreen(fname);

    bool success = performFlashUpdate(binFiles[selectedFileIndex], errMsg, sizeof(errMsg));

    SD.end();

    if (success) {
        // Step 8: Success → reboot (never returns)
        drawSuccessScreen();
    } else {
        // Step 9: Error → back button
        drawErrorScreen(errMsg);
        waitForErrorBack();
    }
}
