/**
 * Unit Tests for SDCPProtocol
 *
 * Tests SDCP message building and parsing utilities.
 */

#include <iostream>
#include <string>
#include <cassert>
#include <cstring>

// Mock Arduino String class
class String {
public:
    String() : data(nullptr), len(0) {}
    String(const char* str) {
        if (str) {
            len = strlen(str);
            data = new char[len + 1];
            strcpy(data, str);
        } else {
            data = nullptr;
            len = 0;
        }
    }
    String(const String& other) {
        if (other.data) {
            len = other.len;
            data = new char[len + 1];
            strcpy(data, other.data);
        } else {
            data = nullptr;
            len = 0;
        }
    }
    ~String() { delete[] data; }
    
    String& operator=(const String& other) {
        if (this != &other) {
            delete[] data;
            if (other.data) {
                len = other.len;
                data = new char[len + 1];
                strcpy(data, other.data);
            } else {
                data = nullptr;
                len = 0;
            }
        }
        return *this;
    }
    
    const char* c_str() const { return data ? data : ""; }
    bool isEmpty() const { return len == 0; }
    operator bool() const { return data != nullptr; }
    
    String& operator+=(const char* str) {
        if (!str) return *this;
        size_t newLen = len + strlen(str);
        char* newData = new char[newLen + 1];
        if (data) {
            strcpy(newData, data);
            strcat(newData, str);
        } else {
            strcpy(newData, str);
        }
        delete[] data;
        data = newData;
        len = newLen;
        return *this;
    }
    
private:
    char* data;
    size_t len;
};

// Mock ArduinoJson (simplified)
class JsonVariant {
public:
    JsonVariant() : type(TYPE_NULL), floatVal(0.0f) {}
    
    bool isNull() const { return type == TYPE_NULL; }
    
    float as<float>() const { return floatVal; }
    
    void setFloat(float val) {
        type = TYPE_FLOAT;
        floatVal = val;
    }
    
private:
    enum { TYPE_NULL, TYPE_FLOAT } type;
    float floatVal;
};

class JsonObject {
public:
    static const int MAX_KEYS = 10;
    
    JsonObject() : keyCount(0) {}
    
    bool containsKey(const char* key) const {
        for (int i = 0; i < keyCount; i++) {
            if (strcmp(keys[i], key) == 0) return true;
        }
        return false;
    }
    
    JsonVariant operator[](const char* key) const {
        for (int i = 0; i < keyCount; i++) {
            if (strcmp(keys[i], key) == 0) return values[i];
        }
        return JsonVariant();
    }
    
    void set(const char* key, float value) {
        if (keyCount < MAX_KEYS) {
            keys[keyCount] = key;
            values[keyCount].setFloat(value);
            keyCount++;
        }
    }
    
private:
    const char* keys[MAX_KEYS];
    JsonVariant values[MAX_KEYS];
    int keyCount;
};

class JsonArray {
public:
    void add(int val) { /* mock */ }
};

class JsonDocument {
public:
    JsonDocument() {}
    
    JsonObject createNestedObject(const char* key) {
        return JsonObject();
    }
    
    JsonArray createNestedArray(const char* key) {
        return JsonArray();
    }
    
    void set(const char* key, const String& val) { /* mock */ }
    void set(const char* key, unsigned long val) { /* mock */ }
    void set(const char* key, int val) { /* mock */ }
    
    JsonDocument& operator[](const char* key) { return *this; }
};

// Include SDCPProtocol
#include "../src/SDCPProtocol.h"
#include "../src/SDCPProtocol.cpp"

// ANSI colors
#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_RESET "\033[0m"

int testsPassed = 0;
int testsFailed = 0;

bool floatEquals(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}

void testBuildCommandMessage() {
    std::cout << "\n=== Test: Build SDCP Command Message ===" << std::endl;
    
    JsonDocument doc;
    String requestId = "test-request-123";
    String mainboardId = "board-456";
    
    bool result = SDCPProtocol::buildCommandMessage(
        doc,
        100,  // command
        requestId,
        mainboardId,
        1234567890,  // timestamp
        1,   // printStatus
        0x03 // machineStatusMask (bits 0 and 1)
    );
    
    assert(result);
    
    std::cout << COLOR_GREEN << "PASS: Command message built successfully" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testTryReadExtrusionValueNormalKey() {
    std::cout << "\n=== Test: Read Extrusion Value (Normal Key) ===" << std::endl;
    
    JsonObject printInfo;
    printInfo.set("TotalExtrusion", 123.45f);
    
    float output = 0.0f;
    bool result = SDCPProtocol::tryReadExtrusionValue(
        printInfo,
        "TotalExtrusion",
        nullptr,
        output
    );
    
    assert(result);
    assert(floatEquals(output, 123.45f));
    
    std::cout << COLOR_GREEN << "PASS: Normal key read successfully" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testTryReadExtrusionValueHexKey() {
    std::cout << "\n=== Test: Read Extrusion Value (Hex Key Fallback) ===" << std::endl;
    
    JsonObject printInfo;
    printInfo.set("54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00", 456.78f);
    
    float output = 0.0f;
    bool result = SDCPProtocol::tryReadExtrusionValue(
        printInfo,
        "TotalExtrusion",  // Try normal first
        "54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00",  // Fall back to hex
        output
    );
    
    assert(result);
    assert(floatEquals(output, 456.78f));
    
    std::cout << COLOR_GREEN << "PASS: Hex key fallback works" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testTryReadExtrusionValueNotFound() {
    std::cout << "\n=== Test: Read Extrusion Value (Not Found) ===" << std::endl;
    
    JsonObject printInfo;  // Empty object
    
    float output = 999.0f;  // Should remain unchanged
    bool result = SDCPProtocol::tryReadExtrusionValue(
        printInfo,
        "NonExistentKey",
        nullptr,
        output
    );
    
    assert(!result);
    assert(floatEquals(output, 999.0f));  // Unchanged
    
    std::cout << COLOR_GREEN << "PASS: Missing key returns false without modifying output" << COLOR_RESET << std::endl;
    testsPassed++;
}

void testSDCPConstants() {
    std::cout << "\n=== Test: SDCP Constants ===" << std::endl;
    
    // Verify timing constants are reasonable
    assert(SDCPTiming::ACK_TIMEOUT_MS > 0);
    assert(SDCPTiming::EXPECTED_FILAMENT_SAMPLE_MS > 0);
    assert(SDCPTiming::SDCP_LOSS_TIMEOUT_MS > 0);
    assert(SDCPTiming::PAUSE_REARM_DELAY_MS > 0);
    
    // Verify default values
    assert(SDCPDefaults::FILAMENT_DEFICIT_THRESHOLD_MM > 0.0f);
    
    std::cout << COLOR_GREEN << "PASS: SDCP constants are valid" << COLOR_RESET << std::endl;
    testsPassed++;
}

int main() {
    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "   SDCPProtocol Unit Test Suite\n";
    std::cout << "========================================\n";
    
    testBuildCommandMessage();
    testTryReadExtrusionValueNormalKey();
    testTryReadExtrusionValueHexKey();
    testTryReadExtrusionValueNotFound();
    testSDCPConstants();
    
    std::cout << "\n========================================\n";
    std::cout << "Test Results:\n";
    std::cout << COLOR_GREEN << "  Passed: " << testsPassed << COLOR_RESET << std::endl;
    if (testsFailed > 0) {
        std::cout << COLOR_RED << "  Failed: " << testsFailed << COLOR_RESET << std::endl;
    }
    std::cout << "========================================\n\n";
    
    return testsFailed > 0 ? 1 : 0;
}