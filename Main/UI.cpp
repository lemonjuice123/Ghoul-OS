#include <math.h>
#include <string.h>
#include <stdio.h>
#include <SD.h>
#include "UI.h"
#include "Config.h"
#include "Icons.h"
#include "FileBrowser.h"
#include "Audio.h"
#include "BLEKeyboard.h"
#include "PacketMon.h"

// =====================================================================
// Ghoul OS - UI.cpp
//
// Handles the boot splash, the two-pane launcher (left = selected app's
// icon + description, right = animated scrolling list of app names with
// a scrollbar) and the generic app placeholder screen. Redraws are kept
// partial wherever possible to avoid flicker: only the regions that
// actually changed are cleared and repainted.
// =====================================================================

// ------------------------------------------------------------------
// Module state
// ------------------------------------------------------------------
static Adafruit_ST7735 *tft = nullptr;

UIState currentState = BOOT;
int8_t currentAppIndex = 0; // app shown while in APP state

static int8_t selectedIndex = 0;  // confirmed/target selection in the menu
static int8_t lastDrawnLeftIndex = -1; // avoids redrawing left panel needlessly

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

// ----------------------------- File preview -----------------------------
#define FILEVIEW_MAX_LINES 40
#define FILEVIEW_LINE_LEN  27

static char filePreviewBuffer[FB_PREVIEW_BUF_SIZE + 1];
static size_t filePreviewLen = 0;
static uint8_t filesViewingEntry = 0; // index into fileEntries[] being previewed
static char fileViewLines[FILEVIEW_MAX_LINES][FILEVIEW_LINE_LEN];
static uint8_t fileViewLineCount = 0;
static uint8_t filePreviewLineOffset = 0; // which wrapped line is scrolled to the top

// ----------------------------- BMP viewer ---------------------------------
#define BMP_BATCH 16
static int32_t  bmpWidth, bmpHeight;
static uint16_t bmpBpp;
static uint32_t bmpDataOffset;
static uint32_t bmpRowStride;
static bool     bmpTopDown;
static char     bmpFilePath[FB_MAX_PATH_LEN + FB_MAX_NAME_LEN + 2];
static uint8_t  bmpViewingEntry;
static uint16_t bmpSrcLine[BMP_LINE_MAX];
static uint8_t  bmpRowBuf[BMP_LINE_MAX * 4];
static uint16_t *bmpBatchBuf = nullptr;

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

// ----------------------------- BLECMD state -------------------------------
#define BLE_ACT_KEY      0
#define BLE_ACT_CONSUMER 1
#define BLE_ACT_STRING   2

struct BleAction {
    const char* label;
    uint8_t type;
    uint8_t modifiers;
    uint8_t keyCode;
    uint16_t usageCode;
    const char* str;
};

static const BleAction bleActions[] = {
    { "Volume Up",    BLE_ACT_CONSUMER, 0,    0,    0x00E9, nullptr },
    { "Volume Down",  BLE_ACT_CONSUMER, 0,    0,    0x00EA, nullptr },
    { "Mute",         BLE_ACT_CONSUMER, 0,    0,    0x00E2, nullptr },
    { "Type: haha..", BLE_ACT_STRING,   0,    0,    0,      "haha got your keyboard" },
    { "Close Window", BLE_ACT_KEY,      0x04, 0x3E, 0,      nullptr },
    { "Copy",         BLE_ACT_KEY,      0x01, 0x06, 0,      nullptr },
    { "Paste",        BLE_ACT_KEY,      0x01, 0x19, 0,      nullptr },
    { "Tab",          BLE_ACT_KEY,      0,    0x2B, 0,      nullptr },
    { "Escape",       BLE_ACT_KEY,      0,    0x29, 0,      nullptr },
    { "Enter",        BLE_ACT_KEY,      0,    0x28, 0,      nullptr },
};

#define BLECMD_ACTION_COUNT (sizeof(bleActions) / sizeof(bleActions[0]))

static uint8_t blecmdCursor = 0;
static uint8_t blecmdScrollTop = 0;
static bool blecmdTyping = false;
static bool blecmdTypeReleasePending = false;
static const char* blecmdTypeStr = nullptr;
static uint8_t blecmdTypeIdx = 0;
static unsigned long blecmdTypeLastTime = 0;
#define BLECMD_TYPE_PRESS_MS  10
#define BLECMD_TYPE_INTERVAL  80

static bool blecmdLastConnected = false;

// ----------------------------- BT Scanner state ----------------------------
static uint8_t btCursor = 0;
static uint8_t btScrollTop = 0;

// Forward declarations for internal helpers (not part of the public UI.h API)
static void drawLeftPanel(bool fullRedraw);
static void drawScrollbar();
static void startCarouselAnimation();
static void wrapDescriptionText(const char* text, uint8_t maxCharsPerLine,
                                 char lines[][32], uint8_t maxLines, uint8_t* lineCount);
