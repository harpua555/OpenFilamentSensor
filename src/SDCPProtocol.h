#ifndef SDCP_PROTOCOL_H
#define SDCP_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>

// SDCP protocol constants for telemetry keys
namespace SDCPKeys {
    // Hex-encoded keys used by some printer firmware variants
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
 * SDCP Protocol helper class
 *
 * Handles SDCP message building and parsing without maintaining state.
 * All functions are static - this is a utility class only.
 */
class SDCPProtocol {
public:
    /**
     * Build an SDCP command JSON payload
     *
     * @param doc JSON document to populate (caller allocates)
     * @param command SDCP command code
     * @param requestId Unique request identifier (UUID without dashes)
     * @param mainboardId Printer mainboard ID (empty string if unknown)
     * @param timestamp Current epoch time
     * @param printStatus Current print status
     * @param machineStatusMask Bitmask of active machine statuses
     * @return true if successful, false if doc too small
     */
    static bool buildCommandMessage(
        JsonDocument& doc,
        int command,
        const String& requestId,
        const String& mainboardId,
        unsigned long timestamp,
        int printStatus,
        uint8_t machineStatusMask
    );

    /**
     * Try to read an extrusion value from PrintInfo, checking both
     * normal and hex-encoded key variants
     *
     * @param printInfo JSON object containing print telemetry
     * @param key Normal key name (e.g., "TotalExtrusion")
     * @param hexKey Hex-encoded key variant (or nullptr to skip)
     * @param output Output parameter for the value
     * @return true if value was found and read
     */
    static bool tryReadExtrusionValue(
        JsonObject& printInfo,
        const char* key,
        const char* hexKey,
        float& output
    );
};

#endif  // SDCP_PROTOCOL_H
