#include "JamDetector.h"
#include "Logger.h"
#include "SettingsManager.h"

JamDetector::JamDetector() {
    state.jammed = false;
    state.hardJamTriggered = false;
    state.softJamTriggered = false;
    state.hardJamPercent = 0.0f;
    state.softJamPercent = 0.0f;
    state.passRatio = 1.0f;
    state.deficit = 0.0f;
    state.graceState = GraceState::IDLE;
    state.graceActive = false;

    hardJamAccumulatedMs = 0;
    softJamAccumulatedMs = 0;
    lastEvalMs = 0;
    lastPulseCount = 0;
    resumeGracePulseBaseline = 0;
    resumeGraceActualBaseline = 0.0f;
    jamPauseRequested = false;
    wasInGrace = false;
    smoothedDeficitRatio = 0.0f;
}

void JamDetector::reset(unsigned long currentTimeMs) {
    state.jammed = false;
    state.hardJamTriggered = false;
    state.softJamTriggered = false;
    state.hardJamPercent = 0.0f;
    state.softJamPercent = 0.0f;
    state.passRatio = 1.0f;
    state.deficit = 0.0f;
    state.graceState = GraceState::START_GRACE;
    state.graceActive = true;

    hardJamAccumulatedMs = 0;
    softJamAccumulatedMs = 0;
    lastEvalMs = currentTimeMs;
    lastPulseCount = 0;
    jamPauseRequested = false;
    wasInGrace = true;
    smoothedDeficitRatio = 0.0f;
}

void JamDetector::onResume(unsigned long currentTimeMs, unsigned long currentPulseCount, float currentActualMm) {
    state.graceState = GraceState::RESUME_GRACE;
    state.graceActive = true;
    resumeGracePulseBaseline = currentPulseCount;
    resumeGraceActualBaseline = currentActualMm;
    jamPauseRequested = false;

    // Reset jam accumulators on resume
    hardJamAccumulatedMs = 0;
    softJamAccumulatedMs = 0;
    state.hardJamPercent = 0.0f;
    state.softJamPercent = 0.0f;

    lastEvalMs = currentTimeMs;
}

bool JamDetector::evaluateGraceState(unsigned long currentTimeMs, unsigned long printStartTimeMs,
                                     float expectedDistance, unsigned long movementPulseCount,
                                     const JamConfig& config) {
    // Handle different grace states
    switch (state.graceState) {
        case GraceState::IDLE:
            return false;  // No grace when idle

        case GraceState::START_GRACE: {
            // Time-based grace after print start
            unsigned long timeSinceStart = currentTimeMs - printStartTimeMs;
            if (timeSinceStart < config.startTimeoutMs) {
                return true;  // Still in startup timeout
            }
            if (timeSinceStart < config.graceTimeMs) {
                return true;  // Still in grace period
            }
            // Grace expired, transition to ACTIVE
            state.graceState = GraceState::ACTIVE;
            return false;
        }

        case GraceState::RESUME_GRACE: {
            // Wait for movement after resume
            bool pulsesSeen = (movementPulseCount > resumeGracePulseBaseline);
            bool expectedBuilt = (expectedDistance >= RESUME_GRACE_MIN_MOVEMENT_MM);

            if (pulsesSeen || expectedBuilt) {
                // Movement detected, clear resume grace
                state.graceState = GraceState::ACTIVE;
                return false;
            }
            return true;  // Still in resume grace
        }

        case GraceState::ACTIVE:
            return false;  // No grace, actively detecting

        case GraceState::JAMMED:
            return false;  // Already jammed, no grace
    }

    return false;
}

bool JamDetector::evaluateHardJam(float expectedDistance, float passRatio,
                                  bool newPulseSinceLastEval, unsigned long elapsedMs,
                                  const JamConfig& config) {
    bool hardCondition = (expectedDistance >= MIN_HARD_WINDOW_MM) &&
                        (passRatio < HARD_PASS_THRESHOLD);

    if (hardCondition) {
        // Accumulate hard jam time
        hardJamAccumulatedMs += elapsedMs;
        if (hardJamAccumulatedMs > config.hardJamTimeMs) {
            hardJamAccumulatedMs = config.hardJamTimeMs;
        }
    } else if (passRatio >= HARD_RECOVERY_RATIO ||
               expectedDistance < (MIN_HARD_WINDOW_MM * 0.5f)) {
        // Recovery: good flow or low expected distance
        hardJamAccumulatedMs = 0;
    }

    // Calculate percentage
    if (config.hardJamTimeMs > 0) {
        state.hardJamPercent = (100.0f * (float)hardJamAccumulatedMs) / (float)config.hardJamTimeMs;
        if (state.hardJamPercent > 100.0f) state.hardJamPercent = 100.0f;
    } else {
        state.hardJamPercent = 0.0f;
    }

    // Trigger if accumulated time exceeds threshold
    return (hardJamAccumulatedMs >= config.hardJamTimeMs);
}

