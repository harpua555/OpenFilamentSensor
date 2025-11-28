#include "SettingsManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <stdlib.h>

#include "Logger.h"

SettingsManager &SettingsManager::getInstance()
{
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager()
{
    isLoaded                     = false;
    requestWifiReconnect         = false;
    wifiChanged                  = false;
    settings.ap_mode             = false;
    settings.ssid                = "";
    settings.passwd              = "";
    settings.elegooip            = "";
    settings.pause_on_runout     = true;
    settings.start_print_timeout = 10000;
    settings.enabled             = true;
    settings.has_connected       = false;
    settings.detection_length_mm        = 10.0f;  // DEPRECATED: Use ratio-based detection
    settings.detection_grace_period_ms  = 5000;   // 5000ms grace period for print start (reduced from 8s)
    settings.detection_ratio_threshold  = 0.25f;  // 25% passing threshold (~75% deficit)
    settings.detection_hard_jam_mm      = 5.0f;   // 5mm expected with zero movement = hard jam
    settings.detection_soft_jam_time_ms = 7000;   // 7 seconds to signal slow clog (balanced for quick detection)
    settings.detection_hard_jam_time_ms = 3000;   // 3 seconds of negligible flow (quick response to complete jams)
    settings.sdcp_loss_behavior         = 2;
    settings.flow_telemetry_stale_ms    = 1000;
    settings.ui_refresh_interval_ms     = 1000;
    settings.log_level                  = 0;      // Default to Normal logging
    settings.suppress_pause_commands    = false;  // Pause commands enabled by default
    settings.movement_mm_per_pulse      = 2.88f;  // Actual sensor spec (2.88mm per pulse)
    settings.auto_calibrate_sensor      = false;  // Disabled by default
    settings.purge_filament_mm          = 47.0f;
}

bool SettingsManager::load()
{
    File file = LittleFS.open("/user_settings.json", "r");
    if (!file)
    {
        logger.log("Settings file not found, using defaults");
        isLoaded = true;
        return false;
    }

    // JSON allocation: 1536 bytes stack (increased from 800 for expanded settings)
    // With 28+ fields, ArduinoJson needs ~900-1200 bytes
    // Last measured: 2025-11-26
    // See: .claude/hardcoded-allocations.md for maintenance notes
    StaticJsonDocument<1536> doc;
    DeserializationError     error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        logger.log("Settings JSON parsing error, using defaults");
        isLoaded = true;
        return false;
    }

    settings.ap_mode             = doc["ap_mode"] | false;
    settings.ssid                = (doc["ssid"] | "");
    settings.ssid.trim();
    settings.passwd              = (doc["passwd"] | "");
    settings.passwd.trim();
    settings.elegooip            = (doc["elegooip"] | "");
    settings.elegooip.trim();
    settings.pause_on_runout     = doc["pause_on_runout"] | true;
    settings.enabled             = doc["enabled"] | true;
    settings.start_print_timeout = doc["start_print_timeout"] | 10000;
    settings.has_connected       = doc["has_connected"] | false;
    settings.detection_length_mm = doc.containsKey("detection_length_mm")
                                        ? doc["detection_length_mm"].as<float>()
                                        : 10.0f;  // Default
    settings.sdcp_loss_behavior =
        doc.containsKey("sdcp_loss_behavior") ? doc["sdcp_loss_behavior"].as<int>() : 2;
    settings.flow_telemetry_stale_ms =
        doc.containsKey("flow_telemetry_stale_ms")
            ? doc["flow_telemetry_stale_ms"].as<int>()
            : 1000;
    settings.ui_refresh_interval_ms =
        doc.containsKey("ui_refresh_interval_ms")
            ? doc["ui_refresh_interval_ms"].as<int>()
            : 1000;

    // Load suppress_pause_commands (independent of log_level)
    settings.suppress_pause_commands = doc.containsKey("suppress_pause_commands")
                                          ? doc["suppress_pause_commands"].as<bool>()
                                          : false;

    // Load log_level
    settings.log_level = doc.containsKey("log_level") ? doc["log_level"].as<int>() : 0;
    // Clamp to valid range (0=Normal, 1=Verbose, 2=Pin Values)
    if (settings.log_level < 0) settings.log_level = 0;
    if (settings.log_level > 2) settings.log_level = 2;

    settings.movement_mm_per_pulse = doc.containsKey("movement_mm_per_pulse")
                                         ? doc["movement_mm_per_pulse"].as<float>()
                                         : 2.88f;  // Correct sensor spec
    settings.detection_grace_period_ms = doc.containsKey("detection_grace_period_ms")
                                             ? doc["detection_grace_period_ms"].as<int>()
                                             : 8000;  // Default 8000ms
    // Keep purge_filament_mm in settings for potential future use, but don't expose getters/setters
    settings.purge_filament_mm = doc.containsKey("purge_filament_mm")
                                     ? doc["purge_filament_mm"].as<float>()
                                     : 47.0f;
    settings.detection_ratio_threshold = doc.containsKey("detection_ratio_threshold")
                                             ? doc["detection_ratio_threshold"].as<float>()
                                             : 0.25f;  // Default 25% passing deficit
    settings.detection_hard_jam_mm = doc.containsKey("detection_hard_jam_mm")
                                         ? doc["detection_hard_jam_mm"].as<float>()
                                         : 5.0f;  // Default 5mm
    settings.detection_soft_jam_time_ms = doc.containsKey("detection_soft_jam_time_ms")
                                              ? doc["detection_soft_jam_time_ms"].as<int>()
                                              : 10000;  // Default 10 seconds
    settings.detection_hard_jam_time_ms = doc.containsKey("detection_hard_jam_time_ms")
                                              ? doc["detection_hard_jam_time_ms"].as<int>()
                                              : 5000;  // Default 5 seconds
    settings.auto_calibrate_sensor = doc.containsKey("auto_calibrate_sensor")
                                         ? doc["auto_calibrate_sensor"].as<bool>()
                                         : false;  // Default disabled
    settings.test_recording_mode = doc.containsKey("test_recording_mode")
                                       ? doc["test_recording_mode"].as<bool>()
                                       : false;  // Default disabled

    // Update logger with loaded log level
    logger.setLogLevel((LogLevel)settings.log_level);

    isLoaded = true;
    return true;
}

