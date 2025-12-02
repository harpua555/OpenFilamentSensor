#include "JamDetector.h"
#include "Logger.h"
#include "SettingsManager.h"

// Global singletons (provided elsewhere)

namespace
{
    // Minimum windowed distance before we even try to declare a jam.
    constexpr float MIN_HARD_WINDOW_MM       = 10.0f;
    constexpr float MIN_SOFT_WINDOW_MM       = 8.0f;
    constexpr float MIN_SOFT_DEFICIT_MM      = 4.0f;

    // For rate-based detection (mm/s)
    constexpr float MIN_EXPECTED_RATE_MM_S   = 0.4f;   // below this we consider it not really extruding
    constexpr float MIN_RATE_FOR_RATIO_MM_S  = 0.2f;   // below this we just treat ratio as 1.0
    constexpr float MIN_ACTUAL_RATE_MM_S     = 0.05f;  // basically no movement

    constexpr float HARD_RATE_RATIO          = 0.25f;  // hard jam if sensor < 25% of expected
    constexpr float HARD_RECOVERY_RATIO      = 0.75f;  // recovery once >= 75% of expected rate

    // Smoothed "how bad is the deficit" purely for UI
    constexpr float RATIO_SMOOTHING_ALPHA    = 0.08f;

    // Resume grace: disable detection until we have moved enough again
    constexpr float         RESUME_GRACE_15MM_THRESHOLD = 15.0f;  // ~15mm expected extrusion after resume
    constexpr unsigned long RESUME_MIN_PULSES           = 5;      // or a few pulses, whichever comes first

    // We do not let dt explode; caps keep rates reasonably stable
    constexpr unsigned long MAX_EVAL_INTERVAL_MS        = 1000;
    constexpr unsigned long DEFAULT_EVAL_INTERVAL_MS    = 1000;
    constexpr bool          USE_WINDOWED_RATE_SAMPLES   = true;
}

JamDetector::JamDetector()
{
    reset(0);
}

void JamDetector::reset(unsigned long currentTimeMs)
{
    (void)currentTimeMs;

    state.jammed               = false;
    state.hardJamTriggered     = false;
    state.softJamTriggered     = false;
    state.hardJamPercent       = 0.0f;
    state.softJamPercent       = 0.0f;
    state.passRatio            = 1.0f;
    state.deficit              = 0.0f;
    state.expectedRateMmPerSec = 0.0f;
    state.actualRateMmPerSec   = 0.0f;
    state.graceState           = GraceState::IDLE;
    state.graceActive          = false;

    hardJamAccumulatedMs       = 0;
    softJamAccumulatedMs       = 0;
    lastEvalMs                 = 0;
    lastPulseCount             = 0;
    resumeGracePulseBaseline   = 0;
    resumeGraceActualBaseline  = 0.0f;
    resumeGraceStartTimeMs     = 0;
    prevExpectedDistance       = 0.0f;
    prevActualDistance         = 0.0f;
    jamPauseRequested          = false;
    wasInGrace                 = false;
    smoothedDeficitRatio       = 0.0f;
}

void JamDetector::onResume(unsigned long currentTimeMs,
                           unsigned long currentPulseCount,
                           float         currentActualMm)
{
    state.graceState  = GraceState::RESUME_GRACE;
    state.graceActive = true;

    resumeGracePulseBaseline  = currentPulseCount;
    resumeGraceActualBaseline = currentActualMm;
    resumeGraceStartTimeMs    = currentTimeMs;

    // Clear existing jam accumulation so we do not instantly re-trigger
    hardJamAccumulatedMs   = 0;
    softJamAccumulatedMs   = 0;
    state.hardJamPercent   = 0.0f;
    state.softJamPercent   = 0.0f;
    state.jammed           = false;
    state.hardJamTriggered = false;
    state.softJamTriggered = false;

    // Clear pause request flag so future jams can be detected
    jamPauseRequested = false;
}

