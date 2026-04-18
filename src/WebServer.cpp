#include "WebServer.h"

#include <AsyncJson.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>

#include "ElegooCC.h"
#include "Logger.h"

#define SPIFFS LittleFS

namespace
{
constexpr const char kRouteGetSettings[]      = "/get_settings";
constexpr const char kRouteUpdateSettings[]   = "/update_settings";
constexpr const char kRouteTestPause[]        = "/test_pause";
constexpr const char kRouteTestResume[]       = "/test_resume";
constexpr const char kRouteDiscoverPrinter[]  = "/discover_printer";
constexpr const char kRouteSensorStatus[]     = "/sensor_status";
constexpr const char kRouteLogsText[]         = "/api/logs_text";
constexpr const char kRouteLogsLive[]         = "/api/logs_live";
constexpr const char kRouteLogsClear[]        = "/api/logs/clear";
constexpr const char kRouteCoredump[]         = "/api/coredump";
constexpr const char kRouteCoredumpStatus[]   = "/api/coredump/status";
constexpr const char kRouteCoredumpClear[]    = "/api/coredump/clear";
constexpr const char kRoutePanic[]            = "/api/panic";
constexpr const char kRouteVersion[]          = "/version";
constexpr const char kRouteStatusEvents[]     = "/status_events";
constexpr const char kRouteLiteRoot[]         = "/lite";
constexpr const char kRouteFavicon[]          = "/favicon.ico";
constexpr const char kRouteRoot[]             = "/";
constexpr const char kLiteIndexPath[]         = "/lite/index.htm";
constexpr const char kRouteReset[]            = "/api/reset";
}  // namespace

// External reference to firmware version from main.cpp
extern const char *firmwareVersion;
extern const char *chipFamily;

/**
 * @brief Produce a compact build timestamp thumbprint in MMDDYYHHMMSS format.
 *
 * Converts a build date and time string into a 12-digit thumbprint representing
 * month, day, two-digit year, hour, minute, and second.
 *
 * @param date Build date string in the format "Mon DD YYYY" (for example, "Nov 25 2025").
 * @param time Build time string in the format "HH:MM:SS" (for example, "08:10:22").
 * @return String 12-character thumbprint "MMDDYYHHMMSS" (for example, "112525081022").
 */
String getBuildThumbprint(const char* date, const char* time) {
    // Parse __DATE__ format: "Nov 25 2025"
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char month_str[4] = {0};
    int day, year;
    sscanf(date, "%3s %d %d", month_str, &day, &year);  // %3s limits to 3 chars + null

    int month = 1;
    for (int i = 0; i < 12; i++) {
        if (strcmp(month_str, months[i]) == 0) {
            month = i + 1;
            break;
        }
    }

    // Parse __TIME__ format: "08:10:22"
    int hour, minute, second;
    sscanf(time, "%d:%d:%d", &hour, &minute, &second);

    // Format as MMDDYYHHMMSS
    char thumbprint[13];
    snprintf(thumbprint, sizeof(thumbprint), "%02d%02d%02d%02d%02d%02d",
             month, day, year % 100, hour, minute, second);
    return String(thumbprint);
}

// Read filesystem build thumbprint from file
String getFilesystemThumbprint() {
    File file = SPIFFS.open("/build_timestamp.txt", "r");
    if (!file) {
        return "unknown";
    }
    String thumbprint = file.readStringUntil('\n');
    file.close();
    thumbprint.trim();
    return thumbprint.length() > 0 ? thumbprint : "unknown";
}

// Read build version from file
String getBuildVersion() {
    File file = SPIFFS.open("/build_version.txt", "r");
    if (!file) {
        return "0.0.0";
    }
    String version = file.readStringUntil('\n');
    file.close();
    version.trim();
    return version.length() > 0 ? version : "0.0.0";
}

