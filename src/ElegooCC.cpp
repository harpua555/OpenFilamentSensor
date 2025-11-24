#include "ElegooCC.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "FilamentMotionSensor.h"
#include "Logger.h"
#include "SettingsManager.h"

#define ACK_TIMEOUT_MS 5000
constexpr float        DEFAULT_FILAMENT_DEFICIT_THRESHOLD_MM = 8.4f;
constexpr unsigned int EXPECTED_FILAMENT_SAMPLE_MS           = 1000;  // Log max once per second to prevent heap exhaustion
constexpr unsigned int EXPECTED_FILAMENT_STALE_MS            = 1000;
constexpr unsigned int SDCP_LOSS_TIMEOUT_MS                  = 10000;
constexpr unsigned int PAUSE_REARM_DELAY_MS                  = 3000;
static const char*     TOTAL_EXTRUSION_HEX_KEY       = "54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00";
static const char*     CURRENT_EXTRUSION_HEX_KEY =
    "43 75 72 72 65 6E 74 45 78 74 72 75 73 69 6F 6E 00";
// UDP discovery port used by the Elegoo SDCP implementation (matches the
// Home Assistant integration and printer firmware).
static const uint16_t  SDCP_DISCOVERY_PORT = 3000;

// External function to get current time (from main.cpp)
extern unsigned long getTime();

ElegooCC &ElegooCC::getInstance()
{
    static ElegooCC instance;
    return instance;
}

ElegooCC::ElegooCC()
{
    lastMovementValue = -1;
    lastChangeTime    = 0;

    mainboardID       = "";
    printStatus       = SDCP_PRINT_STATUS_IDLE;
    machineStatusMask = 0;  // No statuses active initially
    currentLayer      = 0;
    totalLayer        = 0;
    progress          = 0;
    currentTicks      = 0;
    totalTicks        = 0;
    PrintSpeedPct     = 0;
    filamentStopped   = false;
    filamentRunout    = false;
    lastPing          = 0;
    expectedFilamentMM         = 0;
    actualFilamentMM           = 0;
    lastExpectedDeltaMM        = 0;
    expectedTelemetryAvailable = false;
    lastSuccessfulTelemetryMs  = 0;
    lastTelemetryReceiveMs     = 0;
    lastStatusReceiveMs          = 0;
    telemetryAvailableLastStatus = false;
    currentDeficitMm             = 0.0f;
    deficitThresholdMm           = 0.0f;
    deficitRatio                 = 0.0f;
    smoothedDeficitRatio         = 0.0f;
    hardJamPercent               = 0.0f;
    softJamPercent               = 0.0f;
    hardJamAccumulatedMs         = 0;
    softJamAccumulatedMs         = 0;
    lastJamEvalMs                = 0;
    lastJamDebugMs               = 0;
    lastHardJamPulseCount        = 0;
    movementPulseCount           = 0;
    lastFlowLogMs                = 0;
    lastSummaryLogMs             = 0;
    lastLoggedExpected           = -1.0f;
    lastLoggedActual             = -1.0f;
    lastLoggedDeficit            = -1.0f;
    lastLoggedPrintStatus        = -1;
    lastLoggedLayer              = -1;
    lastLoggedTotalLayer         = -1;
    jamPauseRequested            = false;
    trackingFrozen               = false;
    resumeGraceActive            = false;
    resumeGracePulseBaseline     = 0;
    resumeGraceActualBaseline    = 0.0f;
    motionSensor.reset();

    waitingForAck       = false;
    pendingAckCommand   = -1;
    pendingAckRequestId = "";
    ackWaitStartTime    = 0;
    lastPauseRequestMs  = 0;
    lastStatusRequestMs = 0;
    lastPrintEndMs      = 0;

    // TODO: send a UDP broadcast, M99999 on Port 30000, maybe using AsyncUDP.h and listen for the
    // result. this will give us the printer IP address.

    // event handler - use lambda to capture 'this' pointer
    webSocket.onEvent([this](WStype_t type, uint8_t *payload, size_t length)
                      { this->webSocketEvent(type, payload, length); });
}

void ElegooCC::setup()
{
    bool shouldConect = !settingsManager.isAPMode();
    if (shouldConect)
    {
        connect();
    }
}

void ElegooCC::webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
        case WStype_DISCONNECTED:
            logger.log("Disconnected from Centauri Carbon");
            // Reset acknowledgment state on disconnect
            waitingForAck       = false;
            pendingAckCommand   = -1;
            pendingAckRequestId = "";
            ackWaitStartTime    = 0;
            break;
        case WStype_CONNECTED:
            logger.log("Connected to Carbon Centauri");
            sendCommand(SDCP_COMMAND_STATUS);

            break;
        case WStype_TEXT:
        {
            StaticJsonDocument<2048> doc;
            DeserializationError     error = deserializeJson(doc, payload);

            if (error)
            {
                logger.logf("JSON parsing failed: %s", error.c_str());
                return;
            }

            // Check if this is a command acknowledgment response
            if (doc.containsKey("Id") && doc.containsKey("Data"))
            {
                handleCommandResponse(doc);
            }
            // Check if this is a status response
            else if (doc.containsKey("Status"))
            {
                handleStatus(doc);
            }
        }
        break;
        case WStype_BIN:
            logger.log("Received unspported binary data");
            break;
        case WStype_ERROR:
            logger.logf("WebSocket error: %s", payload);
            break;
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            logger.log("Received unspported fragment data");
            break;
    }
}

