#ifndef CYD_CONFIG_H
#define CYD_CONFIG_H

// ═══════════════════════════════════════════════════════════════════════════
// HaleHound-CYD Master Pin Configuration
// Supports: ESP32-2432S028 (2.8") and ESP32-3248S035 (3.5")
// Created: 2026-02-06
// ═══════════════════════════════════════════════════════════════════════════
//
// BOARD SELECTION: Uncomment ONE of the following lines
// (Must match User_Setup.h selection!)
// ═══════════════════════════════════════════════════════════════════════════

#define CYD_28    // ESP32-2432S028 - 2.8" 320x240 ILI9341
//#define CYD_35    // ESP32-3248S035 - 3.5" 480x320 ST7796

// ═══════════════════════════════════════════════════════════════════════════
// BOARD-SPECIFIC SETTINGS
// ═══════════════════════════════════════════════════════════════════════════

#ifdef CYD_28
  #define CYD_BOARD_NAME    "HaleHound-CYD 2.8\""
  #define CYD_SCREEN_WIDTH  240
  #define CYD_SCREEN_HEIGHT 320
  #define CYD_TFT_BL        21    // Backlight on GPIO21
#endif

#ifdef CYD_35
  #define CYD_BOARD_NAME    "HaleHound-CYD 3.5\""
  #define CYD_SCREEN_WIDTH  320
  #define CYD_SCREEN_HEIGHT 480
  #define CYD_TFT_BL        27    // Backlight on GPIO27
#endif

// ═══════════════════════════════════════════════════════════════════════════
// DISPLAY PINS (HSPI) - Same for both boards
// ═══════════════════════════════════════════════════════════════════════════
#define CYD_TFT_MISO    12
#define CYD_TFT_MOSI    13
#define CYD_TFT_SCLK    14
#define CYD_TFT_CS      15
#define CYD_TFT_DC       2
#define CYD_TFT_RST     -1    // Connected to EN reset

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH CONTROLLER (XPT2046)
// ═══════════════════════════════════════════════════════════════════════════

#define CYD_TOUCH_CS    33
#define CYD_TOUCH_IRQ   36    // Same on both boards

#ifdef CYD_28
  // 2.8" has SEPARATE touch SPI bus
  #define CYD_TOUCH_MOSI  32
  #define CYD_TOUCH_MISO  39
  #define CYD_TOUCH_CLK   25
#endif

#ifdef CYD_35
  // 3.5" SHARES SPI with display (resistive version)
  #define CYD_TOUCH_MOSI  13    // Shared with TFT
  #define CYD_TOUCH_MISO  12    // Shared with TFT
  #define CYD_TOUCH_CLK   14    // Shared with TFT

  // Capacitive GT911 version (uncomment if using 3248S035C)
  //#define CYD_USE_GT911
  //#define CYD_GT911_SDA   33
  //#define CYD_GT911_SCL   32
  //#define CYD_GT911_RST   25
  //#define CYD_GT911_INT   21
#endif

// ═══════════════════════════════════════════════════════════════════════════
// SHARED SPI BUS (VSPI) - SD Card, CC1101, and NRF24L01 share this bus
// Each device has its own CS pin - only one active at a time!
// ═══════════════════════════════════════════════════════════════════════════
#define VSPI_SCK        18    // Shared SPI Clock
#define VSPI_MOSI       23    // Shared SPI MOSI
#define VSPI_MISO       19    // Shared SPI MISO

// ═══════════════════════════════════════════════════════════════════════════
// SD CARD (Shares VSPI bus with radios)
// ═══════════════════════════════════════════════════════════════════════════
//
// WIRING DIAGRAM:
// ┌─────────────┐      ┌─────────────┐
// │  MicroSD    │      │     CYD     │
// │   Card      │      │   ESP32     │
// ├─────────────┤      ├─────────────┤
// │ VCC ────────┼──────┤ 3.3V        │
// │ GND ────────┼──────┤ GND         │
// │ CLK ────────┼──────┤ GPIO 18     │ (shared VSPI)
// │ MOSI ───────┼──────┤ GPIO 23     │ (shared VSPI)
// │ MISO ───────┼──────┤ GPIO 19     │ (shared VSPI)
// │ CS ─────────┼──────┤ GPIO 5      │ (SD exclusive)
// └─────────────┘      └─────────────┘
//
// NOTE: SD card on CYD is built-in (microSD slot on back of board)
// Perfect for storing DuckyScript payloads!
//
// ═══════════════════════════════════════════════════════════════════════════

