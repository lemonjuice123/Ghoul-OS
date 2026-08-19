//====================================================================
// UI.h – UI state machine and function prototypes for Ghoul OS.
//====================================================================
#ifndef UI_H
#define UI_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "Apps.h"

// ---------------------------------------------------------------------------
// UI states.
// ---------------------------------------------------------------------------
enum UIState {
    BOOT,
    MENU,
    APP,
    KEYTEST,    // "Key Test" app: live keypad press visualizer
    SETTINGS,   // "Settings" app: audio feedback on/off toggle
    FILES,      // "Files" app: SD card directory listing
    FILEVIEW,  // "Files" app: simple text preview of a selected file
    MINIPIANO,  // "MiniPiano" app: 9-key mini piano
    BREAKOUT,   // "Games" app: breakout
    BMPVIEW,    // "Files" app: BMP image viewer
    PACKETMON,  // "PackMon" app: live 802.11 packet-rate graph
    WIFISCAN,   // "WiFi Scanner" app: nearby SSID scanner
    STOCKWATCH  // "StockWatch" app: live stock prices via Finnhub
};

extern UIState currentState;
extern int8_t currentAppIndex; // index of the app currently open in APP state

// ------------------------------ lifecycle ---------------------------------
void uiInit(Adafruit_ST7735 *displayPtr);
void uiHandleKey(char key);
void uiTick();

// ----------------------------- drawing API ---------------------------------
void drawBoot();
void drawStatusBar();
void drawMenu(bool fullRedraw);
void drawCarousel(bool fullRedraw);
void drawIcon(int16_t x, int16_t y, const uint16_t* icon, uint8_t w, uint8_t h);
void drawBitmapIcon(int16_t x, int16_t y, const uint16_t* bitmap, uint8_t w, uint8_t h);
void drawDescription();
void drawSelection();
bool animateScroll();
void drawApp();
void drawKeyTest();
void drawSettings();
void drawFiles();
void drawFileView();
void drawMiniPiano();
void drawBreakout();
void drawPacketMon();
void drawWifiScanner();
void drawStockWatch();
void clearContent();

#endif // UI_H
