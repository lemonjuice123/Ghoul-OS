#include "BLEKeyboard.h"
#include "Config.h"

#if defined(ESP32)

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <BLEClient.h>

// ==================== Shared BLE state ====================
enum BleMode { BLE_NONE = 0, BLE_HID, BLE_SCANNER };
static BleMode currentBleMode = BLE_NONE;

// ==================== HID (Peripheral) ====================
static BLEServer *pServer = nullptr;
static BLEHIDDevice *pHID = nullptr;
static BLECharacteristic *pInputKB = nullptr;
static BLECharacteristic *pInputCC = nullptr;
static bool hidConnected = false;

static const uint8_t hidReportDescriptor[] = {
    // Keyboard (Report ID 1)
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x05,        //   Report Count (5)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant)
    0xC0,              // End Collection

    // Consumer Control (Report ID 2)
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x03FF)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x03FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0               // End Collection
};

class BleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override { hidConnected = true; }
    void onDisconnect(BLEServer*) override { hidConnected = false; }
};

// ==================== Scanner (Central) ====================
struct BtDevice {
    char name[BT_NAME_LEN];
    char address[18];
    int8_t rssi;
};

static BtDevice btDevices[BT_MAX_DEVICES];
static uint8_t btDeviceCount = 0;
static bool scannerInitialized = false;
static bool btScanning = false;
static bool btClientConnected = false;
static BLEClient *pClient = nullptr;

class BtScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice dev) override {
        if (btDeviceCount >= BT_MAX_DEVICES) return;

        String addr = dev.getAddress().toString();

        for (uint8_t i = 0; i < btDeviceCount; i++)
        {
            if (addr.equals(btDevices[i].address))
            {
                btDevices[i].rssi = dev.getRSSI();
                return;
            }
        }

        BtDevice &d = btDevices[btDeviceCount];
        String n = dev.getName();
        if (n.length() > 0)
        {
            strncpy(d.name, n.c_str(), BT_NAME_LEN - 1);
        }
        else
        {
            strncpy(d.name, "Unknown", BT_NAME_LEN - 1);
        }
        d.name[BT_NAME_LEN - 1] = '\0';
        strncpy(d.address, addr.c_str(), 17);
        d.address[17] = '\0';
        d.rssi = dev.getRSSI();
        btDeviceCount++;
    }
};

class BtClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient*) override { btClientConnected = true; }
    void onDisconnect(BLEClient*) override { btClientConnected = false; }
};

static BtScanCallbacks btScanCbs;
static BtClientCallbacks btClientCbs;

// ==================== HID API ====================
void bleKeyboardInit()
{
    if (currentBleMode == BLE_HID) return;

    if (currentBleMode == BLE_SCANNER)
    {
        BLEScan *scan = BLEDevice::getScan();
        if (scan)
        {
            scan->stop();
            scan->setAdvertisedDeviceCallbacks(nullptr);
        }
        btScannerDisconnect();
        scannerInitialized = false;
        btDeviceCount = 0;
        btScanning = false;
        BLEDevice::deinit(false);
        currentBleMode = BLE_NONE;
    }

    BLEDevice::init("GhoulOS");

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new BleServerCallbacks());

    pHID = new BLEHIDDevice(pServer);
    pInputKB = pHID->inputReport(1);
    pInputCC = pHID->inputReport(2);

    pHID->manufacturer()->setValue("GhoulOS");
    pHID->pnp(0x02, 0x1234, 0x5678, 0x0100);
    pHID->hidInfo(0x00, 0x01);
    pHID->reportMap((uint8_t *)hidReportDescriptor, sizeof(hidReportDescriptor));
    pHID->startServices();

    BLEAdvertising *adv = BLEDevice::getAdvertising();

    BLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setCompleteServices(pHID->hidService()->getUUID());
    advData.setName("GhoulOS");
    advData.setAppearance(0x03C4);
    adv->setAdvertisementData(advData);

    BLEAdvertisementData scanData;
    scanData.setName("GhoulOS");
    adv->setScanResponseData(scanData);

    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x0C);
    BLEDevice::startAdvertising();

    currentBleMode = BLE_HID;
}

void bleKeyboardDeinit()
{
    if (currentBleMode != BLE_HID) return;

    BLEDevice::getAdvertising()->stop();

    delete pHID;
    pServer = nullptr;
    pHID = nullptr;
    pInputKB = nullptr;
    pInputCC = nullptr;
    hidConnected = false;

    BLEDevice::deinit(false);
    currentBleMode = BLE_NONE;
}

bool bleKeyboardIsConnected() { return hidConnected; }
bool bleKeyboardIsActive()    { return currentBleMode == BLE_HID; }