static void drawKeyTestCell(uint8_t row, uint8_t col, bool highlighted);
static void handleKeyTestPress(char key);
static void drawMusicCell(uint8_t row, uint8_t col, bool highlighted);
static void handleMusicKeyPress(char key);
static void drawAudioToggleRow();
static bool filesGetDisplayEntry(uint8_t displayIdx, const char** outName, bool* outIsDir, uint32_t* outSize);
static void formatFileSize(uint32_t size, char* out, size_t outSize);
static void drawFilesChrome();
static void drawFilesRow(uint8_t rowSlot, bool cursorHere);
static void refreshFilesRows();
static void drawFilesReturn();
static void openFilePreview(uint8_t realIdx);
static void wrapFileBuffer();
static void breakoutInit();
static void breakoutTick();
static void breakoutHandleKey(char key);
static void drawBLECMDStatus();
static void handleBLECMDKey(char key);
static uint8_t blecmdKeyToHID(char key);
static uint8_t charToHIDKey(char c);
static void blecmdExecuteAction(uint8_t index);
static void drawBLECMDRow(uint8_t rowSlot, bool cursorHere);
static void drawBLECMDList();
static void drawBTScannerStatus();
static void drawBTScannerList();
static void drawPacketMonStatus();
static void drawPacketMonGraph();

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
    // Short audio click on every navigation press, the same way small
    // handheld devices (e.g. M5Stick) give audio feedback. Does nothing
    // if audio feedback is currently disabled in Settings.
    if (currentState != BLECMD &&
        (key == KEY_UP || key == KEY_DOWN || key == KEY_SELECT || key == KEY_BACK))
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
            else if (appList[currentAppIndex].launch == launchBLECMD)
            {
                currentState = BLECMD;
                blecmdCursor = 0;
                blecmdScrollTop = 0;
                blecmdTyping = false;
                blecmdTypeReleasePending = false;
                bleKeyboardInit();
                drawBLECMD();
            }
            else if (appList[currentAppIndex].launch == launchBluetooth)
            {
                currentState = BTSCANNER;
                btCursor = 0;
                btScrollTop = 0;
                btScannerInit();
                drawBTScanner();
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
        if (key == KEY_BACK)
        {
            currentState = MENU;
            drawMenu(true);
        }
        else if (key == KEY_SELECT)
        {
            audioSetEnabled(!audioIsEnabled());
            drawAudioToggleRow();
        }
        // KEY_UP/KEY_DOWN intentionally do nothing -- there's only one
        // setting here, so there's nothing to navigate between.
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
    else if (currentState == BLECMD)
    {
        if (key == KEY_BACK)
        {
            bleKeyboardDeinit();
            currentState = MENU;
            drawMenu(true);
        }
        else
        {
            handleBLECMDKey(key);
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
    else if (currentState == BTSCANNER)
    {
        if (key == KEY_BACK)
        {
            btScannerDisconnect();
            btScannerDeinit();
            currentState = MENU;
            drawMenu(true);
        }
        else if (key == KEY_UP)
        {
            if (btCursor > 0)
            {
                btCursor--;
                if (btCursor < btScrollTop) btScrollTop = btCursor;
                drawBTScannerList();
            }
        }
        else if (key == KEY_DOWN)
        {
            uint8_t count = btScannerDeviceCount();
            if (count > 0 && btCursor < count - 1)
            {
                btCursor++;
                if (btCursor >= btScrollTop + BT_VISIBLE_ROWS)
                    btScrollTop = btCursor - BT_VISIBLE_ROWS + 1;
                drawBTScannerList();
            }
        }
        else if (key == KEY_SELECT)
        {
            if (!btScannerIsConnected() && btScannerDeviceCount() > 0)
            {
                tft->setTextSize(1);
                tft->setTextColor(COLOR_FG);
                const char* msg = "Connecting...";
                tft->fillRect(0, CONTENT_TOP + 12, SCREEN_WIDTH, 12, COLOR_BG);
                int16_t mx = (SCREEN_WIDTH - (int16_t)strlen(msg) * 6) / 2;
                tft->setCursor(mx, CONTENT_TOP + 14);
                tft->print(msg);

                btScannerConnect(btCursor);
                drawBTScannerStatus();
                drawBTScannerList();
            }
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
            currentState = FILES;
            drawFilesReturn();
        }
    }
}

void uiTick()
{
    unsigned long now = millis();

    if (currentState == BOOT)
    {
        if (now - bootStartTime >= BOOT_SPLASH_DURATION_MS)
        {
            currentState = MENU;
            drawMenu(true);
        }
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
    else if (currentState == BLECMD)
    {
        if (bleKeyboardIsConnected() != blecmdLastConnected)
        {
            drawBLECMDStatus();
        }

        if (blecmdTyping)
        {
            if (blecmdTypeReleasePending)
            {
                if (now - blecmdTypeLastTime >= BLECMD_TYPE_PRESS_MS)
                {
                    bleKeyboardReleaseKey();
                    blecmdTypeReleasePending = false;
                    blecmdTypeIdx++;
                    blecmdTypeLastTime = now;
                }
            }
            else if (now - blecmdTypeLastTime >= BLECMD_TYPE_INTERVAL)
            {
                blecmdTypeLastTime = now;
                if (blecmdTypeStr == nullptr || blecmdTypeStr[blecmdTypeIdx] == '\0')
                {
                    blecmdTyping = false;
                    drawBLECMDStatus();
                }
                else
                {
                    uint8_t hidCode = charToHIDKey(blecmdTypeStr[blecmdTypeIdx]);
                    if (hidCode != 0)
                    {
                        bleKeyboardSendKey(0, hidCode);
                        blecmdTypeReleasePending = true;
                    }
                    else
                    {
                        blecmdTypeIdx++;
                    }
                }
            }
        }
    }
    else if (currentState == BTSCANNER)
    {
        static unsigned long lastBtRefresh = 0;
        if (now - lastBtRefresh >= 1000)
        {
            lastBtRefresh = now;
            drawBTScannerStatus();
            drawBTScannerList();
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
}

// =====================================================================
// Boot screen
// =====================================================================

void drawBoot()
{
    tft->fillScreen(COLOR_BG);

    // Full-screen splash bitmap (currently an empty placeholder, see
    // Icons.h). drawIcon() safely falls back to a vector placeholder
    // when ICONS_HAVE_DATA is 0.
    drawIcon(0, 0, bootLogo, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Branding text drawn on top so the splash still looks intentional
    // while the real logo bitmap is empty.
    
    

   
    
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

    // Hollow-box status indicators: filled blue when active, dim outline when off.
    uint8_t sz = STATUSBAR_ICON_SIZE - 2;
    int16_t iy = (STATUSBAR_HEIGHT - sz) / 2;
    int16_t x = SCREEN_WIDTH - 4 - sz;

    // Battery (no sensor -- always dim hollow)
    tft->drawRoundRect(x, iy, sz, sz, 1, COLOR_DIM);
    x -= (sz + 4);

    // WiFi (no stack active yet -- always dim hollow)
    tft->drawRoundRect(x, iy, sz, sz, 1, COLOR_DIM);
    x -= (sz + 4);

    // Bluetooth (filled when BLE keyboard or scanner is active)
    uint16_t btColor = (bleKeyboardIsActive() || btScannerIsActive()) ? COLOR_ACCENT : COLOR_DIM;
    tft->drawRoundRect(x, iy, sz, sz, 1, btColor);
    if (bleKeyboardIsActive() || btScannerIsActive())
    {
        tft->fillRoundRect(x + 1, iy + 1, sz - 2, sz - 2, 1, btColor);
    }
    x -= (sz + 4);

    // SD card (filled when card is present and ready)
    uint16_t sdColor = sdReady ? COLOR_ACCENT : COLOR_DIM;
    tft->drawRoundRect(x, iy, sz, sz, 1, sdColor);
    if (sdReady)
    {
        tft->fillRoundRect(x + 1, iy + 1, sz - 2, sz - 2, 1, sdColor);
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
        lastDrawnLeftIndex = -1; // force the left panel to redraw
    }

    drawLeftPanel(fullRedraw);
    drawCarousel(fullRedraw);
}

// --- Left panel: big icon and description of the selected app ---
static void drawLeftPanel(bool fullRedraw)
{
    if (!fullRedraw && lastDrawnLeftIndex == selectedIndex)
    {
        return;
    }

    lastDrawnLeftIndex = selectedIndex;

    tft->fillRect(
        LEFT_PANEL_X,
        CONTENT_TOP,
        LEFT_PANEL_W,
        CONTENT_HEIGHT,
        COLOR_BG
    );

    drawDescription();
}

void drawDescription()
{
    const App &app = appList[selectedIndex];

    // Fill the entire left panel.
    // Leave exactly 1 pixel below the status bar
    // and 4 pixels above the bottom.

    int16_t boxX = LEFT_PANEL_X;
    int16_t boxY = CONTENT_TOP + 1;
    int16_t boxW = LEFT_PANEL_W;
    int16_t boxH = CONTENT_HEIGHT - 5;

    tft->drawRoundRect(boxX, boxY,
                       boxW, boxH,
                       ICON_BORDER_RADIUS,
                       ICON_BORDER_COLOR);

    uint8_t maxCharsPerLine = (boxW - 8) / 6;
    if (maxCharsPerLine < 4)
        maxCharsPerLine = 4;

    char lines[12][32];
    uint8_t lineCount = 0;

    wrapDescriptionText(app.description,
                        maxCharsPerLine,
                        lines,
                        12,
                        &lineCount);

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);

    int16_t y = boxY + 6;

    // Draw title
    tft->setCursor(boxX + 5, y);
    tft->print(app.name);

    y += 12;

    // Divider
    tft->drawFastHLine(boxX + 3,
                       y,
                       boxW - 6,
                       COLOR_DIM);

    y += 6;

    // Description
    tft->setTextColor(COLOR_DIM);

    for(uint8_t i = 0; i < lineCount; i++)
    {
        if(y + 8 > boxY + boxH - 4)
            break;

        tft->setCursor(boxX + 5, y);
        tft->print(lines[i]);
        y += 9;
    }
}

// Simple greedy word-wrap into a fixed-size buffer of lines.
static void wrapDescriptionText(const char* text, uint8_t maxCharsPerLine,
                                 char lines[][32], uint8_t maxLines, uint8_t* lineCount)
{
    *lineCount = 0;
    size_t textLen = strlen(text);
    size_t pos = 0;

    while (pos < textLen && *lineCount < maxLines)
    {
        size_t remaining = textLen - pos;
        size_t take = remaining < maxCharsPerLine ? remaining : maxCharsPerLine;

        // Try to break on a space so words are not split mid-word.
        if (take < remaining)
        {
            size_t breakAt = take;
            while (breakAt > 0 && text[pos + breakAt] != ' ')
            {
                breakAt--;
            }
            if (breakAt > 0)
            {
                take = breakAt;
            }
        }

        size_t copyLen = take < 31 ? take : 31;
        memcpy(lines[*lineCount], &text[pos], copyLen);
        lines[*lineCount][copyLen] = '\0';
        (*lineCount)++;

        pos += take;
        while (pos < textLen && text[pos] == ' ')
        {
            pos++; // skip the space we broke on
        }
    }
}

// --- Right panel: animated vertical list of app names -----------------
void drawCarousel(bool fullRedraw)
{
    (void)fullRedraw; // the carousel always repaints just its own panel

    // Partial redraw: clear only the right panel, never the full screen.
    tft->fillRect(RIGHT_PANEL_X, CONTENT_TOP, RIGHT_PANEL_W, CONTENT_HEIGHT, COLOR_BG);

    // Fixed highlight box behind the centered (selected) row. It stays
    // put while the list slides underneath it -- same idea as the old
    // dot carousel's ring, just now sized to hold a line of text.
    drawSelection();

    float centerY = CONTENT_TOP + CONTENT_HEIGHT / 2.0f;

    // Reserve room for the scrollbar + a gap so text never runs under it.
    int16_t textAreaX = RIGHT_PANEL_X + 3;
    int16_t textAreaRight = SCREEN_WIDTH - SCROLLBAR_WIDTH - SCROLLBAR_GAP - 2;
    uint8_t maxChars = (uint8_t)((textAreaRight - textAreaX) / 6);
    if (maxChars < 1) maxChars = 1;
    if (maxChars > 15) maxChars = 15;

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
        // and left to smear, since the right-panel clear above only
        // touches the content area, never the status bar itself.
        if (y - 4 < CONTENT_TOP || y > SCREEN_HEIGHT - 2)
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

        tft->setTextSize(1);
        tft->setTextColor(color);
        tft->setCursor(textAreaX, (int16_t)y - 3);
        tft->print(label);
    }

    drawScrollbar();

    // Redraw the status bar on top last. This guarantees any row that
    // slides close to the boundary is hidden behind it rather than
    // showing as a stray blob -- belt-and-braces alongside the clip
    // check above.
    drawStatusBar();
}

// Fixed highlight box behind the centered/selected row in the list.
void drawSelection()
{
    float centerY = CONTENT_TOP + CONTENT_HEIGHT / 2.0f;

    int16_t boxX = RIGHT_PANEL_X;
    int16_t boxW = RIGHT_PANEL_W - SCROLLBAR_WIDTH - SCROLLBAR_GAP;
    int16_t boxH = CAROUSEL_ITEM_H - 2;
    int16_t boxY = (int16_t)(centerY - boxH / 2.0f);

    tft->fillRoundRect(boxX, boxY, boxW, boxH, 3, COLOR_SELECT_BG);
    tft->drawRoundRect(boxX, boxY, boxW, boxH, 3, COLOR_ACCENT_DIM);
}

// Vertical scrollbar along the rightmost edge showing progress through
// the full app list.
static void drawScrollbar()
{
    int16_t trackX = SCREEN_WIDTH - SCROLLBAR_WIDTH - 1;
    int16_t trackY = CONTENT_TOP + 2;
    int16_t trackH = CONTENT_HEIGHT - 4;

    tft->drawFastVLine(trackX + SCROLLBAR_WIDTH / 2, trackY, trackH, COLOR_DIM);

    int16_t thumbH = trackH / (int16_t)APP_COUNT;
    if (thumbH < 6) thumbH = 6;
    if (thumbH > trackH) thumbH = trackH;

    float progress = (APP_COUNT > 1) ? (displayIndex / (float)(APP_COUNT - 1)) : 0.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    int16_t thumbY = trackY + (int16_t)(progress * (trackH - thumbH));

    tft->fillRoundRect(trackX, thumbY, SCROLLBAR_WIDTH, thumbH, 1, COLOR_ACCENT);
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

    // The left panel (icon/description) updates immediately so it
    // reads correctly for the whole duration of the list's slide.
    drawLeftPanel(false);
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
        tft->setTextSize(2);
        tft->setTextColor(COLOR_FG);
        const char* title = "GhoulOS";
        int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 12) / 2;
        if (tx < 2) tx = 2;
        tft->setCursor(tx, CONTENT_TOP + 10);
        tft->print(title);

        tft->setTextSize(1);
        tft->setTextColor(COLOR_DIM);
        int16_t y = CONTENT_TOP + 34;
        tft->setCursor(8, y);  tft->print("Platform: ESP32");       y += 10;
        tft->setCursor(8, y);  tft->print("Display: ST7735 160x128"); y += 10;
        tft->setCursor(8, y);  tft->print("Input: 4x4 Matrix Pad"); y += 10;

        char heapBuf[28];
        snprintf(heapBuf, sizeof(heapBuf), "Free heap: %lu B",
                 (unsigned long)ESP.getFreeHeap());
        tft->setCursor(8, y);  tft->print(heapBuf);                 y += 10;

        uint16_t umins = (secondsSinceBoot / 60) % 100;
        uint16_t usecs = secondsSinceBoot % 60;
        char upBuf[20];
        snprintf(upBuf, sizeof(upBuf), "Uptime: %02u:%02u", umins, usecs);
        tft->setCursor(8, y);  tft->print(upBuf);

        tft->setTextColor(COLOR_DIM);
        char hint[16];
        snprintf(hint, sizeof(hint), "%c = Back", KEY_BACK);
        int16_t hintX = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
        tft->setCursor(hintX, SCREEN_HEIGHT - 10);
        tft->print(hint);
        return;
    }

    // Generic placeholder screen for every other app.

    // Large centered icon.
    int16_t iconX = (SCREEN_WIDTH - APP_SCREEN_ICON_SIZE) / 2;
    int16_t iconY = CONTENT_TOP + 10;
    drawIcon(iconX, iconY, app.icon, APP_SCREEN_ICON_SIZE, APP_SCREEN_ICON_SIZE);

    // App title, centered.
    tft->setTextSize(2);
    tft->setTextColor(COLOR_FG);
    int16_t titleY = iconY + APP_SCREEN_ICON_SIZE + 8;
    int16_t titleX = (SCREEN_WIDTH - (int16_t)strlen(app.name) * 12) / 2;
    if (titleX < 2) titleX = 2;
    tft->setCursor(titleX, titleY);
    tft->print(app.name);

    // "Coming Soon" caption, centered.
    tft->setTextSize(1);
    tft->setTextColor(COLOR_ACCENT);
    const char* caption = "Coming Soon";
    int16_t capX = (SCREEN_WIDTH - (int16_t)strlen(caption) * 6) / 2;
    tft->setCursor(capX, titleY + 20);
    tft->print(caption);

    // Back hint at the bottom of the screen.
    tft->setTextColor(COLOR_DIM);
    char hint[16];
    snprintf(hint, sizeof(hint), "%c = Back", KEY_BACK);
    int16_t hintX = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hintX, SCREEN_HEIGHT - 10);
    tft->print(hint);
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

    tft->fillRoundRect(x, y, w, h, 3, fillColor);
    tft->drawRoundRect(x, y, w, h, 3, borderColor);

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

    tft->setTextColor(COLOR_DIM);
    char hint[28];
    snprintf(hint, sizeof(hint), "%c = Back (not shown above)", KEY_BACK);
    int16_t hintX = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    if (hintX < 2) hintX = 2;
    tft->setCursor(hintX, SCREEN_HEIGHT - 10);
    tft->print(hint);
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

    tft->fillRoundRect(x, y, w, h, 3, fillColor);
    tft->drawRoundRect(x, y, w, h, 3, borderColor);

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

    tft->setTextColor(COLOR_DIM);
    char hint[30];
    if (audioIsEnabled())
    {
        snprintf(hint, sizeof(hint), "Press 1-9    %c = Back", KEY_BACK);
    }
    else
    {
        snprintf(hint, sizeof(hint), "Muted (see Settings)  %c=Back", KEY_BACK);
    }
    int16_t hintX = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    if (hintX < 2) hintX = 2;
    tft->setCursor(hintX, SCREEN_HEIGHT - 10);
    tft->print(hint);
}

// =====================================================================
// Settings app -- audio feedback on/off toggle
// =====================================================================

// Draws (or redraws) the audio toggle: a label, an on/off pill switch,
// and an ON/OFF caption -- the only thing this screen shows, since
// there's just the one setting.
static void drawAudioToggleRow()
{
    // Clear the whole toggle area before repainting it.
    tft->fillRect(0, SETTINGS_TOGGLE_Y - 4, SCREEN_WIDTH, 56, COLOR_BG);

    const char* label = "Audio Feedback";
    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    int16_t lx = (SCREEN_WIDTH - (int16_t)strlen(label) * 6) / 2;
    if (lx < 2) lx = 2;
    tft->setCursor(lx, SETTINGS_TOGGLE_Y);
    tft->print(label);

    bool on = audioIsEnabled();

    int16_t pillW = 40;
    int16_t pillH = 18;
    int16_t pillX = (SCREEN_WIDTH - pillW) / 2;
    int16_t pillY = SETTINGS_TOGGLE_Y + 14;

    tft->fillRoundRect(pillX, pillY, pillW, pillH, pillH / 2, on ? COLOR_ACCENT : COLOR_DIM);
    tft->drawRoundRect(pillX, pillY, pillW, pillH, pillH / 2, COLOR_FG);

    // Knob slides to the right when on, left when off.
    int16_t knobD = pillH - 4;
    int16_t knobX = on ? (pillX + pillW - knobD - 2) : (pillX + 2);
    int16_t knobY = pillY + 2;
    tft->fillRoundRect(knobX, knobY, knobD, knobD, knobD / 2, COLOR_BG);

    tft->setTextColor(COLOR_DIM);
    const char* stateText = on ? "ON" : "OFF";
    int16_t sx = (SCREEN_WIDTH - (int16_t)strlen(stateText) * 6) / 2;
    tft->setCursor(sx, pillY + pillH + 6);
    tft->print(stateText);
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
    tft->setCursor(titleX, CONTENT_TOP + 4);
    tft->print(title);

    drawAudioToggleRow();

    tft->setTextColor(COLOR_DIM);
    char hint[24];
    snprintf(hint, sizeof(hint), "%c=Toggle   %c=Back", KEY_SELECT, KEY_BACK);
    int16_t hintX = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    if (hintX < 2) hintX = 2;
    tft->setCursor(hintX, SCREEN_HEIGHT - 10);
    tft->print(hint);
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
    tft->drawRoundRect(BREAKOUT_PLAY_X - 1, BREAKOUT_PLAY_Y - 1,
                       BREAKOUT_PLAY_W + 2, BREAKOUT_PLAY_H + 2, 2, COLOR_DIM);

    // Bricks
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
                case 1:  color = 0xFFE0; break; // yellow
                case 2:  color = 0x07E0; break; // green
                default: color = 0x07FF; break; // cyan
            }

            tft->fillRoundRect(bx, by, BREAKOUT_BRICK_W, BREAKOUT_BRICK_H, 1, color);
        }
    }

    // Paddle
    int16_t paddleY = BREAKOUT_PLAY_Y + BREAKOUT_PLAY_H - BREAKOUT_PADDLE_H;
    tft->fillRoundRect(breakoutPaddleX, paddleY,
                       BREAKOUT_PADDLE_W, BREAKOUT_PADDLE_H, 2, COLOR_ACCENT);

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

    // Hint
    tft->setTextSize(1);
    tft->setTextColor(COLOR_DIM);
    const char* hint = "4=Left 6=Right 5=Launch";
    hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);
}

