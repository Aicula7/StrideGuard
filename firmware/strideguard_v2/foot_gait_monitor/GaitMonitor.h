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
  float rollRateDps;
  float rollAngleDeg;
  uint32_t beforeContactMs;
  uint32_t afterContactMs;
};

constexpr Settings WalkingSettings = {
    MaxBaselineIntervals, 450, 1800, 1.25f, 1.10f, 0.35f, 1.35f, 250.0f, 12.0f, 40, 100};
constexpr Settings HandheldSettings = {
    8, 650, 3500, 1.15f, 1.07f, 0.18f, 1.15f, 100.0f, 10.0f, 60, 180};

struct Result {
  bool contact = false;
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

  // A pause or missing sample breaks pending evidence, including the stride
  // across the break. Keep previously accepted baseline measurements.
  void interrupt() {
    haveSample = false;
    haveContact = false;
    contactArmed = false;
    contactPending = false;
    historyCount = 0;
    historyNext = 0;
    clearTiming();
  }

  uint8_t baselineCount() const { return count; }
  bool calibrated() const { return count == settings().baselineTarget; }
  float baselineStrideMs() const { return strideCenter; }
  float strideToleranceMs() const { return strideTolerance; }
  float impactThresholdG() const { return impactThreshold; }

  Result observe(uint32_t now, float accelerationG, float rollDps, bool clipped,
                 bool enableRoll = true) {
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

    const bool crossed = haveSample && accelerationG >= config.contactG && contactArmed;
    if (accelerationG >= config.contactG) contactArmed = false;
    if (accelerationG <= config.rearmG) contactArmed = true;

    if (crossed && (!haveContact || now - lastContactMs >= config.minimumStrideMs)) {
      haveContact = true;
      lastContactMs = now;
      contactPending = true;
      peakImpact = accelerationG;
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
        contactClipped |= clipped;
        roll.add(sample, elapsed, config.rollRateDps);
      }
      if (age >= config.afterContactMs) result = finishContact();
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
    result.suddenRoll = rollEnabled &&
                       ((roll.angle >= config.rollAngleDeg && roll.fastPositive >= 2) ||
                        (roll.angle <= -config.rollAngleDeg && roll.fastNegative >= 2));
    if (result.suddenRoll) {
      clearTiming();
      return result;
    }

    result.hardLanding = calibrated() && peakImpact >= impactThreshold;
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
      result.baselineAccepted = true;
      strides[count] = static_cast<float>(stride);
      impacts[count] = peakImpact;
      if (++count == config.baselineTarget) {
        finishBaseline();
        result.baselineReady = true;
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
    strideTolerance = std::max(100.0f, std::max(0.15f * strideCenter, 4.4478f * mad));
    const float impactCenter = median(impacts);
    const auto& config = settings();
    impactThreshold = std::max(impactCenter + config.impactMarginG,
                               config.impactMultiplier * impactCenter);
  }

  Profile activeProfile;
  RollSample history[HistorySize] = {};
  uint8_t historyCount = 0;
  uint8_t historyNext = 0;
  RollWindow roll;
  uint32_t lastSampleMs = 0;
  uint32_t lastContactMs = 0;
  uint32_t timingContactMs = 0;
  bool haveSample = false;
  bool haveContact = false;
  bool contactArmed = false;
  bool contactPending = false;
  bool contactClipped = false;
  bool haveTimingContact = false;
  bool timingAlertActive = false;
  bool rollEnabled = true;
  float peakImpact = 0;
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