void bleKeyboardSendKey(uint8_t modifiers, uint8_t keyCode)
{
    if (!hidConnected || !pInputKB) return;
    uint8_t report[8] = {};
    report[0] = modifiers;
    report[2] = keyCode;
    pInputKB->setValue(report, sizeof(report));
    pInputKB->notify();
}

void bleKeyboardReleaseKey()
{
    if (!hidConnected || !pInputKB) return;
    uint8_t report[8] = {};
    pInputKB->setValue(report, sizeof(report));
    pInputKB->notify();
}

void bleKeyboardSendConsumer(uint16_t usageCode)
{
    if (!hidConnected || !pInputCC) return;
    uint8_t report[2];
    report[0] = usageCode & 0xFF;
    report[1] = (usageCode >> 8) & 0xFF;
    pInputCC->setValue(report, sizeof(report));
    pInputCC->notify();
}

void bleKeyboardReleaseConsumer()
{
    if (!hidConnected || !pInputCC) return;
    uint8_t report[2] = {0, 0};
    pInputCC->setValue(report, sizeof(report));
    pInputCC->notify();
}

// ==================== Scanner API ====================
void btScannerInit()
{
    if (currentBleMode == BLE_SCANNER) return;

    if (currentBleMode == BLE_HID)
    {
        bleKeyboardDeinit();
    }

    BLEDevice::init("GhoulOS");

    btDeviceCount = 0;
    btClientConnected = false;
    pClient = nullptr;

    BLEScan *pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(&btScanCbs);
    pScan->setActiveScan(true);
    pScan->setInterval(1349);
    pScan->setWindow(1249);
    pScan->start(10, false);

    btScanning = true;
    scannerInitialized = true;
    currentBleMode = BLE_SCANNER;
}

void btScannerDeinit()
{
    if (currentBleMode != BLE_SCANNER) return;

    BLEScan *pScan = BLEDevice::getScan();
    if (pScan)
    {
        pScan->stop();
        pScan->setAdvertisedDeviceCallbacks(nullptr);
    }

    btScannerDisconnect();

    scannerInitialized = false;
    btDeviceCount = 0;
    btScanning = false;

    BLEDevice::deinit(false);
    currentBleMode = BLE_NONE;
}

bool btScannerIsScanning()
{
    if (currentBleMode != BLE_SCANNER || !btScanning) return false;
    BLEScan *pScan = BLEDevice::getScan();
    if (pScan && !pScan->isScanning())
    {
        btScanning = false;
        return false;
    }
    return true;
}
bool btScannerIsActive()      { return scannerInitialized; }
bool btScannerIsConnected()   { return btClientConnected; }
uint8_t btScannerDeviceCount(){ return btDeviceCount; }

const char* btScannerDeviceName(uint8_t index)
{
    if (index >= btDeviceCount) return "";
    return btDevices[index].name;
}

int8_t btScannerDeviceRSSI(uint8_t index)
{
    if (index >= btDeviceCount) return 0;
    return btDevices[index].rssi;
}

bool btScannerConnect(uint8_t index)
{
    if (index >= btDeviceCount) return false;
    if (btClientConnected) return false;

    BLEScan *pScan = BLEDevice::getScan();
    if (pScan) pScan->stop();
    btScanning = false;

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(&btClientCbs);

    BLEAddress addr(btDevices[index].address);
    bool ok = pClient->connect(addr);

    if (ok)
    {
        btClientConnected = true;
    }
    else
    {
        delete pClient;
        pClient = nullptr;
    }

    return ok;
}

void btScannerDisconnect()
{
    if (pClient)
    {
        if (btClientConnected)
        {
            pClient->disconnect();
        }
        delete pClient;
        pClient = nullptr;
    }
    btClientConnected = false;
}

#else

void bleKeyboardInit() {}
void bleKeyboardDeinit() {}
bool bleKeyboardIsConnected() { return false; }
bool bleKeyboardIsActive() { return false; }
void bleKeyboardSendKey(uint8_t, uint8_t) {}
void bleKeyboardReleaseKey() {}
void bleKeyboardSendConsumer(uint16_t) {}
void bleKeyboardReleaseConsumer() {}
void btScannerInit() {}
void btScannerDeinit() {}
bool btScannerIsScanning() { return false; }
bool btScannerIsActive() { return false; }
bool btScannerIsConnected() { return false; }
uint8_t btScannerDeviceCount() { return 0; }
const char* btScannerDeviceName(uint8_t) { return ""; }
int8_t btScannerDeviceRSSI(uint8_t) { return 0; }
bool btScannerConnect(uint8_t) { return false; }
void btScannerDisconnect() {}

#endif