void ElegooCC::handleCommandResponse(JsonDocument &doc)
{
    String     id   = doc["Id"];
    JsonObject data = doc["Data"];

    if (data.containsKey("Cmd") && data.containsKey("RequestID"))
    {
        int    cmd         = data["Cmd"];
        int    ack         = data["Data"]["Ack"];
        String requestId   = data["RequestID"];
        String mainboardId = data["MainboardID"];

        // Only log acknowledgments for commands we're actively waiting for
        // (skip STATUS commands to avoid log spam - they're sent every 250ms)
        if (waitingForAck && cmd == pendingAckCommand && requestId == pendingAckRequestId)
        {
            logger.logf("Received acknowledgment for command %d (Ack: %d)", cmd, ack);
            waitingForAck       = false;
            pendingAckCommand   = -1;
            pendingAckRequestId = "";
            ackWaitStartTime    = 0;
        }

        // Store mainboard ID if we don't have it yet
        if (mainboardID.isEmpty() && !mainboardId.isEmpty())
        {
            mainboardID = mainboardId;
            logger.logf("Stored MainboardID: %s", mainboardID.c_str());
        }
    }
}

void ElegooCC::handleStatus(JsonDocument &doc)
{
    JsonObject status      = doc["Status"];
    String     mainboardId = doc["MainboardID"];
    unsigned long statusTimestamp = millis();
    bool wasPrinting = isPrinting();
    lastStatusReceiveMs          = statusTimestamp;
    // Parse current status (which contains machine status array)
    if (status.containsKey("CurrentStatus"))
    {
        JsonArray currentStatus = status["CurrentStatus"];

        // Convert JsonArray to int array for machine statuses
        int statuses[5];  // Max 5 statuses
        int count = min((int) currentStatus.size(), 5);
        for (int i = 0; i < count; i++)
        {
            statuses[i] = currentStatus[i].as<int>();
        }

        // Set all machine statuses at once
        setMachineStatuses(statuses, count);
    }

    // Parse CurrentCoords to extract Z coordinate
    if (status.containsKey("CurrenCoord"))
    {
        String coordsStr   = status["CurrenCoord"].as<String>();
        int    firstComma  = coordsStr.indexOf(',');
        int    secondComma = coordsStr.indexOf(',', firstComma + 1);
        if (firstComma != -1 && secondComma != -1)
        {
            String zStr = coordsStr.substring(secondComma + 1);
            currentZ    = zStr.toFloat();
        }
    }

    // Parse print info
    if (status.containsKey("PrintInfo"))
    {
        JsonObject          printInfo = status["PrintInfo"];
        sdcp_print_status_t newStatus = printInfo["Status"].as<sdcp_print_status_t>();

        // Any time we receive a well-formed PrintInfo block, treat SDCP
        // telemetry as available at the connection level, even if this
        // particular payload doesn't include extrusion fields. Extrusion
        // freshness is tracked separately via expectedTelemetryAvailable.
        telemetryAvailableLastStatus = true;
        lastSuccessfulTelemetryMs    = statusTimestamp;

        if (newStatus != printStatus)
        {
            bool wasPrinting   = (printStatus == SDCP_PRINT_STATUS_PRINTING);
            bool isPrintingNow = (newStatus == SDCP_PRINT_STATUS_PRINTING);

            if (isPrintingNow)
            {
                // Transition into PRINTING.
                // If we previously issued a jam-driven pause, treat the next
                // PRINTING state as a resume regardless of any intermediate
                // statuses (e.g. HEATING or other transitional codes).
                if (jamPauseRequested ||
                    printStatus == SDCP_PRINT_STATUS_PAUSED ||
                    printStatus == SDCP_PRINT_STATUS_PAUSING)
                {
                    logger.log("Print status changed to printing (resume)");
                    trackingFrozen = false;
                    // On resume, reset the motion sensor so jam detection starts fresh
                    motionSensor.reset();
                    currentDeficitMm        = 0.0f;
                    deficitRatio            = 0.0f;
                    smoothedDeficitRatio    = 0.0f;
                    jamPauseRequested       = false;
                    filamentStopped         = false;
                    resumeGraceActive       = true;
                    resumeGracePulseBaseline = movementPulseCount;
                    resumeGraceActualBaseline = actualFilamentMM;
                    if (settingsManager.getVerboseLogging())
                    {
                        logger.log("Motion sensor reset (resume after pause)");
                        logger.log("Post-resume grace active until movement detected");
                    }
                }
                else
                {
                    // Treat all other transitions into PRINTING as a new print.
                    logger.log("Print status changed to printing");
                    startedAt = millis();
                    resetFilamentTracking();

                    // Log active settings for this print (excluding network config)
                    logger.logf("Print settings: pulse=%.2fmm mode=%d window=%dms grace=%dms ratio_thr=%.2f hard_jam=%.1fmm soft_time=%dms hard_time=%dms",
                               settingsManager.getMovementMmPerPulse(),
                               settingsManager.getTrackingMode(),
                               settingsManager.getTrackingWindowMs(),
                               settingsManager.getDetectionGracePeriodMs(),
                               settingsManager.getDetectionRatioThreshold(),
                               settingsManager.getDetectionHardJamMm(),
                               settingsManager.getDetectionSoftJamTimeMs(),
                               settingsManager.getDetectionHardJamTimeMs());
                }
            }
            else if (wasPrinting)
            {
                // Transition out of PRINTING.
                if (newStatus == SDCP_PRINT_STATUS_PAUSED ||
                    newStatus == SDCP_PRINT_STATUS_PAUSING)
                {
                    // Printer has entered a paused state. If we requested the
                    // pause due to a filament jam, freeze tracking so the
                    // displayed values stay at the moment of pause.
                    logger.log("Print status changed to paused");
                    if (jamPauseRequested)
                    {
                        trackingFrozen = true;
                        logger.log("Freezing filament tracking while paused after jam");
                    }
                }
                else
                {
                    // Print has ended (stopped/completed/etc). Log a summary and
                    // fully reset tracking for the next job.
                    logger.logf(
                        "Print summary: status=%d progress=%d layer=%d/%d ticks=%d/%d "
                        "expected=%.2fmm actual=%.2fmm deficit=%.2fmm pulses=%lu",
                        (int) newStatus, progress, currentLayer, totalLayer, currentTicks,
                        totalTicks, expectedFilamentMM, actualFilamentMM, currentDeficitMm,
                        movementPulseCount);

                    // Auto-calibration: calculate mm_per_pulse from print data
                    if (settingsManager.getAutoCalibrateSensor() && movementPulseCount > 0 &&
                        expectedFilamentMM > 50.0f)
                    {
                        // Minimum thresholds: 50+ pulses and 50+mm expected for reliable calibration
                        if (movementPulseCount >= 50)
                        {
                            float calculatedMmPerPulse = expectedFilamentMM / (float) movementPulseCount;

                            // Sanity check: should be between 2.5-3.5mm (reasonable range for SFS 2.0)
                            if (calculatedMmPerPulse >= 2.5f && calculatedMmPerPulse <= 3.5f)
                            {
                                float oldValue = settingsManager.getMovementMmPerPulse();
                                settingsManager.setMovementMmPerPulse(calculatedMmPerPulse);

                                // Disable auto-calibration after successful calibration
                                // Only needs to run once to determine the correct value
                                settingsManager.setAutoCalibrateSensor(false);
                                settingsManager.save();

                                logger.logf(
                                    "Auto-calibration: Updated mm_per_pulse from %.3f to %.3f "
                                    "(based on %.2fmm expected / %lu pulses)",
                                    oldValue, calculatedMmPerPulse, expectedFilamentMM, movementPulseCount);
                                logger.log("Auto-calibration: Disabled after successful calibration");
                            }
                            else
                            {
                                logger.logf(
                                    "Auto-calibration: Calculated value %.3f is outside valid range "
                                    "(2.5-3.5mm), keeping current setting",
                                    calculatedMmPerPulse);
                            }
                        }
                        else
                        {
                            logger.logf(
                                "Auto-calibration: Not enough pulses (%lu, need 50+) for reliable "
                                "calibration",
                                movementPulseCount);
                        }
                    }

                    logger.log("Print left printing state, resetting filament tracking");
                    resetFilamentTracking();
                }
            }
        }
    printStatus  = newStatus;
    bool nowPrinting = isPrinting();
    if (wasPrinting && !nowPrinting)
    {
        lastPrintEndMs = statusTimestamp;
    }
    else if (nowPrinting)
    {
        lastPrintEndMs = 0;
    }
        currentLayer = printInfo["CurrentLayer"];
        totalLayer   = printInfo["TotalLayer"];
        progress     = printInfo["Progress"];
        currentTicks = printInfo["CurrentTicks"];
        totalTicks   = printInfo["TotalTicks"];
        PrintSpeedPct = printInfo["PrintSpeedPct"];

        // Update extrusion tracking (expected/actual/deficit) based on any
        // TotalExtrusion / CurrentExtrusion fields present in this payload.
        processFilamentTelemetry(printInfo, statusTimestamp);

        if (settingsManager.getVerboseLogging())
        {
            // Only log if meaningful status values have changed
            if ((int)printStatus != lastLoggedPrintStatus ||
                currentLayer != lastLoggedLayer ||
                totalLayer != lastLoggedTotalLayer)
            {
                logger.logf(
                    "Flow debug: SDCP status print=%d layer=%d/%d progress=%d expected=%.2fmm "
                    "delta=%.2fmm telemetry=%d",
                    (int) printStatus, currentLayer, totalLayer, progress, expectedFilamentMM,
                    lastExpectedDeltaMM, telemetryAvailableLastStatus ? 1 : 0);
                lastLoggedPrintStatus = (int)printStatus;
                lastLoggedLayer = currentLayer;
                lastLoggedTotalLayer = totalLayer;
            }
        }
    }

    // Store mainboard ID if we don't have it yet (I'm unsure if we actually need this)
    if (mainboardID.isEmpty() && !mainboardId.isEmpty())
    {
        mainboardID = mainboardId;
        logger.logf("Stored MainboardID: %s", mainboardID.c_str());
    }
}

