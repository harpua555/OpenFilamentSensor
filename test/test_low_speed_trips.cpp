/**
 * Unit Tests for Low-Speed Trip Edge Cases
 *
 * Tests edge conditions that can cause false positives or missed detections
 * at low extrusion rates, layer changes, and borderline thresholds.
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <cmath>
#include <cassert>

// Mock Arduino environment
unsigned long _mockMillis = 0;
#include "mocks/Arduino.h" // Provides millis()

// Provide a mock for the global getTime() function needed by Logger.cpp
unsigned long getTime() {
    return _mockMillis / 1000;
}

// Shared test helpers (colors, time utilities, assertions)
#include "mocks/test_mocks.h"

// Include the actual implementation
#include "../src/JamDetector.h"
#include "../src/JamDetector.cpp"

int testsPassed = 0;
int testsFailed = 0;

// Create a default config for tests
JamConfig createDefaultConfig() {
    JamConfig config;
    config.graceTimeMs = 0;      // No grace for these tests
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 3000;
    config.hardJamTimeMs = 2000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;
    return config;
}

void testLowSpeedNoFalsePositive() {
    std::cout << "\n=== Test: Low Speed No False Positive ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Very slow extrusion (0.5 mm/s expected, 0.4 mm/s actual = 80%)
    // Should NOT trigger hard jam because passRatio is good
    _mockMillis = 5000;  // 4 seconds in
    JamState state = detector.update(
        10.0f,   // expected distance (above MIN_SOFT_WINDOW_MM)
        8.0f,    // actual distance (80%)
        100,     // pulseCount
        true,    // isPrinting
        true,    // hasTelemetry
        _mockMillis,
        printStartTime,
        config,
        0.5f,    // expectedRate - just above MIN_EXPECTED_RATE_MM_S (0.4)
        0.4f     // actualRate (80% = good flow)
    );
    
    assert(!state.jammed);
    assert(!state.hardJamTriggered);
    // passRatio = 0.4/0.5 = 0.8 which is above threshold
    assert(floatEquals(state.passRatio, 0.8f, 0.05f));
    
    std::cout << COLOR_GREEN << "PASS: Low speed with good ratio does not trigger" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testBorderlineActualRate() {
    std::cout << "\n=== Test: Borderline Actual Rate ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Test with actualRate exactly at MIN_ACTUAL_RATE_MM_S (0.05)
    // Hard jam requires actualRate < 0.05, so exactly 0.05 should NOT trigger
    _mockMillis = 5000;
    JamState state = detector.update(
        15.0f,   // expected (above MIN_HARD_WINDOW_MM)
        0.75f,   // actual 
        100,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        5.0f,    // expectedRate
        0.05f    // actualRate = exactly MIN_ACTUAL_RATE_MM_S - should NOT trigger
    );
    
    // passRatio = 0.05/5.0 = 0.01 which is < HARD_RATE_RATIO (0.25)
    // But actualRate is exactly at threshold, not below it
    // Should be borderline - accumulating but not yet triggered
    assert(!state.hardJamTriggered);
    
    std::cout << COLOR_GREEN << "PASS: Borderline actual rate does not immediately trigger" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testBelowMinExpectedRate() {
    std::cout << "\n=== Test: Below Minimum Expected Rate ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // When expectedRate is below MIN_EXPECTED_RATE_MM_S (0.4),
    // we should treat this as "not really extruding" and not accumulate jam time
    _mockMillis = 5000;
    JamState state = detector.update(
        5.0f,    // expected distance
        0.0f,    // actual distance (zero!)
        10,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        0.3f,    // expectedRate - BELOW MIN_EXPECTED_RATE_MM_S
        0.0f     // actualRate - zero
    );
    
    // Even with zero actual, should not trigger because expectedRate is too low
    assert(!state.hardJamTriggered);
    assert(state.hardJamPercent == 0.0f);
    
    std::cout << COLOR_GREEN << "PASS: Low expected rate suppresses false positives" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testLayerChangeZeroFlow() {
    std::cout << "\n=== Test: Layer Change Zero Flow ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Normal printing
    _mockMillis = 2000;
    JamState state = detector.update(
        15.0f, 14.0f, 100, true, true, _mockMillis, printStartTime, config,
        5.0f, 4.5f  // Good flow
    );
    assert(!state.jammed);
    
    // Layer change: brief period with zero expected rate (travel move)
    _mockMillis = 3000;
    state = detector.update(
        15.0f,    // Same expected (no new extrusion during travel)
        14.0f,    // Same actual
        100,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        0.0f,     // Zero expectedRate during travel
        0.0f      // Zero actualRate
    );
    
    // Should not accumulate jam time during travel moves
    assert(!state.jammed);
    assert(state.hardJamPercent == 0.0f);
    
    std::cout << COLOR_GREEN << "PASS: Layer change with zero flow does not trigger" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testTripCodeClassification() {
    std::cout << "\n=== Test: TripCode Classification ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Initial state should have NONE
    JamState state = detector.getState();
    assert(state.tripCode == TripCode::NONE);
    
    std::cout << COLOR_GREEN << "PASS: Initial tripCode is NONE" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testSoftJamAccumulation() {
    std::cout << "\n=== Test: Soft Jam at Low Speed ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    config.softJamTimeMs = 2000;  // 2 second threshold
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Sustained under-extrusion at low speed (60%)
    _mockMillis = 2000;
    JamState state = detector.update(
        10.0f,   // expected
        6.0f,    // actual (60%)
        50,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        1.0f,    // expectedRate - low but above threshold
        0.6f     // actualRate (60% = below 70% threshold)
    );
    
    // Should be accumulating soft jam
    assert(!state.jammed);  // Not yet
    assert(state.softJamPercent > 0.0f);
    
    // Continue for another second - should trigger
    _mockMillis = 3000;
    state = detector.update(
        15.0f, 9.0f, 75, true, true, _mockMillis, printStartTime, config,
        1.0f, 0.6f
    );
    
    assert(state.jammed);
    assert(state.softJamTriggered);
    assert(floatEquals(state.softJamPercent, 100.0f));
    
    std::cout << COLOR_GREEN << "PASS: Soft jam accumulates correctly at low speed" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testMinimumWindowDistance() {
    std::cout << "\n=== Test: Minimum Window Distance ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Very small expected distance (below MIN_HARD_WINDOW_MM = 10mm)
    // Should NOT trigger hard jam even with bad ratio
    _mockMillis = 5000;
    JamState state = detector.update(
        5.0f,    // expected - below MIN_HARD_WINDOW_MM (10mm)
        0.0f,    // actual - zero!
        10,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        2.0f,    // expectedRate - above threshold
        0.0f     // actualRate - zero
    );
    
    // Should not trigger because window is too small
    assert(!state.hardJamTriggered);
    
    std::cout << COLOR_GREEN << "PASS: Minimum window distance protects against early false positives" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testRecoveryFromLowSpeed() {
    std::cout << "\n=== Test: Recovery From Low Speed Jam Condition ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config = createDefaultConfig();
    config.hardJamTimeMs = 3000;  // 3 second threshold
    
    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);
    
    // Start with conditions that would accumulate hard jam
    _mockMillis = 2000;
    JamState state = detector.update(
        15.0f, 0.5f, 50, true, true, _mockMillis, printStartTime, config,
        5.0f, 0.02f  // Very low actual rate
    );
    
    assert(state.hardJamPercent > 0.0f);
    float accumBefore = state.hardJamPercent;
    
    // Now flow recovers - passRatio > HARD_RECOVERY_RATIO (0.75)
    _mockMillis = 3000;
    state = detector.update(
        20.0f, 16.0f, 150, true, true, _mockMillis, printStartTime, config,
        5.0f, 4.0f  // Good flow now (80%)
    );
    
    // Hard jam accumulation should clear on recovery
    assert(state.hardJamPercent == 0.0f);
    
    std::cout << COLOR_GREEN << "PASS: Recovery clears hard jam accumulation" << COLOR_RESET << std::endl;
    testsPassed++;
}

int main() {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "    Low-Speed Trip Test Suite\n";
    std::cout << "========================================\n";
    
    testLowSpeedNoFalsePositive();
    testBorderlineActualRate();
    testBelowMinExpectedRate();
    testLayerChangeZeroFlow();
    testTripCodeClassification();
    testSoftJamAccumulation();
    testMinimumWindowDistance();
    testRecoveryFromLowSpeed();
    
    std::cout << "\n========================================\n";
    std::cout << "Test Results:\n";
    std::cout << COLOR_GREEN << "  Passed: " << testsPassed << COLOR_RESET << std::endl;
    if (testsFailed > 0) {
        std::cout << COLOR_RED << "  Failed: " << testsFailed << COLOR_RESET << std::endl;
    }
    std::cout << "========================================\n\n";
    
    return testsFailed > 0 ? 1 : 0;
}
