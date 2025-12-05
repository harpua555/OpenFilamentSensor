/**
 * SDCPProtocol Unit Tests
 * 
 * Comprehensive tests for the SDCPProtocol utility class covering:
 * - Command message building
 * - JSON structure validation
 * - Extrusion value parsing (normal and hex-encoded keys)
 * - Edge cases and error handling
 */

#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cstring>

// Mock Arduino environment
class String {
public:
    String() : data("") {}
    String(const char* str) : data(str ? str : "") {}
    String(const std::string& str) : data(str) {}
    
    const char* c_str() const { return data.c_str(); }
    bool isEmpty() const { return data.empty(); }
    size_t length() const { return data.length(); }
    
    String& operator=(const char* str) {
        data = str ? str : "";
        return *this;
    }
    
    String& operator=(const std::string& str) {
        data = str;
        return *this;
    }
    
    String& operator+=(const String& other) {
        data += other.data;
        return *this;
    }
    
    bool operator==(const char* other) const {
        return data == (other ? other : "");
    }
    
    std::string data;
};

unsigned long _mockMillis = 0;
unsigned long millis() { return _mockMillis; }

// Include ArduinoJson (assuming it's available in the test environment)
#include <ArduinoJson.h>

// Include the actual SDCPProtocol implementation
#include "../src/SDCPProtocol.h"
#include "../src/SDCPProtocol.cpp"

// ANSI colors
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

struct TestResult {
    std::string name;
    bool passed;
    std::string details;
};
std::vector<TestResult> testResults;

void recordTest(const std::string& name, bool passed, const std::string& details = "") {
    testResults.push_back({name, passed, details});
    std::cout << (passed ? COLOR_GREEN : COLOR_RED)
              << (passed ? "[PASS] " : "[FAIL] ")
              << name
              << COLOR_RESET;
    if (!details.empty()) {
        std::cout << " - " << details;
    }
    std::cout << std::endl;
}

void printTestHeader(const std::string& testName) {
    std::cout << "\n" << COLOR_CYAN << "=== " << testName << " ===" << COLOR_RESET << std::endl;
}

// Test: Build command message with all fields
void testBuildCommandMessage() {
    printTestHeader("Test 1: Build Command Message");
    
    StaticJsonDocument<1024> doc;
    String requestId = "12345678901234567890123456789012";
    String mainboardId = "MB123456789";
    unsigned long timestamp = 1638360000;
    int command = 128;  // START_PRINT
    int printStatus = 13;  // PRINTING
    uint8_t machineStatus = 0x03;  // bits 0 and 1 set
    
    bool success = SDCPProtocol::buildCommandMessage(
        doc, command, requestId, mainboardId, timestamp, printStatus, machineStatus
    );
    
    recordTest("buildCommandMessage returns true on success", success);
    
    // Verify structure
    bool hasId = doc.containsKey("Id");
    bool hasData = doc.containsKey("Data");
    recordTest("Message contains Id field", hasId);
    recordTest("Message contains Data field", hasData);
    
    if (hasData) {
        JsonObject data = doc["Data"];
        bool hasCmd = data.containsKey("Cmd");
        bool hasRequestId = data.containsKey("RequestID");
        bool hasMainboardId = data.containsKey("MainboardID");
        bool hasTimestamp = data.containsKey("TimeStamp");
        bool hasFrom = data.containsKey("From");
        bool hasPrintStatus = data.containsKey("PrintStatus");
        bool hasCurrentStatus = data.containsKey("CurrentStatus");
        
        recordTest("Data contains Cmd field", hasCmd);
        recordTest("Data contains RequestID field", hasRequestId);
        recordTest("Data contains MainboardID field", hasMainboardId);
        recordTest("Data contains TimeStamp field", hasTimestamp);
        recordTest("Data contains From field", hasFrom);
        recordTest("Data contains PrintStatus field", hasPrintStatus);
        recordTest("Data contains CurrentStatus array", hasCurrentStatus);
        
        // Verify values
        bool cmdCorrect = (data["Cmd"].as<int>() == command);
        bool fromCorrect = (data["From"].as<int>() == 0);
        bool printStatusCorrect = (data["PrintStatus"].as<int>() == printStatus);
        
        recordTest("Cmd value is correct", cmdCorrect);
        recordTest("From value is 0 (Home Assistant compatible)", fromCorrect);
        recordTest("PrintStatus value is correct", printStatusCorrect);
        
        // Verify CurrentStatus array
        if (hasCurrentStatus) {
            JsonArray currentStatus = data["CurrentStatus"];
            int expectedCount = 2;  // bits 0 and 1 are set
            bool countCorrect = (currentStatus.size() == expectedCount);
            recordTest("CurrentStatus array has correct count", countCorrect);
            
            if (countCorrect) {
                bool containsZero = false;
                bool containsOne = false;
                for (JsonVariant v : currentStatus) {
                    int val = v.as<int>();
                    if (val == 0) containsZero = true;
                    if (val == 1) containsOne = true;
                }
                recordTest("CurrentStatus contains expected values", containsZero && containsOne);
            }
        }
    }
    
    // Verify Topic field when mainboardId is provided
    bool hasTopic = doc.containsKey("Topic");
    recordTest("Message contains Topic field when mainboardId provided", hasTopic);
    
    if (hasTopic) {
        String topic = doc["Topic"].as<const char*>();
        bool topicCorrect = (topic.data.find("sdcp/request/") == 0 && 
                            topic.data.find(mainboardId.c_str()) != std::string::npos);
        recordTest("Topic follows sdcp/request/<MainboardID> pattern", topicCorrect);
    }
}

