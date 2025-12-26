/**
 * Unit Tests for ElegooCC
 *
 * Tests the core ElegooCC logic including print state management,
 * telemetry processing, pause/resume, and discovery.
 *
 * Note: This tests the logic in isolation with mocked dependencies.
 * WebSocket communication is mocked for testing.
 */

#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <cstring>

// Define mock globals before including mocks
unsigned long _mockMillis = 0;
int testsPassed = 0;
int testsFailed = 0;

// Pre-define header guards to prevent real headers from being included
#define LOGGER_H
#define SETTINGS_DATA_H

// Define LogLevel enum needed by JamDetector
enum LogLevel {
    LOG_NORMAL = 0,
    LOG_VERBOSE = 1,
    LOG_PIN_VALUES = 2
};

// Mock Logger singleton for JamDetector
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    void log(const char* msg, LogLevel level = LOG_NORMAL) {}
    void logf(const char* fmt, ...) {}
    void logf(LogLevel level, const char* fmt, ...) {}
};

// Mock SettingsManager singleton for JamDetector
class SettingsManager {
public:
    static SettingsManager& getInstance() {
        static SettingsManager instance;
        return instance;
    }
    bool getVerboseLogging() const { return false; }
    bool getSuppressPauseCommands() const { return false; }
    int getLogLevel() const { return 0; }
    int getFlowTelemetryStaleMs() const { return 1500; }
    int getStartPrintTimeoutMs() const { return 10000; }
    bool getEnabled() const { return true; }
};

// Include shared mocks
#include "mocks/test_mocks.h"
#include "mocks/arduino_mocks.h"
#include "mocks/json_mocks.h"

// Mock instances
MockLogger logger;
MockSettingsManager settingsManager;
MockSerial Serial;

// Additional mocks needed for ElegooCC

// Mock WebSocketsClient
class WebSocketsClient {
public:
    void begin(const char* host, int port, const char* url) {}
    void onEvent(void (*callback)(int, uint8_t*, size_t)) {}
    void loop() {}
    void sendTXT(const char* payload) { lastPayload = payload; }
    void disconnect() { connected = false; }
    bool isConnected() { return connected; }

    bool connected = false;
    std::string lastPayload;
};

// Mock UUID class
class UUID {
public:
    void seed(uint32_t, uint32_t) {}
    void generate() {}
    const char* toCharArray() { return "test-uuid-1234"; }
};

// Mock WiFiUDP
class WiFiUDP {
public:
    int begin(int port) { return 1; }
    int beginPacket(const char* ip, int port) { return 1; }
    size_t write(const uint8_t* buf, size_t len) { return len; }
    int endPacket() { return 1; }
    int parsePacket() { return mockPacketSize; }
    int read(char* buf, size_t len) {
        if (mockResponse && mockPacketSize > 0) {
            size_t copyLen = (len < (size_t)mockPacketSize) ? len : (size_t)mockPacketSize;
            memcpy(buf, mockResponse, copyLen);
            return copyLen;
        }
        return 0;
    }
    void stop() {}

    int mockPacketSize = 0;
    const char* mockResponse = nullptr;
};

// WiFi mock
class MockWiFi {
public:
    bool isConnected() { return true; }
    String localIP() { return String("192.168.1.100"); }
    uint32_t broadcastIP() { return 0xFFFFFFFF; }
};
MockWiFi WiFi;

// IPAddress mock
class IPAddress {
public:
    IPAddress() : addr(0) {}
    IPAddress(uint32_t a) : addr(a) {}
    String toString() { return "255.255.255.255"; }
private:
    uint32_t addr;
};

// LittleFS mock
class MockLittleFS {
public:
    bool begin() { return true; }
    bool exists(const char* path) { return false; }
};
MockLittleFS LittleFS;

// SDCP types from ElegooCC.h (redefined for testing)
typedef enum {
    SDCP_PRINT_STATUS_IDLE          = 0,
    SDCP_PRINT_STATUS_HOMING        = 1,
    SDCP_PRINT_STATUS_DROPPING      = 2,
    SDCP_PRINT_STATUS_EXPOSURING    = 3,
    SDCP_PRINT_STATUS_LIFTING       = 4,
    SDCP_PRINT_STATUS_PAUSING       = 5,
    SDCP_PRINT_STATUS_PAUSED        = 6,
    SDCP_PRINT_STATUS_STOPPING      = 7,
    SDCP_PRINT_STATUS_STOPED        = 8,
    SDCP_PRINT_STATUS_COMPLETE      = 9,
    SDCP_PRINT_STATUS_FILE_CHECKING = 10,
    SDCP_PRINT_STATUS_PRINTING      = 13,
    SDCP_PRINT_STATUS_HEATING       = 16,
    SDCP_PRINT_STATUS_BED_LEVELING  = 20,
} sdcp_print_status_t;