#define SD_CS            5    // SD Card Chip Select (built-in slot)
#define SD_SCK          VSPI_SCK
#define SD_MOSI         VSPI_MOSI
#define SD_MISO         VSPI_MISO

// Legacy aliases for radio code
#define RADIO_SPI_SCK   VSPI_SCK
#define RADIO_SPI_MOSI  VSPI_MOSI
#define RADIO_SPI_MISO  VSPI_MISO

// ═══════════════════════════════════════════════════════════════════════════
// CC1101 SubGHz RADIO (Red HW-863 Module)
// ═══════════════════════════════════════════════════════════════════════════
//
// WIRING DIAGRAM:
// ┌─────────────┐      ┌─────────────┐
// │   CC1101    │      │     CYD     │
// │   HW-863    │      │   ESP32     │
// ├─────────────┤      ├─────────────┤
// │ VCC ────────┼──────┤ 3.3V        │
// │ GND ────────┼──────┤ GND         │
// │ SCK ────────┼──────┤ GPIO 18     │
// │ MOSI ───────┼──────┤ GPIO 23     │
// │ MISO ───────┼──────┤ GPIO 19     │
// │ CS ─────────┼──────┤ GPIO 27     │ (CN1 connector)
// │ GDO0 ───────┼──────┤ GPIO 22     │ (P3 connector) TX to radio
// │ GDO2 ───────┼──────┤ GPIO 35     │ (P3 connector) RX from radio
// └─────────────┘      └─────────────┘
//
// IMPORTANT: GDO0/GDO2 naming is confusing!
// - GDO0 (GPIO22) = Data going TO the CC1101 (for TX)
// - GDO2 (GPIO35) = Data coming FROM the CC1101 (for RX)
// This matches the HaleHound fix for CiferTech's swapped pins.
//
// ═══════════════════════════════════════════════════════════════════════════

#define CC1101_CS       27    // Chip Select - CN1 connector
#define CC1101_GDO0     22    // TX data TO radio - P3 connector
#ifdef NMRF_HAT
  #define CC1101_GDO2   22    // Hat: no GDO2 wire — GDO0 outputs same RX data
#else
  #define CC1101_GDO2   35    // RX data FROM radio - P3 connector (INPUT ONLY)
#endif

// SPI bus aliases
#define CC1101_SCK      RADIO_SPI_SCK
#define CC1101_MOSI     RADIO_SPI_MOSI
#define CC1101_MISO     RADIO_SPI_MISO

// RCSwitch compatibility (HaleHound pin naming)
// REMEMBER: CiferTech had TX/RX swapped - we fixed it!
#define TX_PIN          CC1101_GDO0   // GPIO22 - enableTransmit()
#define RX_PIN          CC1101_GDO2   // GPIO35 - enableReceive()

// ═══════════════════════════════════════════════════════════════════════════
// NRF24L01+PA+LNA 2.4GHz RADIO
// ═══════════════════════════════════════════════════════════════════════════
//
// WIRING DIAGRAM:
// ┌─────────────┐      ┌─────────────┐
// │  NRF24L01   │      │     CYD     │
// │  +PA+LNA    │      │   ESP32     │
// ├─────────────┤      ├─────────────┤
// │ VCC ────────┼──────┤ 3.3V        │ (add 10uF cap if unstable!)
// │ GND ────────┼──────┤ GND         │
// │ SCK ────────┼──────┤ GPIO 18     │ (shared with CC1101)
// │ MOSI ───────┼──────┤ GPIO 23     │ (shared with CC1101)
// │ MISO ───────┼──────┤ GPIO 19     │ (shared with CC1101)
// │ CSN ────────┼──────┤ GPIO 4      │ (was RGB Red LED)
// │ CE ─────────┼──────┤ GPIO 16     │ (was RGB Green LED)
// │ IRQ ────────┼──────┤ GPIO 17     │ (was RGB Blue LED) OPTIONAL
// └─────────────┘      └─────────────┘
//
// NOTE: The +PA+LNA version needs clean 3.3V power!
// Add a 10uF capacitor between VCC and GND at the module if you get
// communication errors or the module resets randomly.
//
// ═══════════════════════════════════════════════════════════════════════════

#ifdef NMRF_HAT
  // NM-RF-Hat: physical switch selects CC1101 or NRF24 on same two GPIOs
  #define NRF24_CSN     27    // Shared with CC1101_CS via hat switch
  #define NRF24_CE      22    // Shared with CC1101_GDO0 via hat switch