static void breakoutHandleKey(char key)
{
    if (breakoutGameOver || breakoutGameWon) return;

    int16_t moveStep = BREAKOUT_PADDLE_W / 2;
    int16_t maxX = BREAKOUT_PLAY_X + BREAKOUT_PLAY_W - BREAKOUT_PADDLE_W;

    if (key == '4')
    {
        breakoutPaddleX -= moveStep;
        if (breakoutPaddleX < BREAKOUT_PLAY_X) breakoutPaddleX = BREAKOUT_PLAY_X;
    }
    else if (key == '6')
    {
        breakoutPaddleX += moveStep;
        if (breakoutPaddleX > maxX) breakoutPaddleX = maxX;
    }
    else if (key == '5' && !breakoutLaunched)
    {
        breakoutLaunched = true;
    }

    drawBreakout();
}

static void breakoutTick()
{
    if (breakoutGameOver || breakoutGameWon || !breakoutLaunched) return;

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

    // Ball lost
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

                if (allGone) breakoutGameWon = true;

                drawBreakout();
                return;
            }
        }
    }

    drawBreakout();
}

// =====================================================================
// BLECMD app -- BLE HID keyboard with preset actions
// =====================================================================

static uint8_t blecmdKeyToHID(char key)
{
    if (key >= '1' && key <= '9') return (uint8_t)(key - '1' + 0x1E);
    if (key == '0') return 0x27;
    if (key == 'A') return 0x04;
    if (key == 'B') return 0x05;
    if (key == 'C') return 0x06;
    if (key == 'D') return 0x07;
    if (key == '#') return 0x28;
    return 0;
}

