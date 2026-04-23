#include "sleep_analyzer.h"

const float SleepAnalyzer::SLEEPINESS_THRESHOLD = 0.6f;
const float SleepAnalyzer::BASELINE_MOVEMENT_THRESHOLD = 0.2f;
const float SleepAnalyzer::BASELINE_HR_STABILITY_THRESHOLD = 5.0f;
const float SleepAnalyzer::BASELINE_RR_STABILITY_THRESHOLD = 2.0f;
const float SleepAnalyzer::EMA_ALPHA = 0.2f;
const float SleepAnalyzer::CONFIDENCE_MARGIN = 0.1f;
const float SleepAnalyzer::BASELINE_BETA = 0.01f;
const float SleepAnalyzer::HYSTERESIS_ENTER_DEEP = 0.7f;
const float SleepAnalyzer::HYSTERESIS_EXIT_DEEP = 0.5f;
const float SleepAnalyzer::HYSTERESIS_ENTER_REM = 0.6f;
const float SleepAnalyzer::HYSTERESIS_EXIT_REM = 0.4f;

SleepAnalyzer::SleepAnalyzer() {
    reset();
}

SleepAnalyzer::~SleepAnalyzer() {
}

void SleepAnalyzer::reset() {
    currentState = SLEEP_NO_PERSON;
    pendingState = SLEEP_NO_PERSON;
    memset(&stats, 0, sizeof(SleepStatistics));
    memset(&score, 0, sizeof(SleepScore));
    memset(&cycle, 0, sizeof(SleepCycle));

    stateEnterTime = millis();
    pendingStateTime = 0;
    noPersonTimer = 0;
    outOfBedTimer = 0;
    sleepinessDuration = 0;
    awakeDuration = 0;
    deepSleepDuration = 0;
    lightSleepDuration = 0;
    remSleepDuration = 0;
    movementHighDuration = 0;
    gettingUpDuration = 0;
    deepStableDuration = 0;

    baselineHR = 70.0f;
    baselineRR = 16.0f;
    baselineCalibrated = false;
    baselineSampleCount = 0;
    baselineHRSum = 0;
    baselineRRSum = 0;
    lastBaselineHR = 0;
    lastBaselineRR = 0;
    hrStabilitySum = 0;
    rrStabilitySum = 0;
    stabilitySampleCount = 0;

    currentSleepiness = 0;
    currentDeepScore = 0;
    currentLightScore = 0;
    currentAwakeScore = 0;
    currentRemScore = 0;

    lastRRValue = 0;
    wasAsleep = false;
}

PresenceData SleepAnalyzer::evaluatePresence() {
    PresenceData p;
    memset(&p, 0, sizeof(PresenceData));

    if (sensorData.heart_valid || sensorData.breath_valid) {
        p.isPresent = true;
        p.confidence = 0.9f;
    }

    if (sensorData.presence == 1) {
        p.isPresent = true;
        p.confidence = max(p.confidence, 0.7f);
    }

    if (sensorData.distance > 0) {
        p.distance = sensorData.distance;
        if (p.distance > 20 && p.distance < 100) {
            p.isPresent = true;
        }
    } else {
        p.distance = -1;
    }

    p.motionEnergy = sensorData.body_movement / 100.0f;

    return p;
}

float SleepAnalyzer::sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

float SleepAnalyzer::emaSmooth(float input, float last, float alpha) {
    return alpha * input + (1.0f - alpha) * last;
}

float SleepAnalyzer::updateBaseline(float current, float input, float beta) {
    return (1.0f - beta) * current + beta * input;
}

bool SleepAnalyzer::isBestScore(float score, float s2, float s3, float s4, float margin) {
    return (score > s2 + margin && score > s3 + margin && score > s4 + margin);
}

float SleepAnalyzer::normalizeHR(float hr) {
    if (!baselineCalibrated) return 0.0f;
    float norm = (hr - baselineHR) / 20.0f;
    return constrain_value(norm, -1.0f, 1.0f);
}

float SleepAnalyzer::normalizeRR(float rr) {
    if (!baselineCalibrated) return 0.0f;
    float norm = (rr - baselineRR) / 4.0f;
    return constrain_value(norm, -1.0f, 1.0f);
}

float SleepAnalyzer::normalizeHRV(float hrv) {
    float norm = hrv / 50.0f;
    return constrain_value(norm, 0.0f, 1.0f);
}

float SleepAnalyzer::normalizeMovement(float movement) {
    return constrain_value(movement / 100.0f, 0.0f, 1.0f);
}

