/**
 * JamDetector Unit Tests
 * 
 * Comprehensive tests for the JamDetector class covering:
 * - Grace period state transitions
 * - Hard jam detection under various conditions
 * - Soft jam detection and accumulation
 * - Detection mode switching (both, hard-only, soft-only)
 * - Resume grace period behavior
 * - Edge cases and boundary conditions
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <cassert>

// Mock Arduino environment
unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }

// Mock logger - simplified for testing
namespace logger {
    void log(const char* msg) { /* no-op */ }
    void logf(const char* fmt, ...) { /* no-op */ }
}

// Mock settings manager - provide defaults
class MockSettingsManager {
public:
    static MockSettingsManager& getInstance() {
        static MockSettingsManager instance;
        return instance;
    }
    int getLogLevel() const { return 0; }
    bool getVerboseLogging() const { return false; }
} settingsManager;

// Include the actual JamDetector implementation
#include "../src/JamDetector.h"
#include "../src/JamDetector.cpp"

// ANSI colors for output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string details;
};
std::vector<TestResult> testResults;

void recordTest(const std::string& name, bool passed, const std::string& details = "") {
    testResults.push_back({name, passed, details});
    std::cout << (passed ? COLOR_GREEN : COLOR_RED)
              << (passed ? "[PASS] " : "[FAIL] ")
              << name
              << COLOR_RESET;
    if (!details.empty()) {
        std::cout << " - " << details;
    }
    std::cout << std::endl;
}

void printTestHeader(const std::string& testName) {
    std::cout << "\n" << COLOR_CYAN << "=== " << testName << " ===" << COLOR_RESET << std::endl;
}

// Test: Initial state after construction
void testInitialState() {
    printTestHeader("Test 1: Initial State");
    
    JamDetector detector;
    JamState state = detector.getState();
    
    bool passed = !state.jammed &&
                  !state.hardJamTriggered &&
                  !state.softJamTriggered &&
                  state.hardJamPercent == 0.0f &&
                  state.softJamPercent == 0.0f &&
                  state.graceState == GraceState::IDLE &&
                  !state.graceActive;
    
    recordTest("Initial state is clean", passed);
}

// Test: Grace period timing after reset
void testStartGracePeriod() {
    printTestHeader("Test 2: Start Grace Period");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 1000;
    config.startTimeoutMs = 500;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 5000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    // Within start timeout - should be in START_GRACE
    _mockMillis = 1200;
    JamState state = detector.update(
        10.0f,  // expected
        0.0f,   // actual (jam condition but grace active)
        0,      // pulse count
        true,   // is printing
        true,   // has telemetry
        _mockMillis,
        printStartTime,
        config,
        5.0f,   // expected rate
        0.0f    // actual rate
    );
    
    bool withinTimeout = (state.graceState == GraceState::START_GRACE && state.graceActive);
    recordTest("Within start timeout grace is active", withinTimeout);
    
    // After timeout but within grace time
    _mockMillis = 1800;
    state = detector.update(10.0f, 0.0f, 0, true, true, _mockMillis, printStartTime, config, 5.0f, 0.0f);
    bool withinGrace = (state.graceState == GraceState::START_GRACE && state.graceActive);
    recordTest("Within grace period after timeout", withinGrace);
    
    // After grace period expires - should transition to ACTIVE
    _mockMillis = 2100;
    state = detector.update(10.0f, 10.0f, 10, true, true, _mockMillis, printStartTime, config, 5.0f, 5.0f);
    bool graceExpired = (state.graceState == GraceState::ACTIVE && !state.graceActive);
    recordTest("Grace period expires and transitions to ACTIVE", graceExpired);
}