static uint8_t charToHIDKey(char c)
{
    if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
    if (c >= 'A' && c <= 'Z') return 0x04 + (c - 'A');
    if (c >= '1' && c <= '9') return 0x1E + (c - '1');
    if (c == '0') return 0x27;
    if (c == ' ') return 0x2C;
    if (c == '\n') return 0x28;
    if (c == '-') return 0x2D;
    if (c == '=') return 0x2E;
    if (c == '[') return 0x2F;
    if (c == ']') return 0x30;
    if (c == '\\') return 0x31;
    if (c == ';') return 0x33;
    if (c == '\'') return 0x34;
    if (c == ',') return 0x36;
    if (c == '.') return 0x37;
    if (c == '/') return 0x38;
    if (c == '`') return 0x35;
    return 0;
}

static void blecmdExecuteAction(uint8_t index)
{
    if (index >= BLECMD_ACTION_COUNT) return;
    const BleAction &action = bleActions[index];

    switch (action.type)
    {
        case BLE_ACT_KEY:
            bleKeyboardSendKey(action.modifiers, action.keyCode);
            delay(50);
            bleKeyboardReleaseKey();
            break;
        case BLE_ACT_CONSUMER:
            bleKeyboardSendConsumer(action.usageCode);
            delay(50);
            bleKeyboardReleaseConsumer();
            break;
        case BLE_ACT_STRING:
            blecmdTypeStr = action.str;
            blecmdTypeIdx = 0;
            blecmdTyping = true;
            blecmdTypeReleasePending = false;
            blecmdTypeLastTime = millis();
            break;
    }
}

