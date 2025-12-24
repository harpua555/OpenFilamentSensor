// JamDetector - public interface for jam detection logic.
// Implementation is provided in JamDetector.cpp.

#ifndef JAM_DETECTOR_IFACE_H
#define JAM_DETECTOR_IFACE_H

#include <Arduino.h>

// Grace period states for jam detection
enum class GraceState : uint8_t
{
    IDLE = 0,      // Not printing, no detection
    START_GRACE,   // Print just started, time-based grace active
    RESUME_GRACE,  // Resumed after pause, waiting for movement
    ACTIVE,        // Actively detecting jams
    JAMMED         // Jam detected and latched
};

// Detection mode controls which jam checks are active
enum class DetectionMode : uint8_t
{
    BOTH      = 0,  // Both hard and soft jams are active
    HARD_ONLY = 1,  // Only hard jam detection is active
    SOFT_ONLY = 2   // Only soft jam detection is active
};

// Trip classification codes for debugging and diagnostics
enum class TripCode : uint8_t
{
    NONE              = 0,   // No trip condition
    HARD_ZERO_FLOW    = 1,   // Hard: sensor rate near zero
    HARD_RATE_RATIO   = 2,   // Hard: rate ratio below threshold
    SOFT_UNDER_EXT    = 3,   // Soft: sustained under-extrusion
    LOW_SPEED_ANOMALY = 4,   // Diagnostic: low expected rate edge case
};

// Jam detection result
struct JamState
{
    bool       jammed;               // True if either hard or soft jam detected
    bool       hardJamTriggered;     // True if hard jam (near-zero flow)
    bool       softJamTriggered;     // True if soft jam (sustained under-extrusion)
    float      hardJamPercent;       // Hard jam progress (0-100%)
    float      softJamPercent;       // Soft jam progress (0-100%)
    float      passRatio;            // Current pass ratio (actual/expected), rate-based
    float      deficit;              // Current deficit in mm (windowed)
    float      expectedRateMmPerSec; // Derived expected flow rate (mm/s)
    float      actualRateMmPerSec;   // Derived sensor flow rate (mm/s)
    GraceState graceState;           // Current grace period state
    bool       graceActive;          // True if any grace is active
    TripCode   tripCode;             // Current trip classification (for debugging)
};

// Configuration for jam detection (stored separately to save RAM)
struct JamConfig
{
    float        ratioThreshold;   // Soft jam threshold (e.g., 0.70 = 70% pass ratio)
    float        hardJamMm;        // Hard jam window threshold (mm)
    uint16_t     softJamTimeMs;    // Soft jam accumulation time (ms)
    uint16_t     hardJamTimeMs;    // Hard jam accumulation time (ms)
    uint16_t     graceTimeMs;      // Grace period after print start and resume (ms)
    DetectionMode detectionMode = DetectionMode::BOTH;
};

/**
 * Jam detector - handles all jam detection logic in one place.
 *
 * Public API is kept stable so callers (ElegooCC, etc.) only need
 * to consume JamState fields and JamConfig as before.
 */
class JamDetector
{
public:
    JamDetector();

    /**
     * Reset to initial state (call on print start).
     * @param currentTimeMs Current millis() value.
     */
    void reset(unsigned long currentTimeMs);

    /**
     * Notify detector that print resumed after pause.
     * @param currentTimeMs      Current millis() value.
     * @param currentPulseCount  Current pulse count baseline.
     * @param currentActualMm    Current actual filament mm baseline.
     */
    void onResume(unsigned long currentTimeMs,
                  unsigned long currentPulseCount,
                  float         currentActualMm);

    /**
     * Update jam detection state.
     * @param expectedDistance    Windowed expected distance (mm).
     * @param actualDistance      Windowed actual distance (mm).
     * @param movementPulseCount  Total pulse count.
     * @param isPrinting          True if printer is actively printing.
     * @param hasTelemetry        True if SDCP telemetry is fresh.
     * @param currentTimeMs       Current millis() value.
     * @param printStartTimeMs    Time when print started (millis).
     * @param config              Jam detection configuration.
     * @return JamState with current detection status.
     */
    JamState update(float              expectedDistance,
                    float              actualDistance,
                    unsigned long      movementPulseCount,
                    bool               isPrinting,
                    bool               hasTelemetry,
                    unsigned long      currentTimeMs,
                    unsigned long      printStartTimeMs,
                    const JamConfig&   config,
                    float              expectedRateMmPerSec,
                    float              actualRateMmPerSec);

    /**
     * Get current state.
     */
    JamState getState() const { return state; }

    /**
     * Check if pause was requested due to jam.
     */
    bool isPauseRequested() const { return jamPauseRequested; }

    /**
     * Notify that pause command was sent.
     */
    void setPauseRequested() { jamPauseRequested = true; }

    /**
     * Clear pause request (after resume completes).
     */
    void clearPauseRequest() { jamPauseRequested = false; }

private:
    // Current state
    JamState state;

    // Accumulation timers (uint16_t to save RAM - max ~65s)
    uint16_t hardJamAccumulatedMs;
    uint16_t softJamAccumulatedMs;

    // Last evaluation time (for delta calculations)
    unsigned long lastEvalMs;

    // Last pulse count (for diagnostics and resume grace)
    unsigned long lastPulseCount;

    // Resume grace tracking
    unsigned long resumeGracePulseBaseline;
    float         resumeGraceActualBaseline;
    unsigned long resumeGraceStartTimeMs;

    // Previous windowed distances (for rate derivation)
    float prevExpectedDistance;
    float prevActualDistance;

    // Flags (bit fields to save RAM)
    bool jamPauseRequested : 1;
    bool wasInGrace        : 1;  // Track grace transitions for logging

    // Smoothed deficit ratio for display (EWMA)
    float smoothedDeficitRatio;

    // Grace period helper
    bool evaluateGraceState(unsigned long currentTimeMs,
                            unsigned long printStartTimeMs,
                            float         expectedDistance,
                            unsigned long movementPulseCount,
                            const JamConfig& config);

    // Jam condition evaluators (rate-based)
    bool evaluateHardJam(float          expectedDistance,
                         float          passRatio,
                         float          expectedRate,
                         float          actualRate,
                         unsigned long  elapsedMs,
                         const JamConfig& config);

    bool evaluateSoftJam(float          expectedDistance,
                         float          deficit,
                         float          passRatio,
                         float          expectedRate,
                         float          actualRate,
                         unsigned long  elapsedMs,
                         const JamConfig& config);
};

#endif  // JAM_DETECTOR_IFACE_H
