/**
 * FilamentMotionSensor Unit Tests
 * 
 * Comprehensive tests for the windowed tracking algorithm covering:
 * - Initialization and reset behavior
 * - Expected position updates
 * - Sensor pulse tracking
 * - Windowed distance calculations
 * - Deficit calculations
 * - Flow ratio calculations
 * - Grace period handling
 * - Sample window pruning
 */

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cassert>

// Mock Arduino environment
unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }

// Include the actual sensor implementation
#include "../src/FilamentMotionSensor.h"
#include "../src/FilamentMotionSensor.cpp"

// ANSI colors
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

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
    
    FilamentMotionSensor sensor;
    
    bool notInitialized = !sensor.isInitialized();
    recordTest("Sensor not initialized before first update", notInitialized);
    
    float deficit = sensor.getDeficit();
    bool deficitZero = (deficit == 0.0f);
    recordTest("Initial deficit is zero", deficitZero);
    
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    bool distancesZero = (expected == 0.0f && actual == 0.0f);
    recordTest("Initial distances are zero", distancesZero);
}

// Test: Reset behavior
void testReset() {
    printTestHeader("Test 2: Reset Behavior");
    
    FilamentMotionSensor sensor;
    
    // Add some data
    sensor.updateExpectedPosition(50.0f);
    sensor.addSensorPulse(2.88f);
    
    // Reset
    sensor.reset();
    
    bool notInitialized = !sensor.isInitialized();
    recordTest("Not initialized after reset", notInitialized);
    
    float deficit = sensor.getDeficit();
    bool deficitZero = (deficit == 0.0f);
    recordTest("Deficit zero after reset", deficitZero);
}

// Test: Expected position updates
void testExpectedPositionUpdate() {
    printTestHeader("Test 3: Expected Position Updates");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    sensor.updateExpectedPosition(10.0f);
    bool initialized = sensor.isInitialized();
    recordTest("Initialized after first expected position update", initialized);
    
    float expected = sensor.getExpectedDistance();
    bool expectedCorrect = (std::abs(expected - 10.0f) < 0.01f);
    recordTest("Expected distance tracked correctly", expectedCorrect);
    
    // Second update
    _mockMillis = 2000;
    sensor.updateExpectedPosition(25.0f);
    expected = sensor.getExpectedDistance();
    bool expectedUpdated = (std::abs(expected - 25.0f) < 0.01f);
    recordTest("Expected distance updates correctly", expectedUpdated);
}

// Test: Sensor pulse tracking
void testSensorPulseTracking() {
    printTestHeader("Test 4: Sensor Pulse Tracking");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    sensor.updateExpectedPosition(20.0f);
    
    // Add pulses (2.88mm per pulse)
    _mockMillis = 1100;
    sensor.addSensorPulse(2.88f);
    
    float actual = sensor.getSensorDistance();
    bool firstPulse = (std::abs(actual - 2.88f) < 0.01f);
    recordTest("First pulse tracked correctly", firstPulse);
    
    _mockMillis = 1200;
    sensor.addSensorPulse(2.88f);
    actual = sensor.getSensorDistance();
    bool secondPulse = (std::abs(actual - 5.76f) < 0.01f);
    recordTest("Second pulse accumulates correctly", secondPulse);
    
    // Add multiple pulses
    for (int i = 0; i < 5; i++) {
        _mockMillis += 100;
        sensor.addSensorPulse(2.88f);
    }
    
    actual = sensor.getSensorDistance();
    float expectedActual = 2.88f * 7;  // 7 total pulses
    bool multipleCorrect = (std::abs(actual - expectedActual) < 0.01f);
    recordTest("Multiple pulses accumulate correctly", multipleCorrect);
}

// Test: Deficit calculation
void testDeficitCalculation() {
    printTestHeader("Test 5: Deficit Calculation");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // Case 1: Expected > Actual (deficit)
    sensor.updateExpectedPosition(30.0f);
    sensor.addSensorPulse(2.88f);
    sensor.addSensorPulse(2.88f);
    sensor.addSensorPulse(2.88f);  // 8.64mm actual
    
    float deficit = sensor.getDeficit();
    float expectedDeficit = 30.0f - 8.64f;
    bool deficitCorrect = (std::abs(deficit - expectedDeficit) < 0.01f);
    recordTest("Deficit calculated correctly when expected > actual", deficitCorrect);
    
    // Case 2: Expected == Actual (no deficit)
    sensor.reset();
    _mockMillis = 2000;
    sensor.updateExpectedPosition(14.4f);
    for (int i = 0; i < 5; i++) {
        sensor.addSensorPulse(2.88f);  // 14.4mm total
    }
    
    deficit = sensor.getDeficit();
    bool noDeficit = (std::abs(deficit) < 0.01f);
    recordTest("No deficit when expected == actual", noDeficit);
    
    // Case 3: Expected < Actual (no deficit - capped at 0)
    sensor.reset();
    _mockMillis = 3000;
    sensor.updateExpectedPosition(5.0f);
    for (int i = 0; i < 3; i++) {
        sensor.addSensorPulse(2.88f);  // 8.64mm actual
    }
    
    deficit = sensor.getDeficit();
    bool deficitZero = (deficit == 0.0f);
    recordTest("Deficit is zero when actual > expected", deficitZero);
}

