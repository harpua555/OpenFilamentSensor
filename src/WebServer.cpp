#include "WebServer.h"

#include <AsyncJson.h>

#include "ElegooCC.h"
#include "Logger.h"

#define SPIFFS LittleFS

// External reference to firmware version from main.cpp
extern const char *firmwareVersion;
extern const char *chipFamily;

WebServer::WebServer(int port) : server(port), statusEvents("/status_events") {}

void WebServer::begin()
{
    server.begin();

    // Get settings endpoint
    server.on("/get_settings", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String jsonResponse = settingsManager.toJson(false);
                  request->send(200, "application/json", jsonResponse);
              });

    server.addHandler(new AsyncCallbackJsonWebHandler(
        "/update_settings",
        [this](AsyncWebServerRequest *request, JsonVariant &json)
        {
            JsonObject jsonObj = json.as<JsonObject>();
            settingsManager.setElegooIP(jsonObj["elegooip"].as<String>());
            settingsManager.setSSID(jsonObj["ssid"].as<String>());
            if (jsonObj.containsKey("passwd") && jsonObj["passwd"].as<String>().length() > 0)
            {
                settingsManager.setPassword(jsonObj["passwd"].as<String>());
            }
            settingsManager.setAPMode(jsonObj["ap_mode"].as<bool>());
            settingsManager.setPauseOnRunout(jsonObj["pause_on_runout"].as<bool>());
            settingsManager.setEnabled(jsonObj["enabled"].as<bool>());
            settingsManager.setStartPrintTimeout(jsonObj["start_print_timeout"].as<int>());
            // Handle both new and deprecated settings names for backwards compatibility
            if (jsonObj.containsKey("detection_length_mm"))
            {
                settingsManager.setDetectionLengthMM(jsonObj["detection_length_mm"].as<float>());
            }
            else if (jsonObj.containsKey("expected_deficit_mm"))
            {
                // Deprecated: redirect to new setting
                settingsManager.setDetectionLengthMM(jsonObj["expected_deficit_mm"].as<float>());
            }
            if (jsonObj.containsKey("detection_grace_period_ms"))
            {
                settingsManager.setDetectionGracePeriodMs(
                    jsonObj["detection_grace_period_ms"].as<int>());
            }
            if (jsonObj.containsKey("tracking_mode"))
            {
                settingsManager.setTrackingMode(jsonObj["tracking_mode"].as<int>());
            }
            if (jsonObj.containsKey("tracking_window_ms"))
            {
                settingsManager.setTrackingWindowMs(jsonObj["tracking_window_ms"].as<int>());
            }
            if (jsonObj.containsKey("tracking_ewma_alpha"))
            {
                settingsManager.setTrackingEwmaAlpha(
                    jsonObj["tracking_ewma_alpha"].as<float>());
            }
            if (jsonObj.containsKey("detection_ratio_threshold"))
            {
                settingsManager.setDetectionRatioThreshold(
                    jsonObj["detection_ratio_threshold"].as<float>());
            }
            if (jsonObj.containsKey("detection_hard_jam_mm"))
            {
                settingsManager.setDetectionHardJamMm(
                    jsonObj["detection_hard_jam_mm"].as<float>());
            }
            if (jsonObj.containsKey("detection_soft_jam_time_ms"))
            {
                settingsManager.setDetectionSoftJamTimeMs(
                    jsonObj["detection_soft_jam_time_ms"].as<int>());
            }
            if (jsonObj.containsKey("detection_hard_jam_time_ms"))
            {
                settingsManager.setDetectionHardJamTimeMs(
                    jsonObj["detection_hard_jam_time_ms"].as<int>());
            }
            // expected_flow_window_ms is deprecated and ignored (no longer used)
            if (jsonObj.containsKey("sdcp_loss_behavior"))
            {
                settingsManager.setSdcpLossBehavior(jsonObj["sdcp_loss_behavior"].as<int>());
            }
            if (jsonObj.containsKey("flow_telemetry_stale_ms"))
            {
                settingsManager.setFlowTelemetryStaleMs(
                    jsonObj["flow_telemetry_stale_ms"].as<int>());
            }
            if (jsonObj.containsKey("ui_refresh_interval_ms"))
            {
                settingsManager.setUiRefreshIntervalMs(
                    jsonObj["ui_refresh_interval_ms"].as<int>());
            }
            // Deprecated settings - accepted but ignored for backwards compatibility
            // zero_deficit_logging - removed (use verbose_logging)
            // use_total_extrusion_deficit - removed (always use total mode)
            // total_vs_delta_logging - removed (only one mode now)
            // packet_flow_logging - removed (use verbose_logging)
            // use_total_extrusion_backlog - removed (always enabled)
            if (jsonObj.containsKey("dev_mode"))
            {
                settingsManager.setDevMode(jsonObj["dev_mode"].as<bool>());
            }
            if (jsonObj.containsKey("verbose_logging"))
            {
                settingsManager.setVerboseLogging(jsonObj["verbose_logging"].as<bool>());
            }
            if (jsonObj.containsKey("flow_summary_logging"))
            {
                settingsManager.setFlowSummaryLogging(
                    jsonObj["flow_summary_logging"].as<bool>());
            }
            if (jsonObj.containsKey("movement_mm_per_pulse"))
            {
                settingsManager.setMovementMmPerPulse(
                    jsonObj["movement_mm_per_pulse"].as<float>());
            }
            if (jsonObj.containsKey("auto_calibrate_sensor"))
            {
                settingsManager.setAutoCalibrateSensor(
                    jsonObj["auto_calibrate_sensor"].as<bool>());
            }
            settingsManager.save();
            jsonObj.clear();
            request->send(200, "text/plain", "ok");
        }));

    server.on("/discover_printer", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String ip;
                  // Use a 3s timeout for discovery via the ElegooCC helper.
                  if (!elegooCC.discoverPrinterIP(ip, 3000))
                  {
                      DynamicJsonDocument jsonDoc(128);
                      jsonDoc["error"] = "No printer found";
                      String jsonResponse;
                      serializeJson(jsonDoc, jsonResponse);
                      request->send(504, "application/json", jsonResponse);
                      return;
                  }

                  settingsManager.setElegooIP(ip);
                  settingsManager.save(true);

                  DynamicJsonDocument jsonDoc(128);
                  jsonDoc["elegooip"] = ip;
                  String jsonResponse;
                  serializeJson(jsonDoc, jsonResponse);
                  request->send(200, "application/json", jsonResponse);
              });

    // Setup ElegantOTA
    ElegantOTA.begin(&server);

    statusEvents.onConnect([](AsyncEventSourceClient *client) {
        client->send("connected", "init", millis(), 1000);
    });
    server.addHandler(&statusEvents);

    // Sensor status endpoint
    server.on("/sensor_status", HTTP_GET,
              [this](AsyncWebServerRequest *request)
              {
                  printer_info_t elegooStatus = elegooCC.getCurrentInformation();

                  DynamicJsonDocument jsonDoc(768);
                  buildStatusJson(jsonDoc, elegooStatus);

                  String jsonResponse;
                  serializeJson(jsonDoc, jsonResponse);
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
    server.on("/api/logs_text", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String textResponse = logger.getLogsAsText();
                  AsyncWebServerResponse *response =
                      request->beginResponse(200, "text/plain", textResponse);
                  response->addHeader("Content-Disposition", "attachment; filename=\"logs.txt\"");
                  request->send(response);
              });

    // Live logs endpoint (last 100 entries for UI display)
    server.on("/api/logs_live", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  String textResponse = logger.getLogsAsText(100);  // Only last 100 entries
                  request->send(200, "text/plain", textResponse);
              });

    // Version endpoint
    server.on("/version", HTTP_GET,
              [](AsyncWebServerRequest *request)
              {
                  DynamicJsonDocument jsonDoc(256);
                  jsonDoc["firmware_version"] = firmwareVersion;
                  jsonDoc["chip_family"]      = chipFamily;
                  jsonDoc["build_date"]       = __DATE__;
                  jsonDoc["build_time"]       = __TIME__;

                  String jsonResponse;
                  serializeJson(jsonDoc, jsonResponse);
                  request->send(200, "application/json", jsonResponse);
              });

    // Serve lightweight UI from /lite (if available)
    // Keep explicit /lite path for backwards compatibility
    server.serveStatic("/lite", SPIFFS, "/lite/").setDefaultFile("index.htm");

    // Always serve the lightweight UI at the root as well.
    server.serveStatic("/", SPIFFS, "/lite/").setDefaultFile("index.htm");

    // SPA-style routing: for any unknown GET that isn't an API or asset,
    // serve index.htm so that the frontend router can handle the path.
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_GET &&
            !request->url().startsWith("/api/") &&
            !request->url().startsWith("/assets/"))
        {
            request->send(SPIFFS, "/lite/index.htm", "text/html");
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

    jsonDoc["mac"]       = WiFi.macAddress();
    jsonDoc["ip"]        = WiFi.localIP().toString();
    jsonDoc["rssi"]      = WiFi.RSSI();
    jsonDoc["free_heap"] = ESP.getFreeHeap();

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
    elegoo["hardJamPercent"]       = elegooStatus.hardJamPercent;
    elegoo["softJamPercent"]       = elegooStatus.softJamPercent;
    elegoo["movementPulses"]       = (uint32_t) elegooStatus.movementPulseCount;
    elegoo["uiRefreshIntervalMs"]  = settingsManager.getUiRefreshIntervalMs();
    elegoo["flowTelemetryStaleMs"] = settingsManager.getFlowTelemetryStaleMs();
}

void WebServer::broadcastStatusUpdate()
{
    printer_info_t elegooStatus = elegooCC.getCurrentInformation();
    DynamicJsonDocument jsonDoc(768);
    buildStatusJson(jsonDoc, elegooStatus);
    String payload;
    serializeJson(jsonDoc, payload);
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
