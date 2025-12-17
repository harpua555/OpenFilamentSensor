#include "SettingsManager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <cstddef>
#include <stdlib.h>

#include "Logger.h"

namespace
{
enum class SettingKind
{
    Bool,
    Int,
    Float,
    String
};

struct SettingField
{
    const char* key;
    SettingKind kind;
    size_t      offset;
    bool        trimString;
    bool        includeInJson;
    bool        redactWhenRedacted;
    bool        hasDefault;
    bool        boolDefault;
    int         intDefault;
    float       floatDefault;
    const char* stringDefault;
};

SettingField makeBoolField(const char* key, size_t offset, bool defaultValue,
                           bool includeInJson = true)
{
    SettingField field{key, SettingKind::Bool, offset, false, includeInJson, false, true};
    field.boolDefault = defaultValue;
    field.intDefault  = 0;
    field.floatDefault = 0.0f;
    field.stringDefault = nullptr;
    return field;
}

SettingField makeIntField(const char* key, size_t offset, int defaultValue,
                          bool includeInJson = true)
{
    SettingField field{key, SettingKind::Int, offset, false, includeInJson, false, true};
    field.boolDefault = false;
    field.intDefault  = defaultValue;
    field.floatDefault = 0.0f;
    field.stringDefault = nullptr;
    return field;
}

SettingField makeFloatField(const char* key, size_t offset, float defaultValue,
                            bool includeInJson = true)
{
    SettingField field{key, SettingKind::Float, offset, false, includeInJson, false, true};
    field.boolDefault = false;
    field.intDefault  = 0;
    field.floatDefault = defaultValue;
    field.stringDefault = nullptr;
    return field;
}

SettingField makeStringField(const char* key, size_t offset, const char* defaultValue,
                             bool trim, bool includeInJson = true, bool redact = false)
{
    SettingField field{key, SettingKind::String, offset, trim, includeInJson, redact, true};
    field.boolDefault = false;
    field.intDefault  = 0;
    field.floatDefault = 0.0f;
    field.stringDefault = defaultValue;
    return field;
}

static const SettingField kSettingFields[] = {
    makeBoolField("ap_mode", offsetof(user_settings, ap_mode), false),
    makeStringField("ssid", offsetof(user_settings, ssid), "", true),
    makeStringField("passwd", offsetof(user_settings, passwd), "", true, true, true),
    makeStringField("elegooip", offsetof(user_settings, elegooip), "", true),
    makeBoolField("pause_on_runout", offsetof(user_settings, pause_on_runout), true),
    makeIntField("start_print_timeout", offsetof(user_settings, start_print_timeout), 10000),
    makeBoolField("enabled", offsetof(user_settings, enabled), true),
    makeBoolField("has_connected", offsetof(user_settings, has_connected), false),
    makeFloatField("detection_length_mm", offsetof(user_settings, detection_length_mm), 10.0f,
                   false),
    makeIntField("detection_grace_period_ms", offsetof(user_settings, detection_grace_period_ms),
                 8000),
    makeIntField("detection_ratio_threshold", offsetof(user_settings, detection_ratio_threshold),
                 25),  // 25 = 25% passing threshold
    makeFloatField("detection_hard_jam_mm", offsetof(user_settings, detection_hard_jam_mm), 5.0f),
    makeIntField("detection_soft_jam_time_ms",
                 offsetof(user_settings, detection_soft_jam_time_ms), 10000),
    makeIntField("detection_hard_jam_time_ms",
                 offsetof(user_settings, detection_hard_jam_time_ms), 5000),
    makeIntField("detection_mode", offsetof(user_settings, detection_mode), 0),
    makeIntField("sdcp_loss_behavior", offsetof(user_settings, sdcp_loss_behavior), 2),
    makeIntField("flow_telemetry_stale_ms", offsetof(user_settings, flow_telemetry_stale_ms), 1000),
    makeIntField("ui_refresh_interval_ms", offsetof(user_settings, ui_refresh_interval_ms), 1000),
    makeIntField("log_level", offsetof(user_settings, log_level), 0),
    makeBoolField("suppress_pause_commands", offsetof(user_settings, suppress_pause_commands),
                  false),
    makeFloatField("movement_mm_per_pulse", offsetof(user_settings, movement_mm_per_pulse), 2.88f),
    makeBoolField("auto_calibrate_sensor", offsetof(user_settings, auto_calibrate_sensor), false),
    makeFloatField("pulse_reduction_percent", offsetof(user_settings, pulse_reduction_percent), 100.0f),
    makeBoolField("test_recording_mode", offsetof(user_settings, test_recording_mode), false),
    makeBoolField("show_debug_page", offsetof(user_settings, show_debug_page), false),
};

constexpr size_t SETTINGS_JSON_CAPACITY = 1536;  // Increased from 1152 to prevent truncation

template <typename T>
T& fieldAt(user_settings& settings, size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(&settings) + offset);
}

