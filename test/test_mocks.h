#ifndef TEST_MOCKS_H
#define TEST_MOCKS_H

// Prevent the real headers from being included in tests.
#define LOGGER_H
#define SETTINGS_DATA_H

/**
     * Access the global Logger singleton.
     * @returns Reference to the single Logger instance.
     */
    class Logger {
public:
    static Logger& getInstance() { static Logger inst; return inst; }
    void log(const char* msg, int level = 0) { (void)msg; (void)level; }
    void log(const void* msg, int level = 0) { (void)msg; (void)level; }
    /**
 * Accepts a printf-style format string and corresponding arguments for logging; in this mock implementation it performs no action.
 * @param fmt Format string that specifies how subsequent arguments would be formatted for output.
 */
void logf(const char* fmt, ...) { (void)fmt; }
    /**
 * Format and log a message at the specified log level.
 * @param level Log level associated with the message.
 * @param fmt printf-style format string followed by arguments matching its specifiers.
 */
void logf(int level, const char* fmt, ...) { (void)level; (void)fmt; }
    /**
 * Accept a printf-style format and arguments intended for verbose logging; in this test mock the call has no effect.
 * @param fmt printf-style format string describing the message to log; additional arguments (variadic) provide values for format specifiers.
 */
void logVerbose(const char* fmt, ...) { (void)fmt; }
    /**
 * Log a normal-priority message; accepts a printf-style format and variadic arguments. This mock implementation performs no operation.
 * @param fmt Format string followed by arguments corresponding to its format specifiers.
 */
void logNormal(const char* fmt, ...) { (void)fmt; }
    /**
 * Log a formatted message about pin values (no-op in test mocks).
 * @param fmt Format string describing the pin values; additional arguments provide values for format specifiers.
 */
void logPinValues(const char* fmt, ...) { (void)fmt; }
    int getLogLevel() const { return 0; }
    /**
 * Set the logger's verbosity level (no-op in this mock implementation).
 * @param level Desired log verbosity level; this mock ignores the value.
 */
void setLogLevel(int level) { (void)level; }
};

/**
     * Access the singleton SettingsManager instance.
     * @returns Reference to the single SettingsManager instance.
     */
    class SettingsManager {
public:
    static SettingsManager& getInstance() { static SettingsManager inst; return inst; }
    bool getVerboseLogging() const { return false; }
    template<typename T>
    T getSetting(int offset) const { (void)offset; return T(); }
    template<typename T>
    void setSetting(int offset, T value) { (void)offset; (void)value; }
};

#define logger Logger::getInstance()
#define settingsManager SettingsManager::getInstance()

#endif  // TEST_MOCKS_H