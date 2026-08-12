#include <string.h>
#include "PacketMon.h"
#include "Config.h"

#if defined(ESP32)

#include <WiFi.h>
#include <esp_wifi.h>

// ==================== Shared sniffer state ====================
static portMUX_TYPE pktMux = portMUX_INITIALIZER_UNLOCKED;

// Written from the promiscuous-mode callback (WiFi driver task context),
// read/cleared from packetMonTick() (main loop context) -- always under
// pktMux, since the two run on different tasks/cores.
static volatile uint32_t pktCounter = 0;
static volatile int32_t  rssiSum = 0;
static volatile uint32_t rssiCount = 0;

static bool sniffing = false;
static uint8_t currentChannel = 1;
static bool autoHop = false;
static unsigned long lastHopTime = 0;

static uint8_t history[PACKETMON_HISTORY_LEN];
static uint8_t historyHead = 0; // index of the OLDEST sample (next slot to overwrite)
static unsigned long lastBinTime = 0;

static uint32_t lastPktRate = 0;
static int8_t   lastAvgRssi = 0;

// Runs in the WiFi driver's own task -- keep it tiny, never touch the
// TFT/SD/BLE from here, and never block.
static void IRAM_ATTR promiscCallback(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA && type != WIFI_PKT_CTRL)
    {
        return;
    }

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;

    portENTER_CRITICAL_ISR(&pktMux);
    pktCounter++;
    rssiSum += pkt->rx_ctrl.rssi;
    rssiCount++;
    portEXIT_CRITICAL_ISR(&pktMux);
}

void packetMonInit()
{
    if (sniffing)
    {
        return;
    }

    // Passive sniffing just needs the radio up, not associated to
    // anything -- make sure nothing's left joined from a previous app.
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect();

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&promiscCallback);
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);

    portENTER_CRITICAL(&pktMux);
    pktCounter = 0;
    rssiSum = 0;
    rssiCount = 0;
    portEXIT_CRITICAL(&pktMux);

    memset(history, 0, sizeof(history));
    historyHead = 0;
    lastPktRate = 0;
    lastAvgRssi = 0;

    lastBinTime = millis();
    lastHopTime = millis();
    autoHop = false;

    sniffing = true;
}

void packetMonDeinit()
{
    if (!sniffing)
    {
        return;
    }

    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WiFi.mode(WIFI_MODE_NULL);

    sniffing = false;
}

bool packetMonIsActive() { return sniffing; }

void packetMonSetChannel(uint8_t channel)
{
    if (channel < PACKETMON_CHANNEL_MIN) channel = PACKETMON_CHANNEL_MIN;
    if (channel > PACKETMON_CHANNEL_MAX) channel = PACKETMON_CHANNEL_MAX;

    currentChannel = channel;
    if (sniffing)
    {
        esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    }
}

void packetMonNextChannel()
{
    packetMonSetChannel(currentChannel >= PACKETMON_CHANNEL_MAX ? PACKETMON_CHANNEL_MIN : currentChannel + 1);
}

void packetMonPrevChannel()
{
    packetMonSetChannel(currentChannel <= PACKETMON_CHANNEL_MIN ? PACKETMON_CHANNEL_MAX : currentChannel - 1);
}

uint8_t packetMonGetChannel() { return currentChannel; }

void packetMonSetAutoHop(bool enabled)
{
    autoHop = enabled;
    lastHopTime = millis();
}

bool packetMonIsAutoHop() { return autoHop; }

uint32_t packetMonGetPacketRate() { return lastPktRate; }
int8_t   packetMonGetAvgRSSI()    { return lastAvgRssi; }

uint8_t packetMonHistoryLen() { return PACKETMON_HISTORY_LEN; }

uint8_t packetMonHistoryAt(uint8_t indexFromOldest)
{
    if (indexFromOldest >= PACKETMON_HISTORY_LEN)
    {
        return 0;
    }
    uint8_t idx = (uint8_t)((historyHead + indexFromOldest) % PACKETMON_HISTORY_LEN);
    return history[idx];
}

bool packetMonTick()
{
    if (!sniffing)
    {
        return false;
    }

    unsigned long now = millis();

    if (autoHop && (now - lastHopTime >= PACKETMON_HOP_INTERVAL_MS))
    {
        lastHopTime = now;
        packetMonNextChannel();
    }

    if (now - lastBinTime < PACKETMON_BIN_INTERVAL_MS)
    {
        return false;
    }
    lastBinTime = now;

    uint32_t count;
    int32_t  rSum;
    uint32_t rCount;

    portENTER_CRITICAL(&pktMux);
    count  = pktCounter;
    rSum   = rssiSum;
    rCount = rssiCount;
    pktCounter = 0;
    rssiSum = 0;
    rssiCount = 0;
    portEXIT_CRITICAL(&pktMux);

    lastPktRate = count * (1000UL / PACKETMON_BIN_INTERVAL_MS);
    lastAvgRssi = (rCount > 0) ? (int8_t)(rSum / (int32_t)rCount) : 0;

    // Ring buffer: historyHead always points at the oldest sample --
    // i.e. the slot we're about to overwrite with the newest one.
    uint8_t barVal = (count > 255) ? 255 : (uint8_t)count;
    history[historyHead] = barVal;
    historyHead = (uint8_t)((historyHead + 1) % PACKETMON_HISTORY_LEN);

    return true;
}

#else

void packetMonInit() {}
void packetMonDeinit() {}
bool packetMonIsActive() { return false; }
void packetMonSetChannel(uint8_t) {}
void packetMonNextChannel() {}
void packetMonPrevChannel() {}
uint8_t packetMonGetChannel() { return 0; }
void packetMonSetAutoHop(bool) {}
bool packetMonIsAutoHop() { return false; }
bool packetMonTick() { return false; }
uint32_t packetMonGetPacketRate() { return 0; }
int8_t packetMonGetAvgRSSI() { return 0; }
uint8_t packetMonHistoryLen() { return 0; }
uint8_t packetMonHistoryAt(uint8_t) { return 0; }

#endif
