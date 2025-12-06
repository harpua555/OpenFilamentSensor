#include "FilamentMotionSensor.h"

static const unsigned long INVALID_SAMPLE_TIMESTAMP = ~0UL;

/**
 * @brief Construct a FilamentMotionSensor configured for filament motion tracking.
 *
 * Initializes the sensor object with a default 5000 ms (5 second) sliding window and prepares
 * internal state for collecting expected and actual extrusion samples.
 */
FilamentMotionSensor::FilamentMotionSensor()
{
    windowSizeMs = 5000;  // 5 second window
    reset();
}

/**
 * @brief Reset internal state and windowed tracking to an initial baseline.
 *
 * Clears initialization and pulse-tracking flags, resets the extrusion baseline
 * and timestamp anchors, and empties the sliding window of samples so subsequent
 * measurements start from a clean state.
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
 * @brief Update expected filament position using cumulative extrusion telemetry.
 *
 * Establishes an initial baseline on first telemetry, detects retractions to clear
 * the windowed tracking buffer (without touching the grace-period timer), and
 * records incremental expected extrusion into the sample window when appropriate.
 *
 * @param totalExtrusionMm Cumulative extruder distance in millimeters from firmware telemetry.
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
 * @brief Record a sensor pulse into the windowed motion tracker.
 *
 * Adds the given filament distance represented by a sensor pulse to the current time window:
 * it updates the timestamp of the last pulse, discards any pre-pulse samples when the first
 * detected pulse arrives, and associates the pulse with the most recent sample within the
 * active window or creates a new sample when appropriate.
 *
 * @param mmPerPulse Millimeters of filament represented by this pulse; ignored if less than
 *                   or equal to 0.0.
 *
 * Notes:
 * - No action is taken if the sensor subsystem is not initialized.
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
 * @brief Record a new expected/actual filament movement sample into the time window.
 *
 * Prunes samples outside the configured window, finalizes the duration for the previous
 * sample based on elapsed time (capped to the window), and appends a new sample with
 * the provided expected and actual delta distances. Uses a fixed-size circular buffer
 * and increments the stored sample count when space permits.
 *
 * @param expectedDeltaMm Expected extrusion distance for this sample, in millimeters.
 * @param actualDeltaMm Actual measured extrusion distance for this sample, in millimeters.
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
 * @brief Removes stored samples that fall outside the configured time window.
 *
 * Prunes the internal circular sample buffer so only samples with timestamps
 * within the most recent windowSizeMs remain. If no samples are within the
 * window, the stored sample count is set to zero. The insertion index
 * (nextSampleIndex) is not changed; callers should read samples using the
 * updated sampleCount to determine the current valid range.
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
 * @brief Retrieve the accumulated expected filament movement over the current time window.
 *
 * @return float Accumulated expected filament movement in millimeters within the sensor's configured window; returns 0.0 if the sensor is not initialized.
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
 * @brief Retrieves the filament movement measured by the sensor within the current time window.
 *
 * @return The accumulated filament movement from sensor pulses, in millimeters, within the active window.
 *         Returns `0.0f` if the sensor is not initialized or no samples are available.
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
 * @brief Compute the windowed expected and actual filament feed rates.
 *
 * Computes feed rates (millimeters per second) for both the expected extrusion
 * and the measured sensor distance using samples inside the current time
 * window. If there are fewer than 100 ms of accumulated sample duration the
 * outputs remain 0 to avoid unstable rate estimates.
 *
 * @param[out] expectedRate Receives the computed expected feed rate in mm/s.
 * @param[out] actualRate Receives the computed measured feed rate in mm/s.
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
 * @brief Reports whether the filament motion sensor has completed its initialization.
 *
 * @return `true` if the sensor has been initialized, `false` otherwise.
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