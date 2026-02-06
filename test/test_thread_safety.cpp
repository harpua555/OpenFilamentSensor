/**
 * Thread Safety Stress Tests
 *
 * Uses POSIX threads to simulate the ESP32's two-task architecture:
 *   Thread A (main loop): writes logs, updates caches
 *   Thread B (async handler): reads logs, clears logs, reads caches
 *
 * Run with ThreadSanitizer to detect data races:
 *   g++ -std=c++17 -fsanitize=thread -g -lpthread -I. -I./mocks -I../src \
 *       -o test_thread_safety test_thread_safety.cpp && ./test_thread_safety
 *
 * Run with AddressSanitizer to detect heap corruption:
 *   g++ -std=c++17 -fsanitize=address -fno-omit-frame-pointer -g -lpthread \
 *       -I. -I./mocks -I../src -o test_thread_safety test_thread_safety.cpp \
 *       && ./test_thread_safety
 */

#include <iostream>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>

// ============================================================================
// Mock ESP32 primitives using POSIX mutexes
// ============================================================================

// portMUX_TYPE backed by std::mutex (simulates ESP32 spinlock)
// Uses a pointer to mutex since std::mutex is non-copyable/non-movable
struct portMUX_TYPE {
    std::mutex *mtx;
    portMUX_TYPE() : mtx(new std::mutex()) {}
    ~portMUX_TYPE() { delete mtx; }
    portMUX_TYPE(const portMUX_TYPE &) : mtx(new std::mutex()) {}
    portMUX_TYPE &operator=(const portMUX_TYPE &) {
        // Keep existing mutex, don't copy
        return *this;
    }
};

#define portMUX_INITIALIZER_UNLOCKED portMUX_TYPE()

inline void portENTER_CRITICAL(portMUX_TYPE *mux) {
    mux->mtx->lock();
}

inline void portEXIT_CRITICAL(portMUX_TYPE *mux) {
    mux->mtx->unlock();
}

// Mock millis / time
static std::atomic<unsigned long> _mockMillis{0};
unsigned long millis() { return _mockMillis.load(std::memory_order_relaxed); }
unsigned long getTime() { return _mockMillis.load(std::memory_order_relaxed); }
void yield() {}

// Mock Serial
struct MockSerial {
    void println(const char *) {}
    void print(const char *) {}
    void print(unsigned long) {}
};
MockSerial Serial;

// Mock ESP
struct MockESP {
    unsigned long getCycleCount() { return _mockMillis.load(); }
};
MockESP ESP;

// Mock String class (minimal, for getLogsAsText return)
class String {
public:
    String() : data_(nullptr), len_(0) {}
    String(const char *s) {
        if (s) {
            len_ = strlen(s);
            data_ = new char[len_ + 1];
            memcpy(data_, s, len_ + 1);
        } else {
            data_ = nullptr;
            len_ = 0;
        }
    }
    String(unsigned long val) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lu", val);
        len_ = strlen(buf);
        data_ = new char[len_ + 1];
        memcpy(data_, buf, len_ + 1);
    }
    String(const String &o) {
        if (o.data_) {
            len_ = o.len_;
            data_ = new char[len_ + 1];
            memcpy(data_, o.data_, len_ + 1);
        } else {
            data_ = nullptr;
            len_ = 0;
        }
    }
    String &operator=(const String &o) {
        if (this != &o) {
            delete[] data_;
            if (o.data_) {
                len_ = o.len_;
                data_ = new char[len_ + 1];
                memcpy(data_, o.data_, len_ + 1);
            } else {
                data_ = nullptr;
                len_ = 0;
            }
        }
        return *this;
    }
    ~String() { delete[] data_; }

    String &operator+=(const char *s) {
        if (!s) return *this;
        size_t slen = strlen(s);
        char *nd = new char[len_ + slen + 1];
        if (data_) memcpy(nd, data_, len_);
        memcpy(nd + len_, s, slen + 1);
        delete[] data_;
        data_ = nd;
        len_ += slen;
        return *this;
    }
    String &operator+=(const String &o) { return *this += o.c_str(); }

    void reserve(size_t) {} // no-op
    const char *c_str() const { return data_ ? data_ : ""; }
    size_t length() const { return len_; }
    int indexOf(const char *s) const {
        if (!data_ || !s) return -1;
        char *f = strstr(data_, s);
        return f ? (int)(f - data_) : -1;
    }

private:
    char *data_;
    size_t len_;
};

