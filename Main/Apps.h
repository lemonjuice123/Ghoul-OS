#ifndef APPS_H
#define APPS_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - Apps.h
//
// The application framework. Every entry in the launcher is described
// by one App struct: a name, a short description, a pointer to its
// icon bitmap and a launch callback that runs when the user opens it.
//
// TO ADD A NEW APP IN THE FUTURE, you only need to:
//   1. Add an icon bitmap to Icons.h
//   2. Add a launch function to Apps.cpp
//   3. Add one line to the appList[] table in Apps.cpp
// Nothing else in the project needs to change.
// =====================================================================

struct App
{
    const char* name;
    const char* description;
    const uint16_t* icon;
    void (*launch)();
};

extern const App appList[];
extern const uint8_t APP_COUNT;

// Generic placeholder launch handler shared by every app for now.
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

#endif // APPS_H
