#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SD.h>
#include "UI.h"
#include "Config.h"
#include "Icons.h"
#include "FileBrowser.h"
#include "Audio.h"
#include "Theme.h"
#include "Prefs.h"
#include "BleHid.h"
#include "PacketMon.h"
#include "WifiScan.h"
#include "StockWatch.h"

// =====================================================================
// Ghoul OS - UI.cpp
//
// Handles the boot splash, the animated text launcher and every app
// screen. Redraws are kept partial wherever possible to avoid flicker:
// only the regions that actually changed are cleared and repainted.
// =====================================================================

// ------------------------------------------------------------------
// Module state
// ------------------------------------------------------------------
static Adafruit_ST7735 *tft = nullptr;

UIState currentState = BOOT;
int8_t currentAppIndex = 0; // app shown while in APP state

static int8_t selectedIndex = 0;  // confirmed/target selection in the menu

static float displayIndex = 0.0f;      // animated fractional carousel position
static int8_t targetIndex = 0;         // where the animation is heading
static float animStartValue = 0.0f;
static unsigned long animStartTime = 0;
static bool animating = false;

static unsigned long lastFrameTime = 0;
static unsigned long lastClockUpdate = 0;
static unsigned long bootStartTime = 0;
static unsigned long secondsSinceBoot = 0;

// ------------------------------ Key Test app -------------------------------
// Mirrors the physical 4x4 keypad layout in GhoulOS.ino so the on-screen
// grid lines up with the real buttons.
static const char keyTestLayout[KEYTEST_ROWS][KEYTEST_COLS] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};
static char keyTestLastKey = 0; // 0 = nothing highlighted yet

// ------------------------------- Music app ----------------------------------
static uint8_t musicLastNote = 255; // 255 = nothing highlighted yet (0-8 = a note)

// ------------------------------- Files app ----------------------------------
static uint8_t filesCursor = 0;     // index into the *displayed* list (incl. "..")
static uint8_t filesScrollTop = 0;  // display index of the first visible row

// ---------------------------- Settings app ---------------------------------
// Rows: 0 = audio feedback, 1 = colour theme, 2 = splash image,
// 3 = auto-sleep timeout. Selecting a row opens its list (settingsSub).
enum SettingsSub : uint8_t
{
    SETTINGS_SUB_NONE = 0,
    SETTINGS_SUB_THEME,
    SETTINGS_SUB_SPLASH,
    SETTINGS_SUB_SLEEP
};

static uint8_t     settingsCursor = 0;
static SettingsSub settingsSub = SETTINGS_SUB_NONE;

// Splash-image list: entry 0 is always the built-in logo, followed by
// every .bmp found in the SD card's root folder.
static char    splashFiles[SPLASH_MAX_FILES][FB_MAX_NAME_LEN + 2];
static uint8_t splashFileCount = 0;
static uint8_t splashCursor = 0;
static uint8_t splashScrollTop = 0;

static uint8_t sleepCursor = 0;

// ---------------------------- Auto sleep -----------------------------------
// Any key press resets lastActivityMs. When the timeout chosen in
// Settings elapses the panel is switched off until the next key press.
static unsigned long lastActivityMs = 0;
static bool          deviceAsleep = false;

// ----------------------------- File preview -----------------------------
// FILEVIEW_MAX_LINES stays comfortably under 255 so fileViewLineCount/
// filePreviewLineOffset can remain plain uint8_t without any risk of
// wraparound.
#define FILEVIEW_MAX_LINES 240
#define FILEVIEW_LINE_LEN  26

static char filePreviewBuffer[FB_PREVIEW_BUF_SIZE + 1];
static size_t filePreviewLen = 0;
static uint8_t filesViewingEntry = 0; // index into fileEntries[] being previewed
static char fileViewLines[FILEVIEW_MAX_LINES][FILEVIEW_LINE_LEN];
static uint8_t fileViewLineCount = 0;
static uint8_t filePreviewLineOffset = 0; // which wrapped line is scrolled to the top
static bool filePreviewIsBinary = false;        // content looked like binary, not text
static bool filePreviewBufferTruncated = false; // file is bigger than FB_PREVIEW_BUF_SIZE
static bool filePreviewLinesTruncated = false;  // more wrapped lines than FILEVIEW_MAX_LINES allows

// ----------------------------- Breakout game ------------------------------
static int16_t breakoutPaddleX;
static int16_t breakoutBallX, breakoutBallY;
static int8_t  breakoutBallDX, breakoutBallDY;
static uint8_t breakoutBricks[BREAKOUT_BRICK_ROWS][BREAKOUT_BRICK_COLS];
static uint8_t breakoutLives;
static uint16_t breakoutScore;
static bool    breakoutLaunched;
static bool    breakoutGameWon;
static bool    breakoutGameOver;
static unsigned long lastBreakoutUpdate = 0;

// ----------------------------- WiFi Scanner state ---------------------------
static uint8_t wifiCursor = 0;
static uint8_t wifiScrollTop = 0;
static uint16_t wifiMarqueeOffset = 0; // shared ticker position for overflowing SSIDs

// ----------------------------- StockWatch state -----------------------------
static uint8_t stockCursor = 0;
static uint8_t stockScrollTop = 0;

// ----------------------------- Calculator state -----------------------------
enum CalcStage {
    CALC_STAGE_NUM1,
    CALC_STAGE_NUM2,
    CALC_STAGE_OP,
    CALC_STAGE_RESULT
};

static CalcStage calcStage = CALC_STAGE_NUM1;
static char calcNum1Buf[CALC_MAX_DIGITS + 1] = "";
static char calcNum2Buf[CALC_MAX_DIGITS + 1] = "";
static uint8_t calcNum1Len = 0;
static uint8_t calcNum2Len = 0;
static uint8_t calcOpIndex = 0;
static double calcResult = 0.0;
static bool calcHasError = false;
static char calcErrorMsg[28] = "";

// ----------------------------- Matrix state -----------------------------
struct MatrixColumn {
    int16_t headRow;      // current head row (can be negative = not yet on screen)
    uint8_t trailLen;     // how many rows behind the head stay lit before going blank
    uint8_t speedDiv;     // this column advances once every speedDiv ticks
    uint8_t speedCounter;
};
static MatrixColumn matrixCols[MATRIX_COLS];
static const char matrixChars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%&*+-/=<>?";

// ----------------------------- 2048 state -----------------------------
// Tile values are stored as uint32_t (not uint16_t) purely so there is
// zero risk of overflow even if play went absurdly far beyond what a
// 4x4 board can realistically reach.
static uint32_t g2048Grid[G2048_SIZE][G2048_SIZE];
static uint32_t g2048Score = 0;
static bool g2048GameOver = false;

// RGB565 tile colors, indexed by "level" = log2(value) - 1 (tile value 2
// is level 0). Levels 0-1 echo the original game's light neutral early
// tiles; levels 2-19 are an evenly spaced color-wheel sweep so every
// higher tile is clearly, distinctly colored rather than just fading to
// grey -- this is a deliberate departure from the rest of the UI's
// green monochrome theme, specifically for this app, as asked.
static const uint16_t g2048TileColors[] = {
    0xEF3B, // level  0 -- tile        2
    0xEF19, // level  1 -- tile        4
    0xE945, // level  2 -- tile        8
    0xEB45, // level  3 -- tile       16
    0xED65, // level  4 -- tile       32
    0xEF65, // level  5 -- tile       64
    0xAF65, // level  6 -- tile      128
    0x6F65, // level  7 -- tile      256
    0x2F65, // level  8 -- tile      512
    0x2F6D, // level  9 -- tile     1024
    0x2F75, // level 10 -- tile     2048
    0x2F7D, // level 11 -- tile     4096
    0x2D7D, // level 12 -- tile     8192
    0x2B5D, // level 13 -- tile    16384
    0x295D, // level 14 -- tile    32768
    0x695D, // level 15 -- tile    65536
    0xA95D, // level 16 -- tile   131072
    0xE95D, // level 17 -- tile   262144
    0xE955, // level 18 -- tile   524288
    0xE94D, // level 19 -- tile  1048576
};
#define G2048_COLOR_COUNT (sizeof(g2048TileColors) / sizeof(g2048TileColors[0]))

// Forward declarations for internal helpers (not part of the public UI.h API)
static void startCarouselAnimation();
static void drawKeyTestCell(uint8_t row, uint8_t col, bool highlighted);
static void handleKeyTestPress(char key);
static void drawMusicCell(uint8_t row, uint8_t col, bool highlighted);
static void handleMusicKeyPress(char key);
static void drawSettingsRow(uint8_t row);
static void drawThemePicker();
static void drawSplashPicker();
static void drawSleepPicker();
static void scanSplashFiles();
static void redrawCurrentScreen();
static bool bmpShowFullScreen(const char* path);
static bool filesGetDisplayEntry(uint8_t displayIdx, const char** outName, bool* outIsDir, uint32_t* outSize);
static void formatFileSize(uint32_t size, char* out, size_t outSize);
static void drawFilesChrome();
static void drawFilesRow(uint8_t rowSlot, bool cursorHere);
static void refreshFilesRows();
static void drawFilesReturn();
static void openFilePreview(uint8_t realIdx);
static bool wrapFileBuffer();
static bool looksBinary(const char* buf, size_t len);
static void breakoutInit();
static void breakoutTick();
static void breakoutHandleKey(char key);
static void breakoutEraseBallAt(int16_t x, int16_t y);
static void breakoutDrawBallAt(int16_t x, int16_t y);
static void breakoutErasePaddleAt(int16_t x);
static void breakoutDrawPaddleAt(int16_t x);
static void breakoutEraseBrickAt(uint8_t r, uint8_t c);
static void breakoutUpdateHUD();
static void breakoutEraseMessageArea();
static void drawPacketMonStatus();
static void drawPacketMonGraph();
static void drawWifiScannerStatus();
static void drawWifiScannerList();
static void drawStockWatchStatus();
static void drawStockWatchList();
static void calculatorInit();
static void calculatorHandleKey(char key);
static void calculatorCompute();
static void calculatorFormatResult(char* out, size_t outSize);
static void matrixTick();
static void g2048Init();
static void g2048HandleKey(char key);
static bool g2048Move(char direction);
static bool g2048SlideLine(uint32_t* line, uint8_t len, uint32_t* scoreDelta);
static void g2048SpawnTile();
static bool g2048HasMoves();
static uint8_t g2048TileLevel(uint32_t value);

// =====================================================================
// BLE keyboard app -- link screen, Keyboard/Remote submenu, on-screen
// keyboard and the remote D-pad. All Bluetooth work lives in BleHid.cpp;
// everything below is purely presentation and key routing.
// =====================================================================

enum BleMenuOption : uint8_t {
    BLE_MENU_KEYBOARD = 0,
    BLE_MENU_REMOTE   = 1
};

enum RemoteButton : uint8_t {
    REMOTE_BTN_UP = 0,
    REMOTE_BTN_DOWN,
    REMOTE_BTN_LEFT,
    REMOTE_BTN_RIGHT,
    REMOTE_BTN_ENTER,
    REMOTE_BTN_ESC,
    REMOTE_BTN_VOL_UP,
    REMOTE_BTN_VOL_DOWN,
    REMOTE_BTN_NONE = 255
};

static uint8_t bleMenuCursor = BLE_MENU_KEYBOARD; // highlighted submenu row
static bool    bleUiConnected = false;            // last connection state drawn
static uint8_t remoteActiveButton = REMOTE_BTN_NONE; // last remote button pressed

// ---- Keyboard screen ------------------------------------------------
// "Keyboard" opens a scrollable list of the individual keystrokes this
// app can send: one row per key, Up/Down to choose, Select to send it.
// Deliberately not a numpad grid -- every entry is a single keystroke.
enum BleKeystrokeSpecial : uint8_t
{
    BLE_SPECIAL_NONE = 0,
    BLE_SPECIAL_ENTER,
    BLE_SPECIAL_BACKSPACE,
    BLE_SPECIAL_TAB,
    BLE_SPECIAL_SPACE,
    BLE_SPECIAL_ESCAPE,
    BLE_SPECIAL_UP,
    BLE_SPECIAL_DOWN,
    BLE_SPECIAL_LEFT,
    BLE_SPECIAL_RIGHT,
    BLE_SPECIAL_VOL_UP,
    BLE_SPECIAL_VOL_DOWN
};

// label  = what the list shows
// ch     = printable character to type (0 for a named key)
// special= named key to send when ch is 0
struct BleKeystrokeEntry
{
    const char* label;
    char        ch;
    uint8_t     special;
};

static const BleKeystrokeEntry bleKeystrokeList[] = {
    { "Enter",        0,   BLE_SPECIAL_ENTER      },
    { "Backspace",    0,   BLE_SPECIAL_BACKSPACE  },
    { "Tab",          0,   BLE_SPECIAL_TAB        },
    { "Space",        0,   BLE_SPECIAL_SPACE      },
    { "Escape",       0,   BLE_SPECIAL_ESCAPE     },
    { "Up arrow",     0,   BLE_SPECIAL_UP         },
    { "Down arrow",   0,   BLE_SPECIAL_DOWN       },
    { "Left arrow",   0,   BLE_SPECIAL_LEFT       },
    { "Right arrow",  0,   BLE_SPECIAL_RIGHT      },
    { "Volume up",    0,   BLE_SPECIAL_VOL_UP     },
    { "Volume down",  0,   BLE_SPECIAL_VOL_DOWN   },
    { "1", '1', BLE_SPECIAL_NONE },
    { "2", '2', BLE_SPECIAL_NONE },
    { "3", '3', BLE_SPECIAL_NONE },
    { "4", '4', BLE_SPECIAL_NONE },
    { "5", '5', BLE_SPECIAL_NONE },
    { "6", '6', BLE_SPECIAL_NONE },
    { "7", '7', BLE_SPECIAL_NONE },
    { "8", '8', BLE_SPECIAL_NONE },
    { "9", '9', BLE_SPECIAL_NONE },
    { "0", '0', BLE_SPECIAL_NONE },
    { "*", '*', BLE_SPECIAL_NONE },
    { "#", '#', BLE_SPECIAL_NONE },
    { "A", 'A', BLE_SPECIAL_NONE },
    { "B", 'B', BLE_SPECIAL_NONE },
    { "C", 'C', BLE_SPECIAL_NONE },
    { "D", 'D', BLE_SPECIAL_NONE }
};

static const uint8_t BLE_KEYSTROKE_COUNT =
    (uint8_t)(sizeof(bleKeystrokeList) / sizeof(bleKeystrokeList[0]));

static uint8_t       bleKbCursor     = 0;    // highlighted keystroke
static uint8_t       bleKbScrollTop  = 0;    // first row on screen
static uint8_t       bleKbFlashRow   = 0xFF; // row just sent (0xFF = none)
static unsigned long bleKbFlashUntil = 0;    // when that flash ends

// Draws one row of the keystroke list (only when it is on screen).
static void drawBleKeyboardRow(uint8_t index)
{
    if (index >= BLE_KEYSTROKE_COUNT) return;
    if (index < bleKbScrollTop) return;
    if (index >= (uint8_t)(bleKbScrollTop + BLE_LIST_VISIBLE)) return;

    int16_t slot = (int16_t)(index - bleKbScrollTop);
    int16_t x = 8;
    int16_t y = BLE_LIST_Y + slot * BLE_LIST_ROW_H;
    int16_t w = SCREEN_WIDTH - 16;
    int16_t h = BLE_LIST_ROW_H - 2;

    bool flashing   = (bleKbFlashRow == index);
    bool cursorHere = (index == bleKbCursor);

    tft->fillRect(x, y, w, h, COLOR_BG);

    if (flashing)
    {
        tft->fillRect(x, y, w, h, COLOR_ACCENT);
    }
    else if (cursorHere)
    {
        tft->fillRect(x, y, w, h, COLOR_SELECT_BG);
        tft->drawRect(x, y, w, h, COLOR_ACCENT_DIM);
    }

    tft->setTextSize(1);
    tft->setTextColor(flashing ? COLOR_BG : (cursorHere ? COLOR_FG : COLOR_DIM));
    tft->setCursor(x + 5, y + (h - 8) / 2);
    tft->print(bleKeystrokeList[index].label);
}

// Thin scrollbar on the right edge, so it is obvious the list continues.
static void drawBleKeyboardScrollBar()
{
    int16_t x = SCREEN_WIDTH - 5;
    int16_t y = BLE_LIST_Y;
    int16_t h = BLE_LIST_VISIBLE * BLE_LIST_ROW_H;

    tft->fillRect(x, y, 3, h, COLOR_BG);

    if (BLE_KEYSTROKE_COUNT <= BLE_LIST_VISIBLE)
    {
        return;
    }

    tft->drawRect(x, y, 3, h, COLOR_DIM);

    int16_t thumbH = (int16_t)((int32_t)h * BLE_LIST_VISIBLE / BLE_KEYSTROKE_COUNT);
    if (thumbH < 6) thumbH = 6;

    int16_t span   = h - thumbH;
    int16_t thumbY = y + (int16_t)((int32_t)span * bleKbScrollTop /
                                   (BLE_KEYSTROKE_COUNT - BLE_LIST_VISIBLE));

    tft->fillRect(x, thumbY, 3, thumbH, COLOR_FG);
}

// Repaints every visible row plus the scrollbar -- used when the list is
// first shown and whenever it scrolls.
static void drawBleKeyboardRows()
{
    for (uint8_t i = 0; i < BLE_LIST_VISIBLE; i++)
    {
        uint8_t index = (uint8_t)(bleKbScrollTop + i);

        if (index >= BLE_KEYSTROKE_COUNT)
        {
            int16_t y = BLE_LIST_Y + i * BLE_LIST_ROW_H;
            tft->fillRect(8, y, SCREEN_WIDTH - 16, BLE_LIST_ROW_H - 2, COLOR_BG);
            continue;
        }

        drawBleKeyboardRow(index);
    }

    drawBleKeyboardScrollBar();
}

// Moves the highlight by one row, scrolling the list if it runs off.
static void bleKbMoveCursor(int8_t delta)
{
    int16_t next = (int16_t)bleKbCursor + delta;
    if (next < 0 || next >= (int16_t)BLE_KEYSTROKE_COUNT)
    {
        return;
    }

    uint8_t previous    = bleKbCursor;
    uint8_t previousTop = bleKbScrollTop;
    bleKbCursor = (uint8_t)next;

    if (bleKbCursor < bleKbScrollTop)
    {
        bleKbScrollTop = bleKbCursor;
    }
    else if (bleKbCursor >= (uint8_t)(bleKbScrollTop + BLE_LIST_VISIBLE))
    {
        bleKbScrollTop = (uint8_t)(bleKbCursor - BLE_LIST_VISIBLE + 1);
    }

    if (bleKbScrollTop != previousTop)
    {
        drawBleKeyboardRows();
    }
    else
    {
        drawBleKeyboardRow(previous);
        drawBleKeyboardRow(bleKbCursor);
    }
}

