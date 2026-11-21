//====================================================================
// BleHid.h - Bluetooth LE HID keyboard for Ghoul OS.
//
// Implemented directly on the BLE stack that ships with the ESP32
// Arduino core (BLEDevice / BLEServer / BLEHIDDevice), so there is no
// extra library to install and nothing to keep in sync with a
// third-party fork.
//
// The device advertises as a standard HID keyboard and sends two kinds
// of report: ordinary keyboard keys (arrows, Enter, Escape, typed
// characters) and consumer/media keys (volume up/down).
//====================================================================
#ifndef BLEHID_H
#define BLEHID_H

#include <Arduino.h>

// Starts advertising as "BLE_DEVICE_NAME" (see Config.h). Safe to call
// more than once: the second and later calls do nothing. Called once per
// boot -- the connection is meant to survive leaving the app, so the
// host does not have to re-pair every time.
void bleHidBegin();

// True while a host (PC/phone) is connected and receiving reports.
bool bleHidIsConnected();

// Keystrokes sent from the Keyboard/Remote screens.
void bleHidSendUp();
void bleHidSendDown();
void bleHidSendLeft();
void bleHidSendRight();
void bleHidSendEnter();
void bleHidSendEscape();
void bleHidSendVolumeUp();
void bleHidSendVolumeDown();
void bleHidSendBackspace();
void bleHidSendTab();
void bleHidSendSpace();

// Types one character from the on-screen keyboard (digits, letters,
// '*' and '#'). Characters it cannot map are ignored.
void bleHidSendChar(char c);

#endif // BLEHID_H
