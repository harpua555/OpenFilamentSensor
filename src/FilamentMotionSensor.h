#ifndef FILAMENT_MOTION_SENSOR_H
#define FILAMENT_MOTION_SENSOR_H

#include <Arduino.h>

// Sample for windowed tracking
struct FilamentSample
{
    unsigned long timestampMs;
    unsigned long durationMs;
    float         expectedMm;
    float         actualMm;
};

/**
 * Initialize a FilamentMotionSensor instance and its internal tracking state.
 */

/**
 * Reset all tracking state to the initial (pre-print) condition.
 */

/**
 * Update the expected extrusion position from printer telemetry.
 * @param totalExtrusionMm Current total extrusion measured by the printer in millimeters.
 */

/**
 * Record a sensor pulse representing filament movement.
 * @param mmPerPulse Distance in millimeters that a single sensor pulse represents.
 */

/**
 * Report the positive difference where expected extrusion exceeds actual sensor-measured extrusion.
 * @return Deficit in millimeters (zero if actual ≥ expected).
 */

/**
 * Report the accumulated expected extrusion distance within the current tracking window or since reset.
 * @return Expected distance in millimeters.
 */

/**
 * Report the accumulated actual sensor-measured distance within the current tracking window or since reset.
 * @return Actual sensor distance in millimeters.
 */

/**
 * Fill the provided references with the average expected and actual extrusion rates over the tracking window.
 * @param expectedRate Output reference updated with the average expected rate in millimeters per second.
 * @param actualRate Output reference updated with the average actual (sensor) rate in millimeters per second.
 */

/**
 * Indicate whether the sensor has received at least one expected position update from telemetry.
 * @return `true` if an expected position update has been received, `false` otherwise.
 */

/**
 * Indicate whether tracking is still within a grace period following initialization, retraction, or a telemetry gap.
 * @param gracePeriodMs Grace period duration in milliseconds.
 * @return `true` if the time since the last relevant event is less than `gracePeriodMs`, `false` otherwise.
 */

/**
 * Compute the ratio of actual (sensor) extrusion to expected extrusion for the current window.
 * @return Ratio `actual / expected` (0.0 or greater), or `0` if tracking is not initialized or expected is zero.
 */
class FilamentMotionSensor
{
   public:
    FilamentMotionSensor();

    /**
     * Reset all tracking state
     * Call when: print starts, print resumes after pause, or print ends
     */
    void reset();

    /**
     * Update the expected extrusion position from printer telemetry
     * @param totalExtrusionMm Current total extrusion value from SDCP
     */
    void updateExpectedPosition(float totalExtrusionMm);

    /**
     * Record a sensor pulse (filament actually moved)
     * @param mmPerPulse Distance in mm that one pulse represents (e.g., 2.88mm)
     */
    void addSensorPulse(float mmPerPulse);

    /**
     * Get current deficit (how much expected exceeds actual)
     * @return Deficit in mm (0 or positive value)
     */
    float getDeficit();

    /**
     * Get the expected extrusion distance since last reset/window
     * @return Expected distance in mm
     */
    float getExpectedDistance();

    /**
     * Get the actual sensor distance since last reset/window
     * @return Actual distance in mm
     */
    float getSensorDistance();

    /**
     * Get the average expected and actual rates within the tracking window.
     */
    void getWindowedRates(float &expectedRate, float &actualRate);

    /**
     * Check if tracking has been initialized with first telemetry
     * @return true if we've received at least one expected position update
     */
    bool isInitialized() const;

    /**
     * Returns true if we're still within the configured grace period after
     * initialization, retraction, or telemetry gap.
     */
    bool isWithinGracePeriod(unsigned long gracePeriodMs) const;

    /**
     * Get ratio of actual to expected (for calibration/debugging)
     * @return Ratio (0.0 to 1.0+), or 0 if not initialized
     */
    float getFlowRatio();

   private:
    // Common state
    bool                 initialized;
    bool                 firstPulseReceived;  // Track if first pulse detected (skip pre-prime extrusion)
    unsigned long        lastExpectedUpdateMs;

    // Windowed tracking state
    static const int MAX_SAMPLES = 20;  // Store up to 20 samples (covers 5sec at 250ms poll rate)
    FilamentSample   samples[MAX_SAMPLES];
    int              sampleCount;
    int              nextSampleIndex;
    unsigned long    windowSizeMs;

    // Sensor pulse tracking
    unsigned long lastSensorPulseMs;  // Track when last pulse was detected
    float         lastTotalExtrusionMm;  // Last known extrusion position (reset with instance)

    // Helper methods for windowed tracking
    void addSample(float expectedDeltaMm, float actualDeltaMm);
    void pruneOldSamples();
    void getWindowedDistances(float &expectedMm, float &actualMm);
};

#endif  // FILAMENT_MOTION_SENSOR_H