// Test: Flow ratio calculation
void testFlowRatioCalculation() {
    printTestHeader("Test 6: Flow Ratio Calculation");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // Before initialization
    float ratio = sensor.getFlowRatio();
    bool ratioZeroBeforeInit = (ratio == 0.0f);
    recordTest("Flow ratio is 0 before initialization", ratioZeroBeforeInit);
    
    // Perfect match (100% flow)
    sensor.updateExpectedPosition(28.8f);
    for (int i = 0; i < 10; i++) {
        sensor.addSensorPulse(2.88f);  // 28.8mm
    }
    
    ratio = sensor.getFlowRatio();
    bool perfectRatio = (std::abs(ratio - 1.0f) < 0.01f);
    recordTest("Flow ratio ~1.0 for perfect match", perfectRatio);
    
    // Under-extrusion (50% flow)
    sensor.reset();
    _mockMillis = 2000;
    sensor.updateExpectedPosition(28.8f);
    for (int i = 0; i < 5; i++) {
        sensor.addSensorPulse(2.88f);  // 14.4mm (50%)
    }
    
    ratio = sensor.getFlowRatio();
    bool halfRatio = (std::abs(ratio - 0.5f) < 0.01f);
    recordTest("Flow ratio ~0.5 for 50% flow", halfRatio);
    
    // Over-extrusion (150% flow)
    sensor.reset();
    _mockMillis = 3000;
    sensor.updateExpectedPosition(28.8f);
    for (int i = 0; i < 15; i++) {
        sensor.addSensorPulse(2.88f);  // 43.2mm (150%)
    }
    
    ratio = sensor.getFlowRatio();
    bool overRatio = (ratio > 1.4f && ratio < 1.6f);
    recordTest("Flow ratio ~1.5 for 150% flow", overRatio);
}

// Test: Grace period behavior
void testGracePeriod() {
    printTestHeader("Test 7: Grace Period Behavior");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    sensor.updateExpectedPosition(10.0f);
    
    // Immediately after update
    bool inGrace = sensor.isWithinGracePeriod(500);
    recordTest("Within grace period immediately after update", inGrace);
    
    // Just before grace expires
    _mockMillis = 1499;
    inGrace = sensor.isWithinGracePeriod(500);
    recordTest("Still in grace period just before expiry", inGrace);
    
    // After grace expires
    _mockMillis = 1501;
    bool graceExpired = !sensor.isWithinGracePeriod(500);
    recordTest("Grace period expires after configured time", graceExpired);
}

// Test: Windowed tracking with time
void testWindowedTracking() {
    printTestHeader("Test 8: Windowed Tracking");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // Add data over time
    sensor.updateExpectedPosition(10.0f);
    sensor.addSensorPulse(2.88f);
    
    _mockMillis = 2000;
    sensor.updateExpectedPosition(20.0f);
    sensor.addSensorPulse(2.88f);
    
    _mockMillis = 3000;
    sensor.updateExpectedPosition(30.0f);
    sensor.addSensorPulse(2.88f);
    
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    
    bool trackingWorking = (expected == 30.0f && std::abs(actual - 8.64f) < 0.01f);
    recordTest("Windowed tracking accumulates over time", trackingWorking);
}

// Test: Multiple resets
void testMultipleResets() {
    printTestHeader("Test 9: Multiple Resets");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // First session
    sensor.updateExpectedPosition(20.0f);
    sensor.addSensorPulse(2.88f);
    
    sensor.reset();
    
    // Second session
    _mockMillis = 2000;
    sensor.updateExpectedPosition(15.0f);
    sensor.addSensorPulse(2.88f);
    sensor.addSensorPulse(2.88f);
    
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    
    bool resetWorks = (expected == 15.0f && std::abs(actual - 5.76f) < 0.01f);
    recordTest("Reset clears previous session data", resetWorks);
    
    // Third session
    sensor.reset();
    _mockMillis = 3000;
    sensor.updateExpectedPosition(25.0f);
    
    expected = sensor.getExpectedDistance();
    actual = sensor.getSensorDistance();
    
    bool secondResetWorks = (expected == 25.0f && actual == 0.0f);
    recordTest("Multiple resets work correctly", secondResetWorks);
}

