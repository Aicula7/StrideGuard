#pragma once

#include <algorithm>
#include <cmath>
#include <stdint.h>

namespace GaitMonitor {

constexpr float Gravity = 9.80665f;
constexpr uint8_t MaxBaselineIntervals = 20;
constexpr uint32_t MaximumSampleGapMs = 25;

enum class Profile { Walking, Handheld };

struct Settings {
  uint8_t baselineTarget;
  uint32_t minimumStrideMs;
  uint32_t maximumStrideMs;
  float contactG;
  float rearmG;
  float impactMarginG;
  float impactMultiplier;
  float minHardLandingG;
  float stiffHeelVerticalG;
  float stiffHeelJerkGps;
  float rollRateDps;
  float rollAngleDeg;
  float airborneRollDeg;
  float airborneRateDps;
  uint32_t beforeContactMs;
  uint32_t afterContactMs;
};

// Calibrated thresholds for top-of-shoe Velcro mount (prevents normal walking/swing false positives)
constexpr Settings WalkingSettings = {
    MaxBaselineIntervals, 480, 1800, 1.30f, 1.12f, 0.85f, 1.55f, 2.65f, 2.45f, 85.0f, 280.0f, 18.0f, 42.0f, 380.0f, 40, 100};
constexpr Settings HandheldSettings = {
    8, 650, 3500, 1.18f, 1.08f, 0.35f, 1.30f, 1.65f, 1.50f, 40.0f, 140.0f, 13.0f, 30.0f, 200.0f, 60, 180};

struct Result {
  bool contact = false;
  bool airborneRollAlert = false;
  bool clipped = false;
  bool gap = false;
  bool baselineReady = false;
  bool timingAlert = false;
  bool hardLanding = false;
  bool suddenRoll = false;
  bool intervalValid = false;
  bool baselineAccepted = false;
  bool strideOutlier = false;
  bool timingActive = false;
  uint8_t timingWindowCount = 0;
  uint8_t timingOutliers = 0;
  uint32_t contactMs = 0;
  uint16_t strideMs = 0;
  float peakImpactG = 0;
  float rollPeakDps = 0;
  float rollAngleDeg = 0;
};

class Detector {
 public:
  explicit Detector(Profile profile = Profile::Walking) : activeProfile(profile) {}

  void reset() { *this = Detector(activeProfile); }

  void setProfile(Profile profile) { *this = Detector(profile); }

  Profile profile() const { return activeProfile; }

  const Settings& settings() const {
    return activeProfile == Profile::Handheld ? HandheldSettings : WalkingSettings;
  }

  void interrupt() {
    haveSample = false;
    haveContact = false;
    contactArmed = false;
    contactPending = false;
    historyCount = 0;
    historyNext = 0;
    freeRollAngle = 0.0f;
    freeRollPeakDps = 0.0f;
    clearTiming();
  }

  uint8_t baselineCount() const { return count; }
  bool calibrated() const { return count == settings().baselineTarget; }
  float baselineStrideMs() const { return strideCenter; }
  float strideToleranceMs() const { return strideTolerance; }
  float impactThresholdG() const { return impactThreshold; }

