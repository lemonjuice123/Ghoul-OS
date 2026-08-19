#include <string.h>
#include "StockWatch.h"
#include "Config.h"

// =====================================================================
// Ghoul OS - StockWatch.cpp
// =====================================================================

// --------------------------------------------------------------------------
// Symbols tracked by the app. Edit this list to whatever companies you
// want to watch -- ticker symbols must match Finnhub's naming exactly
// (e.g. "AAPL", not "Apple").
// --------------------------------------------------------------------------
static const char* const stockSymbols[] = {
    "AAPL", "MSFT", "GOOGL", "AMZN", "TSLA", "NVDA", "META", "NFLX"
};
#define STOCK_SYMBOL_COUNT (sizeof(stockSymbols) / sizeof(stockSymbols[0]))

struct StockQuote {
    const char* symbol;
    float price;
};

static StockQuote stockQuotes[STOCK_SYMBOL_COUNT];
static uint8_t stockQuoteCount = 0; // how many of the above actually got a valid price
static StockWatchStatus stockStatus = STOCK_STATUS_IDLE;

#if defined(ESP32)

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// Milliseconds to wait for WiFi.begin() to associate before giving up.
#define STOCK_WIFI_CONNECT_TIMEOUT_MS 15000UL
// Milliseconds to wait for each individual HTTPS request/response.
#define STOCK_HTTP_TIMEOUT_MS 8000UL

static bool ensureWifiConnected()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    stockStatus = STOCK_STATUS_CONNECTING_WIFI;

    WiFi.mode(WIFI_STA);
    WiFi.begin(STOCK_WIFI_SSID, STOCK_WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < STOCK_WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(100);
    }

    return WiFi.status() == WL_CONNECTED;
}

// Fetches one symbol's current price into *outPrice. Returns false on
// any failure (network, HTTP status, or unparsable body) -- the caller
// just skips that symbol rather than aborting the whole refresh, so a
// single bad symbol/rate-limit hiccup doesn't blank the whole list.
static bool fetchQuote(const char* symbol, float* outPrice)
{
    WiFiClientSecure client;
    // Skips TLS certificate validation. Finnhub's certificate would
    // otherwise need its root CA embedded here and kept up to date
    // across any future cert rotation -- for a hobby price display
    // that's not worth the added complexity, but it does mean this
    // connection isn't protected against a man-in-the-middle attack.
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(STOCK_HTTP_TIMEOUT_MS);

    String url = String("https://finnhub.io/api/v1/quote?symbol=") + symbol + "&token=" + FINNHUB_API_KEY;

    if (!http.begin(client, url))
    {
        return false;
    }

    int code = http.GET();
    bool ok = false;

    if (code == HTTP_CODE_OK)
    {
        String payload = http.getString();

        // Finnhub's quote response looks like:
        //   {"c":123.45,"d":1.2,"dp":0.9,"h":124,"l":121,"o":122,"pc":122.3,"t":1690000000}
        // "c" is the current price -- pull it out directly rather than
        // pulling in a full JSON library for one field.
        int idx = payload.indexOf("\"c\":");
        if (idx >= 0)
        {
            float price = payload.substring(idx + 4).toFloat();
            // Finnhub returns c=0 for an unknown/invalid symbol rather
            // than an HTTP error, so treat that as "no data" too.
            if (price > 0.0f)
            {
                *outPrice = price;
                ok = true;
            }
        }
    }

    http.end();
    return ok;
}

void stockWatchPrepareRefresh()
{
    stockStatus = STOCK_STATUS_CONNECTING_WIFI;
}

void stockWatchRefresh()
{
    stockQuoteCount = 0;

    if (!ensureWifiConnected())
    {
        stockStatus = STOCK_STATUS_WIFI_FAILED;
        return;
    }

    stockStatus = STOCK_STATUS_FETCHING;

    for (uint8_t i = 0; i < STOCK_SYMBOL_COUNT; i++)
    {
        float price = 0.0f;
        if (fetchQuote(stockSymbols[i], &price))
        {
            stockQuotes[stockQuoteCount].symbol = stockSymbols[i];
            stockQuotes[stockQuoteCount].price = price;
            stockQuoteCount++;
        }
    }

    // Small insertion sort, ascending by price (lowest first, as asked)
    // -- stockQuoteCount is tiny (<= STOCK_SYMBOL_COUNT) so O(n^2) is
    // plenty fast here.
    for (uint8_t i = 1; i < stockQuoteCount; i++)
    {
        StockQuote tmp = stockQuotes[i];
        int8_t j = (int8_t)i - 1;
        while (j >= 0 && stockQuotes[j].price > tmp.price)
        {
            stockQuotes[j + 1] = stockQuotes[j];
            j--;
        }
        stockQuotes[j + 1] = tmp;
    }

    stockStatus = (stockQuoteCount > 0) ? STOCK_STATUS_READY : STOCK_STATUS_NO_DATA;
}

void stockWatchDeinit()
{
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
}

#else

void stockWatchPrepareRefresh() { stockStatus = STOCK_STATUS_CONNECTING_WIFI; }
void stockWatchRefresh() { stockQuoteCount = 0; stockStatus = STOCK_STATUS_WIFI_FAILED; }
void stockWatchDeinit() {}

#endif

StockWatchStatus stockWatchGetStatus() { return stockStatus; }

uint8_t stockWatchCount() { return stockQuoteCount; }

const char* stockWatchSymbol(uint8_t index)
{
    return (index < stockQuoteCount) ? stockQuotes[index].symbol : "";
}

float stockWatchPrice(uint8_t index)
{
    return (index < stockQuoteCount) ? stockQuotes[index].price : 0.0f;
}