// Test: Windowed rates calculation
void testWindowedRates() {
    printTestHeader("Test 10: Windowed Rates Calculation");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // Initial state
    float expectedRate = 0.0f;
    float actualRate = 0.0f;
    sensor.getWindowedRates(expectedRate, actualRate);
    
    bool ratesZeroInitially = (expectedRate == 0.0f && actualRate == 0.0f);
    recordTest("Rates are zero initially", ratesZeroInitially);
    
    // Add some data
    sensor.updateExpectedPosition(10.0f);
    sensor.addSensorPulse(2.88f);
    sensor.addSensorPulse(2.88f);
    
    _mockMillis = 2000;  // 1 second later
    sensor.updateExpectedPosition(20.0f);  // +10mm expected
    sensor.addSensorPulse(2.88f);  // +2.88mm actual
    
    sensor.getWindowedRates(expectedRate, actualRate);
    
    // Expected rate should be around 10mm/s, actual around 2.88mm/s
    bool ratesReasonable = (expectedRate > 0.0f && actualRate > 0.0f && expectedRate > actualRate);
    recordTest("Rates are calculated and reasonable", ratesReasonable);
}

// Test: Edge case - zero expected but has pulses
void testZeroExpectedWithPulses() {
    printTestHeader("Test 11: Zero Expected with Pulses");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // Don't update expected position, just add pulses
    sensor.addSensorPulse(2.88f);
    sensor.addSensorPulse(2.88f);
    
    bool notInitialized = !sensor.isInitialized();
    recordTest("Not initialized without expected position update", notInitialized);
    
    float deficit = sensor.getDeficit();
    bool deficitZero = (deficit == 0.0f);
    recordTest("Deficit is zero without expected position", deficitZero);
}

// Test: Edge case - large time gap
void testLargeTimeGap() {
    printTestHeader("Test 12: Large Time Gap Handling");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    sensor.updateExpectedPosition(10.0f);
    sensor.addSensorPulse(2.88f);
    
    // Large time jump (simulating pause/resume)
    _mockMillis = 100000;
    
    sensor.updateExpectedPosition(15.0f);
    sensor.addSensorPulse(2.88f);
    
    // Should still work correctly
    bool initialized = sensor.isInitialized();
    float expected = sensor.getExpectedDistance();
    
    bool worksAfterGap = (initialized && expected == 15.0f);
    recordTest("Works correctly after large time gap", worksAfterGap);
}

// Test: Rapid updates
void testRapidUpdates() {
    printTestHeader("Test 13: Rapid Updates");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    // Rapid expected position updates (every 100ms)
    for (int i = 0; i < 10; i++) {
        _mockMillis += 100;
        sensor.updateExpectedPosition((i + 1) * 2.0f);
    }
    
    // Rapid pulse updates
    for (int i = 0; i < 5; i++) {
        _mockMillis += 50;
        sensor.addSensorPulse(2.88f);
    }
    
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    
    bool handlesRapid = (expected == 20.0f && std::abs(actual - 14.4f) < 0.01f);
    recordTest("Handles rapid updates correctly", handlesRapid);
}

// Test: Alternating expected and pulse updates
void testAlternatingUpdates() {
    printTestHeader("Test 14: Alternating Updates");
    
    FilamentMotionSensor sensor;
    _mockMillis = 1000;
    
    for (int i = 0; i < 10; i++) {
        _mockMillis += 100;
        sensor.updateExpectedPosition((i + 1) * 3.0f);
        sensor.addSensorPulse(2.88f);
    }
    
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    
    bool alternatingWorks = (expected == 30.0f && std::abs(actual - 28.8f) < 0.01f);
    recordTest("Alternating updates work correctly", alternatingWorks);
}

// Main test runner
int main() {
    std::cout << COLOR_BLUE << "\n"
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║        FilamentMotionSensor Unit Test Suite                ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n"
              << COLOR_RESET << "\n";
    
    // Run all tests
    testInitialState();
    testReset();
    testExpectedPositionUpdate();
    testSensorPulseTracking();
    testDeficitCalculation();
    testFlowRatioCalculation();
    testGracePeriod();
    testWindowedTracking();
    testMultipleResets();
    testWindowedRates();
    testZeroExpectedWithPulses();
    testLargeTimeGap();
    testRapidUpdates();
    testAlternatingUpdates();
    
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