#ifndef ELEGOOCC_H
#define ELEGOOCC_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

#include "FilamentMotionSensor.h"
#include "JamDetector.h"
//#include "JamDetector_iface.h"
#include "UUID.h"

#define CARBON_CENTAURI_PORT 3030

// Pin definitions - can be overridden via build flags
#ifndef FILAMENT_RUNOUT_PIN
#define FILAMENT_RUNOUT_PIN 12
#endif

#ifndef MOVEMENT_SENSOR_PIN
#define MOVEMENT_SENSOR_PIN 13
#endif

// Status codes
typedef enum
{
    SDCP_PRINT_STATUS_IDLE          = 0,   // Idle
    SDCP_PRINT_STATUS_HOMING        = 1,   // Homing
    SDCP_PRINT_STATUS_DROPPING      = 2,   // Descending
    SDCP_PRINT_STATUS_EXPOSURING    = 3,   // Exposing
    SDCP_PRINT_STATUS_LIFTING       = 4,   // Lifting
    SDCP_PRINT_STATUS_PAUSING       = 5,   // Executing Pause Action
    SDCP_PRINT_STATUS_PAUSED        = 6,   // Suspended
    SDCP_PRINT_STATUS_STOPPING      = 7,   // Executing Stop Action
    SDCP_PRINT_STATUS_STOPED        = 8,   // Stopped
    SDCP_PRINT_STATUS_COMPLETE      = 9,   // Print Completed
    SDCP_PRINT_STATUS_FILE_CHECKING = 10,  // File Checking in Progress
    SDCP_PRINT_STATUS_PRINTING      = 13,  // Printing
    SDCP_PRINT_STATUS_UNKNOWN_15    = 15,  // unknown
    SDCP_PRINT_STATUS_HEATING       = 16,  // Heating
    SDCP_PRINT_STATUS_UNKNOWN_18    = 18,  // Unknown
    SDCP_PRINT_STATUS_UNKNOWN_19    = 19,  // Unknown
    SDCP_PRINT_STATUS_BED_LEVELING  = 20,  // Bed Leveling
    SDCP_PRINT_STATUS_UNKNOWN_21    = 21,  // Unknown
} sdcp_print_status_t;

// Extended Status Error Codes
typedef enum
{
    SDCP_PRINT_ERROR_NONE               = 0,  // Normal
    SDCP_PRINT_ERROR_CHECK              = 1,  // File MD5 Check Failed
    SDCP_PRINT_ERROR_FILEIO             = 2,  // File Read Failed
    SDCP_PRINT_ERROR_INVLAID_RESOLUTION = 3,  // Resolution Mismatch
    SDCP_PRINT_ERROR_UNKNOWN_FORMAT     = 4,  // Format Mismatch
    SDCP_PRINT_ERROR_UNKNOWN_MODEL      = 5   // Machine Model Mismatch
} sdcp_print_error_t;

typedef enum
{
    SDCP_MACHINE_STATUS_IDLE              = 0,  // Idle
    SDCP_MACHINE_STATUS_PRINTING          = 1,  // Executing print task
    SDCP_MACHINE_STATUS_FILE_TRANSFERRING = 2,  // File transfer in progress
    SDCP_MACHINE_STATUS_EXPOSURE_TESTING  = 3,  // Exposure test in progress
    SDCP_MACHINE_STATUS_DEVICES_TESTING   = 4,  // Device self-check in progress
} sdcp_machine_status_t;

typedef enum
{
    SDCP_COMMAND_STATUS                = 0,
    SDCP_COMMAND_ATTRIBUTES            = 1,
    SDCP_COMMAND_START_PRINT           = 128,
    SDCP_COMMAND_PAUSE_PRINT           = 129,
    SDCP_COMMAND_STOP_PRINT            = 130,
    SDCP_COMMAND_CONTINUE_PRINT        = 131,
    SDCP_COMMAND_STOP_FEEDING_MATERIAL = 132,
} sdcp_command_t;

// Struct to hold current printer information
typedef struct
{
    String              mainboardID;
    sdcp_print_status_t printStatus;
    bool                filamentStopped;
    bool                filamentRunout;
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
} printer_info_t;

class ElegooCC
{
   private:
    struct TransportState
    {
        WebSocketsClient webSocket;
        String           ipAddress;
        unsigned long    lastPing            = 0;
        bool             waitingForAck       = false;
        int              pendingAckCommand   = -1;
        String           pendingAckRequestId;
        unsigned long    ackWaitStartTime    = 0;
        unsigned long    lastStatusRequestMs = 0;
    };

    TransportState        transport;
    UUID                  uuid;
    StaticJsonDocument<1200> messageDoc;
    // Variables to track movement sensor state
    int           lastMovementValue;  // Initialize to invalid value
    unsigned long lastChangeTime;

