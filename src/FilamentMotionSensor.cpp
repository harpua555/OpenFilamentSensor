#include "FilamentMotionSensor.h"

FilamentMotionSensor::FilamentMotionSensor()
{
    reset();
}

void FilamentMotionSensor::reset()
{
    initialized           = false;
    firstPulseReceived    = false;
    lastExpectedUpdateMs  = millis();
    lastTotalExtrusionMm  = 0.0f;
    
    // Reset global pulse counters
    totalSensorMm         = 0.0f;
    sensorMmAtLastUpdate  = 0.0f;
    preInitActualMm       = 0.0f;
    preInitPulseCount     = 0;
    lastSensorPulseMs     = millis();

    // Clear buckets
    for (int i = 0; i < BUCKET_COUNT; i++)
    {
        expectedBuckets[i]  = 0.0f;
        actualBuckets[i]    = 0.0f;
        bucketTimestamps[i] = 0; // 0 will be treated as stale immediately
    }
}

int FilamentMotionSensor::getCurrentBucketIndex()
{
    unsigned long now = millis();
    int index = (now / BUCKET_SIZE_MS) % BUCKET_COUNT;
    
    // Check if this bucket belongs to a previous time cycle
    // We allow a small tolerance or strict equality, but strictly 
    // speaking, if the stored timestamp isn't essentially "now" (rounded to bucket),
    // it is stale.
    // Simpler check: If timestamp is older than WINDOW_SIZE, it is definitely garbage.
    // But since we wrap around, we must clear it *before* writing if it's from the last lap.
    
    // We clear aggressively: If the bucket's timestamp doesn't match the current 
    // calculated window-slot time, it is stale.
    // Actually, simple ring buffer logic:
    // If we are writing to it, we ensure it's fresh.
    
    unsigned long bucketStart = (now / BUCKET_SIZE_MS) * BUCKET_SIZE_MS;
    
    if (bucketTimestamps[index] != bucketStart)
    {
        // This is a new time slot for this index. Reset it.
        expectedBuckets[index] = 0.0f;
        actualBuckets[index]   = 0.0f;
        bucketTimestamps[index] = bucketStart;
    }
    
    return index;
}

void FilamentMotionSensor::clearStaleBuckets(unsigned long currentTime)
{
    // Optional maintenance: Iterate and clear buckets older than window
    // This isn't strictly necessary for correctness if we only Sum valid ones,
    // but helps debugging.
    // We will rely on sumWindow() filtering instead for performance.
}

void FilamentMotionSensor::updateExpectedPosition(float totalExtrusionMm)
{
    unsigned long now = millis();

    if (!initialized)
    {
        initialized           = true;
        lastExpectedUpdateMs  = now;
        lastTotalExtrusionMm  = totalExtrusionMm;
        sensorMmAtLastUpdate  = totalSensorMm;
        
        // Handle pre-init pulses?
        // In the old code, we added them to the first sample.
        // In decoupled logic, we can just dump them into the current actual bucket
        // (effectively saying "they happened now").
        // Or we ignore them. Let's ignore them for stability, or minimal impact.
        // But we DO mark first pulse received to enable tracking.
        if (preInitPulseCount > 0)
        {
             firstPulseReceived = true;
             // Add pre-init mm to totalSensorMm? It's already there (addSensorPulse adds to total).
             // But we need to sync the snapshot.
             sensorMmAtLastUpdate = totalSensorMm; // Baseline starts NOW.
        }
        return;
    }

    // 1. Calculate Raw Deltas
    float expectedDelta = totalExtrusionMm - lastTotalExtrusionMm;
    float actualSinceLast = totalSensorMm - sensorMmAtLastUpdate;
    
    // 2. Calculate "Orphaned" Actuals
    // These are pulses that happened since the last update but are NO LONGER in the window.
    // To find this, we sum the *current* window.
    float winExpected, winActual;
    sumWindow(winExpected, winActual);
    
    // If the window holds 20mm, but we moved 100mm since last update, 
    // then 80mm have fallen off the edge.
    float orphanedActual = actualSinceLast - winActual;
    if (orphanedActual < 0.0f) orphanedActual = 0.0f;
    
    // 3. Adjust Expected Delta (Orphan Subtraction)
    // Reduce the spike by the amount of history we lost.
    float adjustedDelta = expectedDelta - orphanedActual;
    if (adjustedDelta < 0.0f) adjustedDelta = 0.0f;

    // 4. Record to Bucket
    if (adjustedDelta > 0.001f) // Filter tiny noise
    {
        int index = getCurrentBucketIndex();
        expectedBuckets[index] += adjustedDelta;
    }

    // 5. Update Snapshots
    lastTotalExtrusionMm = totalExtrusionMm;
    sensorMmAtLastUpdate = totalSensorMm;
    lastExpectedUpdateMs = now;
}