// Sends the given keystroke over BLE and lights its row briefly so the
// send is visible on the panel.
static void sendBleKeystroke(uint8_t index)
{
    if (index >= BLE_KEYSTROKE_COUNT)
    {
        return;
    }

    const BleKeystrokeEntry &entry = bleKeystrokeList[index];

    if (bleHidIsConnected())
    {
        if (entry.ch != 0)
        {
            bleHidSendChar(entry.ch);
        }
        else
        {
            switch (entry.special)
            {
                case BLE_SPECIAL_ENTER:     bleHidSendEnter();      break;
                case BLE_SPECIAL_BACKSPACE: bleHidSendBackspace();  break;
                case BLE_SPECIAL_TAB:       bleHidSendTab();        break;
                case BLE_SPECIAL_SPACE:     bleHidSendSpace();      break;
                case BLE_SPECIAL_ESCAPE:    bleHidSendEscape();     break;
                case BLE_SPECIAL_UP:        bleHidSendUp();         break;
                case BLE_SPECIAL_DOWN:      bleHidSendDown();       break;
                case BLE_SPECIAL_LEFT:      bleHidSendLeft();       break;
                case BLE_SPECIAL_RIGHT:     bleHidSendRight();      break;
                case BLE_SPECIAL_VOL_UP:    bleHidSendVolumeUp();   break;
                case BLE_SPECIAL_VOL_DOWN:  bleHidSendVolumeDown(); break;
                default: break;
            }
        }
    }

    bleKbFlashRow   = index;
    bleKbFlashUntil = millis() + BLE_LIST_FLASH_MS;
    drawBleKeyboardRow(index);
}

// Draws one labelled box (ESC / VOL UP / VOL DOWN), filled when active.
static void drawRemoteBox(int16_t x, int16_t y, int16_t w, int16_t h,
                          const char* label, bool active)
{
    tft->fillRect(x, y, w, h, active ? COLOR_ACCENT : COLOR_BG);
    tft->drawRect(x, y, w, h, active ? COLOR_ACCENT : COLOR_FG);

    tft->setTextSize(1);
    tft->setTextColor(active ? COLOR_BG : COLOR_FG);
    int16_t lx = x + (w - (int16_t)strlen(label) * 6) / 2;
    int16_t ly = y + (h - 8) / 2;
    tft->setCursor(lx, ly);
    tft->print(label);
}

// Draws one button of the remote screen. active = filled solid (the
// "this is the button you just pressed" highlight); inactive = hollow
// outline, so only one shape is ever lit at a time.
static void drawRemoteButton(uint8_t button, bool active)
{
    uint16_t fill = active ? COLOR_ACCENT : COLOR_BG;
    uint16_t line = active ? COLOR_ACCENT : COLOR_FG;
    int16_t  midY = (REMOTE_ROW_TOP + REMOTE_ROW_BOTTOM) / 2;

    switch (button)
    {
        case REMOTE_BTN_UP:
            tft->fillTriangle(REMOTE_MID_X, REMOTE_UP_APEX_Y,
                              REMOTE_MID_X - REMOTE_UP_HW, REMOTE_UP_BASE_Y,
                              REMOTE_MID_X + REMOTE_UP_HW, REMOTE_UP_BASE_Y, fill);
            tft->drawTriangle(REMOTE_MID_X, REMOTE_UP_APEX_Y,
                              REMOTE_MID_X - REMOTE_UP_HW, REMOTE_UP_BASE_Y,
                              REMOTE_MID_X + REMOTE_UP_HW, REMOTE_UP_BASE_Y, line);
            break;

        case REMOTE_BTN_DOWN:
            tft->fillTriangle(REMOTE_MID_X - REMOTE_DOWN_HW, REMOTE_DOWN_TOP_Y,
                              REMOTE_MID_X + REMOTE_DOWN_HW, REMOTE_DOWN_TOP_Y,
                              REMOTE_MID_X, REMOTE_DOWN_APEX_Y, fill);
            tft->drawTriangle(REMOTE_MID_X - REMOTE_DOWN_HW, REMOTE_DOWN_TOP_Y,
                              REMOTE_MID_X + REMOTE_DOWN_HW, REMOTE_DOWN_TOP_Y,
                              REMOTE_MID_X, REMOTE_DOWN_APEX_Y, line);
            break;

        case REMOTE_BTN_LEFT:
            // Flat edge on the right (facing the centre square), point to the left.
            tft->fillTriangle(REMOTE_MID_X - REMOTE_ARM_GAP, REMOTE_ROW_TOP,
                              REMOTE_MID_X - REMOTE_ARM_GAP, REMOTE_ROW_BOTTOM,
                              REMOTE_MID_X - REMOTE_ARM_GAP - REMOTE_ARM_LEN, midY, fill);
            tft->drawTriangle(REMOTE_MID_X - REMOTE_ARM_GAP, REMOTE_ROW_TOP,
                              REMOTE_MID_X - REMOTE_ARM_GAP, REMOTE_ROW_BOTTOM,
                              REMOTE_MID_X - REMOTE_ARM_GAP - REMOTE_ARM_LEN, midY, line);
            break;

        case REMOTE_BTN_RIGHT:
            tft->fillTriangle(REMOTE_MID_X + REMOTE_ARM_GAP, REMOTE_ROW_TOP,
                              REMOTE_MID_X + REMOTE_ARM_GAP, REMOTE_ROW_BOTTOM,
                              REMOTE_MID_X + REMOTE_ARM_GAP + REMOTE_ARM_LEN, midY, fill);
            tft->drawTriangle(REMOTE_MID_X + REMOTE_ARM_GAP, REMOTE_ROW_TOP,
                              REMOTE_MID_X + REMOTE_ARM_GAP, REMOTE_ROW_BOTTOM,
                              REMOTE_MID_X + REMOTE_ARM_GAP + REMOTE_ARM_LEN, midY, line);
            break;

        case REMOTE_BTN_ENTER:
        {
            int16_t x = REMOTE_MID_X - REMOTE_MID_HW;
            int16_t w = REMOTE_MID_HW * 2;
            int16_t h = REMOTE_ROW_BOTTOM - REMOTE_ROW_TOP;
            tft->fillRect(x, REMOTE_ROW_TOP, w, h, fill);
            tft->drawRect(x, REMOTE_ROW_TOP, w, h, line);
            break;
        }

        case REMOTE_BTN_ESC:
            drawRemoteBox(REMOTE_ESC_X, REMOTE_ESC_Y, REMOTE_ESC_W, REMOTE_ESC_H,
                          "esc", active);
            break;

        case REMOTE_BTN_VOL_UP:
            drawRemoteBox(REMOTE_VOL_LEFT_X, REMOTE_VOL_Y, REMOTE_VOL_W, REMOTE_VOL_H,
                          "VOL UP", active);
            break;

        case REMOTE_BTN_VOL_DOWN:
            drawRemoteBox(REMOTE_VOL_RIGHT_X, REMOTE_VOL_Y, REMOTE_VOL_W, REMOTE_VOL_H,
                          "VOL DOWN", active);
            break;

        default:
            break;
    }
}

// Draws one row of the Keyboard/Remote submenu.
static void drawBleMenuRow(uint8_t option, bool selected)
{
    int16_t y = BLE_MENU_ROW_Y + option * (BLE_MENU_ROW_H + BLE_MENU_ROW_GAP);
    int16_t x = RIGHT_PANEL_X;
    int16_t w = RIGHT_PANEL_W;

    tft->fillRect(x, y, w, BLE_MENU_ROW_H, selected ? COLOR_SELECT_BG : COLOR_BG);
    tft->drawRect(x, y, w, BLE_MENU_ROW_H, selected ? COLOR_ACCENT : COLOR_DIM);

    const char* label = (option == BLE_MENU_KEYBOARD) ? "Keyboard" : "Remote";
    tft->setTextSize(2);
    tft->setTextColor(selected ? COLOR_ACCENT : COLOR_FG);
    tft->setCursor(x + 10, y + 3);
    tft->print(label);

    if (selected)
    {
        // Small solid arrow on the right edge of the highlighted row.
        tft->fillTriangle(x + w - 14, y + 6,
                          x + w - 14, y + BLE_MENU_ROW_H - 6,
                          x + w - 7, y + BLE_MENU_ROW_H / 2, COLOR_ACCENT);
    }
}

// Link screen: shows the advertising/pairing state until a host connects,
// then the two-option submenu (Keyboard / Remote).
void drawBleKeyboard()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "BLE KEYBOARD";
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    tft->setCursor(titleX, CONTENT_TOP + 2);
    tft->print(title);

    bool connected = bleHidIsConnected();

    const char* status;
    uint16_t statusColor;
    if (connected)
    {
        status = "CONNECTED";
        statusColor = COLOR_ACCENT;
    }
    else
    {
        status = "advertising...";
        statusColor = COLOR_FG;
    }

    int16_t statusX = (SCREEN_WIDTH - (int16_t)strlen(status) * 6) / 2;
    if (statusX < 2) statusX = 2;
    tft->setTextColor(statusColor);
    tft->setCursor(statusX, CONTENT_TOP + 14);
    tft->print(status);

    if (connected)
    {
        drawBleMenuRow(BLE_MENU_KEYBOARD, bleMenuCursor == BLE_MENU_KEYBOARD);
        drawBleMenuRow(BLE_MENU_REMOTE,   bleMenuCursor == BLE_MENU_REMOTE);

        const char* desc = (bleMenuCursor == BLE_MENU_KEYBOARD)
                         ? "Pick a key to send"
                         : "Arrows + media keys";
        tft->setTextColor(COLOR_DIM);
        int16_t dx = (SCREEN_WIDTH - (int16_t)strlen(desc) * 6) / 2;
        tft->setCursor(dx, BLE_MENU_ROW_Y + 2 * (BLE_MENU_ROW_H + BLE_MENU_ROW_GAP) + 4);
        tft->print(desc);
    }
    else
    {
        const char* line1 = "Pair from your host with:";
        int16_t x1 = (SCREEN_WIDTH - (int16_t)strlen(line1) * 6) / 2;
        if (x1 < 2) x1 = 2;
        tft->setTextColor(COLOR_FG);
        tft->setCursor(x1, CONTENT_TOP + 28);
        tft->print(line1);

        tft->setTextSize(2);
        tft->setTextColor(COLOR_ACCENT);
        int16_t nameX = (SCREEN_WIDTH - (int16_t)strlen(BLE_DEVICE_NAME) * 12) / 2;
        if (nameX < 2) nameX = 2;
        tft->setCursor(nameX, CONTENT_TOP + 40);
        tft->print(BLE_DEVICE_NAME);
        tft->setTextSize(1);

        const char* line2 = "Settings > Bluetooth";
        int16_t x2 = (SCREEN_WIDTH - (int16_t)strlen(line2) * 6) / 2;
        tft->setTextColor(COLOR_DIM);
        tft->setCursor(x2, CONTENT_TOP + 62);
        tft->print(line2);
    }
}

// Keyboard screen: a scrollable list of every keystroke this app can
// send, one per row. Up/Down move the highlight and Select sends that
// key; the row lights solid for a moment so the send is visible.
void drawBleKeyboardView()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "KEYSTROKES";
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    tft->setCursor(titleX, CONTENT_TOP + 2);
    tft->print(title);

    drawBleKeyboardRows();
}

// Remote screen: D-pad of triangles, centre square (Enter), ESC corner
// button and the two volume boxes, exactly as drawn in the layout sketch.
void drawBleRemote()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    tft->setCursor(4, CONTENT_TOP + 2);
    tft->print("REMOTE");

    for (uint8_t b = REMOTE_BTN_UP; b <= REMOTE_BTN_VOL_DOWN; b++)
    {
        drawRemoteButton(b, false);
    }
    remoteActiveButton = REMOTE_BTN_NONE;
}

// Maps a keypad press to the remote layout, lights that shape, and sends
// the matching keystroke.
static void handleBleRemoteKey(char key)
{
    uint8_t button = REMOTE_BTN_NONE;

    switch (key)
    {
        case '2': button = REMOTE_BTN_UP;       break; // up arrow
        case '6': button = REMOTE_BTN_RIGHT;    break; // right arrow
        case '4': button = REMOTE_BTN_LEFT;     break; // left arrow
        case '8': button = REMOTE_BTN_DOWN;     break; // down arrow
        case '5': button = REMOTE_BTN_ENTER;    break; // enter
        case '0': button = REMOTE_BTN_ESC;      break; // escape
        case '1': button = REMOTE_BTN_VOL_DOWN; break; // volume down
        case '3': button = REMOTE_BTN_VOL_UP;   break; // volume up
        default:  return;                              // any other key does nothing
    }

    // '0' and '5' are also the OS's nav keys, so they already clicked at
    // the top of uiHandleKey(); beep for the rest to keep feedback even.
    if (key != KEY_UP && key != KEY_SELECT)
    {
        audioBeepNav();
    }

    if (remoteActiveButton != REMOTE_BTN_NONE && remoteActiveButton != button)
    {
        drawRemoteButton(remoteActiveButton, false);
    }
    drawRemoteButton(button, true);
    remoteActiveButton = button;

    if (!bleHidIsConnected())
    {
        return;
    }

    switch (button)
    {
        case REMOTE_BTN_UP:       bleHidSendUp();         break;
        case REMOTE_BTN_DOWN:     bleHidSendDown();       break;
        case REMOTE_BTN_LEFT:     bleHidSendLeft();       break;
        case REMOTE_BTN_RIGHT:    bleHidSendRight();      break;
        case REMOTE_BTN_ENTER:    bleHidSendEnter();      break;
        case REMOTE_BTN_ESC:      bleHidSendEscape();     break;
        case REMOTE_BTN_VOL_UP:   bleHidSendVolumeUp();   break;
        case REMOTE_BTN_VOL_DOWN: bleHidSendVolumeDown(); break;
        default: break;
    }
}

// =====================================================================
// Lifecycle
// =====================================================================

void uiInit(Adafruit_ST7735 *displayPtr)
{
    tft = displayPtr;
    currentState = BOOT;
    bootStartTime = millis();
    lastFrameTime = millis();
    lastClockUpdate = millis();
    selectedIndex = 0;
    targetIndex = 0;
    displayIndex = 0.0f;
}