bool JamDetector::evaluateGraceState(unsigned long currentTimeMs,
                                     unsigned long printStartTimeMs,
                                     float         expectedDistance,
                                     unsigned long movementPulseCount,
                                     const JamConfig& config)
{
    switch (state.graceState)
    {
        case GraceState::IDLE:
            state.graceActive = false;
            return false;

        case GraceState::START_GRACE:
        {
            unsigned long sinceStart = currentTimeMs - printStartTimeMs;

            // Startup timeout: SDCP can be messy right after print start
            if (sinceStart < config.startTimeoutMs)
            {
                state.graceActive = true;
                return true;
            }

            // Grace window after timeout, regardless of flow
            if (sinceStart < config.graceTimeMs)
            {
                state.graceActive = true;
                return true;
            }

            // Enough time has passed; go active
            state.graceState  = GraceState::ACTIVE;
            state.graceActive = false;
            return false;
        }

        case GraceState::RESUME_GRACE:
        {
            // Conditions to exit resume grace: enough pulses, enough expected distance, or timeout
            bool enoughPulses   = (movementPulseCount >= resumeGracePulseBaseline + RESUME_MIN_PULSES);
            bool enoughExpected = (expectedDistance >= RESUME_GRACE_15MM_THRESHOLD);
            unsigned long sinceResume = currentTimeMs - resumeGraceStartTimeMs;
            bool timeExceeded  = (sinceResume >= config.graceTimeMs);

            if (!enoughPulses && !enoughExpected && !timeExceeded)
            {
                state.graceActive = true;
                return true;  // stay in grace
            }

            state.graceState  = GraceState::ACTIVE;
            state.graceActive = false;
            return false;
        }

        case GraceState::ACTIVE:
            state.graceActive = false;
            return false;

        case GraceState::JAMMED:
            // Jammed is latched; no special grace
            state.graceActive = false;
            return false;
    }

    state.graceActive = false;
    return false;
}

bool JamDetector::evaluateHardJam(float         expectedDistance,
                                  float         passRatio,
                                  float         expectedRate,
                                  float         actualRate,
                                  unsigned long elapsedMs,
                                  const JamConfig& config)
{
    // Hard jam if:
    //  - we are really extruding
    //  - window has enough distance to be meaningful
    //  - sensor rate is tiny
    //  - rate ratio is very low
    bool extrudingNow = (expectedRate >= MIN_EXPECTED_RATE_MM_S);

    bool hardCondition =
        extrudingNow &&
        (expectedDistance >= MIN_HARD_WINDOW_MM) &&
        (actualRate < MIN_ACTUAL_RATE_MM_S) &&
        (passRatio < HARD_RATE_RATIO);

    if (hardCondition)
    {
        hardJamAccumulatedMs += elapsedMs;
        if (hardJamAccumulatedMs > config.hardJamTimeMs)
        {
            hardJamAccumulatedMs = config.hardJamTimeMs;
        }
    }
    else
    {
        // Recovery: once we are back to good flow, clear
        if (passRatio >= HARD_RECOVERY_RATIO || !extrudingNow)
        {
            hardJamAccumulatedMs = 0;
        }
    }

    if (config.hardJamTimeMs > 0)
    {
        state.hardJamPercent =
            (100.0f * static_cast<float>(hardJamAccumulatedMs) /
             static_cast<float>(config.hardJamTimeMs));
    }
    else
    {
        state.hardJamPercent = hardCondition ? 100.0f : 0.0f;
    }

    return (hardJamAccumulatedMs >= config.hardJamTimeMs);
}

