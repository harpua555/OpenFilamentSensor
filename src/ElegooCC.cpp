#include "ElegooCC.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "FilamentMotionSensor.h"
#include "Logger.h"
#include "SDCPProtocol.h"
#include "SettingsManager.h"

#include <vector>

// Define and initialize the static pulse counter
volatile unsigned long ElegooCC::isrPulseCounter = 0;

#define ACK_TIMEOUT_MS SDCPTiming::ACK_TIMEOUT_MS
constexpr float        DEFAULT_FILAMENT_DEFICIT_THRESHOLD_MM = SDCPDefaults::FILAMENT_DEFICIT_THRESHOLD_MM;
constexpr unsigned int EXPECTED_FILAMENT_SAMPLE_MS           = SDCPTiming::EXPECTED_FILAMENT_SAMPLE_MS;  // Log max once per second to prevent heap exhaustion
constexpr unsigned int EXPECTED_FILAMENT_STALE_MS            = SDCPTiming::EXPECTED_FILAMENT_STALE_MS;
constexpr unsigned int SDCP_LOSS_TIMEOUT_MS                  = SDCPTiming::SDCP_LOSS_TIMEOUT_MS;
constexpr unsigned int PAUSE_REARM_DELAY_MS                  = SDCPTiming::PAUSE_REARM_DELAY_MS;
static const char*     TOTAL_EXTRUSION_HEX_KEY       = SDCPKeys::TOTAL_EXTRUSION_HEX;
static const char*     CURRENT_EXTRUSION_HEX_KEY     = SDCPKeys::CURRENT_EXTRUSION_HEX;
static const uint16_t  SDCP_DISCOVERY_PORT = 3000;

namespace
{
JamConfig buildJamConfigFromSettings()
{
    JamConfig config;
    config.ratioThreshold = settingsManager.getDetectionRatioThreshold();
    if (config.ratioThreshold <= 0.0f || config.ratioThreshold > 1.0f)
    {
        config.ratioThreshold = 0.70f;
    }

    config.hardJamMm = settingsManager.getDetectionHardJamMm();
    if (config.hardJamMm <= 0.0f)
    {
        config.hardJamMm = 5.0f;
    }

    config.softJamTimeMs = settingsManager.getDetectionSoftJamTimeMs();
    if (config.softJamTimeMs <= 0)
    {
        config.softJamTimeMs = 3000;
    }

    config.hardJamTimeMs = settingsManager.getDetectionHardJamTimeMs();
    if (config.hardJamTimeMs <= 0)
    {
        config.hardJamTimeMs = 2000;
    }

    config.graceTimeMs    = settingsManager.getDetectionGracePeriodMs();
    config.detectionMode   = static_cast<DetectionMode>(settingsManager.getDetectionMode());
    return config;
}
}  // namespace

// External function to get current time (from main.cpp)
extern unsigned long getTime();

ElegooCC &ElegooCC::getInstance()
{
    static ElegooCC instance;
    return instance;
}

ElegooCC::ElegooCC()
{
    startedAt = 0;  // Initialize to prevent invalid grace periods
    // Interrupt-driven pulse counter initialization
    lastIsrPulseCount = 0;
    // Legacy pin tracking (used only when tracking is frozen after jam pause)
    lastMovementValue = -1;  // Initialize to invalid value
    lastChangeTime    = 0;
    mainboardID       = "";
    taskId            = "";
    filename          = "";
    printStatus       = SDCP_PRINT_STATUS_IDLE;
    machineStatusMask = 0;
    currentLayer      = 0;
    totalLayer        = 0;
    progress          = 0;
    currentTicks      = 0;
    totalTicks        = 0;
    PrintSpeedPct     = 0;
    filamentStopped   = false;
    filamentRunout    = false;
    runoutPausePending        = false;
    runoutPauseCommanded      = false;
    runoutPauseRemainingMm    = 0.0f;
    runoutPauseDelayMm        = DEFAULT_RUNOUT_PAUSE_DELAY_MM;
    runoutPauseStartExpectedMm = 0.0f;
    transport.lastPing            = 0;
    transport.waitingForAck       = false;
    transport.pendingAckCommand   = -1;
    transport.pendingAckRequestId = "";
    transport.ackWaitStartTime    = 0;
    transport.lastStatusRequestMs = 0;
    expectedFilamentMM            = 0;
    actualFilamentMM              = 0;
    lastExpectedDeltaMM           = 0;
    expectedTelemetryAvailable    = false;
    lastSuccessfulTelemetryMs     = 0;
    lastTelemetryReceiveMs        = 0;
    lastStatusReceiveMs           = 0;
    telemetryAvailableLastStatus  = false;
    movementPulseCount            = 0;
    lastFlowLogMs                 = 0;
    lastSummaryLogMs              = 0;
    lastPinDebugLogMs             = 0;
    lastLoggedExpected            = -1.0f;
    lastLoggedActual              = -1.0f;
    lastLoggedDeficit             = -1.0f;
    lastLoggedPrintStatus         = -1;
    lastLoggedLayer               = -1;
    lastLoggedTotalLayer          = -1;
    newPrintDetected              = false;
    trackingFrozen                = false;
    hasBeenPaused                 = false;
    motionSensor.reset();
    pauseTriggeredByRunout        = false;
    lastPauseRequestMs = 0;
    lastPrintEndMs     = 0;
    lastJamDetectorUpdateMs = 0;
    cacheLock = portMUX_INITIALIZER_UNLOCKED;

    // event handler
    transport.webSocket.onEvent([this](WStype_t type, uint8_t *payload, size_t length)
                                { this->webSocketEvent(type, payload, length); });
}