// CRC32 for SSE payload deduplication (replaces full String comparison)
uint32_t WebServer::crc32(const char *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= (uint8_t)data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

WebServer::WebServer(int port) : server(port), statusEvents(kRouteStatusEvents) {}

void WebServer::begin()
{
    server.begin();

    // --- GET /get_settings ---
    // Serves pre-built cached settings JSON (built in loop() on main task)
    // Thread-safe: double-buffered copy, short lock, no heap allocation
    server.on(kRouteGetSettings, HTTP_GET,
              [this](AsyncWebServerRequest *request)
              {
                  char jsonBuf[kCacheBufSize];
                  size_t len = cachedSettings.read(jsonBuf, sizeof(jsonBuf));

                  if (len == 0)
                  {
                      request->send(503, "application/json", "{\"error\":\"initializing\"}");
                  }
                  else
                  {
                      request->send(200, "application/json", jsonBuf);
                  }
              });

    // --- POST /update_settings ---
    // Thread-safe: copies JSON into pendingSettingsDoc and sets flag;
    // actual settings mutation happens in loop() on the main task
    server.addHandler(new AsyncCallbackJsonWebHandler(
        kRouteUpdateSettings,
        [this](AsyncWebServerRequest *request, JsonVariant &json)
        {
            bool alreadyPending = false;
            portENTER_CRITICAL(&pendingMutex);
            if (pendingSettingsUpdate)
            {
                alreadyPending = true;
            }
            else
            {
                pendingSettingsDoc.clear();
                JsonObject src = json.as<JsonObject>();
                JsonObject dst = pendingSettingsDoc.to<JsonObject>();
                for (JsonPair kv : src)
                {
                    dst[kv.key()] = kv.value();
                }
                pendingSettingsUpdate = true;
            }
            portEXIT_CRITICAL(&pendingMutex);

            if (alreadyPending)
            {
                request->send(429, "application/json",
                              "{\"error\":\"Settings update already pending\"}");
                return;
            }

            request->send(200, "application/json", "{\"status\":\"ok\"}");
        }));

    // --- POST /test_pause ---
    // Thread-safe: sets flag, loop() calls pausePrint()
    server.on(kRouteTestPause, HTTP_POST,
              [this](AsyncWebServerRequest *request)
              {
                  pendingPause = true;
                  request->send(200, "text/plain", "ok");
              });

    // --- POST /test_resume ---
    // Thread-safe: sets flag, loop() calls continuePrint()
    server.on(kRouteTestResume, HTTP_POST,
              [this](AsyncWebServerRequest *request)
              {
                  pendingResume = true;
                  request->send(200, "text/plain", "ok");
              });

    // POST /discover_printer - Start discovery scan
    // Thread-safe: sets flag, loop() calls startDiscoveryAsync()
    server.on(kRouteDiscoverPrinter, HTTP_POST,
              [this](AsyncWebServerRequest *request)
              {
                  if (elegooCC.isDiscoveryActive())
                  {
                      request->send(200, "application/json", "{\"active\":true}");
                      return;
                  }
                  pendingDiscovery = true;
                  request->send(200, "application/json", "{\"started\":true}");
              });

    // GET /discover_printer - Poll discovery status and results
    // Thread-safe: double-buffered copy, short lock, no heap allocation
    server.on(kRouteDiscoverPrinter, HTTP_GET,
              [this](AsyncWebServerRequest *request)
              {
                  char jsonBuf[kCacheBufSize];
                  size_t len = cachedDiscovery.read(jsonBuf, sizeof(jsonBuf));

                  if (len == 0)
                  {
                      request->send(200, "application/json", "{\"active\":false,\"printers\":[]}");
                  }
                  else
                  {
                      request->send(200, "application/json", jsonBuf);
                  }
              });

    // Setup ElegantOTA
    ElegantOTA.begin(&server);

    // Reset device endpoint
    server.on(kRouteReset, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  logger.log("Device reset requested via web UI");
                  request->send(200, "text/plain", "Restarting...");
                  // Delay slightly to allow response to be sent
                  delay(1000);
                  ESP.restart();
              });

    // SSE client connect handler.
    // NOTE: Do NOT call statusEvents.count() here. AsyncEventSource::_addClient() holds
    // _client_queue_lock when it calls this callback, so calling count() (which also
    // acquires that mutex) causes a recursive-lock deadlock → Task WDT crash.
    // Excess client cleanup is handled by cleanupSSEClients() in the main loop.
    statusEvents.onConnect([](AsyncEventSourceClient *client) {
        client->send("connected", "init", millis(), 1000);
    });
    server.addHandler(&statusEvents);

    // --- GET /sensor_status ---
    // Thread-safe: double-buffered copy, short lock, no heap allocation
    server.on(kRouteSensorStatus, HTTP_GET,
              [this](AsyncWebServerRequest *request)
              {
                  char jsonBuf[kCacheBufSize];
                  size_t len = cachedSensorStatus.read(jsonBuf, sizeof(jsonBuf));

                  if (len == 0)
                  {
                      request->send(503, "application/json", "{\"error\":\"initializing\"}");
                  }
                  else
                  {
                      request->send(200, "application/json", jsonBuf);
                  }
              });

    // Logs endpoint (DISABLED - JSON serialization of 1024 entries exceeds 32KB buffer)
    // Use /api/logs_live or /api/logs_text instead

    // Raw text logs endpoint (full logs for download)
    server.on(kRouteLogsText, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  AsyncResponseStream *streamResponse = request->beginResponseStream("text/plain");
                  streamResponse->addHeader("Content-Disposition", "attachment; filename=\"logs.txt\"");

                  logger.streamLogs(streamResponse);

                  request->send(streamResponse);
              });

    // Coredump download endpoint (raw partition bytes)
    server.on(kRouteCoredump, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  if (esp_core_dump_image_check() != ESP_OK)
                  {
                      request->send(404, "text/plain", "No coredump available");
                      return;
                  }

                  const esp_partition_t *partition =
                      esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                               nullptr);
                  if (!partition || partition->size == 0)
                  {
                      request->send(404, "text/plain", "Coredump partition not found");
                      return;
                  }

                  const size_t partitionSize = partition->size;
                  AsyncWebServerResponse *response =
                      request->beginChunkedResponse(
                          "application/octet-stream",
                          [partition, partitionSize](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
                              if (index >= partitionSize || maxLen == 0)
                              {
                                  return 0;
                              }

                              size_t toRead = partitionSize - index;
                              if (toRead > maxLen)
                              {
                                  toRead = maxLen;
                              }

                              if (esp_partition_read(partition, index, buffer, toRead) != ESP_OK)
                              {
                                  return 0;
                              }

                              return toRead;
                          });

                  response->addHeader(
                      "Content-Disposition",
                      "attachment; filename=\"coredump.bin\"");
                  request->send(response);
              });

    // Coredump status endpoint
    server.on(kRouteCoredumpStatus, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  const esp_partition_t *partition =
                      esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                               nullptr);
                  bool available = (partition != nullptr && partition->size > 0 &&
                                    esp_core_dump_image_check() == ESP_OK);
                  request->send(200, "application/json",
                                available ? "{\"available\":true}" : "{\"available\":false}");
              });

    // Coredump clear endpoint (erase coredump partition)
    server.on(kRouteCoredumpClear, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  const esp_partition_t *partition =
                      esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                               ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                               nullptr);
                  if (!partition || partition->size == 0)
                  {
                      request->send(404, "text/plain", "Coredump partition not found");
                      return;
                  }

                  esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
                  if (err != ESP_OK)
                  {
                      request->send(500, "text/plain", "Failed to clear coredump");
                      return;
                  }

                  logger.log("Coredump cleared via web UI");
                  request->send(200, "text/plain", "ok");
              });

    // Live logs endpoint (last 100 entries for UI display)
    server.on(kRouteLogsLive, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String textResponse = logger.getLogsAsText(100);  // Only last 100 entries
                  request->send(200, "text/plain", textResponse);
              });

    // Clear logs endpoint
    server.on(kRouteLogsClear, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  logger.clearLogs();
                  logger.log("Logs cleared via web UI");
                  request->send(200, "text/plain", "ok");
              });

    // Trigger a controlled panic for testing coredumps