bool SettingsManager::save(bool skipWifiCheck)
{
    String output = toJson(true);

    File file = LittleFS.open("/user_settings.json", "w");
    if (!file)
    {
        logger.log("Failed to open settings file for writing");
        return false;
    }

    if (file.print(output) == 0)
    {
        logger.log("Failed to write settings to file");
        file.close();
        return false;
    }

    file.close();
    logger.log("Settings saved successfully");
    if (!skipWifiCheck && wifiChanged)
    {
        logger.log("WiFi changed, requesting reconnection");
        requestWifiReconnect = true;
        wifiChanged          = false;
    }
    return true;
}

const user_settings &SettingsManager::getSettings()
{
    if (!isLoaded)
    {
        load();
    }
    return settings;
}

String SettingsManager::getSSID()
{
    return getSettings().ssid;
}

String SettingsManager::getPassword()
{
    return getSettings().passwd;
}

bool SettingsManager::isAPMode()
{
    return getSettings().ap_mode;
}

String SettingsManager::getElegooIP()
{
    return getSettings().elegooip;
}

bool SettingsManager::getPauseOnRunout()
{
    return getSettings().pause_on_runout;
}

int SettingsManager::getStartPrintTimeout()
{
    return getSettings().start_print_timeout;
}

bool SettingsManager::getEnabled()
{
    return getSettings().enabled;
}

bool SettingsManager::getHasConnected()
{
    return getSettings().has_connected;
}

float SettingsManager::getDetectionLengthMM()
{
    return getSettings().detection_length_mm;
}

int SettingsManager::getDetectionGracePeriodMs()
{
    return getSettings().detection_grace_period_ms;
}

float SettingsManager::getDetectionRatioThreshold()
{
    return getSettings().detection_ratio_threshold;
}

float SettingsManager::getDetectionHardJamMm()
{
    return getSettings().detection_hard_jam_mm;
}

int SettingsManager::getDetectionSoftJamTimeMs()
{
    return getSettings().detection_soft_jam_time_ms;
}

