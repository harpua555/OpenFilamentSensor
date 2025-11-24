#ifndef FILAMENT_MOTION_SENSOR_H
#define FILAMENT_MOTION_SENSOR_H

#include <Arduino.h>

// Tracking algorithm modes
enum FilamentTrackingMode
{
    TRACKING_MODE_CUMULATIVE = 0,  // Simple cumulative (legacy, has drift issues)
    TRACKING_MODE_WINDOWED = 1,    // Sliding time window (Klipper-style)
    TRACKING_MODE_EWMA = 2         // Exponentially weighted moving average
};

// Sample for windowed tracking
struct FilamentSample
{
    unsigned long timestampMs;
    float         expectedMm;
    float         actualMm;
};

/**
 * Filament motion sensor with multiple tracking algorithms
 *
 * Supports three modes:
 * 1. Cumulative: Simple tracking from baseline (has calibration drift)
 * 2. Windowed: Sliding time window like Klipper (handles drift)
 * 3. EWMA: Exponentially weighted moving average (simpler, handles drift)
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
     * Set tracking mode
     * @param mode Algorithm to use for tracking
     * @param windowMs Window size for windowed mode (default 5000ms)
     * @param ewmaAlpha Smoothing factor for EWMA mode (default 0.3)
     */
    void setTrackingMode(FilamentTrackingMode mode, unsigned long windowMs = 5000,
                        float ewmaAlpha = 0.3f);

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
     * Check if jam is detected using ratio-based detection
     * @param ratioThreshold Soft jam: deficit ratio threshold (0.7 = 70% deficit, < 30% passing)
     * @param hardJamThresholdMm Hard jam: mm expected with zero movement to trigger
     * @param softJamTimeMs Soft jam: how long ratio must stay bad (ms, e.g., 3000 = 3 sec)
     * @param hardJamTimeMs Hard jam: how long zero movement required (ms, e.g., 2000 = 2 sec)
     * @param checkIntervalMs How often isJammed() is called (ms, e.g., 1000 = every second)
     * @param gracePeriodMs Grace period in ms after expected position update before checking
     * @return true if jam detected (either hard or soft)
     */
    bool isJammed(float ratioThreshold, float hardJamThresholdMm,
                  int softJamTimeMs, int hardJamTimeMs, int checkIntervalMs,
                  unsigned long gracePeriodMs = 0) const;

    /**
     * Get current deficit (how much expected exceeds actual)
     * @return Deficit in mm (0 or positive value)
     */
    float getDeficit() const;

    /**
     * Get the expected extrusion distance since last reset/window
     * @return Expected distance in mm
     */
    float getExpectedDistance() const;

    /**
     * Get the actual sensor distance since last reset/window
     * @return Actual distance in mm
     */
    float getSensorDistance() const;

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
    float getFlowRatio() const;
    float getHardJamProgressPercent() const;
    float getSoftJamProgressPercent() const;

   private:
    // Common state
    bool                 initialized;
    bool                 firstPulseReceived;  // Track if first pulse detected (skip pre-prime extrusion)
    FilamentTrackingMode trackingMode;
    unsigned long        lastExpectedUpdateMs;

    // Cumulative mode state
    float baselinePositionMm;
    float expectedPositionMm;
    float sensorDistanceMm;

    // Windowed mode state
    static const int MAX_SAMPLES = 20;  // Store up to 20 samples (covers 5sec at 250ms poll rate)
    FilamentSample   samples[MAX_SAMPLES];
    int              sampleCount;
    int              nextSampleIndex;
    unsigned long    windowSizeMs;

    // EWMA mode state
    float ewmaExpectedMm;
    float ewmaActualMm;
    float ewmaAlpha;  // Smoothing factor (0.0-1.0, higher = more weight on recent)
    float ewmaLastExpectedMm;
    float ewmaLastActualMm;

    // Jam detection trackers
    mutable float  lastWindowDeficitMm;     // Last deficit used to compute growth rate
    mutable unsigned long lastDeficitTimestampMs;
    mutable unsigned long lastJamEvaluationMs;
    mutable unsigned long hardJamAccumulatedMs;
    mutable unsigned int  hardJamConsecutiveChecks;
    mutable unsigned int  hardJamRequiredChecks;
    mutable unsigned long softJamAccumulatedMs;
    mutable int           lastSoftJamTimeMs;
    mutable bool          softJamActive;
    mutable float         softJamDeficitAccumMm;
    mutable float         hardJamAccumExpectedMm;
    mutable float         hardJamAccumActualMm;
    mutable float         lastCumulativeExpectedPositionMm;
    mutable float         lastCumulativeSensorDistanceMm;
    unsigned long         lastSensorPulseMs;  // Track when last pulse was detected
    // Helper methods for windowed tracking
    void addSample(float expectedDeltaMm, float actualDeltaMm);
    void pruneOldSamples();
    void getWindowedDistances(float &expectedMm, float &actualMm) const;
};

#endif  // FILAMENT_MOTION_SENSOR_H