template <typename T>
const T& fieldAtConst(const user_settings& settings, size_t offset)
{
    return *reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(&settings) + offset);
}

void applyDefault(const SettingField& field, user_settings& settings)
{
    if (!field.hasDefault)
    {
        return;
    }

    switch (field.kind)
    {
        case SettingKind::Bool:
            fieldAt<bool>(settings, field.offset) = field.boolDefault;
            break;
        case SettingKind::Int:
            fieldAt<int>(settings, field.offset) = field.intDefault;
            break;
        case SettingKind::Float:
            fieldAt<float>(settings, field.offset) = field.floatDefault;
            break;
        case SettingKind::String:
        {
            String value = field.stringDefault ? field.stringDefault : "";
            if (field.trimString)
            {
                value.trim();
            }
            fieldAt<String>(settings, field.offset) = value;
            break;
        }
    }
}

void applyVariant(const SettingField& field, JsonVariantConst value, user_settings& settings)
{
    switch (field.kind)
    {
        case SettingKind::Bool:
            fieldAt<bool>(settings, field.offset) = value.as<bool>();
            break;
        case SettingKind::Int:
            fieldAt<int>(settings, field.offset) = value.as<int>();
            break;
        case SettingKind::Float:
            fieldAt<float>(settings, field.offset) = value.as<float>();
            break;
        case SettingKind::String:
        {
            const char* raw = value.as<const char*>();
            String      str = raw ? raw : "";
            if (field.trimString)
            {
                str.trim();
            }
            fieldAt<String>(settings, field.offset) = str;
            break;
        }
    }
}

void serializeField(const SettingField& field, JsonDocument& doc,
                    const user_settings& settings, bool includePasswords)
{
    if (!field.includeInJson)
    {
        return;
    }
    if (!includePasswords && field.redactWhenRedacted)
    {
        return;
    }

    switch (field.kind)
    {
        case SettingKind::Bool:
            doc[field.key] = fieldAtConst<bool>(settings, field.offset);
            break;
        case SettingKind::Int:
            doc[field.key] = fieldAtConst<int>(settings, field.offset);
            break;
        case SettingKind::Float:
        {
            float val = fieldAtConst<float>(settings, field.offset);
            // mm_per_pulse needs 4 decimals for calibration precision, others need 2
            if (strcmp(field.key, "movement_mm_per_pulse") == 0) {
                doc[field.key] = roundf(val * 10000.0f) / 10000.0f;
            } else {
                doc[field.key] = roundf(val * 100.0f) / 100.0f;
            }
            break;
        }
        case SettingKind::String:
            doc[field.key] = fieldAtConst<String>(settings, field.offset);
            break;
    }
}
}  // namespace

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
    settings.detection_ratio_threshold  = 25;     // 25% passing threshold (~75% deficit)
    settings.detection_hard_jam_mm      = 5.0f;   // 5mm expected with zero movement = hard jam
    settings.detection_soft_jam_time_ms = 7000;   // 7 seconds to signal slow clog (balanced for quick detection)
    settings.detection_hard_jam_time_ms = 3000;   // 3 seconds of negligible flow (quick response to complete jams)
    settings.detection_mode = 0;                  // 0 = both hard + soft detection
    settings.sdcp_loss_behavior         = 2;
    settings.flow_telemetry_stale_ms    = 1000;
    settings.ui_refresh_interval_ms     = 1000;
    settings.log_level                  = 0;      // Default to Normal logging
    settings.suppress_pause_commands    = false;  // Pause commands enabled by default
    settings.movement_mm_per_pulse      = 2.88f;  // Actual sensor spec (2.88mm per pulse)
    settings.auto_calibrate_sensor      = false;  // Disabled by default
    settings.pulse_reduction_percent    = 100.0f;  // Default: no reduction
    settings.test_recording_mode        = false;
    settings.show_debug_page            = false;
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

    // JSON allocation: 1152 bytes stack (descriptor-driven, measured <900 bytes used)
    StaticJsonDocument<SETTINGS_JSON_CAPACITY> doc;
    DeserializationError                          error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        logger.log("Settings JSON parsing error, using defaults");
        isLoaded = true;
        return false;
    }

    for (const auto& field : kSettingFields)
    {
        JsonVariantConst value = doc[field.key];
        if (value.isNull())
        {
            applyDefault(field, settings);
            continue;
        }
        applyVariant(field, value, settings);
    }

    // Migration: handle legacy 0.0-1.0 float format for detection_ratio_threshold
    // Old format stored 0.40 for 40%, new format stores 40
    if (doc.containsKey("detection_ratio_threshold"))
    {
        float rawValue = doc["detection_ratio_threshold"].as<float>();
        if (rawValue > 0.0f && rawValue <= 1.0f)
        {
            // Legacy 0.0-1.0 format, convert to 0-100
            settings.detection_ratio_threshold = static_cast<int>(rawValue * 100.0f + 0.5f);
            logger.logf(LOG_NORMAL, "Migrated detection_ratio_threshold: %.2f -> %d%%",
                        rawValue, settings.detection_ratio_threshold);
        }
    }

    // Clamp to valid range (0=Normal, 1=Verbose, 2=Pin Values)
    if (settings.log_level < 0)
    {
        settings.log_level = 0;
    }
    else if (settings.log_level > 2)
    {
        settings.log_level = 2;
    }

    if (settings.detection_mode < 0)
    {
        settings.detection_mode = 0;
    }
    else if (settings.detection_mode > 2)
    {
        settings.detection_mode = 2;
    }

    // Update logger with loaded log level
    logger.setLogLevel(static_cast<LogLevel>(settings.log_level));

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
    // Stored as 0-100 int, returned as 0.0-1.0 float for JamDetector compatibility
    return getSettings().detection_ratio_threshold / 100.0f;
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