#ifdef ENABLE_CRASH_TESTING
    server.on(kRoutePanic, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  logger.log("Panic requested via web UI");
                  request->send(200, "text/plain", "Triggering panic...");
                  delay(250);
                  esp_system_abort("User-requested panic");
              });
#endif

    // Build version JSON once at startup (avoids LittleFS reads from async handler)
    {
        #ifdef BUILD_DATE
            const char* buildDate = BUILD_DATE;
            const char* buildTime = BUILD_TIME;
        #else
            const char* buildDate = __DATE__;
            const char* buildTime = __TIME__;
        #endif

        StaticJsonDocument<512> jsonDoc;
        jsonDoc["firmware_version"] = firmwareVersion;
        jsonDoc["chip_family"]      = chipFamily;
        jsonDoc["build_date"]       = buildDate;
        jsonDoc["build_time"]       = buildTime;
        jsonDoc["firmware_thumbprint"] = getBuildThumbprint(buildDate, buildTime);
        jsonDoc["filesystem_thumbprint"] = getFilesystemThumbprint();
        jsonDoc["build_version"] = getBuildVersion();

        serializeJson(jsonDoc, cachedVersionJson, sizeof(cachedVersionJson));
    }

    // Version endpoint - serves pre-built JSON (no LittleFS access, thread-safe)
    server.on(kRouteVersion, HTTP_GET,
              [this](AsyncWebServerRequest *request)
              {
                  request->send(200, "application/json", cachedVersionJson);
              });

    // Serve lightweight UI from /lite (if available)
    // Keep explicit /lite path for backwards compatibility
    server.serveStatic(kRouteLiteRoot, SPIFFS, "/lite/").setDefaultFile("index.htm");

    // Serve favicon explicitly because the root static handler only matches "/".
    server.serveStatic(kRouteFavicon, SPIFFS, "/lite/favicon.ico");

    // Always serve the lightweight UI at the root as well.
    server.serveStatic(kRouteRoot, SPIFFS, "/lite/").setDefaultFile("index.htm");

    // SPA-style routing: for any unknown GET that isn't an API or asset,
    // serve index.htm so that the frontend router can handle the path.
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_GET &&
            !request->url().startsWith("/api/") &&
            !request->url().startsWith("/assets/"))
        {
            request->send(SPIFFS, kLiteIndexPath, "text/html");
        }
        else
        {
            request->send(404, "text/plain", "Not found");
        }
    });
}

