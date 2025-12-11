/**
 * Shared Test Mocks
 *
 * Common mock implementations for unit testing.
 * Include this file to get MockLogger and MockSettingsManager.
 */

#ifndef TEST_MOCKS_H
#define TEST_MOCKS_H

#include <cstdarg>
#include <cstdio>

// ANSI color codes for test output
#define COLOR_GREEN   "\033[32m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_RESET   "\033[0m"

// Global test counters
extern int testsPassed;
extern int testsFailed;

// Mock time management
extern unsigned long _mockMillis;

inline unsigned long millis() { return _mockMillis; }

inline void resetMockTime() {
    _mockMillis = 0;
}

inline void advanceTime(unsigned long ms) {
    _mockMillis += ms;
}

inline void setMockTime(unsigned long ms) {
    _mockMillis = ms;
}

// Float comparison helper
inline bool floatEquals(float a, float b, float epsilon = 0.001f) {
    return (a - b) < epsilon && (b - a) < epsilon;
}

/**
 * Mock Logger - No-op implementation for testing
 */
class MockLogger {
public:
    void log(const char* msg) { /* no-op */ }
    void logf(const char* fmt, ...) { /* no-op */ }
    void logVerbose(const char* fmt, ...) { /* no-op */ }
    void logNormal(const char* fmt, ...) { /* no-op */ }
    void logPinValues(const char* fmt, ...) { /* no-op */ }
    int getLogLevel() const { return 0; }
    void setLogLevel(int level) { /* no-op */ }
};

/**
 * Mock SettingsManager - Returns default values for testing
 */
class MockSettingsManager {
public:
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
    bool getPauseOnRunout() const { return true; }
    int getFlowTelemetryStaleMs() const { return 1500; }
};

// Global mock instances (defined in test files that include this header)
// Each test file should define these:
// MockLogger logger;
// MockSettingsManager settingsManager;

// Note: Real Logger and SettingsManager singletons are provided by
// mocks/Logger.h and mocks/SettingsManager.h which intercept
// the real headers when -I./mocks is in the include path.

/**
 * Test assertion helper with message
 */
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cout << COLOR_RED << "FAIL: " << message << COLOR_RESET << std::endl; \
            std::cout << "  at " << __FILE__ << ":" << __LINE__ << std::endl; \
            testsFailed++; \
            return; \
        } \
    } while(0)

/**
 * Test pass message
 */
#define TEST_PASS(message) \
    do { \
        std::cout << COLOR_GREEN << "PASS: " << message << COLOR_RESET << std::endl; \
        testsPassed++; \
    } while(0)

/**
 * Test section header
 */
#define TEST_SECTION(name) \
    std::cout << "\n=== Test: " << name << " ===" << std::endl

/**
 * Test suite header
 */
#define TEST_SUITE_BEGIN(name) \
    std::cout << "\n"; \
    std::cout << "========================================\n"; \
    std::cout << "    " << name << "\n"; \
    std::cout << "========================================\n"

/**
 * Test suite summary
 */
#define TEST_SUITE_END() \
    std::cout << "\n========================================\n"; \
    std::cout << "Test Results:\n"; \
    std::cout << COLOR_GREEN << "  Passed: " << testsPassed << COLOR_RESET << std::endl; \
    if (testsFailed > 0) { \
        std::cout << COLOR_RED << "  Failed: " << testsFailed << COLOR_RESET << std::endl; \
    } \
    std::cout << "========================================\n\n"; \
    return testsFailed > 0 ? 1 : 0

#endif // TEST_MOCKS_H
