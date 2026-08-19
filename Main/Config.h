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
// Retro CRT / terminal theme: monochrome phosphor green on black,
// inspired by old amber/green terminal readouts (see e.g. the classic
// "REGISTER CONTROL" synth-tracker screens) and the Pip-Boy's green
// display. Every screen in the UI draws through these macros rather
// than hardcoded hex, so changing the theme here re-themes the whole
// device -- no blue/cyan/white left anywhere.
#define COLOR_BG         0x0000  // Black
#define COLOR_FG         0x3FED  // Bright phosphor green -- normal text
#define COLOR_ACCENT     0xBFF7  // Near-white mint green -- headers/selected text/emphasis
#define COLOR_ACCENT_DIM 0x1468  // Mid green -- borders, dividers
#define COLOR_DIM        0x1366  // Dark green -- secondary/hint text
#define COLOR_STATUSBAR  0x00C1  // Near-black green -- status bar strip
#define COLOR_SELECT_BG  0x01E3  // Dark green fill -- selection highlight background

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

// Vertical scrollbar drawn along the rightmost edge of the screen,
// showing progress through the app list -- drawn as a solid rectangle
// block, not a rounded pill, to match the rest of the theme.
#define SCROLLBAR_WIDTH 4
#define SCROLLBAR_GAP   3

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
// A single audio-feedback on/off toggle shown by the "Settings" app.
#define SETTINGS_TOGGLE_Y (CONTENT_TOP + 40)

// ---------------------------- FILES APP LAYOUT ----------------------------
// Scrollable directory listing shown by the "Files" app.
#define FILES_VISIBLE_ROWS 6
#define FILES_ROW_H         14
#define FILES_LIST_Y        (CONTENT_TOP + 16)

// Simple text preview shown when a file (rather than a folder) is opened.
#define FILEVIEW_VISIBLE_LINES 10
#define FILEVIEW_LINE_H         9

// BMP image viewer
#define BMP_LINE_MAX 320       // max source pixels buffered per row

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
#define STOCK_WIFI_SSID     "AKKU_2.4G"
#define STOCK_WIFI_PASSWORD "akku@2023"

// Finnhub API key -- create a free one at https://finnhub.io/register
// >>> Fill this in with your real Finnhub API key. <<<
#define FINNHUB_API_KEY "da1usihr01qp0a25tvcgda1usihr01qp0a25tvd0"

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

// ---------------------------- ICON DATA FLAG ------------------------------
// All bitmaps in Icons.h are currently empty placeholders (see project
// brief). drawIcon() renders a safe vector placeholder glyph instead of
// reading the (currently zero-length) PROGMEM arrays. Once real RGB565
// bitmap data is added to an icon array, flip this to 1 to enable the
// real pixel blit performed by drawBitmapIcon().
#define ICONS_HAVE_DATA 1

#endif // CONFIG_H