void WebServer::processPendingCommands()
{
    // Process pending pause command
    if (pendingPause)
    {
        pendingPause = false;
        elegooCC.pausePrint();
    }

    // Process pending resume command
    if (pendingResume)
    {
        pendingResume = false;
        elegooCC.continuePrint();
    }

    // Process pending discovery
    if (pendingDiscovery)
    {
        pendingDiscovery = false;
        if (!elegooCC.isDiscoveryActive())
        {
            elegooCC.startDiscoveryAsync(5000, nullptr);
        }
    }

    // Process pending reconnect (triggered by IP change in settings update)
    if (pendingReconnect)
    {
        pendingReconnect = false;
        elegooCC.reconnect();
    }

    // Process pending settings update
    if (pendingSettingsUpdate)
    {
        portENTER_CRITICAL(&pendingMutex);
        // Copy the doc locally so we can release the mutex quickly
        StaticJsonDocument<1024> localDoc;
        localDoc.set(pendingSettingsDoc);
        pendingSettingsUpdate = false;
        portEXIT_CRITICAL(&pendingMutex);

        JsonObject jsonObj = localDoc.as<JsonObject>();

        // Track if IP address changed to trigger reconnect
        String oldIp = settingsManager.getElegooIP();
        bool ipChanged = false;

        // Only update fields that are present in the request
        if (jsonObj.containsKey("elegooip"))
        {
            String newIp = jsonObj["elegooip"].as<String>();
            ipChanged = (oldIp != newIp) && newIp.length() > 0;
            settingsManager.setElegooIP(newIp);
        }
        if (jsonObj.containsKey("ssid"))
            settingsManager.setSSID(jsonObj["ssid"].as<String>());
        if (jsonObj.containsKey("passwd") && jsonObj["passwd"].as<String>().length() > 0)
            settingsManager.setPassword(jsonObj["passwd"].as<String>());
        if (jsonObj.containsKey("ap_mode"))
            settingsManager.setAPMode(jsonObj["ap_mode"].as<bool>());
        if (jsonObj.containsKey("pause_on_runout"))
            settingsManager.setPauseOnRunout(jsonObj["pause_on_runout"].as<bool>());
        if (jsonObj.containsKey("enabled"))
            settingsManager.setEnabled(jsonObj["enabled"].as<bool>());
        if (jsonObj.containsKey("detection_length_mm"))
            settingsManager.setDetectionHardJamMm(jsonObj["detection_length_mm"].as<float>());
        if (jsonObj.containsKey("detection_grace_period_ms"))
            settingsManager.setDetectionGracePeriodMs(jsonObj["detection_grace_period_ms"].as<int>());
        if (jsonObj.containsKey("detection_ratio_threshold"))
        {
            float threshold = jsonObj["detection_ratio_threshold"].as<float>();
            // Accept legacy 0.0-1.0 ratio payloads as well as 0-100 percent.
            if (threshold > 0.0f && threshold <= 1.0f)
            {
                threshold *= 100.0f;
            }
            settingsManager.setDetectionRatioThreshold(static_cast<int>(threshold + 0.5f));
        }
        if (jsonObj.containsKey("detection_hard_jam_mm"))
            settingsManager.setDetectionHardJamMm(jsonObj["detection_hard_jam_mm"].as<float>());
        if (jsonObj.containsKey("detection_soft_jam_time_ms"))
            settingsManager.setDetectionSoftJamTimeMs(jsonObj["detection_soft_jam_time_ms"].as<int>());
        if (jsonObj.containsKey("detection_hard_jam_time_ms"))
            settingsManager.setDetectionHardJamTimeMs(jsonObj["detection_hard_jam_time_ms"].as<int>());
        if (jsonObj.containsKey("detection_mode"))
            settingsManager.setDetectionMode(jsonObj["detection_mode"].as<int>());
        if (jsonObj.containsKey("sdcp_loss_behavior"))
            settingsManager.setSdcpLossBehavior(jsonObj["sdcp_loss_behavior"].as<int>());
        if (jsonObj.containsKey("flow_telemetry_stale_ms"))
            settingsManager.setFlowTelemetryStaleMs(jsonObj["flow_telemetry_stale_ms"].as<int>());
        if (jsonObj.containsKey("ui_refresh_interval_ms"))
            settingsManager.setUiRefreshIntervalMs(jsonObj["ui_refresh_interval_ms"].as<int>());
        if (jsonObj.containsKey("suppress_pause_commands"))
            settingsManager.setSuppressPauseCommands(jsonObj["suppress_pause_commands"].as<bool>());
        if (jsonObj.containsKey("log_level"))
            settingsManager.setLogLevel(jsonObj["log_level"].as<int>());
        if (jsonObj.containsKey("movement_mm_per_pulse"))
            settingsManager.setMovementMmPerPulse(jsonObj["movement_mm_per_pulse"].as<float>());
        if (jsonObj.containsKey("auto_calibrate_sensor"))
            settingsManager.setAutoCalibrateSensor(jsonObj["auto_calibrate_sensor"].as<bool>());
        if (jsonObj.containsKey("pulse_reduction_percent"))
            settingsManager.setPulseReductionPercent(jsonObj["pulse_reduction_percent"].as<float>());
        if (jsonObj.containsKey("test_recording_mode"))
            settingsManager.setTestRecordingMode(jsonObj["test_recording_mode"].as<bool>());
        if (jsonObj.containsKey("show_debug_page"))
            settingsManager.setShowDebugPage(jsonObj["show_debug_page"].as<bool>());
        if (jsonObj.containsKey("timezone_offset_minutes"))
            settingsManager.setTimezoneOffsetMinutes(jsonObj["timezone_offset_minutes"].as<int>());

        bool saved = settingsManager.save();
        if (saved)
        {
            elegooCC.refreshCaches();
            settingsJsonDirty = true;  // Rebuild cached settings JSON
            if (ipChanged)
            {
                pendingReconnect = true;
            }
        }
        else
        {
            logger.log("Failed to save settings from pending update");
        }
    }
}

