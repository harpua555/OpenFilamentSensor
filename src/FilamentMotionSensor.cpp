#include "FilamentMotionSensor.h"

static const unsigned long INVALID_SAMPLE_TIMESTAMP = ~0UL;

/**
 * @brief Initializes a FilamentMotionSensor with a 5-second tracking window.
 *
 * Sets the window size used for motion sampling to 5000 ms and resets internal
 * tracking state and timers.
 */
FilamentMotionSensor::FilamentMotionSensor()
{
    windowSizeMs = 5000;  // 5 second window
    reset();
}

/**
 * @brief Reset the motion sensor to its initial, uninitialized state.
 *
 * Clears all windowed samples, resets internal flags and counters, and
 * reinitializes timing and extrusion baseline values so the sensor starts
 * fresh as if just constructed.
 *
 * After calling this method the sensor will treat the next telemetry update
 * as the baseline and ignore any prior pulses until new data arrives.
 */
void FilamentMotionSensor::reset()
{
    initialized           = false;
    firstPulseReceived    = false;  // Reset pulse tracking
    lastExpectedUpdateMs  = millis();
    lastTotalExtrusionMm  = 0.0f;  // Reset extrusion baseline

    // Reset windowed state
    sampleCount           = 0;
    nextSampleIndex       = 0;
    for (int i = 0; i < MAX_SAMPLES; i++)
    {
        samples[i].timestampMs = 0;
        samples[i].expectedMm  = 0.0f;
        samples[i].actualMm    = 0.0f;
        samples[i].durationMs  = 0;
    }

    lastSensorPulseMs = millis();  // Initialize to current time
}

/**
 * @brief Update the expected extrusion baseline and record expected movement into the tracking window.
 *
 * Establishes the initial baseline the first time telemetry is received. If the provided
 * cumulative extrusion decreased since the last update (a retraction), clears the current
 * windowed samples while preserving the grace-period timer. If extrusion increased and a
 * sensor pulse has already been observed, appends a window sample for the expected delta
 * (with zero actual movement; sensor pulses will later populate actual movement).
 *
 * @param totalExtrusionMm Cumulative total extrusion in millimeters reported by the firmware.
 */
void FilamentMotionSensor::updateExpectedPosition(float totalExtrusionMm)
{
    unsigned long currentTime = millis();

    if (!initialized)
    {
        // First telemetry received - establish baseline
        initialized           = true;
        lastExpectedUpdateMs  = currentTime;
        lastTotalExtrusionMm  = totalExtrusionMm;
        return;
    }

    // Handle retractions: reset windowed tracking
    if (totalExtrusionMm < lastTotalExtrusionMm)
    {
        // Retraction detected - clear window
        // NOTE: Do NOT reset lastExpectedUpdateMs here! Retractions during normal
        // printing should not restart the grace period timer, otherwise jam detection
        // never activates (grace period keeps resetting every few seconds).
        // Grace period should only start on: (1) print start, (2) resume from pause
        sampleCount          = 0;
        nextSampleIndex      = 0;
    }

    // Calculate delta for windowed tracking
    float expectedDelta = totalExtrusionMm - lastTotalExtrusionMm;

    // Only track expected position changes after first pulse received
    // This skips priming/purge moves at print start
    if (firstPulseReceived && expectedDelta > 0.01f)
    {
        // Add sample with zero actual (will be updated by sensor pulses)
        addSample(expectedDelta, 0.0f);
    }

    lastTotalExtrusionMm = totalExtrusionMm;
}

/**
 * @brief Integrates a sensor pulse (filament movement) into the windowed motion samples.
 *
 * Adds the provided filament distance for a sensor pulse to the most recent sample inside the current time window,
 * or appends a new sample at the current time when no recent sample exists. On the first detected pulse the method
 * discards any pre-pulse samples (pre-prime/purge extrusion). Also updates the timestamp of the last sensor pulse.
 *
 * @param mmPerPulse Distance in millimeters represented by this sensor pulse; pulses with values <= 0 are ignored.
 */
