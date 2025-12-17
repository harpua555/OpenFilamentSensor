/**
 * Mock SettingsManager.h
 *
 * Intercepts #include "SettingsManager.h" for unit testing.
 * Provides default values for all settings.
 *
 * NOTE: Uses SETTINGS_DATA_H guard to match real header and prevent
 * redefinition when JamDetector.cpp includes the real SettingsManager.h
 */

#ifndef SETTINGS_DATA_H
#define SETTINGS_DATA_H

class SettingsManager {
public:
    static SettingsManager& getInstance() {
        static SettingsManager instance;
        return instance;
    }

    // Detection settings
    bool getVerboseLogging() const { return false; }
    bool getFlowSummaryLogging() const { return false; }
    bool getPinDebugLogging() const { return false; }
    int getLogLevel() const { return 0; }
    float getMovementMmPerPulse() const { return 2.88f; }
    int getDetectionGracePeriodMs() const { return 5000; }
    float getDetectionRatioThreshold() const { return 0.25f; }
    float getDetectionHardJamMm() const { return 5.0f; }
    int getDetectionSoftJamTimeMs() const { return 10000; }
    int getDetectionHardJamTimeMs() const { return 3000; }
    int getDetectionMode() const { return 0; }  // BOTH
    bool getSuppressPauseCommands() const { return false; }
    int getFlowTelemetryStaleMs() const { return 1500; }
    int getStartPrintTimeoutMs() const { return 10000; }
    bool getEnabled() const { return true; }
    float getDetectionLengthMm() const { return 10.0f; }

    // Network settings
    const char* getSSID() const { return "TestNetwork"; }
    const char* getPassword() const { return "password"; }
    bool isAPMode() const { return false; }
    const char* getElegooIP() const { return "192.168.1.100"; }

    // UI settings
    int getUIRefreshIntervalMs() const { return 1000; }

    // Calibration settings
    bool getAutoCalibrateSensor() const { return false; }
    float getPulseReductionPercent() const { return 100.0f; }
    float getPurgeFilamentMm() const { return 47.0f; }

    // Recording settings
    bool getTestRecordingMode() const { return false; }

    // UI visibility settings
    bool getShowDebugPage() const { return false; }

    // State
    bool getHasConnected() const { return false; }
    bool getPauseOnRunout() const { return true; }
    int getSDCPLossBehavior() const { return 2; }
};

// Global settingsManager instance - mirrors the pattern used in main.cpp
// JamDetector.cpp uses a global `settingsManager` reference
inline SettingsManager& settingsManager = SettingsManager::getInstance();

#endif // SETTINGS_DATA_H