void WebServer::refreshCachedResponses()
{
    // Rebuild sensor status JSON (called every loop iteration from main task)
    {
        printer_info_t elegooStatus = elegooCC.getCurrentInformation();
        StaticJsonDocument<768> jsonDoc;
        buildStatusJson(jsonDoc, elegooStatus);

        char jsonBuf[kCacheBufSize];
        size_t len = serializeJson(jsonDoc, jsonBuf, sizeof(jsonBuf));

        cachedSensorStatus.publish(jsonBuf, len);
        cachedPrintStatus = elegooStatus.printStatus;
    }

    // Rebuild discovery JSON (only while discovery is active or results exist)
    {
        StaticJsonDocument<1024> jsonDoc;
        jsonDoc["active"] = elegooCC.isDiscoveryActive();

        JsonArray printers = jsonDoc.createNestedArray("printers");
        for (const auto &res : elegooCC.getDiscoveryResults())
        {
            JsonObject p = printers.createNestedObject();
            p["ip"]      = res.ip;
            p["payload"] = res.payload;
        }

        char jsonBuf[kCacheBufSize];
        size_t len = serializeJson(jsonDoc, jsonBuf, sizeof(jsonBuf));

        cachedDiscovery.publish(jsonBuf, len);
    }

    // Rebuild settings JSON only when dirty
    if (settingsJsonDirty)
    {
        settingsJsonDirty = false;
        String newSettingsJson = settingsManager.toJson(false);

        cachedSettings.publish(newSettingsJson.c_str(), newSettingsJson.length());
    }
}