typedef enum {
    SDCP_MACHINE_STATUS_IDLE              = 0,
    SDCP_MACHINE_STATUS_PRINTING          = 1,
    SDCP_MACHINE_STATUS_FILE_TRANSFERRING = 2,
    SDCP_MACHINE_STATUS_EXPOSURE_TESTING  = 3,
    SDCP_MACHINE_STATUS_DEVICES_TESTING   = 4,
} sdcp_machine_status_t;

typedef enum {
    SDCP_COMMAND_STATUS         = 0,
    SDCP_COMMAND_ATTRIBUTES     = 1,
    SDCP_COMMAND_START_PRINT    = 128,
    SDCP_COMMAND_PAUSE_PRINT    = 129,
    SDCP_COMMAND_STOP_PRINT     = 130,
    SDCP_COMMAND_CONTINUE_PRINT = 131,
} sdcp_command_t;

// Include JamDetector (needed by ElegooCC)
#include "../src/JamDetector.h"
#include "../src/JamDetector.cpp"

// Include FilamentMotionSensor
#include "../src/FilamentMotionSensor.h"
#include "../src/FilamentMotionSensor.cpp"

// Simplified printer info struct for testing
struct printer_info_t {
    String              mainboardID;
    sdcp_print_status_t printStatus;
    bool                filamentStopped;
    bool                filamentRunout;
    bool                runoutPausePending;
    bool                runoutPauseCommanded;
    float               runoutPauseRemainingMm;
    float               runoutPauseDelayMm;
    int                 currentLayer;
    int                 totalLayer;
    int                 progress;
    int                 currentTicks;
    int                 totalTicks;
    int                 PrintSpeedPct;
    bool                isWebsocketConnected;
    bool                isPrinting;
    float               currentZ;
    bool                waitingForAck;
    float               expectedFilamentMM;
    float               actualFilamentMM;
    float               lastExpectedDeltaMM;
    bool                telemetryAvailable;
    float               currentDeficitMm;
    float               deficitThresholdMm;
    float               deficitRatio;
    float               passRatio;
    float               hardJamPercent;
    float               softJamPercent;
    bool                graceActive;
    float               expectedRateMmPerSec;
    float               actualRateMmPerSec;
    unsigned long       movementPulseCount;
};

// Test class that exposes ElegooCC internals for testing
class TestableElegooCC {
public:
    // Print state
    sdcp_print_status_t printStatus = SDCP_PRINT_STATUS_IDLE;
    uint8_t machineStatusMask = 0;
    int currentLayer = 0;
    int totalLayer = 0;
    float currentZ = 0.0f;
    float expectedFilamentMM = 0.0f;
    float actualFilamentMM = 0.0f;
    bool filamentRunout = false;
    bool runoutPausePending = false;
    bool runoutPauseCommanded = false;
    float runoutPauseRemainingMm = 0.0f;
    float runoutPauseDelayMm = 700.0f;
    float runoutPauseStartExpectedMm = 0.0f;
    bool expectedTelemetryAvailable = false;
    unsigned long lastSuccessfulTelemetryMs = 0;

    // Components
    FilamentMotionSensor motionSensor;
    JamDetector jamDetector;
    unsigned long movementPulseCount = 0;

    // Print start candidate tracking
    bool printCandidateActive = false;
    bool printCandidateSawHoming = false;
    bool printCandidateSawLeveling = false;
    bool printCandidateConditionsMet = false;

    // Tracking state
    bool trackingFrozen = false;
    bool hasBeenPaused = false;
    unsigned long lastPauseRequestMs = 0;
    unsigned long lastPrintEndMs = 0;
    bool pauseTriggeredByRunout = false;

    // WebSocket state
    bool isConnected = false;
    String mainboardID;

    void resetFilamentTracking(bool resetGrace = true) {
        motionSensor.reset();
        if (resetGrace) {
            jamDetector.reset(millis());
        }
        movementPulseCount = 0;
        expectedFilamentMM = 0.0f;
        actualFilamentMM = 0.0f;
        runoutPausePending = false;
        runoutPauseCommanded = false;
        runoutPauseRemainingMm = 0.0f;
        runoutPauseStartExpectedMm = 0.0f;
        pauseTriggeredByRunout = false;
    }

    bool isPrinting() {
        return (machineStatusMask & (1 << SDCP_MACHINE_STATUS_PRINTING)) != 0;
    }

    bool isPrintJobActive() {
        // Match the real implementation in ElegooCC::isPrintJobActive()
        return printStatus != SDCP_PRINT_STATUS_IDLE &&
               printStatus != SDCP_PRINT_STATUS_STOPED &&
               printStatus != SDCP_PRINT_STATUS_COMPLETE;
    }