void uiHandleKey(char key)
{
    // Any key press counts as activity and wakes a sleeping panel. The
    // press that wakes it is consumed, so it does not also launch or
    // scroll anything.
    lastActivityMs = millis();
    if (deviceAsleep)
    {
        tft->enableDisplay(true);
        deviceAsleep = false;
        redrawCurrentScreen();
        return;
    }

    // Short audio click on every navigation press, the same way small
    // handheld devices (e.g. M5Stick) give audio feedback. Does nothing
    // if audio feedback is currently disabled in Settings.
    if (key == KEY_UP || key == KEY_DOWN || key == KEY_SELECT || key == KEY_BACK)
    {
        audioBeepNav();
    }

    if (currentState == MENU)
    {
        if (key == KEY_UP)
        {
            if (selectedIndex > 0)
            {
                selectedIndex--;
                startCarouselAnimation();
            }
        }
        else if (key == KEY_DOWN)
        {
            if (selectedIndex < (int8_t)(APP_COUNT - 1))
            {
                selectedIndex++;
                startCarouselAnimation();
            }
        }
        else if (key == KEY_SELECT)
        {
            currentAppIndex = selectedIndex;
            appList[currentAppIndex].launch();

            if (appList[currentAppIndex].launch == launchKeyTest)
            {
                currentState = KEYTEST;
                drawKeyTest();
            }
            else if (appList[currentAppIndex].launch == launchSettings)
            {
                currentState = SETTINGS;
                settingsCursor = 0;
                settingsSub = SETTINGS_SUB_NONE;
                drawSettings();
            }
            else if (appList[currentAppIndex].launch == launchFiles)
            {
                currentState = FILES;
                drawFiles();
            }
            else if (appList[currentAppIndex].launch == launchMiniPiano)
            {
                currentState = MINIPIANO;
                drawMiniPiano();
            }
            else if (appList[currentAppIndex].launch == launchGames)
            {
                currentState = BREAKOUT;
                breakoutInit();
                drawBreakout();
            }
            else if (appList[currentAppIndex].launch == launchPackMon)
            {
                currentState = PACKETMON;
                packetMonInit();
                drawPacketMon();
            }
            else if (appList[currentAppIndex].launch == launchWifiScanner)
            {
                currentState = WIFISCAN;
                wifiCursor = 0;
                wifiScrollTop = 0;
                wifiMarqueeOffset = 0;
                wifiScanInit();
                drawWifiScanner();
            }
            else if (appList[currentAppIndex].launch == launchStockWatch)
            {
                currentState = STOCKWATCH;
                stockCursor = 0;
                stockScrollTop = 0;
                stockWatchPrepareRefresh(); // instant -- shows "Connecting..." below
                drawStockWatch();
                stockWatchRefresh();        // blocking: WiFi connect + HTTPS fetches
                drawStockWatchStatus();
                drawStockWatchList();
            }
            else if (appList[currentAppIndex].launch == launchCalculator)
            {
                currentState = CALCULATOR;
                calculatorInit();
                drawCalculator();
            }
            else if (appList[currentAppIndex].launch == launchMatrix)
            {
                currentState = MATRIX;
                drawMatrix();
            }
            else if (appList[currentAppIndex].launch == launch2048)
            {
                currentState = GAME2048;
                g2048Init();
                drawGame2048();
            }
            else if (appList[currentAppIndex].launch == launchBleKeyboard)
            {
                currentState = BLEKEY;
                bleMenuCursor = BLE_MENU_KEYBOARD;
                drawBleKeyboard();  // "advertising..." before the BLE stack spins up
                bleHidBegin();      // no-op if it is already running
                bleUiConnected = bleHidIsConnected();
                drawBleKeyboard();
            }
            else
            {
                currentState = APP;
                drawApp();
            }
        }
        // KEY_BACK has no effect at the top level of the menu.
    }
    else if (currentState == APP)
    {
        if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
    }
    else if (currentState == KEYTEST)
    {
        if (key == KEY_BACK)
        {
            // KEY_BACK ('D') always exits back to the launcher, so it's
            // the one key on the pad that can't be visualized here --
            // every other key (including the physical '*' key) lights
            // up its matching cell.
            currentState = MENU;
            drawMenu(true);
        }
        else
        {
            handleKeyTestPress(key);
        }
    }
    else if (currentState == SETTINGS)
    {
        if (settingsSub == SETTINGS_SUB_THEME)
        {
            // Choosing a colour: Up/Down cycle through the available
            // themes and each one is applied (and previewed) instantly.
            // Back confirms the choice, saves it to flash and returns to
            // the settings rows.
            uint8_t count = themeCount();
            if (key == KEY_UP)
            {
                uint8_t i = themeGetIndex();
                themeSetIndex(i == 0 ? (uint8_t)(count - 1) : (uint8_t)(i - 1));
                drawThemePicker();
            }
            else if (key == KEY_DOWN)
            {
                themeSetIndex((uint8_t)((themeGetIndex() + 1) % count));
                drawThemePicker();
            }
            else if (key == KEY_BACK || key == KEY_SELECT)
            {
                themeSave();
                settingsSub = SETTINGS_SUB_NONE;
                drawSettings();
            }
        }
        else if (settingsSub == SETTINGS_SUB_SPLASH)
        {
            // Pick a .bmp from the card's root folder, or keep the
            // built-in logo. Select stores the choice; Back leaves it
            // untouched.
            uint8_t total = (uint8_t)(splashFileCount + 1);

            if (key == KEY_UP)
            {
                if (splashCursor > 0)
                {
                    splashCursor--;
                    if (splashCursor < splashScrollTop) splashScrollTop = splashCursor;
                    drawSplashPicker();
                }
            }
            else if (key == KEY_DOWN)
            {
                if (splashCursor + 1 < total)
                {
                    splashCursor++;
                    if (splashCursor >= splashScrollTop + SETTINGS_LIST_VISIBLE)
                        splashScrollTop = (uint8_t)(splashCursor - SETTINGS_LIST_VISIBLE + 1);
                    drawSplashPicker();
                }
            }
            else if (key == KEY_SELECT)
            {
                prefsSetSplashPath(splashCursor == 0 ? "" : splashFiles[splashCursor - 1]);
                settingsSub = SETTINGS_SUB_NONE;
                drawSettings();
            }
            else if (key == KEY_BACK)
            {
                settingsSub = SETTINGS_SUB_NONE;
                drawSettings();
            }
        }
        else if (settingsSub == SETTINGS_SUB_SLEEP)
        {
            // How long the device may sit idle before the panel switches
            // off. Select stores the choice; Back leaves it untouched.
            if (key == KEY_UP)
            {
                if (sleepCursor > 0)
                {
                    sleepCursor--;
                    drawSleepPicker();
                }
            }
            else if (key == KEY_DOWN)
            {
                if (sleepCursor + 1 < prefsSleepCount())
                {
                    sleepCursor++;
                    drawSleepPicker();
                }
            }
            else if (key == KEY_SELECT)
            {
                prefsSetSleepIndex(sleepCursor);
                settingsSub = SETTINGS_SUB_NONE;
                drawSettings();
            }
            else if (key == KEY_BACK)
            {
                settingsSub = SETTINGS_SUB_NONE;
                drawSettings();
            }
        }
        else if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
        else if (key == KEY_UP)
        {
            if (settingsCursor > 0)
            {
                settingsCursor--;
                drawSettings();
            }
        }
        else if (key == KEY_DOWN)
        {
            if (settingsCursor + 1 < SETTINGS_ROW_COUNT)
            {
                settingsCursor++;
                drawSettings();
            }
        }
        else if (key == KEY_SELECT)
        {
            if (settingsCursor == 0)
            {
                audioSetEnabled(!audioIsEnabled());
                drawSettingsRow(0);
            }
            else if (settingsCursor == 1)
            {
                settingsSub = SETTINGS_SUB_THEME;
                drawThemePicker();
            }
            else if (settingsCursor == 2)
            {
                scanSplashFiles();
                splashCursor = 0;
                splashScrollTop = 0;
                settingsSub = SETTINGS_SUB_SPLASH;
                drawSplashPicker();
            }
            else
            {
                sleepCursor = prefsSleepIndex();
                settingsSub = SETTINGS_SUB_SLEEP;
                drawSleepPicker();
            }
        }
    }
    else if (currentState == MINIPIANO)
    {
        if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
        else
        {
            handleMusicKeyPress(key);
        }
    }
    else if (currentState == BREAKOUT)
    {
        if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
        else
        {
            breakoutHandleKey(key);
        }
    }
    else if (currentState == PACKETMON)
    {
        if (key == KEY_BACK)
        {
            packetMonDeinit();
            currentState = MENU;
            drawMenu(true);
        }
        else if (key == KEY_UP)
        {
            packetMonNextChannel();
            drawPacketMonStatus();
        }
        else if (key == KEY_DOWN)
        {
            packetMonPrevChannel();
            drawPacketMonStatus();
        }
        else if (key == KEY_SELECT)
        {
            packetMonSetAutoHop(!packetMonIsAutoHop());
            drawPacketMonStatus();
        }
    }
    else if (currentState == WIFISCAN)
    {
        if (key == KEY_BACK)
        {
            wifiScanDeinit();
            currentState = MENU;
            drawMenu(true);
        }
        else if (key == KEY_UP)
        {
            if (wifiCursor > 0)
            {
                wifiCursor--;
                if (wifiCursor < wifiScrollTop) wifiScrollTop = wifiCursor;
                wifiMarqueeOffset = 0; // restart the ticker so the newly-selected row reads from the start
                drawWifiScannerList();
            }
        }
        else if (key == KEY_DOWN)
        {
            uint8_t count = wifiScanNetworkCount();
            if (count > 0 && wifiCursor < count - 1)
            {
                wifiCursor++;
                if (wifiCursor >= wifiScrollTop + WIFI_VISIBLE_ROWS)
                    wifiScrollTop = wifiCursor - WIFI_VISIBLE_ROWS + 1;
                wifiMarqueeOffset = 0;
                drawWifiScannerList();
            }
        }
        else if (key == KEY_SELECT)
        {
            // Trigger a fresh scan (a no-op while one is already running).
            if (!wifiScanIsScanning())
            {
                wifiCursor = 0;
                wifiScrollTop = 0;
                wifiMarqueeOffset = 0;
                wifiScanStart();
                drawWifiScannerStatus();
                drawWifiScannerList();
            }
        }
    }
    else if (currentState == STOCKWATCH)
    {
        if (key == KEY_BACK)
        {
            stockWatchDeinit();
            currentState = MENU;
            drawMenu(true);
        }
        else if (key == KEY_UP)
        {
            if (stockCursor > 0)
            {
                stockCursor--;
                if (stockCursor < stockScrollTop) stockScrollTop = stockCursor;
                drawStockWatchList();
            }
        }
        else if (key == KEY_DOWN)
        {
            uint8_t count = stockWatchCount();
            if (count > 0 && stockCursor < count - 1)
            {
                stockCursor++;
                if (stockCursor >= stockScrollTop + STOCK_VISIBLE_ROWS)
                    stockScrollTop = stockCursor - STOCK_VISIBLE_ROWS + 1;
                drawStockWatchList();
            }
        }
        else if (key == KEY_SELECT)
        {
            // Manual refresh -- pulls fresh prices for every symbol again.
            stockCursor = 0;
            stockScrollTop = 0;
            stockWatchPrepareRefresh();
            drawStockWatchStatus();
            stockWatchRefresh();
            drawStockWatchStatus();
            drawStockWatchList();
        }
    }
    else if (currentState == CALCULATOR)
    {
        if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
        else
        {
            calculatorHandleKey(key);
        }
    }
    else if (currentState == MATRIX)
    {
        // Any key dismisses it -- this is a screensaver, not an app with
        // its own controls.
        currentState = MENU;
        drawMenu(true);
    }
    else if (currentState == GAME2048)
    {
        if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
        else
        {
            g2048HandleKey(key);
        }
    }
    else if (currentState == BLEKEY)
    {
        if (key == KEY_BACK)
        {
            // The BLE link is deliberately left up: leaving the app should
            // not drop the pairing, so the host stays connected.
            currentState = MENU;
            drawMenu(true);
        }
        else if (!bleHidIsConnected())
        {
            // Nothing to choose from until a host connects.
        }
        else if (key == KEY_UP)
        {
            if (bleMenuCursor > BLE_MENU_KEYBOARD)
            {
                bleMenuCursor--;
                drawBleKeyboard();
            }
        }
        else if (key == KEY_DOWN)
        {
            if (bleMenuCursor < BLE_MENU_REMOTE)
            {
                bleMenuCursor++;
                drawBleKeyboard();
            }
        }
        else if (key == KEY_SELECT)
        {
            if (bleMenuCursor == BLE_MENU_KEYBOARD)
            {
                currentState = BLEKB;
                bleKbFlashRow = 0xFF; // no stale flash from last time
                if (bleKbCursor < bleKbScrollTop)
                {
                    bleKbScrollTop = bleKbCursor;
                }
                drawBleKeyboardView();
            }
            else
            {
                currentState = BLEREMOTE;
                drawBleRemote();
            }
        }
    }
    else if (currentState == BLEKB)
    {
        if (key == KEY_BACK)
        {
            currentState = BLEKEY;
            drawBleKeyboard();
        }
        else if (key == KEY_UP)
        {
            bleKbMoveCursor(-1);
        }
        else if (key == KEY_DOWN)
        {
            bleKbMoveCursor(1);
        }
        else if (key == KEY_SELECT)
        {
            sendBleKeystroke(bleKbCursor);
        }
        // Anything else has nothing to send: the list is the only way to
        // pick a keystroke, which keeps this screen to one key per row.
    }
    else if (currentState == BLEREMOTE)
    {
        if (key == KEY_BACK)
        {
            currentState = BLEKEY;
            bleMenuCursor = BLE_MENU_REMOTE;
            drawBleKeyboard();
        }
        else
        {
            handleBleRemoteKey(key);
        }
    }
    else if (currentState == FILES)
    {
        if (!sdReady)
        {
            // Nothing to browse -- only BACK does anything.
            if (key == KEY_BACK)
            {
                currentState = MENU;
                drawMenu(true);
            }
            return;
        }

        uint8_t displayedCount = fileEntryCount + (hasParentEntry ? 1 : 0);

        if (key == KEY_BACK)
        {
            if (filesGoUp())
            {
                drawFiles(); // fresh directory: rebuild the whole screen
            }
            else
            {
                currentState = MENU;
                drawMenu(true);
            }
        }
        else if (key == KEY_UP)
        {
            if (filesCursor > 0)
            {
                filesCursor--;
                if (filesCursor < filesScrollTop)
                {
                    filesScrollTop = filesCursor;
                }
                refreshFilesRows();
            }
        }
        else if (key == KEY_DOWN)
        {
            if (displayedCount > 0 && filesCursor < displayedCount - 1)
            {
                filesCursor++;
                if (filesCursor >= filesScrollTop + FILES_VISIBLE_ROWS)
                {
                    filesScrollTop = filesCursor - FILES_VISIBLE_ROWS + 1;
                }
                refreshFilesRows();
            }
        }
        else if (key == KEY_SELECT)
        {
            const char* name;
            bool isDir;
            uint32_t size;
            if (filesGetDisplayEntry(filesCursor, &name, &isDir, &size))
            {
                if (isDir)
                {
                    if (strcmp(name, "..") == 0)
                    {
                        filesGoUp();
                    }
                    else
                    {
                        uint8_t realIdx = filesCursor - (hasParentEntry ? 1 : 0);
                        filesEnterDirectory(realIdx);
                    }
                    drawFiles(); // entered/left a directory: fresh listing
                }
                else
                {
                    uint8_t realIdx = filesCursor - (hasParentEntry ? 1 : 0);
                    openFilePreview(realIdx);
                }
            }
        }
    }
    else if (currentState == FILEVIEW)
    {
        if (key == KEY_BACK)
        {
            currentState = FILES;
            drawFilesReturn(); // same directory, cursor/scroll preserved
        }
        else if (key == KEY_UP)
        {
            if (filePreviewLineOffset > 0)
            {
                filePreviewLineOffset--;
                drawFileView();
            }
        }
        else if (key == KEY_DOWN)
        {
            if (filePreviewLineOffset + FILEVIEW_VISIBLE_LINES < fileViewLineCount)
            {
                filePreviewLineOffset++;
                drawFileView();
            }
        }
    }
    else if (currentState == BMPVIEW)
    {
        if (key == KEY_BACK)
        {
            // The viewer's row buffer is allocated and freed inside
            // drawBmpView(), so leaving is just a state change.
            currentState = FILES;
            drawFilesReturn();
        }
    }
}

void uiTick()
{
    unsigned long now = millis();

    // A sleeping panel does nothing at all until a key wakes it -- no
    // animation, no SD/network work, no sound.
    if (deviceAsleep)
    {
        return;
    }

    // Advances the boot jingle's note sequencer. Silent and cheap once the
    // melody has finished or the splash has ended.
    audioBootTuneService();

    if (currentState == BOOT)
    {
        if (now - bootStartTime >= BOOT_SPLASH_DURATION_MS)
        {
            audioBootTuneStop();
            currentState = MENU;
            drawMenu(true);
        }
        return;
    }

    // Auto sleep: once the timeout chosen in Settings has passed without
    // a key press, switch the display off. Any key press wakes it.
    uint16_t sleepSecs = prefsSleepSeconds(prefsSleepIndex());
    if (sleepSecs > 0 && (now - lastActivityMs) >= (unsigned long)sleepSecs * 1000UL)
    {
        audioStop();
        tft->enableDisplay(false);
        deviceAsleep = true;
        return;
    }

    // Cap the update rate to roughly 30 FPS for smooth, low-overhead animation.
    if (now - lastFrameTime < FRAME_INTERVAL_MS)
    {
        return;
    }
    lastFrameTime = now;

    if (currentState == MENU)
    {
        if (animateScroll())
        {
            drawCarousel(false); // partial redraw: right panel only
        }

        // Placeholder clock: ticks once a second, redraws only the
        // status bar strip rather than the whole screen.
        if (now - lastClockUpdate >= 1000)
        {
            lastClockUpdate = now;
            secondsSinceBoot++;
            drawStatusBar();
        }
    }
    else if (currentState == BREAKOUT)
    {
        if (now - lastBreakoutUpdate >= 50)
        {
            lastBreakoutUpdate = now;
            breakoutTick();
        }
    }
    else if (currentState == PACKETMON)
    {
        // packetMonTick() self-throttles to one history bin every
        // PACKETMON_BIN_INTERVAL_MS and only returns true then, so this
        // redraws no more often than the graph actually has new data.
        if (packetMonTick())
        {
            drawPacketMonStatus();
            drawPacketMonGraph();
        }
    }
    else if (currentState == WIFISCAN)
    {
        // A scan just finished -- refresh the status line and results.
        if (wifiScanTick())
        {
            drawWifiScannerStatus();
            drawWifiScannerList();
        }

        // Advance the shared marquee ticker and redraw the list so any
        // SSID too long to fit its row keeps scrolling smoothly. This is
        // already capped to ~30 FPS by the FRAME_INTERVAL_MS gate above,
        // so we only need our own slower WIFI_MARQUEE_STEP_MS throttle
        // on top of that.
        static unsigned long lastWifiMarquee = 0;
        if (now - lastWifiMarquee >= WIFI_MARQUEE_STEP_MS)
        {
            lastWifiMarquee = now;
            wifiMarqueeOffset++;
            drawWifiScannerList();
        }
    }
    else if (currentState == BLEKEY || currentState == BLEKB || currentState == BLEREMOTE)
    {
        // Let the "keystroke sent" flash fade back to the normal row.
        if (currentState == BLEKB && bleKbFlashRow != 0xFF &&
            (long)(now - bleKbFlashUntil) >= 0)
        {
            uint8_t sentRow = bleKbFlashRow;
            bleKbFlashRow = 0xFF;
            drawBleKeyboardRow(sentRow);
        }

        // Follow the link: reveal the submenu the moment a host pairs,
        // and fall back to the link screen if the host walks away.
        bool connected = bleHidIsConnected();
        if (connected != bleUiConnected)
        {
            bleUiConnected = connected;
            if (connected)
            {
                drawBleKeyboard(); // now shows Keyboard / Remote
            }
            else
            {
                currentState = BLEKEY;
                bleMenuCursor = BLE_MENU_KEYBOARD;
                drawBleKeyboard();
            }
        }
    }
    else if (currentState == MATRIX)
    {
        static unsigned long lastMatrixTick = 0;
        if (now - lastMatrixTick >= MATRIX_TICK_MS)
        {
            lastMatrixTick = now;
            matrixTick();
        }
    }
}

// =====================================================================
// Boot screen
// =====================================================================

