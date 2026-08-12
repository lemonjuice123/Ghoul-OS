#ifndef PACKETMON_H
#define PACKETMON_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - PacketMon.h
//
// "PackMon" -- a live 802.11 packet-rate monitor, inspired by
// spacehuhn's PacketMonitor32 (github.com/spacehuhn/PacketMonitor32):
// puts the ESP32 radio into promiscuous (sniffer) mode on a single
// WiFi channel and counts raw frames seen per time slice. It never
// associates to a network, transmits, injects, or inspects/decodes
// frame payloads -- it only times and counts what the radio already
// receives over the air, the same way a spectrum/traffic monitor
// would. UI.cpp renders the result as a scrolling bar-graph strip
// chart plus a packets/sec + average RSSI readout.
//
// Channel is changed with Up/Down; Select toggles "auto-hop" (cycles
// channels every PACKETMON_HOP_INTERVAL_MS, handy for a quick sweep
// of the whole band); Back leaves the app and turns the radio back
// off via packetMonDeinit().
// =====================================================================

// Starts the sniffer on the last-used channel (channel 1 on first
// launch). Safe to call again if already active (no-op).
void packetMonInit();

// Stops the sniffer and releases the WiFi radio. Call when leaving
// the app so other apps (or a future WiFi Scanner) can use it again.
void packetMonDeinit();

bool packetMonIsActive();

// Channel 1-13 (2.4GHz). Clamped to that range.
void packetMonSetChannel(uint8_t channel);
void packetMonNextChannel();
void packetMonPrevChannel();
uint8_t packetMonGetChannel();

// Auto-hop cycles through channels 1-13 every PACKETMON_HOP_INTERVAL_MS
// so long as it stays enabled; manually changing channel via
// packetMonSetChannel()/Next/Prev does NOT implicitly disable it --
// the caller (UI.cpp) decides that policy.
void packetMonSetAutoHop(bool enabled);
bool packetMonIsAutoHop();

// Call regularly (e.g. every uiTick()). Internally rate-limits itself
// to one history bin every PACKETMON_BIN_INTERVAL_MS. Returns true
// exactly on the tick where a new bin was captured, so the caller
// knows when it's worth redrawing.
bool packetMonTick();

uint32_t packetMonGetPacketRate(); // packets/sec, from the last completed bin
int8_t   packetMonGetAvgRSSI();    // dBm, from the last completed bin (0 if none)

// Ring-buffer history of per-bin packet counts, oldest to newest, one
// entry per screen column in the graph.
uint8_t packetMonHistoryLen();
uint8_t packetMonHistoryAt(uint8_t indexFromOldest);

#endif // PACKETMON_H
