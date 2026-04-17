#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncEventSource.h>
#include <ElegantOTA.h>
#include <LittleFS.h>

#include "SettingsManager.h"
#include "ElegooCC.h"

// Define SPIFFS as LittleFS
#define SPIFFS LittleFS

// Maximum SSE clients allowed simultaneously
// Kept low to reduce memory pressure on page refresh (old connection lingers briefly)
static constexpr int kMaxSSEClients = 2;

#ifdef STRESS_MODE
static constexpr unsigned long kStatusBroadcastIntervalMsDefault = 200;
static constexpr unsigned long kStatusBroadcastIntervalMsPrinting = 50;
static constexpr int kStressCacheRefreshes = 3;
#else
static constexpr unsigned long kStatusBroadcastIntervalMsDefault = 5000;
static constexpr unsigned long kStatusBroadcastIntervalMsPrinting = 1000;
static constexpr int kStressCacheRefreshes = 0;
#endif

class WebServer
{
   private:
    AsyncWebServer server;
    AsyncEventSource statusEvents;
    unsigned long lastStatusBroadcastMs = 0;
    unsigned long statusBroadcastIntervalMs = kStatusBroadcastIntervalMsDefault;

    // --- Thread-safe command queue (async handlers set flags, loop() processes) ---

    // Pending settings update: async handler parses JSON into this doc, loop() applies it
    volatile bool pendingSettingsUpdate = false;
    StaticJsonDocument<1024> pendingSettingsDoc;
    portMUX_TYPE pendingMutex = portMUX_INITIALIZER_UNLOCKED;

    // Pending action commands from web handlers
    volatile bool pendingPause = false;
    volatile bool pendingResume = false;
    volatile bool pendingDiscovery = false;
    volatile bool pendingReconnect = false;  // Set when IP changed during settings update

    // --- Pre-built cached responses (double-buffered, short-lock copy) ---
    // Main loop writes to buf[!activeIdx], then flips activeIdx.
    // Async handlers snapshot activeIdx/len under a short lock (no heap allocation).
    static constexpr size_t kCacheBufSize = 1536;  // Fits sensor (~600B), settings (~1KB), discovery (~1KB)

    struct CachedResponse {
        char   buf[2][kCacheBufSize];
        size_t len[2] = {0, 0};
        volatile int activeIdx = 0;  // Single-word write is atomic on ESP32

        // Main loop: write to inactive buffer, then flip
        void publish(const char *json, size_t jsonLen) {
            int writeIdx = !activeIdx;
            size_t copyLen = (jsonLen < kCacheBufSize - 1) ? jsonLen : (kCacheBufSize - 1);
            memcpy(buf[writeIdx], json, copyLen);
            buf[writeIdx][copyLen] = '\0';
            len[writeIdx] = copyLen;
            // Memory barrier + atomic flip
            portENTER_CRITICAL(&_mutex);
            activeIdx = writeIdx;
            portEXIT_CRITICAL(&_mutex);
        }

        // Async handler: copy active buffer under short lock (no heap alloc)
        size_t read(char *out, size_t outSize) const {
            if (outSize == 0) {
                return 0;
            }
            size_t copyLen = 0;
            portENTER_CRITICAL(&_mutex);
            int idx = activeIdx;
            copyLen = len[idx];
            if (copyLen >= outSize) {
                copyLen = outSize - 1;
            }
            memcpy(out, buf[idx], copyLen);
            out[copyLen] = '\0';
            portEXIT_CRITICAL(&_mutex);
            return copyLen;
        }

        mutable portMUX_TYPE _mutex = portMUX_INITIALIZER_UNLOCKED;
    };

    CachedResponse cachedSensorStatus;
    CachedResponse cachedSettings;
    CachedResponse cachedDiscovery;
    sdcp_print_status_t cachedPrintStatus = SDCP_PRINT_STATUS_IDLE;
    volatile bool settingsJsonDirty = true;  // Start dirty to build initial cache

    // Cached version JSON (built once at startup, never changes)
    char cachedVersionJson[512] = {0};

    // --- SSE deduplication ---
    // CRC32 of last idle payload for dedup (replaces full String comparison)
    uint32_t lastIdlePayloadCrc = 0;
    bool hasLastIdlePayload = false;

    // SSE client cleanup tracking
    unsigned long lastSSECleanupMs = 0;

    void buildStatusJson(StaticJsonDocument<768> &jsonDoc, const printer_info_t &elegooStatus);
    void broadcastStatusUpdate();
    void processPendingCommands();
    void refreshCachedResponses();
    void cleanupSSEClients();

    static uint32_t crc32(const char *data, size_t length);

   public:
    WebServer(int port = 80);
    void begin();
    void loop();
};

#endif  // WEB_SERVER_H