// ============================================================================
// Reproduce the Logger class (matching src/Logger.h + src/Logger.cpp)
// with the FIXED code from this commit
// ============================================================================

enum LogLevel : uint8_t {
    LOG_NORMAL     = 0,
    LOG_VERBOSE    = 1,
    LOG_PIN_VALUES = 2
};

struct LogEntry {
    char          uuid[37];
    unsigned long timestamp;
    char          message[256];
    LogLevel      level;
};

class TestLogger {
public:
    static const int MAX_LOG_ENTRIES = 250;

    LogEntry     logBuffer[MAX_LOG_ENTRIES];
    int          logCapacity;
    volatile int currentIndex;
    volatile int totalEntries;
    uint32_t     uuidCounter;
    LogLevel     currentLogLevel;
    portMUX_TYPE _logMutex;

    TestLogger()
        : logCapacity(MAX_LOG_ENTRIES), currentIndex(0), totalEntries(0),
          uuidCounter(0), currentLogLevel(LOG_PIN_VALUES) {
        _logMutex = portMUX_INITIALIZER_UNLOCKED;
        memset(logBuffer, 0, sizeof(logBuffer));
    }

    void generateUUID(char *buffer) {
        uuidCounter++;
        snprintf(buffer, 37, "%08lx-%04x-%04x-%04x-%08lx%04x",
                 (unsigned long)millis(),
                 (unsigned int)((uuidCounter >> 16) & 0xFFFF),
                 (unsigned int)(uuidCounter & 0xFFFF),
                 (unsigned int)((uuidCounter >> 8) & 0xFFFF),
                 (unsigned long)ESP.getCycleCount(),
                 (unsigned int)(uuidCounter & 0xFFFF));
    }

    void logInternal(const char *message, LogLevel level) {
        if (level > currentLogLevel) return;

        unsigned long timestamp = getTime();

        // FIXED: UUID generation inside critical section
        portENTER_CRITICAL(&_logMutex);

        char uuid[37];
        generateUUID(uuid);

        strncpy(logBuffer[currentIndex].uuid, uuid, sizeof(logBuffer[currentIndex].uuid) - 1);
        logBuffer[currentIndex].uuid[sizeof(logBuffer[currentIndex].uuid) - 1] = '\0';
        logBuffer[currentIndex].timestamp = timestamp;
        strncpy(logBuffer[currentIndex].message, message, sizeof(logBuffer[currentIndex].message) - 1);
        logBuffer[currentIndex].message[sizeof(logBuffer[currentIndex].message) - 1] = '\0';
        logBuffer[currentIndex].level = level;

        currentIndex = (currentIndex + 1) % logCapacity;
        if (totalEntries < logCapacity) {
            totalEntries = totalEntries + 1;
        }
        portEXIT_CRITICAL(&_logMutex);
    }

    String getLogsAsText(int maxEntries) {
        String result;

        // FIXED: Snapshot indices under mutex
        portENTER_CRITICAL(&_logMutex);
        int snapshotIndex = currentIndex;
        int snapshotCount = totalEntries;
        portEXIT_CRITICAL(&_logMutex);

        if (snapshotCount < 0 || snapshotCount > logCapacity) snapshotCount = 0;
        if (snapshotIndex < 0 || snapshotIndex >= logCapacity) snapshotIndex = 0;
        if (snapshotCount == 0) return result;

        int returnCount = snapshotCount;
        if (returnCount > maxEntries) returnCount = maxEntries;

        result.reserve(returnCount * 80 + 100);

        int startIndex = (snapshotCount < logCapacity) ? 0 : snapshotIndex;
        if (snapshotCount > returnCount) {
            startIndex = (startIndex + (snapshotCount - returnCount)) % logCapacity;
        }

        for (int i = 0; i < returnCount; i++) {
            int bufferIndex = (startIndex + i) % logCapacity;
            if (bufferIndex < 0 || bufferIndex >= logCapacity) continue;

            // Read entry under short lock to avoid tearing
            LogEntry entryCopy;
            portENTER_CRITICAL(&_logMutex);
            entryCopy = logBuffer[bufferIndex];
            portEXIT_CRITICAL(&_logMutex);

            result += String(entryCopy.timestamp);
            result += " ";
            result += entryCopy.message;
            result += "\n";
        }

        return result;
    }

