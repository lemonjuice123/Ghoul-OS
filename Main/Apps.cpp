// =====================================================================
// Apps.cpp – definitions for the Ghoul OS app table and launch stubs.
// =====================================================================

#include "Apps.h"
#include "Icons.h"

// --------------------------------------------------------------------------
// Forward declarations for the launch functions referenced from UI.cpp.
// Each launch is a simple stub; real functionality can be added later.
// --------------------------------------------------------------------------
void launchWifiScanner() {}
void launchStockWatch() {}
void launchGames()      {}
void launchPackMon()    {}
void launchAbout()      {}
void launchSettings()   {}
void launchFiles()      {}
void launchMiniPiano()  {}
void launchKeyTest()    {}

// --------------------------------------------------------------------------
// App table – each entry provides name, description, optional icon, and launch.
// --------------------------------------------------------------------------
const App appList[] = {
    { "WiFi Scanner", "Scan nearby wireless networks",   iconWifi,      launchWifiScanner },
    { "StockWatch",   "Live stock prices via Finnhub",   iconWifi,      launchStockWatch  },
    { "PackMon",      "Live 802.11 packet monitor",      iconPackMon,   launchPackMon },
    { "Files",        "Browse the SD card file system",  iconFiles,     launchFiles       },
    { "MiniPiano",    "9-key mini piano (keys 1-9)",     iconMusic,     launchMiniPiano   },
    { "Key Test",     "See each keypad press live",      iconKeyTest,   launchKeyTest     },
    { "Settings",     "Toggle audio feedback on/off",    iconSettings,  launchSettings    },
    { "Games",        "Launch built-in mini games",      iconGames,     launchGames       },
    { "About",        "Device and firmware information", iconAbout,     launchAbout       }
};

const uint8_t APP_COUNT = sizeof(appList) / sizeof(appList[0]);
