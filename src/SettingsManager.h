#include <Arduino.h>
#include <ArduinoJson.h>

#ifndef SETTINGS_DATA_H
#define SETTINGS_DATA_H

struct user_settings
{
    String ssid;
    String passwd;
    bool   ap_mode;
    String elegooip;
    bool   pause_on_runout;
    int    start_print_timeout;
    bool   enabled;
    bool   has_connected;
    float  detection_length_mm;          // DEPRECATED: Use ratio-based detection instead
    int    detection_grace_period_ms;    // Grace period after move command before checking jams
    int    detection_ratio_threshold;    // Soft jam: pass ratio threshold (25 = 25% actual allowed before alert)
    float  detection_hard_jam_mm;        // Hard jam: mm expected with zero movement to trigger
      int    detection_soft_jam_time_ms;   // Soft jam: how long ratio must stay bad (ms, e.g., 3000 = 3 sec)
      int    detection_hard_jam_time_ms;   // Hard jam: how long zero movement required (ms, e.g., 2000 = 2 sec)
      int    detection_mode;               // 0=Soft+Hard, 1=Hard only, 2=Soft only
      int    sdcp_loss_behavior;
    int    flow_telemetry_stale_ms;
    int    ui_refresh_interval_ms;
    int    log_level;                 // 0=Normal, 1=Verbose, 2=Pin Values
    bool   suppress_pause_commands;   // Suppress pause/cancel commands (for testing/development)
    float  movement_mm_per_pulse;
    bool   auto_calibrate_sensor;  // Auto-calibrate mm_per_pulse at print end
    float  pulse_reduction_percent;  // Pulse reduction for testing (0-100, default 100)
    bool   test_recording_mode;    // Enable CSV test data recording to ./condensed directory
    bool   show_debug_page;        // Show Debug page in web UI (default false)
};

class SettingsManager
{
   private:
    user_settings settings;
    bool          isLoaded;
    bool          wifiChanged;

    SettingsManager();

    SettingsManager(const SettingsManager &)            = delete;
    SettingsManager &operator=(const SettingsManager &) = delete;

   public:
    static SettingsManager &getInstance();

    // Flag to request WiFi reconnection with new credentials
    bool requestWifiReconnect;

    bool load();
    bool save(bool skipWifiCheck = false);

    //  (loads if not already loaded)
    const user_settings &getSettings();

    String getSSID();
    String getPassword();
    bool   isAPMode();
    String getElegooIP();
    bool   getPauseOnRunout();
    int    getStartPrintTimeout();
    bool   getEnabled();
    bool   getHasConnected();
    float  getDetectionLengthMM();          // DEPRECATED: Use ratio-based detection
    int    getDetectionGracePeriodMs();     // Grace period for look-ahead moves
    float  getDetectionRatioThreshold();    // Soft jam ratio threshold (returns 0.0-1.0 for internal use)
      float  getDetectionHardJamMm();         // Hard jam threshold
      int    getDetectionSoftJamTimeMs();     // Soft jam duration threshold
      int    getDetectionHardJamTimeMs();     // Hard jam duration threshold
      int    getDetectionMode();              // Detection mode selector (0=both,1=hard,2=soft)
    int    getSdcpLossBehavior();
    int    getFlowTelemetryStaleMs();
    int    getUiRefreshIntervalMs();
    int    getLogLevel();                      // Get current log level (0-2)
    bool   getSuppressPauseCommands();         // Get pause command suppression state
    bool   getVerboseLogging();                // Returns true if log level >= 1
    bool   getFlowSummaryLogging();            // Returns true if log level >= 1
    bool   getPinDebugLogging();               // Returns true if log level >= 2
    float  getMovementMmPerPulse();
    bool   getAutoCalibrateSensor();
    float  getPulseReductionPercent();         // Get pulse reduction percentage (0-100)
    bool   getTestRecordingMode();             // Get test recording mode state
    bool   getShowDebugPage();                   // Get show debug page state

    void setSSID(const String &ssid);
    void setPassword(const String &password);
    void setAPMode(bool apMode);
    void setElegooIP(const String &ip);
    void setPauseOnRunout(bool pauseOnRunout);
    void setStartPrintTimeout(int timeoutMs);
    void setEnabled(bool enabled);
    void setHasConnected(bool hasConnected);
    void setDetectionLengthMM(float value);            // DEPRECATED: Use ratio-based detection
    void setDetectionGracePeriodMs(int periodMs);      // Grace period setter
    void setDetectionRatioThreshold(int thresholdPercent);  // Soft jam ratio threshold setter (0-100%)
    void setDetectionHardJamMm(float mmThreshold);     // Hard jam threshold setter
      void setDetectionSoftJamTimeMs(int timeMs);        // Soft jam duration setter
      void setDetectionHardJamTimeMs(int timeMs);        // Hard jam duration setter
      void setDetectionMode(int mode);                    // Detection mode selector
    void setSdcpLossBehavior(int behavior);
    void setFlowTelemetryStaleMs(int staleMs);
    void setUiRefreshIntervalMs(int intervalMs);
    void setLogLevel(int level);                   // Set log level (0-2), updates logger
    void setSuppressPauseCommands(bool suppress);  // Set pause command suppression
    void setMovementMmPerPulse(float mmPerPulse);
    void setAutoCalibrateSensor(bool autoCal);
    void setPulseReductionPercent(float percent);   // Set pulse reduction percentage (0-100)
    void setTestRecordingMode(bool enabled);       // Enable/disable test recording mode
    void setShowDebugPage(bool show);             // Show/hide debug page in web UI

    String toJson(bool includePassword = true);
};

#define settingsManager SettingsManager::getInstance()

#endif