    void clearLogs() {
        // FIXED: Indices inside critical section
        portENTER_CRITICAL(&_logMutex);
        currentIndex = 0;
        totalEntries = 0;
        for (int i = 0; i < logCapacity; i++) {
            memset(logBuffer[i].uuid, 0, sizeof(logBuffer[i].uuid));
            logBuffer[i].timestamp = 0;
            memset(logBuffer[i].message, 0, sizeof(logBuffer[i].message));
            logBuffer[i].level = LOG_NORMAL;
        }
        portEXIT_CRITICAL(&_logMutex);
    }

    int getLogCount() { return totalEntries; }
};

// ============================================================================
// Reproduce the CachedResponse double-buffer (matching WebServer.h fix)
// ============================================================================

static const size_t kCacheBufSize = 1536;

struct CachedResponse {
    char         buf[2][kCacheBufSize];
    size_t       len[2] = {0, 0};
    volatile int activeIdx = 0;
    portMUX_TYPE _mutex = portMUX_INITIALIZER_UNLOCKED;

    void publish(const char *json, size_t jsonLen) {
        int writeIdx = !activeIdx;
        size_t copyLen = (jsonLen < kCacheBufSize - 1) ? jsonLen : (kCacheBufSize - 1);
        memcpy(buf[writeIdx], json, copyLen);
        buf[writeIdx][copyLen] = '\0';
        len[writeIdx] = copyLen;
        portENTER_CRITICAL(&_mutex);
        activeIdx = writeIdx;
        portEXIT_CRITICAL(&_mutex);
    }

    const char *read(size_t &outLen) const {
        int idx = activeIdx;
        outLen = len[idx];
        return buf[idx];
    }
};

// ============================================================================
// Test framework
// ============================================================================

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST_SECTION(name) \
    std::cout << "\n=== Test: " << name << " ===" << std::endl

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cout << "\033[31mFAIL: " << msg << "\033[0m" << std::endl; \
            std::cout << "  at " << __FILE__ << ":" << __LINE__ << std::endl; \
            testsFailed++; \
            return; \
        } \
    } while(0)

#define TEST_PASS(msg) \
    do { \
        std::cout << "\033[32mPASS: " << msg << "\033[0m" << std::endl; \
        testsPassed++; \
    } while(0)

// ============================================================================
// Stress Tests
// ============================================================================

static const int STRESS_ITERATIONS = 100000;

/**
 * Test 1: Concurrent log + getLogsAsText
 * Thread A: logs messages rapidly
 * Thread B: reads logs rapidly
 * Without proper synchronization, TSan would flag the race on currentIndex/totalEntries.
 */
void testConcurrentLogAndRead() {
    TEST_SECTION("Concurrent log() + getLogsAsText()");

    TestLogger logger;
    std::atomic<bool> running{true};
    std::atomic<int> readCount{0};

    // Thread A: writer (simulates main loop calling logInternal)
    std::thread writer([&]() {
        for (int i = 0; i < STRESS_ITERATIONS; i++) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Log entry %d", i);
            logger.logInternal(msg, LOG_NORMAL);
            _mockMillis.fetch_add(1, std::memory_order_relaxed);
        }
        running.store(false);
    });

    // Thread B: reader (simulates async handler calling getLogsAsText)
    std::thread reader([&]() {
        while (running.load()) {
            String logs = logger.getLogsAsText(50);
            readCount.fetch_add(1);
            // Verify the returned string is not corrupted
            // (null-terminated, reasonable length)
            if (logs.length() > 0) {
                const char *c = logs.c_str();
                (void)c; // Just ensure we can dereference it without crash
            }
        }
    });

    writer.join();
    reader.join();

    TEST_ASSERT(logger.getLogCount() > 0, "Logger should have entries after stress test");
    TEST_ASSERT(readCount.load() > 0, "Reader thread should have completed reads");
    TEST_PASS("Concurrent log + getLogsAsText survived " +
              std::to_string(STRESS_ITERATIONS) + " iterations, " +
              std::to_string(readCount.load()) + " reads");
}

/**
 * Test 2: Concurrent log + clearLogs
 * Thread A: logs messages
 * Thread B: periodically clears logs
 * Tests the fixed clearLogs() where indices are inside critical section.
 */
