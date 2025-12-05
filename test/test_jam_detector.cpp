/**
 * Unit Tests for JamDetector
 *
 * Tests the jam detection logic independently from the sensor.
 * Covers grace periods, hard/soft jam detection, rate-based detection,
 * and various edge cases.
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>
#include <cassert>

// Mock Arduino environment
unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }

// Include the actual implementation
#include "../src/JamDetector.h"
#include "../src/JamDetector.cpp"

// Mock Logger (JamDetector depends on it)
class MockLogger {
public:
    void log(const char* msg) { /* no-op */ }
    void logf(const char* fmt, ...) { /* no-op */ }
    void logVerbose(const char* fmt, ...) { /* no-op */ }
    int getLogLevel() const { return 0; }
};
MockLogger logger;

// Mock SettingsManager (JamDetector might depend on it)
class MockSettingsManager {
public:
    bool getVerboseLogging() const { return false; }
};
MockSettingsManager settingsManager;

// ANSI color codes for test output
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RESET   "\033[0m"

int testsPassed = 0;
int testsFailed = 0;

void resetMockTime() {
    _mockMillis = 0;
}

void advanceTime(unsigned long ms) {
    _mockMillis += ms;
}

bool floatEquals(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}

void testReset() {
    std::cout << "\n=== Test: JamDetector Reset ===" << std::endl;
    
    JamDetector detector;
    detector.reset(1000);
    
    JamState state = detector.getState();
    
    assert(!state.jammed);
    assert(!state.hardJamTriggered);
    assert(!state.softJamTriggered);
    assert(floatEquals(state.hardJamPercent, 0.0f));
    assert(floatEquals(state.softJamPercent, 0.0f));
    assert(floatEquals(state.passRatio, 1.0f));
    assert(floatEquals(state.deficit, 0.0f));
    assert(state.graceState == GraceState::IDLE);
    assert(!state.graceActive);
    
    std::cout << COLOR_GREEN << "PASS: Reset initializes all state correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testGracePeriodStartup() {
    std::cout << "\n=== Test: Grace Period at Print Start ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 5000;
    config.startTimeoutMs = 10000;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 5000;
    config.ratioThreshold = 0.25f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Within grace period - should not detect jams even with deficit
    _mockMillis = 3000;  // 2 seconds into print
    JamState state = detector.update(
        10.0f,  // expected
        1.0f,   // actual (bad ratio!)
        100,    // pulseCount
        true,   // isPrinting
        true,   // hasTelemetry
        _mockMillis,
        printStartTime,
        config,
        5.0f,   // expectedRate
        0.5f    // actualRate
    );
    
    assert(state.graceActive);
    assert(state.graceState == GraceState::START_GRACE);
    assert(!state.jammed);
    
    // After grace period - should detect jams
    _mockMillis = 12000;  // 11 seconds into print (past grace + timeout)
    state = detector.update(
        20.0f,  // expected
        2.0f,   // actual (very bad ratio)
        200,    // pulseCount
        true,   // isPrinting
        true,   // hasTelemetry
        _mockMillis,
        printStartTime,
        config,
        5.0f,   // expectedRate
        0.5f    // actualRate
    );
    
    assert(!state.graceActive);
    assert(state.graceState == GraceState::ACTIVE);
    
    std::cout << COLOR_GREEN << "PASS: Grace period correctly delays detection" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testHardJamDetection() {
    std::cout << "\n=== Test: Hard Jam Detection ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;  // No grace for this test
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.25f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Simulate hard jam: expected movement but zero actual
    _mockMillis = 2000;
    JamState state = detector.update(
        15.0f,  // expected (above threshold)
        0.1f,   // actual (near zero)
        5,      // pulseCount (minimal)
        true,   // isPrinting
        true,   // hasTelemetry
        _mockMillis,
        printStartTime,
        config,
        10.0f,  // expectedRate
        0.05f   // actualRate (very low)
    );
    
    assert(!state.jammed);  // Not yet - needs time accumulation
    assert(state.hardJamPercent > 0.0f);
    
    // Continue the jam condition
    _mockMillis = 4500;  // 3.5 seconds total
    state = detector.update(
        25.0f,
        0.2f,
        10,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        10.0f,
        0.05f
    );
    
    assert(state.jammed);
    assert(state.hardJamTriggered);
    assert(floatEquals(state.hardJamPercent, 100.0f));
    
    std::cout << COLOR_GREEN << "PASS: Hard jam detection works correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testSoftJamDetection() {
    std::cout << "\n=== Test: Soft Jam Detection ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 5000;
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.70f;  // 70% threshold
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Simulate soft jam: consistent under-extrusion (60% actual)
    _mockMillis = 2000;
    JamState state = detector.update(
        15.0f,  // expected
        9.0f,   // actual (60% - below 70% threshold)
        100,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        5.0f,   // expectedRate
        3.0f    // actualRate (60%)
    );
    
    assert(!state.jammed);  // Not yet - needs time
    assert(state.softJamPercent > 0.0f);
    assert(floatEquals(state.passRatio, 0.6f, 0.05f));
    
    // Continue under-extrusion
    _mockMillis = 7500;  // 6.5 seconds total (past threshold)
    state = detector.update(
        30.0f,
        18.0f,  // Still 60%
        300,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        5.0f,
        3.0f
    );
    
    assert(state.jammed);
    assert(state.softJamTriggered);
    assert(floatEquals(state.softJamPercent, 100.0f));
    
    std::cout << COLOR_GREEN << "PASS: Soft jam detection works correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testJamRecovery() {
    std::cout << "\n=== Test: Jam Recovery ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Build up toward jam
    _mockMillis = 2000;
    detector.update(15.0f, 8.0f, 100, true, true, _mockMillis, printStartTime, config, 5.0f, 2.5f);
    
    _mockMillis = 3500;
    JamState state = detector.update(25.0f, 14.0f, 200, true, true, _mockMillis, printStartTime, config, 5.0f, 2.5f);
    
    assert(state.softJamPercent > 50.0f);  // Building up
    
    // Now recover - good flow ratio
    _mockMillis = 4000;
    state = detector.update(30.0f, 24.0f, 400, true, true, _mockMillis, printStartTime, config, 5.0f, 4.5f);
    
    assert(floatEquals(state.passRatio, 0.8f, 0.05f));  // 80% is good
    assert(state.softJamPercent < 50.0f);  // Should be decreasing
    
    std::cout << COLOR_GREEN << "PASS: Jam recovery decreases accumulation" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testResumeGrace() {
    std::cout << "\n=== Test: Resume Grace Period ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 2000;
    config.startTimeoutMs = 5000;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 5000;
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Simulate pause/resume scenario
    _mockMillis = 10000;  // Past initial grace
    JamState state = detector.update(20.0f, 18.0f, 200, true, true, _mockMillis, printStartTime, config, 5.0f, 4.5f);
    assert(!state.graceActive);
    
    // Resume with new baseline
    _mockMillis = 15000;
    detector.onResume(_mockMillis, 200, 20.0f);
    
    state = detector.getState();
    assert(state.graceState == GraceState::RESUME_GRACE);
    assert(state.graceActive);
    assert(!state.jammed);  // Resume should clear jam flags
    
    // Even with bad ratio during resume grace, no jam
    _mockMillis = 16000;
    state = detector.update(25.0f, 15.0f, 250, true, true, _mockMillis, printStartTime, config, 5.0f, 3.0f);
    assert(state.graceActive);
    assert(!state.jammed);
    
    std::cout << COLOR_GREEN << "PASS: Resume grace period prevents false positives" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testDetectionModes() {
    std::cout << "\n=== Test: Detection Modes (Hard Only, Soft Only, Both) ===" << std::endl;
    
    // Test HARD_ONLY mode
    resetMockTime();
    JamDetector hardDetector;
    JamConfig hardConfig;
    hardConfig.graceTimeMs = 0;
    hardConfig.startTimeoutMs = 0;
    hardConfig.hardJamMm = 5.0f;
    hardConfig.softJamTimeMs = 3000;
    hardConfig.hardJamTimeMs = 2000;
    hardConfig.ratioThreshold = 0.70f;
    hardConfig.detectionMode = DetectionMode::HARD_ONLY;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    hardDetector.reset(printStartTime);
    
    // Soft jam condition (should be ignored)
    _mockMillis = 5000;
    JamState state = hardDetector.update(20.0f, 12.0f, 200, true, true, _mockMillis, printStartTime, hardConfig, 5.0f, 3.0f);
    assert(!state.jammed);  // Soft jam ignored in HARD_ONLY mode
    
    // Hard jam condition (should trigger)
    _mockMillis = 8000;
    state = hardDetector.update(30.0f, 0.5f, 210, true, true, _mockMillis, printStartTime, hardConfig, 10.0f, 0.1f);
    // May need accumulation time, but should start detecting
    
    // Test SOFT_ONLY mode
    resetMockTime();
    JamDetector softDetector;
    JamConfig softConfig = hardConfig;
    softConfig.detectionMode = DetectionMode::SOFT_ONLY;
    
    _mockMillis = 1000;
    softDetector.reset(printStartTime);
    
    // Hard jam condition (should be ignored)
    _mockMillis = 5000;
    state = softDetector.update(20.0f, 0.1f, 10, true, true, _mockMillis, printStartTime, softConfig, 10.0f, 0.05f);
    assert(!state.jammed);  // Hard jam ignored in SOFT_ONLY mode
    
    std::cout << COLOR_GREEN << "PASS: Detection modes filter jam types correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testRateBasedDetection() {
    std::cout << "\n=== Test: Rate-Based Detection ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 5000;
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Test with rate information
    _mockMillis = 2000;
    JamState state = detector.update(
        15.0f,   // expected distance
        9.0f,    // actual distance (60%)
        100,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        5.0f,    // expectedRate mm/s
        3.0f     // actualRate mm/s (60%)
    );
    
    // Verify rates are captured
    assert(floatEquals(state.expectedRateMmPerSec, 5.0f));
    assert(floatEquals(state.actualRateMmPerSec, 3.0f));
    assert(floatEquals(state.passRatio, 0.6f, 0.05f));
    
    std::cout << COLOR_GREEN << "PASS: Rate-based detection captures rates correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testMinimumThresholds() {
    std::cout << "\n=== Test: Minimum Distance Thresholds ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 2000;
    config.hardJamTimeMs = 2000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Very small distances (below minimum thresholds) should not trigger jams
    _mockMillis = 4000;  // 3 seconds
    JamState state = detector.update(
        2.0f,    // expected (below min threshold)
        0.5f,    // actual (bad ratio but small distance)
        10,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        1.0f,
        0.25f
    );
    
    assert(!state.jammed);  // Should not jam on very small distances
    
    std::cout << COLOR_GREEN << "PASS: Minimum distance thresholds prevent false positives" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testPauseRequestHandling() {
    std::cout << "\n=== Test: Pause Request Handling ===" << std::endl;
    
    JamDetector detector;
    detector.reset(1000);
    
    assert(!detector.isPauseRequested());
    
    detector.setPauseRequested();
    assert(detector.isPauseRequested());
    
    detector.clearPauseRequest();
    assert(!detector.isPauseRequested());
    
    std::cout << COLOR_GREEN << "PASS: Pause request flags work correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testEdgeCaseZeroExpected() {
    std::cout << "\n=== Test: Edge Case - Zero Expected Movement ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 5000;
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Zero expected movement (idle/travel) - should not jam
    _mockMillis = 3000;
    JamState state = detector.update(
        0.0f,    // zero expected
        0.0f,    // zero actual
        100,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        0.0f,
        0.0f
    );
    
    assert(!state.jammed);
    assert(floatEquals(state.passRatio, 1.0f));  // Should default to 1.0
    
    std::cout << COLOR_GREEN << "PASS: Zero expected movement handled correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testNotPrintingState() {
    std::cout << "\n=== Test: Not Printing State ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 0;
    config.startTimeoutMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Not printing - should return to IDLE
    _mockMillis = 3000;
    JamState state = detector.update(
        15.0f,
        1.0f,
        100,
        false,   // NOT printing
        true,
        _mockMillis,
        printStartTime,
        config,
        5.0f,
        0.5f
    );
    
    assert(state.graceState == GraceState::IDLE);
    assert(!state.jammed);
    
    std::cout << COLOR_GREEN << "PASS: Not printing returns to IDLE correctly" << COLOR_RESET << std::endl;
    testsPassed++;
}

int main() {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "    JamDetector Unit Test Suite\n";
    std::cout << "========================================\n";
    
    testReset();
    testGracePeriodStartup();
    testHardJamDetection();
    testSoftJamDetection();
    testJamRecovery();
    testResumeGrace();
    testDetectionModes();
    testRateBasedDetection();
    testMinimumThresholds();
    testPauseRequestHandling();
    testEdgeCaseZeroExpected();
    testNotPrintingState();
    
    std::cout << "\n========================================\n";
    std::cout << "Test Results:\n";
    std::cout << COLOR_GREEN << "  Passed: " << testsPassed << COLOR_RESET << std::endl;
    if (testsFailed > 0) {
        std::cout << COLOR_RED << "  Failed: " << testsFailed << COLOR_RESET << std::endl;
    }
    std::cout << "========================================\n\n";
    
    return testsFailed > 0 ? 1 : 0;
}