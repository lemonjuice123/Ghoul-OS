#ifndef WIFISCAN_H
#define WIFISCAN_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - WifiScan.h
//
// Thin wrapper around the ESP32 core's WiFi.scanNetworks() that the
// "WiFi Scanner" app's UI drives. Scans run asynchronously (the radio
// scan happens in the background) so the UI never blocks/freezes while
// a scan is in progress -- wifiScanTick() just polls WiFi.scanComplete()
// each frame and copies the results over once they're ready.
//
// Results are cached locally (up to WIFI_SCAN_MAX_NETWORKS) and kept
// sorted strongest-signal-first so the list is immediately useful
// without the UI having to do any sorting of its own.
// =====================================================================

#define WIFI_SCAN_MAX_NETWORKS 20
#define WIFI_SCAN_SSID_LEN     33 // 32 chars max SSID + NUL

// Puts the radio into station mode (disconnected) and kicks off the
// first async scan. Safe to call again (e.g. re-entering the app) --
// it just starts a fresh scan.
void wifiScanInit();

// Releases the WiFi radio (mode set to off) so other apps (PackMon,
// a future WiFi client, etc.) can use it again. Call when leaving the
// app.
void wifiScanDeinit();

// True from wifiScanInit()/wifiScanDeinit() bookkeeping -- used by the
// status bar to light up the WiFi icon while the app is open.
bool wifiScanIsActive();

// Kicks off a new async scan without touching WiFi mode/init state.
// Used for the in-app "rescan" action. No-op if a scan is already
// running.
void wifiScanStart();

// True while an async scan is in progress.
bool wifiScanIsScanning();

// Call regularly (e.g. every uiTick()) while the app is open. Polls
// the in-progress scan and, the moment it completes, copies + sorts
// the results into the local cache. Returns true exactly on the tick
// where fresh results became available, so the caller knows when it's
// worth redrawing.
bool wifiScanTick();

uint8_t     wifiScanNetworkCount();
const char* wifiScanSSID(uint8_t index);          // "(hidden)" if the SSID is blank
int8_t      wifiScanRSSI(uint8_t index);           // dBm
uint8_t     wifiScanChannel(uint8_t index);
bool        wifiScanIsEncrypted(uint8_t index);     // false only for fully open networks

#endif // WIFISCAN_H