void ElegooCC::resetFilamentTracking()
{
    lastMovementValue          = -1;
    lastChangeTime             = millis();
    actualFilamentMM           = 0;
    expectedFilamentMM         = 0;
    lastExpectedDeltaMM        = 0;
    expectedTelemetryAvailable = false;
    lastSuccessfulTelemetryMs  = 0;
    filamentStopped            = false;
    lastTelemetryReceiveMs     = 0;
    movementPulseCount         = 0;
    currentDeficitMm           = 0.0f;
    deficitThresholdMm         = 0.0f;
    deficitRatio               = 0.0f;
    smoothedDeficitRatio       = 0.0f;
    lastFlowLogMs              = 0;
    jamPauseRequested          = false;
    trackingFrozen             = false;
    resumeGraceActive          = false;
    resumeGracePulseBaseline   = movementPulseCount;
    resumeGraceActualBaseline  = actualFilamentMM;

    resetJamTracking();

    // Configure and reset the motion sensor
    int trackingMode = settingsManager.getTrackingMode();
    int windowMs = settingsManager.getTrackingWindowMs();
    float ewmaAlpha = settingsManager.getTrackingEwmaAlpha();

    motionSensor.setTrackingMode((FilamentTrackingMode)trackingMode, windowMs, ewmaAlpha);
    motionSensor.reset();

    if (settingsManager.getVerboseLogging())
    {
        const char* modeNames[] = {"Cumulative", "Windowed", "EWMA"};
        const char* modeName = (trackingMode >= 0 && trackingMode <= 2) ? modeNames[trackingMode] : "Unknown";
        logger.logf("Filament tracking reset - Mode: %s, Window: %dms, EWMA Alpha: %.2f",
                   modeName, windowMs, ewmaAlpha);
    }
}

