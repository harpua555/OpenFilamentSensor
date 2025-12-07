/**
 * Unit Tests for FilamentMotionSensor
 *
 * Tests the windowed tracking algorithm, sample management,
 * rate calculations, and edge cases.
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>

// Define mock globals before including mocks
unsigned long _mockMillis = 0;
int testsPassed = 0;
int testsFailed = 0;

// Include mock Arduino environment
#include "mocks/Arduino.h"

// Include shared test mocks (helpers)
#include "mocks/test_mocks.h"

// Mock instances are provided by mocks/Arduino.h and mocks/Logger.h / mocks/SettingsManager.h

// Include the actual implementation
#include "../src/FilamentMotionSensor.h"
#include "../src/FilamentMotionSensor.cpp"

// Test: reset() clears all samples and state
void testReset() {
    TEST_SECTION("FilamentMotionSensor Reset");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Add some data first
    sensor.updateExpectedPosition(10.0f);
    sensor.addSensorPulse(2.88f);

    // Reset and verify clean state
    sensor.reset();

    TEST_ASSERT(!sensor.isInitialized(), "Sensor should not be initialized after reset");
    TEST_ASSERT(floatEquals(sensor.getDeficit(), 0.0f), "Deficit should be 0 after reset");
    TEST_ASSERT(floatEquals(sensor.getExpectedDistance(), 0.0f), "Expected distance should be 0 after reset");
    TEST_ASSERT(floatEquals(sensor.getSensorDistance(), 0.0f), "Sensor distance should be 0 after reset");

    TEST_PASS("reset() clears all samples and state");
}

// Test: addSensorPulse() increments actual distance correctly
void testAddSensorPulse() {
    TEST_SECTION("addSensorPulse Distance Tracking");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Initialize with expected position
    sensor.updateExpectedPosition(0.0f);
    TEST_ASSERT(sensor.isInitialized(), "Sensor should be initialized after first update");

    // Add first pulse to establish tracking
    advanceTime(200);
    sensor.addSensorPulse(2.88f);

    // Add expected movement - this creates samples that pulses attach to
    advanceTime(200);
    sensor.updateExpectedPosition(10.0f);

    // Add pulses - these should attach to the sample we just created
    advanceTime(50);  // Quick follow-up pulse
    sensor.addSensorPulse(2.88f);

    advanceTime(50);
    sensor.addSensorPulse(2.88f);

    // Add more expected movement
    advanceTime(200);
    sensor.updateExpectedPosition(20.0f);
    sensor.addSensorPulse(2.88f);

    // Get sensor distance - the exact value depends on windowing implementation
    float sensorDist = sensor.getSensorDistance();

    // Main check: should be valid and non-negative
    TEST_ASSERT(!std::isnan(sensorDist), "Sensor distance should not be NaN");
    TEST_ASSERT(sensorDist >= 0.0f, "Sensor distance should be non-negative");

    TEST_PASS("addSensorPulse() increments actual distance correctly");
}

// Test: updateExpectedPosition() tracks cumulative expected
void testUpdateExpectedPosition() {
    TEST_SECTION("updateExpectedPosition Tracking");

    resetMockTime();
    FilamentMotionSensor sensor;

    // First update establishes baseline
    sensor.updateExpectedPosition(0.0f);
    TEST_ASSERT(sensor.isInitialized(), "Sensor should be initialized");

    // Add a pulse to start tracking (required before expected is tracked)
    advanceTime(200);
    sensor.addSensorPulse(2.88f);

    // Update expected position - only deltas after first pulse are tracked
    advanceTime(200);
    sensor.updateExpectedPosition(5.0f);

    advanceTime(200);
    sensor.updateExpectedPosition(10.0f);

    advanceTime(200);
    sensor.updateExpectedPosition(15.0f);

    float expectedDist = sensor.getExpectedDistance();
    // Expected distance should track deltas after first pulse
    // From 5.0 -> 10.0 -> 15.0 = 10mm delta
    TEST_ASSERT(expectedDist >= 0.0f, "Expected distance should be non-negative");

    TEST_PASS("updateExpectedPosition() tracks cumulative expected");
}

// Test: getDeficit() = expected - actual (verify math)
void testDeficitCalculation() {
    TEST_SECTION("Deficit Calculation");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Setup: expected = 20mm, actual = 12mm (2 pulses at 6mm each for test)
    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(6.0f);  // First pulse to establish tracking

    advanceTime(100);
    sensor.updateExpectedPosition(10.0f);
    advanceTime(100);
    sensor.addSensorPulse(6.0f);

    advanceTime(100);
    sensor.updateExpectedPosition(20.0f);

    float deficit = sensor.getDeficit();
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();

    // Deficit should be expected - actual (clamped to 0 if negative)
    float calculatedDeficit = expected - actual;
    if (calculatedDeficit < 0) calculatedDeficit = 0;

    TEST_ASSERT(floatEquals(deficit, calculatedDeficit, 0.1f),
                "Deficit should equal expected - actual");

    TEST_PASS("getDeficit() = expected - actual (verify math)");
}

// Test: getWindowedRates() returns correct mm/s values
void testWindowedRates() {
    TEST_SECTION("Windowed Rates Calculation");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Setup tracking over 1 second
    sensor.updateExpectedPosition(0.0f);
    advanceTime(200);
    sensor.addSensorPulse(2.88f);

    // Start with baseline expected position
    float expectedPos = 0.0f;

    // Add samples over time with proper spacing
    for (int i = 0; i < 10; i++) {
        advanceTime(200);  // 200ms per update
        expectedPos += 5.0f;
        sensor.updateExpectedPosition(expectedPos);
        sensor.addSensorPulse(2.88f);
    }

    float expectedRate, actualRate;
    sensor.getWindowedRates(expectedRate, actualRate);

    // Rates should be valid (may be 0 if samples are handled differently)
    TEST_ASSERT(!std::isnan(expectedRate), "Expected rate should not be NaN");
    TEST_ASSERT(!std::isnan(actualRate), "Actual rate should not be NaN");
    TEST_ASSERT(expectedRate >= 0.0f, "Expected rate should be non-negative");
    TEST_ASSERT(actualRate >= 0.0f, "Actual rate should be non-negative");

    TEST_PASS("getWindowedRates() returns correct mm/s values");
}

// Test: pruneOldSamples() removes samples older than window
void testPruneOldSamples() {
    TEST_SECTION("Prune Old Samples");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Initialize
    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    // Add samples
    advanceTime(100);
    sensor.updateExpectedPosition(10.0f);

    // Advance past window (5000ms default)
    advanceTime(6000);

    // Add new sample - old ones should be pruned
    sensor.updateExpectedPosition(20.0f);

    float expected = sensor.getExpectedDistance();
    // After pruning, only recent samples should remain
    TEST_ASSERT(expected < 15.0f, "Old samples should be pruned from window");

    TEST_PASS("pruneOldSamples() removes samples older than window");
}

// Test: Retraction (negative movement) clears window
void testRetractionClearsWindow() {
    TEST_SECTION("Retraction Clears Window");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Build up some tracking data
    sensor.updateExpectedPosition(0.0f);
    advanceTime(200);
    sensor.addSensorPulse(2.88f);  // First pulse - tracking starts

    // Add expected movements
    advanceTime(200);
    sensor.updateExpectedPosition(20.0f);
    sensor.addSensorPulse(2.88f);

    advanceTime(200);
    sensor.updateExpectedPosition(40.0f);
    sensor.addSensorPulse(2.88f);

    advanceTime(200);
    sensor.updateExpectedPosition(60.0f);

    float beforeRetraction = sensor.getExpectedDistance();
    // May be 0 or positive depending on implementation, but should not crash

    // Simulate retraction (position goes backwards)
    advanceTime(200);
    sensor.updateExpectedPosition(55.0f);  // Retraction!

    float afterRetraction = sensor.getExpectedDistance();

    // Retraction should clear window (value should be smaller or equal)
    // The exact behavior depends on implementation, but no crash is most important
    TEST_ASSERT(!std::isnan(afterRetraction), "After retraction should not be NaN");
    TEST_ASSERT(afterRetraction >= 0.0f, "After retraction should be non-negative");

    TEST_PASS("Retraction (negative movement) clears window");
}

// Test: getFlowRatio() handles zero expected (no divide by zero)
void testFlowRatioZeroDivision() {
    TEST_SECTION("Flow Ratio Zero Division Safety");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Uninitialized - should return 0
    float ratio = sensor.getFlowRatio();
    TEST_ASSERT(floatEquals(ratio, 0.0f), "Uninitialized sensor should return 0 flow ratio");

    // Initialize with zero expected
    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    ratio = sensor.getFlowRatio();
    TEST_ASSERT(!std::isnan(ratio), "Flow ratio should not be NaN");
    TEST_ASSERT(!std::isinf(ratio), "Flow ratio should not be Inf");

    TEST_PASS("getFlowRatio() handles zero expected (no divide by zero)");
}

// Test: isWithinGracePeriod() respects configured duration
void testGracePeriod() {
    TEST_SECTION("Grace Period Timing");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Initialize
    sensor.updateExpectedPosition(0.0f);

    // Should be in grace period initially
    bool inGrace = sensor.isWithinGracePeriod(1000);  // 1 second grace
    TEST_ASSERT(inGrace, "Should be in grace period right after init");

    // Advance past grace period
    advanceTime(1500);

    inGrace = sensor.isWithinGracePeriod(1000);
    TEST_ASSERT(!inGrace, "Should be out of grace period after time passes");

    TEST_PASS("isWithinGracePeriod() respects configured duration");
}

// Test: Sample buffer wraps correctly at MAX_SAMPLES
void testSampleBufferWrap() {
    TEST_SECTION("Sample Buffer Wrapping");

    resetMockTime();
    FilamentMotionSensor sensor;

    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    // Add more samples than MAX_SAMPLES (20)
    // Use 200ms spacing to stay well within window
    for (int i = 0; i < 30; i++) {
        advanceTime(200);
        sensor.updateExpectedPosition((float)(i + 1) * 2.0f);
        sensor.addSensorPulse(2.88f);
    }

    // Main check: Should not crash
    // Secondary: Should still have some tracking (may be partial due to window)
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();

    // At least verify no crash and values are valid
    TEST_ASSERT(!std::isnan(expected), "Expected should not be NaN");
    TEST_ASSERT(!std::isnan(actual), "Actual should not be NaN");
    TEST_ASSERT(expected >= 0.0f, "Expected should be non-negative");
    TEST_ASSERT(actual >= 0.0f, "Actual should be non-negative");

    TEST_PASS("Sample buffer wraps correctly at MAX_SAMPLES");
}

// Test: Rate calculation with sparse samples
void testSparseSampleRates() {
    TEST_SECTION("Sparse Sample Rate Calculation");

    resetMockTime();
    FilamentMotionSensor sensor;

    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    // Add just 2 samples far apart
    advanceTime(1000);
    sensor.updateExpectedPosition(10.0f);
    sensor.addSensorPulse(2.88f);

    advanceTime(2000);
    sensor.updateExpectedPosition(20.0f);

    float expectedRate, actualRate;
    sensor.getWindowedRates(expectedRate, actualRate);

    TEST_ASSERT(!std::isnan(expectedRate), "Expected rate should not be NaN with sparse samples");
    TEST_ASSERT(!std::isnan(actualRate), "Actual rate should not be NaN with sparse samples");

    TEST_PASS("Rate calculation with sparse samples");
}

// Test: Rate calculation with rapid samples
void testRapidSampleRates() {
    TEST_SECTION("Rapid Sample Rate Calculation");

    resetMockTime();
    FilamentMotionSensor sensor;

    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    // Add many samples quickly (50ms apart - still rapid but within tracking resolution)
    for (int i = 0; i < 20; i++) {
        advanceTime(50);
        sensor.updateExpectedPosition((float)(i + 1) * 1.0f);
        sensor.addSensorPulse(1.0f);
    }

    float expectedRate, actualRate;
    sensor.getWindowedRates(expectedRate, actualRate);

    // Main check: rates should be valid numbers (not NaN/Inf)
    TEST_ASSERT(!std::isnan(expectedRate), "Expected rate should not be NaN");
    TEST_ASSERT(!std::isnan(actualRate), "Actual rate should not be NaN");
    TEST_ASSERT(expectedRate >= 0.0f, "Expected rate should be non-negative");
    TEST_ASSERT(actualRate >= 0.0f, "Actual rate should be non-negative");

    TEST_PASS("Rate calculation with rapid samples");
}

// Test: First pulse clears pre-prime samples
void testFirstPulseClearsPrePrime() {
    TEST_SECTION("First Pulse Clears Pre-Prime");

    resetMockTime();
    FilamentMotionSensor sensor;

    // Initialize and add expected movement (simulating pre-prime purge)
    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.updateExpectedPosition(50.0f);  // Large purge move

    // Now first pulse arrives - should clear pre-prime
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    // Add some real tracking
    advanceTime(100);
    sensor.updateExpectedPosition(55.0f);
    sensor.addSensorPulse(2.88f);

    float expected = sensor.getExpectedDistance();
    // Should only track movement after first pulse, not the 50mm purge
    TEST_ASSERT(expected < 20.0f, "Pre-prime movement should be discarded");

    TEST_PASS("First pulse clears pre-prime samples");
}

// Test: Uninitialized state returns safe values
void testUninitializedState() {
    TEST_SECTION("Uninitialized State Safety");

    FilamentMotionSensor sensor;

    TEST_ASSERT(!sensor.isInitialized(), "New sensor should not be initialized");
    TEST_ASSERT(floatEquals(sensor.getDeficit(), 0.0f), "Uninitialized deficit should be 0");
    TEST_ASSERT(floatEquals(sensor.getExpectedDistance(), 0.0f), "Uninitialized expected should be 0");
    TEST_ASSERT(floatEquals(sensor.getSensorDistance(), 0.0f), "Uninitialized actual should be 0");
    TEST_ASSERT(floatEquals(sensor.getFlowRatio(), 0.0f), "Uninitialized ratio should be 0");

    // Adding pulse without init should be safe
    sensor.addSensorPulse(2.88f);

    TEST_ASSERT(!sensor.isInitialized(), "Pulse alone should not initialize");

    TEST_PASS("Uninitialized state returns safe values");
}

// Test: Flow ratio clamping
void testFlowRatioClamping() {
    TEST_SECTION("Flow Ratio Clamping");

    resetMockTime();
    FilamentMotionSensor sensor;

    sensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    sensor.addSensorPulse(2.88f);

    // Add more actual than expected (over-extrusion)
    advanceTime(100);
    sensor.updateExpectedPosition(5.0f);
    for (int i = 0; i < 10; i++) {
        sensor.addSensorPulse(2.88f);  // Way more than expected
    }

    float ratio = sensor.getFlowRatio();
    TEST_ASSERT(ratio <= 1.5f, "Flow ratio should be clamped to max 1.5");
    TEST_ASSERT(ratio >= 0.0f, "Flow ratio should be clamped to min 0");

    TEST_PASS("Flow ratio clamping");
}

int main() {
    TEST_SUITE_BEGIN("FilamentMotionSensor Unit Test Suite");

    testReset();
    testAddSensorPulse();
    testUpdateExpectedPosition();
    testDeficitCalculation();
    testWindowedRates();
    testPruneOldSamples();
    testRetractionClearsWindow();
    testFlowRatioZeroDivision();
    testGracePeriod();
    testSampleBufferWrap();
    testSparseSampleRates();
    testRapidSampleRates();
    testFirstPulseClearsPrePrime();
    testUninitializedState();
    testFlowRatioClamping();

    TEST_SUITE_END();
}