bool JamDetector::evaluateSoftJam(float expectedDistance, float deficit, float passRatio,
                                  unsigned long elapsedMs, const JamConfig& config) {
    bool softCondition = (expectedDistance >= MIN_SOFT_WINDOW_MM) &&
                        (deficit >= MIN_SOFT_DEFICIT_MM) &&
                        (passRatio < config.ratioThreshold);

    if (softCondition) {
        // Accumulate soft jam time
        softJamAccumulatedMs += elapsedMs;
        if (softJamAccumulatedMs > config.softJamTimeMs) {
            softJamAccumulatedMs = config.softJamTimeMs;
        }
    } else if (passRatio >= config.ratioThreshold * 0.85f) {
        // Recovery: flow improved to 85% of threshold
        softJamAccumulatedMs = 0;
    }

    // Calculate percentage
    if (config.softJamTimeMs > 0) {
        state.softJamPercent = (100.0f * (float)softJamAccumulatedMs) / (float)config.softJamTimeMs;
        if (state.softJamPercent > 100.0f) state.softJamPercent = 100.0f;
    } else {
        state.softJamPercent = 0.0f;
    }

    // Trigger if accumulated time exceeds threshold
    return (softJamAccumulatedMs >= config.softJamTimeMs);
}

JamState JamDetector::update(float expectedDistance, float actualDistance,
                             unsigned long movementPulseCount, bool isPrinting,
                             bool hasTelemetry, unsigned long currentTimeMs,
                             unsigned long printStartTimeMs, const JamConfig& config) {
    // If not printing or no telemetry, reset to IDLE
    if (!isPrinting || !hasTelemetry) {
        if (state.graceState != GraceState::IDLE) {
            state.graceState = GraceState::IDLE;
            state.graceActive = false;
            state.jammed = false;
            hardJamAccumulatedMs = 0;
            softJamAccumulatedMs = 0;
        }
        return state;
    }

    // Calculate elapsed time since last evaluation
    unsigned long elapsedMs;
    if (lastEvalMs == 0) {
        elapsedMs = 1000;  // First evaluation, use 1 second
    } else {
        elapsedMs = currentTimeMs - lastEvalMs;
        if (elapsedMs > 1000) {
            elapsedMs = 1000;  // Cap at 1 second
        }
        if (elapsedMs == 0) {
            elapsedMs = 1;  // Minimum 1ms
        }
    }
    lastEvalMs = currentTimeMs;

    // Calculate metrics
    float deficit = expectedDistance - actualDistance;
    if (deficit < 0.0f) deficit = 0.0f;

    float passRatio = (expectedDistance > 0.0f) ? (actualDistance / expectedDistance) : 1.0f;
    if (passRatio < 0.0f) passRatio = 0.0f;

    float deficitRatioValue = (expectedDistance > 1.0f) ? (deficit / expectedDistance) : 0.0f;

    // Apply EWMA smoothing to deficit ratio for display
    smoothedDeficitRatio = RATIO_SMOOTHING_ALPHA * deficitRatioValue +
                          (1.0f - RATIO_SMOOTHING_ALPHA) * smoothedDeficitRatio;

    // Update state metrics
    state.passRatio = passRatio;
    state.deficit = deficit;

    // Check if we're in grace period
    bool graceActive = evaluateGraceState(currentTimeMs, printStartTimeMs,
                                         expectedDistance, movementPulseCount, config);
    state.graceActive = graceActive;

    // Log grace period transitions
    if (graceActive != wasInGrace && settingsManager.getVerboseLogging()) {
        if (graceActive) {
            logger.logf("Grace period ACTIVE (state=%d)", (int)state.graceState);
        } else {
            logger.log("Grace period CLEARED - jam detection ENABLED");
        }
    }
    wasInGrace = graceActive;

    // Reset accumulators during grace period
    if (graceActive) {
        hardJamAccumulatedMs = 0;
        softJamAccumulatedMs = 0;
        state.hardJamPercent = 0.0f;
        state.softJamPercent = 0.0f;
        state.jammed = false;
        state.hardJamTriggered = false;
        state.softJamTriggered = false;
        return state;
    }

    // Detect new pulses since last evaluation
    bool newPulseSinceLastEval = (movementPulseCount != lastPulseCount);
    lastPulseCount = movementPulseCount;

    // Evaluate jam conditions (only when not in grace)
    state.hardJamTriggered = evaluateHardJam(expectedDistance, passRatio,
                                            newPulseSinceLastEval, elapsedMs, config);
    state.softJamTriggered = evaluateSoftJam(expectedDistance, deficit, passRatio,
                                            elapsedMs, config);

    // Update jammed state
    bool wasJammed = state.jammed;
    state.jammed = state.hardJamTriggered || state.softJamTriggered;

    // Log jam state changes
    if (state.jammed && !wasJammed && settingsManager.getVerboseLogging()) {
        const char* jamType = "soft";
        if (state.hardJamTriggered && state.softJamTriggered) {
            jamType = "hard+soft";
        } else if (state.hardJamTriggered) {
            jamType = "hard";
        }
        logger.logf("Filament jam detected (%s)! Expected %.2fmm, sensor %.2fmm, deficit %.2fmm ratio=%.2f",
                   jamType, expectedDistance, actualDistance, deficit, deficitRatioValue);
    } else if (!state.jammed && wasJammed && !jamPauseRequested) {
        logger.log("Filament flow resumed");
    }

    // Transition to JAMMED state if needed
    if (state.jammed && state.graceState == GraceState::ACTIVE) {
        state.graceState = GraceState::JAMMED;
    }

    return state;
}