static void drawBLECMDRow(uint8_t rowSlot, bool cursorHere)
{
    int16_t y = BLECMD_LIST_Y + rowSlot * BLECMD_ROW_H;
    int16_t rowX = 8;
    int16_t rowW = SCREEN_WIDTH - 16;
    int16_t rowH = BLECMD_ROW_H - 2;

    tft->fillRect(rowX, y, rowW, rowH, COLOR_BG);

    uint8_t idx = blecmdScrollTop + rowSlot;
    if (idx >= BLECMD_ACTION_COUNT) return;

    if (cursorHere)
    {
        tft->fillRoundRect(rowX, y, rowW, rowH, 2, COLOR_SELECT_BG);
        tft->drawRoundRect(rowX, y, rowW, rowH, 2, COLOR_ACCENT_DIM);
    }

    tft->setTextSize(1);
    tft->setTextColor(cursorHere ? COLOR_FG : COLOR_DIM);
    tft->setCursor(rowX + 4, y + (rowH - 8) / 2);
    tft->print(bleActions[idx].label);
}

static void drawBLECMDList()
{
    for (uint8_t i = 0; i < BLECMD_VISIBLE_ROWS; i++)
    {
        uint8_t idx = blecmdScrollTop + i;
        drawBLECMDRow(i, idx == blecmdCursor);
    }
}