void FilamentMotionSensor::addSensorPulse(float mmPerPulse)
{
    if (mmPerPulse <= 0.0f) return;

    if (!initialized)
    {
        preInitActualMm += mmPerPulse;
        preInitPulseCount++;
        totalSensorMm += mmPerPulse; // Maintain global total
        return;
    }

    // Simply add to the current time bucket
    int index = getCurrentBucketIndex();
    actualBuckets[index] += mmPerPulse;
    
    // Maintain global monotonic counter
    totalSensorMm += mmPerPulse;
    lastSensorPulseMs = millis();
    firstPulseReceived = true;
}

void FilamentMotionSensor::sumWindow(float &outExpected, float &outActual)
{
    outExpected = 0.0f;
    outActual   = 0.0f;
    
    unsigned long now = millis();
    unsigned long cutoff = now - WINDOW_SIZE_MS;
    
    // Iterate all buckets
    for (int i = 0; i < BUCKET_COUNT; i++)
    {
        // Only include buckets that are strictly within the window
        // (bucketTimestamp >= cutoff)
        if (bucketTimestamps[i] >= cutoff && bucketTimestamps[i] <= now)
        {
            outExpected += expectedBuckets[i];
            outActual   += actualBuckets[i];
        }
    }
}

float FilamentMotionSensor::getDeficit()
{
    if (!initialized) return 0.0f;
    
    float exp, act;
    sumWindow(exp, act);
    
    float deficit = exp - act;
    return (deficit > 0.0f) ? deficit : 0.0f;
}

float FilamentMotionSensor::getExpectedDistance()
{
    if (!initialized) return 0.0f;
    float exp, act;
    sumWindow(exp, act);
    return exp;
}

float FilamentMotionSensor::getSensorDistance()
{
    if (!initialized) return 0.0f;
    float exp, act;
    sumWindow(exp, act);
    return act;
}

void FilamentMotionSensor::getWindowedRates(float &expectedRate, float &actualRate)
{
    expectedRate = 0.0f;
    actualRate   = 0.0f;
    
    if (!initialized) return;

    float expSum, actSum;
    sumWindow(expSum, actSum);
    
    // In the decoupled model, we calculate rate over the full window size
    // OR the valid duration.
    // For simplicity and stability, we assume the window is filling up or full.
    // We divide by WINDOW_SIZE_MS if full?
    // Better: We track how much time is actually covered by the valid buckets.
    
        static const unsigned long MIN_VALID_DURATION_MS = 250;
    
        unsigned long now = millis();
    
        unsigned long cutoff;
    
        if (now < WINDOW_SIZE_MS) {
    
            cutoff = 0; // Prevent unsigned long underflow
    
        } else {
    
            cutoff = now - WINDOW_SIZE_MS;
    
        }
    
    
    
        unsigned long validDuration = 0;
    
        
    
        for (int i = 0; i < BUCKET_COUNT; i++)
    
        {
    
            if (bucketTimestamps[i] >= cutoff && bucketTimestamps[i] <= now)
    
            {
    
                validDuration += BUCKET_SIZE_MS;
    
            }
    
        }
    
        
    
        // If we have very little data (e.g. just started), prevent division by zero
    
        if (validDuration < MIN_VALID_DURATION_MS) validDuration = MIN_VALID_DURATION_MS;  
    
    float durationSec = validDuration / 1000.0f;
    expectedRate = expSum / durationSec;
    actualRate   = actSum / durationSec;
}

bool FilamentMotionSensor::isInitialized() const
{
    return initialized;
}

bool FilamentMotionSensor::isWithinGracePeriod(unsigned long gracePeriodMs) const
{
    if (!initialized || gracePeriodMs == 0) return false;
    unsigned long now = millis();
    return (now - lastExpectedUpdateMs) < gracePeriodMs;
}

float FilamentMotionSensor::getFlowRatio()
{
    if (!initialized) return 0.0f;
    float exp = getExpectedDistance();
    if (exp <= 0.001f) return 0.0f;
    
    float ratio = getSensorDistance() / exp;
    if (ratio > 1.5f) ratio = 1.5f;
    if (ratio < 0.0f) ratio = 0.0f;
    return ratio;
}