void ElegooCC::resetJamTracking()
{
    hardJamAccumulatedMs  = 0;
    softJamAccumulatedMs  = 0;
    hardJamPercent        = 0.0f;
    softJamPercent        = 0.0f;
    lastJamEvalMs         = 0;
    lastJamDebugMs        = 0;
    lastHardJamPulseCount = movementPulseCount;
}

bool ElegooCC::tryReadExtrusionValue(JsonObject &printInfo, const char *key, const char *hexKey,
                                     float &output)
{
    if (printInfo.containsKey(key) && !printInfo[key].isNull())
    {
        output = printInfo[key].as<float>();
        return true;
    }

    if (hexKey != nullptr && printInfo.containsKey(hexKey) && !printInfo[hexKey].isNull())
    {
        output = printInfo[hexKey].as<float>();
        return true;
    }

    return false;
}

bool ElegooCC::processFilamentTelemetry(JsonObject &printInfo, unsigned long currentTime)
{
    float totalValue = 0;
    bool  hasTotal   = tryReadExtrusionValue(printInfo, "TotalExtrusion", TOTAL_EXTRUSION_HEX_KEY,
                                            totalValue);

    // New simplified approach: only use TotalExtrusion (Klipper-style)
    if (hasTotal)
    {
        expectedFilamentMM = totalValue < 0 ? 0 : totalValue;

        // Update the motion sensor with the new expected position
        motionSensor.updateExpectedPosition(expectedFilamentMM);

        // Mark telemetry as available and fresh
        expectedTelemetryAvailable = true;
        lastTelemetryReceiveMs     = currentTime;

        if (settingsManager.getVerboseLogging())
        {
            float windowedExpected = motionSensor.getExpectedDistance();
            float windowedSensor = motionSensor.getSensorDistance();
            float currentDeficit = motionSensor.getDeficit();

            // Only log if values have changed
            if (windowedExpected != lastLoggedExpected ||
                windowedSensor != lastLoggedActual ||
                currentDeficit != lastLoggedDeficit)
            {
                // Show SDCP expected + cumulative sensor (not windowed) for clarity
                logger.logf("Telemetry: sdcp_exp=%.2fmm cumul_sns=%.2fmm pulses=%lu | win_exp=%.2f win_sns=%.2f deficit=%.2f",
                            expectedFilamentMM, actualFilamentMM, movementPulseCount,
                            windowedExpected, windowedSensor, currentDeficit);
                lastLoggedExpected = windowedExpected;
                lastLoggedActual = windowedSensor;
                lastLoggedDeficit = currentDeficit;
            }
        }

        return true;
    }

    return false;
}

void ElegooCC::pausePrint()
{
    if (settingsManager.getDevMode())
    {
        lastPauseRequestMs = millis();
        logger.log("Dev mode is enabled: pausePrint suppressed (would send pause command)");
        return;
    }
    jamPauseRequested   = true;
    trackingFrozen      = false;
    lastPauseRequestMs = millis();
    sendCommand(SDCP_COMMAND_PAUSE_PRINT, true);
}

void ElegooCC::continuePrint()
{
    sendCommand(SDCP_COMMAND_CONTINUE_PRINT, true);
}

void ElegooCC::sendCommand(int command, bool waitForAck)
{
    if (!webSocket.isConnected())
    {
        logger.logf("Can't send command, websocket not connected: %d", command);
        return;
    }

    // If this command requires an ack and we're already waiting for one, skip it
    if (waitForAck && waitingForAck)
    {
        logger.logf("Skipping command %d - already waiting for ack from command %d", command,
                    pendingAckCommand);
        return;
    }

    uuid.generate();
    String uuidStr = String(uuid.toCharArray());
    uuidStr.replace("-", "");  // RequestID doesn't want dashes

    // Get current timestamp
    unsigned long timestamp = getTime();

    StaticJsonDocument<512> doc;
    doc["Id"] = uuidStr;
    JsonObject data = doc.createNestedObject("Data");
    data["Cmd"]     = command;
    data["RequestID"]   = uuidStr;
    data["MainboardID"] = mainboardID;
    data["TimeStamp"]   = timestamp;
    // Match the Home Assistant integration's client identity for SDCP commands.
    // From = 0 is used there and is known to work reliably for pause/stop.
    data["From"] = 0;

    // Explicit empty Data object (matches existing payload structure)
    data.createNestedObject("Data");

    // Include current SDCP print and machine status, mirroring the status payload fields.
    data["PrintStatus"] = static_cast<int>(printStatus);
    JsonArray currentStatus = data.createNestedArray("CurrentStatus");
    for (int s = 0; s <= 4; ++s)
    {
        if (hasMachineStatus(static_cast<sdcp_machine_status_t>(s)))
        {
            currentStatus.add(s);
        }
    }

    // When we know the MainboardID, include a Topic field that matches the
    // "sdcp/request/<MainboardID>" pattern used by the Elegoo HA integration.
    if (!mainboardID.isEmpty())
    {
        String topic = "sdcp/request/";
        topic += mainboardID;
        doc["Topic"] = topic;
    }

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    // If this command requires an ack, set the tracking state
    if (waitForAck)
    {
        waitingForAck       = true;
        pendingAckCommand   = command;
        pendingAckRequestId = uuidStr;
        ackWaitStartTime    = millis();
        logger.logf("Waiting for acknowledgment for command %d with request ID %s", command,
                    uuidStr.c_str());
    }

    webSocket.sendTXT(jsonPayload);
    if (command == SDCP_COMMAND_STATUS)
    {
        lastStatusRequestMs = millis();
    }
}