void SleepAnalyzer::calibrateBaseline(const HeartRateData& hrData,
                                       const RespirationData& rrData,
                                       const BodyMovementData& movementData) {
    if (!hrData.isValid || !rrData.isValid) return;
    if (currentState != SLEEP_AWAKE && currentState != SLEEP_IN_BED) return;

    if (movementData.isValid && normalizeMovement(movementData.movement) > BASELINE_MOVEMENT_THRESHOLD) {
        return;
    }

    if (baselineCalibrated) {
        float hrDiff = fabs(hrData.bpmSmoothed - lastBaselineHR);
        float rrDiff = fabs(rrData.rateSmoothed - lastBaselineRR);
        hrStabilitySum += hrDiff;
        rrStabilitySum += rrDiff;
        stabilitySampleCount++;

        if (stabilitySampleCount >= 5) {
            float avgHrDiff = hrStabilitySum / stabilitySampleCount;
            float avgRrDiff = rrStabilitySum / stabilitySampleCount;
            if (avgHrDiff > BASELINE_HR_STABILITY_THRESHOLD ||
                avgRrDiff > BASELINE_RR_STABILITY_THRESHOLD) {
                hrStabilitySum = 0;
                rrStabilitySum = 0;
                stabilitySampleCount = 0;
                return;
            }
            hrStabilitySum = 0;
            rrStabilitySum = 0;
            stabilitySampleCount = 0;
        }

        baselineHR = updateBaseline(baselineHR, hrData.bpmSmoothed, BASELINE_BETA);
        baselineRR = updateBaseline(baselineRR, rrData.rateSmoothed, BASELINE_BETA);
        lastBaselineHR = hrData.bpmSmoothed;
        lastBaselineRR = rrData.rateSmoothed;
        return;
    }

    baselineHRSum += hrData.bpmSmoothed;
    baselineRRSum += rrData.rateSmoothed;
    baselineSampleCount++;

    if (baselineSampleCount >= 30) {
        lastBaselineHR = baselineHR;
        lastBaselineRR = baselineRR;
        baselineHR = baselineHRSum / baselineSampleCount;
        baselineRR = baselineRRSum / baselineSampleCount;
        baselineCalibrated = true;
        baselineHRSum = 0;
        baselineRRSum = 0;
        baselineSampleCount = 0;
        Serial.printf("📐 睡眠基线校准完成: HR=%.1f, RR=%.1f\n", baselineHR, baselineRR);
    }
}

float SleepAnalyzer::calculateSleepinessScore(const HeartRateData& hrData,
                                               const RespirationData& rrData,
                                               const HRVEstimate& hrvData,
                                               const BodyMovementData& movementData) {
    float hrSleepFactor = 0.5f, hrvNorm = 0, rrStable = 0, moveNorm = 0;

    if (hrData.isValid) {
        float hrNorm = normalizeHR(hrData.bpmSmoothed);
        hrSleepFactor = (1.0f - hrNorm) * 0.5f;
    }
    if (hrvData.isValid) {
        hrvNorm = normalizeHRV(hrvData.rmssd);
    }
    if (rrData.isValid) {
        rrStable = 1.0f - constrain_value(rrData.variability / 5.0f, 0.0f, 1.0f);
    }
    if (movementData.isValid) {
        moveNorm = normalizeMovement(movementData.movement);
    }

    float x = 3.0f * (hrSleepFactor - 0.5f)
            + 2.5f * (hrvNorm - 0.5f)
            + 2.0f * (rrStable - 0.5f)
            + 2.5f * (0.5f - moveNorm);

    return sigmoid(x);
}

float SleepAnalyzer::calculateDeepSleepScore(const HeartRateData& hrData,
                                              const RespirationData& rrData,
                                              const HRVEstimate& hrvData,
                                              const BodyMovementData& movementData) {
    if (movementData.isValid && movementData.movement > DEEP_SLEEP_HARD_MOVEMENT_LIMIT) {
        return 0.0f;
    }

    float hrSleepFactor = 0.5f, hrvNorm = 0, moveNorm = 0;

    if (hrData.isValid) {
        float hrNorm = normalizeHR(hrData.bpmSmoothed);
        hrSleepFactor = (1.0f - hrNorm) * 0.5f;
    }
    if (hrvData.isValid) {
        hrvNorm = normalizeHRV(hrvData.rmssd);
    }
    if (movementData.isValid) {
        moveNorm = normalizeMovement(movementData.movement);
    }

    float x = 4.0f * (hrSleepFactor - 0.5f)
            + 3.0f * (hrvNorm - 0.5f)
            + 2.0f * (0.5f - moveNorm);

    return sigmoid(x);
}

float SleepAnalyzer::calculateLightSleepScore(const HeartRateData& hrData,
                                               const RespirationData& rrData,
                                               const HRVEstimate& hrvData,
                                               const BodyMovementData& movementData) {
    float hrMid = 0, hrvMid = 0, moveMid = 0, rrStable = 0;

    if (hrData.isValid) {
        float hrNorm = normalizeHR(hrData.bpmSmoothed);
        float hrSleepFactor = (1.0f - hrNorm) * 0.5f;
        hrMid = 1.0f - fabs(hrSleepFactor - 0.5f) * 2.0f;
    }
    if (hrvData.isValid) {
        float hrvNorm = normalizeHRV(hrvData.rmssd);
        hrvMid = 1.0f - fabs(hrvNorm - 0.5f) * 2.0f;
    }
    if (movementData.isValid) {
        float moveNorm = normalizeMovement(movementData.movement);
        if (moveNorm >= 0.1f && moveNorm <= 0.4f) {
            moveMid = 1.0f - fabs(moveNorm - 0.25f) * 4.0f;
        }
    }
    if (rrData.isValid) {
        rrStable = rrData.regularity;
    }

    float light = 0.3f * hrMid
                + 0.3f * hrvMid
                + 0.2f * moveMid
                + 0.2f * rrStable;

    return constrain_value(light, 0.0f, 1.0f);
}