void drawBLECMD()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "BLE Keyboard";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    blecmdLastConnected = false;
    drawBLECMDStatus();

    drawBLECMDList();

    tft->setTextColor(COLOR_DIM);
    const char* hint = "0=Up #=Dn 5=Send";
    int16_t hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);
}

static void drawBLECMDStatus()
{
    tft->fillRect(0, CONTENT_TOP + 12, SCREEN_WIDTH, 12, COLOR_BG);

    tft->setTextSize(1);

    const char* s = nullptr;
    uint16_t color = COLOR_FG;

    if (blecmdTyping)
    {
        s = "Typing...";
        color = COLOR_ACCENT;
    }
    else if (bleKeyboardIsConnected())
    {
        s = "Connected";
        color = COLOR_ACCENT;
    }
    else if (bleKeyboardIsActive())
    {
        s = "Waiting...";
        color = COLOR_DIM;
    }

    if (s)
    {
        tft->setTextColor(color);
        int16_t sx = (SCREEN_WIDTH - (int16_t)strlen(s) * 6) / 2;
        tft->setCursor(sx, CONTENT_TOP + 14);
        tft->print(s);
    }

    blecmdLastConnected = bleKeyboardIsConnected();
}

static void handleBLECMDKey(char key)
{
    if (blecmdTyping) return;

    if (key == KEY_UP)
    {
        if (blecmdCursor > 0)
        {
            blecmdCursor--;
            if (blecmdCursor < blecmdScrollTop) blecmdScrollTop = blecmdCursor;
            drawBLECMDList();
        }
    }
    else if (key == KEY_DOWN)
    {
        if (blecmdCursor < BLECMD_ACTION_COUNT - 1)
        {
            blecmdCursor++;
            if (blecmdCursor >= blecmdScrollTop + BLECMD_VISIBLE_ROWS)
                blecmdScrollTop = blecmdCursor - BLECMD_VISIBLE_ROWS + 1;
            drawBLECMDList();
        }
    }
    else if (key == KEY_SELECT)
    {
        blecmdExecuteAction(blecmdCursor);
        drawBLECMDStatus();
    }
}

