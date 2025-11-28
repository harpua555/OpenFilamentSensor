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

    // Reset jam tracking
    lastWindowDeficitMm     = 0.0f;
    lastDeficitTimestampMs  = 0;
    lastJamEvaluationMs     = 0;
    hardJamAccumulatedMs    = 0;
    hardJamConsecutiveChecks = 0;
    hardJamRequiredChecks   = 0;
    softJamAccumulatedMs    = 0;
    lastSoftJamTimeMs       = 0;
    softJamActive           = false;
    softJamDeficitAccumMm   = 0.0f;
    hardJamAccumExpectedMm  = 0.0f;
    hardJamAccumActualMm    = 0.0f;
    lastSensorPulseMs       = millis();  // Initialize to current time
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

    // Update windowed tracking - add actual distance to most recent sample
    if (sampleCount > 0)
    {
        // Find most recent sample and add actual distance
        int mostRecentIndex = (nextSampleIndex - 1 + MAX_SAMPLES) % MAX_SAMPLES;
        if (mostRecentIndex >= 0 && mostRecentIndex < sampleCount)
        {
            samples[mostRecentIndex].actualMm += mmPerPulse;
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
    unsigned long currentTime = millis();
    unsigned long cutoffTime  = currentTime - windowSizeMs;

    // Remove samples older than window
    int newCount = 0;
    for (int i = 0; i < sampleCount; i++)
    {
        int idx = (nextSampleIndex - sampleCount + i + MAX_SAMPLES) % MAX_SAMPLES;
        if (samples[idx].timestampMs >= cutoffTime)
        {
            // Keep this sample
            if (newCount != i)
            {
                // Compact array
                int newIdx = (nextSampleIndex - sampleCount + newCount + MAX_SAMPLES) % MAX_SAMPLES;
                samples[newIdx] = samples[idx];
            }
            newCount++;
        }
    }

    sampleCount = newCount;
    // Update nextSampleIndex to point after the last valid sample
    // This ensures mostRecentIndex calculation works correctly after pruning
    if (sampleCount > 0)
    {
        nextSampleIndex = sampleCount;
    }
    else
    {
        nextSampleIndex = 0;
    }
}

void FilamentMotionSensor::getWindowedDistances(float &expectedMm, float &actualMm) const
{
    expectedMm = 0.0f;
    actualMm   = 0.0f;

    // Sum all samples in window
    for (int i = 0; i < sampleCount; i++)
    {
        int idx = (nextSampleIndex - sampleCount + i + MAX_SAMPLES) % MAX_SAMPLES;
        expectedMm += samples[idx].expectedMm;
        actualMm   += samples[idx].actualMm;
    }
}

bool FilamentMotionSensor::isJammed(float ratioThreshold, float hardJamThresholdMm,
                                    int softJamTimeMs, int hardJamTimeMs, int checkIntervalMs,
                                    unsigned long gracePeriodMs) const
{
    if (!initialized || checkIntervalMs <= 0)
    {
        return false;
    }

    if (ratioThreshold <= 0.0f)
    {
        ratioThreshold = 0.25f;
    }
    if (ratioThreshold > 1.0f)
    {
        ratioThreshold = 1.0f;
    }
    if (softJamTimeMs <= 0)
    {
        softJamTimeMs = 10000;
    }
    if (hardJamTimeMs <= 0)
    {
        hardJamTimeMs = 5000;
    }

    unsigned long currentTime = millis();

    if (gracePeriodMs > 0)
    {
        unsigned long timeSinceUpdate = currentTime - lastExpectedUpdateMs;
        if (timeSinceUpdate < gracePeriodMs)
        {
            hardJamAccumulatedMs     = 0;
            hardJamConsecutiveChecks = 0;
            softJamAccumulatedMs     = 0;
            softJamActive            = false;
            lastJamEvaluationMs      = currentTime;
            return false;
        }
    }

    float expectedDistance = getExpectedDistance();
    float actualDistance   = getSensorDistance();
    float windowDeficit    = expectedDistance - actualDistance;
    if (windowDeficit < 0.0f)
    {
        windowDeficit = 0.0f;
    }

    lastWindowDeficitMm    = windowDeficit;
    lastDeficitTimestampMs = currentTime;

    float passingRatio = (expectedDistance > 0.0f) ? (actualDistance / expectedDistance) : 1.0f;
    if (passingRatio < 0.0f)
    {
        passingRatio = 0.0f;
    }

    unsigned long evaluationDeltaMs = (lastJamEvaluationMs == 0) ? (unsigned long)checkIntervalMs
                                                                    : currentTime - lastJamEvaluationMs;
    if (evaluationDeltaMs > (unsigned long)checkIntervalMs)
    {
        evaluationDeltaMs = checkIntervalMs;
    }
    lastJamEvaluationMs = currentTime;

    const float HARD_PASS_RATIO_THRESHOLD      = 0.35f;  // Trigger if <35% passing (severe jam/slippage)
    const float MIN_HARD_WINDOW_EXPECTED_MM    = 1.0f;
    const int   MIN_HARD_WINDOW_SAMPLES        = 3;  // Require 3+ samples to prevent false positives after retractions
    unsigned int requiredHardChecks            = (hardJamTimeMs + checkIntervalMs - 1) / checkIntervalMs;
    if (requiredHardChecks == 0)
    {
        requiredHardChecks = 1;
    }
    hardJamRequiredChecks = requiredHardChecks;

    // Hard jam condition (per evaluation):
    //  - windowed expected distance is at least 1mm, and
    //  - less than 35% of filament is passing (severe jam/heavy slippage), and
    //  - we have at least 3 samples in the window (prevents false positives right after retractions)
    bool hardCondition = (expectedDistance >= MIN_HARD_WINDOW_EXPECTED_MM) &&
                         (passingRatio < HARD_PASS_RATIO_THRESHOLD) &&
                         (sampleCount >= MIN_HARD_WINDOW_SAMPLES);

    // Only reset hard-jam accumulation when we see real movement after we have
    // started counting. Transient ratio improvements without pulses should not
    // erase progress toward a hard jam.
    unsigned long timeSinceLastPulse  = currentTime - lastSensorPulseMs;
    bool          receivedPulseRecent = (timeSinceLastPulse <= (unsigned long)(checkIntervalMs + 500));

    if (hardCondition)
    {
        hardJamAccumulatedMs += evaluationDeltaMs;
        if (hardJamAccumulatedMs > (unsigned long)hardJamTimeMs)
        {
            hardJamAccumulatedMs = hardJamTimeMs;
        }
    }
    else if (hardJamAccumulatedMs > 0 && receivedPulseRecent)
    {
        // Jam progress is cleared only when we have seen pulses again and the
        // window no longer looks like a hard jam.
        hardJamAccumulatedMs = 0;
    }
    // else: keep accumulated time as-is (no pulses, or never started)

    // Derive "consecutive checks" for UI from accumulated time so it reflects
    // actual seconds spent in a hard-jam-like state.
    hardJamConsecutiveChecks = hardJamAccumulatedMs / (unsigned long)checkIntervalMs;

    bool hardJamTriggered = false;
    if (hardJamAccumulatedMs >= (unsigned long)hardJamTimeMs)
    {
        // Final safety: only veto the jam if the printer is effectively
        // requesting no filament over this window (idle / travel / ironing).
        // We gate on expected distance, not sensor movement: a real hard jam
        // is exactly "non‑trivial expected, zero pulses".
        if (expectedDistance >= MIN_HARD_WINDOW_EXPECTED_MM)
        {
            hardJamTriggered = true;
        }
        else
        {
            hardJamAccumulatedMs     = 0;
            hardJamConsecutiveChecks = 0;
        }
    }

    // Soft jam detection uses windowed deficit (not cumulative delta)
    // This allows it to catch gradual partial clogs over the tracking window
    const float MIN_SOFT_DEFICIT_MM = 0.5f;
    const float MIN_SOFT_PER_CHECK_MM = 0.25f;

    // Use windowed deficit for per-check threshold
    // windowDeficit is already calculated above from expectedDistance - actualDistance
    bool softCondition = passingRatio < ratioThreshold && windowDeficit >= MIN_SOFT_PER_CHECK_MM;

    if (softCondition)
    {
        softJamActive = true;
        softJamAccumulatedMs += evaluationDeltaMs;
        if (softJamAccumulatedMs > (unsigned long)softJamTimeMs)
        {
            softJamAccumulatedMs = softJamTimeMs;
        }
        softJamDeficitAccumMm += windowDeficit;
    }
    else
    {
        softJamActive = false;
        softJamAccumulatedMs = 0;
        softJamDeficitAccumMm = 0.0f;
    }
    lastSoftJamTimeMs = softJamTimeMs;

    bool softJamTriggered = softJamAccumulatedMs >= (unsigned long)softJamTimeMs &&
                            softJamDeficitAccumMm >= MIN_SOFT_DEFICIT_MM;

    return hardJamTriggered || softJamTriggered;
}

float FilamentMotionSensor::getHardJamProgressPercent() const
{
    if (hardJamRequiredChecks == 0)
    {
        return 0.0f;
    }

    float percent = (100.0f * (float)hardJamConsecutiveChecks) / (float)hardJamRequiredChecks;
    if (percent > 100.0f)
    {
        percent = 100.0f;
    }
    return percent;
}

float FilamentMotionSensor::getSoftJamProgressPercent() const
{
    if (lastSoftJamTimeMs <= 0)
    {
        return 0.0f;
    }

    float percent = (100.0f * (float)softJamAccumulatedMs) / (float)lastSoftJamTimeMs;
    if (percent > 100.0f)
    {
        percent = 100.0f;
    }
    return percent;
}

float FilamentMotionSensor::getDeficit() const
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

float FilamentMotionSensor::getExpectedDistance() const
{
    if (!initialized)
    {
        return 0.0f;
    }

    float expectedMm, actualMm;
    getWindowedDistances(expectedMm, actualMm);
    return expectedMm;
}

float FilamentMotionSensor::getSensorDistance() const
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

float FilamentMotionSensor::getFlowRatio() const
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