bool JamDetector::evaluateSoftJam(float         expectedDistance,
                                  float         deficit,
                                  float         passRatio,
                                  float         expectedRate,
                                  float         actualRate,
                                  unsigned long elapsedMs,
                                  const JamConfig& config)
{
    (void)actualRate;  // not strictly needed, but kept for future tuning

    bool extrudingNow = (expectedRate >= MIN_EXPECTED_RATE_MM_S);

    // Soft jam: we are extruding, deficit is slowly growing, and ratio is below threshold
    bool softCondition =
        extrudingNow &&
        (expectedDistance >= MIN_SOFT_WINDOW_MM) &&
        (deficit >= MIN_SOFT_DEFICIT_MM) &&
        (passRatio < config.ratioThreshold);

    if (softCondition)
    {
        softJamAccumulatedMs += elapsedMs;
        if (softJamAccumulatedMs > config.softJamTimeMs)
        {
            softJamAccumulatedMs = config.softJamTimeMs;
        }
    }
    else
    {
        // Recovery: flow improved significantly
        if (passRatio >= config.ratioThreshold * 0.85f || !extrudingNow)
        {
            softJamAccumulatedMs = 0;
        }
    }

    if (config.softJamTimeMs > 0)
    {
        state.softJamPercent =
            (100.0f * static_cast<float>(softJamAccumulatedMs) /
             static_cast<float>(config.softJamTimeMs));
    }
    else
    {
        state.softJamPercent = softCondition ? 100.0f : 0.0f;
    }

    return (softJamAccumulatedMs >= config.softJamTimeMs);
}

