#include <string.h>
#include "WifiScan.h"
#include "Config.h"

// =====================================================================
// Ghoul OS - WifiScan.cpp
// =====================================================================

#if defined(ESP32)

#include <WiFi.h>

struct WifiNetworkEntry {
    char    ssid[WIFI_SCAN_SSID_LEN];
    int8_t  rssi;
    uint8_t channel;
    bool    encrypted;
};

static WifiNetworkEntry networks[WIFI_SCAN_MAX_NETWORKS];
static uint8_t networkCount = 0;
static bool active = false;    // app is open / radio owned by the scanner
static bool scanning = false;  // an async scan is currently in flight

static void copyResultsAndSort(int16_t n)
{
    uint8_t count = (n > WIFI_SCAN_MAX_NETWORKS) ? WIFI_SCAN_MAX_NETWORKS : (uint8_t)n;

    for (uint8_t i = 0; i < count; i++)
    {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0)
        {
            // Leave blank -- wifiScanSSID() reports "(hidden)" for this case
            // so callers never have to special-case it themselves.
            networks[i].ssid[0] = '\0';
        }
        else
        {
            strncpy(networks[i].ssid, ssid.c_str(), WIFI_SCAN_SSID_LEN - 1);
            networks[i].ssid[WIFI_SCAN_SSID_LEN - 1] = '\0';
        }

        networks[i].rssi      = (int8_t)WiFi.RSSI(i);
        networks[i].channel   = (uint8_t)WiFi.channel(i);
        networks[i].encrypted = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    networkCount = count;

    // Small insertion sort, strongest signal first -- networkCount is
    // small (<= WIFI_SCAN_MAX_NETWORKS) so O(n^2) is plenty fast here.
    for (uint8_t i = 1; i < networkCount; i++)
    {
        WifiNetworkEntry tmp = networks[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && networks[j].rssi < tmp.rssi)
        {
            networks[j + 1] = networks[j];
            j--;
        }
        networks[j + 1] = tmp;
    }

    // Frees the scan-result memory the WiFi driver was holding.
    WiFi.scanDelete();
}

void wifiScanInit()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    networkCount = 0;
    active = true;

    wifiScanStart();
}

void wifiScanDeinit()
{
    if (scanning)
    {
        WiFi.scanDelete();
    }
    WiFi.mode(WIFI_OFF);

    active = false;
    scanning = false;
    networkCount = 0;
}

bool wifiScanIsActive() { return active; }

void wifiScanStart()
{
    if (scanning) return;

    // async = true (never blocks the UI), show_hidden = false.
    WiFi.scanNetworks(true, false);
    scanning = true;
}

bool wifiScanIsScanning() { return scanning; }

bool wifiScanTick()
{
    if (!scanning)
    {
        return false;
    }

    int16_t result = WiFi.scanComplete(); // -2 = failed/none, -1 = running, >=0 = done

    if (result == WIFI_SCAN_RUNNING)
    {
        return false;
    }

    if (result == WIFI_SCAN_FAILED)
    {
        networkCount = 0;
        scanning = false;
        return true;
    }

    copyResultsAndSort(result);
    scanning = false;
    return true;
}

uint8_t wifiScanNetworkCount() { return networkCount; }

const char* wifiScanSSID(uint8_t index)
{
    if (index >= networkCount) return "";
    return (networks[index].ssid[0] == '\0') ? "(hidden)" : networks[index].ssid;
}

int8_t wifiScanRSSI(uint8_t index)
{
    return (index < networkCount) ? networks[index].rssi : 0;
}

uint8_t wifiScanChannel(uint8_t index)
{
    return (index < networkCount) ? networks[index].channel : 0;
}

bool wifiScanIsEncrypted(uint8_t index)
{
    return (index < networkCount) ? networks[index].encrypted : false;
}

#else

void wifiScanInit() {}
void wifiScanDeinit() {}
bool wifiScanIsActive() { return false; }
void wifiScanStart() {}
bool wifiScanIsScanning() { return false; }
bool wifiScanTick() { return false; }
uint8_t wifiScanNetworkCount() { return 0; }
const char* wifiScanSSID(uint8_t) { return ""; }
int8_t wifiScanRSSI(uint8_t) { return 0; }
uint8_t wifiScanChannel(uint8_t) { return 0; }
bool wifiScanIsEncrypted(uint8_t) { return false; }

#endif
