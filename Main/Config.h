#ifndef CONFIG_H
#define CONFIG_H

// =====================================================================
// Ghoul OS - Config.h
// Central place for pin assignments, layout, colors and timing so the
// rest of the codebase never contains magic numbers.
// =====================================================================

// ---------------------- TFT DISPLAY (ST7735R) -----------------------
// SCK  -> GPIO18 (ESP32 hardware VSPI SCK, used automatically by SPI.h)
// MOSI -> GPIO23 (ESP32 hardware VSPI MOSI, used automatically by SPI.h)
#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17

// Physical panel is 128x160 (portrait). We run it rotated into
// 160x128 landscape, flipped vertically. If the image on your panel
// appears mirrored or upside down, try TFT_ROTATION 1 instead of 3 --
// this is the only line you should need to touch.
#define TFT_ROTATION 3

#define SCREEN_WIDTH  160
#define SCREEN_HEIGHT 128

// ---------------------------- KEYPAD ---------------------------------
// This is a standard 4x4 matrix keypad module. Its pin header is
// silkscreened "L1-L4" and "R1-R4" (not "C1-C4"/"R1-R4"), but
// electrically R1-R4 are the four ROW lines and L1-L4 are the four
// COLUMN lines -- KEYPAD_COL_PINS below wires to the board's L1-L4 pins.
#define KEYPAD_ROWS 4
#define KEYPAD_COLS 4

// R1..R4 (rows)    -> GPIO32, 33, 25, 14
// L1..L4 (columns) -> GPIO13, 12, 15, 2   <-- board silkscreen calls these "L1-L4"
static const uint8_t KEYPAD_ROW_PINS[KEYPAD_ROWS] = {32, 33, 25, 14};
static const uint8_t KEYPAD_COL_PINS[KEYPAD_COLS] = {13, 12, 15, 2}; // == board's L1-L4

// ------------------------ NAVIGATION KEYS -----------------------------
#define KEY_UP     '0'
#define KEY_DOWN   '#'
#define KEY_SELECT '5'
#define KEY_BACK   '*'

// ------------------------------ TIMING ---------------------------------
#define BOOT_SPLASH_DURATION_MS 3000UL
#define FRAME_INTERVAL_MS       33UL   // ~30 FPS
#define ANIM_DURATION_MS        220UL  // carousel slide duration

// ------------------------------ COLORS (RGB565) -------------------------
// Retro CRT / terminal look: one monochrome phosphor colour on black,
// picked to feel like the Pip-Boy's green display.
//
// The palette is NOT fixed at compile time: Settings > Colour Theme
// switches the whole device between seven phosphor colours (Green,
// Amber, Blue, White, Purple, Red, Pink) and remembers the choice in
// flash. Every screen still draws through the COLOR_* names below, but
// those names now expand to runtime variables set by Theme.cpp, so a
// theme change re-colours the entire UI at once.
//
// A few elements deliberately keep their own fixed colour because the
// colour carries information rather than being decoration: the 2048
// tiles and the PackMon packet-rate bars.
extern uint16_t ghColorBg;         // page background (always black)
extern uint16_t ghColorFg;         // normal text
extern uint16_t ghColorAccent;     // headers, selected text, emphasis
extern uint16_t ghColorAccentDim;  // borders, dividers
extern uint16_t ghColorDim;        // secondary text
extern uint16_t ghColorStatusbar;  // status bar strip
extern uint16_t ghColorSelectBg;   // selection highlight fill

#define COLOR_BG         ghColorBg
#define COLOR_FG         ghColorFg
#define COLOR_ACCENT     ghColorAccent
#define COLOR_ACCENT_DIM ghColorAccentDim
#define COLOR_DIM        ghColorDim
#define COLOR_STATUSBAR  ghColorStatusbar
#define COLOR_SELECT_BG  ghColorSelectBg

// ------------------------------ LAYOUT -----------------------------------
#define STATUSBAR_HEIGHT 14
#define CONTENT_TOP      (STATUSBAR_HEIGHT)
#define CONTENT_HEIGHT   (SCREEN_HEIGHT - STATUSBAR_HEIGHT)

// The launcher is now a single centered text carousel spanning nearly
// the full screen width -- no more icon/description side panel.
#define RIGHT_PANEL_X     6
#define RIGHT_PANEL_W     (SCREEN_WIDTH - RIGHT_PANEL_X - 4)

// Bigger row height to fit the larger (size-2) carousel text.
#define CAROUSEL_ITEM_H        22
#define CAROUSEL_VISIBLE_ITEMS 5

