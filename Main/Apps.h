//====================================================================
// Apps.h – declarations for the Ghoul OS application table.
//====================================================================
#ifndef APPS_H
#define APPS_H

#include <Arduino.h>

// ---------------------------------------------------------------------
// Core app struct and launch function prototype type.
// ---------------------------------------------------------------------
typedef void (*AppLaunchFn)();

struct App {
    const char*   name;
    const char*   description;
    const uint16_t* icon;
    AppLaunchFn   launch;
};

// Generic placeholder launch (used for apps without dedicated UI).
void genericAppLaunch();

// Launch stubs – each must be defined somewhere (Apps.cpp).
void launchWifiScanner();
void launchStockWatch();
void launchGames();
void launchPackMon();
void launchAbout();
void launchSettings();
void launchFiles();
void launchMiniPiano();
void launchKeyTest();

// App table and count – defined in Apps.cpp.
extern const App appList[];
extern const uint8_t APP_COUNT;

#endif // APPS_H