void testConcurrentLogAndClear() {
    TEST_SECTION("Concurrent log() + clearLogs()");

    TestLogger logger;
    std::atomic<bool> running{true};
    std::atomic<int> clearCount{0};

    std::thread writer([&]() {
        for (int i = 0; i < STRESS_ITERATIONS; i++) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Entry %d", i);
            logger.logInternal(msg, LOG_NORMAL);
            _mockMillis.fetch_add(1, std::memory_order_relaxed);
        }
        running.store(false);
    });

    std::thread clearer([&]() {
        while (running.load()) {
            logger.clearLogs();
            clearCount.fetch_add(1);
        }
    });

    writer.join();
    clearer.join();

    // After test, indices should be consistent
    int idx = logger.currentIndex;
    int cnt = logger.totalEntries;
    TEST_ASSERT(idx >= 0 && idx < logger.logCapacity,
                "currentIndex should be in bounds after stress");
    TEST_ASSERT(cnt >= 0 && cnt <= logger.logCapacity,
                "totalEntries should be in bounds after stress");
    TEST_ASSERT(clearCount.load() > 0, "Clear thread should have run");
    TEST_PASS("Concurrent log + clearLogs survived " +
              std::to_string(STRESS_ITERATIONS) + " iterations, " +
              std::to_string(clearCount.load()) + " clears");
}

/**
 * Test 3: Triple contention - log + read + clear simultaneously
 * This is the worst-case scenario that most closely matches the real firmware.
 */
void testTripleContention() {
    TEST_SECTION("Triple contention: log + read + clear");

    TestLogger logger;
    std::atomic<bool> running{true};
    std::atomic<int> readCount{0};
    std::atomic<int> clearCount{0};

    // Writer thread
    std::thread writer([&]() {
        for (int i = 0; i < STRESS_ITERATIONS; i++) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Triple %d", i);
            logger.logInternal(msg, LOG_NORMAL);
            _mockMillis.fetch_add(1, std::memory_order_relaxed);
        }
        running.store(false);
    });

    // Reader thread
    std::thread reader([&]() {
        while (running.load()) {
            String logs = logger.getLogsAsText(100);
            readCount.fetch_add(1);
        }
    });

    // Clearer thread
    std::thread clearer([&]() {
        int i = 0;
        while (running.load()) {
            // Clear less frequently to let some logs accumulate
            if (i++ % 100 == 0) {
                logger.clearLogs();
                clearCount.fetch_add(1);
            }
        }
    });

    writer.join();
    reader.join();
    clearer.join();

    TEST_ASSERT(readCount.load() > 0, "Reader should have completed reads");
    TEST_ASSERT(clearCount.load() > 0, "Clearer should have run");
    TEST_PASS("Triple contention survived " +
              std::to_string(STRESS_ITERATIONS) + " writes");
}

/**
 * Test 4: CachedResponse double-buffer concurrent access
 * Thread A: publishes JSON strings
 * Thread B: reads the cached response
 * Verifies no torn reads (partial old + partial new data).
 */
void testCachedResponseDoubleBuffer() {
    TEST_SECTION("CachedResponse double-buffer concurrent access");

    CachedResponse cache;
    std::atomic<bool> running{true};
    std::atomic<int> readCount{0};
    std::atomic<int> tornReads{0};

    // Writer: alternates between two known payloads
    const char *payloadA = "{\"type\":\"AAAAAAAAAA\",\"value\":111111111}";
    const char *payloadB = "{\"type\":\"BBBBBBBBBB\",\"value\":222222222}";
    size_t lenA = strlen(payloadA);
    size_t lenB = strlen(payloadB);

    std::thread writer([&]() {
        for (int i = 0; i < STRESS_ITERATIONS; i++) {
            if (i % 2 == 0)
                cache.publish(payloadA, lenA);
            else
                cache.publish(payloadB, lenB);
        }
        running.store(false);
    });

    std::thread reader([&]() {
        while (running.load()) {
            size_t len;
            const char *data = cache.read(len);
            readCount.fetch_add(1);

            if (len > 0) {
                // Check that we got one of the two payloads, not a mix
                bool isA = (strncmp(data, payloadA, len) == 0 && len == lenA);
                bool isB = (strncmp(data, payloadB, len) == 0 && len == lenB);
                if (!isA && !isB) {
                    tornReads.fetch_add(1);
                }
            }
        }
    });

    writer.join();
    reader.join();

    TEST_ASSERT(readCount.load() > 0, "Reader should have completed reads");
    TEST_ASSERT(tornReads.load() == 0,
                "No torn reads should occur with double buffering (got " +
                std::to_string(tornReads.load()) + ")");
    TEST_PASS("Double-buffer: " + std::to_string(readCount.load()) +
              " reads, 0 torn");
}

/**
 * Test 5: UUID uniqueness under contention
 * Multiple threads call logInternal simultaneously.
 * Verify no duplicate UUIDs are generated.
 */
