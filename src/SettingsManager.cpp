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
    settings.detection_min_start_mm     = 12.0f;  // Minimum total extrusion before jam detection
    settings.detection_ratio_threshold  = 0.25f;  // 25% passing threshold (~75% deficit)
    settings.detection_hard_jam_mm      = 5.0f;   // 5mm expected with zero movement = hard jam
    settings.detection_soft_jam_time_ms = 7000;   // 7 seconds to signal slow clog (balanced for quick detection)
    settings.detection_hard_jam_time_ms = 3000;   // 3 seconds of negligible flow (quick response to complete jams)
    settings.tracking_mode              = 1;      // 1 = Windowed (Klipper-style)
    settings.tracking_window_ms         = 3000;   // 3 second sliding window
    settings.tracking_ewma_alpha        = 0.3f;   // 30% weight on new samples
    settings.sdcp_loss_behavior         = 2;
    settings.flow_telemetry_stale_ms    = 1000;
    settings.ui_refresh_interval_ms     = 1000;
    settings.log_level                  = 0;      // Default to Normal logging
    settings.suppress_pause_commands    = false;  // Pause commands enabled by default
    settings.dev_mode                   = false;  // DEPRECATED: Kept for backwards compatibility
    settings.verbose_logging            = false;  // DEPRECATED: Kept for backwards compatibility
    settings.flow_summary_logging       = false;  // DEPRECATED: Kept for backwards compatibility
    settings.pin_debug_logging          = false;  // DEPRECATED: Kept for backwards compatibility
    settings.movement_mm_per_pulse      = 2.88f;  // Actual sensor spec (2.88mm per pulse)
    settings.auto_calibrate_sensor      = false;  // Disabled by default
    settings.purge_filament_mm          = 47.0f;

    // Deprecated settings (for migration)
    settings.expected_deficit_mm      = 0.0f;
    settings.expected_flow_window_ms  = 0;
    settings.zero_deficit_logging           = false;
    settings.use_total_extrusion_deficit    = false;
    settings.total_vs_delta_logging         = false;
    settings.packet_flow_logging            = false;
    settings.use_total_extrusion_backlog    = true;  // Always true now
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

    // Migrate old expected_deficit_mm to new detection_length_mm
    if (doc.containsKey("detection_length_mm"))
    {
        settings.detection_length_mm = doc["detection_length_mm"].as<float>();
    }
    else if (doc.containsKey("expected_deficit_mm"))
    {
        // Migration path: use old value if new one doesn't exist
        settings.detection_length_mm = doc["expected_deficit_mm"].as<float>();
        logger.log("Migrated expected_deficit_mm to detection_length_mm");
    }
    else
    {
        settings.detection_length_mm = 10.0f;  // Default
    }

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

    // Load log_level with migration from old boolean fields
    if (doc.containsKey("log_level"))
    {
        settings.log_level = doc["log_level"].as<int>();
        // Clamp to valid range
        if (settings.log_level < 0) settings.log_level = 0;
        if (settings.log_level > 3) settings.log_level = 3;
    }
    else
    {
        // Migrate from old boolean fields
        bool dev_mode = doc.containsKey("dev_mode") ? doc["dev_mode"].as<bool>() : false;
        bool pin_debug = doc.containsKey("pin_debug_logging") ? doc["pin_debug_logging"].as<bool>() : false;
        bool verbose = doc.containsKey("verbose_logging") ? doc["verbose_logging"].as<bool>() : false;
        bool flow_summary = doc.containsKey("flow_summary_logging") ? doc["flow_summary_logging"].as<bool>() : false;

        // Determine log level from old boolean flags (highest priority wins)
        if (dev_mode || pin_debug)
        {
            settings.log_level = 3;  // Dev level
            logger.log("Migrated dev_mode/pin_debug_logging to log_level=3 (Dev)");
        }
        else if (verbose)
        {
            settings.log_level = 2;  // Verbose level
            logger.log("Migrated verbose_logging to log_level=2 (Verbose)");
        }
        else if (flow_summary)
        {
            settings.log_level = 1;  // Debug level
            logger.log("Migrated flow_summary_logging to log_level=1 (Debug)");
        }
        else
        {
            settings.log_level = 0;  // Normal level
        }
    }

    // Keep deprecated fields in sync for backwards compatibility
    settings.dev_mode = (settings.log_level >= 3);
    settings.verbose_logging = (settings.log_level >= 2);
    settings.flow_summary_logging = (settings.log_level >= 1);
    settings.pin_debug_logging = (settings.log_level >= 3);
    settings.movement_mm_per_pulse = doc.containsKey("movement_mm_per_pulse")
                                         ? doc["movement_mm_per_pulse"].as<float>()
                                         : 2.88f;  // Correct sensor spec
    settings.detection_grace_period_ms = doc.containsKey("detection_grace_period_ms")
                                             ? doc["detection_grace_period_ms"].as<int>()
                                             : 8000;  // Default 8000ms
    settings.detection_min_start_mm = 12.0f;
    if (doc.containsKey("detection_min_start_mm"))
    {
        float minStartMm = doc["detection_min_start_mm"].as<float>();
        if (isnan(minStartMm) || minStartMm < 0.0f || minStartMm > 999.0f)
        {
            minStartMm = 12.0f;
        }
        settings.detection_min_start_mm = minStartMm;
    }
    settings.purge_filament_mm = 47.0f;
    if (doc.containsKey("purge_filament_mm"))
    {
        float purgeMm = doc["purge_filament_mm"].as<float>();
        if (isnan(purgeMm) || purgeMm < 0.0f || purgeMm > 999.0f)
        {
            purgeMm = 47.0f;
        }
        settings.purge_filament_mm = purgeMm;
    }
    settings.tracking_mode = doc.containsKey("tracking_mode")
                                 ? doc["tracking_mode"].as<int>()
                                 : 1;  // Default to Windowed mode
    settings.tracking_window_ms = doc.containsKey("tracking_window_ms")
                                      ? doc["tracking_window_ms"].as<int>()
                                      : 3000;  // Default 3 seconds
    settings.tracking_ewma_alpha = doc.containsKey("tracking_ewma_alpha")
                                       ? doc["tracking_ewma_alpha"].as<float>()
                                       : 0.3f;  // Default 0.3
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

    // Load deprecated settings for backwards compatibility (ignored in new code)
    settings.expected_deficit_mm = settings.detection_length_mm;  // Keep in sync
    settings.expected_flow_window_ms = 0;
    settings.zero_deficit_logging = false;
    settings.use_total_extrusion_deficit = false;
    settings.total_vs_delta_logging = false;
    settings.packet_flow_logging = false;
    settings.use_total_extrusion_backlog = true;

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