int SettingsManager::getDetectionMode()
{
    return getSettings().detection_mode;
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

float SettingsManager::getPulseReductionPercent()
{
    return getSettings().pulse_reduction_percent;
}

bool SettingsManager::getTestRecordingMode()
{
    return getSettings().test_recording_mode;
}

bool SettingsManager::getShowDebugPage()
{
    return getSettings().show_debug_page;
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

void SettingsManager::setDetectionRatioThreshold(int thresholdPercent)
{
    if (!isLoaded)
        load();
    // Clamp to valid range (0-100%)
    if (thresholdPercent < 0) thresholdPercent = 0;
    if (thresholdPercent > 100) thresholdPercent = 100;
    settings.detection_ratio_threshold = thresholdPercent;
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

void SettingsManager::setDetectionMode(int mode)
{
    if (!isLoaded)
        load();
    if (mode < 0)
    {
        mode = 0;
    }
    else if (mode > 2)
    {
        mode = 2;
    }
    settings.detection_mode = mode;
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

void SettingsManager::setPulseReductionPercent(float percent)
{
    if (!isLoaded)
        load();
    // Clamp value between 0 and 100
    if (percent < 0.0f)
        percent = 0.0f;
    else if (percent > 100.0f)
        percent = 100.0f;
    settings.pulse_reduction_percent = percent;
}

void SettingsManager::setTestRecordingMode(bool enabled)
{
    if (!isLoaded)
        load();
    settings.test_recording_mode = enabled;
}

void SettingsManager::setShowDebugPage(bool show)
{
    if (!isLoaded)
        load();
    settings.show_debug_page = show;
}

String SettingsManager::toJson(bool includePassword)
{
    String output;
    output.reserve(SETTINGS_JSON_CAPACITY);

    StaticJsonDocument<SETTINGS_JSON_CAPACITY> doc;
    for (const auto& field : kSettingFields)
    {
        serializeField(field, doc, settings, includePassword);
    }

    serializeJson(doc, output);

    // Pin Values level: Check if approaching allocation limit
    if (getLogLevel() >= LOG_PIN_VALUES)
    {
        size_t actualSize = measureJson(doc);
        size_t warnThreshold = static_cast<size_t>(SETTINGS_JSON_CAPACITY * 0.85f);
        if (actualSize > warnThreshold)
        {
            logger.logf(LOG_PIN_VALUES,
                        "SettingsManager toJson size: %zu / %u bytes (%.1f%%)",
                        actualSize, (unsigned)SETTINGS_JSON_CAPACITY,
                        (actualSize * 100.0f / SETTINGS_JSON_CAPACITY));
        }
    }

    return output;
}
