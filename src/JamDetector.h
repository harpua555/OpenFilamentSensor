#ifndef JAM_DETECTOR_H
#define JAM_DETECTOR_H

#include <Arduino.h>

// Grace period states for jam detection
enum class GraceState : uint8_t {
    IDLE = 0,         // Not printing, no detection
    START_GRACE,      // Print just started, time-based grace active
    RESUME_GRACE,     // Resumed after pause, waiting for movement
    ACTIVE,           // Actively detecting jams
    JAMMED            // Jam detected and latched
};

// Jam detection result
struct JamState {
    bool jammed;              // True if either hard or soft jam detected
    bool hardJamTriggered;    // True if hard jam (near-zero flow)
    bool softJamTriggered;    // True if soft jam (sustained under-extrusion)
    float hardJamPercent;     // Hard jam progress (0-100%)
    float softJamPercent;     // Soft jam progress (0-100%)
    float passRatio;          // Current pass ratio (actual/expected)
    float deficit;            // Current deficit in mm
    GraceState graceState;    // Current grace period state
    bool graceActive;         // True if any grace is active
};

// Configuration for jam detection (stored separately to save RAM)
struct JamConfig {
    float ratioThreshold;     // Soft jam threshold (e.g., 0.70 = 70% pass ratio)
    float hardJamMm;          // Hard jam window threshold (mm)
    uint16_t softJamTimeMs;   // Soft jam accumulation time (ms)
    uint16_t hardJamTimeMs;   // Hard jam accumulation time (ms)
    uint16_t graceTimeMs;     // Grace period after print start (ms)
    uint16_t startTimeoutMs;  // Total timeout before detection starts (ms)
};

/**
 * Jam detector - handles all jam detection logic in one place
 *
 * Memory-optimized with:
 * - uint16_t for time values (max 65.5 seconds, sufficient for jam detection)
 * - Bit fields for boolean flags
 * - Consolidated grace period logic
 */
class JamDetector {
public:
    JamDetector();

    /**
     * Reset to initial state (call on print start)
     * @param currentTimeMs Current millis() value
     */
    void reset(unsigned long currentTimeMs);

    /**
     * Notify detector that print resumed after pause
     * @param currentTimeMs Current millis() value
     * @param currentPulseCount Current pulse count baseline
     * @param currentActualMm Current actual filament mm baseline
     */
    void onResume(unsigned long currentTimeMs, unsigned long currentPulseCount, float currentActualMm);

    /**
     * Update jam detection state
     * @param expectedDistance Windowed expected distance (mm)
     * @param actualDistance Windowed actual distance (mm)
     * @param movementPulseCount Total pulse count
     * @param isPrinting True if printer is actively printing
     * @param hasTelemetry True if SDCP telemetry is fresh
     * @param currentTimeMs Current millis() value
     * @param printStartTimeMs Time when print started (millis)
     * @param config Jam detection configuration
     * @return JamState with current detection status
     */
    JamState update(float expectedDistance, float actualDistance,
                    unsigned long movementPulseCount, bool isPrinting,
                    bool hasTelemetry, unsigned long currentTimeMs,
                    unsigned long printStartTimeMs, const JamConfig& config);

    /**
     * Get current state
     */
    JamState getState() const { return state; }

    /**
     * Check if pause was requested due to jam
     */
    bool isPauseRequested() const { return jamPauseRequested; }

    /**
     * Notify that pause command was sent
     */
    void setPauseRequested() { jamPauseRequested = true; }

    /**
     * Clear pause request (after resume completes)
     */
    void clearPauseRequest() { jamPauseRequested = false; }

private:
    // Current state
    JamState state;

    // Accumulation timers (uint16_t to save RAM - max 65535ms / 65 seconds)
    uint16_t hardJamAccumulatedMs;
    uint16_t softJamAccumulatedMs;

    // Last evaluation time (for delta calculations)
    unsigned long lastEvalMs;

    // Last pulse count (for hard jam new-pulse detection)
    unsigned long lastPulseCount;

    // Resume grace tracking
    unsigned long resumeGracePulseBaseline;
    float resumeGraceActualBaseline;
    unsigned long resumeGraceStartTimeMs;

    // Flags (bit fields to save RAM)
    bool jamPauseRequested : 1;
    bool wasInGrace : 1;  // Track grace transitions for logging

    // Smoothed deficit ratio for display (EWMA)
    float smoothedDeficitRatio;

    // Grace period helper methods
    bool evaluateGraceState(unsigned long currentTimeMs, unsigned long printStartTimeMs,
                           float expectedDistance, unsigned long movementPulseCount,
                           const JamConfig& config);

    // Jam condition evaluators
    bool evaluateHardJam(float expectedDistance, float passRatio,
                        bool newPulseSinceLastEval, unsigned long elapsedMs,
                        const JamConfig& config);

    bool evaluateSoftJam(float expectedDistance, float deficit, float passRatio,
                        unsigned long elapsedMs, const JamConfig& config);

    // Constants
    static constexpr float HARD_PASS_THRESHOLD = 0.10f;       // <10% flow = hard jam
    static constexpr float HARD_RECOVERY_RATIO = 0.35f;       // >35% flow = hard jam recovery
    static constexpr float MIN_HARD_WINDOW_MM = 1.0f;         // Min expected for hard jam
    static constexpr float MIN_SOFT_WINDOW_MM = 1.0f;         // Min expected for soft jam
    static constexpr float MIN_SOFT_DEFICIT_MM = 0.25f;       // Min deficit for soft jam
    static constexpr float RESUME_GRACE_MIN_MOVEMENT_MM = 5.0f;  // Min movement to clear resume grace
    static constexpr float RESUME_GRACE_15MM_THRESHOLD = 15.0f;  // 15mm expected to clear resume grace
    static constexpr uint16_t RESUME_GRACE_6SEC_TIMEOUT = 6000; // 6 seconds to clear resume grace
    static constexpr float RATIO_SMOOTHING_ALPHA = 0.1f;      // EWMA smoothing factor
};

#endif  // JAM_DETECTOR_H
