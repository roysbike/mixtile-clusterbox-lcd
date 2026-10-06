#pragma once

// LCDWiki E32R28T: ESP32-WROOM-32E, ILI9341 240x320, XPT2046 on its own SPI.
// E32N28T is the same module without the resistive panel.

static constexpr int TFT_PIN_CS = 15;
static constexpr int TFT_PIN_DC = 2;
static constexpr int TFT_PIN_SCK = 14;
static constexpr int TFT_PIN_MOSI = 13;
static constexpr int TFT_PIN_MISO = 12;
static constexpr int TFT_PIN_BL = 21;

static constexpr int TP_PIN_SCK = 25;
static constexpr int TP_PIN_MOSI = 32;
static constexpr int TP_PIN_MISO = 39;
static constexpr int TP_PIN_CS = 33;
static constexpr int TP_PIN_IRQ = 36;

// Common-anode RGB. Low turns the LED on.
static constexpr int LED_PIN_RED = 22;
static constexpr int LED_PIN_GREEN = 16;
static constexpr int LED_PIN_BLUE = 17;

static constexpr int BOOT_PIN = 0;

// Landscape. Rotation is set in config.h.
static constexpr int PANEL_W = 320;
static constexpr int PANEL_H = 240;