// Top edge of the fixed row viewport the launcher scrolls inside. It is
// centred in the content area, leaving 2px above and below the five
// visible rows (5 * 22 = 110px tall).
#define CAROUSEL_LIST_Y  (CONTENT_TOP + (CONTENT_HEIGHT - (CAROUSEL_VISIBLE_ITEMS * CAROUSEL_ITEM_H)) / 2)

#define STATUSBAR_ICON_SIZE 10

// Shared border color for bordered panels (app placeholder screens,
// the PackMon graph, etc.) so they all read as one consistent theme.
#define ICON_BORDER_COLOR  COLOR_ACCENT_DIM

// ---------------------------- SD CARD ---------------------------------
// SD_CS  -> GPIO4    (dedicated chip-select)
// MOSI   -> GPIO23   (shared VSPI bus with the TFT)
// MISO   -> GPIO19   (VSPI default; the TFT never uses MISO)
// SCK    -> GPIO18   (shared VSPI bus with the TFT)
// The SD card and the TFT share the same SPI bus with separate CS
// lines, which is the normal/expected way to wire the two together.
#define SD_CS_PIN 4

// ---------------------------- PIEZO BUZZER ---------------------------------
// Passive piezo buzzer for short UI click/beep feedback and the Music
// app's mini piano. Connect its + (signal) lead to GPIO27 and its -
// (ground) lead to GND. GPIO27 is otherwise unused by this project and
// carries no boot-strapping meaning, so it's safe to drive directly.
#define PIEZO_PIN 27

// ------------------------- KEY TEST APP LAYOUT ---------------------------
// A 4x4 grid mirroring the physical keypad, used by the "Key Test" app
// to visualize which button was just pressed.
#define KEYTEST_ROWS   4
#define KEYTEST_COLS   4
#define KEYTEST_CELL_W 34
#define KEYTEST_CELL_H 20
#define KEYTEST_GRID_X ((SCREEN_WIDTH - (KEYTEST_COLS * KEYTEST_CELL_W)) / 2)
#define KEYTEST_GRID_Y (CONTENT_TOP + 18)

// -------------------------- SETTINGS APP LAYOUT ---------------------------
// Four stacked rows: audio feedback, colour theme, splash image and
// auto-sleep timeout. Up/Down moves the cursor between rows; Select acts
// on the highlighted row (toggle audio, or open the matching list).
#define SETTINGS_ROW_X  6
#define SETTINGS_ROW_W  (SCREEN_WIDTH - 12)
#define SETTINGS_ROW_Y  (CONTENT_TOP + 16)   // 30
#define SETTINGS_ROW_H  18
#define SETTINGS_ROW_GAP 4
#define SETTINGS_ROW_COUNT 4

// The theme list shown while the colour row is open -- one 12px row per
// available colour, which is exactly what fits under the title.
#define SETTINGS_THEME_Y  (CONTENT_TOP + 16) // 30
#define SETTINGS_THEME_H  12

// The splash-image and sleep-timeout lists: eight 12px rows under the
// title, scrolling when there are more entries than that.
#define SETTINGS_LIST_Y        (CONTENT_TOP + 12) // 26
#define SETTINGS_LIST_H        12
#define SETTINGS_LIST_VISIBLE  8

// ---------------------------- FILES APP LAYOUT ----------------------------
// Scrollable directory listing shown by the "Files" app.
#define FILES_VISIBLE_ROWS 6
#define FILES_ROW_H         14
#define FILES_LIST_Y        (CONTENT_TOP + 16)

// Simple text preview shown when a file (rather than a folder) is opened.
// The preview owns the whole panel (no status bar, no filename line) so
// this is 13 lines of 9px inside the 128px screen.
#define FILEVIEW_VISIBLE_LINES 13
#define FILEVIEW_LINE_H         9

// ---------------------------- SPLASH IMAGE --------------------------------
// How many .bmp files in the SD card's root folder are offered in
// Settings > Splash. Only the names are stored, never the image itself.
#define SPLASH_MAX_FILES 24

// BMP image viewer -- images wider or taller than this are refused, which
// keeps the per-row read buffer small (4 bytes/pixel * BMP_MAX_DIM).
#define BMP_MAX_DIM 2048