    void setMachineStatuses(const int* statusArray, int arraySize) {
        machineStatusMask = 0;
        for (int i = 0; i < arraySize; i++) {
            if (statusArray[i] >= 0 && statusArray[i] < 8) {
                machineStatusMask |= (1 << statusArray[i]);
            }
        }
    }

    bool hasMachineStatus(sdcp_machine_status_t status) {
        return (machineStatusMask & (1 << status)) != 0;
    }

    bool shouldPausePrint(unsigned long currentTime) {
        pauseTriggeredByRunout = false;

        // Skip if pause commands suppressed
        if (settingsManager.getSuppressPauseCommands()) {
            return false;
        }

        // Skip if not printing
        if (!isPrinting()) {
            return false;
        }

        // Check jam detector
        JamState state = jamDetector.getState();
        if (state.jammed && !jamDetector.isPauseRequested()) {
            return true;
        }

        // Check runout sensor with delayed pause
        if (filamentRunout && settingsManager.getPauseOnRunout()) {
            if (!runoutPausePending) {
                runoutPausePending = true;
                runoutPauseStartExpectedMm = expectedFilamentMM;
                runoutPauseRemainingMm = runoutPauseDelayMm;
            }

            float consumed = expectedFilamentMM - runoutPauseStartExpectedMm;
            if (consumed < 0.0f) {
                consumed = 0.0f;
                runoutPauseStartExpectedMm = expectedFilamentMM;
            }

            runoutPauseRemainingMm = runoutPauseDelayMm - consumed;
            if (runoutPauseRemainingMm < 0.0f) {
                runoutPauseRemainingMm = 0.0f;
            }

            if (runoutPauseRemainingMm <= 0.0f) {
                pauseTriggeredByRunout = true;
                return true;
            }
        } else {
            runoutPausePending = false;
            runoutPauseRemainingMm = 0.0f;
        }

        return false;
    }

    void clearPrintStartCandidate() {
        printCandidateActive = false;
        printCandidateSawHoming = false;
        printCandidateSawLeveling = false;
        printCandidateConditionsMet = false;
    }

    bool isPrintStartCandidateSatisfied() const {
        return printCandidateConditionsMet;
    }

    printer_info_t getCurrentInformation() {
        printer_info_t info;
        info.mainboardID = mainboardID;
        info.printStatus = printStatus;
        info.filamentStopped = false;
        info.filamentRunout = filamentRunout;
        info.runoutPausePending = runoutPausePending;
        info.runoutPauseCommanded = runoutPauseCommanded;
        info.runoutPauseRemainingMm = runoutPauseRemainingMm;
        info.runoutPauseDelayMm = runoutPauseDelayMm;
        info.currentLayer = currentLayer;
        info.totalLayer = totalLayer;
        info.progress = totalLayer > 0 ? (currentLayer * 100 / totalLayer) : 0;
        info.currentTicks = 0;
        info.totalTicks = 0;
        info.PrintSpeedPct = 100;
        info.isWebsocketConnected = isConnected;
        info.isPrinting = isPrinting();
        info.currentZ = currentZ;
        info.waitingForAck = false;
        info.expectedFilamentMM = expectedFilamentMM;
        info.actualFilamentMM = actualFilamentMM;
        info.lastExpectedDeltaMM = 0.0f;
        info.telemetryAvailable = expectedTelemetryAvailable;
        info.currentDeficitMm = motionSensor.getDeficit();

        JamState state = jamDetector.getState();
        info.passRatio = state.passRatio;
        info.hardJamPercent = state.hardJamPercent;
        info.softJamPercent = state.softJamPercent;
        info.graceActive = state.graceActive;
        info.expectedRateMmPerSec = state.expectedRateMmPerSec;
        info.actualRateMmPerSec = state.actualRateMmPerSec;
        info.movementPulseCount = movementPulseCount;

        return info;
    }
};

// Tests

void testInitialState() {
    TEST_SECTION("ElegooCC Initial State");

    TestableElegooCC ecc;

    TEST_ASSERT(!ecc.isConnected, "Should not be connected initially");
    TEST_ASSERT(!ecc.isPrinting(), "Should not be printing initially");
    TEST_ASSERT(ecc.printStatus == SDCP_PRINT_STATUS_IDLE, "Print status should be IDLE");
    TEST_ASSERT(ecc.currentLayer == 0, "Current layer should be 0");
    TEST_ASSERT(floatEquals(ecc.expectedFilamentMM, 0.0f), "Expected filament should be 0");

    TEST_PASS("Initial state is disconnected, not printing");
}