float SleepAnalyzer::calculateAwakeScore(const HeartRateData& hrData,
                                          const RespirationData& rrData,
                                          const HRVEstimate& hrvData,
                                          const BodyMovementData& movementData) {
    float moveNorm = 0, hrAwakeFactor = 0.5f, rrVar = 0;

    if (movementData.isValid) {
        moveNorm = normalizeMovement(movementData.movement);
    }
    if (hrData.isValid) {
        float hrNorm = normalizeHR(hrData.bpmSmoothed);
        hrAwakeFactor = (hrNorm + 1.0f) * 0.5f;
    }
    if (rrData.isValid) {
        rrVar = constrain_value(rrData.variability / 5.0f, 0.0f, 1.0f);
    }

    float x = 3.0f * (moveNorm - 0.3f)
            + 2.0f * (hrAwakeFactor - 0.5f)
            + 1.5f * (rrVar - 0.3f);

    return sigmoid(x);
}

float SleepAnalyzer::calculateRemScore(const HeartRateData& hrData,
                                        const RespirationData& rrData,
                                        const HRVEstimate& hrvData,
                                        const BodyMovementData& movementData) {
    float hrHigh = 0, hrvLow = 0, rrUnstable = 0, moveLow = 0, rrChange = 0;

    if (hrData.isValid) {
        float hrNorm = normalizeHR(hrData.bpmSmoothed);
        hrHigh = constrain_value(hrNorm, 0.0f, 1.0f);
    }
    if (hrvData.isValid) {
        float hrvNorm = normalizeHRV(hrvData.rmssd);
        hrvLow = 1.0f - hrvNorm;
    }
    if (rrData.isValid) {
        rrUnstable = constrain_value(rrData.variability / 5.0f, 0.0f, 1.0f);
        if (lastRRValue > 0) {
            float rrDiff = fabs(rrData.rateSmoothed - lastRRValue);
            rrChange = constrain_value(rrDiff / 3.0f, 0.0f, 1.0f);
        }
        lastRRValue = rrData.rateSmoothed;
    }
    if (movementData.isValid) {
        float moveNorm = normalizeMovement(movementData.movement);
        moveLow = 1.0f - moveNorm;
    }

    if (movementData.isValid && movementData.movement > DEEP_SLEEP_HARD_MOVEMENT_LIMIT) {
        return 0.0f;
    }

    float x = 2.5f * (hrHigh - 0.3f)
            + 2.0f * (hrvLow - 0.3f)
            + 1.5f * (rrUnstable - 0.3f)
            + 2.0f * (rrChange - 0.3f)
            + 2.0f * (moveLow - 0.5f);

    return sigmoid(x);
}

bool SleepAnalyzer::tryTransitionTo(SleepState target, unsigned long confirmMs) {
    if (pendingState != target) {
        pendingState = target;
        pendingStateTime = millis();
        return false;
    }
    if (millis() - pendingStateTime >= confirmMs) {
        currentState = target;
        stateEnterTime = millis();
        pendingState = target;
        return true;
    }
    return false;
}

void SleepAnalyzer::updateSleepCycle() {
    if (currentState == SLEEP_DEEP_SLEEP && !cycle.inDeepPhase) {
        cycle.inDeepPhase = true;
        if (cycle.cycleStartTime == 0) {
            cycle.cycleStartTime = millis();
        }
    }

    if (currentState == SLEEP_REM_SLEEP && !cycle.inRemPhase) {
        cycle.inRemPhase = true;
    }

    if (currentState == SLEEP_LIGHT_SLEEP && cycle.inDeepPhase) {
        cycle.inDeepPhase = false;
        cycle.lastDeepEndTime = millis();
    }

    if (currentState == SLEEP_LIGHT_SLEEP && cycle.inRemPhase) {
        cycle.inRemPhase = false;
        cycle.lastRemEndTime = millis();
    }

    if ((currentState == SLEEP_AWAKE || currentState == SLEEP_OUT_OF_BED) &&
        (cycle.inDeepPhase || cycle.inRemPhase) &&
        cycle.cycleStartTime > 0) {
        cycle.cycleCount++;
        cycle.inDeepPhase = false;
        cycle.inRemPhase = false;
        cycle.cycleStartTime = millis();
        stats.sleepCycles = cycle.cycleCount;
        Serial.printf("🔄 完成第 %d 个睡眠周期\n", cycle.cycleCount);
    }
}