  Result observe(uint32_t now, float accelerationG, float verticalG, float rollDps, bool clipped,
                 bool enableRoll = true, bool isTurning = false) {
    Result result;
    rollEnabled = enableRoll;
    const auto& config = settings();
    uint32_t elapsed = haveSample ? now - lastSampleMs : 0;
    if (haveSample && elapsed == 0) return result;
    if (haveSample && elapsed > MaximumSampleGapMs) {
      interrupt();
      elapsed = 0;
      result.gap = true;
    }

    const float dtSec = elapsed * 0.001f;
    const float verticalJerkGps = (haveSample && dtSec > 0.0f) ? std::fabs(verticalG - prevVerticalG) / dtSec : 0.0f;
    prevVerticalG = verticalG;

    // Airborne / swing roll tracker: ONLY active after calibration completes, with continuous leaky decay
    // so normal walking swing never accumulates into a false roll alert.
    if (calibrated() && rollEnabled && !clipped && haveSample && dtSec > 0.0f) {
      if (std::fabs(rollDps) > 45.0f) {
        freeRollAngle = (freeRollAngle * 0.94f) + (rollDps * dtSec);
        freeRollPeakDps = std::max(freeRollPeakDps * 0.94f, std::fabs(rollDps));
      } else {
        freeRollAngle *= 0.78f;
        freeRollPeakDps *= 0.78f;
        if (std::fabs(freeRollAngle) < 2.0f) {
          freeRollAngle = 0.0f;
          freeRollPeakDps = 0.0f;
        }
      }

      if (!contactPending && !isTurning &&
          (std::fabs(freeRollAngle) >= config.airborneRollDeg && freeRollPeakDps >= config.airborneRateDps) &&
          (now - lastAirborneRollMs >= 900)) {
        lastAirborneRollMs = now;
        result.airborneRollAlert = true;
        result.suddenRoll = true;
        result.contactMs = now;
        result.rollAngleDeg = freeRollAngle;
        result.rollPeakDps = freeRollPeakDps;
        freeRollAngle = 0.0f;
        freeRollPeakDps = 0.0f;
      }
    } else if (!calibrated()) {
      freeRollAngle = 0.0f;
      freeRollPeakDps = 0.0f;
    }

    const bool crossed = haveSample && accelerationG >= config.contactG && contactArmed;
    if (accelerationG >= config.contactG) contactArmed = false;
    if (accelerationG <= config.rearmG) contactArmed = true;

    if (crossed && (!haveContact || now - lastContactMs >= config.minimumStrideMs)) {
      haveContact = true;
      lastContactMs = now;
      contactPending = true;
      peakImpact = accelerationG;
      peakVerticalImpact = verticalG;
      peakVerticalJerk = verticalJerkGps;
      sustainedHighImpactFrames = (calibrated() && (accelerationG >= impactThreshold || verticalG >= config.stiffHeelVerticalG)) ? 1 : 0;
      contactTurning = isTurning;
      contactClipped = false;
      roll = RollWindow();
      for (uint8_t i = 0; i < historyCount; ++i) {
        const RollSample& sample = history[i];
        const uint32_t age = now - sample.timeMs;
        if (age <= config.beforeContactMs) {
          roll.add(sample, std::min<uint32_t>(sample.elapsedMs, config.beforeContactMs - age),
                   config.rollRateDps);
          contactClipped |= sample.clipped;
        }
      }
    }

    RollSample sample = {now, elapsed, rollDps, clipped};
    if (contactPending) {
      const uint32_t age = now - lastContactMs;
      if (age <= config.afterContactMs) {
        peakImpact = std::max(peakImpact, accelerationG);
        peakVerticalImpact = std::max(peakVerticalImpact, verticalG);
        peakVerticalJerk = std::max(peakVerticalJerk, verticalJerkGps);
        if (calibrated() && (accelerationG >= impactThreshold || verticalG >= config.stiffHeelVerticalG)) {
          ++sustainedHighImpactFrames;
        }
        contactTurning |= isTurning;
        contactClipped |= clipped;
        roll.add(sample, elapsed, config.rollRateDps);
      }
      if (age >= config.afterContactMs) {
        Result contactResult = finishContact();
        if (calibrated() && result.airborneRollAlert && !contactResult.suddenRoll) {
          contactResult.suddenRoll = true;
          contactResult.rollAngleDeg = result.rollAngleDeg;
          contactResult.rollPeakDps = result.rollPeakDps;
        }
        result = contactResult;
      }
    }
    history[historyNext] = sample;
    historyNext = (historyNext + 1) % HistorySize;
    if (historyCount < HistorySize) ++historyCount;
    haveSample = true;
    lastSampleMs = now;
    return result;
  }

 private:
  static constexpr uint8_t HistorySize = 8;

  struct RollSample {
    uint32_t timeMs;
    uint32_t elapsedMs;
    float rateDps;
    bool clipped;
  };

  struct RollWindow {
    float angle = 0;
    float positivePeak = 0;
    float negativePeak = 0;
    uint8_t fastPositive = 0;
    uint8_t fastNegative = 0;

    void add(const RollSample& sample, uint32_t elapsed, float rateThreshold) {
      angle += sample.rateDps * (elapsed * 0.001f);
      positivePeak = std::max(positivePeak, sample.rateDps);
      negativePeak = std::min(negativePeak, sample.rateDps);
      if (sample.rateDps >= rateThreshold) ++fastPositive;
      if (sample.rateDps <= -rateThreshold) ++fastNegative;
    }
  };

