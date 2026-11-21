//====================================================================
// BleHid.cpp - Bluetooth LE HID keyboard for Ghoul OS.
//
// Implements a standard HID-over-GATT keyboard with two reports:
//   report 1: modifier byte + reserved byte + 6 key slots
//   report 2: 16-bit consumer-control bitfield (volume up/down etc.)
//====================================================================
#include <Arduino.h>
#include "BleHid.h"
#include "Config.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLECharacteristic.h>
#include <BLEAdvertising.h>

// ---------------------------------------------------------------------
// Report IDs (must match the report map below).
// ---------------------------------------------------------------------
#define BLE_HID_REPORT_KEYBOARD 0x01
#define BLE_HID_REPORT_MEDIA    0x02

// Consumer-control bits inside report 2 (bit order follows the report map).
#define BLE_HID_MEDIA_VOLUME_UP   (1u << 5)
#define BLE_HID_MEDIA_VOLUME_DOWN (1u << 6)

// HID keyboard usage IDs for the non-printing keys.
#define BLE_HID_KEY_ENTER     0x28
#define BLE_HID_KEY_ESCAPE    0x29
#define BLE_HID_KEY_BACKSPACE 0x2A
#define BLE_HID_KEY_TAB       0x2B
#define BLE_HID_KEY_SPACE     0x2C
#define BLE_HID_KEY_RIGHT     0x4F
#define BLE_HID_KEY_LEFT      0x50
#define BLE_HID_KEY_DOWN      0x51
#define BLE_HID_KEY_UP        0x52

// Modifier bits.
#define BLE_HID_MOD_LEFT_SHIFT 0x02

// ---------------------------------------------------------------------
// HID report descriptor: a boot-style keyboard (report 1) plus a
// 16-bit consumer control (report 2). Byte-for-byte the usual layout
// so every host (Windows, Linux, macOS, Android) recognises it.
// ---------------------------------------------------------------------
static const uint8_t bleHidReportMap[] = {
    // ---- Report 1: keyboard ----
    0x05, 0x01,        // USAGE_PAGE (Generic Desktop)
    0x09, 0x06,        // USAGE (Keyboard)
    0xA1, 0x01,        // COLLECTION (Application)
    0x85, 0x01,        //   REPORT_ID (1)
    0x05, 0x07,        //   USAGE_PAGE (Keyboard/Keypad)
    0x19, 0xE0,        //   USAGE_MINIMUM (Left Control)
    0x29, 0xE7,        //   USAGE_MAXIMUM (Right GUI)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,        //   REPORT_SIZE (1)
    0x95, 0x08,        //   REPORT_COUNT (8)
    0x81, 0x02,        //   INPUT (Data, Variable, Absolute)  -- modifiers
    0x95, 0x01,        //   REPORT_COUNT (1)
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x81, 0x01,        //   INPUT (Constant)                  -- reserved byte
    0x95, 0x05,        //   REPORT_COUNT (5)                  -- LED bits
    0x75, 0x01,        //   REPORT_SIZE (1)
    0x05, 0x08,        //   USAGE_PAGE (LEDs)
    0x19, 0x01,        //   USAGE_MINIMUM (Num Lock)
    0x29, 0x05,        //   USAGE_MAXIMUM (Kana)
    0x91, 0x02,        //   OUTPUT (Data, Variable, Absolute)
    0x95, 0x01,        //   REPORT_COUNT (1)                  -- LED padding
    0x75, 0x03,        //   REPORT_SIZE (3)
    0x91, 0x01,        //   OUTPUT (Constant)
    0x95, 0x06,        //   REPORT_COUNT (6)                  -- 6 key slots
    0x75, 0x08,        //   REPORT_SIZE (8)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x65,        //   LOGICAL_MAXIMUM (101)
    0x05, 0x07,        //   USAGE_PAGE (Keyboard/Keypad)
    0x19, 0x00,        //   USAGE_MINIMUM (0)
    0x29, 0x65,        //   USAGE_MAXIMUM (101)
    0x81, 0x00,        //   INPUT (Data, Array)
    0xC0,              // END_COLLECTION

    // ---- Report 2: consumer control (media keys) ----
    0x05, 0x0C,        // USAGE_PAGE (Consumer Devices)
    0x09, 0x01,        // USAGE (Consumer Control)
    0xA1, 0x01,        // COLLECTION (Application)
    0x85, 0x02,        //   REPORT_ID (2)
    0x15, 0x00,        //   LOGICAL_MINIMUM (0)
    0x25, 0x01,        //   LOGICAL_MAXIMUM (1)
    0x75, 0x01,        //   REPORT_SIZE (1)
    0x95, 0x10,        //   REPORT_COUNT (16)                 -- 16 bits
    0x09, 0xB5,        //   USAGE (Scan Next Track)      bit 0
    0x09, 0xB6,        //   USAGE (Scan Previous Track)  bit 1
    0x09, 0xB7,        //   USAGE (Stop)                 bit 2
    0x09, 0xCD,        //   USAGE (Play/Pause)           bit 3
    0x09, 0xE2,        //   USAGE (Mute)                 bit 4
    0x09, 0xE9,        //   USAGE (Volume Increment)     bit 5
    0x09, 0xEA,        //   USAGE (Volume Decrement)     bit 6
    0x0A, 0x23, 0x02,  //   USAGE (WWW Home)             bit 7
    0x0A, 0x94, 0x01,  //   USAGE (My Computer)          bit 8
    0x0A, 0x92, 0x01,  //   USAGE (Calculator)           bit 9
    0x0A, 0x2A, 0x02,  //   USAGE (WWW Favourites)       bit 10
    0x0A, 0x21, 0x02,  //   USAGE (WWW Search)           bit 11
    0x0A, 0x26, 0x02,  //   USAGE (WWW Stop)             bit 12
    0x0A, 0x24, 0x02,  //   USAGE (WWW Back)             bit 13
    0x0A, 0x83, 0x01,  //   USAGE (Media Selection)      bit 14
    0x0A, 0x8A, 0x01,  //   USAGE (Mail)                 bit 15
    0x81, 0x02,        //   INPUT (Data, Variable, Absolute)
    0xC0               // END_COLLECTION
};