void drawBoot()
{
    // Full-screen boot splash. When a picture has been chosen in
    // Settings > Splash and it is still on the card, that .bmp fills the
    // whole panel (scaled to fit). Otherwise -- no card, no picture
    // chosen, or the file is gone/damaged -- the built-in logo from
    // Icons.h is shown instead, so boot always has a splash.
    //
    // Whichever is drawn stays up for BOOT_SPLASH_DURATION_MS (see
    // Config.h) before uiTick() switches over to MENU.
    bool drewSplash = false;
    const char* splash = prefsSplashPath();

    if (splash[0] != '\0' && sdReady)
    {
        drewSplash = bmpShowFullScreen(splash);
    }

    if (!drewSplash)
    {
        drawBitmapIcon(0, 0, bootLogo, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    lastActivityMs = millis();

    // Power-on jingle, started exactly when the splash appears and
    // stopped by uiTick() when the splash ends (or when it finishes on
    // its own, whichever comes first).
    audioBootTuneStart();
}

// =====================================================================
// Matrix -- full-screen digital-rain screensaver. Like the boot splash,
// it owns the whole panel with no status bar. Each column falls
// independently at its own (randomized) speed and trail length so they
// never sync up, and any key press exits back to the menu.
// =====================================================================

void drawMatrix()
{
    tft->fillScreen(COLOR_BG);
    tft->setTextSize(1); // guarantee 6x8 glyph cells regardless of what a previous screen left set

    for (uint8_t c = 0; c < MATRIX_COLS; c++)
    {
        matrixCols[c].headRow      = -(int16_t)random(1, MATRIX_ROWS * 2);
        matrixCols[c].trailLen     = 4 + random(6); // 4-9 rows
        matrixCols[c].speedDiv     = 1 + random(3); // 1-3
        matrixCols[c].speedCounter = 0;
    }
}

static void matrixTick()
{
    for (uint8_t c = 0; c < MATRIX_COLS; c++)
    {
        MatrixColumn& col = matrixCols[c];

        col.speedCounter++;
        if (col.speedCounter < col.speedDiv) continue;
        col.speedCounter = 0;

        int16_t x = c * 6;

        // Erase the row now falling off the tail.
        int16_t tailRow = col.headRow - col.trailLen;
        if (tailRow >= 0 && tailRow < MATRIX_ROWS)
        {
            tft->fillRect(x, tailRow * 8, 6, 8, COLOR_BG);
        }

        // Downgrade what was the bright head last tick to the dimmer
        // trail color (with a freshly-picked character -- a bit of
        // flicker in the trail reads as more "alive" than a static one).
        int16_t prevRow = col.headRow - 1;
        if (prevRow >= 0 && prevRow < MATRIX_ROWS)
        {
            tft->fillRect(x, prevRow * 8, 6, 8, COLOR_BG);
            tft->setTextColor(COLOR_FG);
            tft->setCursor(x, prevRow * 8);
            tft->print(matrixChars[random(sizeof(matrixChars) - 1)]);
        }

        // Draw the new bright head.
        if (col.headRow >= 0 && col.headRow < MATRIX_ROWS)
        {
            tft->fillRect(x, col.headRow * 8, 6, 8, COLOR_BG);
            tft->setTextColor(COLOR_ACCENT);
            tft->setCursor(x, col.headRow * 8);
            tft->print(matrixChars[random(sizeof(matrixChars) - 1)]);
        }

        col.headRow++;

        // Once even the tail has scrolled past the bottom, restart this
        // column from a random negative delay so columns stay out of sync.
        if (col.headRow - col.trailLen >= MATRIX_ROWS)
        {
            col.headRow  = -(int16_t)random(1, MATRIX_ROWS * 2);
            col.trailLen = 4 + random(6);
            col.speedDiv = 1 + random(3);
        }
    }
}

// =====================================================================
// 2048 -- slide-and-merge number tile game.
//
// Controls: 2/8/4/6 slide the board up/down/left/right (the same
// direction mapping used elsewhere), 5 starts a new game once the
// current one has ended. * always exits.
// =====================================================================

static uint8_t g2048TileLevel(uint32_t value)
{
    uint8_t level = 0;
    uint32_t v = value;
    while (v > 2)
    {
        v >>= 1;
        level++;
    }
    if (level >= G2048_COLOR_COUNT) level = (uint8_t)(G2048_COLOR_COUNT - 1);
    return level;
}

static void g2048SpawnTile()
{
    uint8_t emptyR[G2048_SIZE * G2048_SIZE];
    uint8_t emptyC[G2048_SIZE * G2048_SIZE];
    uint8_t emptyCount = 0;

    for (uint8_t r = 0; r < G2048_SIZE; r++)
    {
        for (uint8_t c = 0; c < G2048_SIZE; c++)
        {
            if (g2048Grid[r][c] == 0)
            {
                emptyR[emptyCount] = r;
                emptyC[emptyCount] = c;
                emptyCount++;
            }
        }
    }

    if (emptyCount == 0) return; // board is full -- nothing to spawn

    uint8_t pick = (uint8_t)random(emptyCount);
    uint32_t value = (random(10) < 9) ? 2 : 4; // classic 2048 odds: 90% a 2, 10% a 4
    g2048Grid[emptyR[pick]][emptyC[pick]] = value;
}

static bool g2048HasMoves()
{
    for (uint8_t r = 0; r < G2048_SIZE; r++)
    {
        for (uint8_t c = 0; c < G2048_SIZE; c++)
        {
            if (g2048Grid[r][c] == 0) return true;
            if (c + 1 < G2048_SIZE && g2048Grid[r][c] == g2048Grid[r][c + 1]) return true;
            if (r + 1 < G2048_SIZE && g2048Grid[r][c] == g2048Grid[r + 1][c]) return true;
        }
    }
    return false;
}

static void g2048Init()
{
    for (uint8_t r = 0; r < G2048_SIZE; r++)
    {
        for (uint8_t c = 0; c < G2048_SIZE; c++)
        {
            g2048Grid[r][c] = 0;
        }
    }

    g2048Score = 0;
    g2048GameOver = false;

    g2048SpawnTile();
    g2048SpawnTile();
}

// Slides len values toward index 0 (the caller is responsible for
// presenting the line in "direction of movement = toward index 0"
// order, reversing first if needed), merging equal adjacent values
// once each -- a tile that was just created by a merge can't merge
// again in the same move, which is standard 2048 behavior. Returns
// true if the line's contents actually changed.
static bool g2048SlideLine(uint32_t* line, uint8_t len, uint32_t* scoreDelta)
{
    uint32_t original[G2048_SIZE];
    for (uint8_t i = 0; i < len; i++) original[i] = line[i];

    uint32_t compacted[G2048_SIZE] = {0};
    uint8_t writeIdx = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        if (line[i] != 0)
        {
            compacted[writeIdx++] = line[i];
        }
    }

    for (uint8_t i = 0; i + 1 < len; i++)
    {
        if (compacted[i] != 0 && compacted[i] == compacted[i + 1])
        {
            compacted[i] *= 2;
            *scoreDelta += compacted[i];
            compacted[i + 1] = 0;
            i++; // the tile we just merged into is now off-limits this move
        }
    }

    uint32_t result[G2048_SIZE] = {0};
    writeIdx = 0;
    for (uint8_t i = 0; i < len; i++)
    {
        if (compacted[i] != 0)
        {
            result[writeIdx++] = compacted[i];
        }
    }

    bool changed = false;
    for (uint8_t i = 0; i < len; i++)
    {
        if (result[i] != original[i]) changed = true;
        line[i] = result[i];
    }
    return changed;
}

// direction: 'U'/'D'/'L'/'R'. Extracts each row or column in the order
// that direction moves toward (reversing when necessary), slides it,
// then writes it back in the same order it was read.
static bool g2048Move(char direction)
{
    bool anyChanged = false;
    uint32_t scoreDelta = 0;

    if (direction == 'L' || direction == 'R')
    {
        for (uint8_t r = 0; r < G2048_SIZE; r++)
        {
            uint32_t line[G2048_SIZE];
            for (uint8_t c = 0; c < G2048_SIZE; c++)
            {
                uint8_t srcC = (direction == 'L') ? c : (uint8_t)(G2048_SIZE - 1 - c);
                line[c] = g2048Grid[r][srcC];
            }

            if (g2048SlideLine(line, G2048_SIZE, &scoreDelta)) anyChanged = true;

            for (uint8_t c = 0; c < G2048_SIZE; c++)
            {
                uint8_t dstC = (direction == 'L') ? c : (uint8_t)(G2048_SIZE - 1 - c);
                g2048Grid[r][dstC] = line[c];
            }
        }
    }
    else // 'U' or 'D'
    {
        for (uint8_t c = 0; c < G2048_SIZE; c++)
        {
            uint32_t line[G2048_SIZE];
            for (uint8_t r = 0; r < G2048_SIZE; r++)
            {
                uint8_t srcR = (direction == 'U') ? r : (uint8_t)(G2048_SIZE - 1 - r);
                line[r] = g2048Grid[srcR][c];
            }

            if (g2048SlideLine(line, G2048_SIZE, &scoreDelta)) anyChanged = true;

            for (uint8_t r = 0; r < G2048_SIZE; r++)
            {
                uint8_t dstR = (direction == 'U') ? r : (uint8_t)(G2048_SIZE - 1 - r);
                g2048Grid[dstR][c] = line[r];
            }
        }
    }

    g2048Score += scoreDelta;
    return anyChanged;
}

static void g2048HandleKey(char key)
{
    if (g2048GameOver)
    {
        if (key == KEY_SELECT)
        {
            g2048Init();
            drawGame2048();
        }
        return;
    }

    char direction = 0;
    if (key == '2') direction = 'U';
    else if (key == '8') direction = 'D';
    else if (key == '4') direction = 'L';
    else if (key == '6') direction = 'R';
    else return; // ignore any other key

    if (g2048Move(direction))
    {
        g2048SpawnTile();
        if (!g2048HasMoves())
        {
            g2048GameOver = true;
        }
    }

    drawGame2048();
}

void drawGame2048()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    char header[28];
    if (g2048GameOver)
    {
        snprintf(header, sizeof(header), "GAME OVER  Score:%lu", (unsigned long)g2048Score);
        tft->setTextColor(COLOR_ACCENT);
    }
    else
    {
        snprintf(header, sizeof(header), "2048   Score:%lu", (unsigned long)g2048Score);
        tft->setTextColor(COLOR_FG);
    }
    int16_t hx = (SCREEN_WIDTH - (int16_t)strlen(header) * 6) / 2;
    if (hx < 2) hx = 2;
    tft->setCursor(hx, CONTENT_TOP + 2);
    tft->print(header);

    for (uint8_t r = 0; r < G2048_SIZE; r++)
    {
        for (uint8_t c = 0; c < G2048_SIZE; c++)
        {
            int16_t cx = G2048_GRID_X + c * (G2048_CELL + G2048_GAP);
            int16_t cy = G2048_GRID_Y + r * (G2048_CELL + G2048_GAP);
            uint32_t value = g2048Grid[r][c];

            if (value == 0)
            {
                tft->fillRect(cx, cy, G2048_CELL, G2048_CELL, COLOR_ACCENT_DIM);
                tft->drawRect(cx, cy, G2048_CELL, G2048_CELL, COLOR_DIM);
                continue;
            }

            uint8_t level = g2048TileLevel(value);
            tft->fillRect(cx, cy, G2048_CELL, G2048_CELL, g2048TileColors[level]);
            tft->drawRect(cx, cy, G2048_CELL, G2048_CELL, COLOR_BG);

            char label[8];
            snprintf(label, sizeof(label), "%lu", (unsigned long)value);
            uint8_t textLen = (uint8_t)strlen(label);
            int16_t tx = cx + (G2048_CELL - textLen * 6) / 2;
            if (tx < cx) tx = cx; // guard: a number too wide for the cell just left-aligns instead of drawing off it
            int16_t ty = cy + (G2048_CELL - 8) / 2;

            // Dark text on the light levels-0/1 tiles, white on every
            // more saturated color above that.
            tft->setTextColor(level <= 1 ? COLOR_BG : 0xFFFF);
            tft->setCursor(tx, ty);
            tft->print(label);
        }
    }
}

// =====================================================================
// Status bar
// =====================================================================

void drawStatusBar()
{
    tft->fillRect(0, 0, SCREEN_WIDTH, STATUSBAR_HEIGHT, COLOR_STATUSBAR);

    // Placeholder clock (mm:ss since boot -- replace with an RTC value later).
    uint16_t mins = (secondsSinceBoot / 60) % 100;
    uint16_t secs = secondsSinceBoot % 60;
    char clockBuf[6];
    snprintf(clockBuf, sizeof(clockBuf), "%02u:%02u", mins, secs);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    tft->setCursor(4, 3);
    tft->print(clockBuf);

    // Hollow-box status indicators: filled green when active, dim
    // outline when off -- sharp rectangles to match the rest of the
    // theme, not rounded pills.
    uint8_t sz = STATUSBAR_ICON_SIZE - 2;
    int16_t iy = (STATUSBAR_HEIGHT - sz) / 2;
    int16_t x = SCREEN_WIDTH - 4 - sz;

    // Battery (no sensor -- always dim hollow)
    tft->drawRect(x, iy, sz, sz, COLOR_DIM);
    x -= (sz + 4);

    // WiFi (filled while the WiFi Scanner app has the radio open)
    uint16_t wifiColor = wifiScanIsActive() ? COLOR_ACCENT : COLOR_DIM;
    tft->drawRect(x, iy, sz, sz, wifiColor);
    if (wifiScanIsActive())
    {
        tft->fillRect(x + 1, iy + 1, sz - 2, sz - 2, wifiColor);
    }
    x -= (sz + 4);

    // SD card (filled when card is present and ready)
    uint16_t sdColor = sdReady ? COLOR_ACCENT : COLOR_DIM;
    tft->drawRect(x, iy, sz, sz, sdColor);
    if (sdReady)
    {
        tft->fillRect(x + 1, iy + 1, sz - 2, sz - 2, sdColor);
    }
}

// =====================================================================
// Menu (launcher) screen
// =====================================================================

void drawMenu(bool fullRedraw)
{
    if (fullRedraw)
    {
        clearContent();
        drawStatusBar();
    }

    drawCarousel(fullRedraw);
}
// --- The launcher is just this: a centered, animated vertical list of
// app names. No icons, no side panel -- text-only retro terminal menu.
void drawCarousel(bool fullRedraw)
{
    (void)fullRedraw; // the carousel always repaints just its own panel

    // Partial redraw: clear only the menu panel, never the full screen.
    tft->fillRect(RIGHT_PANEL_X, CONTENT_TOP, RIGHT_PANEL_W, CONTENT_HEIGHT, COLOR_BG);

    // Fixed highlight box behind the centered (selected) row. It stays
    // put while the list slides underneath it.
    drawSelection();

    float centerY = CONTENT_TOP + CONTENT_HEIGHT / 2.0f;

    // No scrollbar any more, so the names get the full panel width --
    // only a small margin is kept at the right edge. Carousel text is
    // drawn at size 2 (12px/char).
    int16_t textAreaX = RIGHT_PANEL_X + 4;
    int16_t textAreaRight = SCREEN_WIDTH - 4;
    uint8_t maxChars = (uint8_t)((textAreaRight - textAreaX) / 12);
    if (maxChars < 1) maxChars = 1;
    if (maxChars > 12) maxChars = 12;

    for (uint8_t i = 0; i < APP_COUNT; i++)
    {
        float delta = (float)i - displayIndex;

        // Skip items far enough away that they can't be visible; keeps
        // the loop cheap even with a long app list.
        if (fabsf(delta) > (CAROUSEL_VISIBLE_ITEMS / 2.0f) + 1.0f)
        {
            continue;
        }

        float y = centerY + delta * CAROUSEL_ITEM_H;

        // Hard clip at the content/status-bar boundary: a row that would
        // overlap the status bar is skipped entirely rather than drawn
        // and left to smear, since the panel clear above only touches
        // the content area, never the status bar itself.
        if (y - 8 < CONTENT_TOP || y > SCREEN_HEIGHT - 2)
        {
            continue;
        }

        float dist = fabsf(delta);
        uint16_t color;
        if (dist < 0.5f)      color = COLOR_ACCENT;
        else if (dist < 1.5f) color = COLOR_FG;
        else                  color = COLOR_DIM;

        // Truncate the name so it never runs past the scrollbar. A
        // trailing '.' marks a truncated (rather than short) name.
        const char* name = appList[i].name;
        size_t len = strlen(name);
        char label[16];
        if (len > maxChars)
        {
            size_t copyLen = (maxChars > 1) ? (size_t)(maxChars - 1) : 1;
            if (copyLen > 14) copyLen = 14;
            memcpy(label, name, copyLen);
            label[copyLen] = '.';
            label[copyLen + 1] = '\0';
        }
        else
        {
            size_t copyLen = len > 15 ? 15 : len;
            memcpy(label, name, copyLen);
            label[copyLen] = '\0';
        }

        tft->setTextSize(2);
        tft->setTextColor(color);
        tft->setCursor(textAreaX, (int16_t)y - 8);
        tft->print(label);
    }

    // Redraw the status bar on top last. This guarantees any row that
    // slides close to the boundary is hidden behind it rather than
    // showing as a stray blob -- belt-and-braces alongside the clip
    // check above.
    drawStatusBar();
}

// Fixed outline behind the centered/selected row in the list. Outline
// only -- the inside stays clear so the highlighted name sits on plain
// background, like a selection box on an old terminal.
void drawSelection()
{
    float centerY = CONTENT_TOP + CONTENT_HEIGHT / 2.0f;

    int16_t boxX = RIGHT_PANEL_X;
    int16_t boxW = RIGHT_PANEL_W;
    int16_t boxH = CAROUSEL_ITEM_H - 2;
    int16_t boxY = (int16_t)(centerY - boxH / 2.0f);

    tft->drawRect(boxX, boxY, boxW, boxH, COLOR_ACCENT_DIM);
}

// =====================================================================
// Animation
// =====================================================================

static void startCarouselAnimation()
{
    targetIndex = selectedIndex;
    animStartValue = displayIndex;
    animStartTime = millis();
    animating = true;
}

// Advances displayIndex toward targetIndex using an ease-out curve.
// Returns true if the carousel needs to be repainted this frame.
bool animateScroll()
{
    if (!animating)
    {
        return false;
    }

    unsigned long now = millis();
    float t = (float)(now - animStartTime) / (float)ANIM_DURATION_MS;
    if (t >= 1.0f)
    {
        t = 1.0f;
        animating = false;
    }

    float eased = 1.0f - powf(1.0f - t, 3.0f); // ease-out cubic
    float newValue = animStartValue + ((float)targetIndex - animStartValue) * eased;

    bool changed = fabsf(newValue - displayIndex) > 0.001f;
    displayIndex = newValue;

    // Always repaint on the final frame so the carousel lands exactly
    // on the target position with no rounding drift.
    return changed || !animating;
}

// =====================================================================
// App (placeholder) screen
// =====================================================================

void drawApp()
{
    clearContent();
    drawStatusBar();

    const App &app = appList[currentAppIndex];

    // About screen: show device info instead of generic placeholder
    if (app.launch == launchAbout)
    {
        tft->drawRect(4, CONTENT_TOP + 4, SCREEN_WIDTH - 8, CONTENT_HEIGHT - 20, COLOR_ACCENT_DIM);

        tft->setTextSize(2);
        tft->setTextColor(COLOR_ACCENT);
        const char* title = "GHOUL OS X";
        int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 12) / 2;
        if (tx < 2) tx = 2;
        tft->setCursor(tx, CONTENT_TOP + 12);
        tft->print(title);

        tft->setTextSize(1);
        tft->setTextColor(COLOR_FG);
        int16_t y = CONTENT_TOP + 36;
        tft->setCursor(10, y);  tft->print("Platform: ESP32");       y += 10;
        tft->setCursor(10, y);  tft->print("Display: ST7735 160x128"); y += 10;
        tft->setCursor(10, y);  tft->print("Input: 4x4 Matrix Pad"); y += 10;

        char heapBuf[28];
        snprintf(heapBuf, sizeof(heapBuf), "Free heap: %lu B",
                 (unsigned long)ESP.getFreeHeap());
        tft->setCursor(10, y);  tft->print(heapBuf);                 y += 10;

        uint16_t umins = (secondsSinceBoot / 60) % 100;
        uint16_t usecs = secondsSinceBoot % 60;
        char upBuf[20];
        snprintf(upBuf, sizeof(upBuf), "Uptime: %02u:%02u", umins, usecs);
        tft->setCursor(10, y);  tft->print(upBuf);
        return;
    }

    // Generic placeholder screen for every other app -- a bordered
    // retro panel with the app name as a big centered header and a
    // "*** COMING SOON ***" banner underneath, text-only (no icon).
    int16_t boxY = CONTENT_TOP + 10;
    int16_t boxH = CONTENT_HEIGHT - 30;
    tft->drawRect(8, boxY, SCREEN_WIDTH - 16, boxH, COLOR_ACCENT_DIM);

    tft->setTextSize(2);
    tft->setTextColor(COLOR_ACCENT);
    int16_t titleY = boxY + (boxH / 2) - 20;
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(app.name) * 12) / 2;
    if (titleX < 2) titleX = 2;
    tft->setCursor(titleX, titleY);
    tft->print(app.name);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* caption = "*** COMING SOON ***";
    int16_t capX = (SCREEN_WIDTH - (int16_t)strlen(caption) * 6) / 2;
    tft->setCursor(capX, titleY + 26);
    tft->print(caption);
}

// =====================================================================
// Key Test app -- live keypad press visualizer
// =====================================================================

// Draws (or redraws) a single cell of the on-screen keypad grid.
// Highlighted = the button currently held down; called for one cell at
// a time so pressing a key never requires clearing the whole screen.
static void drawKeyTestCell(uint8_t row, uint8_t col, bool highlighted)
{
    int16_t x = KEYTEST_GRID_X + col * KEYTEST_CELL_W;
    int16_t y = KEYTEST_GRID_Y + row * KEYTEST_CELL_H;
    int16_t w = KEYTEST_CELL_W - 4; // small gap between cells
    int16_t h = KEYTEST_CELL_H - 4;

    uint16_t fillColor   = highlighted ? COLOR_ACCENT : COLOR_BG;
    uint16_t borderColor = highlighted ? COLOR_ACCENT : COLOR_DIM;
    uint16_t textColor   = highlighted ? COLOR_BG : COLOR_FG;

    tft->fillRect(x, y, w, h, fillColor);
    tft->drawRect(x, y, w, h, borderColor);

    char label[2] = { keyTestLayout[row][col], '\0' };
    tft->setTextSize(1);
    tft->setTextColor(textColor);
    tft->setCursor(x + (w - 6) / 2, y + (h - 8) / 2);
    tft->print(label);
}

// Finds and un-highlights whatever cell was previously lit, then
// highlights the newly pressed key's cell -- both as partial (single
// cell) redraws, so key visualization stays flicker-free.
static void handleKeyTestPress(char key)
{
    if (keyTestLastKey != 0)
    {
        for (uint8_t r = 0; r < KEYTEST_ROWS; r++)
        {
            for (uint8_t c = 0; c < KEYTEST_COLS; c++)
            {
                if (keyTestLayout[r][c] == keyTestLastKey)
                {
                    drawKeyTestCell(r, c, false);
                }
            }
        }
    }

    for (uint8_t r = 0; r < KEYTEST_ROWS; r++)
    {
        for (uint8_t c = 0; c < KEYTEST_COLS; c++)
        {
            if (keyTestLayout[r][c] == key)
            {
                drawKeyTestCell(r, c, true);
                keyTestLastKey = key;
                return;
            }
        }
    }
}

void drawKeyTest()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "Key Test";
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    tft->setCursor(titleX, CONTENT_TOP + 4);
    tft->print(title);

    for (uint8_t r = 0; r < KEYTEST_ROWS; r++)
    {
        for (uint8_t c = 0; c < KEYTEST_COLS; c++)
        {
            drawKeyTestCell(r, c, false);
        }
    }
    keyTestLastKey = 0;
}

// =====================================================================
// MiniPiano app -- 9-key mini piano
// =====================================================================

