#include "WebServer.h"

#include <AsyncJson.h>

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

WebServer::WebServer(int port) : server(port), statusEvents(kRouteStatusEvents) {}

void WebServer::begin()
{
    server.begin();

    // Get settings endpoint
    server.on(kRouteGetSettings, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String jsonResponse = settingsManager.toJson(false);
                  request->send(200, "application/json", jsonResponse);
              });

    server.addHandler(new AsyncCallbackJsonWebHandler(
        kRouteUpdateSettings,
        [this](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject jsonObj = json.as<JsonObject>();

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
                settingsManager.setDetectionRatioThreshold(jsonObj["detection_ratio_threshold"].as<int>());
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
                if (ipChanged)
                {
                    elegooCC.reconnect();
                }
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            }
            else
            {
                request->send(500, "application/json",
                              "{\"error\":\"Failed to save settings to flash\"}");
            }
        }));

    server.on(kRouteTestPause, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  elegooCC.pausePrint();
                  request->send(200, "text/plain", "ok");
              });

    server.on(kRouteTestResume, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  elegooCC.continuePrint();
                  request->send(200, "text/plain", "ok");
              });

    // POST /discover_printer - Start discovery scan
    server.on(kRouteDiscoverPrinter, HTTP_POST,
              [](AsyncWebServerRequest *request)
              {
                  if (elegooCC.isDiscoveryActive())
                  {
                      request->send(200, "application/json", "{\"active\":true}");
                      return;
                  }
                  elegooCC.startDiscoveryAsync(5000, nullptr);  // 5 seconds with socket recycling
                  request->send(200, "application/json", "{\"started\":true}");
              });

    // GET /discover_printer - Poll discovery status and results
    server.on(kRouteDiscoverPrinter, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  DynamicJsonDocument jsonDoc(1024);
                  jsonDoc["active"] = elegooCC.isDiscoveryActive();

                  JsonArray printers = jsonDoc.createNestedArray("printers");
                  for (const auto &res : elegooCC.getDiscoveryResults())
                  {
                      JsonObject p = printers.createNestedObject();
                      p["ip"]      = res.ip;
                      p["payload"] = res.payload;
                  }

                  String response;
                  serializeJson(jsonDoc, response);
                  request->send(200, "application/json", response);
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

    statusEvents.onConnect([](AsyncEventSourceClient *client) {
        client->send("connected", "init", millis(), 1000);
    });
    server.addHandler(&statusEvents);

    // Sensor status endpoint
    server.on(kRouteSensorStatus, HTTP_GET,
              [this](AsyncWebServerRequest *request)
              {
                  printer_info_t elegooStatus = elegooCC.getCurrentInformation();

                  // JSON allocation: 576 bytes heap (was 768 bytes)
                  // Measured actual: ~480 bytes (83% utilization, 17% margin)
                  // Last measured: 2025-11-26
                  // See: .claude/hardcoded-allocations.md for maintenance notes
                  DynamicJsonDocument jsonDoc(576);
                  buildStatusJson(jsonDoc, elegooStatus);

                  String jsonResponse;
                  jsonResponse.reserve(576);  // Pre-allocate to prevent fragmentation
                  serializeJson(jsonDoc, jsonResponse);

                  // Pin Values level: Check if approaching allocation limit
                  if (settingsManager.getLogLevel() >= LOG_PIN_VALUES)
                  {
                      size_t actualSize = measureJson(jsonDoc);
                      static bool logged = false;
                      if (!logged && actualSize > 490)  // >85% of 576 bytes
                      {
                          logger.logf(LOG_PIN_VALUES, "WebServer sensor_status JSON size: %zu / 576 bytes (%.1f%%)",
                                     actualSize, (actualSize * 100.0f / 576.0f));
                          logged = true;  // Only log once per session
                      }
                  }

                  request->send(200, "application/json", jsonResponse);
              });

    // Logs endpoint (DISABLED - JSON serialization of 1024 entries exceeds 32KB buffer)
    // Use /api/logs_live or /api/logs_text instead
    // server.on("/api/logs", HTTP_GET,
    //           [](AsyncWebServerRequest *request)
    //           {
    //               String jsonResponse = logger.getLogsAsJson();
    //               request->send(200, "application/json", jsonResponse);
    //           });

    // Raw text logs endpoint (full logs for download)
    server.on(kRouteLogsText, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String textResponse = logger.getLogsAsText();
                  AsyncWebServerResponse *response =
                      request->beginResponse(200, "text/plain", textResponse);
                  response->addHeader("Content-Disposition", "attachment; filename=\"logs.txt\"");
                  request->send(response);
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

    // Version endpoint
    server.on(kRouteVersion, HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  // Use BUILD_DATE and BUILD_TIME if set by build script, otherwise fall back to __DATE__ and __TIME__
                  #ifdef BUILD_DATE
                      const char* buildDate = BUILD_DATE;
                      const char* buildTime = BUILD_TIME;
                  #else
                      const char* buildDate = __DATE__;
                      const char* buildTime = __TIME__;
                  #endif

                  DynamicJsonDocument jsonDoc(512);
                  jsonDoc["firmware_version"] = firmwareVersion;
                  jsonDoc["chip_family"]      = chipFamily;
                  jsonDoc["build_date"]       = buildDate;
                  jsonDoc["build_time"]       = buildTime;
                  jsonDoc["firmware_thumbprint"] = getBuildThumbprint(buildDate, buildTime);
                  jsonDoc["filesystem_thumbprint"] = getFilesystemThumbprint();
                  jsonDoc["build_version"] = getBuildVersion();

                  String jsonResponse;
                  serializeJson(jsonDoc, jsonResponse);
                  request->send(200, "application/json", jsonResponse);
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

void WebServer::loop()
{
    ElegantOTA.loop();
    unsigned long now = millis();
    if (statusEvents.count() > 0 && now - lastStatusBroadcastMs >= statusBroadcastIntervalMs)
    {
        lastStatusBroadcastMs = now;
        broadcastStatusUpdate();
    }
}

void WebServer::buildStatusJson(DynamicJsonDocument &jsonDoc, const printer_info_t &elegooStatus)
{
    jsonDoc["stopped"]        = elegooStatus.filamentStopped;
    jsonDoc["filamentRunout"] = elegooStatus.filamentRunout;

    jsonDoc["mac"] = WiFi.macAddress();
    jsonDoc["ip"]  = WiFi.localIP().toString();

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
    printer_info_t elegooStatus = elegooCC.getCurrentInformation();
    // JSON allocation: 576 bytes heap (was 768 bytes)
    // Measured actual: ~480 bytes (83% utilization, 17% margin)
    // Last measured: 2025-11-26
    // See: .claude/hardcoded-allocations.md for maintenance notes
    DynamicJsonDocument jsonDoc(576);
    buildStatusJson(jsonDoc, elegooStatus);
    String payload;
    payload.reserve(576);  // Pre-allocate to prevent fragmentation
    serializeJson(jsonDoc, payload);

    // Pin Values level: Check if approaching allocation limit
    if (settingsManager.getLogLevel() >= LOG_PIN_VALUES)
    {
        size_t actualSize = measureJson(jsonDoc);
        static bool logged = false;
        if (!logged && actualSize > 490)  // >85% of 576 bytes
        {
            logger.logf(LOG_PIN_VALUES, "WebServer broadcastStatusUpdate JSON size: %zu / 576 bytes (%.1f%%)",
                       actualSize, (actualSize * 100.0f / 576.0f));
            logged = true;  // Only log once per session
        }
    }

    bool idleState = (elegooStatus.printStatus == 0 || elegooStatus.printStatus == 9);
    if (idleState)
    {
        if (payload == lastIdlePayload)
        {
            statusBroadcastIntervalMs = 5000;
            return;
        }
        lastIdlePayload = payload;
    }
    else
    {
        lastIdlePayload = "";
    }

    statusEvents.send(payload.c_str(), "status");

    bool isPrinting = elegooStatus.printStatus != 0 && elegooStatus.printStatus != 9;
    statusBroadcastIntervalMs = isPrinting ? 1000 : 5000;
}