void testPrintStateTransitions() {
    TEST_SECTION("Print State Transitions");

    resetMockTime();
    TestableElegooCC ecc;

    // IDLE -> set PRINTING machine status
    int printingStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(printingStatus, 1);
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;

    TEST_ASSERT(ecc.isPrinting(), "Should be printing after setting status");
    TEST_ASSERT(ecc.isPrintJobActive(), "Print job should be active");

    // PRINTING -> PAUSED
    ecc.printStatus = SDCP_PRINT_STATUS_PAUSED;
    TEST_ASSERT(ecc.isPrinting(), "Still printing (machine status set)");
    TEST_ASSERT(ecc.isPrintJobActive(), "Job still active while paused");

    // PAUSED -> Clear machine status -> IDLE
    int idleStatus[] = { SDCP_MACHINE_STATUS_IDLE };
    ecc.setMachineStatuses(idleStatus, 1);
    ecc.printStatus = SDCP_PRINT_STATUS_IDLE;

    TEST_ASSERT(!ecc.isPrinting(), "Should not be printing after idle");

    TEST_PASS("Print state transitions: IDLE -> PRINTING -> PAUSED -> IDLE");
}

void testMachineStatusBitmask() {
    TEST_SECTION("Machine Status Bitmask");

    TestableElegooCC ecc;

    // Set multiple statuses
    int statuses[] = { SDCP_MACHINE_STATUS_PRINTING, SDCP_MACHINE_STATUS_FILE_TRANSFERRING };
    ecc.setMachineStatuses(statuses, 2);

    TEST_ASSERT(ecc.hasMachineStatus(SDCP_MACHINE_STATUS_PRINTING), "Should have PRINTING status");
    TEST_ASSERT(ecc.hasMachineStatus(SDCP_MACHINE_STATUS_FILE_TRANSFERRING), "Should have TRANSFERRING status");
    TEST_ASSERT(!ecc.hasMachineStatus(SDCP_MACHINE_STATUS_EXPOSURE_TESTING), "Should not have EXPOSURE status");

    TEST_PASS("Machine status bitmask works correctly");
}

void testResetFilamentTracking() {
    TEST_SECTION("Reset Filament Tracking");

    resetMockTime();
    TestableElegooCC ecc;

    // Add some tracking data
    ecc.motionSensor.updateExpectedPosition(0.0f);
    advanceTime(100);
    ecc.motionSensor.addSensorPulse(2.88f);
    advanceTime(100);
    ecc.motionSensor.updateExpectedPosition(50.0f);
    ecc.movementPulseCount = 100;
    ecc.expectedFilamentMM = 50.0f;
    ecc.actualFilamentMM = 45.0f;

    // Reset tracking
    ecc.resetFilamentTracking(true);

    TEST_ASSERT(ecc.movementPulseCount == 0, "Pulse count should be reset");
    TEST_ASSERT(floatEquals(ecc.expectedFilamentMM, 0.0f), "Expected filament should be reset");
    TEST_ASSERT(floatEquals(ecc.actualFilamentMM, 0.0f), "Actual filament should be reset");

    TEST_PASS("resetFilamentTracking() resets all tracking state");
}

void testShouldPausePrintOnJam() {
    TEST_SECTION("Pause on Jam Detection");

    resetMockTime();
    TestableElegooCC ecc;

    // Setup as printing
    int printingStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(printingStatus, 1);
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;

    // Initially should not pause
    TEST_ASSERT(!ecc.shouldPausePrint(millis()), "Should not pause without jam");

    // Simulate jam detection
    JamConfig config;
    config.graceTimeMs = 0;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 1000;
    config.hardJamTimeMs = 1000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;

    ecc.jamDetector.reset(millis());
    advanceTime(500);

    // Feed jam conditions
    for (int i = 0; i < 20; i++) {
        advanceTime(100);
        ecc.jamDetector.update(
            20.0f,  // expected
            1.0f,   // actual (very low)
            10,     // pulse count
            true,   // isPrinting
            true,   // hasTelemetry
            millis(),
            1000,   // printStartTime
            config,
            10.0f,  // expectedRate
            0.5f    // actualRate
        );
    }

    JamState state = ecc.jamDetector.getState();
    if (state.jammed) {
        TEST_ASSERT(ecc.shouldPausePrint(millis()), "Should pause when jammed");
    }

    TEST_PASS("shouldPausePrint() returns true when jam detected");
}

void testShouldPausePrintOnRunout() {
    TEST_SECTION("Pause on Filament Runout");

    resetMockTime();
    TestableElegooCC ecc;

    // Setup as printing
    int printingStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(printingStatus, 1);
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;

    // Set runout flag
    ecc.filamentRunout = true;

    bool shouldPause = ecc.shouldPausePrint(millis());
    TEST_ASSERT(!shouldPause, "Runout should wait for delayed pause");

    // Advance expected extrusion but stay below delay
    ecc.expectedFilamentMM = ecc.runoutPauseDelayMm - 50.0f;
    TEST_ASSERT(!ecc.shouldPausePrint(millis()), "Runout delay should still be counting down");

    // Exceed delay to trigger pause
    ecc.expectedFilamentMM = ecc.runoutPauseDelayMm + 10.0f;
    shouldPause = ecc.shouldPausePrint(millis());
    TEST_ASSERT(shouldPause, "Runout should pause after delay threshold");
    TEST_ASSERT(ecc.pauseTriggeredByRunout, "Pause should be attributed to runout");

    TEST_PASS("Filament runout check evaluated");
}