void SleepAnalyzer::updateState(PresenceData& presence,
                                 const HeartRateData& hrData,
                                 const RespirationData& rrData,
                                 const HRVEstimate& hrvData,
                                 const BodyMovementData& movementData) {
    unsigned long now = millis();

    if (currentState != SLEEP_NO_PERSON && currentState != SLEEP_SESSION_END &&
        (now - stateEnterTime) < MIN_STATE_DWELL_MS) {
        return;
    }

    switch (currentState) {
        case SLEEP_NO_PERSON:
            if (presence.isPresent) {
                noPersonTimer = 0;
                if (presence.distance > 0 && presence.distance > 80) {
                    currentState = SLEEP_OUT_OF_BED;
                } else {
                    currentState = SLEEP_IN_BED;
                }
                stateEnterTime = now;
                pendingState = currentState;
                Serial.printf("🔄 状态切换: 无人 → %s\n", SLEEP_STATE_NAMES[currentState]);
            }
            break;

        case SLEEP_IN_BED:
            if (!presence.isPresent) {
                noPersonTimer += 1000;
                bool hrExists = hrData.isValid && hrData.bpmSmoothed > 0;
                if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && !hrExists) {
                    currentState = SLEEP_SESSION_END;
                    stateEnterTime = now;
                    pendingState = currentState;
                    if (wasAsleep) {
                        calculateSleepScore();
                    }
                    Serial.println("🔄 状态切换: 在床 → 会话结束");
                } else if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && hrExists) {
                    Serial.println("⚠️ 检测到无人但HR存在，可能遮挡，暂不结束会话");
                } else if (noPersonTimer > OUT_OF_BED_SECONDS * 1000) {
                    currentState = SLEEP_OUT_OF_BED;
                    stateEnterTime = now;
                    pendingState = currentState;
                    Serial.println("🔄 状态切换: 在床 → 离床");
                }
            } else {
                noPersonTimer = 0;
                float rawSleepiness = calculateSleepinessScore(hrData, rrData, hrvData, movementData);
                currentSleepiness = emaSmooth(rawSleepiness, currentSleepiness, EMA_ALPHA);
                float movement = movementData.isValid ? movementData.movement : 100;

                if (currentSleepiness > SLEEPINESS_THRESHOLD &&
                    movement < SLEEPINESS_MOVEMENT_THRESHOLD) {
                    sleepinessDuration += 1000;
                    if (sleepinessDuration >= SLEEPINESS_MIN_SECONDS * 1000) {
                        currentState = SLEEP_LIGHT_SLEEP;
                        stateEnterTime = now;
                        pendingState = currentState;
                        stats.sleepStartTime = now;
                        stats.sleepLatency = (now - stats.sessionStartTime) / 1000;
                        wasAsleep = true;
                        cycle.cycleStartTime = now;
                        Serial.printf("🔄 状态切换: 在床 → 浅睡 (入睡耗时:%lus)\n", stats.sleepLatency);
                    }
                } else {
                    sleepinessDuration = 0;
                    if (movement >= MOVEMENT_HIGH_THRESHOLD) {
                        currentState = SLEEP_AWAKE;
                        stateEnterTime = now;
                        pendingState = currentState;
                        stats.sessionStartTime = now;
                        Serial.println("🔄 状态切换: 在床 → 清醒");
                    }
                }
            }
            break;

        case SLEEP_AWAKE:
            if (!presence.isPresent) {
                noPersonTimer += 1000;
                bool hrExists = hrData.isValid && hrData.bpmSmoothed > 0;
                if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && !hrExists) {
                    currentState = SLEEP_SESSION_END;
                    stateEnterTime = now;
                    pendingState = currentState;
                    if (wasAsleep) {
                        calculateSleepScore();
                    }
                    Serial.println("🔄 状态切换: 清醒 → 会话结束");
                } else if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && hrExists) {
                    Serial.println("⚠️ 检测到无人但HR存在，可能遮挡，暂不结束会话");
                } else if (noPersonTimer > OUT_OF_BED_SECONDS * 1000) {
                    currentState = SLEEP_OUT_OF_BED;
                    stateEnterTime = now;
                    pendingState = currentState;
                    Serial.println("🔄 状态切换: 清醒 → 离床");
                }
            } else {
                noPersonTimer = 0;
                float rawSleepiness = calculateSleepinessScore(hrData, rrData, hrvData, movementData);
                currentSleepiness = emaSmooth(rawSleepiness, currentSleepiness, EMA_ALPHA);
                float movement = movementData.isValid ? movementData.movement : 100;

                if (wasAsleep && movement >= GETTING_UP_MOVEMENT_THRESHOLD) {
                    gettingUpDuration += 1000;
                    if (gettingUpDuration >= GETTING_UP_MIN_SECONDS * 1000) {
                        currentState = SLEEP_GETTING_UP;
                        stateEnterTime = now;
                        pendingState = currentState;
                        gettingUpDuration = 0;
                        Serial.println("🔄 状态切换: 清醒 → 起床");
                        break;
                    }
                } else {
                    gettingUpDuration = 0;
                }

                if (currentSleepiness > SLEEPINESS_THRESHOLD &&
                    movement < SLEEPINESS_MOVEMENT_THRESHOLD) {
                    sleepinessDuration += 1000;
                    if (sleepinessDuration >= SLEEPINESS_MIN_SECONDS * 1000) {
                        currentState = SLEEP_LIGHT_SLEEP;
                        stateEnterTime = now;
                        pendingState = currentState;
                        stats.sleepStartTime = now;
                        stats.sleepLatency = (now - stats.sessionStartTime) / 1000;
                        wasAsleep = true;
                        cycle.cycleStartTime = now;
                        Serial.printf("🔄 状态切换: 清醒 → 浅睡 (入睡耗时:%lus)\n", stats.sleepLatency);
                    }
                } else {
                    sleepinessDuration = 0;
                }
            }
            break;

        case SLEEP_LIGHT_SLEEP:
            if (!presence.isPresent) {
                noPersonTimer += 1000;
                if (noPersonTimer > OUT_OF_BED_SECONDS * 1000) {
                    currentState = SLEEP_OUT_OF_BED;
                    stateEnterTime = now;
                    pendingState = currentState;
                    stats.wakeCount++;
                    Serial.println("🔄 状态切换: 浅睡 → 离床");
                }
            } else {
                noPersonTimer = 0;

                if (movementData.isValid && movementData.movement > FAST_AWAKE_MOVEMENT_THRESHOLD) {
                    currentState = SLEEP_AWAKE;
                    stateEnterTime = now;
                    pendingState = SLEEP_AWAKE;
                    stats.wakeCount++;
                    awakeDuration = 0;
                    deepSleepDuration = 0;
                    deepStableDuration = 0;
                    Serial.println("🔄 状态切换: 浅睡 → 清醒 (快速触发:体动大)");
                    break;
                }

                float rawDeep = calculateDeepSleepScore(hrData, rrData, hrvData, movementData);
                float rawLight = calculateLightSleepScore(hrData, rrData, hrvData, movementData);
                float rawAwake = calculateAwakeScore(hrData, rrData, hrvData, movementData);
                float rawRem = calculateRemScore(hrData, rrData, hrvData, movementData);

                currentDeepScore = emaSmooth(rawDeep, currentDeepScore, EMA_ALPHA);
                currentLightScore = emaSmooth(rawLight, currentLightScore, EMA_ALPHA);
                currentAwakeScore = emaSmooth(rawAwake, currentAwakeScore, EMA_ALPHA);
                currentRemScore = emaSmooth(rawRem, currentRemScore, EMA_ALPHA);

                if (isBestScore(currentAwakeScore, currentDeepScore, currentLightScore, currentRemScore, CONFIDENCE_MARGIN)) {
                    awakeDuration += 1000;
                    deepSleepDuration = 0;
                    deepStableDuration = 0;
                    if (tryTransitionTo(SLEEP_AWAKE, AWAKE_SLOW_CONFIRM_SECONDS * 1000)) {
                        stats.wakeCount++;
                        awakeDuration = 0;
                        Serial.println("🔄 状态切换: 浅睡 → 清醒 (慢速触发)");
                    }
                } else if (isBestScore(currentDeepScore, currentAwakeScore, currentLightScore, currentRemScore, CONFIDENCE_MARGIN) &&
                           currentDeepScore >= HYSTERESIS_ENTER_DEEP) {
                    deepSleepDuration += 1000;
                    if (movementData.isValid && movementData.movement < DEEP_SLEEP_HARD_MOVEMENT_LIMIT) {
                        deepStableDuration += 1000;
                    } else {
                        deepStableDuration = 0;
                    }
                    awakeDuration = 0;
                    if (deepStableDuration >= DEEP_STABLE_MIN_SECONDS * 1000 &&
                        tryTransitionTo(SLEEP_DEEP_SLEEP, DEEP_SLEEP_CONFIRM_SECONDS * 1000)) {
                        deepSleepDuration = 0;
                        Serial.println("🔄 状态切换: 浅睡 → 深睡 (稳定≥5分钟)");
                    }
                } else if (isBestScore(currentRemScore, currentAwakeScore, currentDeepScore, currentLightScore, CONFIDENCE_MARGIN) &&
                           currentRemScore >= HYSTERESIS_ENTER_REM) {
                    remSleepDuration += 1000;
                    deepSleepDuration = 0;
                    deepStableDuration = 0;
                    awakeDuration = 0;
                    if (tryTransitionTo(SLEEP_REM_SLEEP, REM_CONFIRM_SECONDS * 1000)) {
                        remSleepDuration = 0;
                        Serial.println("🔄 状态切换: 浅睡 → REM");
                    }
                } else {
                    deepSleepDuration = 0;
                    awakeDuration = 0;
                    deepStableDuration = 0;
                    pendingState = SLEEP_LIGHT_SLEEP;
                }
            }
            break;

        case SLEEP_DEEP_SLEEP:
            if (!presence.isPresent) {
                noPersonTimer += 1000;
                if (noPersonTimer > OUT_OF_BED_SECONDS * 1000) {
                    currentState = SLEEP_OUT_OF_BED;
                    stateEnterTime = now;
                    pendingState = currentState;
                    stats.wakeCount++;
                    Serial.println("🔄 状态切换: 深睡 → 离床");
                }
            } else {
                noPersonTimer = 0;

                if (movementData.isValid && movementData.movement > FAST_AWAKE_MOVEMENT_THRESHOLD) {
                    currentState = SLEEP_AWAKE;
                    stateEnterTime = now;
                    pendingState = SLEEP_AWAKE;
                    stats.wakeCount++;
                    lightSleepDuration = 0;
                    movementHighDuration = 0;
                    Serial.println("🔄 状态切换: 深睡 → 清醒 (快速触发:体动大)");
                    break;
                }

                if (movementData.isValid && movementData.movement > DEEP_SLEEP_HARD_MOVEMENT_LIMIT) {
                    lightSleepDuration += 1000;
                    if (tryTransitionTo(SLEEP_LIGHT_SLEEP, LIGHT_SLEEP_CONFIRM_SECONDS * 1000)) {
                        lightSleepDuration = 0;
                        Serial.println("🔄 状态切换: 深睡 → 浅睡 (体动超限)");
                    }
                } else {
                    lightSleepDuration = 0;
                    pendingState = SLEEP_DEEP_SLEEP;
                }

                if (movementData.isValid && movementData.movement > MOVEMENT_HIGH_THRESHOLD) {
                    movementHighDuration += 1000;
                    if (tryTransitionTo(SLEEP_AWAKE, AWAKE_CONFIRM_SECONDS * 1000)) {
                        stats.wakeCount++;
                        movementHighDuration = 0;
                        Serial.println("🔄 状态切换: 深睡 → 清醒");
                    }
                } else {
                    movementHighDuration = 0;
                }

                float rawRem = calculateRemScore(hrData, rrData, hrvData, movementData);
                float rawLight = calculateLightSleepScore(hrData, rrData, hrvData, movementData);
                currentRemScore = emaSmooth(rawRem, currentRemScore, EMA_ALPHA);
                currentLightScore = emaSmooth(rawLight, currentLightScore, EMA_ALPHA);

                if (currentRemScore > currentLightScore && currentRemScore > HYSTERESIS_EXIT_DEEP &&
                    currentDeepScore < HYSTERESIS_EXIT_DEEP) {
                    remSleepDuration += 1000;
                    if (tryTransitionTo(SLEEP_REM_SLEEP, REM_CONFIRM_SECONDS * 1000)) {
                        remSleepDuration = 0;
                        Serial.println("🔄 状态切换: 深睡 → REM");
                    }
                } else {
                    remSleepDuration = 0;
                }
            }
            break;

        case SLEEP_REM_SLEEP:
            if (!presence.isPresent) {
                noPersonTimer += 1000;
                if (noPersonTimer > OUT_OF_BED_SECONDS * 1000) {
                    currentState = SLEEP_OUT_OF_BED;
                    stateEnterTime = now;
                    pendingState = currentState;
                    stats.wakeCount++;
                    Serial.println("🔄 状态切换: REM → 离床");
                }
            } else {
                noPersonTimer = 0;

                if (movementData.isValid && movementData.movement > FAST_AWAKE_MOVEMENT_THRESHOLD) {
                    currentState = SLEEP_AWAKE;
                    stateEnterTime = now;
                    pendingState = SLEEP_AWAKE;
                    stats.wakeCount++;
                    Serial.println("🔄 状态切换: REM → 清醒 (快速触发:体动大)");
                    break;
                }

                float rawRem = calculateRemScore(hrData, rrData, hrvData, movementData);
                float rawLight = calculateLightSleepScore(hrData, rrData, hrvData, movementData);
                float rawDeep = calculateDeepSleepScore(hrData, rrData, hrvData, movementData);
                float rawAwake = calculateAwakeScore(hrData, rrData, hrvData, movementData);

                currentRemScore = emaSmooth(rawRem, currentRemScore, EMA_ALPHA);
                currentLightScore = emaSmooth(rawLight, currentLightScore, EMA_ALPHA);
                currentDeepScore = emaSmooth(rawDeep, currentDeepScore, EMA_ALPHA);
                currentAwakeScore = emaSmooth(rawAwake, currentAwakeScore, EMA_ALPHA);

                if (isBestScore(currentAwakeScore, currentRemScore, currentLightScore, currentDeepScore, CONFIDENCE_MARGIN)) {
                    awakeDuration += 1000;
                    if (tryTransitionTo(SLEEP_AWAKE, AWAKE_SLOW_CONFIRM_SECONDS * 1000)) {
                        stats.wakeCount++;
                        awakeDuration = 0;
                        Serial.println("🔄 状态切换: REM → 清醒");
                    }
                } else if (isBestScore(currentLightScore, currentRemScore, currentAwakeScore, currentDeepScore, CONFIDENCE_MARGIN) &&
                           currentRemScore < HYSTERESIS_EXIT_REM) {
                    if (tryTransitionTo(SLEEP_LIGHT_SLEEP, LIGHT_SLEEP_CONFIRM_SECONDS * 1000)) {
                        Serial.println("🔄 状态切换: REM → 浅睡");
                    }
                } else if (isBestScore(currentDeepScore, currentRemScore, currentLightScore, currentAwakeScore, CONFIDENCE_MARGIN) &&
                           currentDeepScore >= HYSTERESIS_ENTER_DEEP) {
                    deepStableDuration += 1000;
                    if (deepStableDuration >= DEEP_STABLE_MIN_SECONDS * 1000 &&
                        tryTransitionTo(SLEEP_DEEP_SLEEP, DEEP_SLEEP_CONFIRM_SECONDS * 1000)) {
                        deepStableDuration = 0;
                        Serial.println("🔄 状态切换: REM → 深睡 (稳定≥5分钟)");
                    }
                } else {
                    awakeDuration = 0;
                    deepStableDuration = 0;
                    pendingState = SLEEP_REM_SLEEP;
                }
            }
            break;

        case SLEEP_OUT_OF_BED:
            if (presence.isPresent) {
                noPersonTimer = 0;
                currentState = SLEEP_IN_BED;
                stateEnterTime = now;
                pendingState = currentState;
                Serial.println("🔄 状态切换: 离床 → 在床");
            } else {
                noPersonTimer += 1000;
                bool hrExists = hrData.isValid && hrData.bpmSmoothed > 0;
                if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && !hrExists) {
                    currentState = SLEEP_SESSION_END;
                    stateEnterTime = now;
                    pendingState = currentState;
                    if (wasAsleep) {
                        calculateSleepScore();
                    }
                    Serial.println("🔄 状态切换: 离床 → 会话结束");
                } else if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && hrExists) {
                    Serial.println("⚠️ 检测到无人但HR存在，可能遮挡，暂不结束会话");
                }
            }
            break;

        case SLEEP_GETTING_UP:
            if (!presence.isPresent) {
                noPersonTimer += 1000;
                bool hrExists = hrData.isValid && hrData.bpmSmoothed > 0;
                if (noPersonTimer > NO_PERSON_END_SECONDS * 1000 && !hrExists) {
                    currentState = SLEEP_SESSION_END;
                    stateEnterTime = now;
                    pendingState = currentState;
                    if (wasAsleep) {
                        calculateSleepScore();
                    }
                    Serial.println("🔄 状态切换: 起床 → 会话结束");
                }
            } else {
                float movement = movementData.isValid ? movementData.movement : 0;
                if (movement < GETTING_UP_MOVEMENT_THRESHOLD) {
                    gettingUpDuration += 1000;
                    if (gettingUpDuration >= 60000) {
                        currentState = SLEEP_AWAKE;
                        stateEnterTime = now;
                        pendingState = currentState;
                        gettingUpDuration = 0;
                        Serial.println("🔄 状态切换: 起床 → 清醒 (重新躺下)");
                    }
                } else {
                    gettingUpDuration = 0;
                }
            }
            break;

        case SLEEP_SESSION_END:
            if (presence.isPresent) {
                currentState = SLEEP_IN_BED;
                stateEnterTime = now;
                pendingState = currentState;
                noPersonTimer = 0;
                Serial.println("🔄 状态切换: 会话结束 → 在床（新会话）");
            }
            break;
    }
}

