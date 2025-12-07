/**
 * Mock Logger.h
 *
 * Intercepts #include "Logger.h" for unit testing.
 * Provides a no-op Logger implementation.
 */

#ifndef LOGGER_H
#define LOGGER_H

// Log level enum
enum LogLevel {
    LOG_NORMAL = 0,
    LOG_VERBOSE = 1,
    LOG_PIN_VALUES = 2
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void log(const char* msg, LogLevel level = LOG_NORMAL) { /* no-op */ }
    void log(const class __FlashStringHelper* msg, LogLevel level = LOG_NORMAL) { /* no-op */ }
    void logf(const char* fmt, ...) { /* no-op */ }
    void logf(LogLevel level, const char* fmt, ...) { /* no-op */ }
    void logVerbose(const char* fmt, ...) { /* no-op */ }
    void logNormal(const char* fmt, ...) { /* no-op */ }
    void logPinValues(const char* fmt, ...) { /* no-op */ }
    int getLogLevel() const { return 0; }
    void setLogLevel(int level) { /* no-op */ }
    const char* getLogsAsText(int maxEntries = 100) { return ""; }
    void clearLogs() { /* no-op */ }
};

// Global logger instance - mirrors the pattern used in main.cpp
// JamDetector.cpp uses a global `logger` reference
inline Logger& logger = Logger::getInstance();

#endif // LOGGER_H