// Test: Build command message without mainboard ID
void testBuildCommandMessageNoMainboard() {
    printTestHeader("Test 2: Build Command Message Without Mainboard ID");
    
    StaticJsonDocument<1024> doc;
    String requestId = "12345678901234567890123456789012";
    String mainboardId = "";  // Empty
    unsigned long timestamp = 1638360000;
    
    bool success = SDCPProtocol::buildCommandMessage(
        doc, 129, requestId, mainboardId, timestamp, 6, 0x01
    );
    
    recordTest("Build succeeds without mainboard ID", success);
    
    bool noTopic = !doc.containsKey("Topic");
    recordTest("Topic field omitted when mainboard ID is empty", noTopic);
}

// Test: Try read extrusion value with normal key
void testReadExtrusionValueNormalKey() {
    printTestHeader("Test 3: Read Extrusion Value - Normal Key");
    
    StaticJsonDocument<256> doc;
    JsonObject printInfo = doc.to<JsonObject>();
    printInfo["TotalExtrusion"] = 123.45f;
    printInfo["CurrentExtrusion"] = 67.89f;
    
    float totalValue = 0.0f;
    bool foundTotal = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "TotalExtrusion", nullptr, totalValue
    );
    
    recordTest("Successfully reads TotalExtrusion with normal key", foundTotal);
    
    bool valueCorrect = (std::abs(totalValue - 123.45f) < 0.01f);
    recordTest("TotalExtrusion value is correct", valueCorrect);
    
    float currentValue = 0.0f;
    bool foundCurrent = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "CurrentExtrusion", nullptr, currentValue
    );
    
    recordTest("Successfully reads CurrentExtrusion with normal key", foundCurrent);
    
    bool currentCorrect = (std::abs(currentValue - 67.89f) < 0.01f);
    recordTest("CurrentExtrusion value is correct", currentCorrect);
}

// Test: Try read extrusion value with hex-encoded key
void testReadExtrusionValueHexKey() {
    printTestHeader("Test 4: Read Extrusion Value - Hex Key");
    
    StaticJsonDocument<256> doc;
    JsonObject printInfo = doc.to<JsonObject>();
    
    // Use the hex-encoded key
    const char* hexKey = "54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00";
    printInfo[hexKey] = 234.56f;
    
    float value = 0.0f;
    bool found = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "TotalExtrusion", hexKey, value
    );
    
    recordTest("Successfully reads value with hex-encoded key", found);
    
    bool valueCorrect = (std::abs(value - 234.56f) < 0.01f);
    recordTest("Hex-encoded key value is correct", valueCorrect);
}

