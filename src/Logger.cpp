#include "Logger.h"
#include "time.h"
#include <cstdarg>
#include <cstring>

// External function to get current time (from main.cpp)
extern unsigned long getTime();

Logger &Logger::getInstance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
{
    currentIndex    = 0;
    totalEntries    = 0;
    logCapacity     = MAX_LOG_ENTRIES;
    uuidCounter     = 0;
    currentLogLevel = LOG_NORMAL;  // Default to normal logging

    logBuffer = new (std::nothrow) LogEntry[logCapacity];
    if (!logBuffer)
    {
        logCapacity = FALLBACK_LOG_ENTRIES;
        logBuffer   = new (std::nothrow) LogEntry[logCapacity];
        if (!logBuffer)
        {
            logCapacity = 0;
        }
    }

    // Initialize buffer to avoid garbage data
    if (logBuffer && logCapacity > 0)
    {
        for (int i = 0; i < logCapacity; i++)
        {
            memset(logBuffer[i].uuid, 0, sizeof(logBuffer[i].uuid));
            logBuffer[i].timestamp = 0;
            memset(logBuffer[i].message, 0, sizeof(logBuffer[i].message));
            logBuffer[i].level = LOG_NORMAL;
        }
    }
}

Logger::~Logger()
{
    delete[] logBuffer;
}

void Logger::setLogLevel(LogLevel level)
{
    currentLogLevel = level;
}

LogLevel Logger::getLogLevel() const
{
    return currentLogLevel;
}

void Logger::generateUUID(char *buffer)
{
    // Simple UUID-like string: timestamp-counter format
    // Format: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX (36 chars)
    // This is much faster than true UUID generation and sufficient for log tracking
    uuidCounter++;
    snprintf(buffer, 37, "%08lx-%04x-%04x-%04x-%08lx%04x",
             (unsigned long)millis(),
             (unsigned int)((uuidCounter >> 16) & 0xFFFF),
             (unsigned int)(uuidCounter & 0xFFFF),
             (unsigned int)((uuidCounter >> 8) & 0xFFFF),
             (unsigned long)ESP.getCycleCount(),
             (unsigned int)(uuidCounter & 0xFFFF));
}

void Logger::logInternal(const char *message, LogLevel level)
{
    // Filter based on current log level
    if (level > currentLogLevel)
    {
        return;  // Don't log messages above current level
    }

    // Print to serial first
    Serial.println(message);

    if (logCapacity == 0 || logBuffer == nullptr)
    {
        return;
    }

    // Generate UUID
    char uuid[37];
    generateUUID(uuid);

    // Get current timestamp
    unsigned long timestamp = getTime();

    // Store in circular buffer with fixed-size copy
    strncpy(logBuffer[currentIndex].uuid, uuid, sizeof(logBuffer[currentIndex].uuid) - 1);
    logBuffer[currentIndex].uuid[sizeof(logBuffer[currentIndex].uuid) - 1] = '\0';

    logBuffer[currentIndex].timestamp = timestamp;

    strncpy(logBuffer[currentIndex].message, message, sizeof(logBuffer[currentIndex].message) - 1);
    logBuffer[currentIndex].message[sizeof(logBuffer[currentIndex].message) - 1] = '\0';

    logBuffer[currentIndex].level = level;

    // Update indices
    currentIndex = (currentIndex + 1) % logCapacity;
    if (totalEntries < logCapacity)
    {
        totalEntries = totalEntries + 1;  // Avoid ++ with volatile
    }
}

void Logger::log(const char *message, LogLevel level)
{
    logInternal(message, level);
}

void Logger::log(const __FlashStringHelper *message, LogLevel level)
{
    // Convert F() string to char buffer
    char buffer[256];
    PGM_P p = reinterpret_cast<PGM_P>(message);
    strncpy_P(buffer, p, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    logInternal(buffer, level);
}

void Logger::logf(LogLevel level, const char *format, ...)
{
    // Filter based on current log level before formatting
    if (level > currentLogLevel)
    {
        return;
    }

    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logInternal(buffer, level);
}

void Logger::logf(const char *format, ...)
{
    // Backward-compatible version: defaults to LOG_NORMAL
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logInternal(buffer, LOG_NORMAL);
}

void Logger::logNormal(const char *format, ...)
{
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logInternal(buffer, LOG_NORMAL);
}

void Logger::logVerbose(const char *format, ...)
{
    if (LOG_VERBOSE > currentLogLevel) return;

    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logInternal(buffer, LOG_VERBOSE);
}

void Logger::logPinValues(const char *format, ...)
{
    if (LOG_PIN_VALUES > currentLogLevel) return;

    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logInternal(buffer, LOG_PIN_VALUES);
}

String Logger::getLogsAsText()
{
    return getLogsAsText(MAX_RETURNED_LOG_ENTRIES);
}

String Logger::getLogsAsText(int maxEntries)
{
    String result;

    if (logCapacity == 0 || logBuffer == nullptr)
    {
        return result;
    }

    // Snapshot indices atomically to avoid race conditions
    int snapshotIndex = currentIndex;
    int snapshotCount = totalEntries;

    // Validate snapshot
    if (snapshotCount < 0 || snapshotCount > logCapacity)
    {
        snapshotCount = 0;  // Corrupted, return empty
    }
    if (snapshotIndex < 0 || snapshotIndex >= logCapacity)
    {
        snapshotIndex = 0;  // Corrupted, return empty
    }

    if (snapshotCount == 0)
    {
        return result;
    }

    // Limit entries
    int  returnCount = snapshotCount;
    bool truncated   = false;
    if (returnCount > maxEntries)
    {
        returnCount = maxEntries;
        truncated   = true;
    }

    // Pre-allocate to avoid repeated reallocations
    result.reserve(returnCount * 80 + 100);

    // If we have less than logCapacity entries, start from 0
    // Otherwise, start from currentIndex (oldest entry)
    int startIndex = (snapshotCount < logCapacity) ? 0 : snapshotIndex;

    // If we're limiting entries, skip to the most recent ones
    if (snapshotCount > returnCount)
    {
        startIndex = (startIndex + (snapshotCount - returnCount)) % logCapacity;
    }

    for (int i = 0; i < returnCount; i++)
    {
        int bufferIndex = (startIndex + i) % logCapacity;

        // Bounds check to avoid reading garbage
        if (bufferIndex < 0 || bufferIndex >= logCapacity)
        {
            continue;  // Skip corrupted index
        }

        // Format timestamp as MM.DD.YY-HH:MM:SS (local time)
        time_t localTimestamp = logBuffer[bufferIndex].timestamp;
        struct tm *timeinfo = localtime(&localTimestamp);
        if (timeinfo != nullptr) {
            char timeStr[20];
            strftime(timeStr, sizeof(timeStr), "%m.%d.%y-%H:%M:%S", timeinfo);
            result += timeStr;
        } else {
            result += String(logBuffer[bufferIndex].timestamp);
        }
        result += " ";
        result += logBuffer[bufferIndex].message;
        result += "\n";
    }

    return result;
}

void Logger::clearLogs()
{
    currentIndex = 0;
    totalEntries = 0;
    if (logCapacity == 0 || logBuffer == nullptr)
    {
        return;
    }
    // Clear the buffer
    for (int i = 0; i < logCapacity; i++)
    {
        memset(logBuffer[i].uuid, 0, sizeof(logBuffer[i].uuid));
        logBuffer[i].timestamp = 0;
        memset(logBuffer[i].message, 0, sizeof(logBuffer[i].message));
        logBuffer[i].level = LOG_NORMAL;
    }
}

int Logger::getLogCount()
{
    return totalEntries;
}