void WebServer::cleanupSSEClients()
{
    unsigned long now = millis();
    if (now - lastSSECleanupMs < 10000)
    {
        return;
    }
    lastSSECleanupMs = now;

    int clientCount = statusEvents.count();
    if (clientCount > kMaxSSEClients)
    {
        logger.logf("SSE cleanup: %d clients (max %d), closing stale connections",
                     clientCount, kMaxSSEClients);
        statusEvents.close();
    }
}

void WebServer::loop()
{
    ElegantOTA.loop();
    unsigned long now = millis();

    // Process commands queued by async web handlers (thread-safe)
    processPendingCommands();

    // Rebuild cached JSON responses on the main task (thread-safe)
    refreshCachedResponses();
    if (kStressCacheRefreshes > 0)
    {
        for (int i = 0; i < kStressCacheRefreshes; i++)
        {
            refreshCachedResponses();
        }
    }

    // Periodic SSE client cleanup
    cleanupSSEClients();

    // Periodic web server diagnostics (every 30s)
    static unsigned long lastDiagMs = 0;
    if (now - lastDiagMs >= 30000)
    {
        lastDiagMs = now;
        logger.logf("WebServer diag: SSE clients=%d, heap=%u, minHeap=%u",
                     statusEvents.count(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }

    if (statusEvents.count() > 0 && now - lastStatusBroadcastMs >= statusBroadcastIntervalMs)
    {
        lastStatusBroadcastMs = now;
        unsigned long t0 = millis();
        broadcastStatusUpdate();
        unsigned long elapsed = millis() - t0;
        if (elapsed > 100)
        {
            logger.logf("WARNING: SSE broadcast took %lu ms", elapsed);
        }
    }
}

void WebServer::buildStatusJson(StaticJsonDocument<768> &jsonDoc, const printer_info_t &elegooStatus)
{
    jsonDoc["stopped"]        = elegooStatus.filamentStopped;
    jsonDoc["filamentRunout"] = elegooStatus.filamentRunout;

    jsonDoc["mac"] = WiFi.macAddress();
    jsonDoc["ip"]  = WiFi.localIP().toString();
    jsonDoc["uptimeSec"] = millis() / 1000;

    JsonObject elegoo = jsonDoc["elegoo"].to<JsonObject>();
    elegoo["mainboardID"]          = elegooStatus.mainboardID;
    elegoo["printStatus"]          = (int) elegooStatus.printStatus;
    elegoo["isPrinting"]           = elegooStatus.isPrinting;
    elegoo["currentLayer"]         = elegooStatus.currentLayer;
    elegoo["totalLayer"]           = elegooStatus.totalLayer;
    elegoo["progress"]             = elegooStatus.progress;
    elegoo["currentTicks"]         = elegooStatus.currentTicks;
    elegoo["totalTicks"]           = elegooStatus.totalTicks;
    elegoo["PrintSpeedPct"]        = elegooStatus.PrintSpeedPct;
    elegoo["isWebsocketConnected"] = elegooStatus.isWebsocketConnected;
    elegoo["currentZ"]             = elegooStatus.currentZ;
    elegoo["expectedFilament"]     = elegooStatus.expectedFilamentMM;
    elegoo["actualFilament"]       = elegooStatus.actualFilamentMM;
    elegoo["expectedDelta"]        = elegooStatus.lastExpectedDeltaMM;
    elegoo["telemetryAvailable"]   = elegooStatus.telemetryAvailable;
    elegoo["currentDeficitMm"]     = elegooStatus.currentDeficitMm;
    elegoo["deficitThresholdMm"]   = elegooStatus.deficitThresholdMm;
    elegoo["deficitRatio"]         = elegooStatus.deficitRatio;
    elegoo["passRatio"]            = elegooStatus.passRatio;
    elegoo["ratioThreshold"]       = settingsManager.getDetectionRatioThreshold();
    elegoo["hardJamPercent"]       = elegooStatus.hardJamPercent;
    elegoo["softJamPercent"]       = elegooStatus.softJamPercent;
    elegoo["movementPulses"]       = (uint32_t) elegooStatus.movementPulseCount;
    elegoo["uiRefreshIntervalMs"]  = settingsManager.getUiRefreshIntervalMs();
    elegoo["flowTelemetryStaleMs"] = settingsManager.getFlowTelemetryStaleMs();
    elegoo["graceActive"]          = elegooStatus.graceActive;
    elegoo["graceState"]           = elegooStatus.graceState;
    elegoo["expectedRateMmPerSec"] = elegooStatus.expectedRateMmPerSec;
    elegoo["actualRateMmPerSec"]   = elegooStatus.actualRateMmPerSec;
    elegoo["runoutPausePending"]   = elegooStatus.runoutPausePending;
    elegoo["runoutPauseRemainingMm"] = elegooStatus.runoutPauseRemainingMm;
    elegoo["runoutPauseDelayMm"]   = elegooStatus.runoutPauseDelayMm;
    elegoo["runoutPauseCommanded"] = elegooStatus.runoutPauseCommanded;
}

void WebServer::broadcastStatusUpdate()
{
    // Use the pre-built cached sensor status JSON (double-buffered, short-lock copy)
    char payloadBuf[kCacheBufSize];
    size_t payloadLen = cachedSensorStatus.read(payloadBuf, sizeof(payloadBuf));
    sdcp_print_status_t printStatus = cachedPrintStatus;

    if (payloadLen == 0)
    {
        return;
    }

    bool idleState = (printStatus == SDCP_PRINT_STATUS_IDLE ||
                      printStatus == SDCP_PRINT_STATUS_COMPLETE);

    if (idleState)
    {
        uint32_t payloadCrc = crc32(payloadBuf, payloadLen);
        if (hasLastIdlePayload && payloadCrc == lastIdlePayloadCrc)
        {
            statusBroadcastIntervalMs = kStatusBroadcastIntervalMsDefault;
            return;
        }
        lastIdlePayloadCrc = payloadCrc;
        hasLastIdlePayload = true;
    }
    else
    {
        hasLastIdlePayload = false;
    }

    statusEvents.send(payloadBuf, "status");

    bool isPrinting = (printStatus != SDCP_PRINT_STATUS_IDLE &&
                       printStatus != SDCP_PRINT_STATUS_COMPLETE);
    statusBroadcastIntervalMs = isPrinting ? kStatusBroadcastIntervalMsPrinting
                                           : kStatusBroadcastIntervalMsDefault;
}
