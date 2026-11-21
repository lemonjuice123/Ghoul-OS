#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "Prefs.h"

// =====================================================================
// Ghoul OS - Prefs.cpp
// =====================================================================

#define PREFS_NAMESPACE "ghoulprefs"
#define SPLASH_PATH_LEN 96

static Preferences prefs;
static bool prefsBegun = false;

static char    splashPath[SPLASH_PATH_LEN] = "";
static uint8_t sleepIndex = 0;

// One entry per selectable timeout. Index 0 is "OFF" (never sleep).
static const char* const sleepNames[]      = { "OFF", "15 SEC", "30 SEC", "1 MIN", "2 MIN", "5 MIN", "10 MIN" };
static const char* const sleepShortNames[] = { "OFF", "15s",    "30s",    "1m",    "2m",    "5m",    "10m" };
static const uint16_t    sleepSeconds[]    = { 0,     15,       30,       60,      120,     300,     600 };

#define SLEEP_OPTION_COUNT (sizeof(sleepNames) / sizeof(sleepNames[0]))

static void ensureBegun()
{
    if (!prefsBegun)
    {
        prefsBegun = prefs.begin(PREFS_NAMESPACE, false);
    }
}

void prefsInit()
{
    splashPath[0] = '\0';

    ensureBegun();
    if (!prefsBegun) return;

    String stored = prefs.getString("splash", "");
    if (stored.length() > 0)
    {
        strncpy(splashPath, stored.c_str(), sizeof(splashPath) - 1);
        splashPath[sizeof(splashPath) - 1] = '\0';
    }

    sleepIndex = prefs.getUChar("sleep", 0);
    if (sleepIndex >= SLEEP_OPTION_COUNT) sleepIndex = 0;
}

const char* prefsSplashPath()
{
    return splashPath;
}

void prefsSetSplashPath(const char* path)
{
    if (path == nullptr) path = "";

    strncpy(splashPath, path, sizeof(splashPath) - 1);
    splashPath[sizeof(splashPath) - 1] = '\0';

    ensureBegun();
    if (prefsBegun) prefs.putString("splash", splashPath);
}

uint8_t prefsSleepCount()
{
    return (uint8_t)SLEEP_OPTION_COUNT;
}

const char* prefsSleepName(uint8_t index)
{
    if (index >= SLEEP_OPTION_COUNT) index = 0;
    return sleepNames[index];
}

const char* prefsSleepShortName(uint8_t index)
{
    if (index >= SLEEP_OPTION_COUNT) index = 0;
    return sleepShortNames[index];
}

uint16_t prefsSleepSeconds(uint8_t index)
{
    if (index >= SLEEP_OPTION_COUNT) index = 0;
    return sleepSeconds[index];
}

uint8_t prefsSleepIndex()
{
    return sleepIndex;
}

void prefsSetSleepIndex(uint8_t index)
{
    if (index >= SLEEP_OPTION_COUNT) index = 0;

    sleepIndex = index;

    ensureBegun();
    if (prefsBegun) prefs.putUChar("sleep", sleepIndex);
}