void FilamentMotionSensor::addSensorPulse(float mmPerPulse)
{
    if (mmPerPulse <= 0.0f || !initialized)
    {
        return;
    }

    unsigned long currentTime = millis();
    lastSensorPulseMs = currentTime;  // Track when last pulse was detected

    // First pulse received - clear any pre-pulse samples
    // This discards pre-prime/purge extrusion that happens before sensor detects movement
    if (!firstPulseReceived)
    {
        firstPulseReceived = true;
        sampleCount = 0;
        nextSampleIndex = 0;
    }

    // Update windowed tracking - add actual distance to samples within current time window
    if (sampleCount > 0)
    {
        unsigned long currentTime = millis();
        unsigned long windowStart = currentTime - windowSizeMs;

        // Find the most recent sample within the time window
        int mostRecentIndex = -1;
        unsigned long mostRecentTime = 0;

        for (int i = 0; i < sampleCount; i++)
        {
            int idx = (nextSampleIndex - sampleCount + i + MAX_SAMPLES) % MAX_SAMPLES;
            if (samples[idx].timestampMs >= windowStart && samples[idx].timestampMs > mostRecentTime)
            {
                mostRecentTime = samples[idx].timestampMs;
                mostRecentIndex = idx;
            }
        }

        // If we found a recent sample, add the pulse to it
        // Otherwise, create a new sample for this pulse
        if (mostRecentIndex >= 0)
        {
            samples[mostRecentIndex].actualMm += mmPerPulse;
        }
        else if (firstPulseReceived)
        {
            // No recent sample found, but we're tracking - add pulse to current time window
            pruneOldSamples();
            if (sampleCount < MAX_SAMPLES)
            {
                samples[nextSampleIndex].timestampMs = currentTime;
                samples[nextSampleIndex].expectedMm = 0.0f;  // No expected movement for this pulse alone
                samples[nextSampleIndex].actualMm = mmPerPulse;

                nextSampleIndex = (nextSampleIndex + 1) % MAX_SAMPLES;
                sampleCount++;
            }
        }
    }
}

/**
 * @brief Records a new movement sample (expected vs actual) into the time window.
 *
 * Adds a timestamped sample containing the provided expected and actual extrusion deltas,
 * updates the duration of the previous sample based on the current time, and discards
 * any samples outside the configured time window before appending.
 *
 * @param expectedDeltaMm Expected extrusion distance for this sample, in millimeters.
 * @param actualDeltaMm   Actual measured extrusion distance for this sample, in millimeters.
 */
void FilamentMotionSensor::addSample(float expectedDeltaMm, float actualDeltaMm)
{
    unsigned long currentTime = millis();

    // Prune old samples first
    pruneOldSamples();

    if (sampleCount > 0)
    {
        int previousIndex = (nextSampleIndex - 1 + MAX_SAMPLES) % MAX_SAMPLES;
        unsigned long prevTimestamp = samples[previousIndex].timestampMs;
        unsigned long duration = currentTime - prevTimestamp;
        if (duration == 0)
        {
            duration = 1;
        }
        if (duration > windowSizeMs)
        {
            duration = windowSizeMs;
        }
        samples[previousIndex].durationMs = duration;
    }

    // Add new sample
    samples[nextSampleIndex].timestampMs = currentTime;
    samples[nextSampleIndex].expectedMm  = expectedDeltaMm;
    samples[nextSampleIndex].actualMm    = actualDeltaMm;
    samples[nextSampleIndex].durationMs  = 0;

    nextSampleIndex = (nextSampleIndex + 1) % MAX_SAMPLES;
    if (sampleCount < MAX_SAMPLES)
    {
        sampleCount++;
    }
}

/**
 * @brief Removes samples older than the configured time window from the circular buffer.
 *
 * Uses the current time (millis()) to compute a cutoff (currentTime - windowSizeMs) and drops
 * any samples with timestamps earlier than that cutoff by shrinking sampleCount.
 * If all samples are older than the cutoff, sampleCount is set to 0. The circular buffer's
 * nextSampleIndex is intentionally left unchanged so readers continue to use the same base index.
 */
void FilamentMotionSensor::pruneOldSamples()
{
    if (sampleCount == 0)
    {
        return;
    }

    unsigned long currentTime = millis();
    unsigned long cutoffTime  = currentTime - windowSizeMs;

    // Find the first sample in the window
    int firstKeptIndex = sampleCount; // assume none
    for (int i = 0; i < sampleCount; i++)
    {
        int idx = (nextSampleIndex - sampleCount + i + MAX_SAMPLES) % MAX_SAMPLES;
        if (samples[idx].timestampMs >= cutoffTime)
        {
            firstKeptIndex = i;
            break;
        }
    }

    if (firstKeptIndex == sampleCount)
    {
        // All samples are too old
        sampleCount = 0;
        return;
    }

    int keptCount = sampleCount - firstKeptIndex;
    sampleCount   = keptCount;

    // nextSampleIndex stays the same; base for readers moves forward because sampleCount shrank
}

