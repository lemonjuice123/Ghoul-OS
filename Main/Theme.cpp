#include <Arduino.h>
#include <Preferences.h>

#include "Config.h"
#include "Theme.h"

// =====================================================================
// Ghoul OS - Theme.cpp
//
// One black-background phosphor palette per theme. The four "weight"
// levels (accent, fg, accentDim, dim) keep the same contrast relationship
// in every theme, so every screen stays readable whatever colour is
// picked.
// =====================================================================

// The runtime palette the rest of the code sees (declared in Config.h).
uint16_t ghColorBg        = 0x0000;
uint16_t ghColorFg        = 0x3FED;
uint16_t ghColorAccent    = 0xBFF7;
uint16_t ghColorAccentDim = 0x1468;
uint16_t ghColorDim       = 0x1366;
uint16_t ghColorStatusbar = 0x00C1;
uint16_t ghColorSelectBg  = 0x01E3;

struct ThemePalette
{
    const char* name;
    uint16_t fg;
    uint16_t accent;
    uint16_t accentDim;
    uint16_t dim;
    uint16_t statusbar;
    uint16_t selectBg;
};

static const ThemePalette palettes[] = {
    // name      fg       accent   accentDim dim      statusbar selectBg
    { "Green",  0x3FED, 0xBFF7, 0x1468, 0x1366, 0x00C1, 0x01E3 },
    { "Amber",  0xFD80, 0xFED1, 0x8AC0, 0x6A00, 0x20A0, 0x3900 },
    { "Blue",   0x45DF, 0xCF5F, 0x1AD0, 0x120B, 0x0083, 0x00C5 },
    { "White",  0xDEDB, 0xFFFF, 0x6B6D, 0x4A49, 0x1082, 0x18E3 },
    { "Purple", 0xC41F, 0xEEDF, 0x5971, 0x410B, 0x0822, 0x1845 },
    { "Red",    0xFA49, 0xFE59, 0x88C3, 0x5882, 0x1800, 0x2820 },
    { "Pink",   0xFBD9, 0xFE9D, 0x996D, 0x68E9, 0x1022, 0x2843 }
};

static const uint8_t PALETTE_COUNT =
    (uint8_t)(sizeof(palettes) / sizeof(palettes[0]));

static uint8_t themeIndex = 0;
static Preferences themePrefs;

void themeApply()
{
    const ThemePalette &p = palettes[themeIndex];

    // Every theme keeps a black background -- the phosphor colour is the
    // whole look, and a coloured page would wash the text out.
    ghColorBg        = 0x0000;
    ghColorFg        = p.fg;
    ghColorAccent    = p.accent;
    ghColorAccentDim = p.accentDim;
    ghColorDim       = p.dim;
    ghColorStatusbar = p.statusbar;
    ghColorSelectBg  = p.selectBg;
}

void themeInit()
{
    themePrefs.begin("ghoultheme", false);

    uint8_t saved = themePrefs.getUChar("idx", 0);
    if (saved >= PALETTE_COUNT)
    {
        saved = 0;
    }
    themeIndex = saved;
    themeApply();
}

uint8_t themeCount()
{
    return PALETTE_COUNT;
}

const char* themeName(uint8_t index)
{
    return (index < PALETTE_COUNT) ? palettes[index].name : "";
}

uint16_t themeFgColor(uint8_t index)
{
    return (index < PALETTE_COUNT) ? palettes[index].fg : 0xFFFF;
}

uint8_t themeGetIndex()
{
    return themeIndex;
}

void themeSetIndex(uint8_t index)
{
    if (index >= PALETTE_COUNT || index == themeIndex)
    {
        return;
    }
    themeIndex = index;
    themeApply();
}

void themeSave()
{
    themePrefs.putUChar("idx", themeIndex);
}