void ElegooCC::setup()
{
    // Initialize settings and config caches
    refreshCaches();

    // Set up GPIO interrupt for pulse detection on MOVEMENT_SENSOR_PIN
    // Rising edge trigger: counts each time sensor goes LOW→HIGH
    // IRAM_ATTR requirement handled by Arduino framework for static method
    pinMode(MOVEMENT_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(MOVEMENT_SENSOR_PIN),
                    ElegooCC::pulseCounterISR,
                    RISING);
    logger.logf("Pulse detection via GPIO%d interrupt enabled", MOVEMENT_SENSOR_PIN);

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
            transport.waitingForAck       = false;
            transport.pendingAckCommand   = -1;
            transport.pendingAckRequestId = "";
            transport.ackWaitStartTime    = 0;
            break;
        case WStype_CONNECTED:
            logger.log("Connected to Carbon Centauri");
            sendCommand(SDCP_COMMAND_STATUS);

            break;
        case WStype_TEXT:
        {
            messageDoc.clear();
            DeserializationError error = deserializeJson(messageDoc, payload, length);

            if (error)
            {
                logger.logf("JSON parsing failed: %s (payload size: %zu)", error.c_str(), length);
                return;
            }

            // Check if this is a command acknowledgment response
            if (messageDoc.containsKey("Id") && messageDoc.containsKey("Data"))
            {
                handleCommandResponse(messageDoc);
            }
            // Check if this is a status response
            else if (messageDoc.containsKey("Status"))
            {
                handleStatus(messageDoc);
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

        // Only log acknowledgments for commands that can ack
        if (transport.waitingForAck && cmd == transport.pendingAckCommand &&
            requestId == transport.pendingAckRequestId)
        {
            logger.logf("Received acknowledgment for command %d (Ack: %d)", cmd, ack);
            transport.waitingForAck       = false;
            transport.pendingAckCommand   = -1;
            transport.pendingAckRequestId = "";
            transport.ackWaitStartTime    = 0;
        }

        // Store mainboard ID if we don't have it yet
        //if (mainboardID.isEmpty() && !mainboardId.isEmpty())
        //{
        //    mainboardID = mainboardId;
        //    logger.logf("Stored MainboardID: %s", mainboardID.c_str());
        //}
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
        sdcp_print_status_t previousStatus = printStatus;

        // Any time we receive a well-formed PrintInfo block, treat SDCP
        // telemetry as available at the connection level
        telemetryAvailableLastStatus = true;
        lastSuccessfulTelemetryMs    = statusTimestamp;

        if (newStatus != printStatus)
        {
            // Update paused state tracking
            if (newStatus == SDCP_PRINT_STATUS_PAUSED || newStatus == SDCP_PRINT_STATUS_PAUSING)
            {
                hasBeenPaused = true;
            }
            else if (newStatus == SDCP_PRINT_STATUS_STOPED ||
                     newStatus == SDCP_PRINT_STATUS_COMPLETE ||
                     newStatus == SDCP_PRINT_STATUS_IDLE)
            {
                hasBeenPaused = false;
            }

            bool wasPrinting   = (printStatus == SDCP_PRINT_STATUS_PRINTING);
            bool isPrintingNow = (newStatus == SDCP_PRINT_STATUS_PRINTING);

            if (isPrintingNow)
            {
                // Transition into PRINTING.
                // If we previously issued a jam-driven pause OR we have seen a paused state,
                // treat the next PRINTING state as a resume regardless of any intermediate
                // statuses (e.g. HEATING or other transitional codes).
                if (jamDetector.isPauseRequested() || hasBeenPaused)
                {
                    logger.log("Print status changed to printing (resume)");
                    trackingFrozen = false;
                    // On resume, reset the motion sensor so jam detection starts fresh
                    motionSensor.reset();
                    jamDetector.onResume(statusTimestamp, movementPulseCount, actualFilamentMM);
                    filamentStopped = false;
                    if (settingsManager.getVerboseLogging())
                    {
                        logger.log("Motion sensor reset (resume after pause)");
                        logger.log("Post-resume grace active until movement detected");
                    }
                }
                else if (newPrintDetected)
                {
                    // New print detected via TaskId - initialize tracking
                    logger.log("Print status changed to printing");
                    startedAt = statusTimestamp;
                    resetFilamentTracking();

                    // Log active settings for this print (excluding network config)
                    logger.logf(
                        "Print settings: pulse=%.2fmm grace=%dms ratio_thr=%.2f hard_jam=%.1fmm soft_time=%dms hard_time=%dms",
                        settingsManager.getMovementMmPerPulse(),
                        settingsManager.getDetectionGracePeriodMs(),
                        settingsManager.getDetectionRatioThreshold(),
                        settingsManager.getDetectionHardJamMm(),
                        settingsManager.getDetectionSoftJamTimeMs(),
                        settingsManager.getDetectionHardJamTimeMs());

                    newPrintDetected = false;
                }
                else if (!hasBeenPaused && !jamDetector.isPauseRequested() && startedAt == 0)
                {
                    // Ensure grace period starts even if TaskId arrives late.
                    logger.log("Print status changed to printing (no TaskId yet)");
                    startedAt = statusTimestamp;
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
                    if (jamDetector.isPauseRequested())
                    {
                        trackingFrozen = true;
                        logger.log("Freezing filament tracking while paused after jam");
                    }
                }
                else
                {
                    // Print has ended (stopped/completed/etc). Log a summary and
                    // fully reset tracking for the next job.
                    float finalDeficit = expectedFilamentMM - actualFilamentMM;
                    if (finalDeficit < 0.0f) finalDeficit = 0.0f;

                    logger.logf(
                        "Print summary: status=%d progress=%d layer=%d/%d ticks=%d/%d "
                        "expected=%.2fmm actual=%.2fmm deficit=%.2fmm pulses=%lu",
                        (int) newStatus, progress, currentLayer, totalLayer, currentTicks,
                        totalTicks, expectedFilamentMM, actualFilamentMM, finalDeficit,
                        movementPulseCount);

                    // Auto-calibration logic...
                    if (settingsManager.getAutoCalibrateSensor() && movementPulseCount > 0 &&
                        expectedFilamentMM > 50.0f)
                    {
                        // Minimum thresholds: 50+ pulses and 50+mm expected for reliable calibration
                        if (movementPulseCount >= 50)
                        {
                            // Health check: only calibrate if flow quality was good (>90%)
                            float flowQuality = actualFilamentMM / expectedFilamentMM;

                            if (flowQuality >= 0.90f)
                            {
                                float calculatedMmPerPulse = expectedFilamentMM / (float) movementPulseCount;

                                // Sanity check: should be between 2.5-3.5mm (reasonable range for SFS 2.0)
                                if (calculatedMmPerPulse >= 2.5f && calculatedMmPerPulse <= 3.5f)
                                {
                                    float oldValue = settingsManager.getMovementMmPerPulse();
                                    settingsManager.setMovementMmPerPulse(calculatedMmPerPulse);

                                    // Disable auto-calibration after successful calibration
                                    settingsManager.setAutoCalibrateSensor(false);
                                    settingsManager.save();
                                    refreshCaches();

                                    logger.logf(
                                        "Auto-calibration: Updated mm_per_pulse from %.3f to %.3f "
                                        "(based on %.2fmm expected / %lu pulses, flow quality %.1f%%)",
                                        oldValue, calculatedMmPerPulse, expectedFilamentMM,
                                        movementPulseCount, flowQuality * 100.0f);
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
                                    "Auto-calibration: Skipped - flow quality %.1f%% < 90%% threshold "
                                    "(print may have had jams/under-extrusion)",
                                    flowQuality * 100.0f);
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
            else if ((printStatus == SDCP_PRINT_STATUS_PAUSED || printStatus == SDCP_PRINT_STATUS_PAUSING) &&
                     (newStatus == SDCP_PRINT_STATUS_STOPED || newStatus == SDCP_PRINT_STATUS_COMPLETE || newStatus == SDCP_PRINT_STATUS_IDLE))
            {
                 logger.log("Print stopped from paused state, resetting filament tracking");
                 resetFilamentTracking();
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

        // Extract TaskId - any change indicates a new print job
        String newTaskId = "";
        if (printInfo.containsKey("TaskId") && !printInfo["TaskId"].isNull())
        {
            newTaskId = printInfo["TaskId"].as<String>();
        }

        if (newTaskId != taskId)
        {
            if (!newTaskId.isEmpty())
            {
                newPrintDetected = true;
                if (printStatus == SDCP_PRINT_STATUS_PRINTING && startedAt == 0)
                {
                    // TaskId arrived after PRINTING transition; arm grace period.
                    startedAt = statusTimestamp;
                }
                if (settingsManager.getVerboseLogging())
                {
                    logger.logf("New Print detected via TaskId: %s", newTaskId.c_str());
                }
            }
            taskId = newTaskId;
        }

        if (printInfo.containsKey("Filename") && !printInfo["Filename"].isNull())
        {
            String newFilename = printInfo["Filename"].as<String>();
            if (!newFilename.isEmpty())
            {
                filename = newFilename;
            }
        }

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

    // Store mainboard ID if we don't have it yet
    //if (mainboardID.isEmpty() && !mainboardId.isEmpty())
    //{
    //    mainboardID = mainboardId;
    //   logger.logf("Stored MainboardID: %s", mainboardID.c_str());
    //}
}

void ElegooCC::resetFilamentTracking(bool resetGrace)
{
    unsigned long currentTime = millis();

    lastMovementValue          = -1;
    lastChangeTime             = currentTime;
    actualFilamentMM           = 0;
    expectedFilamentMM         = 0;
    lastExpectedDeltaMM        = 0;
    expectedTelemetryAvailable = false;
    lastSuccessfulTelemetryMs  = 0;
    filamentStopped            = false;
    lastTelemetryReceiveMs     = 0;
    movementPulseCount         = 0;
    lastFlowLogMs              = 0;
    trackingFrozen             = false;
    resetRunoutPauseState();

    // Reset the motion sensor and jam detector
    motionSensor.reset();
    jamDetector.reset(currentTime);

    if (settingsManager.getVerboseLogging())
    {
        logger.log("Filament tracking reset - Mode: Windowed");
    }
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
                JamState jamState = jamDetector.getState();
                // Consolidated telemetry log with jam state info
                logger.logf("Debug: sdcp_exp=%.2fmm cumul_sns=%.2fmm pulses=%lu | win_exp=%.2f win_sns=%.2f deficit=%.2f | jam=%d hard=%.2f soft=%.2f pass=%.2f grace=%d heap=%lu",
                            expectedFilamentMM, actualFilamentMM, movementPulseCount,
                            windowedExpected, windowedSensor, currentDeficit,
                            jamState.jammed ? 1 : 0,
                            jamState.hardJamPercent, jamState.softJamPercent, jamState.passRatio,
                            jamState.graceActive ? 1 : 0, ESP.getFreeHeap());
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
    jamDetector.setPauseRequested();
    lastPauseRequestMs = millis();

    if (settingsManager.getSuppressPauseCommands())
    {
        logger.logf("Pause command suppressed (suppress_pause_commands enabled)");
        return;
    }
    if (!transport.webSocket.isConnected())
    {
        logger.logf("Pause command suppressed: printer websocket not connected");
        return;
    }
    
    if (pauseTriggeredByRunout)
    {
        runoutPauseCommanded = true;
    }

    logger.logf("Pause command sent to printer");
    sendCommand(SDCP_COMMAND_PAUSE_PRINT, true);
}

void ElegooCC::continuePrint()
{
    sendCommand(SDCP_COMMAND_CONTINUE_PRINT, true);
}

void ElegooCC::sendCommand(int command, bool waitForAck)
{
    if (!transport.webSocket.isConnected())
    {
        logger.logf("Can't send command, websocket not connected: %d", command);
        return;
    }

    // If this command requires an ack and we're already waiting for one, skip it
    if (waitForAck && transport.waitingForAck)
    {
        logger.logf("Skipping command %d - already waiting for ack from command %d", command,
                    transport.pendingAckCommand);
        return;
    }

    uuid.generate();
    String uuidStr = String(uuid.toCharArray());
    uuidStr.replace("-", "");  // RequestID doesn't want dashes

    // Get current timestamp
    unsigned long timestamp = getTime();

    // Reuse shared message document for outbound SDCP commands
    messageDoc.clear();
    if (!SDCPProtocol::buildCommandMessage(messageDoc,
                                           command,
                                           uuidStr,
                                           mainboardID,
                                           timestamp,
                                           static_cast<int>(printStatus),
                                           machineStatusMask))
    {
        logger.logf("Failed to build SDCP command %d: JSON document too small", command);
        return;
    }

    String jsonPayload;
    serializeJson(messageDoc, jsonPayload);

    // If this command requires an ack, set the tracking state
    if (waitForAck)
    {
        transport.waitingForAck       = true;
        transport.pendingAckCommand   = command;
        transport.pendingAckRequestId = uuidStr;
        transport.ackWaitStartTime    = millis();
        logger.logf("Waiting for acknowledgment for command %d with request ID %s", command,
                    uuidStr.c_str());
    }

    transport.webSocket.sendTXT(jsonPayload);
    if (command == SDCP_COMMAND_STATUS)
    {
        transport.lastStatusRequestMs = millis();
    }
}

void ElegooCC::refreshCaches()
{
    // Use a short critical section so cache refreshes invoked from other tasks stay consistent
    portENTER_CRITICAL(&cacheLock);
    refreshSettingsCache();
    refreshJamConfig();
    portEXIT_CRITICAL(&cacheLock);
}

void ElegooCC::refreshSettingsCache()
{
    cachedSettings.testRecordingMode = settingsManager.getTestRecordingMode();
    cachedSettings.verboseLogging = settingsManager.getVerboseLogging();
    cachedSettings.flowSummaryLogging = settingsManager.getFlowSummaryLogging();
    cachedSettings.pinDebugLogging = settingsManager.getPinDebugLogging();
    cachedSettings.motionMonitoringEnabled = settingsManager.getEnabled();
    cachedSettings.pulseReductionPercent = settingsManager.getPulseReductionPercent();
    cachedSettings.movementMmPerPulse = settingsManager.getMovementMmPerPulse();
}

void ElegooCC::refreshJamConfig()
{
    cachedJamConfig = buildJamConfigFromSettings();
}

void ElegooCC::reconnect()
{
    // Reconnect to the printer with the current IP from settings
    // Called when settings are updated (e.g., after auto-discovery)
    String configuredIp = settingsManager.getElegooIP();
    if (configuredIp.length() > 0)
    {
        connect();
    }
}

void ElegooCC::maybeRequestStatus(unsigned long currentTime)
{
    if (!transport.webSocket.isConnected())
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
        interval = STATUS_ACTIVE_INTERVAL_MS;
        if (jobActive)
        {
            lastPrintEndMs = 0;
        }
    }
    else
    {
        interval = STATUS_IDLE_INTERVAL_MS;
    }

    if (transport.lastStatusRequestMs == 0 ||
        currentTime - transport.lastStatusRequestMs >= interval)
    {
        sendCommand(SDCP_COMMAND_STATUS);
    }
}

void ElegooCC::connect()
{
    transport.ipAddress = settingsManager.getElegooIP();

    // Don't attempt connection if IP is empty or default placeholder
    if (transport.ipAddress.length() == 0 || transport.ipAddress == "1.1.1.1")
    {
        transport.connectionStartMs = 0;
        return;
    }

    if (transport.webSocket.isConnected())
    {
        transport.webSocket.disconnect();
    }
    transport.webSocket.setReconnectInterval(3000);
    logger.logf("Attempting connection to Elegoo CC @ %s", transport.ipAddress.c_str());
    transport.connectionStartMs = millis();
    transport.webSocket.begin(transport.ipAddress, CARBON_CENTAURI_PORT, "/websocket");
}

void ElegooCC::updateTransport(unsigned long currentTime)
{
    // Suspend WebSocket during discovery to prevent stalling the loop
    if (transport.blocked || discoveryState.active)
    {
        return;
    }

    // Skip WebSocket operations if no IP is configured or using default placeholder
    if (transport.ipAddress.length() == 0 || transport.ipAddress == "1.1.1.1")
    {
        return;
    }

    if (transport.webSocket.isConnected())
    {
        transport.connectionStartMs = 0;
        if (transport.waitingForAck &&
            (currentTime - transport.ackWaitStartTime) >= ACK_TIMEOUT_MS)
        {
            logger.logf("Acknowledgment timeout for command %d, resetting ack state",
                        transport.pendingAckCommand);
            transport.waitingForAck       = false;
            transport.pendingAckCommand   = -1;
            transport.pendingAckRequestId = "";
            transport.ackWaitStartTime    = 0;
        }
        else if (currentTime - transport.lastPing > 29900)
        {
            // Keepalive ping every ~30s
            transport.webSocket.sendTXT("ping");
            transport.lastPing = currentTime;
        }
     
        transport.webSocket.loop();
    }
    else
    {
        // Allow frequent loop() calls for a short window after connect() starts
        bool connectionInProgress = (transport.connectionStartMs != 0) &&
                                    ((currentTime - transport.connectionStartMs) < 10000);
        if (!connectionInProgress && transport.connectionStartMs != 0 &&
            (currentTime - transport.connectionStartMs) >= 10000)
        {
            transport.connectionStartMs = 0;  // Connection attempt timed out
        }

        // When disconnected, throttle loop() calls to avoid blocking the main loop
        // during TCP connection attempts. WebSocketsClient blocks for ~5s on unreachable IPs.
        static unsigned long lastDisconnectedLoopMs = 0;
        static bool initialized = false;

        // On first call, initialize timestamp to current time to prevent immediate block
        if (!initialized)
        {
            lastDisconnectedLoopMs = currentTime;
            initialized = true;
            if (!connectionInProgress)
            {
                return;  // Skip the blocking call on first iteration
            }
        }

        if (connectionInProgress)
        {
            transport.webSocket.loop();
            lastDisconnectedLoopMs = currentTime;
        }
        else if (currentTime - lastDisconnectedLoopMs >= 10000)
        {
            lastDisconnectedLoopMs = currentTime;
            transport.webSocket.loop();
        }
    }
}

void ElegooCC::loop()
{
    unsigned long currentTime = millis();

    updateTransport(currentTime);
    currentTime = millis();

    if (transport.blocked || discoveryState.active)
    {
        updateDiscovery(currentTime);
        return;
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
    updateDiscovery(currentTime);
}

bool ElegooCC::shouldApplyPulseReduction(float reductionPercent)
{
    static float accumulator = 0.0f;

    // 100% or higher: count all pulses (normal operation)
    if (reductionPercent >= 100.0f) {
        accumulator = 0.0f;  // Reset accumulator for consistency
        return true;
    }

    // 0% or lower: count no pulses (simulate complete blockage)
    if (reductionPercent <= 0.0f) {
        accumulator = 0.0f;  // Reset accumulator for consistency
        return false;
    }

    // Accumulator-based logic for fractional pulse counting
    accumulator += reductionPercent;
    if (accumulator >= 100.0f) {
        accumulator -= 100.0f;
        return true;  // Count this pulse
    }

    return false; // Skip this pulse
}

void ElegooCC::resetRunoutPauseState()
{
    runoutPausePending         = false;
    runoutPauseCommanded       = false;
    runoutPauseRemainingMm     = 0.0f;
    runoutPauseStartExpectedMm = expectedFilamentMM;
    pauseTriggeredByRunout     = false;
}

void ElegooCC::updateRunoutPauseCountdown()
{
    if (!filamentRunout)
    {
        resetRunoutPauseState();
        return;
    }

    if (!settingsManager.getPauseOnRunout())
    {
        runoutPausePending     = false;
        runoutPauseRemainingMm = 0.0f;
        pauseTriggeredByRunout = false;
        return;
    }

    if (!runoutPausePending)
    {
        runoutPausePending         = true;
        runoutPauseCommanded       = false;
        runoutPauseStartExpectedMm = expectedFilamentMM;
        runoutPauseRemainingMm     = runoutPauseDelayMm;
        logger.logf("Filament runout detected; delaying pause for %.1fmm of expected extrusion (start=%.2fmm)",
                    runoutPauseDelayMm, runoutPauseStartExpectedMm);
    }

    float consumed = expectedFilamentMM - runoutPauseStartExpectedMm;
    if (consumed < 0.0f)
    {
        consumed = 0.0f;
        runoutPauseStartExpectedMm = expectedFilamentMM;
    }

    runoutPauseRemainingMm = runoutPauseDelayMm - consumed;
    if (runoutPauseRemainingMm < 0.0f)
    {
        runoutPauseRemainingMm = 0.0f;
    }
}

bool ElegooCC::isRunoutPauseReady() const
{
    return filamentRunout && settingsManager.getPauseOnRunout() && runoutPausePending &&
           runoutPauseRemainingMm <= 0.0f;
}

void ElegooCC::checkFilamentRunout(unsigned long currentTime)
{
    // The signal output of the switch sensor is at low level when no filament is detected
    // Some boards/sensors may need inverted logic
    int pinValue = digitalRead(FILAMENT_RUNOUT_PIN);
#ifdef INVERT_RUNOUT_PIN
    pinValue = !pinValue;  // Invert the logic if flag is set
#endif
    bool newFilamentRunout = (pinValue == LOW);

    if (newFilamentRunout != filamentRunout)
    {
        logger.log(newFilamentRunout ? "Filament has run out" : "Filament has been detected");
        if (!newFilamentRunout)
        {
            resetRunoutPauseState();
        }
    }
    filamentRunout = newFilamentRunout;
    updateRunoutPauseCountdown();
}

void ElegooCC::checkFilamentMovement(unsigned long currentTime)
{
    // ============================================================================
    // LOOP TIMING DIAGNOSTIC
    // Monitor main loop performance - log warning if loop stalls exceed 50ms
    // This helps detect WiFi/WebSocket/JSON processing that could miss pulses.
    // ============================================================================
    static unsigned long lastLoopTime = 0;
    static bool wasInDiscovery = false;

    // Reset lastLoopTime after discovery to avoid false stall warnings
    // (discovery blocks the main loop for 7-14 seconds which is expected)
    if (wasInDiscovery)
    {
        lastLoopTime = currentTime;
        wasInDiscovery = false;
    }
    wasInDiscovery = discoveryState.active;

    if (lastLoopTime > 0)
    {
        unsigned long loopDelta = currentTime - lastLoopTime;
        if (loopDelta > 50 && cachedSettings.verboseLogging)
        {
            static unsigned long lastLoopWarningMs = 0;
            if ((currentTime - lastLoopWarningMs) >= 5000)  // Log max once per 5 seconds
            {
                lastLoopWarningMs = currentTime;
                logger.logf("LOOP_STALL: Main loop took %lums (>50ms may miss pulses)", loopDelta);
            }
        }
    }
    lastLoopTime = currentTime;

    // ============================================================================
    // TRACKING FROZEN STATE
    // When tracking is frozen (printer paused after a jam), we still track pin
    // state changes but do NOT count pulses. This preserves the post-jam display.
    // ============================================================================
    if (trackingFrozen)
    {
        // Sync ISR counter to discard pulses accumulated while frozen
        lastIsrPulseCount = ElegooCC::isrPulseCounter;

        // When tracking is frozen (printer paused after a jam), just track pin changes
        int currentMovementValue = digitalRead(MOVEMENT_SENSOR_PIN);
#ifdef INVERT_MOVEMENT_PIN
        currentMovementValue = !currentMovementValue;  // Invert the logic if flag is set
#endif
        if (currentMovementValue != lastMovementValue)
        {
            lastMovementValue = currentMovementValue;
            lastChangeTime    = currentTime;
        }
        return;
    }

    // Test recording mode enables verbose flow logging for CSV extraction
    // Use cached settings to avoid repeated getter calls in hot path (~1000 Hz)
    bool testRecordingMode = cachedSettings.testRecordingMode;
    bool debugFlow         = cachedSettings.verboseLogging || testRecordingMode;
    bool summaryFlow       = cachedSettings.flowSummaryLogging;
    bool currentlyPrinting = isPrinting();

    // ============================================================================
    // PULSE COUNTING POLICY (with INTERRUPT-DRIVEN DETECTION)
    // Count pulses during any active print job (heating, leveling, printing, etc).
    // Uses isPrintJobActive() which returns true when:
    //   printStatus != IDLE && printStatus != STOPPED && printStatus != COMPLETE
    //
    // This ensures pulses are counted from print start (step 4) until print end
    // (step 6), matching the lifecycle managed by candidate detection logic.
    //
    // The trackingFrozen gate (handled above) stops counting during jam-paused state.
    // ============================================================================
    bool shouldCountPulses = isPrintJobActive();

    // ============================================================================
    // READ ACCUMULATED PULSES FROM ISR COUNTER
    // ============================================================================
    unsigned long currentPulseCount = ElegooCC::isrPulseCounter;
    unsigned long newPulses = currentPulseCount - lastIsrPulseCount;
    lastIsrPulseCount = currentPulseCount;

    // Process accumulated pulses
    if (newPulses > 0 && shouldCountPulses)
    {
        float movementMm = cachedSettings.movementMmPerPulse;
        if (movementMm <= 0.0f)
        {
            movementMm = 2.88f;  // Default sensor spec
        }

        // Process each pulse through reduction filter (for test recording mode)
        for (unsigned long i = 0; i < newPulses; i++)
        {
            // Apply pulse reduction filter for testing
            // Use cached settings to avoid repeated getter calls in hot path
            float reductionPercent = cachedSettings.pulseReductionPercent;
            if (!shouldApplyPulseReduction(reductionPercent))
            {
                // Skip this pulse due to reduction setting (test feature)
                continue;
            }

            // Add pulse to motion sensor (Klipper-style)
            motionSensor.addSensorPulse(movementMm);
            actualFilamentMM += movementMm;
            movementPulseCount++;

            // Pin debug logging for pulse detection
            if (settingsManager.getPinDebugLogging())
            {
                logger.log("pulse");
            }
        }

        lastChangeTime = currentTime;
    }

    // Pin debug logging (once per second) - BEFORE early return so it always runs
    // Shows RAW pin values (before any inversion)
    // Use cached settings to avoid repeated getter calls
    //bool pinDebug = cachedSettings.pinDebugLogging;
    //if (pinDebug && (currentTime - lastPinDebugLogMs) >= 1000)
    //{
    //    lastPinDebugLogMs = currentTime;
    //    int runoutPinValue = digitalRead(FILAMENT_RUNOUT_PIN);
    //    int movementPinValue = digitalRead(MOVEMENT_SENSOR_PIN);
    //    logger.logf("PIN: R%d=%d M%d=%d p=%lu",
    //                FILAMENT_RUNOUT_PIN, runoutPinValue,
    //                MOVEMENT_SENSOR_PIN, movementPinValue,
    //                movementPulseCount);
    //}

    // Only run jam detection when actively printing with valid telemetry
    if (!shouldCountPulses || !expectedTelemetryAvailable)
    {
        // Reset jam state when not printing to clear any stale detection
        if (!shouldCountPulses)
        {
            filamentStopped = false;
        }
        return;
    }

    if (!cachedSettings.motionMonitoringEnabled)
    {
        filamentStopped = false;
        return;
    }

    // Use cached jam config instead of rebuilding
    const JamConfig& jamConfig = cachedJamConfig;

    // Get windowed distances from motion sensor
    float expectedDistance = motionSensor.getExpectedDistance();
    float actualDistance = motionSensor.getSensorDistance();
    float windowedExpectedRate = 0.0f;
    float windowedActualRate = 0.0f;
    motionSensor.getWindowedRates(windowedExpectedRate, windowedActualRate);

    // Update jam detector and get current state
    // Throttle jamDetector.update() to 4Hz
    if ((currentTime - lastJamDetectorUpdateMs) >= JAM_DETECTOR_UPDATE_INTERVAL_MS)
    {
        lastJamDetectorUpdateMs = currentTime;
        
        // Update jam detector and get current state
        cachedJamState = jamDetector.update(
            expectedDistance, actualDistance, movementPulseCount,
            currentlyPrinting, expectedTelemetryAvailable,
            currentTime, startedAt, jamConfig,
            windowedExpectedRate, windowedActualRate
        );
        
        // Update filament stopped state (unless latched by pause/tracking freeze)
        if (!jamDetector.isPauseRequested() && !trackingFrozen)
        {
            filamentStopped = cachedJamState.jammed;
        }
    }
    
    // Use cached state for logging
    JamState jamState = cachedJamState;

    // Periodic consolidated logging with all telemetry data + memory monitoring
    if (debugFlow && currentlyPrinting && (currentTime - lastFlowLogMs) >= EXPECTED_FILAMENT_SAMPLE_MS)
    {
        lastFlowLogMs = currentTime;
        uint32_t freeHeap = ESP.getFreeHeap();

        logger.logf(
            "Debug: sdcp_exp=%.2fmm cumul_sns=%.2fmm pulses=%lu | win_exp=%.2f win_sns=%.2f deficit=%.2f | jam=%d hard=%.2f soft=%.2f pass=%.2f grace=%d heap=%lu",
            expectedFilamentMM, actualFilamentMM, movementPulseCount,
            expectedDistance, actualDistance, jamState.deficit,
            jamState.jammed ? 1 : 0,
            jamState.hardJamPercent, jamState.softJamPercent, jamState.passRatio,
            jamState.graceActive ? 1 : 0, freeHeap);
    }

    if (summaryFlow && currentlyPrinting && !debugFlow && (currentTime - lastSummaryLogMs) >= 1000)
    {
        lastSummaryLogMs = currentTime;
        logger.logf("Debug summary: expected=%.2fmm sensor=%.2fmm deficit=%.2fmm "
                    "ratio=%.2f hard=%.2f%% soft=%.2f%% pass=%.2f pulses=%lu",
                    expectedDistance, actualDistance, jamState.deficit,
                    jamState.deficit / (expectedDistance > 0.1f ? expectedDistance : 1.0f),
                    jamState.hardJamPercent, jamState.softJamPercent, jamState.passRatio,
                    movementPulseCount);
    }
}

bool ElegooCC::shouldPausePrint(unsigned long currentTime)
{
    pauseTriggeredByRunout = false;
    bool motionMonitoringEnabled = settingsManager.getEnabled();

    updateRunoutPauseCountdown();
    bool runoutPauseReady    = isRunoutPauseReady();
    bool pauseConditionRunout = runoutPauseReady;
    bool pauseConditionFlow  = motionMonitoringEnabled && filamentStopped;
    bool pauseCondition      = pauseConditionRunout || pauseConditionFlow;

    bool           sdcpLoss      = false;
    unsigned long  lastSuccessMs = lastSuccessfulTelemetryMs;
    int            lossBehavior  = settingsManager.getSdcpLossBehavior();
    if (transport.webSocket.isConnected() && isPrinting() && lastSuccessMs > 0 &&
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
            pauseCondition = pauseConditionRunout;
        }
    }

    if (currentTime - startedAt < settingsManager.getDetectionGracePeriodMs() ||
        !transport.webSocket.isConnected() || transport.waitingForAck || !isPrinting() ||
        !pauseCondition ||
        (lastPauseRequestMs != 0 && (currentTime - lastPauseRequestMs) < PAUSE_REARM_DELAY_MS))
    {
        return false;
    }

    pauseTriggeredByRunout = runoutPauseReady;

    if (runoutPauseReady && !runoutPauseCommanded)
    {
        logger.logf("Runout pause delay satisfied after %.2fmm expected (start=%.2fmm current=%.2fmm)",
                    runoutPauseDelayMm, runoutPauseStartExpectedMm, expectedFilamentMM);
    }

    // log why we paused...
    logger.logf("Pause condition: %d (runout_ready=%d flow=%d sdcp_loss=%d)",
                pauseCondition, pauseConditionRunout ? 1 : 0, pauseConditionFlow ? 1 : 0,
                sdcpLoss ? 1 : 0);
    logger.logf("Filament runout: %d", filamentRunout);
    logger.logf("Filament runout pause enabled: %d", settingsManager.getPauseOnRunout());
    logger.logf("Runout pause remaining: %.2f / %.2f", runoutPauseRemainingMm, runoutPauseDelayMm);
    logger.logf("Filament stopped: %d", filamentStopped);
    logger.logf("Time since print start %d", currentTime - startedAt);
    logger.logf("Is Machine status printing?: %d", hasMachineStatus(SDCP_MACHINE_STATUS_PRINTING));
    logger.logf("Print status: %d", printStatus);
    if (settingsManager.getVerboseLogging())
    {
        JamState jamState = jamDetector.getState();
        logger.logf("Flow state: expected=%.2fmm actual=%.2fmm deficit=%.2fmm "
                    "pass_ratio=%.2f pulses=%lu",
                    expectedFilamentMM, actualFilamentMM, jamState.deficit,
                    jamState.passRatio, movementPulseCount);
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
    JamState jamState = jamDetector.getState();
    bool motionMonitoringEnabled = settingsManager.getEnabled();
    if (!motionMonitoringEnabled)
    {
        jamState = JamState{};
    }

    info.filamentStopped      = motionMonitoringEnabled ? filamentStopped : false;
    info.filamentRunout       = filamentRunout;
    info.runoutPausePending   = filamentRunout && runoutPausePending && settingsManager.getPauseOnRunout();
    info.runoutPauseCommanded = runoutPauseCommanded;
    info.runoutPauseRemainingMm = runoutPauseRemainingMm;
    info.runoutPauseDelayMm   = runoutPauseDelayMm;
    info.mainboardID          = mainboardID;
    info.taskId               = taskId;
    info.filename             = filename;
    info.printStatus          = printStatus;
    info.isPrinting           = isPrinting();
    info.currentLayer         = currentLayer;
    info.totalLayer           = totalLayer;
    info.progress             = progress;
    info.currentTicks         = currentTicks;
    info.totalTicks           = totalTicks;
    info.PrintSpeedPct        = PrintSpeedPct;
    info.isWebsocketConnected = transport.webSocket.isConnected();
    info.currentZ             = currentZ;
    info.waitingForAck        = transport.waitingForAck;
    info.expectedFilamentMM   = expectedFilamentMM;
    info.actualFilamentMM     = actualFilamentMM;
    info.lastExpectedDeltaMM  = lastExpectedDeltaMM;
    info.telemetryAvailable   = telemetryAvailableLastStatus;
    // Expose deficit metrics for UI from jam detector
    info.currentDeficitMm     = jamState.deficit;
    info.deficitThresholdMm   = 0.0f;
    info.deficitRatio         = jamState.deficit / (motionSensor.getExpectedDistance() > 0.1f ? motionSensor.getExpectedDistance() : 1.0f);
    info.passRatio            = jamState.passRatio;
    info.hardJamPercent       = jamState.hardJamPercent;
    info.softJamPercent       = jamState.softJamPercent;
    info.graceActive          = jamState.graceActive;
    info.graceState           = static_cast<uint8_t>(jamState.graceState);
    info.expectedRateMmPerSec = jamState.expectedRateMmPerSec;
    info.actualRateMmPerSec   = jamState.actualRateMmPerSec;
    info.movementPulseCount   = movementPulseCount;

    return info;
}

bool ElegooCC::startDiscoveryAsync(unsigned long timeoutMs, DiscoveryCallback callback)
{
    if (discoveryState.active)
    {
        logger.log("Discovery already in progress");
        return false;
    }

    if (!discoveryState.udp.begin(SDCP_DISCOVERY_PORT))
    {
        logger.log("Failed to open UDP socket for discovery");
        return false;
    }

    // Small delay after binding to ensure socket is ready
    delay(10);

    // Broadcast discovery packet
    IPAddress localIp   = WiFi.localIP();
    IPAddress subnet    = WiFi.subnetMask();
    IPAddress broadcastIp((localIp[0] & subnet[0]) | ~subnet[0],
                          (localIp[1] & subnet[1]) | ~subnet[1],
                          (localIp[2] & subnet[2]) | ~subnet[2],
                          (localIp[3] & subnet[3]) | ~subnet[3]);

    logger.logf("Starting async discovery probe to %s (timeout: %lums)", 
                broadcastIp.toString().c_str(), timeoutMs);

    discoveryState.udp.beginPacket(broadcastIp, SDCP_DISCOVERY_PORT);
    discoveryState.udp.write(reinterpret_cast<const uint8_t *>("M99999"), 6);
    discoveryState.udp.endPacket();

    discoveryState.active    = true;
    discoveryState.startTime = millis();
    discoveryState.lastProbeTime = discoveryState.startTime;
    discoveryState.timeoutMs = timeoutMs;
    discoveryState.callback  = callback;
    discoveryState.seenIps.clear();
    discoveryState.results.clear();

    // Block transport/WebSocket operations while discovery runs
    if (transport.webSocket.isConnected())
    {
        transport.webSocket.disconnect();
    }
    transport.blocked = true;

    return true;
}

void ElegooCC::cancelDiscovery()
{
    if (discoveryState.active)
    {
        discoveryState.udp.stop();
        discoveryState.active = false;
        transport.blocked     = false;
        logger.log("Discovery cancelled");
    }
}

void ElegooCC::updateDiscovery(unsigned long currentTime)
{
    if (!discoveryState.active)
    {
        if (transport.blocked)
        {
            transport.blocked = false;
        }
        return;
    }

    // Check for timeout
    if ((currentTime - discoveryState.startTime) >= discoveryState.timeoutMs)
    {
        logger.logf("Async discovery complete. Found %d printers.", discoveryState.results.size());
        
        // Stop UDP first
        discoveryState.udp.stop();
        discoveryState.active = false;
        transport.blocked     = false;

        // Invoke callback with results
        if (discoveryState.callback)
        {
            discoveryState.callback(discoveryState.results);
        }
        return;
    }

    // Re-broadcast every 400ms to give devices staggered response opportunities
    // The ESP32 UDP buffer may only catch one response per broadcast, so multiple
    // probes with different timing help catch all devices
    if ((currentTime - discoveryState.lastProbeTime) >= 400)
    {
        IPAddress localIp   = WiFi.localIP();
        IPAddress subnet    = WiFi.subnetMask();
        IPAddress broadcastIp((localIp[0] & subnet[0]) | ~subnet[0],
                              (localIp[1] & subnet[1]) | ~subnet[1],
                              (localIp[2] & subnet[2]) | ~subnet[2],
                              (localIp[3] & subnet[3]) | ~subnet[3]);

        discoveryState.udp.beginPacket(broadcastIp, SDCP_DISCOVERY_PORT);
        discoveryState.udp.write(reinterpret_cast<const uint8_t *>("M99999"), 6);
        discoveryState.udp.endPacket();

        discoveryState.lastProbeTime = currentTime;
    }

    // Process all available packets
    // Filter out our own IP (we might receive our own broadcast)
    IPAddress myIp = WiFi.localIP();

    int packetSize;
    while ((packetSize = discoveryState.udp.parsePacket()) > 0)
    {
        IPAddress remoteIp = discoveryState.udp.remoteIP();

        if (remoteIp == myIp)
        {
            discoveryState.udp.flush();
            continue;
        }

        if (remoteIp)
        {
            String ipStr = remoteIp.toString();

            // Check for duplicate
            bool duplicate = false;
            for (const auto& seen : discoveryState.seenIps) {
                if (seen == ipStr) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                discoveryState.seenIps.push_back(ipStr);

                String payload = "";
                char buffer[256];
                int  len = discoveryState.udp.read(buffer, sizeof(buffer) - 1);
                if (len > 0)
                {
                    buffer[len] = '\0';
                    payload = String(buffer);
                }

                logger.logf("Discovered printer at %s", ipStr.c_str());
                discoveryState.results.push_back({ipStr, payload});

                // REQUIRED: Close and reopen the socket after each response
                // The ESP32 WiFiUDP seems to get "stuck" after receiving a response,
                // preventing subsequent responses from being received. Recycling the
                // socket forces it to work properly.
                discoveryState.udp.stop();
                if (!discoveryState.udp.begin(SDCP_DISCOVERY_PORT))
                {
                    logger.log("Failed to reopen UDP socket during discovery");
                }
            } else {
                // CRITICAL: Always drain the packet even if duplicate!
                discoveryState.udp.flush();
            }
        }
        yield();
    }
}

// ============================================================================
// INTERRUPT SERVICE ROUTINE FOR PULSE COUNTING
// ============================================================================
// Static interrupt handler that increments the pulse counter on rising edge.
// This replaces polling-based edge detection and guarantees no pulses are dropped,
// even during loop stalls or high-speed extrusion.
//
// Called by GPIO interrupt on MOVEMENT_SENSOR_PIN rising edge.
// Execution time: ~2-3 microseconds (very fast, safe for ISR).
// ============================================================================
void IRAM_ATTR ElegooCC::pulseCounterISR()
{
    // Directly increment the static counter. This is safe to do from an ISR
    // as it involves no flash-based code.
    ElegooCC::isrPulseCounter++;
}