// Test: Try read extrusion value with fallback to hex key
void testReadExtrusionValueFallback() {
    printTestHeader("Test 5: Read Extrusion Value - Fallback to Hex");
    
    StaticJsonDocument<256> doc;
    JsonObject printInfo = doc.to<JsonObject>();
    
    // Only provide hex key (normal key missing)
    const char* hexKey = "43 75 72 72 65 6E 74 45 78 74 72 75 73 69 6F 6E 00";
    printInfo[hexKey] = 345.67f;
    
    float value = 0.0f;
    bool found = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "CurrentExtrusion", hexKey, value
    );
    
    recordTest("Falls back to hex key when normal key missing", found);
    
    bool valueCorrect = (std::abs(value - 345.67f) < 0.01f);
    recordTest("Fallback hex key value is correct", valueCorrect);
}

// Test: Try read extrusion value - key not found
void testReadExtrusionValueNotFound() {
    printTestHeader("Test 6: Read Extrusion Value - Not Found");
    
    StaticJsonDocument<256> doc;
    JsonObject printInfo = doc.to<JsonObject>();
    printInfo["SomeOtherKey"] = 100.0f;
    
    float value = 999.0f;  // Sentinel value
    bool found = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "TotalExtrusion", "54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00", value
    );
    
    recordTest("Returns false when key not found", !found);
    recordTest("Output value unchanged when key not found", (value == 999.0f));
}

// Test: Try read extrusion value - null value
void testReadExtrusionValueNull() {
    printTestHeader("Test 7: Read Extrusion Value - Null Value");
    
    StaticJsonDocument<256> doc;
    JsonObject printInfo = doc.to<JsonObject>();
    printInfo["TotalExtrusion"] = nullptr;
    
    float value = 999.0f;
    bool found = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "TotalExtrusion", nullptr, value
    );
    
    recordTest("Returns false when value is null", !found);
}

// Test: Try read extrusion value - no hex key provided
void testReadExtrusionValueNoHexKey() {
    printTestHeader("Test 8: Read Extrusion Value - No Hex Key");
    
    StaticJsonDocument<256> doc;
    JsonObject printInfo = doc.to<JsonObject>();
    printInfo["TotalExtrusion"] = 456.78f;
    
    float value = 0.0f;
    bool found = SDCPProtocol::tryReadExtrusionValue(
        printInfo, "TotalExtrusion", nullptr, value
    );
    
    recordTest("Works with nullptr hex key", found);
    
    bool valueCorrect = (std::abs(value - 456.78f) < 0.01f);
    recordTest("Value correct with nullptr hex key", valueCorrect);
}

// Test: Build multiple command types
void testBuildDifferentCommands() {
    printTestHeader("Test 9: Build Different Command Types");
    
    String requestId = "12345678901234567890123456789012";
    String mainboardId = "MB123";
    unsigned long timestamp = 1638360000;
    
    // Test PAUSE command
    StaticJsonDocument<1024> pauseDoc;
    bool pauseSuccess = SDCPProtocol::buildCommandMessage(
        pauseDoc, 129, requestId, mainboardId, timestamp, 13, 0x02
    );
    recordTest("PAUSE command builds successfully", pauseSuccess);
    
    // Test STOP command
    StaticJsonDocument<1024> stopDoc;
    bool stopSuccess = SDCPProtocol::buildCommandMessage(
        stopDoc, 130, requestId, mainboardId, timestamp, 13, 0x02
    );
    recordTest("STOP command builds successfully", stopSuccess);
    
    // Test CONTINUE command
    StaticJsonDocument<1024> continueDoc;
    bool continueSuccess = SDCPProtocol::buildCommandMessage(
        continueDoc, 131, requestId, mainboardId, timestamp, 6, 0x00
    );
    recordTest("CONTINUE command builds successfully", continueSuccess);
    
    // Verify command codes
    bool pauseCmdCorrect = (pauseDoc["Data"]["Cmd"].as<int>() == 129);
    bool stopCmdCorrect = (stopDoc["Data"]["Cmd"].as<int>() == 130);
    bool continueCmdCorrect = (continueDoc["Data"]["Cmd"].as<int>() == 131);
    
    recordTest("PAUSE command code is correct", pauseCmdCorrect);
    recordTest("STOP command code is correct", stopCmdCorrect);
    recordTest("CONTINUE command code is correct", continueCmdCorrect);
}