// ---------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------
static bool bleStarted = false;
static bool bleConnected = false;

static BLECharacteristic *bleInputKeyboard = nullptr;
static BLECharacteristic *bleInputMedia = nullptr;

// ---------------------------------------------------------------------
// Connection tracking. Re-advertising on disconnect is what makes the
// keyboard reconnectable instead of dead after the first session.
// ---------------------------------------------------------------------
class BleHidServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *server) override
    {
        (void)server;
        bleConnected = true;
    }

    void onDisconnect(BLEServer *server) override
    {
        (void)server;
        bleConnected = false;
        BLEDevice::startAdvertising();
    }
};

// ---------------------------------------------------------------------
// Report helpers
// ---------------------------------------------------------------------

// Sends one key press and release. The short gap gives the host time to
// see the press before the release, so a single tap registers reliably.
static void bleHidSendKeyReport(uint8_t modifiers, uint8_t keycode)
{
    if (!bleConnected || bleInputKeyboard == nullptr)
    {
        return;
    }

    uint8_t press[8] = { modifiers, 0x00, keycode, 0x00, 0x00, 0x00, 0x00, 0x00 };
    bleInputKeyboard->setValue(press, sizeof(press));
    bleInputKeyboard->notify();
    delay(8);

    uint8_t release[8] = { 0x00 };
    bleInputKeyboard->setValue(release, sizeof(release));
    bleInputKeyboard->notify();
    delay(8);
}

static void bleHidSendMediaReport(uint16_t bits)
{
    if (!bleConnected || bleInputMedia == nullptr)
    {
        return;
    }

    uint8_t press[2] = { (uint8_t)(bits & 0xFF), (uint8_t)(bits >> 8) };
    bleInputMedia->setValue(press, sizeof(press));
    bleInputMedia->notify();
    delay(8);

    uint8_t release[2] = { 0x00, 0x00 };
    bleInputMedia->setValue(release, sizeof(release));
    bleInputMedia->notify();
    delay(8);
}

