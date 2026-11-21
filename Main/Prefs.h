//====================================================================
// Prefs.h - Ghoul OS device preferences (Settings app).
//
// Two settings that, unlike audio feedback and the colour theme, are
// not owned by their own module:
//
//   - The splash image: a .bmp file on the SD card shown at power-on
//     instead of the built-in logo. An empty path means "built-in".
//   - The auto-sleep timeout: how long the device sits idle (no key
//     press) before the display is switched off to save power. Any key
//     wakes it up again.
//
// Both are kept in flash (NVS), so they survive a power cycle.
//====================================================================
#ifndef PREFS_H
#define PREFS_H

#include <Arduino.h>

// Loads both stored preferences. Call once from setup().
void prefsInit();

// ---- Splash image ---------------------------------------------------
// Absolute SD path of the chosen .bmp (e.g. "/splash.bmp"), or an empty
// string when the built-in logo should be used.
const char* prefsSplashPath();

// Stores the path ("" = built-in logo) and remembers it for next boot.
void prefsSetSplashPath(const char* path);

// ---- Auto sleep ------------------------------------------------------
// The available timeouts, from "OFF" through 15 seconds up to 10 minutes.
uint8_t     prefsSleepCount();
const char* prefsSleepName(uint8_t index);       // "15 SEC"
const char* prefsSleepShortName(uint8_t index);  // "15s"
uint16_t    prefsSleepSeconds(uint8_t index);    // 0 = never sleep

// The timeout currently in use.
uint8_t prefsSleepIndex();

// Changes the timeout and remembers it for next boot.
void prefsSetSleepIndex(uint8_t index);

#endif // PREFS_H