  Result finishContact() {
    contactPending = false;
    Result result;
    result.contact = true;
    result.contactMs = lastContactMs;
    result.clipped = contactClipped;
    result.peakImpactG = peakImpact;
    result.rollAngleDeg = roll.angle;
    result.rollPeakDps = roll.angle >= 0 ? roll.positivePeak : -roll.negativePeak;
    if (contactClipped) {
      clearTiming();
      return result;
    }

    const auto& config = settings();

    // STRICT CALIBRATION GATE: Never emit roll or hard-landing alerts until baseline calibration is complete
    if (calibrated() && !contactTurning) {
      result.suddenRoll = rollEnabled &&
                         ((roll.angle >= config.rollAngleDeg && roll.fastPositive >= 3) ||
                          (roll.angle <= -config.rollAngleDeg && roll.fastNegative >= 3));

      const bool heavyImpact = (peakImpact >= impactThreshold || peakVerticalImpact >= impactThreshold) &&
                               sustainedHighImpactFrames >= 2;
      const bool stiffHeelShock = (peakVerticalImpact >= config.stiffHeelVerticalG &&
                                   peakVerticalJerk >= config.stiffHeelJerkGps &&
                                   sustainedHighImpactFrames >= 2);
      result.hardLanding = heavyImpact || stiffHeelShock;
    }

    if (result.suddenRoll) {
      clearTiming();
      return result;
    }

    const uint32_t stride = lastContactMs - timingContactMs;
    const bool validStride = haveTimingContact && stride >= config.minimumStrideMs &&
                             stride <= config.maximumStrideMs;
    if (!validStride) clearTiming();
    timingContactMs = lastContactMs;
    haveTimingContact = true;
    if (!validStride) return result;

    result.strideMs = static_cast<uint16_t>(stride);
    result.intervalValid = true;
    if (!calibrated()) {
      if (!contactTurning) {
        result.baselineAccepted = true;
        strides[count] = static_cast<float>(stride);
        impacts[count] = std::max(peakImpact, peakVerticalImpact);
        if (++count == config.baselineTarget) {
          finishBaseline();
          result.baselineReady = true;
        }
      }
      return result;
    }

    const bool outlier = std::fabs(stride - strideCenter) > strideTolerance;
    result.strideOutlier = outlier;
    timingHistory = static_cast<uint8_t>(((timingHistory << 1) | outlier) & 0x1f);
    if (windowCount < 5) ++windowCount;
    uint8_t outliers = 0;
    for (uint8_t bits = timingHistory; bits; bits >>= 1) outliers += bits & 1;
    if (windowCount == 5 && outliers >= 3 && outlier && !timingAlertActive) {
      timingAlertActive = true;
      result.timingAlert = true;
    }
    if (windowCount == 5 && outliers == 0) timingAlertActive = false;
    result.timingActive = timingAlertActive;
    result.timingWindowCount = windowCount;
    result.timingOutliers = outliers;
    return result;
  }

  void clearTiming() {
    haveTimingContact = false;
    windowCount = 0;
    timingHistory = 0;
    timingAlertActive = false;
  }

  float median(float* values) const {
    std::sort(values, values + count);
    return (values[(count - 1) / 2] + values[count / 2]) * 0.5f;
  }

  void finishBaseline() {
    strideCenter = median(strides);
    for (uint8_t i = 0; i < count; ++i) {
      strides[i] = std::fabs(strides[i] - strideCenter);
    }
    const float mad = median(strides);
    strideTolerance = std::max(120.0f, std::max(0.18f * strideCenter, 4.4478f * mad));
    const float impactCenter = median(impacts);
    const auto& config = settings();
    impactThreshold = std::max({config.minHardLandingG,
                                impactCenter + config.impactMarginG,
                                config.impactMultiplier * impactCenter});
  }

  Profile activeProfile;
  RollSample history[HistorySize] = {};
  uint8_t historyCount = 0;
  uint8_t historyNext = 0;
  RollWindow roll;
  uint32_t lastSampleMs = 0;
  uint32_t lastContactMs = 0;
  uint32_t timingContactMs = 0;
  uint32_t lastAirborneRollMs = 0;
  bool haveSample = false;
  bool haveContact = false;
  bool contactArmed = false;
  bool contactPending = false;
  bool contactClipped = false;
  bool contactTurning = false;
  bool haveTimingContact = false;
  bool timingAlertActive = false;
  bool rollEnabled = true;
  float prevVerticalG = 1.0f;
  float peakImpact = 0;
  float peakVerticalImpact = 0;
  float peakVerticalJerk = 0;
  uint8_t sustainedHighImpactFrames = 0;
  float freeRollAngle = 0.0f;
  float freeRollPeakDps = 0.0f;
  float strides[MaxBaselineIntervals] = {};
  float impacts[MaxBaselineIntervals] = {};
  uint8_t count = 0;
  uint8_t timingHistory = 0;
  uint8_t windowCount = 0;
  float strideCenter = 0;
  float strideTolerance = 0;
  float impactThreshold = 0;
};

}  // namespace GaitMonitor