// Draws (or redraws) a single cell of the 3x3 piano grid. Cell (row,col)
// always maps to noteIndex = row*3 + col, i.e. keys '1'-'9' in reading
// order -- the same physical layout as those keys on the keypad.
static void drawMusicCell(uint8_t row, uint8_t col, bool highlighted)
{
    uint8_t noteIndex = row * MUSIC_GRID_COLS + col;

    int16_t x = MUSIC_GRID_X + col * MUSIC_CELL_W;
    int16_t y = MUSIC_GRID_Y + row * MUSIC_CELL_H;
    int16_t w = MUSIC_CELL_W - 4; // small gap between cells
    int16_t h = MUSIC_CELL_H - 4;

    uint16_t fillColor   = highlighted ? COLOR_ACCENT : COLOR_BG;
    uint16_t borderColor = highlighted ? COLOR_ACCENT : COLOR_DIM;
    uint16_t textColor   = highlighted ? COLOR_BG : COLOR_FG;

    tft->fillRect(x, y, w, h, fillColor);
    tft->drawRect(x, y, w, h, borderColor);

    // Key number, upper area of the cell.
    char keyLabel[2] = { (char)('1' + noteIndex), '\0' };
    tft->setTextSize(1);
    tft->setTextColor(textColor);
    tft->setCursor(x + (w - 6) / 2, y + 4);
    tft->print(keyLabel);

    // Note name, lower area of the cell.
    tft->setTextColor(highlighted ? COLOR_BG : COLOR_DIM);
    const char* noteName = audioNoteName(noteIndex);
    int16_t noteX = x + (w - (int16_t)strlen(noteName) * 6) / 2;
    tft->setCursor(noteX, y + h - 11);
    tft->print(noteName);
}

// Plays the tone for a pressed '1'-'9' key and highlights its cell,
// un-highlighting whichever cell was lit before -- same "stays lit
// until the next press" pattern as Key Test. Silently ignores any key
// that isn't '1'-'9' (including the Up/Select/Down/Back letters).
static void handleMusicKeyPress(char key)
{
    if (key < '1' || key > '9')
    {
        return;
    }
    uint8_t noteIndex = key - '1';

    if (musicLastNote != 255)
    {
        drawMusicCell(musicLastNote / MUSIC_GRID_COLS, musicLastNote % MUSIC_GRID_COLS, false);
    }
    drawMusicCell(noteIndex / MUSIC_GRID_COLS, noteIndex % MUSIC_GRID_COLS, true);
    musicLastNote = noteIndex;

    audioPlayNote(noteIndex);
}

void drawMiniPiano()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "MiniPiano";
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (titleX < 2) titleX = 2;
    tft->setCursor(titleX, CONTENT_TOP + 2);
    tft->print(title);

    for (uint8_t r = 0; r < MUSIC_GRID_ROWS; r++)
    {
        for (uint8_t c = 0; c < MUSIC_GRID_COLS; c++)
        {
            drawMusicCell(r, c, false);
        }
    }
    musicLastNote = 255;
}

// =====================================================================
// Settings app -- audio feedback on/off + colour theme
// =====================================================================

// Draws (or redraws) one of the two setting rows: an outlined box with
// the setting's name on the left and its current value on the right.
// The highlighted row gets the brighter outline; the other stays dim.
static void drawSettingsRow(uint8_t row)
{
    int16_t y = SETTINGS_ROW_Y + (int16_t)row * (SETTINGS_ROW_H + SETTINGS_ROW_GAP);
    bool here = (settingsCursor == row);

    tft->fillRect(SETTINGS_ROW_X, y, SETTINGS_ROW_W, SETTINGS_ROW_H, COLOR_BG);
    tft->drawRect(SETTINGS_ROW_X, y, SETTINGS_ROW_W, SETTINGS_ROW_H,
                  here ? COLOR_ACCENT : COLOR_ACCENT_DIM);

    const char* label = "AUDIO";
    if (row == 1)      label = "THEME";
    else if (row == 2) label = "SPLASH";
    else if (row == 3) label = "SLEEP";

    tft->setTextSize(1);
    tft->setTextColor(here ? COLOR_ACCENT : COLOR_FG);
    tft->setCursor(SETTINGS_ROW_X + 5, y + (SETTINGS_ROW_H - 8) / 2);
    tft->print(label);

    char value[20];
    uint16_t valueColor;

    if (row == 0)
    {
        snprintf(value, sizeof(value), "%s", audioIsEnabled() ? "ON" : "OFF");
        valueColor = audioIsEnabled() ? COLOR_ACCENT : COLOR_DIM;
    }
    else if (row == 1)
    {
        snprintf(value, sizeof(value), "%s", themeName(themeGetIndex()));
        valueColor = themeFgColor(themeGetIndex());
    }
    else if (row == 2)
    {
        // The chosen splash picture by filename, or DEFAULT for the
        // built-in logo. Long names are shortened to fit the row.
        const char* path = prefsSplashPath();
        if (path[0] == '\0')
        {
            snprintf(value, sizeof(value), "DEFAULT");
            valueColor = COLOR_DIM;
        }
        else
        {
            const char* slash = strrchr(path, '/');
            const char* base = slash ? slash + 1 : path;
            snprintf(value, sizeof(value), "%.14s", base);
            valueColor = COLOR_ACCENT;
        }
    }
    else
    {
        snprintf(value, sizeof(value), "%s", prefsSleepShortName(prefsSleepIndex()));
        valueColor = (prefsSleepIndex() == 0) ? COLOR_DIM : COLOR_ACCENT;
    }

    int16_t vw = (int16_t)strlen(value) * 6;
    tft->setTextColor(valueColor);
    tft->setCursor(SETTINGS_ROW_X + SETTINGS_ROW_W - 5 - vw, y + (SETTINGS_ROW_H - 8) / 2);
    tft->print(value);
}

// The colour theme list: every available colour by name, with the
// current one boxed. Each name is drawn in its own colour so the picker
// doubles as a preview. Up/Down re-themes the whole device as you move.
static void drawThemePicker()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_ACCENT);
    const char* title = "COLOUR THEME";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 3);
    tft->print(title);

    for (uint8_t i = 0; i < themeCount(); i++)
    {
        int16_t y = SETTINGS_THEME_Y + (int16_t)i * SETTINGS_THEME_H;
        bool sel = (i == themeGetIndex());

        if (sel)
        {
            tft->drawRect(6, y - 2, SCREEN_WIDTH - 12, SETTINGS_THEME_H - 1, COLOR_ACCENT);
        }

        const char* name = themeName(i);
        int16_t nx = (SCREEN_WIDTH - (int16_t)strlen(name) * 6) / 2;
        tft->setTextColor(sel ? COLOR_ACCENT : themeFgColor(i));
        tft->setCursor(nx, y);
        tft->print(name);
    }
}

// Scans the SD card's root folder for .bmp files, filling splashFiles[].
// Only names are kept -- the chosen file is read straight from the card
// each time the splash is drawn, so re-copying a picture with the same
// name updates the boot screen without changing the setting.
static void scanSplashFiles()
{
    splashFileCount = 0;

    if (!sdReady) return;

    File dir = SD.open("/");
    if (!dir || !dir.isDirectory())
    {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    while (entry && splashFileCount < SPLASH_MAX_FILES)
    {
        if (!entry.isDirectory())
        {
            const char* rawName = entry.name();
            const char* slash = strrchr(rawName, '/');
            const char* base = slash ? slash + 1 : rawName;
            size_t len = strlen(base);

            if (len > 4 && strcasecmp(base + len - 4, ".bmp") == 0)
            {
                snprintf(splashFiles[splashFileCount], sizeof(splashFiles[0]), "/%s", base);
                splashFileCount++;
            }
        }

        entry.close();
        entry = dir.openNextFile();
    }

    if (entry) entry.close();
    dir.close();
}

// The splash-image list: the built-in logo first, then every .bmp found
// in the card's root folder. The current choice is drawn brighter than
// the rest so it is easy to see what is already selected.
static void drawSplashPicker()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_ACCENT);
    const char* title = "SPLASH IMAGE";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    uint8_t total = (uint8_t)(splashFileCount + 1);

    for (uint8_t slot = 0; slot < SETTINGS_LIST_VISIBLE; slot++)
    {
        uint8_t idx = (uint8_t)(splashScrollTop + slot);
        if (idx >= total) break;

        int16_t y = SETTINGS_LIST_Y + (int16_t)slot * SETTINGS_LIST_H;
        bool sel = (idx == splashCursor);

        if (sel)
        {
            tft->drawRect(6, y - 1, SCREEN_WIDTH - 12, SETTINGS_LIST_H - 1, COLOR_ACCENT);
        }

        char label[28];
        bool isCurrent;

        if (idx == 0)
        {
            snprintf(label, sizeof(label), "BUILT-IN LOGO");
            isCurrent = (prefsSplashPath()[0] == '\0');
        }
        else
        {
            const char* path = splashFiles[idx - 1];
            const char* slash = strrchr(path, '/');
            snprintf(label, sizeof(label), "%.24s", slash ? slash + 1 : path);
            isCurrent = (strcmp(path, prefsSplashPath()) == 0);
        }

        int16_t nx = (SCREEN_WIDTH - (int16_t)strlen(label) * 6) / 2;
        if (nx < 2) nx = 2;
        tft->setTextColor(sel ? COLOR_ACCENT : (isCurrent ? COLOR_FG : COLOR_DIM));
        tft->setCursor(nx, y);
        tft->print(label);
    }
}

// The auto-sleep timeout list. One row per available timeout, from OFF
// up to 10 minutes.
static void drawSleepPicker()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_ACCENT);
    const char* title = "AUTO SLEEP";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    for (uint8_t i = 0; i < prefsSleepCount(); i++)
    {
        int16_t y = SETTINGS_LIST_Y + (int16_t)i * SETTINGS_LIST_H;
        bool sel = (i == sleepCursor);

        if (sel)
        {
            tft->drawRect(6, y - 1, SCREEN_WIDTH - 12, SETTINGS_LIST_H - 1, COLOR_ACCENT);
        }

        const char* name = prefsSleepName(i);
        int16_t nx = (SCREEN_WIDTH - (int16_t)strlen(name) * 6) / 2;
        tft->setTextColor(sel ? COLOR_ACCENT : COLOR_FG);
        tft->setCursor(nx, y);
        tft->print(name);
    }
}

void drawSettings()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "Settings";
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (titleX < 2) titleX = 2;
    tft->setCursor(titleX, CONTENT_TOP + 2);
    tft->print(title);

    for (uint8_t i = 0; i < SETTINGS_ROW_COUNT; i++)
    {
        drawSettingsRow(i);
    }
}

// =====================================================================
// Breakout game
// =====================================================================

static void breakoutInit()
{
    breakoutPaddleX = BREAKOUT_PLAY_X + (BREAKOUT_PLAY_W - BREAKOUT_PADDLE_W) / 2;
    breakoutBallX = breakoutPaddleX + (BREAKOUT_PADDLE_W - BREAKOUT_BALL_SIZE) / 2;
    breakoutBallY = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_PADDLE_H - BREAKOUT_BALL_SIZE - 2;
    breakoutBallDX = 2;
    breakoutBallDY = -2;
    breakoutLives = 3;
    breakoutScore = 0;
    breakoutLaunched = false;
    breakoutGameWon = false;
    breakoutGameOver = false;
    lastBreakoutUpdate = millis();

    for (uint8_t r = 0; r < BREAKOUT_BRICK_ROWS; r++)
    {
        for (uint8_t c = 0; c < BREAKOUT_BRICK_COLS; c++)
        {
            breakoutBricks[r][c] = 1;
        }
    }
}

void drawBreakout()
{
    clearContent();
    drawStatusBar();

    // Title + HUD
    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    tft->setCursor(4, CONTENT_TOP + 2);
    tft->print("Breakout");

    char hud[24];
    snprintf(hud, sizeof(hud), "%u  Lives:%u", breakoutScore, breakoutLives);
    int16_t hx = SCREEN_WIDTH - (int16_t)strlen(hud) * 6 - 4;
    tft->setCursor(hx, CONTENT_TOP + 2);
    tft->print(hud);

    // Play-area border
    tft->drawRect(BREAKOUT_PLAY_X - 1, BREAKOUT_PLAY_Y - 1,
                  BREAKOUT_PLAY_W + 2, BREAKOUT_PLAY_H + 2, COLOR_DIM);

    // Bricks -- a red-to-green heat ramp, no blue/cyan anywhere.
    for (uint8_t r = 0; r < BREAKOUT_BRICK_ROWS; r++)
    {
        for (uint8_t c = 0; c < BREAKOUT_BRICK_COLS; c++)
        {
            if (breakoutBricks[r][c] == 0) continue;

            int16_t bx = BREAKOUT_PLAY_X + c * (BREAKOUT_BRICK_W + BREAKOUT_BRICK_GAP);
            int16_t by = BREAKOUT_PLAY_Y + r * (BREAKOUT_BRICK_H + BREAKOUT_BRICK_GAP);

            uint16_t color;
            switch (r)
            {
                case 0:  color = 0xF800; break; // red
                case 1:  color = 0xFD20; break; // amber
                case 2:  color = 0xFFE0; break; // yellow
                default: color = 0x07E0; break; // green
            }

            tft->fillRect(bx, by, BREAKOUT_BRICK_W, BREAKOUT_BRICK_H, color);
        }
    }

    // Paddle
    int16_t paddleY = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_PADDLE_H;
    tft->fillRect(breakoutPaddleX, paddleY,
                  BREAKOUT_PADDLE_W, BREAKOUT_PADDLE_H, COLOR_ACCENT);

    // Ball
    tft->fillRect(breakoutBallX, breakoutBallY,
                  BREAKOUT_BALL_SIZE, BREAKOUT_BALL_SIZE, COLOR_FG);

    // Overlay messages
    if (breakoutGameOver)
    {
        tft->setTextSize(2);
        tft->setTextColor(0xF800);
        const char* msg = "GAME OVER";
        int16_t mx = (SCREEN_WIDTH - (int16_t)strlen(msg) * 12) / 2;
        tft->setCursor(mx, BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H / 2 - 12);
        tft->print(msg);
    }
    else if (breakoutGameWon)
    {
        tft->setTextSize(2);
        tft->setTextColor(0x07E0);
        const char* msg = "YOU WIN!";
        int16_t mx = (SCREEN_WIDTH - (int16_t)strlen(msg) * 12) / 2;
        tft->setCursor(mx, BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H / 2 - 12);
        tft->print(msg);
    }
    else if (!breakoutLaunched)
    {
        tft->setTextSize(1);
        tft->setTextColor(COLOR_FG);
        const char* msg = "Press 5 to launch";
        int16_t mx = (SCREEN_WIDTH - (int16_t)strlen(msg) * 6) / 2;
        tft->setCursor(mx, BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H / 2 - 4);
        tft->print(msg);
    }
}

// ---------------------------------------------------------------------
// Targeted, single-rect redraws used by breakoutHandleKey()/breakoutTick()
// once the game is running, so a normal frame only touches the few
// pixels that actually changed (the ball, or a moved paddle, or one
// destroyed brick) instead of calling drawBreakout() and repainting the
// whole play field -- that repeated full-screen clear+redraw is what
// caused the visible flash every ~50ms.
// ---------------------------------------------------------------------

static void breakoutEraseBallAt(int16_t x, int16_t y)
{
    tft->fillRect(x, y, BREAKOUT_BALL_SIZE, BREAKOUT_BALL_SIZE, COLOR_BG);
}

static void breakoutDrawBallAt(int16_t x, int16_t y)
{
    tft->fillRect(x, y, BREAKOUT_BALL_SIZE, BREAKOUT_BALL_SIZE, COLOR_FG);
}

static void breakoutErasePaddleAt(int16_t x)
{
    int16_t paddleY = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_PADDLE_H;
    tft->fillRect(x, paddleY, BREAKOUT_PADDLE_W, BREAKOUT_PADDLE_H, COLOR_BG);
}

static void breakoutDrawPaddleAt(int16_t x)
{
    int16_t paddleY = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_PADDLE_H;
    tft->fillRect(x, paddleY, BREAKOUT_PADDLE_W, BREAKOUT_PADDLE_H, COLOR_ACCENT);
}

static void breakoutEraseBrickAt(uint8_t r, uint8_t c)
{
    int16_t bx = BREAKOUT_PLAY_X + c * (BREAKOUT_BRICK_W + BREAKOUT_BRICK_GAP);
    int16_t by = BREAKOUT_PLAY_Y + r * (BREAKOUT_BRICK_H + BREAKOUT_BRICK_GAP);
    tft->fillRect(bx, by, BREAKOUT_BRICK_W, BREAKOUT_BRICK_H, COLOR_BG);
}

static void breakoutUpdateHUD()
{
    // Fixed-width erase (rather than sizing the rect to the new string)
    // so old digits never show through even when the new text is
    // shorter than what was there before -- e.g. lives dropping from
    // "10" to "9" wouldn't fully overwrite a longer previous string.
    const int16_t hudW = 90;
    int16_t hudX = SCREEN_WIDTH - hudW;
    tft->fillRect(hudX, CONTENT_TOP + 2, hudW - 2, 8, COLOR_BG);

    char hud[24];
    snprintf(hud, sizeof(hud), "%u  Lives:%u", breakoutScore, breakoutLives);
    int16_t hx = SCREEN_WIDTH - (int16_t)strlen(hud) * 6 - 4;

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    tft->setCursor(hx, CONTENT_TOP + 2);
    tft->print(hud);
}

static void breakoutEraseMessageArea()
{
    // Generously-sized erase covering where either the "Press 5 to
    // launch" prompt could sit, centered in the play field.
    const char* msg = "Press 5 to launch";
    int16_t mw = (int16_t)strlen(msg) * 6;
    int16_t mx = (SCREEN_WIDTH - mw) / 2;
    int16_t my = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H / 2 - 4;
    tft->fillRect(mx - 2, my - 1, mw + 4, 10, COLOR_BG);
}

static void breakoutHandleKey(char key)
{
    if (breakoutGameOver || breakoutGameWon) return;

    int16_t moveStep = BREAKOUT_PADDLE_W / 2;
    int16_t maxX = BREAKOUT_PLAY_X + BREAKOUT_PLAY_W - BREAKOUT_PADDLE_W;

    if (key == '4' || key == '6')
    {
        int16_t oldX = breakoutPaddleX;

        if (key == '4')
        {
            breakoutPaddleX -= moveStep;
            if (breakoutPaddleX < BREAKOUT_PLAY_X) breakoutPaddleX = BREAKOUT_PLAY_X;
        }
        else
        {
            breakoutPaddleX += moveStep;
            if (breakoutPaddleX > maxX) breakoutPaddleX = maxX;
        }

        // Only touch the two small strips that actually changed --
        // erase the old paddle rect, draw the new one -- instead of a
        // full-screen redraw for a one-key nudge.
        if (breakoutPaddleX != oldX)
        {
            breakoutErasePaddleAt(oldX);
            breakoutDrawPaddleAt(breakoutPaddleX);
        }
    }
    else if (key == '5' && !breakoutLaunched)
    {
        breakoutLaunched = true;
        breakoutEraseMessageArea();
    }
}

