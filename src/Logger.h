#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <UUID.h>
#include <new>

struct LogEntry
{
  String uuid;
  unsigned long timestamp;
  String message;
};

class Logger
{
  private:
    static const int MAX_LOG_ENTRIES = 250;
    static const int FALLBACK_LOG_ENTRIES = 128;
    static const int MAX_RETURNED_LOG_ENTRIES = 250;

    LogEntry *logBuffer;
    int logCapacity;
    volatile int currentIndex;  // volatile to prevent compiler optimization issues
    volatile int totalEntries;
    UUID uuidGenerator;

    Logger();

    // Delete copy constructor and assignment operator
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

  public:
    // Singleton access method
    static Logger &getInstance();

    ~Logger();

    void log(const String &message);
    void log(const char *message);
    void logf(const char *format, ...);
    String getLogsAsJson();
    String getLogsAsText();
    String getLogsAsText(int maxEntries);  // New: limit entries for live display
    void clearLogs();
    int getLogCount();
};

// Convenience macro for easier access
#define logger Logger::getInstance()

#endif // LOGGER_H