float SettingsManager::getDetectionMinStartMm()
{
    return getSettings().detection_min_start_mm;
}

float SettingsManager::getDetectionRatioThreshold()
{
    return getSettings().detection_ratio_threshold;
}

float SettingsManager::getPurgeFilamentMm()
{
    return getSettings().purge_filament_mm;
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

int SettingsManager::getTrackingMode()
{
    return getSettings().tracking_mode;
}

int SettingsManager::getTrackingWindowMs()
{
    return getSettings().tracking_window_ms;
}

float SettingsManager::getTrackingEwmaAlpha()
{
    return getSettings().tracking_ewma_alpha;
}

// Deprecated getters
float SettingsManager::getExpectedDeficitMM()
{
    return getSettings().detection_length_mm;  // Redirect to new setting
}

int SettingsManager::getExpectedFlowWindowMs()
{
    return 0;  // No longer used (distance-based detection only)
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

bool SettingsManager::getDevMode()
{
    // DEPRECATED: Returns true if log level is Dev (3)
    return getSettings().log_level >= 3;
}

bool SettingsManager::getVerboseLogging()
{
    // DEPRECATED: Returns true if log level is Verbose (2) or higher
    return getSettings().log_level >= 2;
}

bool SettingsManager::getFlowSummaryLogging()
{
    // DEPRECATED: Returns true if log level is Debug (1) or higher
    return getSettings().log_level >= 1;
}

bool SettingsManager::getPinDebugLogging()
{
    // DEPRECATED: Returns true if log level is Dev (3)
    return getSettings().log_level >= 3;
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
    settings.expected_deficit_mm = value;  // Keep deprecated field in sync
}

void SettingsManager::setDetectionGracePeriodMs(int periodMs)
{
    if (!isLoaded)
        load();
    settings.detection_grace_period_ms = periodMs;
}

void SettingsManager::setDetectionMinStartMm(float minMm)
{
    if (!isLoaded)
        load();
    if (isnan(minMm) || minMm < 0.0f || minMm > 999.0f)
    {
        minMm = 12.0f;
    }
    settings.detection_min_start_mm = minMm;
}

void SettingsManager::setPurgeFilamentMm(float purgeMm)
{
    if (!isLoaded)
        load();
    if (isnan(purgeMm) || purgeMm < 0.0f || purgeMm > 999.0f)
    {
        purgeMm = 47.0f;
    }
    settings.purge_filament_mm = purgeMm;
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

void SettingsManager::setTrackingMode(int mode)
{
    if (!isLoaded)
        load();
    settings.tracking_mode = mode;
}

void SettingsManager::setTrackingWindowMs(int windowMs)
{
    if (!isLoaded)
        load();
    settings.tracking_window_ms = windowMs;
}

void SettingsManager::setTrackingEwmaAlpha(float alpha)
{
    if (!isLoaded)
        load();
    settings.tracking_ewma_alpha = alpha;
}

// Deprecated setter
void SettingsManager::setExpectedDeficitMM(float value)
{
    setDetectionLengthMM(value);  // Redirect to new setter
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
    // Clamp to valid range
    if (level < 0) level = 0;
    if (level > 3) level = 3;
    settings.log_level = level;
    // Keep deprecated fields in sync
    settings.dev_mode = (level >= 3);
    settings.verbose_logging = (level >= 2);
    settings.flow_summary_logging = (level >= 1);
    settings.pin_debug_logging = (level >= 3);
    // Update logger immediately
    logger.setLogLevel((LogLevel)level);
}

void SettingsManager::setSuppressPauseCommands(bool suppress)
{
    if (!isLoaded)
        load();
    settings.suppress_pause_commands = suppress;
}

void SettingsManager::setDevMode(bool devMode)
{
    // DEPRECATED: Sets log level to 3 (Dev) if true, preserves current level if false
    if (!isLoaded)
        load();
    if (devMode && settings.log_level < 3)
    {
        setLogLevel(3);
    }
    settings.dev_mode = devMode;
}

void SettingsManager::setVerboseLogging(bool verbose)
{
    // DEPRECATED: Sets log level to 2 (Verbose) if true, preserves current level if false
    if (!isLoaded)
        load();
    if (verbose && settings.log_level < 2)
    {
        setLogLevel(2);
    }
    settings.verbose_logging = verbose;
}

void SettingsManager::setFlowSummaryLogging(bool enabled)
{
    // DEPRECATED: Sets log level to 1 (Debug) if true, preserves current level if false
    if (!isLoaded)
        load();
    if (enabled && settings.log_level < 1)
    {
        setLogLevel(1);
    }
    settings.flow_summary_logging = enabled;
}

void SettingsManager::setPinDebugLogging(bool enabled)
{
    // DEPRECATED: Sets log level to 3 (Dev) if true, preserves current level if false
    if (!isLoaded)
        load();
    if (enabled && settings.log_level < 3)
    {
        setLogLevel(3);
    }
    settings.pin_debug_logging = enabled;
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
    doc["detection_min_start_mm"]     = settings.detection_min_start_mm;
    doc["purge_filament_mm"]          = settings.purge_filament_mm;
    doc["detection_ratio_threshold"]  = settings.detection_ratio_threshold;
    doc["detection_hard_jam_mm"]      = settings.detection_hard_jam_mm;
    doc["detection_soft_jam_time_ms"] = settings.detection_soft_jam_time_ms;
    doc["detection_hard_jam_time_ms"] = settings.detection_hard_jam_time_ms;
    doc["tracking_mode"]              = settings.tracking_mode;
    doc["tracking_window_ms"]         = settings.tracking_window_ms;
    doc["tracking_ewma_alpha"]        = settings.tracking_ewma_alpha;
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

    // DEV: Check if approaching allocation limit
    if (getLogLevel() >= LOG_DEV)
    {
        size_t actualSize = measureJson(doc);
        if (actualSize > 1305)  // >85% of 1536 bytes
        {
            logger.logf(LOG_DEV, "SettingsManager toJson size: %zu / 1536 bytes (%.1f%%)",
                       actualSize, (actualSize * 100.0f / 1536.0f));
        }
    }

    return output;
}