// ---------------------------- MINI PIANO APP LAYOUT ----------------------------
// 3x3 grid mirroring keys 1-9 on the physical keypad, used by the
// "MiniPiano" app's mini piano.
#define MUSIC_GRID_ROWS 3
#define MUSIC_GRID_COLS 3
#define MUSIC_CELL_W    42
#define MUSIC_CELL_H    26
#define MUSIC_GRID_X    ((SCREEN_WIDTH - (MUSIC_GRID_COLS * MUSIC_CELL_W)) / 2)
#define MUSIC_GRID_Y    (CONTENT_TOP + 20)

// ---------------------------- BREAKOUT GAME LAYOUT ----------------------------
#define BREAKOUT_PLAY_X      2
#define BREAKOUT_PLAY_Y      (CONTENT_TOP + 12)
#define BREAKOUT_PLAY_W      (SCREEN_WIDTH - 4)
#define BREAKOUT_PLAY_H      (SCREEN_HEIGHT - CONTENT_TOP - 24)
#define BREAKOUT_PADDLE_W    24
#define BREAKOUT_PADDLE_H    4
#define BREAKOUT_BALL_SIZE   3
#define BREAKOUT_BRICK_ROWS  4
#define BREAKOUT_BRICK_COLS  8
#define BREAKOUT_BRICK_W     ((BREAKOUT_PLAY_W - (BREAKOUT_BRICK_COLS - 1)) / BREAKOUT_BRICK_COLS)
#define BREAKOUT_BRICK_H     5
#define BREAKOUT_BRICK_GAP   1

// ---------------------------- CALCULATOR APP LAYOUT ----------------------------
#define CALC_MAX_DIGITS 10                 // max digits accepted per operand
#define CALC_LINE_Y     (CONTENT_TOP + 14) // first text line's y
#define CALC_LINE_H     14                 // vertical spacing between lines

// ---------------------------- MATRIX APP LAYOUT ----------------------------
// Full-screen digital-rain screensaver -- like the boot splash, it draws
// over the whole panel with no status bar. One column per 6px-wide text
// glyph, one row per 8px-tall glyph, covering the entire screen.
#define MATRIX_COLS    (SCREEN_WIDTH / 6)
#define MATRIX_ROWS    (SCREEN_HEIGHT / 8)
#define MATRIX_TICK_MS 60UL

// ---------------------------- 2048 GAME LAYOUT ----------------------------
// Standard 4x4 board. Cell + gap sizes chosen so the whole grid fits
// under the header line with a little breathing room at the bottom.
#define G2048_SIZE    4
#define G2048_CELL    21
#define G2048_GAP     4
#define G2048_GRID_W  (G2048_SIZE * G2048_CELL + (G2048_SIZE - 1) * G2048_GAP)
#define G2048_GRID_H  (G2048_SIZE * G2048_CELL + (G2048_SIZE - 1) * G2048_GAP)
#define G2048_GRID_X  ((SCREEN_WIDTH - G2048_GRID_W) / 2)
#define G2048_GRID_Y  (CONTENT_TOP + 14)

// ---------------------------- WIFI SCANNER APP LAYOUT ----------------------------
// Scrollable list of nearby SSIDs shown by the "WiFi Scanner" app. SSIDs
// too long to fit their row scroll horizontally (a marquee/ticker) so
// the full name is still readable rather than being cut off.
#define WIFI_LIST_Y          (CONTENT_TOP + 24)
#define WIFI_ROW_H           12
#define WIFI_VISIBLE_ROWS    7
#define WIFI_MARQUEE_STEP_MS 120UL  // ms between each 1-character marquee step

// ---------------------------- STOCKWATCH APP ----------------------------
// WiFi credentials used to reach the internet for stock quotes. This is
// a SEPARATE connection from the WiFi Scanner app above -- WiFi Scanner
// only ever passively scans and never associates to a network, while
// StockWatch needs a real internet connection to reach Finnhub.
// >>> Fill these in with your real network's SSID/password. <<<


// Finnhub API key -- create a free one at https://finnhub.io/register
// >>> Fill this in with your real Finnhub API key. <<<


// The list of ticker symbols tracked is in StockWatch.cpp
// (stockSymbols[]) -- edit that array to change which companies show up
// here, not this file.
#define STOCK_LIST_Y      (CONTENT_TOP + 24)
#define STOCK_ROW_H        12
#define STOCK_VISIBLE_ROWS 7