void ElegooCC::maybeRequestStatus(unsigned long currentTime)
{
    if (!webSocket.isConnected())
    {
        return;
    }

    // Use isPrintJobActive() for polling decisions - this includes heating, homing,
    // bed leveling, pausing, etc. (any non-idle state)
    bool jobActive = isPrintJobActive();
    unsigned long interval;
    bool inPostPrintGrace = false;

    if (!jobActive && lastPrintEndMs != 0)
    {
        unsigned long sinceEnd = currentTime - lastPrintEndMs;
        if (sinceEnd < STATUS_POST_PRINT_COOLDOWN_MS)
        {
            inPostPrintGrace = true;
        }
    }

    if (jobActive || inPostPrintGrace)
    {
        interval = STATUS_ACTIVE_INTERVAL_MS;  // 250ms
        if (jobActive)
        {
            lastPrintEndMs = 0;
        }
    }
    else
    {
        interval = STATUS_IDLE_INTERVAL_MS;  // 10000ms
    }

    if (lastStatusRequestMs == 0 || currentTime - lastStatusRequestMs >= interval)
    {
        sendCommand(SDCP_COMMAND_STATUS);
    }
}

void ElegooCC::connect()
{
    if (webSocket.isConnected())
    {
        webSocket.disconnect();
    }
    webSocket.setReconnectInterval(3000);
    ipAddress = settingsManager.getElegooIP();
    logger.logf("Attempting connection to Elegoo CC @ %s", ipAddress.c_str());
    webSocket.begin(ipAddress, CARBON_CENTAURI_PORT, "/websocket");
}

void ElegooCC::loop()
{
    unsigned long currentTime = millis();

    // websocket IP changed, reconnect
    if (ipAddress != settingsManager.getElegooIP())
    {
        connect();  // this will reconnnect if already connected
    }

    if (webSocket.isConnected())
    {
        // Check for acknowledgment timeout (5 seconds)
        // TODO: need to check the actual requestId
        if (waitingForAck && (currentTime - ackWaitStartTime) >= ACK_TIMEOUT_MS)
        {
            logger.logf("Acknowledgment timeout for command %d, resetting ack state",
                        pendingAckCommand);
            waitingForAck       = false;
            pendingAckCommand   = -1;
            pendingAckRequestId = "";
            ackWaitStartTime    = 0;
        }
        else if (currentTime - lastPing > 29900)
        {
            // Keepalive ping every 30 seconds - no need to log, only log actual disconnects
            // For all who venture to this line of code wondering why I didn't use sendPing(), it's
            // because for some reason that doesn't work. but this does!
            this->webSocket.sendTXT("ping");
            lastPing = currentTime;
        }
    }

    // Check filament sensors before determining if we should pause
    checkFilamentMovement(currentTime);
    checkFilamentRunout(currentTime);

    // Check if we should pause the print
    if (shouldPausePrint(currentTime))
    {
        logger.log("Pausing print, detected filament runout or stopped");
        pausePrint();
    }

    maybeRequestStatus(currentTime);

    webSocket.loop();
}

void ElegooCC::checkFilamentRunout(unsigned long currentTime)
{
    // The signal output of the switch sensor is at low level when no filament is detected
    bool newFilamentRunout = digitalRead(FILAMENT_RUNOUT_PIN) == LOW;
    if (newFilamentRunout != filamentRunout)
    {
        logger.log(filamentRunout ? "Filament has run out" : "Filament has been detected");
    }
    filamentRunout = newFilamentRunout;
}

