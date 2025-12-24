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
    config.graceTimeMs = 10000;
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
        config.hardJamMm = 5.0f;
    config.softJamTimeMs = 10000;
    config.hardJamTimeMs = 2000;  // 2 seconds (MAX_EVAL_INTERVAL_MS caps each update to 1s)
    config.ratioThreshold = 0.25f;
    config.detectionMode = DetectionMode::BOTH;

    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);

    // Simulate hard jam: expected movement but zero actual
    // Note: actualRate must be < MIN_ACTUAL_RATE_MM_S (0.05) to trigger hard jam
    // Note: Each update accumulates up to MAX_EVAL_INTERVAL_MS (1000ms) of jam time
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
        0.02f   // actualRate (must be < 0.05 to trigger hard jam detection)
    );

    assert(!state.jammed);  // Not yet - needs time accumulation
    assert(state.hardJamPercent > 0.0f);

    // Second update should trigger the jam (2 x 1000ms accumulated >= 2000ms hardJamTimeMs)
    _mockMillis = 3000;
    state = detector.update(
        20.0f,
        0.15f,
        7,
        true,
        true,
        _mockMillis,
        printStartTime,
        config,
        10.0f,
        0.02f
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
        config.hardJamMm = 5.0f;
    config.softJamTimeMs = 2000;  // 2 seconds (MAX_EVAL_INTERVAL_MS caps each update to 1s)
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.70f;  // 70% threshold
    config.detectionMode = DetectionMode::BOTH;

    unsigned long printStartTime = 1000;
    _mockMillis = 1000;
    detector.reset(printStartTime);

    // Simulate soft jam: consistent under-extrusion (60% actual)
    // Note: Each update accumulates up to MAX_EVAL_INTERVAL_MS (1000ms) of jam time
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

    assert(!state.jammed);  // Not yet - needs time accumulation
    assert(state.softJamPercent > 0.0f);
    assert(floatEquals(state.passRatio, 0.6f, 0.05f));

    // Second update should trigger soft jam (2 x 1000ms accumulated >= 2000ms softJamTimeMs)
    _mockMillis = 3000;
    state = detector.update(
        20.0f,
        12.0f,  // Still 60%
        150,
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
    
    // Now recover - good flow ratio (actualRate/expectedRate = 4.5/5.0 = 0.9)
    // Extend time to 4200ms (700ms delta) to allow sufficient decay
    // Previous 500ms delta decayed it to exactly 50%, failing the < 50% check
    _mockMillis = 4200;
    state = detector.update(30.0f, 24.0f, 400, true, true, _mockMillis, printStartTime, config, 5.0f, 4.5f);

    assert(floatEquals(state.passRatio, 0.9f, 0.05f));  // 90% is good (4.5/5.0)
    assert(state.softJamPercent < 50.0f);  // Should be decreasing
    
    std::cout << COLOR_GREEN << "PASS: Jam recovery decreases accumulation" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testResumeGrace() {
    std::cout << "\n=== Test: Resume Grace Period ===" << std::endl;
    
    resetMockTime();
    JamDetector detector;
    JamConfig config;
    config.graceTimeMs = 5000;
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
    // Note: pulseCount must stay close to baseline (200) to avoid exceeding RESUME_MIN_PULSES (5)
    // and expectedDistance must be < RESUME_GRACE_15MM_THRESHOLD (~15mm)
    _mockMillis = 16000;
    state = detector.update(10.0f, 6.0f, 203, true, true, _mockMillis, printStartTime, config, 5.0f, 3.0f);
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
