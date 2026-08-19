#include <math.h>
#include <string.h>
#include <stdio.h>
#include <SD.h>
#include "UI.h"
#include "Config.h"
#include "Icons.h"
#include "FileBrowser.h"
#include "Audio.h"
#include "PacketMon.h"
#include "WifiScan.h"
#include "StockWatch.h"

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
// FILEVIEW_MAX_LINES stays comfortably under 255 so fileViewLineCount/
// filePreviewLineOffset can remain plain uint8_t without any risk of
// wraparound.
#define FILEVIEW_MAX_LINES 240
#define FILEVIEW_LINE_LEN  27

static char filePreviewBuffer[FB_PREVIEW_BUF_SIZE + 1];
static size_t filePreviewLen = 0;
static uint8_t filesViewingEntry = 0; // index into fileEntries[] being previewed
static char fileViewLines[FILEVIEW_MAX_LINES][FILEVIEW_LINE_LEN];
static uint8_t fileViewLineCount = 0;
static uint8_t filePreviewLineOffset = 0; // which wrapped line is scrolled to the top
static bool filePreviewIsBinary = false;        // content looked like binary, not text
static bool filePreviewBufferTruncated = false; // file is bigger than FB_PREVIEW_BUF_SIZE
static bool filePreviewLinesTruncated = false;  // more wrapped lines than FILEVIEW_MAX_LINES allows

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

// ----------------------------- WiFi Scanner state ---------------------------
static uint8_t wifiCursor = 0;
static uint8_t wifiScrollTop = 0;
static uint16_t wifiMarqueeOffset = 0; // shared ticker position for overflowing SSIDs

// ----------------------------- StockWatch state -----------------------------
static uint8_t stockCursor = 0;
static uint8_t stockScrollTop = 0;

// Forward declarations for internal helpers (not part of the public UI.h API)
static void drawScrollbar();
static void startCarouselAnimation();
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
}

// =====================================================================
// Boot screen
// =====================================================================

void drawBoot()
{
    // Full-screen boot logo, blitted from the RGB565 PROGMEM array in
    // Icons.h (bootLogo -- already sized to exactly SCREEN_WIDTH x
    // SCREEN_HEIGHT, 20480 entries). Shown for BOOT_SPLASH_DURATION_MS
    // (see Config.h) before uiTick() switches over to MENU.
    drawBitmapIcon(0, 0, bootLogo, SCREEN_WIDTH, SCREEN_HEIGHT);
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

    // Reserve room for the scrollbar + a gap so text never runs under it.
    // Carousel text is drawn at size 2 (12px/char) now that it no
    // longer has to share the screen with an icon/description panel.
    int16_t textAreaX = RIGHT_PANEL_X + 4;
    int16_t textAreaRight = SCREEN_WIDTH - SCROLLBAR_WIDTH - SCROLLBAR_GAP - 2;
    uint8_t maxChars = (uint8_t)((textAreaRight - textAreaX) / 12);
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

    drawScrollbar();

    // Redraw the status bar on top last. This guarantees any row that
    // slides close to the boundary is hidden behind it rather than
    // showing as a stray blob -- belt-and-braces alongside the clip
    // check above.
    drawStatusBar();
}

// Fixed highlight box behind the centered/selected row in the list --
// a sharp-cornered rectangle, matching the rest of the retro theme.
void drawSelection()
{
    float centerY = CONTENT_TOP + CONTENT_HEIGHT / 2.0f;

    int16_t boxX = RIGHT_PANEL_X;
    int16_t boxW = RIGHT_PANEL_W - SCROLLBAR_WIDTH - SCROLLBAR_GAP;
    int16_t boxH = CAROUSEL_ITEM_H - 2;
    int16_t boxY = (int16_t)(centerY - boxH / 2.0f);

    tft->fillRect(boxX, boxY, boxW, boxH, COLOR_SELECT_BG);
    tft->drawRect(boxX, boxY, boxW, boxH, COLOR_ACCENT_DIM);
}

// Vertical scrollbar along the rightmost edge showing progress through
// the full app list -- a solid rectangle thumb, not a rounded pill.
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

    tft->fillRect(trackX, thumbY, SCROLLBAR_WIDTH, thumbH, COLOR_ACCENT);
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
        const char* title = "GHOUL OS";
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

        tft->setTextColor(COLOR_DIM);
        char hint[16];
        snprintf(hint, sizeof(hint), "%c = Back", KEY_BACK);
        int16_t hintX = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
        tft->setCursor(hintX, SCREEN_HEIGHT - 10);
        tft->print(hint);
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

    const char* label = "AUDIO FEEDBACK";
    tft->setTextSize(1);
    tft->setTextColor(COLOR_FG);
    int16_t lx = (SCREEN_WIDTH - (int16_t)strlen(label) * 6) / 2;
    if (lx < 2) lx = 2;
    tft->setCursor(lx, SETTINGS_TOGGLE_Y);
    tft->print(label);

    bool on = audioIsEnabled();

    // Bracketed "[ ON ]" / "[ OFF ]" toggle box -- sharp rectangle,
    // matching the rest of the theme instead of a rounded pill switch.
    char boxText[8];
    snprintf(boxText, sizeof(boxText), "[ %s ]", on ? "ON" : "OFF");

    tft->setTextSize(2);
    int16_t boxW = (int16_t)strlen(boxText) * 12 + 12;
    int16_t boxH = 26;
    int16_t boxX = (SCREEN_WIDTH - boxW) / 2;
    int16_t boxY = SETTINGS_TOGGLE_Y + 14;

    tft->fillRect(boxX, boxY, boxW, boxH, on ? COLOR_SELECT_BG : COLOR_BG);
    tft->drawRect(boxX, boxY, boxW, boxH, on ? COLOR_ACCENT : COLOR_DIM);

    tft->setTextColor(on ? COLOR_ACCENT : COLOR_DIM);
    tft->setCursor(boxX + 6, boxY + 5);
    tft->print(boxText);
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

    // Hint
    tft->setTextSize(1);
    tft->setTextColor(COLOR_DIM);
    const char* hint = "4=Left 6=Right 5=Launch";
    hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);
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

    tft->setTextColor(COLOR_DIM);
    const char* hint = "5=Rescan *=Back";
    int16_t hx = (SCREEN_WIDTH - (int16_t)strlen(hint) * 6) / 2;
    tft->setCursor(hx, SCREEN_HEIGHT - 10);
    tft->print(hint);
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

    tft->setTextColor(COLOR_DIM);
    const char* hint = "5=Refresh *=Back";
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