    // machine/status info
    String              mainboardID;
    sdcp_print_status_t printStatus;
    uint8_t             machineStatusMask;  // Bitmask for active statuses
    int                 currentLayer;
    float               currentZ;
    int                 totalLayer;
    int                 progress;
    int                 currentTicks;
    int                 totalTicks;
    int                 PrintSpeedPct;
    bool                filamentStopped;
    bool                filamentRunout;
    float               expectedFilamentMM;
    float               actualFilamentMM;
    float               lastExpectedDeltaMM;
    bool                expectedTelemetryAvailable;
    unsigned long       lastSuccessfulTelemetryMs;
    unsigned long       lastTelemetryReceiveMs;
    unsigned long       lastStatusReceiveMs;
    bool                telemetryAvailableLastStatus;

    unsigned long startedAt;
    FilamentMotionSensor motionSensor;  // Windowed sensor tracking (Klipper-style)
    JamDetector         jamDetector;    // Consolidated jam detection logic
    unsigned long       movementPulseCount;
    unsigned long       lastFlowLogMs;
    unsigned long       lastSummaryLogMs;
    unsigned long       lastPinDebugLogMs;

    // Track last logged values to avoid duplicate verbose logs
    float         lastLoggedExpected;
    float         lastLoggedActual;
    float         lastLoggedDeficit;
    int           lastLoggedPrintStatus;
    int           lastLoggedLayer;
    int           lastLoggedTotalLayer;

    // Print start candidate tracking (to distinguish true job start
    // from transient/attached PRINTING states)
    bool          printCandidateActive;
    bool          printCandidateSawHoming;
    bool          printCandidateSawLeveling;
    bool          printCandidateConditionsMet;
    unsigned long printCandidateIdleSinceMs;

    // Tracking state (for UI freeze on pause)
    bool          trackingFrozen;
    bool          hasBeenPaused;
    
    // Jam detector state caching (for throttled updates)
    JamState      cachedJamState;
    unsigned long lastJamDetectorUpdateMs;

    // Command tracking
    unsigned long lastPauseRequestMs;
    unsigned long lastPrintEndMs;
    static constexpr unsigned long STATUS_IDLE_INTERVAL_MS          = 10000;
    static constexpr unsigned long STATUS_ACTIVE_INTERVAL_MS        = 250;
    static constexpr unsigned long STATUS_POST_PRINT_COOLDOWN_MS    = 20000;
    static constexpr unsigned long JAM_DEBUG_INTERVAL_MS            = 1000;
    static constexpr unsigned long JAM_DETECTOR_UPDATE_INTERVAL_MS  = 250;  // 4Hz

    ElegooCC();

    // Delete copy constructor and assignment operator
    ElegooCC(const ElegooCC &)            = delete;
    ElegooCC &operator=(const ElegooCC &) = delete;

    void webSocketEvent(WStype_t type, uint8_t *payload, size_t length);
    void connect();
    void updateTransport(unsigned long currentTime);
    void handleCommandResponse(JsonDocument &doc);
    void handleStatus(JsonDocument &doc);
    void sendCommand(int command, bool waitForAck = false);

    // Helpers for print start detection
    void clearPrintStartCandidate();
    void updatePrintStartCandidate(sdcp_print_status_t previousStatus,
                                   sdcp_print_status_t newStatus);
    bool isPrintStartCandidateSatisfied() const;
    void updatePrintStartCandidateTimeout(unsigned long currentTime);
   public:
    void pausePrint();
    void continuePrint();

    void resetFilamentTracking(bool resetGrace = true);
    bool processFilamentTelemetry(JsonObject& printInfo, unsigned long currentTime);
    bool tryReadExtrusionValue(JsonObject& printInfo, const char* key, const char* hexKey,
                               float& output);

    // Helper methods for machine status bitmask
    bool hasMachineStatus(sdcp_machine_status_t status);
    void setMachineStatuses(const int *statusArray, int arraySize);
    bool isPrinting();
    bool isPrintJobActive();  // Returns true for any non-idle state (for polling decisions)
    bool shouldPausePrint(unsigned long currentTime);
    void checkFilamentMovement(unsigned long currentTime);
    bool shouldApplyPulseReduction(float reductionPercent);  // Pulse reduction helper function
    void maybeRequestStatus(unsigned long currentTime);
    void checkFilamentRunout(unsigned long currentTime);

   public:
    // Singleton access method
    static ElegooCC &getInstance();

    void setup();
    void loop();

    // Get current printer information
    printer_info_t getCurrentInformation();

    bool discoverPrinterIP(String &outIp, unsigned long timeoutMs = 3000);
};

// Convenience macro for easier access
#define elegooCC ElegooCC::getInstance()

#endif  // ELEGOOCC_H