// ---------------------------- PACKMON APP LAYOUT ----------------------------
// "PackMon": a live 802.11 packet-rate graph, inspired by
// spacehuhn/PacketMonitor32. Each history bin is one pixel-wide column
// on the graph, sampled every PACKETMON_BIN_INTERVAL_MS -- so the full
// width of the graph shows (PACKETMON_HISTORY_LEN * PACKETMON_BIN_INTERVAL_MS)
// ms of scrolling strip-chart (150 columns * 200ms = 30s by default).
#define PACKETMON_GRAPH_X         5
#define PACKETMON_GRAPH_Y         (CONTENT_TOP + 46)
#define PACKETMON_GRAPH_W         (SCREEN_WIDTH - 10)
#define PACKETMON_GRAPH_H         40
#define PACKETMON_HISTORY_LEN     PACKETMON_GRAPH_W
#define PACKETMON_BIN_INTERVAL_MS 200UL
#define PACKETMON_HOP_INTERVAL_MS 2000UL
#define PACKETMON_GRAPH_MAX_PPS   40   // bin count that fills the graph to full height
#define PACKETMON_CHANNEL_MIN     1
#define PACKETMON_CHANNEL_MAX     13

// ---------------------------- BLE KEYBOARD APP ----------------------------
// "BLE Keybd": a Bluetooth LE keyboard/remote. The device advertises this
// name. It is a standard HID keyboard, built on the BLE stack that ships
// with the ESP32 Arduino core -- no extra library needed.
#define BLE_DEVICE_NAME "Ghoul OS Keys"

// Two-option submenu (Keyboard / Remote) shown once a host connects.
#define BLE_MENU_ROW_Y    (CONTENT_TOP + 30)
#define BLE_MENU_ROW_H    22
#define BLE_MENU_ROW_GAP  6

// Keyboard submenu screen: one scrollable row per keystroke, so each key
// can be sent on its own instead of being laid out as a numpad grid.
#define BLE_LIST_Y          (CONTENT_TOP + 14)   // 28
#define BLE_LIST_ROW_H      12
#define BLE_LIST_VISIBLE    7
#define BLE_LIST_FLASH_MS   110UL                // how long a sent row stays lit

// Remote screen geometry -- a D-pad of triangles plus the centre square,
// an ESC button in the top-right corner and two volume boxes at the
// bottom, laid out so it all fits under the status bar on a 160x128
// panel with no overlap.
#define REMOTE_MID_X        (SCREEN_WIDTH / 2)                  // 80
#define REMOTE_UP_APEX_Y    (CONTENT_TOP + 10)                  // 24
#define REMOTE_UP_BASE_Y    (REMOTE_UP_APEX_Y + 18)             // 42
#define REMOTE_UP_HW        12                                  // half-width of the base
#define REMOTE_ROW_TOP      (REMOTE_UP_BASE_Y + 8)              // 50
#define REMOTE_ROW_BOTTOM   (REMOTE_ROW_TOP + 20)               // 70
#define REMOTE_MID_HW       10                                  // half-width of the centre square
#define REMOTE_ARM_GAP      22                                  // flat edge of the left/right triangles
#define REMOTE_ARM_LEN      12                                  // how far their point sticks out
#define REMOTE_DOWN_TOP_Y   (REMOTE_ROW_BOTTOM + 4)             // 74
#define REMOTE_DOWN_APEX_Y  (REMOTE_DOWN_TOP_Y + 18)            // 92
#define REMOTE_DOWN_HW      12
#define REMOTE_VOL_Y        (REMOTE_DOWN_APEX_Y + 8)            // 100
#define REMOTE_VOL_H        14
#define REMOTE_VOL_W        64
#define REMOTE_VOL_LEFT_X   10
#define REMOTE_VOL_RIGHT_X  (SCREEN_WIDTH - 10 - REMOTE_VOL_W)  // 86
#define REMOTE_ESC_W        32
#define REMOTE_ESC_H        13
#define REMOTE_ESC_X        (SCREEN_WIDTH - 4 - REMOTE_ESC_W)   // 124
#define REMOTE_ESC_Y        (CONTENT_TOP + 1)                   // 15

// ---------------------------- ICON DATA FLAG ------------------------------
// All bitmaps in Icons.h are currently empty placeholders (see project
// brief). drawIcon() renders a safe vector placeholder glyph instead of
// reading the (currently zero-length) PROGMEM arrays. Once real RGB565
// bitmap data is added to an icon array, flip this to 1 to enable the
// real pixel blit performed by drawBitmapIcon().
#define ICONS_HAVE_DATA 1

#define FINNHUB_API_KEY "dbumnicetrywebscrapersu3hr01qv51jcep4g"
#define STOCK_WIFI_SSID     "SSID"
#define STOCK_WIFI_PASSWORD "PASS"
#endif // CONFIG_H