JamState JamDetector::update(float         expectedDistance,
                             float         actualDistance,
                             unsigned long movementPulseCount,
                             bool          isPrinting,
                             bool          hasTelemetry,
                             unsigned long currentTimeMs,
                             unsigned long printStartTimeMs,
                             const JamConfig& config,
                             float         windowedExpectedRateMmPerSec,
                             float         windowedActualRateMmPerSec)
{
    // If not printing or no telemetry, reset to idle-ish state
    if (!isPrinting || !hasTelemetry)
    {
        if (state.graceState != GraceState::IDLE)
        {
            state.graceState       = GraceState::IDLE;
            state.graceActive      = false;
            state.jammed           = false;
            state.hardJamTriggered = false;
            state.softJamTriggered = false;
            hardJamAccumulatedMs   = 0;
            softJamAccumulatedMs   = 0;
        }

        lastEvalMs               = currentTimeMs;
        prevExpectedDistance     = expectedDistance;
        prevActualDistance       = actualDistance;
        state.expectedRateMmPerSec = 0.0f;
        state.actualRateMmPerSec   = 0.0f;
        wasInGrace               = false;
        return state;
    }

    // Calculate elapsed time since last evaluation
    unsigned long elapsedMs;
    if (lastEvalMs == 0)
    {
        elapsedMs = DEFAULT_EVAL_INTERVAL_MS;
    }
    else
    {
        elapsedMs = currentTimeMs - lastEvalMs;
        if (elapsedMs > MAX_EVAL_INTERVAL_MS)
        {
            elapsedMs = MAX_EVAL_INTERVAL_MS;
        }
        if (elapsedMs == 0)
        {
            elapsedMs = 1;  // avoid division by zero
        }
    }
    lastEvalMs = currentTimeMs;

    float expectedRate = 0.0f;
    float actualRate   = 0.0f;

    if constexpr (!USE_WINDOWED_RATE_SAMPLES)
    {
        // First derivative: compute rates from windowed distances
        float dtSec = static_cast<float>(elapsedMs) / 1000.0f;
        float dExp  = expectedDistance - prevExpectedDistance;
        float dAct  = actualDistance   - prevActualDistance;

        // Handle retractions / window resets: treat negative deltas as zero flow
        if (dExp < 0.0f) dExp = 0.0f;
        if (dAct < 0.0f) dAct = 0.0f;

        expectedRate = (dtSec > 0.0f) ? (dExp / dtSec) : 0.0f;  // mm/s
        actualRate   = (dtSec > 0.0f) ? (dAct / dtSec) : 0.0f;  // mm/s

        prevExpectedDistance = expectedDistance;
        prevActualDistance   = actualDistance;
    }
    else
    {
        expectedRate        = windowedExpectedRateMmPerSec;
        actualRate          = windowedActualRateMmPerSec;
        prevExpectedDistance = expectedDistance;
        prevActualDistance   = actualDistance;
    }

    // Expose rates for callers / logging
    state.expectedRateMmPerSec = expectedRate;
    state.actualRateMmPerSec   = actualRate;

    // Rate-based pass ratio
    float passRatio;
    if (expectedRate > MIN_RATE_FOR_RATIO_MM_S)
    {
        // expectedRate is guaranteed > 0 here
        passRatio = actualRate / expectedRate;
    }
    else
    {
        // When flow is tiny, treat as OK to avoid noise on drip moves
        passRatio = 1.0f;
    }

    if (passRatio < 0.0f) passRatio = 0.0f;
    if (passRatio > 1.5f) passRatio = 1.5f;

    // Distance-based deficit (still useful for UI + soft jam gating)
    float deficit = expectedDistance - actualDistance;
    if (deficit < 0.0f) deficit = 0.0f;

    // For UI: smooth a deficit ratio (distance-based) so the graph is not jumpy
    float deficitRatioValue =
        (expectedDistance > 1.0f) ? (deficit / expectedDistance) : 0.0f;
    smoothedDeficitRatio =
        RATIO_SMOOTHING_ALPHA * deficitRatioValue +
        (1.0f - RATIO_SMOOTHING_ALPHA) * smoothedDeficitRatio;

    // Update state metrics exposed externally
    state.passRatio = passRatio;   // rate-based
    state.deficit   = deficit;     // windowed (distance-based)

    // Initialize grace state at print start if needed
    if (state.graceState == GraceState::IDLE)
    {
        state.graceState  = GraceState::START_GRACE;
        state.graceActive = true;
    }

    // Evaluate grace; if active, suppress jam accumulation completely
    bool graceActive = evaluateGraceState(currentTimeMs,
                                          printStartTimeMs,
                                          expectedDistance,
                                          movementPulseCount,
                                          config);

    if (graceActive)
    {
        hardJamAccumulatedMs   = 0;
        softJamAccumulatedMs   = 0;
        state.hardJamPercent   = 0.0f;
        state.softJamPercent   = 0.0f;
        state.jammed           = false;
        state.hardJamTriggered = false;
        state.softJamTriggered = false;

        wasInGrace = true;
        return state;
    }

    // At this point, we are fully active
    bool allowHard = (config.detectionMode != DetectionMode::SOFT_ONLY);
    bool allowSoft = (config.detectionMode != DetectionMode::HARD_ONLY);

    if (allowHard)
    {
        state.hardJamTriggered =
            evaluateHardJam(expectedDistance,
                            passRatio,
                            expectedRate,
                            actualRate,
                            elapsedMs,
                            config);
    }
    else
    {
        hardJamAccumulatedMs   = 0;
        state.hardJamPercent   = 0.0f;
        state.hardJamTriggered = false;
    }

    if (allowSoft)
    {
        state.softJamTriggered =
            evaluateSoftJam(expectedDistance,
                            deficit,
                            passRatio,
                            expectedRate,
                            actualRate,
                            elapsedMs,
                            config);
    }
    else
    {
        softJamAccumulatedMs   = 0;
        state.softJamPercent   = 0.0f;
        state.softJamTriggered = false;
    }

    bool wasJammed = state.jammed;
    state.jammed   = state.hardJamTriggered || state.softJamTriggered;

    // Logging on jam transitions (kept conservative to avoid spam)
    if (state.jammed && !wasJammed && settingsManager.getVerboseLogging())
    {
        const char* jamType = "soft";
        if (state.hardJamTriggered && state.softJamTriggered)
        {
            jamType = "hard+soft";
        }
        else if (state.hardJamTriggered)
        {
            jamType = "hard";
        }

        logger.logf(
            "Filament jam detected (%s)! "
            "win_exp=%.2f win_sns=%.2f deficit=%.2f "
            "rate_exp=%.3f rate_sns=%.3f pass=%.2f",
            jamType,
            expectedDistance,
            actualDistance,
            deficit,
            expectedRate,
            actualRate,
            passRatio);
    }
    else if (!state.jammed && wasJammed && !jamPauseRequested)
    {
        logger.log("Filament flow resumed");
    }

    // If jam is latched, reflect that in graceState
    if (state.jammed)
    {
        state.graceState = GraceState::JAMMED;
    }
    else if (state.graceState == GraceState::JAMMED)
    {
        // Recovery from jam -> back to ACTIVE
        state.graceState = GraceState::ACTIVE;
    }

    wasInGrace = false;
    return state;
}