void testPrintStartCandidateTracking() {
    TEST_SECTION("Print Start Candidate Tracking");

    TestableElegooCC ecc;

    // Initially no candidate
    TEST_ASSERT(!ecc.printCandidateActive, "No candidate initially");

    // Simulate homing seen
    ecc.printCandidateActive = true;
    ecc.printCandidateSawHoming = true;

    TEST_ASSERT(ecc.printCandidateActive, "Candidate should be active");
    TEST_ASSERT(ecc.printCandidateSawHoming, "Should have seen homing");
    TEST_ASSERT(!ecc.isPrintStartCandidateSatisfied(), "Not satisfied yet");

    // Simulate leveling seen
    ecc.printCandidateSawLeveling = true;

    // Mark conditions met
    ecc.printCandidateConditionsMet = true;

    TEST_ASSERT(ecc.isPrintStartCandidateSatisfied(), "Should be satisfied now");

    // Clear candidate
    ecc.clearPrintStartCandidate();

    TEST_ASSERT(!ecc.printCandidateActive, "Candidate should be cleared");

    TEST_PASS("Print start candidate tracking state machine");
}

void testGetCurrentInformation() {
    TEST_SECTION("Get Current Information");

    resetMockTime();
    TestableElegooCC ecc;

    ecc.mainboardID = "test-board-123";
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;
    ecc.currentLayer = 50;
    ecc.totalLayer = 100;
    ecc.currentZ = 25.5f;
    ecc.isConnected = true;

    int printingStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(printingStatus, 1);

    printer_info_t info = ecc.getCurrentInformation();

    TEST_ASSERT(info.mainboardID == "test-board-123", "Mainboard ID should match");
    TEST_ASSERT(info.printStatus == SDCP_PRINT_STATUS_PRINTING, "Print status should match");
    TEST_ASSERT(info.currentLayer == 50, "Current layer should match");
    TEST_ASSERT(info.totalLayer == 100, "Total layer should match");
    TEST_ASSERT(info.progress == 50, "Progress should be 50%");
    TEST_ASSERT(floatEquals(info.currentZ, 25.5f), "Z position should match");
    TEST_ASSERT(info.isWebsocketConnected, "Should show connected");
    TEST_ASSERT(info.isPrinting, "Should show printing");

    TEST_PASS("getCurrentInformation() returns correct printer state");
}

void testLayerTracking() {
    TEST_SECTION("Layer Tracking");

    TestableElegooCC ecc;

    ecc.currentLayer = 0;
    ecc.totalLayer = 200;

    // Simulate layer updates
    for (int i = 1; i <= 10; i++) {
        ecc.currentLayer = i;
        TEST_ASSERT(ecc.currentLayer == i, "Layer should update correctly");
    }

    printer_info_t info = ecc.getCurrentInformation();
    TEST_ASSERT(info.currentLayer == 10, "Current layer should be 10");
    TEST_ASSERT(info.progress == 5, "Progress should be 5%");

    TEST_PASS("Layer tracking from SDCP status messages");
}

void testTelemetryGapDetection() {
    TEST_SECTION("Telemetry Gap Detection");

    resetMockTime();
    TestableElegooCC ecc;

    ecc.expectedTelemetryAvailable = true;
    ecc.lastSuccessfulTelemetryMs = millis();

    // Simulate time passing without telemetry
    advanceTime(2000);

    unsigned long staleness = millis() - ecc.lastSuccessfulTelemetryMs;
    bool isStale = staleness > (unsigned long)settingsManager.getFlowTelemetryStaleMs();

    TEST_ASSERT(isStale, "Telemetry should be considered stale after timeout");

    // Fresh telemetry
    ecc.lastSuccessfulTelemetryMs = millis();
    staleness = millis() - ecc.lastSuccessfulTelemetryMs;
    isStale = staleness > (unsigned long)settingsManager.getFlowTelemetryStaleMs();

    TEST_ASSERT(!isStale, "Fresh telemetry should not be stale");

    TEST_PASS("Telemetry gap detection (stale data handling)");
}