// Maps one printable ASCII character to a HID keycode + optional shift.
// Returns 0 for characters this keypad can never produce.
static uint8_t bleHidCharToKeycode(char c, uint8_t *modifiers)
{
    *modifiers = 0;

    if (c >= 'a' && c <= 'z')
    {
        return (uint8_t)(0x04 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'Z')
    {
        *modifiers = BLE_HID_MOD_LEFT_SHIFT;
        return (uint8_t)(0x04 + (c - 'A'));
    }
    if (c >= '1' && c <= '9')
    {
        return (uint8_t)(0x1E + (c - '1'));
    }

    switch (c)
    {
        case '0':  return 0x27;
        case ' ':  return 0x2C;
        case '\n':
        case '\r': return BLE_HID_KEY_ENTER;

        // Shifted number-row characters the keypad can send.
        case '*':  *modifiers = BLE_HID_MOD_LEFT_SHIFT; return 0x25; // Shift+8
        case '#':  *modifiers = BLE_HID_MOD_LEFT_SHIFT; return 0x20; // Shift+3

        default:   return 0x00;
    }
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

void bleHidBegin()
{
    if (bleStarted)
    {
        return;
    }
    bleStarted = true;

    BLEDevice::init(BLE_DEVICE_NAME);

    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new BleHidServerCallbacks());

    BLEHIDDevice *hid = new BLEHIDDevice(server);
    if (hid == nullptr)
    {
        return;
    }

    bleInputKeyboard = hid->inputReport(BLE_HID_REPORT_KEYBOARD);
    bleInputMedia    = hid->inputReport(BLE_HID_REPORT_MEDIA);

    // 0x02 = USB, then vendor/product/version -- any sane HID identity.
    hid->pnp(0x02, 0x05AC, 0x820A, 0x0210);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t *)bleHidReportMap, sizeof(bleHidReportMap));
    hid->setBatteryLevel(100);
    hid->startServices();

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    if (advertising != nullptr)
    {
        advertising->setAppearance(0x03C1); // HID keyboard
        advertising->addServiceUUID(hid->hidService()->getUUID());
        advertising->start();
    }
}

bool bleHidIsConnected()
{
    return bleConnected;
}

void bleHidSendUp()    { bleHidSendKeyReport(0, BLE_HID_KEY_UP);    }
void bleHidSendDown()  { bleHidSendKeyReport(0, BLE_HID_KEY_DOWN);  }
void bleHidSendLeft()  { bleHidSendKeyReport(0, BLE_HID_KEY_LEFT);  }
void bleHidSendRight() { bleHidSendKeyReport(0, BLE_HID_KEY_RIGHT); }
void bleHidSendEnter() { bleHidSendKeyReport(0, BLE_HID_KEY_ENTER); }
void bleHidSendEscape(){ bleHidSendKeyReport(0, BLE_HID_KEY_ESCAPE); }

void bleHidSendBackspace() { bleHidSendKeyReport(0, BLE_HID_KEY_BACKSPACE); }
void bleHidSendTab()       { bleHidSendKeyReport(0, BLE_HID_KEY_TAB);       }
void bleHidSendSpace()     { bleHidSendKeyReport(0, BLE_HID_KEY_SPACE);     }

void bleHidSendVolumeUp()
{
    bleHidSendMediaReport(BLE_HID_MEDIA_VOLUME_UP);
}

void bleHidSendVolumeDown()
{
    bleHidSendMediaReport(BLE_HID_MEDIA_VOLUME_DOWN);
}

void bleHidSendChar(char c)
{
    if (c == 0)
    {
        return;
    }

    uint8_t modifiers = 0;
    uint8_t keycode = bleHidCharToKeycode(c, &modifiers);
    if (keycode == 0)
    {
        return;
    }

    bleHidSendKeyReport(modifiers, keycode);
}
