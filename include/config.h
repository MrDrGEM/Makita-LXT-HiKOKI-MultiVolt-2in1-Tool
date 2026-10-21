#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ==============================================================================
//  Hardware Configuration for Guition JC4827W543 (ESP32-S3 + 4.3" NV3041A QSPI)
// ==============================================================================

// --- Display: NV3041A 480x272 IPS via QSPI ---
#define GFX_QSPI_CS       45
#define GFX_QSPI_SCK      47
#define GFX_QSPI_D0       21
#define GFX_QSPI_D1       48
#define GFX_QSPI_D2       40
#define GFX_QSPI_D3       39
#define GFX_BL             1  // Primary Backlight pin
#define GFX_BL_ALT         2  // Alternative Backlight pin on some board revisions

#define SCREEN_WIDTH     480
#define SCREEN_HEIGHT    272

// --- Touch Controller: Goodix GT911 (Capacitive -C model via I2C) ---
#define TOUCH_GT911_SDA    8
#define TOUCH_GT911_SCL    4
#define TOUCH_GT911_INT    3
#define TOUCH_GT911_RST   38
#define TOUCH_GT911_ADDR  0x5D

// ==============================================================================
//  Battery Pin Assignments (Free GPIOs on JC4827W543)
// ==============================================================================

// --- HiKOKI / Metabo HPT MultiVolt (36V/18V) ---
// Wiring:
//   GND (-)    -> ESP32-S3 GND
//   Signal (T) -> GPIO 16 (4.7k pull-up to 3.3V)
//   Signal (LS)-> GPIO 17 (Inverted TX)
//   Signal (V) -> GPIO 18 (TX)
#define HIKOKI_PIN_IN      16
#define HIKOKI_PIN_INV_OUT 17
#define HIKOKI_PIN_OUT     18

// --- Makita 18V / 36V LXT ---
// Wiring:
//   GND (-)     -> ESP32-S3 GND
//   Data (1W)   -> GPIO 6 (4.7k pull-up to 3.3V, in series 120R)
//   Enable (EN) -> GPIO 7 (4.7k pull-up to 3.3V, in series 120R)
#define MAKITA_PIN_OW       6
#define MAKITA_PIN_EN       7

// --- Autonomous Power Management ---
// Onboard single-cell LiPo battery (JST 2-pin connector on PCB)
// or external step-down buck converter (18V/36V -> 5V into 5V/GND)
#define FIRMWARE_NAME       "Makita & HiKOKI Battery Tool"
#define FIRMWARE_VERSION    "v2.0-Standalone"

#endif // CONFIG_H