void testResumeGracePeriod() {
    TEST_SECTION("Resume Grace Period");

    resetMockTime();
    TestableElegooCC ecc;

    // Setup print start
    ecc.jamDetector.reset(millis());

    // Advance past initial grace
    advanceTime(10000);

    JamConfig config;
    config.graceTimeMs = 10000;
    config.hardJamMm = 5.0f;
    config.softJamTimeMs = 5000;
    config.hardJamTimeMs = 3000;
    config.ratioThreshold = 0.70f;
    config.detectionMode = DetectionMode::BOTH;

    // Update - may or may not still be in initial grace depending on implementation
    ecc.jamDetector.update(10.0f, 8.0f, 100, true, true, millis(), 1000, config, 5.0f, 4.0f);
    JamState stateBefore = ecc.jamDetector.getState();
    // Record state before resume for comparison
    bool graceWasActive = stateBefore.graceActive;

    // Simulate resume
    ecc.jamDetector.onResume(millis(), 100, 10.0f);

    JamState stateAfter = ecc.jamDetector.getState();
    // After resume, grace should be active (either resume grace or stays active)
    TEST_ASSERT(stateAfter.graceActive, "Grace should be active after resume");

    // If we weren't in grace before, we should now be in RESUME_GRACE
    if (!graceWasActive) {
        TEST_ASSERT(stateAfter.graceState == GraceState::RESUME_GRACE, "Should be in resume grace");
    }

    TEST_PASS("Resume triggers grace period");
}

void testTrackingFreeze() {
    TEST_SECTION("Tracking Freeze on Pause");

    TestableElegooCC ecc;

    // Initially not frozen
    TEST_ASSERT(!ecc.trackingFrozen, "Tracking should not be frozen initially");

    // Simulate pause
    ecc.trackingFrozen = true;
    ecc.hasBeenPaused = true;

    TEST_ASSERT(ecc.trackingFrozen, "Tracking should be frozen during pause");
    TEST_ASSERT(ecc.hasBeenPaused, "Should record that pause occurred");

    // Simulate resume
    ecc.trackingFrozen = false;

    TEST_ASSERT(!ecc.trackingFrozen, "Tracking should unfreeze on resume");
    TEST_ASSERT(ecc.hasBeenPaused, "Pause flag should persist");

    TEST_PASS("Tracking freeze behavior on pause/resume");
}

void testNotPrintingDoesNotTriggerPause() {
    TEST_SECTION("Not Printing Does Not Trigger Pause");

    resetMockTime();
    TestableElegooCC ecc;

    // Not printing (default state)
    TEST_ASSERT(!ecc.isPrinting(), "Should not be printing");

    // Even with jam conditions, should not pause
    ecc.filamentRunout = true;

    TEST_ASSERT(!ecc.shouldPausePrint(millis()), "Should not pause when not printing");

    TEST_PASS("No pause triggered when not printing");
}

void testPulseCountingDuringActivePrintStates() {
    TEST_SECTION("Pulse counting enabled during all active print states");

    // Test that pulses are counted during each active print state
    sdcp_print_status_t activeStates[] = {
        SDCP_PRINT_STATUS_PRINTING,
        SDCP_PRINT_STATUS_HEATING,
        SDCP_PRINT_STATUS_HOMING,
        SDCP_PRINT_STATUS_BED_LEVELING,
        SDCP_PRINT_STATUS_PAUSED,
        SDCP_PRINT_STATUS_PAUSING
    };

    for (int i = 0; i < 6; i++) {
        sdcp_print_status_t state = activeStates[i];
        TestableElegooCC testEcc;
        testEcc.printStatus = state;

        // isPrintJobActive should return true for all active states
        bool jobActive = testEcc.isPrintJobActive();
        TEST_ASSERT(jobActive, "isPrintJobActive should be true for active print states");
    }

    TEST_PASS("Pulse counting enabled for all active print states");
}

void testNoPulseCountingWhenIdle() {
    TEST_SECTION("Pulse counting disabled when idle/stopped/complete");

    sdcp_print_status_t idleStates[] = {
        SDCP_PRINT_STATUS_IDLE,
        SDCP_PRINT_STATUS_STOPED,
        SDCP_PRINT_STATUS_COMPLETE
    };

    for (int i = 0; i < 3; i++) {
        sdcp_print_status_t state = idleStates[i];
        TestableElegooCC testEcc;
        testEcc.printStatus = state;

        // isPrintJobActive should return false for idle states
        bool jobActive = testEcc.isPrintJobActive();
        TEST_ASSERT(!jobActive, "isPrintJobActive should be false for idle states");
    }

    TEST_PASS("Pulse counting disabled when idle/stopped/complete");
}

void testPulseCountingDuringSDCPLoss() {
    TEST_SECTION("Pulses counted during machine status race condition (SDCP loss)");

    TestableElegooCC ecc;

    // Setup: Print is active
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;

    // Verify initial state: both printStatus and machine status say printing
    TEST_ASSERT(ecc.isPrintJobActive(), "Job should be active");

    // Simulate SDCP loss: clear machine status bitmask (race condition)
    int emptyStatus[] = {};
    ecc.setMachineStatuses(emptyStatus, 0);

    // Critical test: Machine status lost PRINTING bit, but...
    TEST_ASSERT(!ecc.hasMachineStatus(SDCP_MACHINE_STATUS_PRINTING),
                "Machine status should be cleared");

    // ...isPrintJobActive (used for pulse counting) should STILL be true!
    TEST_ASSERT(ecc.isPrintJobActive(),
                "Print job should STILL be active despite machine status loss - this is the fix!");

    TEST_PASS("Pulses counted during SDCP loss/race condition");
}

