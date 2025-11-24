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
    settings.detection_grace_period_ms  = 500;    // 500ms grace period (reduced from 1500ms)
    settings.detection_ratio_threshold  = 0.25f;  // 25% passing threshold (~75% deficit)
    settings.detection_hard_jam_mm      = 5.0f;   // 5mm expected with zero movement = hard jam
    settings.detection_soft_jam_time_ms = 10000;  // 10 seconds to signal slow clog
    settings.detection_hard_jam_time_ms = 5000;   // 5 seconds of negligible flow
    settings.tracking_mode              = 1;      // 1 = Windowed (Klipper-style)
    settings.tracking_window_ms         = 5000;   // 5 second sliding window
    settings.tracking_ewma_alpha        = 0.3f;   // 30% weight on new samples
    settings.sdcp_loss_behavior         = 2;
    settings.flow_telemetry_stale_ms    = 1000;
    settings.ui_refresh_interval_ms     = 1000;
    settings.dev_mode                   = false;
    settings.verbose_logging            = false;
    settings.flow_summary_logging       = false;
    settings.movement_mm_per_pulse      = 2.88f;  // Actual sensor spec (2.88mm per pulse)
    settings.auto_calibrate_sensor      = false;  // Disabled by default

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

    StaticJsonDocument<1024> doc;
    DeserializationError     error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        logger.log("Settings JSON parsing error, using defaults");
        isLoaded = true;
        return false;
    }

    settings.ap_mode             = doc["ap_mode"] | false;
    settings.ssid                = doc["ssid"] | "";
    settings.passwd              = doc["passwd"] | "";
    settings.elegooip            = doc["elegooip"] | "";
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
    settings.dev_mode =
        doc.containsKey("dev_mode") ? doc["dev_mode"].as<bool>() : false;
    settings.verbose_logging = doc.containsKey("verbose_logging")
                                   ? doc["verbose_logging"].as<bool>()
                                   : false;
    settings.flow_summary_logging = doc.containsKey("flow_summary_logging")
                                        ? doc["flow_summary_logging"].as<bool>()
                                        : false;
    settings.movement_mm_per_pulse = doc.containsKey("movement_mm_per_pulse")
                                         ? doc["movement_mm_per_pulse"].as<float>()
                                         : 2.88f;  // Correct sensor spec
    settings.detection_grace_period_ms = doc.containsKey("detection_grace_period_ms")
                                             ? doc["detection_grace_period_ms"].as<int>()
                                             : 500;  // Default 500ms
    settings.tracking_mode = doc.containsKey("tracking_mode")
                                 ? doc["tracking_mode"].as<int>()
                                 : 1;  // Default to Windowed mode
    settings.tracking_window_ms = doc.containsKey("tracking_window_ms")
                                      ? doc["tracking_window_ms"].as<int>()
                                      : 5000;  // Default 5 seconds
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

    // Load deprecated settings for backwards compatibility (ignored in new code)
    settings.expected_deficit_mm = settings.detection_length_mm;  // Keep in sync
    settings.expected_flow_window_ms = 0;
    settings.zero_deficit_logging = false;
    settings.use_total_extrusion_deficit = false;
    settings.total_vs_delta_logging = false;
    settings.packet_flow_logging = false;
    settings.use_total_extrusion_backlog = true;

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

bool SettingsManager::getDevMode()
{
    return getSettings().dev_mode;
}

bool SettingsManager::getVerboseLogging()
{
    return getSettings().verbose_logging;
}

bool SettingsManager::getFlowSummaryLogging()
{
    return getSettings().flow_summary_logging;
}

float SettingsManager::getMovementMmPerPulse()
{
    return getSettings().movement_mm_per_pulse;
}

bool SettingsManager::getAutoCalibrateSensor()
{
    return getSettings().auto_calibrate_sensor;
}

void SettingsManager::setSSID(const String &ssid)
{
    if (!isLoaded)
        load();
    if (settings.ssid != ssid)
    {
        settings.ssid = ssid;
        wifiChanged   = true;
    }
}

void SettingsManager::setPassword(const String &password)
{
    if (!isLoaded)
        load();
    if (settings.passwd != password)
    {
        settings.passwd = password;
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
    settings.elegooip = ip;
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

void SettingsManager::setDevMode(bool devMode)
{
    if (!isLoaded)
        load();
    settings.dev_mode = devMode;
}

void SettingsManager::setVerboseLogging(bool verbose)
{
    if (!isLoaded)
        load();
    settings.verbose_logging = verbose;
}

void SettingsManager::setFlowSummaryLogging(bool enabled)
{
    if (!isLoaded)
        load();
    settings.flow_summary_logging = enabled;
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

String SettingsManager::toJson(bool includePassword)
{
    String                   output;
    StaticJsonDocument<1024> doc;

    doc["ap_mode"]             = settings.ap_mode;
    doc["ssid"]                = settings.ssid;
    doc["elegooip"]            = settings.elegooip;
    doc["pause_on_runout"]     = settings.pause_on_runout;
    doc["start_print_timeout"] = settings.start_print_timeout;
    doc["enabled"]             = settings.enabled;
    doc["has_connected"]       = settings.has_connected;
    doc["detection_length_mm"]        = settings.detection_length_mm;  // DEPRECATED
    doc["detection_grace_period_ms"]  = settings.detection_grace_period_ms;
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
    doc["dev_mode"]                   = settings.dev_mode;
    doc["verbose_logging"]            = settings.verbose_logging;
    doc["flow_summary_logging"]       = settings.flow_summary_logging;
    doc["movement_mm_per_pulse"]      = settings.movement_mm_per_pulse;
    doc["auto_calibrate_sensor"]      = settings.auto_calibrate_sensor;

    if (includePassword)
    {
        doc["passwd"] = settings.passwd;
    }

    serializeJson(doc, output);
    return output;
}