void ElegooCC::checkFilamentMovement(unsigned long currentTime)
{
    if (trackingFrozen)
    {
        // When tracking is frozen (printer paused after a jam), just track pin changes
        int currentMovementValue = digitalRead(MOVEMENT_SENSOR_PIN);
        if (currentMovementValue != lastMovementValue)
        {
            lastMovementValue = currentMovementValue;
            lastChangeTime    = currentTime;
        }
        return;
    }

    int  currentMovementValue = digitalRead(MOVEMENT_SENSOR_PIN);
    bool debugFlow            = settingsManager.getVerboseLogging();
    bool summaryFlow          = settingsManager.getFlowSummaryLogging();
    bool currentlyPrinting    = isPrinting();

    // Track movement pulses - only count RISING edge (LOW to HIGH transition)
    // This matches typical sensor specs where 2.88mm = one complete pulse cycle
    if (currentMovementValue != lastMovementValue)
    {
        // Only trigger on RISING edge (LOW->HIGH, 0->1)
        if (currentMovementValue == HIGH && lastMovementValue == LOW && isPrinting())
        {
            float movementMm = settingsManager.getMovementMmPerPulse();
            if (movementMm <= 0.0f)
            {
                movementMm = 2.88f;  // Default sensor spec
            }

            // Add pulse to motion sensor (Klipper-style)
            motionSensor.addSensorPulse(movementMm);
            actualFilamentMM += movementMm;
            movementPulseCount++;

            // Removed per-pulse logging - way too verbose, causes heap exhaustion
            // Pulse count is shown in periodic Flow log instead
        }

        lastMovementValue = currentMovementValue;
        lastChangeTime    = currentTime;
    }

    // Only run jam detection when actively printing with valid telemetry
    // This prevents false "jammed" state when idle with stale telemetry data
    if (!currentlyPrinting || !expectedTelemetryAvailable)
    {
        // Reset jam state when not printing to clear any stale detection
        if (!currentlyPrinting)
        {
            filamentStopped = false;
            resumeGraceActive = false;
        }
        return;
    }

    // Get ratio-based detection thresholds
    float ratioThreshold = settingsManager.getDetectionRatioThreshold();
    if (ratioThreshold <= 0.0f || ratioThreshold > 1.0f)
    {
        ratioThreshold = 0.70f;  // Default 70% deficit threshold
    }

    float hardJamThresholdMm = settingsManager.getDetectionHardJamMm();
    if (hardJamThresholdMm <= 0.0f)
    {
        hardJamThresholdMm = 5.0f;  // Default 5mm
    }

    int softJamTimeMs = settingsManager.getDetectionSoftJamTimeMs();
    if (softJamTimeMs <= 0)
    {
        softJamTimeMs = 3000;  // Default 3 seconds
    }

    int hardJamTimeMs = settingsManager.getDetectionHardJamTimeMs();
    if (hardJamTimeMs <= 0)
    {
        hardJamTimeMs = 2000;  // Default 2 seconds
    }

    // Get grace period setting (handles SDCP look-ahead behavior)
    // Calculate deficit and ratio for UI/logging
    float expectedDistance = motionSensor.getExpectedDistance();
    float actualDistance = motionSensor.getSensorDistance();
    float deficit = expectedDistance - actualDistance;
    if (deficit < 0.0f) deficit = 0.0f;

    float passRatio = (expectedDistance > 0.0f) ? (actualDistance / expectedDistance) : 1.0f;
    if (passRatio < 0.0f)
    {
        passRatio = 0.0f;
    }

    float deficitRatioValue = (expectedDistance > 1.0f) ? (deficit / expectedDistance) : 0.0f;

    // Apply EWMA smoothing to deficit ratio for display (reduces transient spikes in UI)
    // Jam detection uses raw values with hysteresis, so smoothing doesn't affect safety
    // Alpha = 0.1 means 10% weight on new value, 90% on history
    const float RATIO_SMOOTHING_ALPHA = 0.1f;
    smoothedDeficitRatio = RATIO_SMOOTHING_ALPHA * deficitRatioValue + (1.0f - RATIO_SMOOTHING_ALPHA) * smoothedDeficitRatio;

    // Update metrics for UI/logging
    currentDeficitMm   = deficit;
    deficitThresholdMm = ratioThreshold * expectedDistance;  // Convert ratio to mm for UI
    deficitRatio       = deficitRatioValue;  // Raw value (internal use only)

    if (resumeGraceActive)
    {
        bool pulsesSeen = (movementPulseCount > resumeGracePulseBaseline);
        bool actualMoved = (actualFilamentMM - resumeGraceActualBaseline) >= RESUME_GRACE_MIN_MOVEMENT_MM;
        bool expectedBuilt = (expectedDistance >= RESUME_GRACE_MIN_MOVEMENT_MM);
        if (pulsesSeen || actualMoved || expectedBuilt)
        {
            resumeGraceActive = false;
            if (debugFlow)
            {
                logger.log("Post-resume grace cleared; jam detection re-enabled");
            }
        }
    }

    unsigned long gracePeriod = settingsManager.getDetectionGracePeriodMs();
    bool baseGraceActive = (gracePeriod > 0) && motionSensor.isWithinGracePeriod(gracePeriod);
    bool withinGrace = baseGraceActive || resumeGraceActive;

    unsigned long elapsedMs;
    if (lastJamEvalMs == 0)
    {
        elapsedMs = EXPECTED_FILAMENT_SAMPLE_MS;
    }
    else
    {
        elapsedMs = currentTime - lastJamEvalMs;
        if (elapsedMs > EXPECTED_FILAMENT_SAMPLE_MS)
        {
            elapsedMs = EXPECTED_FILAMENT_SAMPLE_MS;
        }
    }
    if (elapsedMs == 0)
    {
        elapsedMs = 1;
    }
    lastJamEvalMs = currentTime;

    bool newPulseSinceLastEval = (movementPulseCount != lastHardJamPulseCount);
    lastHardJamPulseCount      = movementPulseCount;

    const float HARD_PASS_THRESHOLD = 0.10f;
    const float HARD_RECOVERY_RATIO = 0.35f;
    float       minHardWindowMm     = hardJamThresholdMm;
    if (minHardWindowMm < 1.0f)
    {
        minHardWindowMm = 1.0f;
    }
    const float MIN_SOFT_WINDOW_MM  = 1.0f;
    const float MIN_SOFT_DEFICIT_MM = 0.25f;

    bool hardCondition = (expectedDistance >= minHardWindowMm) &&
                         (passRatio < HARD_PASS_THRESHOLD);
    if (hardCondition)
    {
        hardJamAccumulatedMs += elapsedMs;
        if (hardJamAccumulatedMs > (unsigned long)hardJamTimeMs)
        {
            hardJamAccumulatedMs = hardJamTimeMs;
        }
    }
    else if (newPulseSinceLastEval || passRatio >= HARD_RECOVERY_RATIO ||
             expectedDistance < (minHardWindowMm * 0.5f))
    {
        hardJamAccumulatedMs = 0;
    }

    bool softCondition = (expectedDistance >= MIN_SOFT_WINDOW_MM) &&
                         (deficit >= MIN_SOFT_DEFICIT_MM) &&
                         (passRatio < ratioThreshold);
    if (softCondition)
    {
        softJamAccumulatedMs += elapsedMs;
        if (softJamAccumulatedMs > (unsigned long)softJamTimeMs)
        {
            softJamAccumulatedMs = softJamTimeMs;
        }
    }
    else if (passRatio >= ratioThreshold * 0.85f || newPulseSinceLastEval)
    {
        softJamAccumulatedMs = 0;
    }

    if (hardJamTimeMs > 0)
    {
        hardJamPercent = (100.0f * (float)hardJamAccumulatedMs) / (float)hardJamTimeMs;
        if (hardJamPercent > 100.0f) hardJamPercent = 100.0f;
    }
    else
    {
        hardJamPercent = 0.0f;
    }

    if (softJamTimeMs > 0)
    {
        softJamPercent = (100.0f * (float)softJamAccumulatedMs) / (float)softJamTimeMs;
        if (softJamPercent > 100.0f) softJamPercent = 100.0f;
    }
    else
    {
        softJamPercent = 0.0f;
    }

    bool hardJamTriggered = false;
    bool softJamTriggered = false;
    bool jammed = false;

    if (withinGrace)
    {
        hardJamAccumulatedMs = 0;
        softJamAccumulatedMs = 0;
        hardJamPercent       = 0.0f;
        softJamPercent       = 0.0f;
        jammed               = false;
    }
    else
    {
        hardJamTriggered = (hardJamTimeMs > 0) &&
                           (hardJamAccumulatedMs >= (unsigned long)hardJamTimeMs);
        softJamTriggered = (softJamTimeMs > 0) &&
                           (softJamAccumulatedMs >= (unsigned long)softJamTimeMs);
        jammed = hardJamTriggered || softJamTriggered;
    }

    // Periodic logging with BOTH windowed and cumulative values + memory monitoring
    if (debugFlow && currentlyPrinting && (currentTime - lastFlowLogMs) >= EXPECTED_FILAMENT_SAMPLE_MS)
    {
        lastFlowLogMs = currentTime;

        // Show windowed values (for tracking algo) AND cumulative values (for debugging)
        float windowedExpected = motionSensor.getExpectedDistance();
        float windowedSensor = motionSensor.getSensorDistance();
        float cumulativeSensor = actualFilamentMM;  // Cumulative sensor total
        uint32_t freeHeap = ESP.getFreeHeap();      // Monitor memory

        logger.logf(
            "Flow: win_exp=%.2f win_sns=%.2f deficit=%.2f | cumul=%.2f pulses=%lu | thr=%.2f ratio=%.2f jam=%d hard=%.2f soft=%.2f pass=%.2f heap=%lu",
            windowedExpected, windowedSensor, currentDeficitMm,
            cumulativeSensor, movementPulseCount,
            deficitThresholdMm, smoothedDeficitRatio, jammed ? 1 : 0,
            hardJamPercent, softJamPercent, passRatio, freeHeap);
    }

    if (debugFlow && (hardCondition || softCondition ||
                      hardJamAccumulatedMs > 0 || softJamAccumulatedMs > 0) &&
        (currentTime - lastJamDebugMs) >= JAM_DEBUG_INTERVAL_MS)
    {
        lastJamDebugMs = currentTime;
        logger.logf(
            "Jam eval: hardCond=%d softCond=%d hardMs=%lu/%d softMs=%lu/%d pass=%.2f win=%.2f sns=%.2f pulses=%lu",
            hardCondition ? 1 : 0, softCondition ? 1 : 0,
            hardJamAccumulatedMs, hardJamTimeMs,
            softJamAccumulatedMs, softJamTimeMs,
            passRatio, expectedDistance, actualDistance, movementPulseCount);
    }

    if (summaryFlow && currentlyPrinting && !debugFlow && (currentTime - lastSummaryLogMs) >= 1000)
    {
        lastSummaryLogMs = currentTime;
        logger.logf("Flow summary: expected=%.2fmm sensor=%.2fmm deficit=%.2fmm "
                    "threshold=%.2fmm ratio=%.2f hard=%.2f soft=%.2f pass=%.2f pulses=%lu",
                    motionSensor.getExpectedDistance(), motionSensor.getSensorDistance(),
                    currentDeficitMm, deficitThresholdMm, smoothedDeficitRatio,
                    hardJamPercent, softJamPercent, passRatio, movementPulseCount);
    }

    // Jam state change detection and logging
    if (jammed && !filamentStopped)
    {
        logger.logf("Filament jam detected! Expected %.2fmm, sensor %.2fmm, deficit %.2fmm ratio=%.2f (thr=%.2f)",
                    expectedDistance, actualDistance,
                    deficit, deficitRatioValue, ratioThreshold);
    }
    else if (!jammed && filamentStopped)
    {
        // Keep jam latched until pause/resume cycle completes
        if (!jamPauseRequested && !trackingFrozen)
        {
            logger.log("Filament flow resumed");
            filamentStopped = false;
        }
    }

    // Update jam state (unless latched by pause request)
    if (!jamPauseRequested && !trackingFrozen)
    {
        filamentStopped = jammed;
    }
}