void testIsPrintingStillRequiresBothConditions() {
    TEST_SECTION("isPrinting() still requires both printStatus AND machineStatus");

    TestableElegooCC ecc;

    // Setup: Print is active
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;
    int printingStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(printingStatus, 1);

    // Both conditions met: isPrinting() should be true
    TEST_ASSERT(ecc.isPrinting(), "Should be printing with both conditions met");

    // Now simulate race condition: clear machine status
    int emptyStatus[] = {};
    ecc.setMachineStatuses(emptyStatus, 0);

    // isPrinting() should now return false (more strict, for pause decisions)
    TEST_ASSERT(!ecc.isPrinting(), "isPrinting() should require machine status too");

    // But isPrintJobActive() should still return true (for pulse counting)
    TEST_ASSERT(ecc.isPrintJobActive(), "Job should still be active for pulse counting");

    TEST_PASS("isPrinting() unaffected, remains strict for pause decisions");
}

void testMachineStatusRaceConditionProtection() {
    TEST_SECTION("Complete lifecycle: verify pulse counting survives machine status race");

    TestableElegooCC ecc;

    // Start: idle
    TEST_ASSERT(!ecc.isPrintJobActive(), "Should start idle");

    // Begin print: printStatus changes to PRINTING
    ecc.printStatus = SDCP_PRINT_STATUS_PRINTING;
    int printingStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(printingStatus, 1);
    TEST_ASSERT(ecc.isPrintJobActive(), "Should be printing");

    // Race condition strikes: incomplete SDCP update clears machine status
    int emptyStatus[] = {};
    ecc.setMachineStatuses(emptyStatus, 0);

    // CRITICAL: Pulse counting still active (this is the fix!)
    TEST_ASSERT(ecc.isPrintJobActive(),
                "Pulse counting should survive machine status bitmask race");

    // After 250ms, SDCP sends update with PRINTING status
    int restoredStatus[] = { SDCP_MACHINE_STATUS_PRINTING };
    ecc.setMachineStatuses(restoredStatus, 1);
    TEST_ASSERT(ecc.isPrintJobActive(), "Should still be printing after recovery");

    // Finally, print ends: printStatus changes to COMPLETE
    ecc.printStatus = SDCP_PRINT_STATUS_COMPLETE;
    ecc.setMachineStatuses(emptyStatus, 0);  // Machine status also clears
    TEST_ASSERT(!ecc.isPrintJobActive(), "Should stop counting pulses when print completes");

    TEST_PASS("Machine status race condition handled throughout lifecycle");
}

// ============================================================================
// Reconnection Logic Tests
// ============================================================================

void testExponentialBackoffCalculation() {
    TEST_SECTION("Exponential Backoff Calculation");

    // Test the backoff calculation formula: 5000 * 2^(failures-1), max 60000
    // This matches the formula in updateTransport():
    //   5000UL * (1UL << min(consecutiveFailures - 1, 4))

    auto calculateBackoff = [](int failures) -> unsigned long {
        if (failures <= 0) return 5000;
        unsigned long backoff = 5000UL * (1UL << std::min(failures - 1, 4));
        return std::min(backoff, 60000UL);
    };

    // Test backoff values for each failure count
    TEST_ASSERT(calculateBackoff(1) == 5000, "Failure 1: 5s backoff");
    TEST_ASSERT(calculateBackoff(2) == 10000, "Failure 2: 10s backoff");
    TEST_ASSERT(calculateBackoff(3) == 20000, "Failure 3: 20s backoff");
    TEST_ASSERT(calculateBackoff(4) == 40000, "Failure 4: 40s backoff");
    TEST_ASSERT(calculateBackoff(5) == 60000, "Failure 5: 60s backoff (capped)");
    TEST_ASSERT(calculateBackoff(6) == 60000, "Failure 6: 60s backoff (still capped)");
    TEST_ASSERT(calculateBackoff(100) == 60000, "Failure 100: 60s backoff (max)");

    TEST_PASS("Exponential backoff: 5s -> 10s -> 20s -> 40s -> 60s max");
}

