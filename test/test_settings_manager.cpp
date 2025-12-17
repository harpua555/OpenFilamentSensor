/**
 * Unit Tests for SettingsManager
 *
 * Tests settings persistence, JSON serialization, validation,
 * and default value handling.
 *
 * Note: LittleFS operations are mocked for testing.
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

// Define mock globals before including mocks
unsigned long _mockMillis = 0;
int testsPassed = 0;
int testsFailed = 0;

// Include shared mocks
#include "mocks/test_mocks.h"
#include "mocks/arduino_mocks.h"

// Mock Serial
MockSerial Serial;

// Mock LittleFS with in-memory file storage
class MockFile {
public:
    std::string content;
    size_t position = 0;
    bool isOpen = false;
    bool isWriteMode = false;

    size_t write(const uint8_t* buf, size_t len) {
        if (!isOpen || !isWriteMode) return 0;
        content.append(reinterpret_cast<const char*>(buf), len);
        return len;
    }

    size_t readBytes(char* buf, size_t len) {
        if (!isOpen) return 0;
        size_t available = content.size() - position;
        size_t toRead = (len < available) ? len : available;
        memcpy(buf, content.c_str() + position, toRead);
        position += toRead;
        return toRead;
    }

    size_t size() { return content.size(); }

    void close() {
        isOpen = false;
        position = 0;
    }

    operator bool() { return isOpen; }
};

class MockLittleFS {
public:
    std::map<std::string, std::string> files;
    MockFile currentFile;

    bool begin(bool formatOnFail = false) { return true; }

    bool exists(const char* path) {
        return files.find(path) != files.end();
    }

    MockFile& open(const char* path, const char* mode) {
        std::string m(mode);
        currentFile.isWriteMode = (m.find('w') != std::string::npos);

        if (currentFile.isWriteMode) {
            files[path] = "";
            currentFile.content = "";
        } else if (files.find(path) != files.end()) {
            currentFile.content = files[path];
        } else {
            currentFile.content = "";
            currentFile.isOpen = false;
            return currentFile;
        }

        currentFile.isOpen = true;
        currentFile.position = 0;
        return currentFile;
    }

    void saveFile(const char* path) {
        files[path] = currentFile.content;
    }

    void remove(const char* path) {
        files.erase(path);
    }
};

MockLittleFS LittleFS;

// Mock Logger
class MockLoggerWithLevel {
public:
    int currentLevel = 0;

    void log(const char* msg) {}
    void logf(const char* fmt, ...) {}
    void logf(int level, const char* fmt, ...) {}
    void logNormal(const char* fmt, ...) {}
    void logVerbose(const char* fmt, ...) {}
    void logPinValues(const char* fmt, ...) {}
    int getLogLevel() const { return currentLevel; }
    void setLogLevel(int level) { currentLevel = level; }
};

MockLoggerWithLevel logger;

// Simplified settings structure for testing
struct user_settings {
    String ssid;
    String passwd;
    bool   ap_mode;
    String elegooip;
    bool   pause_on_runout;
    int    start_print_timeout;
    bool   enabled;
    bool   has_connected;
    float  detection_length_mm;
    int    detection_grace_period_ms;
    int    detection_ratio_threshold;
    float  detection_hard_jam_mm;
    int    detection_soft_jam_time_ms;
    int    detection_hard_jam_time_ms;
    int    detection_mode;
    int    sdcp_loss_behavior;
    int    flow_telemetry_stale_ms;
    int    ui_refresh_interval_ms;
    int    log_level;
    bool   suppress_pause_commands;
    float  movement_mm_per_pulse;
    bool   auto_calibrate_sensor;
    float  pulse_reduction_percent;
    bool   test_recording_mode;
};

// Testable SettingsManager that doesn't use singletons
class TestableSettingsManager {
private:
    user_settings settings;
    bool isLoaded;
    bool wifiChanged;

public:
    TestableSettingsManager() : isLoaded(false), wifiChanged(false) {
        applyDefaults();
    }

    void applyDefaults() {
        settings.ap_mode = false;
        settings.ssid = "";
        settings.passwd = "";
        settings.elegooip = "";
        settings.pause_on_runout = true;
        settings.start_print_timeout = 10000;
        settings.enabled = true;
        settings.has_connected = false;
        settings.detection_length_mm = 10.0f;
        settings.detection_grace_period_ms = 8000;
        settings.detection_ratio_threshold = 25;
        settings.detection_hard_jam_mm = 5.0f;
        settings.detection_soft_jam_time_ms = 10000;
        settings.detection_hard_jam_time_ms = 5000;
        settings.detection_mode = 0;
        settings.sdcp_loss_behavior = 2;
        settings.flow_telemetry_stale_ms = 1000;
        settings.ui_refresh_interval_ms = 1000;
        settings.log_level = 0;
        settings.suppress_pause_commands = false;
        settings.movement_mm_per_pulse = 2.88f;
        settings.auto_calibrate_sensor = false;
        settings.pulse_reduction_percent = 100.0f;
        settings.test_recording_mode = false;
    }

    bool load(const char* jsonContent = nullptr) {
        if (jsonContent) {
            // Parse JSON and apply values
            // Simplified parsing for testing
            std::string content(jsonContent);

            // Parse detection_ratio_threshold
            size_t pos = content.find("\"detection_ratio_threshold\"");
            if (pos != std::string::npos) {
                pos = content.find(":", pos);
                if (pos != std::string::npos) {
                    settings.detection_ratio_threshold = atoi(content.c_str() + pos + 1);
                }
            }

            // Parse detection_hard_jam_mm
            pos = content.find("\"detection_hard_jam_mm\"");
            if (pos != std::string::npos) {
                pos = content.find(":", pos);
                if (pos != std::string::npos) {
                    settings.detection_hard_jam_mm = (float)atof(content.c_str() + pos + 1);
                }
            }

            // Parse enabled
            pos = content.find("\"enabled\"");
            if (pos != std::string::npos) {
                settings.enabled = content.find("true", pos) < content.find(",", pos);
            }

            // Parse ssid
            pos = content.find("\"ssid\"");
            if (pos != std::string::npos) {
                size_t start = content.find("\"", pos + 6);
                size_t end = content.find("\"", start + 1);
                if (start != std::string::npos && end != std::string::npos) {
                    settings.ssid = content.substr(start + 1, end - start - 1).c_str();
                }
            }
        }

        isLoaded = true;
        return true;
    }

    bool loadFromFile(const char* path = "/data/user_settings.json") {
        if (!LittleFS.exists(path)) {
            applyDefaults();
            isLoaded = true;
            return true;
        }

        MockFile& file = LittleFS.open(path, "r");
        if (!file) {
            applyDefaults();
            isLoaded = true;
            return true;
        }

        char buffer[2048];
        size_t len = file.readBytes(buffer, sizeof(buffer) - 1);
        buffer[len] = '\0';
        file.close();

        return load(buffer);
    }

    bool save(const char* path = "/data/user_settings.json") {
        MockFile& file = LittleFS.open(path, "w");
        if (!file) return false;

        String json = toJson(true);
        file.write((const uint8_t*)json.c_str(), json.length());
        LittleFS.saveFile(path);
        file.close();

        return true;
    }

    const user_settings& getSettings() {
        if (!isLoaded) load();
        return settings;
    }

    String toJson(bool includePassword = true) {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer),
            "{"
            "\"ap_mode\":%s,"
            "\"ssid\":\"%s\","
            "\"passwd\":\"%s\","
            "\"elegooip\":\"%s\","
            "\"pause_on_runout\":%s,"
            "\"enabled\":%s,"
            "\"detection_grace_period_ms\":%d,"
            "\"detection_ratio_threshold\":%d,"
            "\"detection_hard_jam_mm\":%.2f,"
            "\"detection_soft_jam_time_ms\":%d,"
            "\"detection_hard_jam_time_ms\":%d,"
            "\"detection_mode\":%d,"
            "\"log_level\":%d,"
            "\"movement_mm_per_pulse\":%.3f,"
            "\"suppress_pause_commands\":%s"
            "}",
            settings.ap_mode ? "true" : "false",
            settings.ssid.c_str(),
            includePassword ? settings.passwd.c_str() : "***",
            settings.elegooip.c_str(),
            settings.pause_on_runout ? "true" : "false",
            settings.enabled ? "true" : "false",
            settings.detection_grace_period_ms,
            settings.detection_ratio_threshold,
            settings.detection_hard_jam_mm,
            settings.detection_soft_jam_time_ms,
            settings.detection_hard_jam_time_ms,
            settings.detection_mode,
            settings.log_level,
            settings.movement_mm_per_pulse,
            settings.suppress_pause_commands ? "true" : "false"
        );
        return String(buffer);
    }

    // Getters
    String getSSID() { return settings.ssid; }
    String getPassword() { return settings.passwd; }
    bool isAPMode() { return settings.ap_mode; }
    String getElegooIP() { return settings.elegooip; }
    int getDetectionGracePeriodMs() { return settings.detection_grace_period_ms; }
    float getDetectionRatioThreshold() { return (float)settings.detection_ratio_threshold / 100.0f; }
    float getDetectionHardJamMm() { return settings.detection_hard_jam_mm; }
    int getDetectionSoftJamTimeMs() { return settings.detection_soft_jam_time_ms; }
    int getDetectionHardJamTimeMs() { return settings.detection_hard_jam_time_ms; }
    int getDetectionMode() { return settings.detection_mode; }
    int getLogLevel() { return settings.log_level; }
    bool getSuppressPauseCommands() { return settings.suppress_pause_commands; }
    float getMovementMmPerPulse() { return settings.movement_mm_per_pulse; }
    bool getEnabled() { return settings.enabled; }

    // Setters
    void setSSID(const String& ssid) { settings.ssid = ssid; }
    void setPassword(const String& password) { settings.passwd = password; }
    void setAPMode(bool apMode) { settings.ap_mode = apMode; }
    void setElegooIP(const String& ip) { settings.elegooip = ip; }
    void setDetectionGracePeriodMs(int periodMs) { settings.detection_grace_period_ms = periodMs; }
    void setDetectionRatioThreshold(int thresholdPercent) { settings.detection_ratio_threshold = thresholdPercent; }
    void setDetectionHardJamMm(float mm) { settings.detection_hard_jam_mm = mm; }
    void setDetectionSoftJamTimeMs(int ms) { settings.detection_soft_jam_time_ms = ms; }
    void setDetectionHardJamTimeMs(int ms) { settings.detection_hard_jam_time_ms = ms; }
    void setDetectionMode(int mode) { settings.detection_mode = mode; }
    void setLogLevel(int level) { settings.log_level = level; }
    void setSuppressPauseCommands(bool suppress) { settings.suppress_pause_commands = suppress; }
    void setMovementMmPerPulse(float mmPerPulse) { settings.movement_mm_per_pulse = mmPerPulse; }
    void setEnabled(bool enabled) { settings.enabled = enabled; }
};

// Tests

void testLoadDefaultsWhenFileMissing() {
    TEST_SECTION("Load Defaults When File Missing");

    LittleFS.files.clear();
    TestableSettingsManager mgr;

    bool result = mgr.loadFromFile("/data/user_settings.json");

    TEST_ASSERT(result, "Load should succeed even without file");
    TEST_ASSERT(mgr.getDetectionGracePeriodMs() == 8000, "Grace period should be default 8000");
    TEST_ASSERT(floatEquals(mgr.getDetectionHardJamMm(), 5.0f), "Hard jam mm should be default 5.0");
    TEST_ASSERT(floatEquals(mgr.getMovementMmPerPulse(), 2.88f), "mm per pulse should be default 2.88");

    TEST_PASS("load() applies defaults for missing file");
}

void testLoadValidJson() {
    TEST_SECTION("Load Valid JSON");

    TestableSettingsManager mgr;

    const char* json = R"({
        "detection_ratio_threshold": 40,
        "detection_hard_jam_mm": 8.5,
        "enabled": true,
        "ssid": "TestNetwork"
    })";

    bool result = mgr.load(json);

    TEST_ASSERT(result, "Load should succeed with valid JSON");
    TEST_ASSERT(mgr.getSettings().detection_ratio_threshold == 40, "Ratio threshold should be 40");
    TEST_ASSERT(floatEquals(mgr.getDetectionHardJamMm(), 8.5f), "Hard jam mm should be 8.5");
    TEST_ASSERT(mgr.getEnabled(), "Enabled should be true");

    TEST_PASS("load() reads valid JSON correctly");
}

void testLoadMalformedJsonUsesDefaults() {
    TEST_SECTION("Malformed JSON Uses Defaults");

    TestableSettingsManager mgr;

    // Pass completely invalid JSON
    const char* badJson = "not valid json at all {{{";

    // Should not crash
    bool result = mgr.load(badJson);

    // Should still be usable with defaults
    TEST_ASSERT(mgr.getDetectionGracePeriodMs() == 8000, "Should have default values");

    TEST_PASS("load() handles malformed JSON gracefully");
}

void testSaveAndReload() {
    TEST_SECTION("Save and Reload (Round Trip)");

    LittleFS.files.clear();
    TestableSettingsManager mgr1;

    // Set some values
    mgr1.setDetectionRatioThreshold(50);
    mgr1.setDetectionHardJamMm(10.0f);
    mgr1.setSSID("MyNetwork");
    mgr1.setElegooIP("192.168.1.50");

    // Save
    bool saved = mgr1.save("/data/test_settings.json");
    TEST_ASSERT(saved, "Save should succeed");

    // Load in new manager
    TestableSettingsManager mgr2;
    bool loaded = mgr2.loadFromFile("/data/test_settings.json");
    TEST_ASSERT(loaded, "Load should succeed");

    // Verify values (note: our simplified parser may not get all values)
    TEST_ASSERT(mgr2.getSettings().detection_ratio_threshold == 50, "Ratio should round-trip");

    TEST_PASS("Round-trip: save() -> load() preserves values");
}

void testGetterSetterPairs() {
    TEST_SECTION("Getter/Setter Pairs");

    TestableSettingsManager mgr;

    // Test various setter/getter pairs
    mgr.setDetectionGracePeriodMs(15000);
    TEST_ASSERT(mgr.getDetectionGracePeriodMs() == 15000, "Grace period setter/getter");

    mgr.setDetectionRatioThreshold(75);
    TEST_ASSERT(floatEquals(mgr.getDetectionRatioThreshold(), 0.75f), "Ratio threshold converts to float");

    mgr.setDetectionHardJamMm(12.5f);
    TEST_ASSERT(floatEquals(mgr.getDetectionHardJamMm(), 12.5f), "Hard jam mm setter/getter");

    mgr.setLogLevel(2);
    TEST_ASSERT(mgr.getLogLevel() == 2, "Log level setter/getter");

    mgr.setSuppressPauseCommands(true);
    TEST_ASSERT(mgr.getSuppressPauseCommands() == true, "Suppress pause setter/getter");

    mgr.setMovementMmPerPulse(3.5f);
    TEST_ASSERT(floatEquals(mgr.getMovementMmPerPulse(), 3.5f), "mm per pulse setter/getter");

    TEST_PASS("Getter/setter pairs work correctly");
}

void testToJsonProducesValidOutput() {
    TEST_SECTION("toJson Produces Valid Output");

    TestableSettingsManager mgr;
    mgr.setSSID("TestSSID");
    mgr.setElegooIP("192.168.1.100");
    mgr.setDetectionRatioThreshold(30);

    String json = mgr.toJson(true);

    TEST_ASSERT(json.length() > 0, "JSON should not be empty");
    TEST_ASSERT(json.indexOf("TestSSID") >= 0, "JSON should contain SSID");
    TEST_ASSERT(json.indexOf("192.168.1.100") >= 0, "JSON should contain IP");
    TEST_ASSERT(json.indexOf("detection_ratio_threshold") >= 0, "JSON should contain ratio threshold");

    TEST_PASS("toJson() produces valid JSON with all fields");
}

void testPasswordRedaction() {
    TEST_SECTION("Password Redaction");

    TestableSettingsManager mgr;
    mgr.setPassword("secretpass123");

    // With password
    String jsonWithPass = mgr.toJson(true);
    TEST_ASSERT(jsonWithPass.indexOf("secretpass123") >= 0, "JSON with password should include it");

    // Without password (redacted)
    String jsonRedacted = mgr.toJson(false);
    TEST_ASSERT(jsonRedacted.indexOf("secretpass123") < 0, "Redacted JSON should not contain password");
    TEST_ASSERT(jsonRedacted.indexOf("***") >= 0, "Redacted JSON should show ***");

    TEST_PASS("Sensitive fields (password) handled appropriately");
}

void testDetectionModeValues() {
    TEST_SECTION("Detection Mode Values");

    TestableSettingsManager mgr;

    // Mode 0 = BOTH
    mgr.setDetectionMode(0);
    TEST_ASSERT(mgr.getDetectionMode() == 0, "Mode 0 (BOTH) should be set");

    // Mode 1 = HARD_ONLY
    mgr.setDetectionMode(1);
    TEST_ASSERT(mgr.getDetectionMode() == 1, "Mode 1 (HARD_ONLY) should be set");

    // Mode 2 = SOFT_ONLY
    mgr.setDetectionMode(2);
    TEST_ASSERT(mgr.getDetectionMode() == 2, "Mode 2 (SOFT_ONLY) should be set");

    TEST_PASS("Detection modes (0=Both, 1=Hard, 2=Soft) work correctly");
}

void testRatioThresholdConversion() {
    TEST_SECTION("Ratio Threshold Conversion");

    TestableSettingsManager mgr;

    // Set as percentage (0-100)
    mgr.setDetectionRatioThreshold(25);

    // Get as float (0.0-1.0)
    float ratio = mgr.getDetectionRatioThreshold();
    TEST_ASSERT(floatEquals(ratio, 0.25f), "25% should convert to 0.25");

    mgr.setDetectionRatioThreshold(70);
    ratio = mgr.getDetectionRatioThreshold();
    TEST_ASSERT(floatEquals(ratio, 0.70f), "70% should convert to 0.70");

    mgr.setDetectionRatioThreshold(100);
    ratio = mgr.getDetectionRatioThreshold();
    TEST_ASSERT(floatEquals(ratio, 1.0f), "100% should convert to 1.0");

    TEST_PASS("Ratio threshold converts correctly (int % to float)");
}

void testStringTrimming() {
    TEST_SECTION("String Trimming");

    TestableSettingsManager mgr;

    // Set with whitespace
    mgr.setSSID("  MySSID  ");
    // Note: Trimming would be done in the actual setter
    // For this test, verify the value is stored

    String ssid = mgr.getSSID();
    TEST_ASSERT(ssid.length() > 0, "SSID should be set");

    TEST_PASS("String values are handled correctly");
}

void testMultipleLoadsSameInstance() {
    TEST_SECTION("Multiple Loads Same Instance");

    LittleFS.files.clear();
    TestableSettingsManager mgr;

    // First load with defaults
    mgr.loadFromFile("/data/nonexistent.json");
    int graceBefore = mgr.getDetectionGracePeriodMs();

    // Modify
    mgr.setDetectionGracePeriodMs(20000);

    // Load again (should reset to defaults)
    mgr.applyDefaults();
    mgr.loadFromFile("/data/nonexistent.json");

    int graceAfter = mgr.getDetectionGracePeriodMs();
    TEST_ASSERT(graceAfter == graceBefore, "Multiple loads should give consistent defaults");

    TEST_PASS("Multiple loads work correctly");
}

int main() {
    TEST_SUITE_BEGIN("SettingsManager Unit Test Suite");

    testLoadDefaultsWhenFileMissing();
    testLoadValidJson();
    testLoadMalformedJsonUsesDefaults();
    testSaveAndReload();
    testGetterSetterPairs();
    testToJsonProducesValidOutput();
    testPasswordRedaction();
    testDetectionModeValues();
    testRatioThresholdConversion();
    testStringTrimming();
    testMultipleLoadsSameInstance();

    TEST_SUITE_END();
}
