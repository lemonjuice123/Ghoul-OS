#include "Apps.h"
#include "Icons.h"

// =====================================================================
// Ghoul OS - Apps.cpp
//
// NOTE: "Settings" and "Key Test" get real, dedicated screens (theme
// picker / live keypad visualizer) instead of the generic "Coming
// Soon" placeholder. UI.cpp recognizes them by comparing their launch
// function pointer against launchSettings / launchKeyTest, so no
// other part of the framework needs to know about them specifically.
// "PackMon" also gets a real, dedicated screen (live packet-rate
// graph) the same way -- UI.cpp recognizes it by comparing the launch
// function pointer against launchPackMon, same mechanism as above.
// =====================================================================

// All other apps currently share this generic placeholder behaviour.
void genericAppLaunch()
{
    // Placeholder: future per-app logic goes here.
}

void launchWifiScanner() { genericAppLaunch(); }
void launchBluetooth()   { genericAppLaunch(); }
void launchFiles()       { genericAppLaunch(); }
void launchMiniPiano()   { genericAppLaunch(); }
void launchSettings()    { genericAppLaunch(); }
void launchBLECMD()      { genericAppLaunch(); }
void launchGames()       { genericAppLaunch(); }
void launchAbout()       { genericAppLaunch(); }
void launchKeyTest()     { genericAppLaunch(); }
void launchPackMon()     { genericAppLaunch(); }

// Master application table shown in the launcher carousel, in order.
const App appList[] = {
    { "WiFi Scanner", "Scan nearby wireless networks",   iconWifi,      launchWifiScanner },
    { "PackMon",      "Live 802.11 packet monitor",      iconPackMon,   launchPackMon     },
    { "Bluetooth",    "Discover and pair BT devices",    iconBluetooth, launchBluetooth   },
    { "Files",        "Browse the SD card file system",  iconFiles,     launchFiles       },
    { "MiniPiano",    "9-key mini piano (keys 1-9)",     iconMusic,     launchMiniPiano   },
    { "Key Test",     "See each keypad press live",      iconKeyTest,   launchKeyTest     },
    { "Settings",     "Toggle audio feedback on/off",    iconSettings,  launchSettings    },
    { "BLECMD",       "BLE HID keyboard",                iconTerminal,  launchBLECMD      },
    { "Games",        "Launch built-in mini games",      iconGames,     launchGames       },
    { "About",        "Device and firmware information", iconAbout,     launchAbout       },
};

const uint8_t APP_COUNT = sizeof(appList) / sizeof(appList[0]);