// =====================================================================
// Bluetooth Scanner app
// =====================================================================

static void drawBTScannerStatus()
{
    tft->fillRect(0, CONTENT_TOP + 12, SCREEN_WIDTH, 12, COLOR_BG);

    tft->setTextSize(1);

    const char* s;
    uint16_t color = COLOR_FG;

    if (btScannerIsConnected())
    {
        s = "Connected!";
        color = COLOR_ACCENT;
    }
    else if (btScannerIsScanning())
    {
        s = "Scanning...";
    }
    else
    {
        s = "Select device";
    }

    tft->setTextColor(color);
    int16_t sx = (SCREEN_WIDTH - (int16_t)strlen(s) * 6) / 2;
    tft->setCursor(sx, CONTENT_TOP + 14);
    tft->print(s);
}

static void drawBTScannerList()
{
    uint8_t count = btScannerDeviceCount();

    for (uint8_t i = 0; i < BT_VISIBLE_ROWS; i++)
    {
        uint8_t idx = btScrollTop + i;
        int16_t y = BT_LIST_Y + i * BT_ROW_H;
        int16_t rowX = 8;
        int16_t rowW = SCREEN_WIDTH - 16;
        int16_t rowH = BT_ROW_H - 2;

        tft->fillRect(rowX, y, rowW, rowH, COLOR_BG);

        if (idx >= count) continue;

        bool cursorHere = (idx == btCursor);

        if (cursorHere)
        {
            tft->fillRoundRect(rowX, y, rowW, rowH, 2, COLOR_SELECT_BG);
            tft->drawRoundRect(rowX, y, rowW, rowH, 2, COLOR_ACCENT_DIM);
        }

        tft->setTextSize(1);
        tft->setTextColor(cursorHere ? COLOR_FG : COLOR_DIM);

        const char* name = btScannerDeviceName(idx);
        char label[20];
        strncpy(label, name, 16);
        label[16] = '\0';
        int16_t nameW = (int16_t)strlen(label) * 6;

        tft->setCursor(rowX + 4, y + (rowH - 8) / 2);
        tft->print(label);

        char rssiBuf[8];
        snprintf(rssiBuf, sizeof(rssiBuf), "%d", btScannerDeviceRSSI(idx));
        int16_t rssiW = (int16_t)strlen(rssiBuf) * 6;
        int16_t szX = rowX + rowW - 4 - rssiW;
        if (szX > rowX + 4 + nameW + 6)
        {
            tft->setCursor(szX, y + (rowH - 8) / 2);
            tft->print(rssiBuf);
        }
    }
}

void drawBTScanner()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    const char* title = "Bluetooth Scanner";
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    drawBTScannerStatus();
    drawBTScannerList();

    tft->setTextColor(COLOR_DIM);
    const char* hint = "5=Connect *=Back";
    int16_t hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);
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
        tft->drawFastVLine(x, yTop, barH, COLOR_ACCENT);
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

    tft->setTextColor(COLOR_DIM);
    const char* hint = "^v=Chan 5=Hop *=Back";
    int16_t hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);
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

// Breadcrumb path (top) + key hint (bottom) -- the parts of the Files
// screen that don't change while browsing the same directory.
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

    tft->setTextColor(COLOR_DIM);
    char hint[20];
    if (sdReady)
    {
        snprintf(hint, sizeof(hint), "%c=Open  %c=Back", KEY_SELECT, KEY_BACK);
    }
    else
    {
        snprintf(hint, sizeof(hint), "%c = Back", KEY_BACK);
    }
    int16_t hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    if (hx < 2) hx = 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);

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
        tft->fillRoundRect(rowX, y, rowW, rowH, 2, COLOR_SELECT_BG);
        tft->drawRoundRect(rowX, y, rowW, rowH, 2, COLOR_ACCENT_DIM);
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
// or scrolling, without touching the breadcrumb/hint chrome above/below.
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
// honoring explicit newlines, up to FILEVIEW_MAX_LINES.
static void wrapFileBuffer()
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

        // Word-wrap this raw line into one or more display lines.
        size_t segStart = 0;
        do
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

            if (fileViewLineCount >= FILEVIEW_MAX_LINES) break;

            memcpy(fileViewLines[fileViewLineCount], &filePreviewBuffer[pos + segStart], take);
            fileViewLines[fileViewLineCount][take] = '\0';
            fileViewLineCount++;

            segStart += take;
            while (segStart < rawLen && filePreviewBuffer[pos + segStart] == ' ')
            {
                segStart++;
            }
        } while (segStart < rawLen && fileViewLineCount < FILEVIEW_MAX_LINES);

        if (rawLen == 0 && fileViewLineCount < FILEVIEW_MAX_LINES)
        {
            // Preserve blank lines.
            fileViewLines[fileViewLineCount][0] = '\0';
            fileViewLineCount++;
        }

        pos = lineEnd + 1; // skip past the '\n'
    }
}

// =====================================================================
// BMP image viewer
// =====================================================================

