#ifndef STOCKWATCH_H
#define STOCKWATCH_H

#include <Arduino.h>

// =====================================================================
// Ghoul OS - StockWatch.h
//
// The "StockWatch" app connects to a real WiFi network (unlike the
// WiFi Scanner app, which only ever passively scans and never
// associates) and pulls live quotes from the Finnhub REST API
// (https://finnhub.io) over HTTPS, one request per symbol in
// stockSymbols[] (see StockWatch.cpp -- edit that list to track
// whatever companies you want).
//
// Your WiFi credentials and Finnhub API key live in Config.h as
// STOCK_WIFI_SSID / STOCK_WIFI_PASSWORD / FINNHUB_API_KEY -- fill
// those in with real values before using this app. A free Finnhub API
// key can be created at https://finnhub.io/register.
//
// Fetching is synchronous/blocking: connecting to WiFi and making one
// HTTPS request per symbol takes a few seconds in total, so
// stockWatchRefresh() is only called once when the app is opened and
// again on a manual refresh (Select key) -- never from uiTick().
// =====================================================================

enum StockWatchStatus {
    STOCK_STATUS_IDLE,             // not yet refreshed this session
    STOCK_STATUS_CONNECTING_WIFI,  // WiFi.begin() in progress
    STOCK_STATUS_WIFI_FAILED,      // couldn't associate within the timeout
    STOCK_STATUS_FETCHING,         // WiFi is up, HTTPS requests in flight
    STOCK_STATUS_READY,            // at least one quote came back successfully
    STOCK_STATUS_NO_DATA           // WiFi connected but every request failed
};

// Instantly marks the app as "about to refresh" (STOCK_STATUS_CONNECTING_WIFI)
// without doing any blocking work -- call this right before drawing the
// screen for the first time, so the person sees "Connecting..." rather
// than a stale or blank status while the real (blocking) refresh below
// is about to run.
void stockWatchPrepareRefresh();

// Connects to WiFi (if not already connected) and fetches a fresh quote
// for every symbol in the list, then sorts the successful results
// ascending by price (lowest first). Blocks for roughly a few seconds
// total. Safe to call repeatedly (e.g. for a manual refresh).
void stockWatchRefresh();

// Disconnects and releases the WiFi radio. Call when leaving the app.
void stockWatchDeinit();

StockWatchStatus stockWatchGetStatus();

uint8_t     stockWatchCount();                 // number of symbols with a valid price right now
const char* stockWatchSymbol(uint8_t index);   // sorted ascending by price
float       stockWatchPrice(uint8_t index);

#endif // STOCKWATCH_H