#else
  #define NRF24_CSN      4    // Chip Select - was RGB Red
  #define NRF24_CE      16    // Chip Enable - was RGB Green
  #define NRF24_IRQ     17    // Interrupt - was RGB Blue (OPTIONAL)
#endif

// SPI bus aliases
#define NRF24_SCK       RADIO_SPI_SCK
#define NRF24_MOSI      RADIO_SPI_MOSI
#define NRF24_MISO      RADIO_SPI_MISO

// ═══════════════════════════════════════════════════════════════════════════
// GPS NEO-6M MODULE (Software Serial)
// ═══════════════════════════════════════════════════════════════════════════
//
// WIRING: GT-U7 GPS connected to P1 JST connector
// ┌─────────────┐      ┌─────────────┐
// │   GT-U7     │      │  CYD P1     │
// │    GPS      │      │  Connector  │
// ├─────────────┤      ├─────────────┤
// │ VCC ────────┼──────┤ VIN         │
// │ GND ────────┼──────┤ GND         │
// │ TX ─────────┼──────┤ RX (GPIO 3) │ (ESP receives GPS data)
// │ RX ─────────┼──────┤ TX (GPIO 1) │ (not used)
// └─────────────┘      └─────────────┘
//
// NOTE: P1 RX/TX are shared with CH340C USB serial.
// When GPS is active, Serial RX from computer is unavailable.
// Serial.println() debug output still works (UART0 TX on GPIO1).
// Uses HardwareSerial UART2 remapped to GPIO3 for reliable reception.
//
// ═══════════════════════════════════════════════════════════════════════════

#define GPS_RX_PIN       3    // P1 RX pin - ESP32 receives from GPS TX
#define GPS_TX_PIN      -1    // Not used - GPS is receive-only
#define GPS_BAUD      9600    // GT-U7 default baud rate

// ═══════════════════════════════════════════════════════════════════════════
// UART SERIAL MONITOR
// ═══════════════════════════════════════════════════════════════════════════
// UART passthrough for hardware hacking - read target device debug ports
// P1 connector: Full duplex via UART0 GPIO3/1 (shared with USB serial)
// Speaker connector: RX only via GPIO26

#define UART_MON_P1_RX        3    // P1 RX pin (shared with USB Serial RX)
#define UART_MON_P1_TX        1    // P1 TX pin (shared with USB Serial TX)
#define UART_MON_SPK_RX      26    // Speaker connector pin (RX only)
#define UART_MON_DEFAULT_BAUD 115200
#define CYD_HAS_SERIAL_MON    1

// ═══════════════════════════════════════════════════════════════════════════
// BUTTONS
// ═══════════════════════════════════════════════════════════════════════════
// CYD boards only have BOOT button - use touchscreen for navigation

#define BOOT_BUTTON      0    // GPIO0 - active LOW, directly readable

// ═══════════════════════════════════════════════════════════════════════════
// TOUCH BUTTON ZONES (Virtual buttons on touchscreen)
// Coordinates for PORTRAIT orientation
// ═══════════════════════════════════════════════════════════════════════════

// For 2.8" (240x320):
#ifdef CYD_28
  // UP button - top left
  #define TOUCH_BTN_UP_X1      0
  #define TOUCH_BTN_UP_Y1      0
  #define TOUCH_BTN_UP_X2     80
  #define TOUCH_BTN_UP_Y2     60

  // DOWN button - bottom left
  #define TOUCH_BTN_DOWN_X1    0
  #define TOUCH_BTN_DOWN_Y1  260
  #define TOUCH_BTN_DOWN_X2   80
  #define TOUCH_BTN_DOWN_Y2  320

  // SELECT button - center
  #define TOUCH_BTN_SEL_X1    80
  #define TOUCH_BTN_SEL_Y1   130
  #define TOUCH_BTN_SEL_X2   160
  #define TOUCH_BTN_SEL_Y2   190

  // BACK button - top right
  #define TOUCH_BTN_BACK_X1  160
  #define TOUCH_BTN_BACK_Y1    0
  #define TOUCH_BTN_BACK_X2  240
  #define TOUCH_BTN_BACK_Y2   60
#endif