int SettingsManager::getDetectionHardJamTimeMs()
{
    return getSettings().detection_hard_jam_time_ms;
}

int SettingsManager::getSdcpLossBehavior()
{
    return getSettings().sdcp_loss_behavior;
}

int SettingsManager::getFlowTelemetryStaleMs()
{
    return getSettings().flow_telemetry_stale_ms;
}

int SettingsManager::getUiRefreshIntervalMs()
{
    return getSettings().ui_refresh_interval_ms;
}

int SettingsManager::getLogLevel()
{
    return getSettings().log_level;
}

bool SettingsManager::getSuppressPauseCommands()
{
    return getSettings().suppress_pause_commands;
}

bool SettingsManager::getVerboseLogging()
{
    // Returns true if log level is Verbose (1) or higher
    return getSettings().log_level >= 1;
}

bool SettingsManager::getFlowSummaryLogging()
{
    // Returns true if log level is Verbose (1) or higher
    // (old Debug level merged into Verbose)
    return getSettings().log_level >= 1;
}

bool SettingsManager::getPinDebugLogging()
{
    // Returns true if log level is Pin Values (2)
    return getSettings().log_level >= 2;
}

float SettingsManager::getMovementMmPerPulse()
{
    return getSettings().movement_mm_per_pulse;
}

bool SettingsManager::getAutoCalibrateSensor()
{
    return getSettings().auto_calibrate_sensor;
}

bool SettingsManager::getTestRecordingMode()
{
    return getSettings().test_recording_mode;
}

void SettingsManager::setSSID(const String &ssid)
{
    if (!isLoaded)
        load();
    String trimmed = ssid;
    trimmed.trim();
    if (settings.ssid != trimmed)
    {
        settings.ssid = trimmed;
        wifiChanged   = true;
    }
}

void SettingsManager::setPassword(const String &password)
{
    if (!isLoaded)
        load();
    String trimmed = password;
    trimmed.trim();
    if (settings.passwd != trimmed)
    {
        settings.passwd = trimmed;
        wifiChanged     = true;
    }
}

void SettingsManager::setAPMode(bool apMode)
{
    if (!isLoaded)
        load();
    if (settings.ap_mode != apMode)
    {
        settings.ap_mode = apMode;
        wifiChanged      = true;
    }
}

void SettingsManager::setElegooIP(const String &ip)
{
    if (!isLoaded)
        load();
    String trimmed = ip;
    trimmed.trim();
    settings.elegooip = trimmed;
}

void SettingsManager::setPauseOnRunout(bool pauseOnRunout)
{
    if (!isLoaded)
        load();
    settings.pause_on_runout = pauseOnRunout;
}

void SettingsManager::setStartPrintTimeout(int timeoutMs)
{
    if (!isLoaded)
        load();
    settings.start_print_timeout = timeoutMs;
}

void SettingsManager::setEnabled(bool enabled)
{
    if (!isLoaded)
        load();
    settings.enabled = enabled;
}

void SettingsManager::setHasConnected(bool hasConnected)
{
    if (!isLoaded)
        load();
    settings.has_connected = hasConnected;
}

void SettingsManager::setDetectionLengthMM(float value)
{
    if (!isLoaded)
        load();
    settings.detection_length_mm = value;
}

void SettingsManager::setDetectionGracePeriodMs(int periodMs)
{
    if (!isLoaded)
        load();
    settings.detection_grace_period_ms = periodMs;
}

void SettingsManager::setDetectionRatioThreshold(float threshold)
{
    if (!isLoaded)
        load();
    settings.detection_ratio_threshold = threshold;
}

void SettingsManager::setDetectionHardJamMm(float mmThreshold)
{
    if (!isLoaded)
        load();
    settings.detection_hard_jam_mm = mmThreshold;
}

void SettingsManager::setDetectionSoftJamTimeMs(int timeMs)
{
    if (!isLoaded)
        load();
    settings.detection_soft_jam_time_ms = timeMs;
}

void SettingsManager::setDetectionHardJamTimeMs(int timeMs)
{
    if (!isLoaded)
        load();
    settings.detection_hard_jam_time_ms = timeMs;
}