void SleepAnalyzer::updateStatistics(unsigned long dt) {
    switch (currentState) {
        case SLEEP_LIGHT_SLEEP:
            stats.lightSleepTime += dt;
            stats.totalSleepTime += dt;
            break;
        case SLEEP_DEEP_SLEEP:
            stats.deepSleepTime += dt;
            stats.totalSleepTime += dt;
            break;
        case SLEEP_REM_SLEEP:
            stats.remSleepTime += dt;
            stats.totalSleepTime += dt;
            break;
        case SLEEP_AWAKE:
            stats.awakeTime += dt;
            break;
        case SLEEP_OUT_OF_BED:
            stats.outOfBedTime += dt;
            break;
        default:
            break;
    }
}

void SleepAnalyzer::calculateSleepScore() {
    float totalHours = stats.totalSleepTime / 3600000.0f;
    if (totalHours >= 7.0f && totalHours <= 9.0f) {
        score.durationScore = 18.0f;
    } else if (totalHours >= 6.0f && totalHours < 7.0f) {
        score.durationScore = 13.0f;
    } else if (totalHours > 9.0f && totalHours <= 10.0f) {
        score.durationScore = 13.0f;
    } else {
        score.durationScore = 5.0f;
    }

    float deepRatio = (stats.totalSleepTime > 0) ?
        (float)stats.deepSleepTime / stats.totalSleepTime : 0;
    if (deepRatio > 0.2f) {
        score.deepScore = 14.0f;
    } else if (deepRatio > 0.15f) {
        score.deepScore = 11.0f;
    } else if (deepRatio > 0.1f) {
        score.deepScore = 7.0f;
    } else {
        score.deepScore = 3.0f;
    }

    if (stats.wakeCount <= 1) {
        score.continuityScore = 11.0f;
    } else if (stats.wakeCount <= 3) {
        score.continuityScore = 7.0f;
    } else if (stats.wakeCount <= 5) {
        score.continuityScore = 4.0f;
    } else {
        score.continuityScore = 2.0f;
    }

    score.physiologyScore = 7.0f;

    float latencyMin = stats.sleepLatency / 60.0f;
    if (latencyMin < 20.0f) {
        score.latencyScore = 8.0f;
    } else if (latencyMin < 30.0f) {
        score.latencyScore = 6.0f;
    } else if (latencyMin < 45.0f) {
        score.latencyScore = 3.0f;
    } else {
        score.latencyScore = 1.0f;
    }

    float sleepEfficiency = 0;
    if (stats.totalSleepTime + stats.awakeTime > 0) {
        sleepEfficiency = (float)stats.totalSleepTime / (stats.totalSleepTime + stats.awakeTime);
    }
    if (sleepEfficiency > 0.9f) {
        score.efficiencyScore = 14.0f;
    } else if (sleepEfficiency > 0.8f) {
        score.efficiencyScore = 10.0f;
    } else if (sleepEfficiency > 0.7f) {
        score.efficiencyScore = 6.0f;
    } else {
        score.efficiencyScore = 3.0f;
    }

    float remRatio = (stats.totalSleepTime > 0) ?
        (float)stats.remSleepTime / stats.totalSleepTime : 0;
    float cycleScoreVal = 0;
    if (stats.sleepCycles >= 4) {
        cycleScoreVal = 20.0f;
    } else if (stats.sleepCycles >= 3) {
        cycleScoreVal = 15.0f;
    } else if (stats.sleepCycles >= 2) {
        cycleScoreVal = 10.0f;
    } else if (stats.sleepCycles >= 1) {
        cycleScoreVal = 6.0f;
    } else {
        cycleScoreVal = 2.0f;
    }
    if (remRatio >= 0.2f && remRatio <= 0.25f) {
        cycleScoreVal += 8.0f;
    } else if (remRatio >= 0.15f) {
        cycleScoreVal += 5.0f;
    } else if (remRatio > 0) {
        cycleScoreVal += 2.0f;
    }
    score.cycleScore = constrain_value(cycleScoreVal, 0.0f, 28.0f);

    float rawTotal = score.durationScore + score.deepScore +
                     score.continuityScore + score.physiologyScore +
                     score.latencyScore + score.efficiencyScore +
                     score.cycleScore;
    score.totalScore = constrain_value(rawTotal / 100.0f * 100.0f, 0.0f, 100.0f);

    Serial.println("━━━━━━━━━━ 睡眠评分 ━━━━━━━━━━");
    Serial.printf("  时长评分: %.0f/18\n", score.durationScore);
    Serial.printf("  深睡评分: %.0f/14\n", score.deepScore);
    Serial.printf("  连续性评分: %.0f/11\n", score.continuityScore);
    Serial.printf("  生理质量评分: %.0f/7\n", score.physiologyScore);
    Serial.printf("  入睡速度评分: %.0f/8\n", score.latencyScore);
    Serial.printf("  睡眠效率评分: %.0f/14 (效率:%.0f%%)\n", score.efficiencyScore, sleepEfficiency * 100);
    Serial.printf("  周期评分: %.0f/28 (周期数:%d, REM占比:%.0f%%)\n",
                  score.cycleScore, stats.sleepCycles, remRatio * 100);
    Serial.printf("  总分: %.0f/100\n", score.totalScore);
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}

