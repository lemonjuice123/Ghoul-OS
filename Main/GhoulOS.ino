// =====================================================================
// Ghoul OS
// A small handheld-style launcher UI for ESP32 + 1.8" ST7735R TFT +
// 4x4 matrix keypad + SD card + piezo buzzer.
//
// Required libraries (install via Arduino IDE Library Manager):
//   - Adafruit GFX Library
//   - Adafruit ST7735 and ST7789 Library
//   - Keypad (by Mark Stanley / Alexander Brevig)
//   - SD and Preferences (both bundled with the ESP32 Arduino core --
//     nothing extra to install for either)
//   - WiFi and esp_wifi (also bundled with the ESP32 Arduino core --
//     used by the "PackMon" app's promiscuous-mode packet monitor)
//
// Board: ESP32 Dev Module (ESP32 DevKit V1)
//
// Wiring:
//   TFT  VCC  -> 3.3V      TFT  LED  -> 3.3V
//   TFT  GND  -> GND       TFT  SCK  -> GPIO18
//   TFT  MOSI -> GPIO23    TFT  CS   -> GPIO5
//   TFT  DC   -> GPIO16    TFT  RST  -> GPIO17
//
//   SD   CS   -> GPIO4     SD   MOSI -> GPIO23 (shared VSPI bus with TFT)
//   SD   MISO -> GPIO19    SD   SCK  -> GPIO18 (shared VSPI bus with TFT)
//
//   Piezo +  -> GPIO27     Piezo -   -> GND
//
//   Keypad rows R1-R4 -> GPIO32, 33, 25, 14
//   Keypad cols L1-L4 -> GPIO13, 12, 15, 2
//   (the board silkscreens its column pins as "L1-L4", not "C1-C4" --
//    they are electrically the matrix's column lines either way)
//
// Navigation: A = Up, C = Down, B = Select/Open, D = Back
// =====================================================================

#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Keypad.h>

#include "Config.h"
#include "Icons.h"
#include "Apps.h"
#include "UI.h"
#include "FileBrowser.h"
#include "Audio.h"

// ------------------------------------------------------------------
// Hardware instances
// ------------------------------------------------------------------
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// 4x4 keypad layout. A, B, C, D are used for navigation (Up/Select/
// Down/Back); 1-9 are used by the Music app; the rest are read but
// otherwise unused for now.
static char keypadLayout[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '4', '7', 'A'},
    {'2', '5', '8', 'B'},
    {'3', '6', '9', 'C'},
    {'*', '0', '#', 'D'}
};

// Keypad library wants non-const pin arrays.
static byte keypadRowPins[KEYPAD_ROWS] = {
    KEYPAD_ROW_PINS[0], KEYPAD_ROW_PINS[1], KEYPAD_ROW_PINS[2], KEYPAD_ROW_PINS[3]
};
static byte keypadColPins[KEYPAD_COLS] = {
    KEYPAD_COL_PINS[0], KEYPAD_COL_PINS[1], KEYPAD_COL_PINS[2], KEYPAD_COL_PINS[3]
};

Keypad keypad = Keypad(makeKeymap(keypadLayout), keypadRowPins, keypadColPins,
                        KEYPAD_ROWS, KEYPAD_COLS);

// ------------------------------------------------------------------
// setup() - hardware init, then boot splash
// ------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);

    keypad.setDebounceTime(20); // helps filter noisy/loose matrix connections

    // ---- Display init ----
    tft.initR(INITR_BLACKTAB); // common 1.8" ST7735R init sequence
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(COLOR_BG);

    // ---- SD card init ----
    // Shares the TFT's VSPI bus (SCK18/MOSI23/MISO19) with its own CS
    // line (GPIO4). It's fine if no card is present or init fails --
    // the Files app checks sdReady and shows a friendly message instead.
    if (!filesInit())
    {
        Serial.println("SD card not found (Files app will show a notice).");
    }

    // ---- Audio init ----
    // Loads the saved audio-on/off preference from flash (defaults to
    // on for a brand-new board). tone()/noTone() on PIEZO_PIN handle
    // the actual buzzer driving, so there's nothing else to set up here.
    audioInit();

    // ---- UI init + boot splash ----
    uiInit(&tft);
    drawBoot();
    // uiTick() (called from loop) advances currentState from BOOT to
    // MENU automatically once BOOT_SPLASH_DURATION_MS has elapsed --
    // no key press is required.
}

// ------------------------------------------------------------------
// loop() - poll input, advance animation/state
// ------------------------------------------------------------------
void loop()
{
    char key = keypad.getKey();
    if (key)
    {
        uiHandleKey(key);
    }

    uiTick();
}
