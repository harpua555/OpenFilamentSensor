#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Log levels - each level includes all previous levels
enum LogLevel : uint8_t
{
    LOG_NORMAL      = 0,  // Production: startup, successes, failures, basic stats
    LOG_VERBOSE     = 1,  // Adds: detailed flow state, sensor resets, telemetry details
    LOG_PIN_VALUES  = 2   // Adds: raw pin states (very verbose - emergency debugging only)
};

// Fixed-size log entry to avoid heap fragmentation
struct LogEntry
{
    char          uuid[37];        // UUID string (36 chars + null terminator)
    unsigned long timestamp;       // Unix timestamp
    char          message[256];    // Fixed-size message buffer
    LogLevel      level;           // Log level for this entry
};

class Logger
{
  private:
    static const int MAX_LOG_ENTRIES = 250;
    static const int FALLBACK_LOG_ENTRIES = 128;
    static const int MAX_RETURNED_LOG_ENTRIES = 250;

    LogEntry *logBuffer;
    int       logCapacity;
    volatile int currentIndex;  // volatile to prevent compiler optimization issues
    volatile int totalEntries;
    uint32_t  uuidCounter;      // Simple counter-based UUID for efficiency

    // Current log level (loaded from settings)
    LogLevel currentLogLevel;

    Logger();

    // Delete copy constructor and assignment operator
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // Helper to generate simple UUID
    void generateUUID(char *buffer);

    // Internal log function with level
    void logInternal(const char *message, LogLevel level);

  public:
    // Singleton access method
    static Logger &getInstance();

    ~Logger();

    // Set the current log level (called from settings)
    void setLogLevel(LogLevel level);
    LogLevel getLogLevel() const;

    // Level-based logging functions
    void log(const char *message, LogLevel level = LOG_NORMAL);
    void log(const __FlashStringHelper *message, LogLevel level = LOG_NORMAL);
    void logf(LogLevel level, const char *format, ...);

    // Backward-compatible logging functions (default to LOG_NORMAL)
    void logf(const char *format, ...);  // Defaults to LOG_NORMAL

    // Convenience functions for specific levels
    void logNormal(const char *format, ...);
    void logVerbose(const char *format, ...);
    void logPinValues(const char *format, ...);

    String getLogsAsText();
    String getLogsAsText(int maxEntries);
    void   clearLogs();
    int    getLogCount();
};

// Convenience macro for easier access
#define logger Logger::getInstance()

#endif  // LOGGER_H