bool ElegooCC::shouldPausePrint(unsigned long currentTime)
{
    if (!settingsManager.getEnabled())
    {
        return false;
    }

    if (filamentRunout && !settingsManager.getPauseOnRunout())
    {
        // if pause on runout is disabled, and filament ran out, skip checking everything else
        // this should let the carbon take care of itself
        return false;
    }

    bool pauseCondition = filamentRunout || filamentStopped;

    bool           sdcpLoss      = false;
    unsigned long  lastSuccessMs = lastSuccessfulTelemetryMs;
    int            lossBehavior  = settingsManager.getSdcpLossBehavior();
    if (webSocket.isConnected() && isPrinting() && lastSuccessMs > 0 &&
        (currentTime - lastSuccessMs) > SDCP_LOSS_TIMEOUT_MS)
    {
        sdcpLoss = true;
    }

    if (sdcpLoss)
    {
        if (lossBehavior == 1)
        {
            pauseCondition = true;
        }
        else if (lossBehavior == 2)
        {
            pauseCondition = false;
        }
    }

    if (currentTime - startedAt < settingsManager.getStartPrintTimeout() ||
        !webSocket.isConnected() || waitingForAck || !isPrinting() ||
        !pauseCondition ||
        (lastPauseRequestMs != 0 && (currentTime - lastPauseRequestMs) < PAUSE_REARM_DELAY_MS))
    {
        return false;
    }

    // log why we paused...
    logger.logf("Pause condition: %d", pauseCondition);
    logger.logf("Filament runout: %d", filamentRunout);
    logger.logf("Filament runout pause enabled: %d", settingsManager.getPauseOnRunout());
    logger.logf("Filament stopped: %d", filamentStopped);
    logger.logf("Time since print start %d", currentTime - startedAt);
    logger.logf("Is Machine status printing?: %d", hasMachineStatus(SDCP_MACHINE_STATUS_PRINTING));
    logger.logf("Print status: %d", printStatus);
    if (settingsManager.getVerboseLogging())
    {
        logger.logf("Flow state: expected=%.2fmm actual=%.2fmm deficit=%.2fmm "
                    "threshold=%.2fmm ratio=%.2f pulses=%lu",
                    expectedFilamentMM, actualFilamentMM, currentDeficitMm,
                    deficitThresholdMm, deficitRatio, movementPulseCount);
    }

    return true;
}

