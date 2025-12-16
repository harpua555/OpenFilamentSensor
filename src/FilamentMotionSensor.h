#ifndef FILAMENT_MOTION_SENSOR_H
#define FILAMENT_MOTION_SENSOR_H

#include <Arduino.h>

/**
 * FilamentMotionSensor - Decoupled Dual-Buffer Implementation
 * 
 * Solves "Pipeline Latency" by storing Expected (Planner) and Actual (Executor)
 * data in independent time-series buffers. This allows the sliding window
 * to absorb the variable delay between planning and execution without
 * artifacting/coupling errors.
 */
class FilamentMotionSensor
{
   public:
    FilamentMotionSensor();

    void reset();

    // Telemetry Update
    void updateExpectedPosition(float totalExtrusionMm);

    // Pulse Update
    void addSensorPulse(float mmPerPulse);

    // Analysis
    float getDeficit();
    float getExpectedDistance();
    float getSensorDistance();
    void getWindowedRates(float &expectedRate, float &actualRate);

    // State Queries
    bool isInitialized() const;
    bool isWithinGracePeriod(unsigned long gracePeriodMs) const;
    float getFlowRatio();

   private:
    // Tracking Constants
    static const int           BUCKET_SIZE_MS = 250;
    static const int           WINDOW_SIZE_MS = 5000;
    static const int           BUCKET_COUNT   = WINDOW_SIZE_MS / BUCKET_SIZE_MS; // 20

    // Independent Circular Buffers
    float         expectedBuckets[BUCKET_COUNT];
    float         actualBuckets[BUCKET_COUNT];
    unsigned long bucketTimestamps[BUCKET_COUNT]; // For stale data clearing

    // State
    bool          initialized;
    bool          firstPulseReceived;
    unsigned long lastExpectedUpdateMs;

    // Pulse Tracking (Global/Monotonic for Dropout Recovery)
    unsigned long lastSensorPulseMs;
    float         totalSensorMm;          // Monotonic total of all pulses since reset
    float         sensorMmAtLastUpdate;   // Snapshot of totalSensorMm at last telemetry update

    // Telemetry Tracking
    float         lastTotalExtrusionMm;   // Last known absolute extrusion from SDCP
    float         preInitActualMm;        // Buffer pulses before init
    unsigned long preInitPulseCount;

    // Helpers
    int           getCurrentBucketIndex();
    void          sumWindow(float &outExpected, float &outActual);
    void          clearStaleBuckets(unsigned long currentTime);
};

#endif  // FILAMENT_MOTION_SENSOR_H