static void breakoutTick()
{
    if (breakoutGameOver || breakoutGameWon || !breakoutLaunched) return;

    // Erase the ball at its current (about-to-be-old) position first --
    // this plus drawing it at its new position at the end is normally
    // ALL that needs to touch the screen on a given frame. That's what
    // stops the full-screen flash: we're no longer wiping and
    // redrawing the border/bricks/paddle/HUD every 50ms, just the one
    // small rect the ball actually occupies.
    breakoutEraseBallAt(breakoutBallX, breakoutBallY);

    breakoutBallX += breakoutBallDX;
    breakoutBallY += breakoutBallDY;

    int16_t playRight  = BREAKOUT_PLAY_X + BREAKOUT_PLAY_W - BREAKOUT_BALL_SIZE;
    int16_t playBottom = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_BALL_SIZE;
    int16_t paddleY    = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_PADDLE_H;

    // Wall collisions
    if (breakoutBallX <= BREAKOUT_PLAY_X)
    {
        breakoutBallX = BREAKOUT_PLAY_X;
        breakoutBallDX = -breakoutBallDX;
    }
    if (breakoutBallX >= playRight)
    {
        breakoutBallX = playRight;
        breakoutBallDX = -breakoutBallDX;
    }
    if (breakoutBallY <= BREAKOUT_PLAY_Y)
    {
        breakoutBallY = BREAKOUT_PLAY_Y;
        breakoutBallDY = -breakoutBallDY;
    }

    // Ball lost -- rare event, full redraw is fine here (resets the
    // ball, may show the launch prompt or GAME OVER banner).
    if (breakoutBallY > playBottom + 8)
    {
        breakoutLives--;
        if (breakoutLives == 0)
        {
            breakoutGameOver = true;
            drawBreakout();
            return;
        }
        breakoutBallX = breakoutPaddleX + (BREAKOUT_PADDLE_W - BREAKOUT_BALL_SIZE) / 2;
        breakoutBallY = paddleY - BREAKOUT_BALL_SIZE - 2;
        breakoutBallDX = 2;
        breakoutBallDY = -2;
        breakoutLaunched = false;
        drawBreakout();
        return;
    }

    // Paddle collision
    if (breakoutBallDY > 0 &&
        breakoutBallY + BREAKOUT_BALL_SIZE >= paddleY &&
        breakoutBallY + BREAKOUT_BALL_SIZE <= paddleY + BREAKOUT_PADDLE_H &&
        breakoutBallX + BREAKOUT_BALL_SIZE > breakoutPaddleX &&
        breakoutBallX < breakoutPaddleX + BREAKOUT_PADDLE_W)
    {
        breakoutBallDY = -breakoutBallDY;
        breakoutBallY = paddleY - BREAKOUT_BALL_SIZE;

        int16_t hitPos = (breakoutBallX + BREAKOUT_BALL_SIZE / 2) - breakoutPaddleX;
        int8_t third = BREAKOUT_PADDLE_W / 3;
        if (hitPos < third)
            breakoutBallDX = -2;
        else if (hitPos > third * 2)
            breakoutBallDX = 2;
    }

    // Brick collisions
    for (uint8_t r = 0; r < BREAKOUT_BRICK_ROWS; r++)
    {
        for (uint8_t c = 0; c < BREAKOUT_BRICK_COLS; c++)
        {
            if (breakoutBricks[r][c] == 0) continue;

            int16_t brickX = BREAKOUT_PLAY_X + c * (BREAKOUT_BRICK_W + BREAKOUT_BRICK_GAP);
            int16_t brickY = BREAKOUT_PLAY_Y + r * (BREAKOUT_BRICK_H + BREAKOUT_BRICK_GAP);

            if (breakoutBallX < brickX + BREAKOUT_BRICK_W &&
                breakoutBallX + BREAKOUT_BALL_SIZE > brickX &&
                breakoutBallY < brickY + BREAKOUT_BRICK_H &&
                breakoutBallY + BREAKOUT_BALL_SIZE > brickY)
            {
                breakoutBricks[r][c] = 0;
                breakoutScore += 10;

                int16_t oL = (breakoutBallX + BREAKOUT_BALL_SIZE) - brickX;
                int16_t oR = (brickX + BREAKOUT_BRICK_W) - breakoutBallX;
                int16_t oT = (breakoutBallY + BREAKOUT_BALL_SIZE) - brickY;
                int16_t oB = (brickY + BREAKOUT_BRICK_H) - breakoutBallY;

                int16_t minO = oL;
                if (oR < minO) minO = oR;
                if (oT < minO) minO = oT;
                if (oB < minO) minO = oB;

                if (minO == oL || minO == oR)
                    breakoutBallDX = -breakoutBallDX;
                else
                    breakoutBallDY = -breakoutBallDY;

                // Check win
                bool allGone = true;
                for (uint8_t rr = 0; rr < BREAKOUT_BRICK_ROWS && allGone; rr++)
                    for (uint8_t cc = 0; cc < BREAKOUT_BRICK_COLS && allGone; cc++)
                        if (breakoutBricks[rr][cc] != 0) allGone = false;

                if (allGone)
                {
                    // Rare, one-time event -- full redraw to show the
                    // YOU WIN! banner.
                    breakoutGameWon = true;
                    drawBreakout();
                }
                else
                {
                    // Common case: erase just the one destroyed brick,
                    // refresh the small HUD text strip (score changed),
                    // and draw the ball at its new bounced position.
                    breakoutEraseBrickAt(r, c);
                    breakoutUpdateHUD();
                    breakoutDrawBallAt(breakoutBallX, breakoutBallY);
                }
                return;
            }
        }
    }

    // Normal frame, nothing but the ball moved.
    breakoutDrawBallAt(breakoutBallX, breakoutBallY);
}

// =====================================================================
// WiFi Scanner app
// =====================================================================