void SleepAnalyzer::update(const HeartRateData& hrData,
                            const RespirationData& rrData,
                            const HRVEstimate& hrvData,
                            const BodyMovementData& movementData) {
    calibrateBaseline(hrData, rrData, movementData);

    PresenceData presence = evaluatePresence();

    updateState(presence, hrData, rrData, hrvData, movementData);

    updateSleepCycle();

    updateStatistics(1000);
}

void SleepAnalyzer::printState() {
    unsigned long stateDuration = (millis() - stateEnterTime) / 1000;
    Serial.printf("🛏️ 睡眠状态: %s | 持续: %02lu:%02lu:%02lu | 困倦度: %.2f | 周期: %d\n",
        SLEEP_STATE_NAMES[currentState],
        stateDuration / 3600, (stateDuration % 3600) / 60, stateDuration % 60,
        currentSleepiness, cycle.cycleCount);
}

void SleepAnalyzer::printStatistics() {
    Serial.println("━━━━━━━━━━ 睡眠统计 ━━━━━━━━━━");
    Serial.printf("  总睡眠: %02lu:%02lu:%02lu\n",
        stats.totalSleepTime / 3600000,
        (stats.totalSleepTime % 3600000) / 60000,
        (stats.totalSleepTime % 60000) / 1000);
    Serial.printf("  深睡: %02lu:%02lu:%02lu\n",
        stats.deepSleepTime / 3600000,
        (stats.deepSleepTime % 3600000) / 60000,
        (stats.deepSleepTime % 60000) / 1000);
    Serial.printf("  浅睡: %02lu:%02lu:%02lu\n",
        stats.lightSleepTime / 3600000,
        (stats.lightSleepTime % 3600000) / 60000,
        (stats.lightSleepTime % 60000) / 1000);
    Serial.printf("  REM: %02lu:%02lu:%02lu\n",
        stats.remSleepTime / 3600000,
        (stats.remSleepTime % 3600000) / 60000,
        (stats.remSleepTime % 60000) / 1000);
    Serial.printf("  清醒: %02lu:%02lu:%02lu\n",
        stats.awakeTime / 3600000,
        (stats.awakeTime % 3600000) / 60000,
        (stats.awakeTime % 60000) / 1000);
    Serial.printf("  离床: %02lu:%02lu:%02lu\n",
        stats.outOfBedTime / 3600000,
        (stats.outOfBedTime % 3600000) / 60000,
        (stats.outOfBedTime % 60000) / 1000);
    Serial.printf("  醒来次数: %d\n", stats.wakeCount);
    Serial.printf("  入睡耗时: %lus\n", stats.sleepLatency);
    Serial.printf("  睡眠周期: %d\n", stats.sleepCycles);
    float sleepEfficiency = 0;
    if (stats.totalSleepTime + stats.awakeTime > 0) {
        sleepEfficiency = (float)stats.totalSleepTime / (stats.totalSleepTime + stats.awakeTime);
    }
    Serial.printf("  睡眠效率: %.0f%%\n", sleepEfficiency * 100);
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}