bool ElegooCC::isPrinting()
{
    return printStatus == SDCP_PRINT_STATUS_PRINTING &&
           hasMachineStatus(SDCP_MACHINE_STATUS_PRINTING);
}

bool ElegooCC::isPrintJobActive()
{
    // Return true for any state that indicates the printer is actively working
    // (not idle, not stopped, not completed)
    return printStatus != SDCP_PRINT_STATUS_IDLE &&
           printStatus != SDCP_PRINT_STATUS_STOPED &&
           printStatus != SDCP_PRINT_STATUS_COMPLETE;
}

// Helper methods for machine status bitmask
bool ElegooCC::hasMachineStatus(sdcp_machine_status_t status)
{
    return (machineStatusMask & (1 << status)) != 0;
}

void ElegooCC::setMachineStatuses(const int *statusArray, int arraySize)
{
    machineStatusMask = 0;  // Clear all statuses first
    for (int i = 0; i < arraySize; i++)
    {
        if (statusArray[i] >= 0 && statusArray[i] <= 4)
        {  // Validate range
            machineStatusMask |= (1 << statusArray[i]);
        }
    }
}

// Get current printer information
printer_info_t ElegooCC::getCurrentInformation()
{
    printer_info_t info;

    info.filamentStopped      = filamentStopped;
    info.filamentRunout       = filamentRunout;
    info.mainboardID          = mainboardID;
    info.printStatus          = printStatus;
    info.isPrinting           = isPrinting();
    info.currentLayer         = currentLayer;
    info.totalLayer           = totalLayer;
    info.progress             = progress;
    info.currentTicks         = currentTicks;
    info.totalTicks           = totalTicks;
    info.PrintSpeedPct        = PrintSpeedPct;
    info.isWebsocketConnected = webSocket.isConnected();
    info.currentZ             = currentZ;
    info.waitingForAck        = waitingForAck;
    info.expectedFilamentMM   = expectedFilamentMM;
    info.actualFilamentMM     = actualFilamentMM;
    info.lastExpectedDeltaMM  = lastExpectedDeltaMM;
    info.telemetryAvailable   = telemetryAvailableLastStatus;
    // Expose deficit metrics for UI/debugging (using smoothed ratio for cleaner display)
    info.currentDeficitMm     = currentDeficitMm;
    info.deficitThresholdMm   = deficitThresholdMm;
    info.deficitRatio         = smoothedDeficitRatio;  // Smoothed for UI display
    info.hardJamPercent       = hardJamPercent;
    info.softJamPercent       = softJamPercent;
    info.movementPulseCount   = movementPulseCount;

    return info;
}

bool ElegooCC::discoverPrinterIP(String &outIp, unsigned long timeoutMs)
{
    WiFiUDP udp;
    if (!udp.begin(SDCP_DISCOVERY_PORT))
    {
        logger.log("Failed to open UDP socket for discovery");
        return false;
    }

    // Use subnet-based broadcast rather than 255.255.255.255 to be friendlier
    // to routers that filter global broadcast.
    IPAddress localIp   = WiFi.localIP();
    IPAddress subnet    = WiFi.subnetMask();
    IPAddress broadcastIp((localIp[0] & subnet[0]) | ~subnet[0],
                          (localIp[1] & subnet[1]) | ~subnet[1],
                          (localIp[2] & subnet[2]) | ~subnet[2],
                          (localIp[3] & subnet[3]) | ~subnet[3]);

    logger.logf("Sending SDCP discovery probe to %s", broadcastIp.toString().c_str());

    udp.beginPacket(broadcastIp, SDCP_DISCOVERY_PORT);
    udp.write(reinterpret_cast<const uint8_t *>("M99999"), 6);
    udp.endPacket();

    unsigned long start = millis();
    while ((millis() - start) < timeoutMs)
    {
        int packetSize = udp.parsePacket();
        if (packetSize > 0)
        {
            IPAddress remoteIp = udp.remoteIP();
            if (remoteIp)
            {
                // Optional: read and log the payload for debugging
                char buffer[128];
                int  len = udp.read(buffer, sizeof(buffer) - 1);
                if (len > 0)
                {
                    buffer[len] = '\0';
                    logger.logf("Discovery reply from %s: %s", remoteIp.toString().c_str(),
                                buffer);
                }
                else
                {
                    logger.logf("Discovery reply from %s (no payload)",
                                remoteIp.toString().c_str());
                }

                outIp = remoteIp.toString();
                udp.stop();
                return true;
            }
        }
        delay(10);
    }

    udp.stop();
    return false;
}
