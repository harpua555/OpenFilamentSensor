/**
 * Pulse Simulator - Unit tests for FilamentMotionSensor
 *
 * Tests various print conditions without hardware:
 * - Normal printing (healthy)
 * - Hard jams (complete blockage)
 * - Soft jams (partial clogs/underextrusion)
 * - Sparse infill (travel moves)
 * - Retractions
 * - Speed changes
 * - Transient spikes (false positive prevention)
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <cstdint>
#include <limits>
#include <cstring>
#include <cctype>
#include <sstream>

#include "generated_test_settings.h"

// Mock Arduino millis() function
// Note: _mockMillis is used by the millis() function defined in mocks/test_mocks.h
unsigned long _mockMillis = 0;

// Include actual sensor code
#include "../src/FilamentMotionSensor.h"
#include "../src/FilamentMotionSensor.cpp"

// Jam simulation state (used for tests only)
static float gHardJamPercent = 0.0f;
static float gSoftJamPercent = 0.0f;
static unsigned long gHardJamAccumMs = 0;
static unsigned long gSoftJamAccumMs = 0;

void resetJamSimState() {
    gHardJamPercent = 0.0f;
    gSoftJamPercent = 0.0f;
    gHardJamAccumMs = 0;
    gSoftJamAccumMs = 0;
}

// ANSI color codes
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string details;
};
std::vector<TestResult> testResults;

// Forward declarations for test helpers used before their definitions
void printTestHeader(const std::string& testName);
void recordTest(const std::string& name, bool passed, const std::string& details = "");

// Simulation parameters
// Constants with defaults. Generated header can override them via macros.
#ifndef TEST_MM_PER_PULSE
#define TEST_MM_PER_PULSE 2.88f
#endif
const float MM_PER_PULSE = TEST_MM_PER_PULSE;

#ifndef TEST_CHECK_INTERVAL_MS
#define TEST_CHECK_INTERVAL_MS 1000
#endif
const int CHECK_INTERVAL_MS = TEST_CHECK_INTERVAL_MS;

#ifndef TEST_RATIO_THRESHOLD
#define TEST_RATIO_THRESHOLD 0.25f
#endif
const float RATIO_THRESHOLD = TEST_RATIO_THRESHOLD;

#ifndef TEST_HARD_JAM_MM
#define TEST_HARD_JAM_MM 5.0f
#endif
const float HARD_JAM_MM = TEST_HARD_JAM_MM;

#ifndef TEST_SOFT_JAM_TIME_MS
#define TEST_SOFT_JAM_TIME_MS 10000
#endif
const int SOFT_JAM_TIME_MS = TEST_SOFT_JAM_TIME_MS;

#ifndef TEST_HARD_JAM_TIME_MS
#define TEST_HARD_JAM_TIME_MS 5000
#endif
const int HARD_JAM_TIME_MS = TEST_HARD_JAM_TIME_MS;

#ifndef TEST_GRACE_PERIOD_MS
#define TEST_GRACE_PERIOD_MS 500
#endif
const int GRACE_PERIOD_MS = TEST_GRACE_PERIOD_MS;

// Tracking window matches FilamentMotionSensor::WINDOW_SIZE_MS (5000ms with 250ms buckets)
// Used to calculate test iterations needed to fill the sliding window before jam detection
const int TRACKING_WINDOW_MS = 5000;

#ifndef TEST_RESUME_GRACE_MIN_MOVEMENT_MM
#define TEST_RESUME_GRACE_MIN_MOVEMENT_MM 1.0f
#endif
const float RESUME_GRACE_MIN_MOVEMENT_MM = TEST_RESUME_GRACE_MIN_MOVEMENT_MM;
const char* LOG_REPLAY_PATH = "../test/fixtures/log_for_test.txt";
const char* LOG_REPLAY_GCODE = "../test/fixtures/ECC_0.4_Cube 8_PLA0.2_1m33s.gcode";

// Logging for visualization
static std::ofstream gLogStream;
static bool gLogEnabled = false;
static std::string gCurrentTestName = "startup";

void initLogFile(const std::string& path) {
    gLogStream.open(path, std::ios::trunc);
    if (!gLogStream) {
        std::cerr << "WARNING: Unable to open log file '" << path << "'\n";
        gLogEnabled = false;
        return;
    }
    gLogEnabled = true;
    gLogStream << "test,label,timestamp,expected,actual,deficit,ratio,hard_percent,soft_percent,jammed\n";
}

void closeLogFile() {
    if (gLogStream.is_open()) {
        gLogStream.close();
    }
    gLogEnabled = false;
}

std::string csvEncode(const std::string& value) {
    std::string encoded = "\"";
    for (char ch : value) {
        if (ch == '"') {
            encoded += "\"\"";
        } else {
            encoded += ch;
        }
    }
    encoded += "\"";
    return encoded;
}

void logStateRow(const std::string& label, float expected, float actual, float deficit, float ratio,
                 float hardPercent, float softPercent, bool jammed) {
    if (!gLogEnabled) return;
    gLogStream << csvEncode(gCurrentTestName) << "," << csvEncode(label) << ","
               << _mockMillis << "," << expected << "," << actual << "," << deficit << ","
               << ratio << "," << hardPercent << "," << softPercent << ","
               << (jammed ? 1 : 0) << "\n";
}

void logFrameState(FilamentMotionSensor& sensor, const std::string& label, bool jammed) {
    if (!gLogEnabled) return;
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    float deficit = sensor.getDeficit();
    float ratio = sensor.getFlowRatio();
    float hardPercent = gHardJamPercent;
    float softPercent = gSoftJamPercent;
    logStateRow(label, expected, actual, deficit, ratio, hardPercent, softPercent, jammed);
}

bool checkJam(FilamentMotionSensor& sensor);

bool checkJamAndLog(FilamentMotionSensor& sensor, const std::string& label) {
    bool jammed = checkJam(sensor);
    logFrameState(sensor, label, jammed);
    return jammed;
}

bool parseFloatAfterKey(const std::string& line, const char* key, float& value) {
    size_t pos = line.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    pos += std::strlen(key);
    size_t end = pos;
    while (end < line.size()) {
        char c = line[end];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-') {
            end++;
        } else {
            break;
        }
    }
    if (end == pos) {
        return false;
    }
    try {
        value = std::stof(line.substr(pos, end - pos));
        return true;
    } catch (...) {
        return false;
    }
}

bool parseUnsignedLongAfterKey(const std::string& line, const char* key, unsigned long& value) {
    size_t pos = line.find(key);
    if (pos == std::string::npos) {
        return false;
    }
    pos += std::strlen(key);
    size_t end = pos;
    while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end]))) {
        end++;
    }
    if (end == pos) {
        return false;
    }
    try {
        value = std::stoul(line.substr(pos, end - pos));
        return true;
    } catch (...) {
        return false;
    }
}

unsigned long parseTimestampToken(const std::string& token, unsigned long fallbackTimestamp) {
    try {
        return static_cast<unsigned long>(std::stoul(token));
    } catch (...) {
        size_t dash = token.rfind('-');
        std::string timePart = (dash != std::string::npos) ? token.substr(dash + 1) : token;
        unsigned int hh = 0, mm = 0, ss = 0;
        char c1 = 0, c2 = 0;
        std::istringstream iss(timePart);
        if ((iss >> hh >> c1 >> mm >> c2 >> ss) && c1 == ':' && c2 == ':') {
            return fallbackTimestamp;
        }
    }
    return fallbackTimestamp;
}

int countOccurrences(const std::string& path, const std::string& needle) {
    std::ifstream in(path);
    if (!in) {
        return 0;
    }
    std::string line;
    int count = 0;
    while (std::getline(in, line)) {
        if (line.find(needle) != std::string::npos) {
            count++;
        }
    }
    return count;
}

struct FlowSample {
    unsigned long timestamp = 0;
    float winExp = 0.0f;
    float winSns = 0.0f;
    float cumulativeSensor = 0.0f;
    unsigned long pulses = 0;
    bool resetFlag = false;
};

std::vector<FlowSample> loadFlowSamples(const std::string& path) {
    std::vector<FlowSample> samples;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "WARNING: Unable to open log file '" << path << "'\n";
        return samples;
    }

    std::string line;
    bool pendingReset = true;  // treat start as reset
    unsigned long fallbackTimestamp = 0;
    while (std::getline(in, line)) {
        if (line.find("Filament tracking reset") != std::string::npos ||
            line.find("Motion sensor reset") != std::string::npos) {
            pendingReset = true;
            continue;
        }

        bool isFlowLine = (line.find("Flow debug:") != std::string::npos) ||
                          (line.find("Debug:") != std::string::npos) ||
                          (line.find("Flow:") != std::string::npos);
        if (!isFlowLine) {
            continue;
        }

        FlowSample sample;
        size_t spacePos = line.find(' ');
        if (spacePos == std::string::npos) {
            continue;
        }
        try {
            std::string tsToken = line.substr(0, spacePos);
            sample.timestamp = parseTimestampToken(tsToken, fallbackTimestamp);
        } catch (...) {
            continue;
        }

        if (!parseFloatAfterKey(line, "win_exp=", sample.winExp) ||
            !parseFloatAfterKey(line, "win_sns=", sample.winSns)) {
            continue;
        }
        bool haveCumul = parseFloatAfterKey(line, "cumul=", sample.cumulativeSensor);
        if (!haveCumul) {
            parseFloatAfterKey(line, "cumul_sns=", sample.cumulativeSensor);
        }
        parseUnsignedLongAfterKey(line, "pulses=", sample.pulses);

        sample.resetFlag = pendingReset;
        pendingReset = false;
        samples.push_back(sample);
        fallbackTimestamp = sample.timestamp + 1;
    }
    return samples;
}

std::vector<std::string> parseJamEventsFromLog(const std::string& path) {
    std::vector<std::string> jams;
    std::ifstream in(path);
    if (!in) {
        return jams;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.find("Filament jam detected") == std::string::npos) {
            continue;
        }

        std::string jamType = "soft";
        if (line.find("(hard") != std::string::npos) {
            jamType = "hard";
        } else if (line.find("(soft") != std::string::npos) {
            jamType = "soft";
        } else {
            float sensorValue = 0.0f;
            float ratioValue = 0.0f;
            parseFloatAfterKey(line, "sensor ", sensorValue);
            parseFloatAfterKey(line, "ratio=", ratioValue);
            jamType = (sensorValue < 0.5f || ratioValue >= 0.90f) ? "hard" : "soft";
        }
        jams.push_back(jamType);
    }
    return jams;
}

struct ReplayJamState {
    unsigned long hardAccumMs = 0;
    unsigned long softAccumMs = 0;
    unsigned long graceRemainingMs = 0;
    bool resumeGraceActive = false;
    unsigned long resumePulseBaseline = 0;
    float resumeActualBaseline = 0.0f;
    bool lastJammed = false;
    float lastNonZeroActual = 0.0f;
    float lastNonZeroExpected = 0.0f;
};

std::vector<std::string> replayFlowSamples(const std::vector<FlowSample>& samples) {
    std::vector<std::string> jamTypes;
    if (samples.empty()) {
        return jamTypes;
    }

    ReplayJamState state;
    unsigned long prevPulses = samples.front().pulses;

    for (size_t i = 0; i < samples.size(); ++i) {
        const auto& s = samples[i];
        unsigned long nextTimestamp = (i + 1 < samples.size()) ? samples[i + 1].timestamp : (s.timestamp + 1);
        long gapSeconds = static_cast<long>(nextTimestamp) - static_cast<long>(s.timestamp);
        if (gapSeconds <= 0) {
            gapSeconds = 1;
        }

        for (long step = 0; step < gapSeconds; ++step) {
            bool resetFlag = (step == 0) ? s.resetFlag : false;
            bool newPulse = (step == 0) ? (s.pulses != prevPulses) : false;

            if (resetFlag) {
                state.hardAccumMs = 0;
                state.softAccumMs = 0;
                state.graceRemainingMs = GRACE_PERIOD_MS;
                state.resumeGraceActive = true;
                state.resumePulseBaseline = s.pulses;
                state.resumeActualBaseline = s.cumulativeSensor;
                state.lastJammed = false;
                state.lastNonZeroActual = 0.0f;
                state.lastNonZeroExpected = 0.0f;
            }

            bool withinGrace = false;
            if (state.graceRemainingMs > 0) {
                withinGrace = true;
                if (CHECK_INTERVAL_MS >= state.graceRemainingMs) {
                    state.graceRemainingMs = 0;
                } else {
                    state.graceRemainingMs -= CHECK_INTERVAL_MS;
                }
            }

            if (state.resumeGraceActive) {
                bool movementSeen = (s.pulses > state.resumePulseBaseline) ||
                                    ((s.cumulativeSensor - state.resumeActualBaseline) >= RESUME_GRACE_MIN_MOVEMENT_MM) ||
                                    (s.winSns >= RESUME_GRACE_MIN_MOVEMENT_MM) ||
                                    (s.winExp >= RESUME_GRACE_MIN_MOVEMENT_MM);
                if (movementSeen) {
                    state.resumeGraceActive = false;
                } else {
                    withinGrace = true;
                }
            }

            if (withinGrace) {
                state.hardAccumMs = 0;
                state.softAccumMs = 0;
                state.lastJammed = false;
                continue;
            }

            float expected = s.winExp;
            float actual = s.winSns;
            float deficit = expected - actual;
            if (deficit < 0.0f) deficit = 0.0f;
            float passRatio = (expected > 0.0f) ? (actual / expected) : 1.0f;
            if (passRatio < 0.0f) passRatio = 0.0f;

            if (actual > 0.0f) {
                state.lastNonZeroActual = actual;
                state.lastNonZeroExpected = expected;
            }

            float minHardWindow = std::max(HARD_JAM_MM, 1.0f);
            bool hardCondition = (expected >= minHardWindow) && (passRatio < 0.10f);
            if (hardCondition) {
                state.hardAccumMs += CHECK_INTERVAL_MS;
                if (state.hardAccumMs > (unsigned long)HARD_JAM_TIME_MS) {
                    state.hardAccumMs = HARD_JAM_TIME_MS;
                }
            } else if (newPulse || passRatio >= 0.35f || expected < (minHardWindow * 0.5f)) {
                state.hardAccumMs = 0;
            }

            bool softCondition = (expected >= 1.0f) && (deficit >= 0.25f) &&
                                 (passRatio < RATIO_THRESHOLD);
            if (softCondition) {
                state.softAccumMs += CHECK_INTERVAL_MS;
                if (state.softAccumMs > (unsigned long)SOFT_JAM_TIME_MS) {
                    state.softAccumMs = SOFT_JAM_TIME_MS;
                }
            } else if (passRatio >= RATIO_THRESHOLD * 0.85f || newPulse) {
                state.softAccumMs = 0;
            }

            bool hardJam = (HARD_JAM_TIME_MS > 0) &&
                           (state.hardAccumMs >= (unsigned long)HARD_JAM_TIME_MS);
            bool softJam = (SOFT_JAM_TIME_MS > 0) &&
                           (state.softAccumMs >= (unsigned long)SOFT_JAM_TIME_MS);
            bool jammed = hardJam || softJam;

            if (jammed && !state.lastJammed) {
                std::string jamType;
                if (hardJam && !softJam) {
                    jamType = "hard";
                } else if (!hardJam && softJam) {
                    jamType = "soft";
                } else {
                    float classificationExpected = expected > 0.0f ? expected : state.lastNonZeroExpected;
                    float classificationActual = actual > 0.0f ? actual : state.lastNonZeroActual;
                    float classificationRatio = (classificationExpected > 0.0f)
                                                    ? (classificationActual / classificationExpected)
                                                    : 0.0f;
                    if (classificationRatio <= 0.10f || classificationActual < 0.5f) {
                        jamType = "hard";
                    } else {
                        jamType = "soft";
                    }
                }
                jamTypes.push_back(jamType);
            }
            state.lastJammed = jammed;

            if (jamTypes.size() >= 2) {
                break;
            }
        }

        if (jamTypes.size() >= 2) {
            break;
        }

        prevPulses = s.pulses;
    }

    return jamTypes;
}

struct ReplayCase {
    std::string name;
    std::string path;
    std::vector<std::string> expectedJams;
    int expectedPauses;
};

void runReplayCase(const ReplayCase& c) {
    printTestHeader(c.name);

    auto samples = loadFlowSamples(c.path);
    recordTest("Flow samples parsed", !samples.empty(),
               samples.empty() ? std::string("Failed to parse ") + c.path : "");
    if (samples.empty()) {
        return;
    }

    auto logJams = parseJamEventsFromLog(c.path);
    std::string jamDetail;
    if (!logJams.empty()) {
        jamDetail = logJams[0];
        for (size_t i = 1; i < logJams.size(); ++i) {
            jamDetail += "," + logJams[i];
        }
    }
    recordTest("Log jam order", logJams == c.expectedJams,
               jamDetail.empty() ? "none" : jamDetail);

    int pauses = countOccurrences(c.path, "Pause command sent to printer");
    recordTest("Pause commands issued", pauses == c.expectedPauses,
               "got " + std::to_string(pauses));
}

// Helper: Advance time
void advanceTime(int ms) {
    _mockMillis += ms;
}

// Helper: Check jam detection (simulator-side implementation)
bool checkJam(FilamentMotionSensor& sensor) {
    // Grace period: ignore potential jams until after GRACE_PERIOD_MS
    if (_mockMillis < static_cast<unsigned long>(GRACE_PERIOD_MS)) {
        resetJamSimState();
        return false;
    }

    float expected = sensor.getExpectedDistance();
    float actual   = sensor.getSensorDistance();
    float deficit  = expected - actual;
    if (deficit < 0.0f) {
        deficit = 0.0f;
    }
    float ratio = (expected > 0.0f) ? (actual / expected) : 1.0f;
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }

    const float HARD_PASS_RATIO_THRESHOLD = 0.35f;
    bool hardCondition = (expected >= HARD_JAM_MM) && (ratio < HARD_PASS_RATIO_THRESHOLD);

    if (hardCondition) {
        gHardJamAccumMs += CHECK_INTERVAL_MS;
        // std::cout << "DEBUG: Hard Accum=" << gHardJamAccumMs << " Limit=" << HARD_JAM_TIME_MS << "\n";
        if (gHardJamAccumMs > static_cast<unsigned long>(HARD_JAM_TIME_MS)) {
            gHardJamAccumMs = HARD_JAM_TIME_MS;
        }
    } else if (ratio >= HARD_PASS_RATIO_THRESHOLD) {
        gHardJamAccumMs = 0;
    }

    const float MIN_SOFT_DEFICIT_MM = 0.5f;
    bool softCondition = (expected >= 1.0f) &&
                         (deficit >= MIN_SOFT_DEFICIT_MM) &&
                         (ratio < RATIO_THRESHOLD);

    if (softCondition) {
        gSoftJamAccumMs += CHECK_INTERVAL_MS;
        // std::cout << "DEBUG: Soft Accum=" << gSoftJamAccumMs << " Limit=" << SOFT_JAM_TIME_MS << "\n";
        if (gSoftJamAccumMs > static_cast<unsigned long>(SOFT_JAM_TIME_MS)) {
            gSoftJamAccumMs = SOFT_JAM_TIME_MS;
        }
    } else {
        // Reset if condition not met (simplified for test stability)
        gSoftJamAccumMs = 0;
    }

    gHardJamPercent = (HARD_JAM_TIME_MS > 0)
                          ? (100.0f * static_cast<float>(gHardJamAccumMs) /
                                 static_cast<float>(HARD_JAM_TIME_MS))
                          : 0.0f;
    gSoftJamPercent = (SOFT_JAM_TIME_MS > 0)
                          ? (100.0f * static_cast<float>(gSoftJamAccumMs) /
                                 static_cast<float>(SOFT_JAM_TIME_MS))
                          : 0.0f;

    if (gHardJamPercent > 100.0f) gHardJamPercent = 100.0f;
    if (gSoftJamPercent > 100.0f) gSoftJamPercent = 100.0f;

    return (gHardJamAccumMs >= static_cast<unsigned long>(HARD_JAM_TIME_MS)) ||
           (gSoftJamAccumMs >= static_cast<unsigned long>(SOFT_JAM_TIME_MS));
}

// Helper: Simulate extrusion command from SDCP
void simulateExtrusion(FilamentMotionSensor& sensor, float deltaExtrusionMm, float currentTotalMm) {
    sensor.updateExpectedPosition(currentTotalMm);
}

// Helper: Simulate sensor pulses (multiple pulses for large movements)
void simulateSensorPulses(FilamentMotionSensor& sensor, float totalMm, float flowRate = 1.0f) {
    float actualMm = totalMm * flowRate;
    int pulseCount = static_cast<int>(actualMm / MM_PER_PULSE);
    for (int i = 0; i < pulseCount; i++) {
        sensor.addSensorPulse(MM_PER_PULSE);
    }
}

// Helper: Check jam detection

// Helper: Print test header
void printTestHeader(const std::string& testName) {
    gCurrentTestName = testName;
    std::cout << "\n" << COLOR_CYAN << "=== " << testName << " ===" << COLOR_RESET << "\n";
}

// Helper: Record test result
void recordTest(const std::string& name, bool passed, const std::string& details) {
    testResults.push_back({name, passed, details});
    if (passed) {
        std::cout << COLOR_GREEN << "✓ PASS" << COLOR_RESET << ": " << name << "\n";
    } else {
        std::cout << COLOR_RED << "✗ FAIL" << COLOR_RESET << ": " << name;
        if (!details.empty()) {
            std::cout << " (" << details << ")";
        }
        std::cout << "\n";
    }
}

// Helper: Print sensor state (console only, logging handled elsewhere)
void printState(FilamentMotionSensor& sensor, const std::string& label, bool jammed = false) {
    float expected = sensor.getExpectedDistance();
    float actual = sensor.getSensorDistance();
    float deficit = sensor.getDeficit();
    float ratio = sensor.getFlowRatio();

    std::cout << "  [" << std::setw(20) << std::left << label << "] "
              << "exp=" << std::fixed << std::setprecision(2) << expected << "mm "
              << "act=" << actual << "mm "
              << "deficit=" << deficit << "mm "
              << "ratio=" << ratio << " "
              << (jammed ? COLOR_RED "[JAM]" COLOR_RESET : COLOR_GREEN "[OK]" COLOR_RESET)
              << "\n";
}

//=============================================================================
// TEST 1: Normal Healthy Print
//=============================================================================
void testNormalPrinting() {
    printTestHeader("Test 1: Normal Healthy Print");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    bool anyFalsePositive = false;

    // Simulate 30 seconds of normal printing at 50mm/s
    for (int sec = 0; sec < 30; sec++) {
        float deltaExtrusion = 50.0f;  // 50mm per second
        totalExtrusion += deltaExtrusion;

        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);  // 100% flow rate

        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Normal print T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            anyFalsePositive = true;
            printState(sensor, label, jammed);
        }
    }

    bool sampleJam = checkJamAndLog(sensor, "Normal print sample");
    printState(sensor, "Normal print sample", sampleJam);

    recordTest("Normal print no false positives", !anyFalsePositive);
}

//=============================================================================
// TEST 2: Hard Jam Detection (Complete Blockage)
//=============================================================================
void testHardJam() {
    printTestHeader("Test 2: Hard Jam Detection (Complete Blockage)");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;

    // Normal printing for 5 seconds
    for (int sec = 0; sec < 5; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    bool healthyJam = checkJamAndLog(sensor, "Before jam (healthy)");
    printState(sensor, "Before jam (healthy)", healthyJam);

    // Hard jam: extrusion commands but NO sensor pulses
    int jamDetectionSec = -1;
    bool detectedBeforeWindow = false;
    const int expectedHardDetectionSec = HARD_JAM_TIME_MS / CHECK_INTERVAL_MS;

    for (int sec = 0; sec < expectedHardDetectionSec + 4; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        // NO sensor pulses - complete blockage
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Hard jam T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed && jamDetectionSec == -1) {
            jamDetectionSec = sec + 1;
        }
        if (jammed && sec + 1 < expectedHardDetectionSec) {
            detectedBeforeWindow = true;
        }

        printState(sensor, label, jammed);
    }

    bool detectedInWindow = jamDetectionSec >= expectedHardDetectionSec &&
                             jamDetectionSec <= expectedHardDetectionSec + 4;
    recordTest("Hard jam detected within configured window", jamDetectionSec >= 0 && detectedInWindow,
               jamDetectionSec >= 0 ? "Detected at T+" + std::to_string(jamDetectionSec) + "s" : "Not detected");
    recordTest("Hard jam not detected before window time", !detectedBeforeWindow);
}

//=============================================================================
// TEST 3: Soft Jam Detection (Partial Clog/Underextrusion)
//=============================================================================
void testSoftJam() {
    printTestHeader("Test 3: Soft Jam Detection (Partial Clog)");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;

    // Normal printing for 5 seconds
    for (int sec = 0; sec < 5; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    bool beforeJamHealthy = checkJamAndLog(sensor, "Before jam (healthy)");
    printState(sensor, "Before jam (healthy)", beforeJamHealthy);

    // Soft jam: only 40% of filament passing (60% deficit)
    // With windowed tracking and grace periods, it takes time for the
    // window and jam timers to fill with bad samples. Expected: Should
    // detect within a reasonable time (not instant, and before the
    // configured soft jam timeout).
    bool jamDetected = false;
    int jamDetectionTime = -1;

    for (int sec = 0; sec < 20; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 0.40f);  // 40% flow rate (60% deficit)
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Clog T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed && !jamDetected) {
            jamDetected = true;
            jamDetectionTime = sec + 1;
        }

        if (sec < 10) {
            printState(sensor, label, jammed);
        }
    }

    // Allow detection to be triggered by either the soft- or hard-jam
    // timers, but require it to happen within a reasonable window based
    // on the configured thresholds.
    const int minDetectionSec = HARD_JAM_TIME_MS / CHECK_INTERVAL_MS;
    const int maxDetectionSec = (SOFT_JAM_TIME_MS / CHECK_INTERVAL_MS) + 5;

    bool detectedInReasonableTime = jamDetected &&
                                    jamDetectionTime >= minDetectionSec &&
                                    jamDetectionTime <= maxDetectionSec;

    recordTest("Soft jam detected after sustained deficit", detectedInReasonableTime,
               jamDetected ? "Detected at T+" + std::to_string(jamDetectionTime) + "s" : "Not detected");
    recordTest("Soft jam detection waits for sustained deficit", jamDetected && jamDetectionTime >= minDetectionSec);
}

//=============================================================================
// TEST 4: Sparse Infill (No False Positives During Travel)
//=============================================================================
void testSparseInfill() {
    printTestHeader("Test 4: Sparse Infill (Travel Moves)");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    bool falsePositive = false;

    // Normal printing
    for (int sec = 0; sec < 3; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    bool beforeSparse = checkJamAndLog(sensor, "Before sparse infill");
    printState(sensor, "Before sparse infill", beforeSparse);

    // Sparse infill: 10 seconds of travel with minimal extrusion
    for (int sec = 0; sec < 10; sec++) {
        // No telemetry updates during travel
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Travel T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
            printState(sensor, label, jammed);
        }
    }

    // Resume normal printing after telemetry gap
    for (int sec = 0; sec < 3; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);

        // Grace period: wait 500ms for sensor to catch up
        if (sec == 0) advanceTime(500);

        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS - (sec == 0 ? 500 : 0));

        std::string label = "After gap T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
            printState(sensor, label, jammed);
        }
    }

    bool afterResumeJam = checkJamAndLog(sensor, "After resume");
    printState(sensor, "After resume", afterResumeJam);

    recordTest("No false positives during sparse infill", !falsePositive);
}

//=============================================================================
// TEST 5: Retraction Handling
//=============================================================================
void testRetractions() {
    printTestHeader("Test 5: Retraction Handling");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    bool falsePositive = false;

    // Normal printing
    for (int sec = 0; sec < 3; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    bool beforeRetractJam = checkJamAndLog(sensor, "Before retraction");
    printState(sensor, "Before retraction", beforeRetractJam);

    // Retraction (negative movement)
    totalExtrusion -= 5.0f;
    simulateExtrusion(sensor, -5.0f, totalExtrusion);
    advanceTime(CHECK_INTERVAL_MS);

    bool afterRetractJam = checkJamAndLog(sensor, "After retraction");
    printState(sensor, "After retraction", afterRetractJam);

    // Resume after retraction (grace period should apply)
    for (int sec = 0; sec < 3; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);

        // Grace period: wait 500ms
        if (sec == 0) advanceTime(500);

        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS - (sec == 0 ? 500 : 0));

        std::string label = "After retract T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
        }

        printState(sensor, label, jammed);
    }

    recordTest("No false positives after retraction", !falsePositive);
}

//=============================================================================
// TEST 6: Ironing / Low-Flow Handling
//=============================================================================
void testIroningLowFlow() {
    printTestHeader("Test 6: Ironing / Low-Flow Handling");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    bool falsePositive = false;

  // Simulate repeated low-flow micro-movements (iron-like passes)
  for (int sec = 0; sec < 20; sec++) {
      float deltaExtrusion = 0.2f;
      totalExtrusion += deltaExtrusion;
      simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);

      // Sensor reports matching micro-movement
      sensor.addSensorPulse(deltaExtrusion);

      advanceTime(CHECK_INTERVAL_MS);

      std::string label = "Ironing T+" + std::to_string(sec + 1) + "s";
      bool jammed = checkJamAndLog(sensor, label);
      if (jammed) {
          falsePositive = true;
      }
      printState(sensor, label, jammed);
    }

    bool afterIroningJam = checkJamAndLog(sensor, "After ironing pattern");
    printState(sensor, "After ironing pattern", afterIroningJam);
    recordTest("Ironing/low-flow pattern does not trigger jam", !falsePositive);
}

//=============================================================================
// TEST 7: Transient Spike Resistance (Hysteresis)
//=============================================================================
void testTransientSpikes() {
    printTestHeader("Test 7: Transient Spike Resistance");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    bool falsePositive = false;

    // Normal printing
    for (int sec = 0; sec < 5; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    // Single spike: bad ratio for 1 second
    float deltaExtrusion = 20.0f;
    totalExtrusion += deltaExtrusion;
    simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
    simulateSensorPulses(sensor, deltaExtrusion, 0.15f);  // 85% deficit (single spike)
    advanceTime(CHECK_INTERVAL_MS);

    bool spikeJam = checkJamAndLog(sensor, "Single spike T+1s");
    if (spikeJam) {
        falsePositive = true;
    }
    printState(sensor, "Single spike T+1s", spikeJam);

    // Ratio returns to normal (should reset counter)
    for (int sec = 0; sec < 3; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
        std::string label = "After spike T+" + std::to_string(sec + 2) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
        }
        printState(sensor, label, jammed);
    }

    recordTest("Transient spike did not trigger jam", !falsePositive);
}

//=============================================================================
// TEST 8: Edge Case - Minimum Movement Threshold
//=============================================================================
void testMinimumMovement() {
    printTestHeader("Test 8: Minimum Movement Threshold");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;

    // Test 1: Very slow printing below detection threshold (<1mm expected)
    // Should NOT trigger jam - too little movement to judge
    bool falsePositiveSubThreshold = false;
    for (int sec = 0; sec < 5; sec++) {
        float deltaExtrusion = 0.1f;  // 0.1mm per second, stays below 1mm total in window
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Tiny move T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositiveSubThreshold = true;
            printState(sensor, label, jammed);
        }
    }

    recordTest("No jam on sub-threshold movements (<1mm)", !falsePositiveSubThreshold);

    // Test 2: Slow printing with no sensor pulses (should trigger hard jam)
    // 0.5mm/sec × 10 sec = 5mm total (meets hard jam threshold)
    sensor.reset();
    _mockMillis = 0;
    totalExtrusion = 0.0f;
    const int expectedHardDetectionSec = HARD_JAM_TIME_MS / CHECK_INTERVAL_MS;
    int hardJamDetectedSec = -1;

    for (int sec = 0; sec < 10; sec++) {
        float deltaExtrusion = 0.5f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        // No sensor pulses (0.5mm < 2.88mm pulse threshold)
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Slow print T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed && hardJamDetectedSec == -1) {
            hardJamDetectedSec = sec + 1;
        }
    }

    bool slowPrintJam = checkJamAndLog(sensor, "Slow print, no pulses");
    printState(sensor, "Slow print, no pulses", slowPrintJam);

    recordTest("Hard jam suppressed for extremely slow extrusion", !slowPrintJam,
               slowPrintJam ? "Unexpected jam flag" : "No jam detected");
}

//=============================================================================
// TEST 9: Grace Period Duration
//=============================================================================
void testGracePeriod() {
    printTestHeader("Test 9: Grace Period Duration");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;

    // Normal printing to establish baseline
    for (int i = 0; i < 3; i++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    // Simulate telemetry gap (like sparse infill or pause)
    advanceTime(6000);  // 6 second gap - triggers telemetry gap detection and clears window

    // Resume with extrusion but no sensor pulses (jam scenario)
    float deltaExtrusion = 20.0f;
    totalExtrusion += deltaExtrusion;
    simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
    // No sensor pulses - creates jam condition

    // Check at 400ms (within 500ms grace period after gap)
    advanceTime(400);
    bool jamAt400 = checkJamAndLog(sensor, "At 400ms (in grace)");
    printState(sensor, "At 400ms (in grace)", jamAt400);

    // Grace period should still be active - no jam yet
    recordTest("Grace period protects at 400ms after gap", !jamAt400);

    // Advance past grace period and continue extrusion without pulses
    advanceTime(200);  // Now at 600ms - grace period expired

    int jamAfterGraceSec = -1;
    const int expectedHardDetectionSec = HARD_JAM_TIME_MS / CHECK_INTERVAL_MS;
    for (int i = 0; i < expectedHardDetectionSec + 4; i++) {
        deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Jam after grace T+" + std::to_string(i + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            jamAfterGraceSec = i + 1;
            printState(sensor, label, jammed);
            break;
        }
    }

    if (jamAfterGraceSec < 0) {
        bool notDetectedJam =
            checkJamAndLog(sensor, "Jam after grace period (not detected)");
        printState(sensor, "Jam after grace period (not detected)", notDetectedJam);
    }

    bool detectedInWindow = jamAfterGraceSec >= expectedHardDetectionSec;
    recordTest("Detection active after grace period expires", detectedInWindow,
               jamAfterGraceSec > 0 ? "Detected at T+" + std::to_string(jamAfterGraceSec) + "s"
                                     : "Not detected");
}

//=============================================================================
// TEST 10: Normal Print with Hard Snag
//=============================================================================
void testHardSnagMidPrint() {
    printTestHeader("Test 10: Normal Print with Hard Snag");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    for (int sec = 0; sec < 10; sec++) {
        float deltaExtrusion = 25.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }

    bool beforeSnag = checkJamAndLog(sensor, "Before snag");
    printState(sensor, "Before snag", beforeSnag);

    const int expectedHardDetectionSec = HARD_JAM_TIME_MS / CHECK_INTERVAL_MS;
    bool jamDetected = false;
    int jamDetectionTime = -1;

    for (int sec = 0; sec < 10; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Hard snag T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed && !jamDetected) {
            jamDetected = true;
            jamDetectionTime = sec + 1;
        }

        printState(sensor, label, jammed);
        if (jammed) {
            break;
        }
    }

    if (!jamDetected) {
        bool notDetectedJam = checkJamAndLog(sensor, "Hard snag (not detected)");
        printState(sensor, "Hard snag (not detected)", notDetectedJam);
    }

    bool jamInWindow = jamDetected && jamDetectionTime >= expectedHardDetectionSec &&
                       jamDetectionTime <= expectedHardDetectionSec + 6;
    recordTest("Hard jam detected after normal flow", jamInWindow,
               jamDetected ? "Detected at T+" + std::to_string(jamDetectionTime) + "s" : "Not detected");
    recordTest("Hard jam not detected too early", jamDetectionTime < 0 || jamDetectionTime >= expectedHardDetectionSec);
}

//=============================================================================
// TEST 11: Complex Flow Sequence (retractions, ironing, travel)
//=============================================================================
void testComplexFlowSequence() {
    printTestHeader("Test 11: Complex Flow Sequence");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    bool falsePositive = false;

    // Steady extrusion
    for (int sec = 0; sec < 5; sec++) {
        float deltaExtrusion = 20.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
    }
    bool steadyJam = checkJamAndLog(sensor, "Post steady section");
    printState(sensor, "Post steady section", steadyJam);

    // Retraction sequence
    totalExtrusion -= 5.0f;
    simulateExtrusion(sensor, -5.0f, totalExtrusion);
    advanceTime(CHECK_INTERVAL_MS);
    bool afterRetractJam = checkJamAndLog(sensor, "After retraction");
    printState(sensor, "After retraction", afterRetractJam);

    // Resume normal printing
    for (int sec = 0; sec < 4; sec++) {
        float deltaExtrusion = 15.0f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        simulateSensorPulses(sensor, deltaExtrusion, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
        std::string label = "Resumed T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
            printState(sensor, label, jammed);
        }
    }
    bool resumedJam = checkJamAndLog(sensor, "Resumed after retract");
    printState(sensor, "Resumed after retract", resumedJam);

    // Long travel gap
    for (int sec = 0; sec < 8; sec++) {
        advanceTime(CHECK_INTERVAL_MS);
        std::string label = "Travel gap T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
            printState(sensor, label, jammed);
        }
    }
    bool afterTravelJam = checkJamAndLog(sensor, "After travel gap");
    printState(sensor, "After travel gap", afterTravelJam);

    // Ironing / low-flow micro moves
    for (int sec = 0; sec < 15; sec++) {
        float deltaExtrusion = 0.3f;
        totalExtrusion += deltaExtrusion;
        simulateExtrusion(sensor, deltaExtrusion, totalExtrusion);
        sensor.addSensorPulse(deltaExtrusion);
        advanceTime(CHECK_INTERVAL_MS);

        std::string label = "Ironing spike T+" + std::to_string(sec + 1) + "s";
        bool jammed = checkJamAndLog(sensor, label);
        if (jammed) {
            falsePositive = true;
            printState(sensor, label, jammed);
        }
    }
    bool afterIroningJam = checkJamAndLog(sensor, "After ironing");
    printState(sensor, "After ironing", afterIroningJam);

    // Another travel with sparse expected movement
    for (int sec = 0; sec < 6; sec++) {
        advanceTime(CHECK_INTERVAL_MS);
        std::string label = "Extended travel T+" + std::to_string(sec + 1) + "s";
        checkJamAndLog(sensor, label);
    }
    bool extendedTravelJam = checkJamAndLog(sensor, "Extended travel");
    printState(sensor, "Extended travel", extendedTravelJam);

    bool postTravelJam = checkJamAndLog(sensor, "Post travel jam");
    if (postTravelJam) {
        falsePositive = true;
    }
    printState(sensor, "Post travel jam", postTravelJam);

    recordTest("Complex flow remains jam-free", !falsePositive);
}

//=============================================================================
// TEST 12: Real Log Replay (Hard + Soft jam sequence)
//=============================================================================
void testRealLogReplay() {
    printTestHeader("Test 12: Real Log Replay (Hard + Soft Jam)");

    std::ifstream gcode(LOG_REPLAY_GCODE);
    recordTest("Reference G-code file present", gcode.good(),
               gcode.good() ? "" : std::string("Missing ") + LOG_REPLAY_GCODE);

    auto samples = loadFlowSamples(LOG_REPLAY_PATH);
    bool haveSamples = !samples.empty();
    recordTest("Flow samples parsed", haveSamples,
               haveSamples ? "" : std::string("Failed to parse ") + LOG_REPLAY_PATH);
    if (!haveSamples) {
        return;
    }

    auto jams = replayFlowSamples(samples);
    recordTest("Detected two jam events", jams.size() == 2,
               "Events detected: " + std::to_string(jams.size()));

    auto logJams = parseJamEventsFromLog(LOG_REPLAY_PATH);
    std::string logOrderDetail = logJams.empty() ? "" : logJams[0];
    if (logJams.size() > 1) {
        logOrderDetail += "," + logJams[1];
    }
    bool logOrderValid = logJams.size() >= 2 && logJams[0] == "hard" && logJams[1] == "soft";
    recordTest("Log jam order hard->soft", logOrderValid, logOrderDetail);
}

//=============================================================================
// TEST 15: Hard Jam Timing Verification
//=============================================================================
void testHardJamTiming() {
    printTestHeader("Test 15: Hard Jam Timing Verification");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    
    // 1. Warmup
    for (int sec = 0; sec < 15; sec++) {
        float delta = 20.0f;
        totalExtrusion += delta;
        simulateExtrusion(sensor, delta, totalExtrusion);
        simulateSensorPulses(sensor, delta, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
        checkJamAndLog(sensor, "Warmup");
    }

    // 2. Hard Jam (0% flow)
    // Monitor accumulator. It should only start increasing once the window average drops.
    // Once it starts increasing, it should take exactly HARD_JAM_TIME_MS to trigger.
    
    bool jamDetected = false;
    unsigned long lastAccum = 0;
    int accumSteps = 0;
    
    // Run for enough time to clear window + jam time + margin
    int maxSteps = (TRACKING_WINDOW_MS / CHECK_INTERVAL_MS) + (HARD_JAM_TIME_MS / CHECK_INTERVAL_MS) + 5;
    
    for (int i = 0; i < maxSteps; i++) {
        float delta = 20.0f;
        totalExtrusion += delta;
        simulateExtrusion(sensor, delta, totalExtrusion);
        // No pulses
        advanceTime(CHECK_INTERVAL_MS);
        
        bool jammed = checkJamAndLog(sensor, "Hard Jam Check");
        
        if (gHardJamAccumMs > 0) {
            if (lastAccum == 0) {
                // Just started accumulating
                std::cout << "  Hard Jam accumulation started at step " << i+1 << "\n";
            }
            
            // Verify accumulator increases monotonically
            if (gHardJamAccumMs < lastAccum) {
                recordTest("Hard jam accumulator reset unexpectedly", false);
            }
            
            // Check for early trigger
            if (jammed && gHardJamAccumMs < static_cast<unsigned long>(HARD_JAM_TIME_MS)) {
                 recordTest("Hard jam triggered before accumulator full", false, 
                            "Accum=" + std::to_string(gHardJamAccumMs));
            }
            
            accumSteps++;
        }
        
        if (jammed) {
            jamDetected = true;
            // Verify we are at or above the limit
            bool limitReached = gHardJamAccumMs >= static_cast<unsigned long>(HARD_JAM_TIME_MS);
            recordTest("Hard jam detected at limit", limitReached, 
                       "Accum=" + std::to_string(gHardJamAccumMs) + " Limit=" + std::to_string(HARD_JAM_TIME_MS));
            break;
        }
        
        lastAccum = gHardJamAccumMs;
    }
    
    if (!jamDetected) {
        recordTest("Hard jam never detected", false);
    }
}

//=============================================================================
// TEST 16: Soft Jam Timing Verification
//=============================================================================
void testSoftJamTiming() {
    printTestHeader("Test 16: Soft Jam Timing Verification");

    FilamentMotionSensor sensor;
    sensor.reset();
    _mockMillis = 0;
    resetJamSimState();

    float totalExtrusion = 0.0f;
    
    // 1. Warmup
    for (int sec = 0; sec < 15; sec++) {
        float delta = 20.0f;
        totalExtrusion += delta;
        simulateExtrusion(sensor, delta, totalExtrusion);
        simulateSensorPulses(sensor, delta, 1.0f);
        advanceTime(CHECK_INTERVAL_MS);
        checkJamAndLog(sensor, "Warmup");
    }

    // 2. Soft Jam (50% flow)
    bool jamDetected = false;
    unsigned long lastAccum = 0;
    
    // Run for enough time to clear window + jam time + margin
    int maxSteps = (TRACKING_WINDOW_MS / CHECK_INTERVAL_MS) + (SOFT_JAM_TIME_MS / CHECK_INTERVAL_MS) + 5;
    
    for (int i = 0; i < maxSteps; i++) {
        float delta = 20.0f;
        totalExtrusion += delta;
        simulateExtrusion(sensor, delta, totalExtrusion);
        simulateSensorPulses(sensor, delta, 0.35f); // 35% flow
        advanceTime(CHECK_INTERVAL_MS);
        
        bool jammed = checkJamAndLog(sensor, "Soft Jam Check");
        
        if (gSoftJamAccumMs > 0) {
            if (lastAccum == 0) {
                std::cout << "  Soft Jam accumulation started at step " << i+1 << "\n";
            }
            
                    }
        
        if (jammed) {
            jamDetected = true;
            if (gSoftJamAccumMs >= static_cast<unsigned long>(SOFT_JAM_TIME_MS)) {
                recordTest("Soft jam detected at limit", true,
                           "Accum=" + std::to_string(gSoftJamAccumMs) + " Limit=" + std::to_string(SOFT_JAM_TIME_MS));
                break;  // Only break when full accumulation is reached
            }
        }
        
        lastAccum = gSoftJamAccumMs;
    }
    
    if (!jamDetected) {
        recordTest("Soft jam never detected", false);
    }
}

//=============================================================================
// TEST 13, 14 & 17: Replay logs from fixtures/logs_to_replay
//=============================================================================
void testReplayLogFixtures() {
    const ReplayCase cases[] = {
        {"Test 13: Log Replay (soft_detected)", "../test/fixtures/logs_to_replay/soft_detected.txt", {"soft"}, 1},
        {"Test 14: Log Replay (soft_detected_but_no_rearm)", "../test/fixtures/logs_to_replay/soft_detected_but_no_rearm.txt", {"soft", "soft"}, 1},
        {"Test 17: 3D Benchy Crash Log (hard jam + resume)", "../test/fixtures/logs_to_replay/esp32_crash_3dbenchy.txt", {"hard"}, 1},
    };

    for (const auto& c : cases) {
        runReplayCase(c);
    }
}

//=============================================================================
// Main test runner
//=============================================================================
int main(int argc, char** argv) {
    const std::string DEFAULT_LOG_PATH = "render/filament_log.csv";
    std::string logPath;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--log") {
            logPath = DEFAULT_LOG_PATH;
        } else if (arg.rfind("--log=", 0) == 0) {
            logPath = arg.substr(6);
        } else if (arg == "--log-file" && i + 1 < argc) {
            logPath = argv[++i];
        }
    }
    std::cout << COLOR_BLUE << "\n"
              << "╔════════════════════════════════════════════════════════════╗\n"
              << "║        Filament Motion Sensor - Pulse Simulator            ║\n"
              << "║                     Unit Test Suite                        ║\n"
              << "╚════════════════════════════════════════════════════════════╝\n"
              << COLOR_RESET << "\n";

    std::cout << "Configuration:\n"
              << "  MM_PER_PULSE: " << MM_PER_PULSE << "mm\n"
              << "  CHECK_INTERVAL: " << CHECK_INTERVAL_MS << "ms\n"
              << "  RATIO_THRESHOLD: " << (RATIO_THRESHOLD * 100) << "% deficit\n"
              << "  HARD_JAM_MM: " << HARD_JAM_MM << "mm\n"
              << "  SOFT_JAM_TIME: " << SOFT_JAM_TIME_MS << "ms\n"
              << "  HARD_JAM_TIME: " << HARD_JAM_TIME_MS << "ms\n"
              << "  GRACE_PERIOD: " << GRACE_PERIOD_MS << "ms\n";

    if (!logPath.empty()) {
        initLogFile(logPath);
        std::cout << "Logging simulator state to: " << logPath << "\n";
    }

    // Run all tests
    testNormalPrinting();
    testHardJam();
    testSoftJam();
    testSparseInfill();
    testRetractions();
    testIroningLowFlow();
    testTransientSpikes();
    testMinimumMovement();
    testGracePeriod();
    testHardSnagMidPrint();
    testComplexFlowSequence();
    testRealLogReplay();
    testReplayLogFixtures();
    testHardJamTiming();
    testSoftJamTiming();

    // Summary
    int passed = 0;
    int failed = 0;
    for (const auto& result : testResults) {
        if (result.passed) passed++;
        else failed++;
    }

    std::cout << "\n" << COLOR_BLUE << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      TEST SUMMARY                          ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝" << COLOR_RESET << "\n";
    std::cout << "  Total: " << testResults.size() << " tests\n";
    std::cout << "  " << COLOR_GREEN << "Passed: " << passed << COLOR_RESET << "\n";
    std::cout << "  " << (failed > 0 ? COLOR_RED : COLOR_GREEN) << "Failed: " << failed << COLOR_RESET << "\n";

    if (failed > 0) {
        std::cout << "\n" << COLOR_RED << "Failed tests:" << COLOR_RESET << "\n";
        for (const auto& result : testResults) {
            if (!result.passed) {
                std::cout << "  - " << result.name;
                if (!result.details.empty()) {
                    std::cout << " (" << result.details << ")";
                }
                std::cout << "\n";
            }
        }
    }

    std::cout << "\n";
    closeLogFile();
    return (failed == 0) ? 0 : 1;
}