// Test: Hard jam detection
void testHardJamDetection() {
    printTestHeader("Test 3: Hard Jam Detection");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 2000;  // 2 seconds to trigger
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    // Wait for grace to expire
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Simulate hard jam: high expected flow, zero actual flow
    _mockMillis = 2000;
    JamState state = detector.update(
        15.0f,  // expected (above MIN_HARD_WINDOW_MM)
        0.0f,   // actual (zero movement)
        0,      // no pulses
        true,   // printing
        true,   // has telemetry
        _mockMillis,
        printStartTime,
        config,
        7.5f,   // high expected rate
        0.0f    // zero actual rate
    );
    
    // Should accumulate but not trigger yet
    bool accumulating = !state.hardJamTriggered && state.hardJamPercent > 0.0f;
    recordTest("Hard jam accumulates without triggering immediately", accumulating);
    
    // Continue jam condition for full duration
    _mockMillis = 4100;  // 2+ seconds of jam
    state = detector.update(30.0f, 0.0f, 0, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    
    bool triggered = state.hardJamTriggered && state.jammed;
    recordTest("Hard jam triggers after sustained zero flow", triggered);
}

// Test: Soft jam detection
void testSoftJamDetection() {
    printTestHeader("Test 4: Soft Jam Detection");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;  // 25% threshold
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;  // 3 seconds
    config.hardJamTimeMs = 5000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    // Wait for grace
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Simulate soft jam: 20% flow rate (below 25% threshold)
    _mockMillis = 2000;
    JamState state = detector.update(
        20.0f,  // expected
        4.0f,   // actual (20% flow)
        4,      // some pulses
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        5.0f,   // expected rate
        1.0f    // actual rate (20% of expected)
    );
    
    bool accumulating = !state.softJamTriggered && state.softJamPercent > 0.0f;
    recordTest("Soft jam accumulates with low flow ratio", accumulating);
    
    // Continue for full duration
    _mockMillis = 5100;  // 3+ seconds
    state = detector.update(50.0f, 10.0f, 10, true, true, _mockMillis, printStartTime, config, 5.0f, 1.0f);
    
    bool triggered = state.softJamTriggered && state.jammed;
    recordTest("Soft jam triggers after sustained under-extrusion", triggered);
}

// Test: Detection mode - hard only
void testHardOnlyMode() {
    printTestHeader("Test 5: Hard-Only Detection Mode");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::HARD_ONLY;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Simulate soft jam condition (should be ignored in hard-only mode)
    _mockMillis = 5000;
    JamState state = detector.update(50.0f, 10.0f, 10, true, true, _mockMillis, printStartTime, config, 5.0f, 1.0f);
    
    bool softIgnored = !state.softJamTriggered && !state.jammed;
    recordTest("Soft jam ignored in HARD_ONLY mode", softIgnored);
    
    // Now test hard jam (should still work)
    _mockMillis = 6000;
    state = detector.update(15.0f, 0.0f, 10, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    _mockMillis = 8100;
    state = detector.update(30.0f, 0.0f, 10, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    
    bool hardDetected = state.hardJamTriggered && state.jammed;
    recordTest("Hard jam still detected in HARD_ONLY mode", hardDetected);
}

// Test: Detection mode - soft only
void testSoftOnlyMode() {
    printTestHeader("Test 6: Soft-Only Detection Mode");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::SOFT_ONLY;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Simulate hard jam condition (should be ignored in soft-only mode)
    _mockMillis = 5000;
    JamState state = detector.update(30.0f, 0.0f, 1, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    
    bool hardIgnored = !state.hardJamTriggered && !state.jammed;
    recordTest("Hard jam ignored in SOFT_ONLY mode", hardIgnored);
    
    // Now test soft jam (should still work)
    _mockMillis = 6000;
    state = detector.update(10.0f, 0.5f, 2, true, true, _mockMillis, printStartTime, config, 5.0f, 0.5f);
    _mockMillis = 9100;
    state = detector.update(25.0f, 5.0f, 5, true, true, _mockMillis, printStartTime, config, 5.0f, 1.0f);
    
    bool softDetected = state.softJamTriggered && state.jammed;
    recordTest("Soft jam still detected in SOFT_ONLY mode", softDetected);
}

// Test: Resume grace period
void testResumeGrace() {
    printTestHeader("Test 7: Resume Grace Period");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 1000;
    config.startTimeoutMs = 500;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 5000;
    
    _mockMillis = 1000;
    detector.reset(_mockMillis);
    
    // Simulate print running then pausing
    _mockMillis = 5000;
    detector.onResume(_mockMillis, 10, 28.8f);
    
    JamState state = detector.getState();
    bool inResumeGrace = (state.graceState == GraceState::RESUME_GRACE && state.graceActive);
    recordTest("Resume grace activated after onResume()", inResumeGrace);
    
    // Simulate jam condition during resume grace (should be ignored)
    _mockMillis = 6000;
    unsigned long printStartTime = 1000;
    state = detector.update(5.0f, 0.0f, 10, true, true, _mockMillis, printStartTime, config, 5.0f, 0.0f);
    
    bool noFalsePositive = !state.jammed && state.graceActive;
    recordTest("Jam ignored during resume grace", noFalsePositive);
    
    // Move enough to exit resume grace
    _mockMillis = 7000;
    state = detector.update(20.0f, 20.0f, 18, true, true, _mockMillis, printStartTime, config, 5.0f, 5.0f);
    
    bool graceExited = (state.graceState == GraceState::ACTIVE && !state.graceActive);
    recordTest("Resume grace exits after sufficient movement", graceExited);
}

// Test: Recovery from jam
void testJamRecovery() {
    printTestHeader("Test 8: Jam Recovery");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    // Skip grace
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Create hard jam
    _mockMillis = 2000;
    detector.update(15.0f, 0.0f, 1, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    _mockMillis = 4100;
    JamState state = detector.update(30.0f, 0.0f, 1, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    
    bool jammed = state.jammed;
    recordTest("Jam detected", jammed);
    
    // Now simulate recovery with good flow
    _mockMillis = 5000;
    state = detector.update(40.0f, 38.0f, 15, true, true, _mockMillis, printStartTime, config, 5.0f, 4.8f);
    
    // Hard jam should clear with good flow rate
    bool recovered = state.hardJamPercent < 50.0f;
    recordTest("Jam accumulation reduces with good flow", recovered);
}

// Test: Edge case - zero expected distance
void testZeroExpectedDistance() {
    printTestHeader("Test 9: Zero Expected Distance");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 5000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    _mockMillis = 1200;
    JamState state = detector.update(0.0f, 0.0f, 0, true, true, _mockMillis, printStartTime, config, 0.0f, 0.0f);
    
    bool noFalseJam = !state.jammed;
    recordTest("No false jam with zero expected distance", noFalseJam);
}

// Test: State after latched jam
void testJamLatchedState() {
    printTestHeader("Test 10: Jam Latched State");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.25f;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Trigger jam
    _mockMillis = 2000;
    detector.update(15.0f, 0.0f, 1, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    _mockMillis = 4100;
    JamState state = detector.update(30.0f, 0.0f, 1, true, true, _mockMillis, printStartTime, config, 7.5f, 0.0f);
    
    bool jammed = state.jammed && (state.graceState == GraceState::JAMMED);
    recordTest("Grace state transitions to JAMMED", jammed);
    
    // Verify pause request functionality
    bool pauseRequested = !detector.isPauseRequested();
    detector.setPauseRequested();
    bool pauseSet = detector.isPauseRequested();
    detector.clearPauseRequest();
    bool pauseCleared = !detector.isPauseRequested();
    
    bool pauseLogicWorks = pauseRequested && pauseSet && pauseCleared;
    recordTest("Pause request flag management works", pauseLogicWorks);
}

// Test: Pass ratio calculation
void testPassRatioCalculation() {
    printTestHeader("Test 11: Pass Ratio Calculation");
    
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 100;
    config.startTimeoutMs = 50;
    config.detectionMode = DetectionMode::BOTH;
    config.ratioThreshold = 0.70f;  // 70% threshold
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 5000;
    
    _mockMillis = 1000;
    unsigned long printStartTime = _mockMillis;
    detector.reset(_mockMillis);
    
    _mockMillis = 1200;
    detector.update(1.0f, 1.0f, 1, true, true, _mockMillis, printStartTime, config, 1.0f, 1.0f);
    
    // Test various flow ratios
    _mockMillis = 2000;
    
    // 100% flow - should pass
    JamState state = detector.update(10.0f, 10.0f, 10, true, true, _mockMillis, printStartTime, config, 5.0f, 5.0f);
    bool perfectFlow = (state.passRatio >= 0.99f);
    recordTest("100% flow produces ~1.0 pass ratio", perfectFlow);
    
    // 75% flow - above threshold
    _mockMillis = 3000;
    state = detector.update(20.0f, 15.0f, 15, true, true, _mockMillis, printStartTime, config, 5.0f, 3.75f);
    bool goodFlow = (state.passRatio >= 0.70f);
    recordTest("75% flow passes 70% threshold", goodFlow);
    
    // 50% flow - below threshold
    _mockMillis = 4000;
    state = detector.update(30.0f, 15.0f, 15, true, true, _mockMillis, printStartTime, config, 5.0f, 2.5f);
    bool poorFlow = (state.passRatio < 0.70f);
    recordTest("50% flow fails 70% threshold", poorFlow);
}

// Main test runner
int main() {
    std::cout << COLOR_BLUE << "\n"
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║              JamDetector Unit Test Suite                   ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n"
              << COLOR_RESET << "\n";
    
    // Run all tests
    testInitialState();
    testStartGracePeriod();
    testHardJamDetection();
    testSoftJamDetection();
    testHardOnlyMode();
    testSoftOnlyMode();
    testResumeGrace();
    testJamRecovery();
    testZeroExpectedDistance();
    testJamLatchedState();
    testPassRatioCalculation();
    
    // Print summary
    std::cout << "\n" << COLOR_BLUE << "╔════════════════════════════════════════════════════════════╗" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "║                      Test Summary                          ║" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "╚════════════════════════════════════════════════════════════╝" << COLOR_RESET << "\n\n";
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& result : testResults) {
        if (result.passed) {
            passed++;
        } else {
            failed++;
            std::cout << COLOR_RED << "FAILED: " << result.name << COLOR_RESET << "\n";
            if (!result.details.empty()) {
                std::cout << "  " << result.details << "\n";
            }
        }
    }
    
    std::cout << "\nTotal tests: " << testResults.size() << "\n";
    std::cout << COLOR_GREEN << "Passed: " << passed << COLOR_RESET << "\n";
    std::cout << (failed > 0 ? COLOR_RED : COLOR_GREEN) << "Failed: " << failed << COLOR_RESET << "\n\n";
    
    return (failed == 0) ? 0 : 1;
}