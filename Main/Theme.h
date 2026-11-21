//====================================================================
// Theme.h - Ghoul OS colour themes.
//
// The whole UI draws through the COLOR_* names in Config.h, which expand
// to the runtime variables defined in Theme.cpp. Calling themeSetIndex()
// swaps all of them at once, so every screen changes colour the next
// time it is drawn.
//
// The chosen theme is stored in flash under the "ghoultheme" namespace,
// so it survives a power cycle (themeSave()).
//====================================================================
#ifndef THEME_H
#define THEME_H

#include <Arduino.h>

// Loads the saved theme from flash and applies it. Call once from
// setup(), before anything is drawn.
void themeInit();

// How many themes there are, and their display names ("Green", "Amber",
// "Blue", "White", "Purple", "Red", "Pink").
uint8_t themeCount();
const char* themeName(uint8_t index);

// The currently applied theme.
uint8_t themeGetIndex();

// Applies a theme immediately (not persisted -- call themeSave() for
// that). Out-of-range indexes are ignored.
void themeSetIndex(uint8_t index);

// Writes the current theme to flash so it is remembered on next boot.
void themeSave();

// Pushes the current theme's palette into the COLOR_* globals. Only
// Theme.cpp needs this; it is public so a future screen can force a
// re-apply after drawing its own palette.
void themeApply();

// A theme's main text colour, for previewing an entry in the picker
// without switching to it.
uint16_t themeFgColor(uint8_t index);

#endif // THEME_H