void FilamentMotionSensor::getWindowedDistances(float &expectedMm, float &actualMm)
{
    expectedMm = 0.0f;
    actualMm   = 0.0f;

    pruneOldSamples();

    // Sum all samples in window
    for (int i = 0; i < sampleCount; i++)
    {
        int idx = (nextSampleIndex - sampleCount + i + MAX_SAMPLES) % MAX_SAMPLES;
        expectedMm += samples[idx].expectedMm;
        actualMm   += samples[idx].actualMm;
    }
}

float FilamentMotionSensor::getDeficit()
{
    if (!initialized)
    {
        return 0.0f;
    }

    float expectedDistance = getExpectedDistance();
    float actualDistance   = getSensorDistance();
    float deficit          = expectedDistance - actualDistance;

    return deficit > 0.0f ? deficit : 0.0f;
}

/**
 * @brief Compute the total expected extrusion distance within the sensor's active time window.
 *
 * @return Total expected extrusion in millimeters summed across samples within the current window; returns 0.0 if the sensor is not initialized or no samples are present.
 */
float FilamentMotionSensor::getExpectedDistance()
{
    if (!initialized)
    {
        return 0.0f;
    }

    float expectedMm, actualMm;
    getWindowedDistances(expectedMm, actualMm);
    return expectedMm;
}

/**
 * @brief Retrieves the total actual filament movement recorded within the current time window.
 *
 * @return float Total actual movement in millimeters recorded by the sensor over the window; `0.0` if the sensor is not initialized or no samples are present.
 */
float FilamentMotionSensor::getSensorDistance()
{
    if (!initialized)
    {
        return 0.0f;
    }

    float expectedMm, actualMm;
    getWindowedDistances(expectedMm, actualMm);
    return actualMm;
}

/**
 * @brief Compute average expected and actual filament movement rates over the current time window.
 *
 * Calculates the windowed average rates (in millimeters per second) for expected extrusion and
 * sensor-measured actual movement using the stored samples within the configured window. Old
 * samples are pruned before computation. If there are no samples or the aggregated sample
 * duration is less than 100 ms, both outputs remain 0.0.
 *
 * @param[out] expectedRate Average expected extrusion rate in mm/s.
 * @param[out] actualRate   Average sensor-measured movement rate in mm/s.
 */
void FilamentMotionSensor::getWindowedRates(float &expectedRate, float &actualRate)
{
    expectedRate = 0.0f;
    actualRate = 0.0f;

    pruneOldSamples();
    if (sampleCount == 0)
    {
        return;
    }

    unsigned long now = millis();
    unsigned long totalDurationMs = 0;
    float expectedSum = 0.0f;
    float actualSum = 0.0f;

    for (int i = 0; i < sampleCount; i++)
    {
        int idx = (nextSampleIndex - sampleCount + i + MAX_SAMPLES) % MAX_SAMPLES;
        unsigned long duration = samples[idx].durationMs;
        if (duration == 0)
        {
            duration = (now > samples[idx].timestampMs)
                           ? (now - samples[idx].timestampMs)
                           : 1;
        }
        if (duration == 0)
        {
            continue;
        }
        if (duration > windowSizeMs)
        {
            duration = windowSizeMs;
        }

        expectedSum += samples[idx].expectedMm;
        actualSum   += samples[idx].actualMm;
        totalDurationMs += duration;
    }

    // Require minimum duration to avoid division issues and unstable rate calculations
    // 100ms minimum ensures reasonable rate values
    if (totalDurationMs < 100)
    {
        return;
    }

    float durationSec = static_cast<float>(totalDurationMs) / 1000.0f;
    expectedRate = expectedSum / durationSec;
    actualRate   = actualSum / durationSec;
}

/**
 * @brief Indicates whether the sensor has been initialized with baseline extrusion telemetry.
 *
 * @return `true` if the sensor has been initialized (baseline established), `false` otherwise.
 */
bool FilamentMotionSensor::isInitialized() const
{
    return initialized;
}

bool FilamentMotionSensor::isWithinGracePeriod(unsigned long gracePeriodMs) const
{
    if (!initialized || gracePeriodMs == 0)
    {
        return false;
    }
    unsigned long currentTime = millis();
    return (currentTime - lastExpectedUpdateMs) < gracePeriodMs;
}

float FilamentMotionSensor::getFlowRatio()
{
    if (!initialized)
    {
        return 0.0f;
    }

    float expectedDistance = getExpectedDistance();
    if (expectedDistance <= 0.0f)
    {
        return 0.0f;
    }

    float actualDistance = getSensorDistance();
    float ratio = actualDistance / expectedDistance;

    // Clamp to reasonable range [0, 1.5]
    if (ratio > 1.5f) ratio = 1.5f;
    if (ratio < 0.0f) ratio = 0.0f;

    return ratio;
}