// For 3.5" (320x480):
#ifdef CYD_35
  // UP button - top left
  #define TOUCH_BTN_UP_X1      0
  #define TOUCH_BTN_UP_Y1      0
  #define TOUCH_BTN_UP_X2    107
  #define TOUCH_BTN_UP_Y2     80

  // DOWN button - bottom left
  #define TOUCH_BTN_DOWN_X1    0
  #define TOUCH_BTN_DOWN_Y1  400
  #define TOUCH_BTN_DOWN_X2  107
  #define TOUCH_BTN_DOWN_Y2  480

  // SELECT button - center
  #define TOUCH_BTN_SEL_X1   107
  #define TOUCH_BTN_SEL_Y1   200
  #define TOUCH_BTN_SEL_X2   213
  #define TOUCH_BTN_SEL_Y2   280

  // BACK button - top right
  #define TOUCH_BTN_BACK_X1  213
  #define TOUCH_BTN_BACK_Y1    0
  #define TOUCH_BTN_BACK_X2  320
  #define TOUCH_BTN_BACK_Y2   80
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FEATURE FLAGS
// ═══════════════════════════════════════════════════════════════════════════

#define CYD_HAS_CC1101      1     // CC1101 SubGHz radio connected
#define CYD_HAS_NRF24       1     // NRF24L01+PA+LNA 2.4GHz radio connected
#define CYD_HAS_GPS         1     // NEO-6M GPS module connected
#define CYD_HAS_SDCARD      1     // SD card ENABLED (shares VSPI with radios)
#define CYD_HAS_RGB_LED     0     // RGB LED DISABLED (pins used for NRF24)
#define CYD_HAS_SPEAKER     0     // Speaker DISABLED (pin used for GPS)
#define CYD_HAS_PCF8574     0     // No I2C button expander (unlike ESP32-DIV)

// ═══════════════════════════════════════════════════════════════════════════
// SPI BUS SHARING - ACTIVE DEVICES
// ═══════════════════════════════════════════════════════════════════════════
//
// VSPI Bus (GPIO 18/19/23) is SHARED by THREE devices:
//   ┌──────────┬─────────┬───────────────────────────────┐
//   │ Device   │ CS Pin  │ Notes                         │
//   ├──────────┼─────────┼───────────────────────────────┤
//   │ SD Card  │ GPIO 5  │ Built-in slot, payload storage│
//   │ CC1101   │ GPIO 27 │ SubGHz radio                  │
//   │ NRF24    │ GPIO 4  │ 2.4GHz radio                  │
//   └──────────┴─────────┴───────────────────────────────┘
//
// IMPORTANT: Only ONE device active at a time!
// Before using a device: Pull its CS LOW, all others HIGH
//
// ═══════════════════════════════════════════════════════════════════════════
// DISABLED/REPURPOSED PINS
// ═══════════════════════════════════════════════════════════════════════════
//
// RGB LED (DISABLED - pins used for NRF24):
//   RGB_RED   = GPIO 4  → NRF24_CSN
//   RGB_GREEN = GPIO 16 → NRF24_CE
//   RGB_BLUE  = GPIO 17 → NRF24_IRQ / GPS_TX
//
// Speaker (DISABLED - pin used for GPS):
//   SPEAKER = GPIO 26 → GPS_RX_PIN
//
// LDR Light Sensor (AVAILABLE - not repurposed):
//   LDR = GPIO 34 (input only, 12-bit ADC)
//   Could use for: ambient light detection, battery voltage divider

// ═══════════════════════════════════════════════════════════════════════════
// POWER MANAGEMENT (Optional)
// ═══════════════════════════════════════════════════════════════════════════
// If you connect battery voltage through a divider to GPIO34:
//   LiPo 3.7V → 10K → GPIO34 → 10K → GND (2:1 divider)
//   Reading: (ADC_value / 4095.0) * 3.3 * 2 = battery voltage

#define BATTERY_ADC_PIN    34
#define BATTERY_DIVIDER    2.0

// ═══════════════════════════════════════════════════════════════════════════
// DEBUG SETTINGS
// ═══════════════════════════════════════════════════════════════════════════

#define CYD_DEBUG           1
#define CYD_DEBUG_BAUD 115200

// ═══════════════════════════════════════════════════════════════════════════
// VALIDATION
// ═══════════════════════════════════════════════════════════════════════════

#if !defined(CYD_28) && !defined(CYD_35)
  #error "CYD_CONFIG: Define either CYD_28 or CYD_35 at the top of cyd_config.h"
#endif

#if defined(CYD_28) && defined(CYD_35)
  #error "CYD_CONFIG: Cannot define both CYD_28 and CYD_35 - choose one"
#endif

#endif // CYD_CONFIG_H
