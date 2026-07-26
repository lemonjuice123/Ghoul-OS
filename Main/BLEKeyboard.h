#ifndef BLE_KEYBOARD_H
#define BLE_KEYBOARD_H

#include <Arduino.h>

void bleKeyboardInit();
void bleKeyboardDeinit();
bool bleKeyboardIsConnected();
bool bleKeyboardIsActive();
void bleKeyboardSendKey(uint8_t modifiers, uint8_t keyCode);
void bleKeyboardReleaseKey();
void bleKeyboardSendConsumer(uint16_t usageCode);
void bleKeyboardReleaseConsumer();

void btScannerInit();
void btScannerDeinit();
bool btScannerIsScanning();
bool btScannerIsActive();
bool btScannerIsConnected();
uint8_t btScannerDeviceCount();
const char* btScannerDeviceName(uint8_t index);
int8_t btScannerDeviceRSSI(uint8_t index);
bool btScannerConnect(uint8_t index);
void btScannerDisconnect();

#endif // BLE_KEYBOARD_H
