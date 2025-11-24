#include "Logger.h"
#include "time.h"

// External function to get current time (from main.cpp)
extern unsigned long getTime();

Logger &Logger::getInstance()
{
  static Logger instance;
  return instance;
}

Logger::Logger()
{
  currentIndex = 0;
  totalEntries = 0;
  logCapacity = MAX_LOG_ENTRIES;
  uuidGenerator.generate();
  logBuffer = new (std::nothrow) LogEntry[logCapacity];
  if (!logBuffer)
  {
    logCapacity = FALLBACK_LOG_ENTRIES;
    logBuffer  = new (std::nothrow) LogEntry[logCapacity];
    if (!logBuffer)
    {
      logCapacity = 0;
    }
  }
}

Logger::~Logger()
{
  delete[] logBuffer;
}

void Logger::log(const String &message)
{
  // Print to serial first
  Serial.println(message);

  // Generate UUID for this log entry
  uuidGenerator.generate();
  String uuid = String(uuidGenerator.toCharArray());

  // Get current timestamp
  unsigned long timestamp = getTime();

  if (logCapacity == 0 || logBuffer == nullptr)
  {
    return;
  }

  // Store in circular buffer
  logBuffer[currentIndex].uuid = uuid;
  logBuffer[currentIndex].timestamp = timestamp;
  logBuffer[currentIndex].message = message;

  // Update indices
  currentIndex = (currentIndex + 1) % logCapacity;
  if (totalEntries < logCapacity)
  {
    totalEntries = totalEntries + 1;  // Avoid ++ with volatile
  }
}

void Logger::log(const char *message)
{
  log(String(message));
}

void Logger::logf(const char *format, ...)
{
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  log(String(buffer));
}

String Logger::getLogsAsJson()
{
  DynamicJsonDocument jsonDoc(32768); // Allocate space for expanded log buffer
  JsonArray logsArray = jsonDoc.createNestedArray("logs");

  if (logCapacity == 0 || logBuffer == nullptr)
  {
    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    return jsonResponse;
  }

  int count = totalEntries;
  if (count == 0)
  {
    String jsonResponse;
    serializeJson(jsonDoc, jsonResponse);
    return jsonResponse;
  }

  // Limit to MAX_RETURNED_LOG_ENTRIES
  int returnCount = count;
  bool truncated = false;
  if (returnCount > MAX_RETURNED_LOG_ENTRIES)
  {
    returnCount = MAX_RETURNED_LOG_ENTRIES;
    truncated = true;
  }

  // If we have less than logCapacity entries, start from 0
  // Otherwise, start from currentIndex (oldest entry)
  int startIndex = (count < logCapacity) ? 0 : currentIndex;

  // If we're truncating, skip to the most recent entries
  if (count > returnCount)
  {
    startIndex = (startIndex + (count - returnCount)) % logCapacity;
  }

  for (int i = 0; i < returnCount; i++)
  {
    int bufferIndex = (startIndex + i) % logCapacity;  // Use logCapacity, not MAX_LOG_ENTRIES

    JsonObject logEntry = logsArray.createNestedObject();
    logEntry["uuid"] = logBuffer[bufferIndex].uuid;
    logEntry["timestamp"] = logBuffer[bufferIndex].timestamp;
    logEntry["message"] = logBuffer[bufferIndex].message;
  }

  jsonDoc["truncated"] = truncated;

  String jsonResponse;
  serializeJson(jsonDoc, jsonResponse);
  return jsonResponse;
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
  int returnCount = snapshotCount;
  bool truncated = false;
  if (returnCount > maxEntries)
  {
    returnCount = maxEntries;
    truncated = true;
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

    result += String(logBuffer[bufferIndex].timestamp);
    result += " ";
    result += logBuffer[bufferIndex].message;
    result += "\n";
  }

  // Truncated message removed - not needed, endpoint always returns last N entries
  // if (truncated)
  // {
  //   result += "[truncated: showing last " + String(returnCount) + " entries]\n";
  // }

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
    logBuffer[i].uuid = "";
    logBuffer[i].timestamp = 0;
    logBuffer[i].message = "";
  }
}

int Logger::getLogCount()
{
  return totalEntries;
}