static void drawWifiScannerStatus()
{
    tft->fillRect(0, CONTENT_TOP + 12, SCREEN_WIDTH, 12, COLOR_BG);

    tft->setTextSize(1);

    char buf[24];
    uint16_t color = COLOR_FG;

    if (wifiScanIsScanning())
    {
        strncpy(buf, "Scanning...", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }
    else
    {
        uint8_t count = wifiScanNetworkCount();
        snprintf(buf, sizeof(buf), "%u network%s found", count, (count == 1) ? "" : "s");
        color = COLOR_ACCENT;
    }

    tft->setTextColor(color);
    int16_t sx = (SCREEN_WIDTH - (int16_t)strlen(buf) * 6) / 2;
    if (sx < 0) sx = 0;
    tft->setCursor(sx, CONTENT_TOP + 14);
    tft->print(buf);
}

// Draws the visible page of scanned SSIDs. Any name too wide for its
// row scrolls as a looping ticker (using the shared wifiMarqueeOffset
// character position) rather than being truncated, so the full SSID
// is still readable.
static void drawWifiScannerList()
{
    uint8_t count = wifiScanNetworkCount();

    for (uint8_t i = 0; i < WIFI_VISIBLE_ROWS; i++)
    {
        uint8_t idx = wifiScrollTop + i;
        int16_t y = WIFI_LIST_Y + i * WIFI_ROW_H;
        int16_t rowX = 8;
        int16_t rowW = SCREEN_WIDTH - 16;
        int16_t rowH = WIFI_ROW_H - 2;

        tft->fillRect(rowX, y, rowW, rowH, COLOR_BG);

        if (idx >= count) continue;

        bool cursorHere = (idx == wifiCursor);

        if (cursorHere)
        {
            tft->fillRect(rowX, y, rowW, rowH, COLOR_SELECT_BG);
            tft->drawRect(rowX, y, rowW, rowH, COLOR_ACCENT_DIM);
        }

        uint16_t rowColor = cursorHere ? COLOR_FG : COLOR_DIM;
        tft->setTextSize(1);
        tft->setTextColor(rowColor);

        // RSSI text, right-aligned within the row.
        char rssiBuf[6];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d", wifiScanRSSI(idx));
        int16_t rssiW = (int16_t)strlen(rssiBuf) * 6;
        int16_t rssiX = rowX + rowW - 4 - rssiW;

        // Small lock glyph just left of the RSSI: filled = secured
        // network, hollow outline = open network.
        const int16_t lockSz = 5;
        int16_t lockX = rssiX - 4 - lockSz;
        int16_t lockY = y + (rowH - lockSz) / 2;
        tft->drawRect(lockX, lockY, lockSz, lockSz, rowColor);
        if (wifiScanIsEncrypted(idx))
        {
            tft->fillRect(lockX + 1, lockY + 1, lockSz - 2, lockSz - 2, rowColor);
        }

        // SSID: fits normally when short enough, otherwise scrolls.
        int16_t nameAreaX = rowX + 4;
        int16_t nameAreaW = lockX - 4 - nameAreaX;
        uint8_t nameSlots = (nameAreaW > 0) ? (uint8_t)(nameAreaW / 6) : 0;
        if (nameSlots > 22) nameSlots = 22;

        const char* ssid = wifiScanSSID(idx);
        size_t ssidLen = strlen(ssid);
        char label[24];

        if (ssidLen <= nameSlots)
        {
            strncpy(label, ssid, sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
        }
        else
        {
            // Circular ticker: the name plus a small gap, repeating --
            // wifiMarqueeOffset is a shared character position advanced
            // once per WIFI_MARQUEE_STEP_MS in uiTick() so every
            // overflowing row scrolls in sync.
            char ring[40];
            snprintf(ring, sizeof(ring), "%s   ", ssid);
            size_t ringLen = strlen(ring);
            uint16_t off = (ringLen > 0) ? (wifiMarqueeOffset % ringLen) : 0;
            uint8_t copyLen = (nameSlots < sizeof(label) - 1) ? nameSlots : (uint8_t)(sizeof(label) - 1);

            for (uint8_t c = 0; c < copyLen; c++)
            {
                label[c] = ring[(off + c) % ringLen];
            }
            label[copyLen] = '\0';
        }

        tft->setCursor(nameAreaX, y + (rowH - 8) / 2);
        tft->print(label);

        tft->setCursor(rssiX, y + (rowH - 8) / 2);
        tft->print(rssiBuf);
    }
}

void drawWifiScanner()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "WiFi Scanner";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    drawWifiScannerStatus();
    drawWifiScannerList();
}

// =====================================================================
// StockWatch app -- live stock prices via the Finnhub API (see
// StockWatch.h/.cpp for the WiFi connect + HTTPS fetching; this only
// reads the sorted results and draws).
// =====================================================================

static void drawStockWatchStatus()
{
    tft->fillRect(0, CONTENT_TOP + 12, SCREEN_WIDTH, 12, COLOR_BG);

    tft->setTextSize(1);

    char buf[28];
    uint16_t color = COLOR_FG;

    switch (stockWatchGetStatus())
    {
        case STOCK_STATUS_CONNECTING_WIFI:
            strncpy(buf, "Connecting to WiFi...", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            break;
        case STOCK_STATUS_WIFI_FAILED:
            strncpy(buf, "WiFi connection failed", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            color = COLOR_DIM;
            break;
        case STOCK_STATUS_FETCHING:
            strncpy(buf, "Fetching quotes...", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            break;
        case STOCK_STATUS_NO_DATA:
            strncpy(buf, "No quotes available", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            color = COLOR_DIM;
            break;
        case STOCK_STATUS_READY:
        {
            uint8_t count = stockWatchCount();
            snprintf(buf, sizeof(buf), "%u quote%s (lowest first)", count, (count == 1) ? "" : "s");
            color = COLOR_ACCENT;
            break;
        }
        case STOCK_STATUS_IDLE:
        default:
            strncpy(buf, "Ready", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            break;
    }

    tft->setTextColor(color);
    int16_t sx = (SCREEN_WIDTH - (int16_t)strlen(buf) * 6) / 2;
    if (sx < 0) sx = 0;
    tft->setCursor(sx, CONTENT_TOP + 14);
    tft->print(buf);
}

// Draws the visible page of quotes, sorted ascending by price (lowest
// first) -- StockWatch.cpp does the sorting, this just reads it off.
static void drawStockWatchList()
{
    uint8_t count = stockWatchCount();

    for (uint8_t i = 0; i < STOCK_VISIBLE_ROWS; i++)
    {
        uint8_t idx = stockScrollTop + i;
        int16_t y = STOCK_LIST_Y + i * STOCK_ROW_H;
        int16_t rowX = 8;
        int16_t rowW = SCREEN_WIDTH - 16;
        int16_t rowH = STOCK_ROW_H - 2;

        tft->fillRect(rowX, y, rowW, rowH, COLOR_BG);

        if (idx >= count) continue;

        bool cursorHere = (idx == stockCursor);

        if (cursorHere)
        {
            tft->fillRect(rowX, y, rowW, rowH, COLOR_SELECT_BG);
            tft->drawRect(rowX, y, rowW, rowH, COLOR_ACCENT_DIM);
        }

        uint16_t rowColor = cursorHere ? COLOR_FG : COLOR_DIM;
        tft->setTextSize(1);
        tft->setTextColor(rowColor);

        // Price, right-aligned, e.g. "$123.45".
        char priceBuf[16];
        snprintf(priceBuf, sizeof(priceBuf), "$%.2f", stockWatchPrice(idx));
        int16_t priceW = (int16_t)strlen(priceBuf) * 6;
        int16_t priceX = rowX + rowW - 4 - priceW;

        tft->setCursor(rowX + 4, y + (rowH - 8) / 2);
        tft->print(stockWatchSymbol(idx));

        tft->setCursor(priceX, y + (rowH - 8) / 2);
        tft->print(priceBuf);
    }
}

void drawStockWatch()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "StockWatch";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    drawStockWatchStatus();
    drawStockWatchList();
}

// =====================================================================
// Calculator.
//
// Flow: type Num1's digits (0-9), press # to confirm and move to Num2,
// type Num2's digits, press # again to move into operation selection --
// there, # moves down the list and 0 moves up, and 5 computes/selects.
// From the result screen, 5 starts a fresh calculation. * always exits.
// =====================================================================

struct CalcOp {
    const char* name;
};

static const CalcOp calcOps[] = {
    { "Addition" },
    { "Subtraction" },
    { "Division" },
    { "Multiplication" },
    { "Percentage of" },
    { "Raised to power of" },
    { "Rooted to" },
    { "Log of" },
    { "Geometric mean" },
    { "Permutation (nPr)" },
    { "Combination (nCr)" },
};
#define CALC_OP_COUNT (sizeof(calcOps) / sizeof(calcOps[0]))

static void calculatorInit()
{
    calcStage = CALC_STAGE_NUM1;
    calcNum1Buf[0] = '\0';
    calcNum2Buf[0] = '\0';
    calcNum1Len = 0;
    calcNum2Len = 0;
    calcOpIndex = 0;
    calcResult = 0.0;
    calcHasError = false;
    calcErrorMsg[0] = '\0';
}

// Computes calcResult from calcNum1Buf/calcNum2Buf and calcOpIndex,
// setting calcHasError + calcErrorMsg instead whenever the chosen
// operation is undefined for the given inputs (divide by zero, log of
// a non-positive number, r > n for nPr/nCr, etc.) so the UI can show a
// clear message rather than a garbage or NaN/Inf result.
static void calculatorCompute()
{
    double n1 = atof(calcNum1Buf);
    double n2 = atof(calcNum2Buf);

    calcHasError = false;
    calcErrorMsg[0] = '\0';
    calcResult = 0.0;

    switch (calcOpIndex)
    {
        case 0: // Addition
            calcResult = n1 + n2;
            break;

        case 1: // Subtraction
            calcResult = n1 - n2;
            break;

        case 2: // Division
            if (n2 == 0.0)
            {
                calcHasError = true;
                strncpy(calcErrorMsg, "Cannot divide by zero", sizeof(calcErrorMsg) - 1);
            }
            else
            {
                calcResult = n1 / n2;
            }
            break;

        case 3: // Multiplication
            calcResult = n1 * n2;
            break;

        case 4: // Percentage of -- n1 percent of n2
            calcResult = (n1 / 100.0) * n2;
            break;

        case 5: // Raised to power of -- n1 ^ n2
            calcResult = pow(n1, n2);
            break;

        case 6: // Rooted to -- the n2-th root of n1
            if (n2 == 0.0)
            {
                calcHasError = true;
                strncpy(calcErrorMsg, "Cannot root by zero", sizeof(calcErrorMsg) - 1);
            }
            else
            {
                calcResult = pow(n1, 1.0 / n2);
            }
            break;

        case 7: // Log of -- log base n2 of n1
            if (n1 <= 0.0 || n2 <= 0.0 || n2 == 1.0)
            {
                calcHasError = true;
                strncpy(calcErrorMsg, "Log undefined for inputs", sizeof(calcErrorMsg) - 1);
            }
            else
            {
                calcResult = log(n1) / log(n2);
            }
            break;

        case 8: // Geometric mean -- sqrt(n1 * n2)
            if (n1 * n2 < 0.0)
            {
                calcHasError = true;
                strncpy(calcErrorMsg, "Undefined: negative product", sizeof(calcErrorMsg) - 1);
            }
            else
            {
                calcResult = sqrt(n1 * n2);
            }
            break;

        case 9:  // Permutation (nPr)
        case 10: // Combination (nCr)
        {
            long n = (long)(n1 + 0.5);
            long r = (long)(n2 + 0.5);

            if (n < 0 || r < 0 || r > n)
            {
                calcHasError = true;
                strncpy(calcErrorMsg, "Need 0 <= r <= n", sizeof(calcErrorMsg) - 1);
            }
            else
            {
                // n! / (n-r)! computed as a running product, rather than
                // two separate full factorials, so it stays in double
                // range for larger n as long as the final ratio does.
                double perm = 1.0;
                for (long i = 0; i < r; i++)
                {
                    perm *= (double)(n - i);
                }

                if (calcOpIndex == 9)
                {
                    calcResult = perm;
                }
                else
                {
                    double rFact = 1.0;
                    for (long i = 2; i <= r; i++)
                    {
                        rFact *= (double)i;
                    }
                    calcResult = perm / rFact;
                }
            }
            break;
        }
    }

    if (!calcHasError && (isnan(calcResult) || isinf(calcResult)))
    {
        calcHasError = true;
        strncpy(calcErrorMsg, "Result undefined", sizeof(calcErrorMsg) - 1);
    }
    calcErrorMsg[sizeof(calcErrorMsg) - 1] = '\0';
}

// Whole-number results print with no decimal places; anything else
// prints with at least 5 decimal digits, as asked.
static void calculatorFormatResult(char* out, size_t outSize)
{
    double rounded = round(calcResult);
    if (fabs(calcResult - rounded) < 1e-9)
    {
        snprintf(out, outSize, "%.0f", calcResult);
    }
    else
    {
        snprintf(out, outSize, "%.5f", calcResult);
    }
}

static void calculatorHandleKey(char key)
{
    bool isDigit = (key >= '0' && key <= '9');

    if (calcStage == CALC_STAGE_NUM1 || calcStage == CALC_STAGE_NUM2)
    {
        char* buf     = (calcStage == CALC_STAGE_NUM1) ? calcNum1Buf : calcNum2Buf;
        uint8_t* len  = (calcStage == CALC_STAGE_NUM1) ? &calcNum1Len : &calcNum2Len;

        if (isDigit)
        {
            if (*len < CALC_MAX_DIGITS)
            {
                buf[*len] = key;
                (*len)++;
                buf[*len] = '\0';
                drawCalculator();
            }
        }
        else if (key == '#')
        {
            if (*len == 0) return; // need at least one digit before advancing

            if (calcStage == CALC_STAGE_NUM1)
            {
                calcStage = CALC_STAGE_NUM2;
            }
            else
            {
                calcStage = CALC_STAGE_OP;
                calcOpIndex = 0;
            }
            drawCalculator();
        }
    }
    else if (calcStage == CALC_STAGE_OP)
    {
        if (key == '#') // down
        {
            calcOpIndex = (uint8_t)((calcOpIndex + 1) % CALC_OP_COUNT);
            drawCalculator();
        }
        else if (key == '0') // up
        {
            calcOpIndex = (calcOpIndex == 0) ? (uint8_t)(CALC_OP_COUNT - 1) : (uint8_t)(calcOpIndex - 1);
            drawCalculator();
        }
        else if (key == KEY_SELECT)
        {
            calculatorCompute();
            calcStage = CALC_STAGE_RESULT;
            drawCalculator();
        }
    }
    else if (calcStage == CALC_STAGE_RESULT)
    {
        if (key == KEY_SELECT)
        {
            calculatorInit();
            drawCalculator();
        }
    }
}

void drawCalculator()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "Calculator";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    int16_t y = CALC_LINE_Y;
    char line[32];

    // Num1 -- shown with a trailing cursor while it's still being typed.
    if (calcStage == CALC_STAGE_NUM1)
    {
        snprintf(line, sizeof(line), "Num1: %s_", calcNum1Buf);
    }
    else
    {
        snprintf(line, sizeof(line), "Num1: %s", calcNum1Buf);
    }
    tft->setTextColor(COLOR_FG);
    tft->setCursor(6, y);
    tft->print(line);
    y += CALC_LINE_H;

    // Num2 -- blank until we've left the Num1 stage.
    if (calcStage != CALC_STAGE_NUM1)
    {
        if (calcStage == CALC_STAGE_NUM2)
        {
            snprintf(line, sizeof(line), "Num2: %s_", calcNum2Buf);
        }
        else
        {
            snprintf(line, sizeof(line), "Num2: %s", calcNum2Buf);
        }
        tft->setCursor(6, y);
        tft->print(line);
    }
    y += CALC_LINE_H;

    // Operation -- a live "<Name>" while choosing, a plain "Op: Name"
    // once a result has been computed.
    if (calcStage == CALC_STAGE_OP || calcStage == CALC_STAGE_RESULT)
    {
        char opLine[32];
        if (calcStage == CALC_STAGE_OP)
        {
            tft->setTextColor(COLOR_ACCENT);
            snprintf(opLine, sizeof(opLine), "<%s>", calcOps[calcOpIndex].name);
        }
        else
        {
            tft->setTextColor(COLOR_FG);
            snprintf(opLine, sizeof(opLine), "Op: %s", calcOps[calcOpIndex].name);
        }
        tft->setCursor(6, y);
        tft->print(opLine);
    }
    y += CALC_LINE_H;

    // Result / error.
    if (calcStage == CALC_STAGE_RESULT)
    {
        char resLine[32];
        if (calcHasError)
        {
            tft->setTextColor(COLOR_DIM);
            snprintf(resLine, sizeof(resLine), "%s", calcErrorMsg);
        }
        else
        {
            char resultStr[24];
            calculatorFormatResult(resultStr, sizeof(resultStr));
            tft->setTextColor(COLOR_ACCENT);
            snprintf(resLine, sizeof(resLine), "= %s", resultStr);
        }
        tft->setCursor(6, y);
        tft->print(resLine);
    }

}

// =====================================================================
// PackMon app -- live 802.11 packet-rate graph (see PacketMon.h/.cpp
// for the actual sniffing; this only reads its counters and draws).
// =====================================================================

static void drawPacketMonStatus()
{
    tft->fillRect(0, CONTENT_TOP + 12, SCREEN_WIDTH, 24, COLOR_BG);
    tft->setTextSize(1);

    char line1[24];
    snprintf(line1, sizeof(line1), "Ch %2u  %s", packetMonGetChannel(),
              packetMonIsAutoHop() ? "(auto-hop)" : "");
    tft->setTextColor(COLOR_FG);
    tft->setCursor(6, CONTENT_TOP + 14);
    tft->print(line1);

    char line2[24];
    snprintf(line2, sizeof(line2), "%lu pkt/s  RSSI %d",
              (unsigned long)packetMonGetPacketRate(), packetMonGetAvgRSSI());
    tft->setTextColor(COLOR_ACCENT);
    tft->setCursor(6, CONTENT_TOP + 24);
    tft->print(line2);
}

// Bar colour by packet rate: a fixed cool -> hot scale (teal, green,
// yellow, orange, red) so a busy channel reads instantly. Deliberately
// independent of the theme palette -- here the colour is information,
// not decoration.
static uint16_t packetMonRateColor(uint8_t count)
{
    static const uint16_t ramp[5] = { 0x0614, 0x4680, 0xFE80, 0xFA40, 0xF800 };

    uint16_t ratio = (uint16_t)count * 100U / PACKETMON_GRAPH_MAX_PPS;
    if (ratio > 100U) ratio = 100U;

    uint8_t seg = (uint8_t)(ratio / 25U);        // 0..4 -> ramp[seg]..ramp[seg+1]
    if (seg > 3) seg = 3;
    uint16_t t = (uint16_t)(ratio - seg * 25U) * 4U; // 0..96 within the segment
    uint16_t a = ramp[seg];
    uint16_t b = ramp[seg + 1];

    uint16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    uint16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;

    uint16_t r = ((ar * (100 - t)) + (br * t) + 50) / 100;
    uint16_t g = ((ag * (100 - t)) + (bg * t) + 50) / 100;
    uint16_t bl = ((ab * (100 - t)) + (bb * t) + 50) / 100;

    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void drawPacketMonGraph()
{
    tft->fillRect(PACKETMON_GRAPH_X, PACKETMON_GRAPH_Y, PACKETMON_GRAPH_W, PACKETMON_GRAPH_H, COLOR_BG);
    tft->drawRect(PACKETMON_GRAPH_X - 1, PACKETMON_GRAPH_Y - 1,
                  PACKETMON_GRAPH_W + 2, PACKETMON_GRAPH_H + 2, ICON_BORDER_COLOR);

    uint8_t len = packetMonHistoryLen();
    for (uint8_t i = 0; i < len && i < PACKETMON_GRAPH_W; i++)
    {
        uint8_t count = packetMonHistoryAt(i);

        uint16_t barH = ((uint16_t)count * PACKETMON_GRAPH_H) / PACKETMON_GRAPH_MAX_PPS;
        if (barH > PACKETMON_GRAPH_H) barH = PACKETMON_GRAPH_H;
        if (barH == 0) continue;

        int16_t x = PACKETMON_GRAPH_X + i;
        int16_t yTop = PACKETMON_GRAPH_Y + (PACKETMON_GRAPH_H - barH);
        tft->drawFastVLine(x, yTop, barH, packetMonRateColor(count));
    }
}

void drawPacketMon()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "PackMon";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    drawPacketMonStatus();
    drawPacketMonGraph();
}

// =====================================================================
// Files app -- SD card directory browser
// =====================================================================

// Maps a position in the *displayed* list (which has a synthetic ".."
// row prepended whenever we're not at the root) back to a name/type/size.
// Returns false if displayIdx is past the end of the list.
static bool filesGetDisplayEntry(uint8_t displayIdx, const char** outName, bool* outIsDir, uint32_t* outSize)
{
    if (hasParentEntry)
    {
        if (displayIdx == 0)
        {
            *outName = "..";
            *outIsDir = true;
            *outSize = 0;
            return true;
        }
        displayIdx--; // shift past the synthetic ".." row
    }

    if (displayIdx >= fileEntryCount)
    {
        return false;
    }

    *outName = fileEntries[displayIdx].name;
    *outIsDir = fileEntries[displayIdx].isDir;
    *outSize = fileEntries[displayIdx].size;
    return true;
}

// Formats a byte count as a short human-readable string ("482B", "3.4K", "1.2M").
static void formatFileSize(uint32_t size, char* out, size_t outSize)
{
    if (size < 1024)
    {
        snprintf(out, outSize, "%luB", (unsigned long)size);
    }
    else if (size < 1024UL * 1024UL)
    {
        snprintf(out, outSize, "%.1fK", size / 1024.0f);
    }
    else
    {
        snprintf(out, outSize, "%.1fM", size / (1024.0f * 1024.0f));
    }
}

// Breadcrumb path (top) -- the part of the Files screen that doesn't
// change while browsing the same directory.
static void drawFilesChrome()
{
    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);

    char breadcrumb[27];
    size_t pathLen = strlen(currentPath);
    if (pathLen > sizeof(breadcrumb) - 2)
    {
        // Path too long for the screen: show its tail with a leading dot.
        snprintf(breadcrumb, sizeof(breadcrumb), ".%s",
                 currentPath + (pathLen - (sizeof(breadcrumb) - 2)));
    }
    else
    {
        strncpy(breadcrumb, currentPath, sizeof(breadcrumb) - 1);
        breadcrumb[sizeof(breadcrumb) - 1] = '\0';
    }

    int16_t bx = (SCREEN_WIDTH - (int16_t)strlen(breadcrumb) * 6) / 2;
    if (bx < 2) bx = 2;
    tft->setCursor(bx, CONTENT_TOP + 4);
    tft->print(breadcrumb);

    if (!sdReady)
    {
        tft->setTextColor(COLOR_DIM);
        const char* msg = "SD card not found";
        int16_t mx = (SCREEN_WIDTH - (int16_t)strlen(msg) * 6) / 2;
        tft->setCursor(mx, CONTENT_TOP + 40);
        tft->print(msg);
    }
}

// Draws (or redraws) a single visible row of the directory listing.
// rowSlot is the row's position on screen (0..FILES_VISIBLE_ROWS-1); the
// actual list index is filesScrollTop + rowSlot.
static void drawFilesRow(uint8_t rowSlot, bool cursorHere)
{
    int16_t y = FILES_LIST_Y + rowSlot * FILES_ROW_H;
    int16_t rowX = 8;
    int16_t rowW = SCREEN_WIDTH - 16;
    int16_t rowH = FILES_ROW_H - 2;

    tft->fillRect(rowX, y, rowW, rowH, COLOR_BG);

    uint8_t displayIdx = filesScrollTop + rowSlot;
    const char* name;
    bool isDir;
    uint32_t size;
    if (!filesGetDisplayEntry(displayIdx, &name, &isDir, &size))
    {
        return; // nothing to show in this slot
    }

    if (cursorHere)
    {
        tft->fillRect(rowX, y, rowW, rowH, COLOR_SELECT_BG);
        tft->drawRect(rowX, y, rowW, rowH, COLOR_ACCENT_DIM);
    }

    char label[24];
    if (isDir)
    {
        if (strcmp(name, "..") == 0)
        {
            snprintf(label, sizeof(label), "[.. up]");
        }
        else
        {
            snprintf(label, sizeof(label), "%s/", name);
        }
    }
    else
    {
        strncpy(label, name, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
    }

    const uint8_t maxChars = 20;
    if (strlen(label) > maxChars)
    {
        label[maxChars - 1] = '.';
        label[maxChars] = '\0';
    }

    tft->setTextSize(1);
    tft->setTextColor(isDir ? COLOR_ACCENT : (cursorHere ? COLOR_FG : COLOR_DIM));
    tft->setCursor(rowX + 4, y + (rowH - 8) / 2);
    tft->print(label);

    // Right-aligned size, only for files, only if it won't collide with the name.
    if (!isDir)
    {
        char sizeBuf[10];
        formatFileSize(size, sizeBuf, sizeof(sizeBuf));
        int16_t sizeW = (int16_t)strlen(sizeBuf) * 6;
        int16_t szX = rowX + rowW - 4 - sizeW;
        int16_t nameRight = rowX + 4 + (int16_t)strlen(label) * 6 + 4;
        if (szX > nameRight)
        {
            tft->setTextColor(COLOR_DIM);
            tft->setCursor(szX, y + (rowH - 8) / 2);
            tft->print(sizeBuf);
        }
    }
}

// Repaints all currently visible rows -- used after moving the cursor
// or scrolling, without touching the breadcrumb above.
static void refreshFilesRows()
{
    for (uint8_t i = 0; i < FILES_VISIBLE_ROWS; i++)
    {
        drawFilesRow(i, (filesScrollTop + i) == filesCursor);
    }
}

// Full redraw for a *new* directory (freshly entered or backed out of):
// resets the cursor/scroll to the top of the list.
void drawFiles()
{
    clearContent();
    drawStatusBar();
    drawFilesChrome();

    filesCursor = 0;
    filesScrollTop = 0;

    if (sdReady)
    {
        refreshFilesRows();
    }
}

// Redraw used when returning to the Files screen from the file preview,
// for the *same* directory -- keeps the cursor/scroll position.
static void drawFilesReturn()
{
    clearContent();
    drawStatusBar();
    drawFilesChrome();

    if (sdReady)
    {
        refreshFilesRows();
    }
}

// =====================================================================
// File preview -- simple scrollable text viewer
// =====================================================================

// Wraps filePreviewBuffer (already read from SD) into fixed-width lines,
// honoring explicit newlines, up to FILEVIEW_MAX_LINES. Returns true if
// the whole buffer was consumed, or false if it had to stop early
// because FILEVIEW_MAX_LINES was reached first (caller uses this to
// show an accurate "more below" note instead of silently cutting off).
static bool wrapFileBuffer()
{
    fileViewLineCount = 0;
    size_t pos = 0;

    while (pos < filePreviewLen && fileViewLineCount < FILEVIEW_MAX_LINES)
    {
        // Find the end of this line: the next '\n' or end of buffer.
        size_t lineEnd = pos;
        while (lineEnd < filePreviewLen && filePreviewBuffer[lineEnd] != '\n')
        {
            lineEnd++;
        }
        size_t rawLen = lineEnd - pos;

        // Strip a trailing '\r' from Windows-style line endings.
        if (rawLen > 0 && filePreviewBuffer[pos + rawLen - 1] == '\r')
        {
            rawLen--;
        }

        if (rawLen == 0)
        {
            // Blank source line -> exactly one blank display line.
            fileViewLines[fileViewLineCount][0] = '\0';
            fileViewLineCount++;
        }
        else
        {
            // Word-wrap this raw line into one or more display lines.
            size_t segStart = 0;
            while (segStart < rawLen && fileViewLineCount < FILEVIEW_MAX_LINES)
            {
                size_t remaining = rawLen - segStart;
                size_t take = remaining < (FILEVIEW_LINE_LEN - 1) ? remaining : (FILEVIEW_LINE_LEN - 1);

                if (take < remaining)
                {
                    size_t breakAt = take;
                    while (breakAt > 0 && filePreviewBuffer[pos + segStart + breakAt] != ' ')
                    {
                        breakAt--;
                    }
                    if (breakAt > 0) take = breakAt;
                }

                memcpy(fileViewLines[fileViewLineCount], &filePreviewBuffer[pos + segStart], take);
                fileViewLines[fileViewLineCount][take] = '\0';
                fileViewLineCount++;

                segStart += take;
                while (segStart < rawLen && filePreviewBuffer[pos + segStart] == ' ')
                {
                    segStart++;
                }
            }
        }

        pos = lineEnd + 1; // skip past the '\n' (or past the end, if there wasn't one)
    }

    return pos >= filePreviewLen;
}

// Cheap heuristic to tell a binary file from text before trying to wrap
// it: if more than ~15% of a sample of the bytes are NUL/control
// characters (outside tab/CR/LF), treat it as binary rather than
// rendering a screen full of garbage glyphs.
static bool looksBinary(const char* buf, size_t len)
{
    if (len == 0) return false;

    size_t sample = (len < 512) ? len : 512;
    size_t suspicious = 0;

    for (size_t i = 0; i < sample; i++)
    {
        uint8_t c = (uint8_t)buf[i];
        if (c == 0)
        {
            suspicious += 4; // a NUL byte is a very strong binary signal
            continue;
        }
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (c < 0x20 || c == 0x7F) suspicious++;
    }

    return (suspicious * 100) > (sample * 15);
}

// =====================================================================
// BMP image viewer
//
// A self-contained bitmap reader -- no image library. The structure
// follows the usual ESP32 image-viewer example (pre-allocate the line
// buffers once, decode into an RGB565 frame and push whole rows to the
// panel) rather than poking single pixels.
//
// The decoded frame costs 2 bytes per visible pixel (36 KB for a
// full-screen picture) and the source row buffer is reused for every
// row, so a multi-megapixel desktop wallpaper still shows up on the
// little 160x128 panel.
//
// Supported: 1/4/8 bpp paletted, 16 bpp X1R5G5B5, 16/32 bpp bitfields,
// 24 bpp BGR, 32 bpp BGRA, bottom-up (normal) and top-down (negative
// height) files. Compressed (RLE) bitmaps are refused with a message
// instead of being drawn as garbage.
// =====================================================================

struct BmpHeader
{
    int32_t  width;
    int32_t  height;
    uint8_t  bpp;
    bool     topDown;
    bool     bitfields;
    uint32_t dataOffset;
    uint32_t stride;                    // bytes in one padded file row
    uint32_t redMask, greenMask, blueMask;
    uint16_t palette[256];
};

static BmpHeader   bmpInfo;
static char        bmpPath[FB_MAX_PATH_LEN + FB_MAX_NAME_LEN + 2];
static uint8_t     bmpViewingEntry;
static bool        bmpFailed;
static const char *bmpErrorLine1 = "";
static const char *bmpErrorLine2 = "";
static uint16_t    bmpColMap[SCREEN_WIDTH];

static inline uint16_t bmpU16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t bmpU32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t bmpRgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                      ((uint16_t)(g & 0xFC) << 3) |
                      (uint16_t)(b >> 3));
}

// Pulls one colour channel out of a pixel word using its bitfield mask
// and scales it to a full 0..255 range, so 5-bit, 6-bit, 8-bit and even
// 10-bit channels all end up looking right.
static inline uint8_t bmpChannel(uint32_t pixel, uint32_t mask)
{
    if (mask == 0) return 0;

    uint8_t shift = 0;
    while (shift < 32 && ((mask >> shift) & 1u) == 0) shift++;

    uint32_t field = mask >> shift;      // mask moved down to bit 0
    uint8_t  count = 0;
    while (count < 32 && ((field >> count) & 1u) != 0) count++;

    if (count == 0) return 0;

    uint32_t value = (pixel & mask) >> shift;

    if (count >= 8) return (uint8_t)(value >> (count - 8));

    // Fewer than 8 bits: widen to a full byte by repeating the pattern
    // (5-bit 0b11111 becomes 0xFF, not 0x3F), which is what makes a 5-bit
    // green channel look right on the 6-bit-green panel.
    uint32_t out    = value;
    uint8_t  filled = count;

    while (filled < 8)
    {
        uint8_t missing = (uint8_t)(8 - filled);
        uint8_t take    = (count < missing) ? count : missing;
        out = (out << take) | (value >> (count - take));
        filled = (uint8_t)(filled + take);
    }
    return (uint8_t)(out & 0xFFu);
}

// Converts real pixel x of an already-read padded source row to RGB565.
static uint16_t bmpPixel(const uint8_t *row, int32_t x)
{
    switch (bmpInfo.bpp)
    {
    case 24:
    {
        const uint8_t *p = row + (uint32_t)x * 3u;
        return bmpRgb565(p[2], p[1], p[0]);          // stored B, G, R
    }
    case 32:
    {
        const uint8_t *p = row + (uint32_t)x * 4u;
        if (!bmpInfo.bitfields)
        {
            return bmpRgb565(p[2], p[1], p[0]);      // stored B, G, R, A
        }
        uint32_t v = bmpU32(p);
        return bmpRgb565(bmpChannel(v, bmpInfo.redMask),
                         bmpChannel(v, bmpInfo.greenMask),
                         bmpChannel(v, bmpInfo.blueMask));
    }
    case 16:
    {
        uint32_t v = bmpU16(row + (uint32_t)x * 2u);
        return bmpRgb565(bmpChannel(v, bmpInfo.redMask),
                         bmpChannel(v, bmpInfo.greenMask),
                         bmpChannel(v, bmpInfo.blueMask));
    }
    case 8:
        return bmpInfo.palette[row[x]];
    case 4:
    {
        uint8_t pair = row[x >> 1];
        return bmpInfo.palette[(x & 1) ? (uint8_t)(pair & 0x0F) : (uint8_t)(pair >> 4)];
    }
    case 1:
    {
        uint8_t b = row[x >> 3];
        return bmpInfo.palette[(b >> (7 - (x & 7))) & 0x01];
    }
    default:
        break;
    }
    return 0;
}

static bool bmpFail(const char *line1, const char *line2)
{
    bmpErrorLine1 = line1;
    bmpErrorLine2 = line2;
    bmpFailed = true;
    return false;
}

// Reads and validates the header, filling in bmpInfo. Returns false (and
// leaves an explanation behind) when the file cannot be shown.
static bool bmpParseHeader(const char *path)
{
    bmpFailed     = false;
    bmpErrorLine1 = "";
    bmpErrorLine2 = "";

    File f = SD.open(path, FILE_READ);
    if (!f) return bmpFail("Cannot open file", "");

    uint8_t hdr[54];
    bool ok = false;

    do
    {
        uint32_t fileSize = (uint32_t)f.size();
        if (fileSize < sizeof(hdr)) break;
        if (f.read(hdr, sizeof(hdr)) != (int)sizeof(hdr)) break;
        if (hdr[0] != 'B' || hdr[1] != 'M') break;

        uint32_t dataOffset  = bmpU32(&hdr[10]);
        uint32_t dibSize     = bmpU32(&hdr[14]);
        int32_t  w           = (int32_t)bmpU32(&hdr[18]);
        int32_t  h           = (int32_t)bmpU32(&hdr[22]);
        uint16_t planes      = bmpU16(&hdr[26]);
        uint16_t bpp         = bmpU16(&hdr[28]);
        uint32_t compression = bmpU32(&hdr[30]);
        uint32_t colorsUsed  = bmpU32(&hdr[46]);

        if (dibSize < 40) break;                    // old OS/2 core header
        if (planes != 1) break;
        if (bpp != 1 && bpp != 4 && bpp != 8 &&
            bpp != 16 && bpp != 24 && bpp != 32) break;
        if (w <= 0 || w > BMP_MAX_DIM) break;
        if (h == 0 || h > BMP_MAX_DIM || h < -BMP_MAX_DIM) break;

        bool bitfields = false;
        if (compression == 3 || compression == 6)   // BI_BITFIELDS / +alpha
        {
            if (bpp != 16 && bpp != 32) break;
            bitfields = true;
        }
        else if (compression != 0)
        {
            break;                                  // RLE and anything exotic
        }

        int32_t  height = (h < 0) ? -h : h;
        uint32_t stride = (((uint32_t)w * (uint32_t)bpp + 31u) / 32u) * 4u;
        if (stride == 0 || stride > 65535u) break;

        // Prove the pixel block really is inside the file before drawing,
        // and never claim more rows than the file actually holds -- a
        // truncated copy would otherwise read past the end and paint
        // whatever junk comes back.
        if (dataOffset < 14u + dibSize) break;
        if (dataOffset > fileSize) break;

        uint32_t rowsInFile = (fileSize - dataOffset) / stride;
        if (rowsInFile == 0) break;                 // not even one full row
        if ((uint32_t)height > rowsInFile) height = (int32_t)rowsInFile;

        uint32_t rMask, gMask, bMask;
        if (bitfields)
        {
            // Masks sit immediately after the 40-byte header, i.e. byte
            // 54 of the file, for every BI_BITFIELDS bitmap.
            uint8_t m[12];
            if (!f.seek(54) || f.read(m, 12) != 12) break;
            rMask = bmpU32(&m[0]);
            gMask = bmpU32(&m[4]);
            bMask = bmpU32(&m[8]);
            if (rMask == 0 || gMask == 0 || bMask == 0) break;
        }
        else if (bpp == 16)
        {
            rMask = 0x7C00u; gMask = 0x03E0u; bMask = 0x001Fu;  // X1R5G5B5
        }
        else
        {
            rMask = 0xFF0000u; gMask = 0x00FF00u; bMask = 0x0000FFu;
        }

        memset(bmpInfo.palette, 0, sizeof(bmpInfo.palette));
        if (bpp <= 8)
        {
            uint32_t entries = colorsUsed;
            if (entries == 0 || entries > 256u) entries = 1ul << bpp;

            if (!f.seek(14u + dibSize)) break;

            // Entries the file does not actually carry stay black (the
            // table was zeroed above), so a wrong count cannot read past
            // the array.
            uint8_t ent[4];
            for (uint32_t i = 0; i < entries; i++)
            {
                if (f.read(ent, 4) != 4) break;
                bmpInfo.palette[i] = bmpRgb565(ent[2], ent[1], ent[0]);
            }
        }

        bmpInfo.width      = w;
        bmpInfo.height     = height;
        bmpInfo.bpp        = (uint8_t)bpp;
        bmpInfo.topDown    = (h < 0);
        bmpInfo.bitfields  = bitfields;
        bmpInfo.dataOffset = dataOffset;
        bmpInfo.stride     = stride;
        bmpInfo.redMask    = rMask;
        bmpInfo.greenMask  = gMask;
        bmpInfo.blueMask   = bMask;
        ok = true;
    } while (0);

    f.close();

    if (!ok) return bmpFail("Cannot show BMP", "unsupported or damaged");
    return true;
}

static void bmpDrawMessage()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);

    const char *lines[2] = { bmpErrorLine1, bmpErrorLine2 };
    int16_t y = CONTENT_TOP + 42;

    for (uint8_t i = 0; i < 2; i++)
    {
        if (lines[i] == nullptr || lines[i][0] == '\0') continue;

        int16_t x = (int16_t)((SCREEN_WIDTH - (int16_t)strlen(lines[i]) * 6) / 2);
        if (x < 2) x = 2;

        tft->setTextColor(i == 0 ? COLOR_FG : COLOR_DIM);
        tft->setCursor(x, (int16_t)(y + (int16_t)i * 12));
        tft->print(lines[i]);
    }
}

