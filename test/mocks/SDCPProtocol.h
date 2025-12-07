#ifndef MOCK_SDCP_PROTOCOL_H
#define MOCK_SDCP_PROTOCOL_H

/**
 * Mock SDCPProtocol for unit tests
 * Provides stub implementation without Arduino/ESP dependencies
 */

#include "json_mocks.h"

// SDCP protocol constants for telemetry keys
namespace SDCPKeys {
    static const char* TOTAL_EXTRUSION_HEX   = "54 6F 74 61 6C 45 78 74 72 75 73 69 6F 6E 00";
    static const char* CURRENT_EXTRUSION_HEX = "43 75 72 72 65 6E 74 45 78 74 72 75 73 69 6F 6E 00";
}

// SDCP protocol timing constants
namespace SDCPTiming {
    constexpr unsigned long ACK_TIMEOUT_MS = 5000;
    constexpr unsigned int  EXPECTED_FILAMENT_SAMPLE_MS = 1000;
    constexpr unsigned int  EXPECTED_FILAMENT_STALE_MS = 1000;
    constexpr unsigned int  SDCP_LOSS_TIMEOUT_MS = 10000;
    constexpr unsigned int  PAUSE_REARM_DELAY_MS = 3000;
}

// SDCP protocol defaults
namespace SDCPDefaults {
    constexpr float FILAMENT_DEFICIT_THRESHOLD_MM = 8.4f;
}

/**
 * Mock SDCP Protocol helper class
 */
class SDCPProtocol {
public:
    static bool buildCommandMessage(
        JsonDocument& doc,
        int command,
        const String& requestId,
        const String& mainboardId,
        unsigned long timestamp,
        int printStatus,
        uint8_t machineStatusMask
    ) {
        // Mock implementation - just return true
        return true;
    }

    static bool tryReadExtrusionValue(
        JsonObject& printInfo,
        const char* key,
        const char* hexKey,
        float& output
    ) {
        // Mock implementation - check if key exists
        if (printInfo.containsKey(key)) {
            output = printInfo[key].as<float>();
            return true;
        }
        return false;
    }
};

#endif  // MOCK_SDCP_PROTOCOL_H