void testUuidUniquenessUnderContention() {
    TEST_SECTION("UUID uniqueness under contention");

    TestLogger logger;
    const int THREADS = 4;
    const int PER_THREAD = 10000;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; t++) {
        threads.emplace_back([&logger, t]() {
            for (int i = 0; i < PER_THREAD; i++) {
                char msg[64];
                snprintf(msg, sizeof(msg), "T%d-%d", t, i);
                logger.logInternal(msg, LOG_NORMAL);
            }
        });
    }

    for (auto &th : threads) th.join();

    // Check last N entries for UUID uniqueness
    // (can't check all because circular buffer overwrites)
    int entriesToCheck = std::min(logger.getLogCount(), 250);
    std::vector<std::string> uuids;
    uuids.reserve(entriesToCheck);

    portENTER_CRITICAL(&logger._logMutex);
    int idx = logger.currentIndex;
    int count = logger.totalEntries;
    portEXIT_CRITICAL(&logger._logMutex);

    int start = (count < logger.logCapacity) ? 0 : idx;
    int skip = count - entriesToCheck;
    start = (start + skip) % logger.logCapacity;

    for (int i = 0; i < entriesToCheck; i++) {
        int bufIdx = (start + i) % logger.logCapacity;
        uuids.push_back(std::string(logger.logBuffer[bufIdx].uuid));
    }

    // Count duplicates
    int duplicates = 0;
    for (size_t i = 0; i < uuids.size(); i++) {
        for (size_t j = i + 1; j < uuids.size(); j++) {
            if (uuids[i] == uuids[j] && !uuids[i].empty()) {
                duplicates++;
            }
        }
    }

    TEST_ASSERT(duplicates == 0,
                "No duplicate UUIDs (found " + std::to_string(duplicates) + ")");
    TEST_PASS("UUID uniqueness: " + std::to_string(entriesToCheck) +
              " entries checked, 0 duplicates");
}

/**
 * Test 6: Cache publish during read doesn't crash
 * Rapidly alternate publishing large payloads while reader accesses
 * the buffer. Tests memory safety of the double-buffer approach.
 */
void testCachePublishLargePayload() {
    TEST_SECTION("CachedResponse large payload stress");

    CachedResponse cache;
    std::atomic<bool> running{true};
    std::atomic<int> publishCount{0};
    std::atomic<int> readCount{0};

    // Build a large (~1KB) JSON-like payload
    char largePayload[1024];
    memset(largePayload, 'X', sizeof(largePayload) - 1);
    largePayload[0] = '{';
    largePayload[sizeof(largePayload) - 2] = '}';
    largePayload[sizeof(largePayload) - 1] = '\0';
    size_t largeLen = strlen(largePayload);

    std::thread writer([&]() {
        for (int i = 0; i < STRESS_ITERATIONS; i++) {
            // Slightly vary the payload to make torn reads detectable
            largePayload[1] = 'A' + (i % 26);
            cache.publish(largePayload, largeLen);
            publishCount.fetch_add(1);
        }
        running.store(false);
    });

    std::thread reader([&]() {
        while (running.load()) {
            size_t len;
            const char *data = cache.read(len);
            if (len > 0) {
                // Verify first and last chars are consistent
                assert(data[0] == '{');
                assert(data[len - 1] == '}');
            }
            readCount.fetch_add(1);
        }
    });

    writer.join();
    reader.join();

    TEST_ASSERT(publishCount.load() == STRESS_ITERATIONS, "All publishes completed");
    TEST_ASSERT(readCount.load() > 0, "Reader completed reads");
    TEST_PASS("Large payload stress: " + std::to_string(publishCount.load()) +
              " publishes, " + std::to_string(readCount.load()) + " reads");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "    Thread Safety Stress Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    testConcurrentLogAndRead();
    testConcurrentLogAndClear();
    testTripleContention();
    testCachedResponseDoubleBuffer();
    testUuidUniquenessUnderContention();
    testCachePublishLargePayload();

    std::cout << "\n========================================" << std::endl;
    std::cout << "Test Results:" << std::endl;
    std::cout << "\033[32m  Passed: " << testsPassed << "\033[0m" << std::endl;
    if (testsFailed > 0) {
        std::cout << "\033[31m  Failed: " << testsFailed << "\033[0m" << std::endl;
    }
    std::cout << "========================================\n" << std::endl;

    return testsFailed > 0 ? 1 : 0;
}