static bool parseBmpHeader(const char* path)
{
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    uint8_t hdr[54];
    if (f.read(hdr, 54) != 54) { f.close(); return false; }
    if (hdr[0] != 'B' || hdr[1] != 'M') { f.close(); return false; }

    bmpDataOffset = *(uint32_t*)&hdr[10];
    int32_t w     = *(int32_t*)&hdr[18];
    int32_t h     = *(int32_t*)&hdr[22];
    bmpBpp        = *(uint16_t*)&hdr[28];
    uint32_t comp  = *(uint32_t*)&hdr[30];

    if (comp != 0)             { f.close(); return false; }
    if (bmpBpp != 24 && bmpBpp != 16 && bmpBpp != 32)
                               { f.close(); return false; }
    if (w <= 0 || w > 4096 || h == 0 || h > 4096)
                               { f.close(); return false; }

    bmpTopDown = (h < 0);
    bmpHeight  = bmpTopDown ? -h : h;
    bmpWidth   = w;

    uint32_t rowBytesRaw = ((uint32_t)bmpBpp / 8) * bmpWidth;
    bmpRowStride = (rowBytesRaw + 3) & ~3u;

    f.close();
    return true;
}

static void drawBmpView();
static void openBmpView(uint8_t realIdx)
{
    if (realIdx >= fileEntryCount) return;

    const char* name = fileEntries[realIdx].name;
    if (currentPath[1] == '\0')
        snprintf(bmpFilePath, sizeof(bmpFilePath), "/%s", name);
    else
        snprintf(bmpFilePath, sizeof(bmpFilePath), "%s/%s", currentPath, name);

    if (!parseBmpHeader(bmpFilePath))
    {
        return;
    }

    bmpViewingEntry = realIdx;
    currentState = BMPVIEW;
    drawBmpView();
}

static void drawBmpView()
{
    clearContent();
    drawStatusBar();
    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    tft->setCursor(52, CONTENT_TOP + 50);
    tft->print("Loading...");

    File f = SD.open(bmpFilePath, FILE_READ);
    if (!f)
    {
        clearContent();
        drawStatusBar();
        tft->setCursor(40, CONTENT_TOP + 50);
        tft->setTextColor(COLOR_DIM);
        tft->print("(cannot open)");
        return;
    }

    uint8_t bpp = bmpBpp / 8;

    // Allocate batch buffer: BMP_BATCH rows of RGB565 pixels.
    if (!bmpBatchBuf)
        bmpBatchBuf = (uint16_t *)malloc(BMP_BATCH * (uint32_t)bmpWidth * sizeof(uint16_t));

    f.seek(bmpDataOffset);

    for (int32_t startRow = 0; startRow < bmpHeight; startRow += BMP_BATCH)
    {
        int32_t count = (bmpHeight - startRow < BMP_BATCH) ? (bmpHeight - startRow) : BMP_BATCH;

        // --- Phase 1: Read BMP_BATCH rows from SD, convert to RGB565 ---
        for (int32_t r = 0; r < count; r++)
        {
            f.read(bmpRowBuf, bmpRowStride);

            uint16_t *dst = bmpBatchBuf ? (bmpBatchBuf + r * bmpWidth) : bmpSrcLine;
            for (uint16_t i = 0; i < bmpWidth; i++)
            {
                uint16_t off = i * bpp;
                if (bmpBpp == 24)
                {
                    dst[i] = ((uint16_t)(bmpRowBuf[off + 2] >> 3) << 11) |
                             ((uint16_t)(bmpRowBuf[off + 1] >> 2) << 5)  |
                             (bmpRowBuf[off] >> 3);
                }
                else if (bmpBpp == 16)
                {
                    uint16_t raw = bmpRowBuf[off] | ((uint16_t)bmpRowBuf[off + 1] << 8);
                    dst[i] = (((raw >> 10) & 0x1F) << 11) |
                             (((raw >> 5) & 0x1F) << 6)  |
                             (raw & 0x1F);
                }
                else
                {
                    dst[i] = ((uint16_t)(bmpRowBuf[off + 2] >> 3) << 11) |
                             ((uint16_t)(bmpRowBuf[off + 1] >> 2) << 5)  |
                             (bmpRowBuf[off] >> 3);
                }
            }
        }

        // --- Phase 2: Write batch to TFT (no SD reads in between) ---
        for (int32_t r = 0; r < count; r++)
        {
            uint16_t *row = bmpBatchBuf ? (bmpBatchBuf + r * bmpWidth) : bmpSrcLine;
            tft->startWrite();
            tft->setAddrWindow(0, startRow + r, bmpWidth, 1);
            tft->writePixels(row, bmpWidth);
            tft->endWrite();
        }
    }

    if (bmpBatchBuf) { free(bmpBatchBuf); bmpBatchBuf = nullptr; }
    f.close();
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
    filePreviewLen = ok ? bytesRead : 0;

    filesViewingEntry = realIdx;
    filePreviewLineOffset = 0;
    wrapFileBuffer();

    currentState = FILEVIEW;
    drawFileView();
}

void drawFileView()
{
    clearContent();
    drawStatusBar();

    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    char title[22];
    strncpy(title, fileEntries[filesViewingEntry].name, sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    int16_t tx = (SCREEN_WIDTH - (int16_t)strlen(title) * 6) / 2;
    if (tx < 2) tx = 2;
    tft->setCursor(tx, CONTENT_TOP + 2);
    tft->print(title);

    tft->setTextColor(COLOR_FG);
    if (fileViewLineCount == 0)
    {
        const char* empty = "(empty or unreadable)";
        int16_t ex = (SCREEN_WIDTH - (int16_t)strlen(empty) * 6) / 2;
        tft->setCursor(ex, CONTENT_TOP + 30);
        tft->print(empty);
    }
    else
    {
        int16_t textY = CONTENT_TOP + 14;
        for (uint8_t i = 0; i < FILEVIEW_VISIBLE_LINES; i++)
        {
            uint8_t lineIdx = filePreviewLineOffset + i;
            if (lineIdx >= fileViewLineCount) break;
            tft->setCursor(6, textY + i * FILEVIEW_LINE_H);
            tft->print(fileViewLines[lineIdx]);
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
