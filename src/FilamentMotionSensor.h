#ifndef FILAMENT_MOTION_SENSOR_H
#define FILAMENT_MOTION_SENSOR_H

#include <Arduino.h>

// Sample for windowed tracking
struct FilamentSample
{
    unsigned long timestampMs;
    float         expectedMm;
    float         actualMm;
};

/**
 * Filament motion sensor with windowed tracking algorithm
 *
 * Uses sliding time window (Klipper-style) to handle calibration drift
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

    // Helper methods for windowed tracking
    void addSample(float expectedDeltaMm, float actualDeltaMm);
    void pruneOldSamples();
    void getWindowedDistances(float &expectedMm, float &actualMm);
};

#endif  // FILAMENT_MOTION_SENSOR_H