// Test: Machine status mask encoding
void testMachineStatusMask() {
    printTestHeader("Test 10: Machine Status Mask Encoding");
    
    StaticJsonDocument<1024> doc;
    String requestId = "12345678901234567890123456789012";
    String mainboardId = "MB123";
    
    // Test all bits set
    uint8_t allBits = 0x1F;  // bits 0-4
    SDCPProtocol::buildCommandMessage(
        doc, 128, requestId, mainboardId, 1638360000, 13, allBits
    );
    
    JsonArray statusArray = doc["Data"]["CurrentStatus"];
    bool correctCount = (statusArray.size() == 5);
    recordTest("All status bits (0-4) create 5 entries", correctCount);
    
    // Test single bit
    doc.clear();
    SDCPProtocol::buildCommandMessage(
        doc, 128, requestId, mainboardId, 1638360000, 13, 0x08  // bit 3
    );
    
    JsonArray singleBit = doc["Data"]["CurrentStatus"];
    bool singleCorrect = (singleBit.size() == 1 && singleBit[0].as<int>() == 3);
    recordTest("Single status bit creates single entry with correct value", singleCorrect);
    
    // Test no bits
    doc.clear();
    SDCPProtocol::buildCommandMessage(
        doc, 128, requestId, mainboardId, 1638360000, 13, 0x00
    );
    
    JsonArray noBits = doc["Data"]["CurrentStatus"];
    bool emptyCorrect = (noBits.size() == 0);
    recordTest("Zero status mask creates empty array", emptyCorrect);
}

// Main test runner
int main() {
    std::cout << COLOR_BLUE << "\n"
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║            SDCPProtocol Unit Test Suite                    ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n"
              << COLOR_RESET << "\n";
    
    // Run all tests
    testBuildCommandMessage();
    testBuildCommandMessageNoMainboard();
    testReadExtrusionValueNormalKey();
    testReadExtrusionValueHexKey();
    testReadExtrusionValueFallback();
    testReadExtrusionValueNotFound();
    testReadExtrusionValueNull();
    testReadExtrusionValueNoHexKey();
    testBuildDifferentCommands();
    testMachineStatusMask();
    
    // Print summary
    std::cout << "\n" << COLOR_BLUE << "╔════════════════════════════════════════════════════════════╗" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "║                      Test Summary                          ║" << COLOR_RESET << "\n";
    std::cout << COLOR_BLUE << "╚════════════════════════════════════════════════════════════╝" << COLOR_RESET << "\n\n";
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& result : testResults) {
        if (result.passed) {
            passed++;
        } else {
            failed++;
            std::cout << COLOR_RED << "FAILED: " << result.name << COLOR_RESET << "\n";
            if (!result.details.empty()) {
                std::cout << "  " << result.details << "\n";
            }
        }
    }
    
    std::cout << "\nTotal tests: " << testResults.size() << "\n";
    std::cout << COLOR_GREEN << "Passed: " << passed << COLOR_RESET << "\n";
    std::cout << (failed > 0 ? COLOR_RED : COLOR_GREEN) << "Failed: " << failed << COLOR_RESET << "\n\n";
    
    return (failed == 0) ? 0 : 1;
}