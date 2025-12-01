#include "FilamentMotionSensor.h"

static const unsigned long INVALID_SAMPLE_TIMESTAMP = ~0UL;

FilamentMotionSensor::FilamentMotionSensor()
{
    windowSizeMs = 5000;  // 5 second window
    reset();
}

void FilamentMotionSensor::reset()
{
    initialized           = false;
    firstPulseReceived    = false;  // Reset pulse tracking
    lastExpectedUpdateMs  = millis();

    // Reset windowed state
    sampleCount           = 0;
    nextSampleIndex       = 0;
    for (int i = 0; i < MAX_SAMPLES; i++)
    {
        samples[i].timestampMs = 0;
        samples[i].expectedMm  = 0.0f;
        samples[i].actualMm    = 0.0f;
    }

    lastSensorPulseMs = millis();  // Initialize to current time
}

void FilamentMotionSensor::updateExpectedPosition(float totalExtrusionMm)
{
    unsigned long currentTime = millis();
    static float lastTotalExtrusionMm = 0.0f;

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

void FilamentMotionSensor::addSample(float expectedDeltaMm, float actualDeltaMm)
{
    unsigned long currentTime = millis();

    // Prune old samples first
    pruneOldSamples();

    // Add new sample
    samples[nextSampleIndex].timestampMs = currentTime;
    samples[nextSampleIndex].expectedMm  = expectedDeltaMm;
    samples[nextSampleIndex].actualMm    = actualDeltaMm;

    nextSampleIndex = (nextSampleIndex + 1) % MAX_SAMPLES;
    if (sampleCount < MAX_SAMPLES)
    {
        sampleCount++;
    }
}

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