void SettingsManager::setSdcpLossBehavior(int behavior)
{
    if (!isLoaded)
        load();
    settings.sdcp_loss_behavior = behavior;
}

void SettingsManager::setFlowTelemetryStaleMs(int staleMs)
{
    if (!isLoaded)
        load();
    settings.flow_telemetry_stale_ms = staleMs;
}

void SettingsManager::setUiRefreshIntervalMs(int intervalMs)
{
    if (!isLoaded)
        load();
    settings.ui_refresh_interval_ms = intervalMs;
}

void SettingsManager::setLogLevel(int level)
{
    if (!isLoaded)
        load();
    // Clamp to valid range (0=Normal, 1=Verbose, 2=Pin Values)
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    settings.log_level = level;
    // Update logger immediately
    logger.setLogLevel((LogLevel)level);
}

void SettingsManager::setSuppressPauseCommands(bool suppress)
{
    if (!isLoaded)
        load();
    settings.suppress_pause_commands = suppress;
}

void SettingsManager::setMovementMmPerPulse(float mmPerPulse)
{
    if (!isLoaded)
        load();
    settings.movement_mm_per_pulse = mmPerPulse;
}

void SettingsManager::setAutoCalibrateSensor(bool autoCal)
{
    if (!isLoaded)
        load();
    settings.auto_calibrate_sensor = autoCal;
}

void SettingsManager::setTestRecordingMode(bool enabled)
{
    if (!isLoaded)
        load();
    settings.test_recording_mode = enabled;
}

String SettingsManager::toJson(bool includePassword)
{
    String                   output;
    output.reserve(1536);  // Pre-allocate to prevent fragmentation
    // JSON allocation: 1536 bytes stack (increased from 800 for expanded settings)
    // With 28+ fields, ArduinoJson needs ~900-1200 bytes
    // Last measured: 2025-11-26
    // See: .claude/hardcoded-allocations.md for maintenance notes
    StaticJsonDocument<1536> doc;

    doc["ap_mode"]             = settings.ap_mode;
    doc["ssid"]                = settings.ssid;
    doc["elegooip"]            = settings.elegooip;
    doc["pause_on_runout"]     = settings.pause_on_runout;
    doc["start_print_timeout"] = settings.start_print_timeout;
    doc["enabled"]             = settings.enabled;
    doc["has_connected"]       = settings.has_connected;
    doc["detection_grace_period_ms"]  = settings.detection_grace_period_ms;
    doc["purge_filament_mm"]          = settings.purge_filament_mm;  // Keep for future use
    doc["detection_ratio_threshold"]  = settings.detection_ratio_threshold;
    doc["detection_hard_jam_mm"]      = settings.detection_hard_jam_mm;
    doc["detection_soft_jam_time_ms"] = settings.detection_soft_jam_time_ms;
    doc["detection_hard_jam_time_ms"] = settings.detection_hard_jam_time_ms;
    doc["sdcp_loss_behavior"]         = settings.sdcp_loss_behavior;
    doc["flow_telemetry_stale_ms"]    = settings.flow_telemetry_stale_ms;
    doc["ui_refresh_interval_ms"]     = settings.ui_refresh_interval_ms;
    doc["log_level"]                  = settings.log_level;  // Unified logging level
    doc["suppress_pause_commands"]    = settings.suppress_pause_commands;
    doc["movement_mm_per_pulse"]      = settings.movement_mm_per_pulse;
    doc["auto_calibrate_sensor"]      = settings.auto_calibrate_sensor;
    doc["test_recording_mode"]        = settings.test_recording_mode;

    if (includePassword)
    {
        doc["passwd"] = settings.passwd;
    }

    serializeJson(doc, output);

    // Pin Values level: Check if approaching allocation limit
    if (getLogLevel() >= LOG_PIN_VALUES)
    {
        size_t actualSize = measureJson(doc);
        if (actualSize > 1305)  // >85% of 1536 bytes
        {
            logger.logf(LOG_PIN_VALUES, "SettingsManager toJson size: %zu / 1536 bytes (%.1f%%)",
                       actualSize, (actualSize * 100.0f / 1536.0f));
        }
    }

    return output;
}