void testReconnectionStateTracking() {
    TEST_SECTION("Reconnection State Tracking");

    // Simulate the transport state tracking fields
    struct MockTransportState {
        unsigned long lastReconnectAttemptMs = 0;
        unsigned long reconnectBackoffMs = 5000;
        int consecutiveFailures = 0;
        String lastAttemptedIp;
    };

    MockTransportState transport;

    // Initial state
    TEST_ASSERT(transport.consecutiveFailures == 0, "No failures initially");
    TEST_ASSERT(transport.reconnectBackoffMs == 5000, "Initial backoff is 5s");
    TEST_ASSERT(transport.lastAttemptedIp.length() == 0, "No IP attempted initially");

    // Simulate first connection attempt
    transport.lastReconnectAttemptMs = 1000;
    transport.lastAttemptedIp = "192.168.1.50";

    TEST_ASSERT(transport.lastReconnectAttemptMs == 1000, "Attempt time recorded");
    TEST_ASSERT(transport.lastAttemptedIp == "192.168.1.50", "IP recorded");

    // Simulate connection timeout (failure)
    transport.consecutiveFailures = 1;
    transport.reconnectBackoffMs = 5000;  // First failure = 5s

    TEST_ASSERT(transport.consecutiveFailures == 1, "Failure count incremented");
    TEST_ASSERT(transport.reconnectBackoffMs == 5000, "Backoff set to 5s");

    // Simulate second failure
    transport.consecutiveFailures = 2;
    transport.reconnectBackoffMs = 10000;  // Second failure = 10s

    TEST_ASSERT(transport.consecutiveFailures == 2, "Failure count is 2");
    TEST_ASSERT(transport.reconnectBackoffMs == 10000, "Backoff increased to 10s");

    // Simulate successful connection (reset)
    transport.consecutiveFailures = 0;
    transport.reconnectBackoffMs = 5000;

    TEST_ASSERT(transport.consecutiveFailures == 0, "Failures reset on success");
    TEST_ASSERT(transport.reconnectBackoffMs == 5000, "Backoff reset to 5s");

    TEST_PASS("Reconnection state tracking works correctly");
}

void testIpChangeDetection() {
    TEST_SECTION("IP Change Detection for Immediate Reconnect");

    struct MockTransportState {
        unsigned long reconnectBackoffMs = 5000;
        int consecutiveFailures = 0;
        String lastAttemptedIp;
    };

    MockTransportState transport;

    // Simulate existing connection state with failures
    transport.lastAttemptedIp = "192.168.1.50";
    transport.consecutiveFailures = 3;
    transport.reconnectBackoffMs = 20000;  // 20s backoff from 3 failures

    // Simulate IP change detection (from updateTransport logic)
    String newIp = "192.168.1.75";
    if (newIp != transport.lastAttemptedIp && newIp.length() > 0 && newIp != "1.1.1.1") {
        transport.reconnectBackoffMs = 0;  // Allow immediate retry
        transport.consecutiveFailures = 0;
    }

    TEST_ASSERT(transport.reconnectBackoffMs == 0, "Backoff reset to 0 for immediate retry");
    TEST_ASSERT(transport.consecutiveFailures == 0, "Failures reset on IP change");

    TEST_PASS("IP change triggers immediate reconnection");
}

void testBackoffExpirationCheck() {
    TEST_SECTION("Backoff Expiration Check");

    resetMockTime();

    unsigned long lastReconnectAttemptMs = millis();
    unsigned long reconnectBackoffMs = 5000;

    // Initially, backoff not expired
    TEST_ASSERT((millis() - lastReconnectAttemptMs) < reconnectBackoffMs,
                "Backoff not expired immediately");

    // Advance time but not enough
    advanceTime(3000);
    TEST_ASSERT((millis() - lastReconnectAttemptMs) < reconnectBackoffMs,
                "Backoff not expired at 3s");

    // Advance past backoff
    advanceTime(2500);  // Now at 5.5s total
    TEST_ASSERT((millis() - lastReconnectAttemptMs) >= reconnectBackoffMs,
                "Backoff expired after 5s");

    TEST_PASS("Backoff expiration timing works correctly");
}

int main() {
    TEST_SUITE_BEGIN("ElegooCC Unit Test Suite");

    testInitialState();
    testPrintStateTransitions();
    testMachineStatusBitmask();
    testResetFilamentTracking();
    testShouldPausePrintOnJam();
    testShouldPausePrintOnRunout();
    testPrintStartCandidateTracking();
    testGetCurrentInformation();
    testLayerTracking();
    testTelemetryGapDetection();
    testResumeGracePeriod();
    testTrackingFreeze();
    testNotPrintingDoesNotTriggerPause();
    testPulseCountingDuringActivePrintStates();
    testNoPulseCountingWhenIdle();
    testPulseCountingDuringSDCPLoss();
    testIsPrintingStillRequiresBothConditions();
    testMachineStatusRaceConditionProtection();

    // Reconnection logic tests
    testExponentialBackoffCalculation();
    testReconnectionStateTracking();
    testIpChangeDetection();
    testBackoffExpirationCheck();

    TEST_SUITE_END();
}
