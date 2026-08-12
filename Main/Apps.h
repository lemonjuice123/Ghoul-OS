#ifndef APPS_H
#define APPS_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - Apps.h
//
// Declares the App struct/table and every app's launch function.
// Definitions live in Apps.cpp only -- this header must stay
// declarations-only, since it's included from multiple translation
// units (Apps.cpp, GHOULSOS.ino, UI.cpp via UI.h). Defining functions
// or globals here would give each of those .cpp files its own copy,
// which the linker rejects as a duplicate ("multiple definition")
// symbol.
//
// NOTE: "Settings" and "Key Test" get real, dedicated screens (theme
// picker / live keypad visualizer) instead of the generic "Coming
// Soon" placeholder. UI.cpp recognizes them by comparing their launch
// function pointer against launchSettings / launchKeyTest, so no
// other part of the framework needs to know about them specifically.
// =====================================================================

// Function pointer type used by every app's launch handler.
typedef void (*AppLaunchFn)();

struct App
{
    const char*   name;
    const char*   description;
    const uint16_t* icon;
    AppLaunchFn   launch;
};

// All other apps currently share this generic placeholder behaviour.
void genericAppLaunch();

void launchWifiScanner();
void launchBluetooth();
void launchFiles();
void launchMiniPiano();
void launchSettings();
void launchBLECMD();
void launchGames();
void launchAbout();
void launchKeyTest();
void launchPackMon();

// Master application table shown in the launcher carousel, in order.
extern const App appList[];
extern const uint8_t APP_COUNT;

#endif // APPS_H