// Decodes the parsed bitmap and draws it: shrunk to fit when bigger than
// the content area, otherwise centred at 1:1.
//
// The picture is decoded into one RGB565 frame buffer first and only then
// sent to the panel, in a single address window with one transfer per row.
// Keeping the SD reads and the panel writes in separate phases means the
// shared SPI bus is never flipped back and forth mid-picture, and the
// pixels leave through the same byte path the rest of the UI uses
// (writeBytes, via writePixels(..., bigEndian = true)), which is the path
// already known to work on this panel. The frame is stored already
// byte-swapped, since that path sends the bytes exactly as they sit in RAM.
// Decodes the parsed bitmap and blits it into the given screen
// rectangle: shrunk to fit when bigger, otherwise centred at 1:1. The
// caller is responsible for clearing/backfilling the surroundings.
// Returns false when the picture could not be prepared or read at all.
static bool bmpBlit(int16_t areaX, int16_t areaY, int16_t areaW, int16_t areaH)
{
    if (bmpFailed) return false;

    int16_t outW = (int16_t)bmpInfo.width;
    int16_t outH = (int16_t)bmpInfo.height;

    if (outW > areaW || outH > areaH)
    {
        // 8.8 fixed-point scale factor; never less than 1:1 (256).
        uint32_t sx = ((uint32_t)bmpInfo.width  << 8) / (uint32_t)areaW;
        uint32_t sy = ((uint32_t)bmpInfo.height << 8) / (uint32_t)areaH;
        uint32_t scale = (sx > sy) ? sx : sy;
        if (scale < 256u) scale = 256u;

        outW = (int16_t)(((uint32_t)bmpInfo.width  << 8) / scale);
        outH = (int16_t)(((uint32_t)bmpInfo.height << 8) / scale);
        if (outW < 1) outW = 1;
        if (outH < 1) outH = 1;
        if (outW > areaW) outW = areaW;
        if (outH > areaH) outH = areaH;
    }

    int16_t outX = (int16_t)(areaX + (areaW - outW) / 2);
    int16_t outY = (int16_t)(areaY + (areaH - outH) / 2);

    // Column lookup: which source column each screen column shows.
    for (int16_t ox = 0; ox < outW; ox++)
    {
        int32_t srcX = (int32_t)((uint32_t)ox * (uint32_t)bmpInfo.width / (uint32_t)outW);
        if (srcX >= bmpInfo.width) srcX = bmpInfo.width - 1;
        bmpColMap[ox] = (uint16_t)srcX;
    }

    File f = SD.open(bmpPath, FILE_READ);
    if (!f)
    {
        bmpFail("Cannot open file", "");
        return false;
    }

    uint8_t  *row   = (uint8_t *)malloc(bmpInfo.stride);
    uint16_t *frame = (uint16_t *)malloc((size_t)outW * (size_t)outH * sizeof(uint16_t));

    if (row == nullptr || frame == nullptr)
    {
        free(row);
        free(frame);
        f.close();
        bmpFail("Out of memory", "image too large");
        return false;
    }

    bool anyRow = false;

    for (int16_t oy = 0; oy < outH; oy++)
    {
        uint16_t *dst = frame + (uint32_t)oy * (uint32_t)outW;

        // Nearest-neighbour pick of the source row for this screen row.
        int32_t srcY = (int32_t)((int32_t)oy * bmpInfo.height / outH);
        if (srcY >= bmpInfo.height) srcY = bmpInfo.height - 1;

        // Bottom-up files (the normal case) store the last row first.
        int32_t fileRow = bmpInfo.topDown ? srcY : (bmpInfo.height - 1 - srcY);

        uint32_t pos = bmpInfo.dataOffset + (uint32_t)fileRow * bmpInfo.stride;
        bool haveRow = false;

        if (f.seek(pos))
        {
            size_t got = f.read(row, bmpInfo.stride);
            if (got > 0)
            {
                // Short row at the end of a clipped file: pad with black
                // instead of leaving stale bytes from the previous row.
                if (got < bmpInfo.stride) memset(row + got, 0, bmpInfo.stride - got);
                haveRow = true;
                anyRow  = true;
            }
        }

        if (!haveRow)
        {
            // A row that cannot be read shows as black rather than as a
            // half-drawn mess.
            memset(dst, 0, (size_t)outW * sizeof(uint16_t));
            continue;
        }

        for (int16_t ox = 0; ox < outW; ox++)
        {
            dst[ox] = __builtin_bswap16(bmpPixel(row, (int32_t)bmpColMap[ox]));
        }
    }

    free(row);
    f.close();

    if (!anyRow)
    {
        free(frame);
        bmpFail("Cannot read image", "file looks incomplete");
        return false;
    }

    // One window for the whole picture, then the rows in order. Nothing
    // else touches the SPI bus until the last row is out.
    tft->startWrite();
    tft->setAddrWindow((uint16_t)outX, (uint16_t)outY, (uint16_t)outW, (uint16_t)outH);

    for (int16_t oy = 0; oy < outH; oy++)
    {
        tft->writePixels(frame + (uint32_t)oy * (uint32_t)outW, (uint32_t)outW, true, true);
    }

    tft->endWrite();

    free(frame);
    return true;
}

// BMP viewer screen: the picture fills the content area under the status
// bar, or an explanation is shown when it cannot be drawn.
static void drawBmpView()
{
    clearContent();
    drawStatusBar();

    if (!bmpBlit(0, CONTENT_TOP, SCREEN_WIDTH, SCREEN_HEIGHT - CONTENT_TOP))
    {
        bmpDrawMessage();
    }
}

// Draws the given .bmp across the entire panel. Used by the boot splash,
// which has no status bar. Returns false when the file cannot be shown
// (the caller then falls back to the built-in logo).
static bool bmpShowFullScreen(const char* path)
{
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(bmpPath)) return false;

    memcpy(bmpPath, path, len + 1);

    if (!bmpParseHeader(bmpPath)) return false;

    tft->fillScreen(COLOR_BG);
    return bmpBlit(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
}

// Opens fileEntries[realIdx] in the viewer. A file that cannot be shown
// still switches into BMPVIEW, so it shows a clear message and KEY_BACK
// takes the user back to the file list.
static void openBmpView(uint8_t realIdx)
{
    if (realIdx >= fileEntryCount) return;

    const char *name = fileEntries[realIdx].name;
    if (currentPath[1] == '\0')
        snprintf(bmpPath, sizeof(bmpPath), "/%s", name);
    else
        snprintf(bmpPath, sizeof(bmpPath), "%s/%s", currentPath, name);

    bmpViewingEntry = realIdx;
    bmpParseHeader(bmpPath);

    currentState = BMPVIEW;
    drawBmpView();
}

// Reads fileEntries[realIdx] from SD and switches into the FILEVIEW state.
static void openFilePreview(uint8_t realIdx)
{
    if (realIdx >= fileEntryCount)
    {
        return;
    }

    const char* name = fileEntries[realIdx].name;
    size_t len = strlen(name);
    if (len >= 4)
    {
        const char* ext = name + len - 4;
        if (strcasecmp(ext, ".bmp") == 0)
        {
            openBmpView(realIdx);
            return;
        }
    }

    size_t bytesRead = 0;
    bool ok = filesReadPreview(realIdx, filePreviewBuffer, sizeof(filePreviewBuffer), &bytesRead);
    (void)ok; // bytesRead is authoritative even on a partial/failed read -- see filesReadPreview()
    filePreviewLen = bytesRead;

    filesViewingEntry = realIdx;
    filePreviewLineOffset = 0;

    filePreviewIsBinary = (filePreviewLen > 0) && looksBinary(filePreviewBuffer, filePreviewLen);

    // The file on the SD card can be bigger than what we read into the
    // fixed-size preview buffer -- flag that so drawFileView() can say
    // so plainly instead of the preview just quietly stopping partway
    // through, which is what used to read as "broken" on larger files.
    filePreviewBufferTruncated = (fileEntries[realIdx].size > (uint32_t)filePreviewLen);

    if (filePreviewIsBinary)
    {
        fileViewLineCount = 0;
        filePreviewLinesTruncated = false;
    }
    else
    {
        filePreviewLinesTruncated = !wrapFileBuffer();
    }

    currentState = FILEVIEW;
    drawFileView();
}

void drawFileView()
{
    // Text preview owns the whole panel: no status bar, no filename line,
    // so as much of the file as possible is on screen. Back returns to
    // the Files list, which repaints its own chrome.
    tft->fillScreen(COLOR_BG);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    if (fileViewLineCount == 0)
    {
        // Distinguish *why* there's nothing to show, rather than one
        // ambiguous catch-all message -- an actually-empty file, a
        // binary file we deliberately didn't try to render as text, and
        // a genuine read failure (SD hiccup, file vanished, etc.) are
        // three different situations and look different to the user.
        const char* empty;
        if (filePreviewLen == 0 && fileEntries[filesViewingEntry].size == 0)
        {
            empty = "(empty file)";
        }
        else if (filePreviewIsBinary)
        {
            empty = "(binary file - no preview)";
        }
        else
        {
            empty = "(could not read file)";
        }

        int16_t ex = (SCREEN_WIDTH - (int16_t)strlen(empty) * 6) / 2;
        if (ex < 2) ex = 2;
        tft->setCursor(ex, SCREEN_HEIGHT / 2 - 4);
        tft->print(empty);
    }
    else
    {
        // 26 characters per line at 6px each, 2px left margin -- the
        // whole 160px width is used, starting at the very top row.
        for (uint8_t i = 0; i < FILEVIEW_VISIBLE_LINES; i++)
        {
            uint8_t lineIdx = filePreviewLineOffset + i;
            if (lineIdx >= fileViewLineCount) break;
            tft->setCursor(2, 2 + i * FILEVIEW_LINE_H);
            tft->print(fileViewLines[lineIdx]);
        }

        // Say plainly when we're only showing part of the file, instead
        // of the preview silently stopping -- this is the main thing
        // that used to look broken on files with a lot of content.
        if (filePreviewBufferTruncated)
        {
            char note[36];
            snprintf(note, sizeof(note), "Showing %lu of %lu bytes",
                     (unsigned long)filePreviewLen,
                     (unsigned long)fileEntries[filesViewingEntry].size);
            tft->setTextColor(COLOR_DIM);
            tft->setCursor(4, SCREEN_HEIGHT - 9);
            tft->print(note);
        }
        else if (filePreviewLinesTruncated)
        {
            char note[36];
            snprintf(note, sizeof(note), "Showing first %u lines", (unsigned)FILEVIEW_MAX_LINES);
            tft->setTextColor(COLOR_DIM);
            tft->setCursor(4, SCREEN_HEIGHT - 9);
            tft->print(note);
        }
    }
}

// =====================================================================
// Shared drawing helpers
// =====================================================================

void clearContent()
{
    tft->fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_HEIGHT, COLOR_BG);
}

// Blits a real RGB565 PROGMEM bitmap, pixel by pixel. This is the code
// path that activates automatically once icon bitmaps in Icons.h are
// filled with real data and ICONS_HAVE_DATA is set to 1 in Config.h.
void drawBitmapIcon(int16_t x, int16_t y, const uint16_t* bitmap, uint8_t w, uint8_t h)
{
    for (uint8_t row = 0; row < h; row++)
    {
        for (uint8_t col = 0; col < w; col++)
        {
            uint16_t color = pgm_read_word(&bitmap[(uint32_t)row * w + col]);
            tft->drawPixel(x + col, y + row, color);
        }
    }
}

// Public icon-drawing entry point used everywhere in the UI. Falls back
// to a lightweight placeholder glyph while an icon's bitmap is empty, so
// the interface still looks intentional rather than broken/blank.
void drawIcon(int16_t x, int16_t y, const uint16_t* icon, uint8_t w, uint8_t h)
{
#if ICONS_HAVE_DATA
    drawBitmapIcon(x, y, icon, w, h);
#else
    (void)icon; // bitmap not yet populated -- draw placeholder instead
    tft->drawRoundRect(x, y, w, h, min(4, (int)(w / 4)), ICON_BORDER_COLOR);
    tft->drawLine(x, y, x + w - 1, y + h - 1, COLOR_DIM);
    tft->drawLine(x + w - 1, y, x, y + h - 1, COLOR_DIM);
#endif
}

// =====================================================================
// Auto sleep -- waking the panel back up
// =====================================================================

// Repaints whichever screen is currently open. The display is fully
// switched off while asleep, so after waking it has to be drawn again
// from scratch -- the right screen for whatever app was open when the
// timeout fired, not a jump back to the menu.
static void redrawCurrentScreen()
{
    switch (currentState)
    {
    case BOOT:      drawBoot();        break;
    case MENU:      drawMenu(true);    break;
    case APP:       drawApp();         break;
    case KEYTEST:   drawKeyTest();     break;
    case SETTINGS:
        if (settingsSub == SETTINGS_SUB_THEME)       drawThemePicker();
        else if (settingsSub == SETTINGS_SUB_SPLASH) drawSplashPicker();
        else if (settingsSub == SETTINGS_SUB_SLEEP)  drawSleepPicker();
        else                                         drawSettings();
        break;
    case FILES:     drawFiles();       break;
    case FILEVIEW:  drawFileView();    break;
    case MINIPIANO: drawMiniPiano();   break;
    case BREAKOUT:  drawBreakout();    break;
    case BMPVIEW:   drawBmpView();     break;
    case PACKETMON: drawPacketMon();   break;
    case WIFISCAN:  drawWifiScanner(); break;
    case STOCKWATCH: drawStockWatch(); break;
    case CALCULATOR: drawCalculator(); break;
    case MATRIX:    drawMatrix();      break;
    case GAME2048:  drawGame2048();    break;
    case BLEKEY:    drawBleKeyboard(); break;
    case BLEKB:     drawBleKeyboardView(); break;
    case BLEREMOTE: drawBleRemote();   break;
    default:        drawMenu(true);    break;
    }
}
