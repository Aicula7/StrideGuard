#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

// Experimental baseline-relative STRIDE TIMING detector.
// Input: timestamps of successive SAME-FOOT strikes, never raw IMU packets.
// It does not measure balance, stance duration, impact, or fatigue directly.
class GaitStabilityDetector {
 public:
  static constexpr size_t WindowSize = 5;
  static constexpr size_t BaselineWindows = 3;  // Short initial bench calibration.
  static constexpr size_t BaselineIntervalTarget = WindowSize * BaselineWindows;
  static constexpr uint8_t EnterWindows = 2;
  static constexpr uint8_t ExitWindows = 2;
  static constexpr uint32_t CooldownMs = 10000;
  static constexpr uint32_t MinStrideMs = 350;
  static constexpr uint32_t MaxStrideMs = 2500;
  enum class State { Calibrating, Waiting, Stable, Suspect, Unstable };

  struct Result {
    bool accepted = false;
    bool referenceOnly = false;
    bool windowComplete = false;
    bool justCalibrated = false;
    bool gapReset = false;
    bool alert = false;
    bool cooldownSuppressed = false;
    uint32_t intervalMs = 0;
    size_t windowCount = 0;
    size_t baselineWindows = 0;
    float meanMs = 0;
    float cvPercent = 0;
    float baselineCv = 0;
    float enterCv = 0;
    float exitCv = 0;
    State state = State::Calibrating;
  };

  void reset() {
    count_ = 0;
    baselineCount_ = 0;
    baselineCvSum_ = 0;
    baselineCv_ = 0;
    enterCv_ = 0;
    exitCv_ = 0;
    high_ = 0;
    low_ = 0;
    unstable_ = false;
    monitoringReady_ = false;
    hasPrevious_ = false;
    hasAlert_ = false;
    previousMs_ = 0;
    lastAlertMs_ = 0;
  }

  bool calibrated() const { return baselineCount_ == BaselineWindows; }

  size_t baselineIntervals() const {
    return baselineCount_ * WindowSize + (calibrated() ? 0 : count_);
  }

  // A pause or missing packet breaks timing continuity, not calibration.
  // Keep partial/complete calibration windows made from valid intervals.
  // After calibration, discard incomplete monitoring windows and pending
  // alert evidence so separate motion segments cannot combine into an alert.
  void interrupt(bool clockRestarted = false) {
    hasPrevious_ = false;
    high_ = 0;
    low_ = 0;
    unstable_ = false;
    monitoringReady_ = false;
    if (calibrated()) count_ = 0;
    if (clockRestarted) {
      hasAlert_ = false;
      lastAlertMs_ = 0;
    }
  }

  State state() const {
    if (!calibrated()) return State::Calibrating;
    if (!monitoringReady_) return State::Waiting;
    if (unstable_) return State::Unstable;
    return high_ ? State::Suspect : State::Stable;
  }

  // Call once per detected same-foot strike. uint32_t subtraction handles
  // millis() rollover when events are observed continuously.
  Result observeStride(uint32_t timestampMs) {
    Result out = snapshot();
    if (!hasPrevious_) {
      previousMs_ = timestampMs;
      hasPrevious_ = true;
      out.referenceOnly = true;
      return out;
    }
    const uint32_t interval = timestampMs - previousMs_;
    out.intervalMs = interval;
    if (interval < MinStrideMs) return out;  // Ignore bounce/duplicates.
    previousMs_ = timestampMs;
    if (interval > MaxStrideMs) {
      // Exclude this interval without erasing valid baseline samples.
      interrupt();
      previousMs_ = timestampMs;
      hasPrevious_ = true;
      out = snapshot();
      out.intervalMs = interval;
      out.gapReset = true;
      return out;
    }
    intervals_[count_++] = static_cast<float>(interval);
    out.accepted = true;
    out.windowCount = count_;
    if (count_ != WindowSize) return out;

    out.windowComplete = true;
    float sum = 0;
    for (size_t i = 0; i < WindowSize; ++i) sum += intervals_[i];
    out.meanMs = sum / static_cast<float>(WindowSize);
    float squared = 0;
    for (size_t i = 0; i < WindowSize; ++i) {
      const float delta = intervals_[i] - out.meanMs;
      squared += delta * delta;
    }
    out.cvPercent = 100.0f * sqrtf(squared / static_cast<float>(WindowSize)) / out.meanMs;
    count_ = 0;  // Non-overlapping windows, not repeated votes on the same data.

    if (!calibrated()) {
      baselineCvSum_ += out.cvPercent;
      ++baselineCount_;
      if (calibrated()) {
        // Compare windows of the same size. Freeze baseline during monitoring.
        baselineCv_ = baselineCvSum_ / static_cast<float>(BaselineWindows);
        // Experimental tunable parameters, NOT validated universal cutoffs.
        enterCv_ = fmaxf(2.0f * baselineCv_, baselineCv_ + 3.0f);
        exitCv_ = fmaxf(1.5f * baselineCv_, baselineCv_ + 1.5f);
        out.justCalibrated = true;
        monitoringReady_ = true;
      }
    } else if (!unstable_) {
      monitoringReady_ = true;
      high_ = out.cvPercent > enterCv_ ? high_ + 1 : 0;
      low_ = 0;
      if (high_ >= EnterWindows) {
        unstable_ = true;
        high_ = 0;
        if (!hasAlert_ || timestampMs - lastAlertMs_ >= CooldownMs) {
          out.alert = true;
          hasAlert_ = true;
          lastAlertMs_ = timestampMs;
        } else {
          out.cooldownSuppressed = true;
        }
      }
    } else {
      low_ = out.cvPercent < exitCv_ ? low_ + 1 : 0;
      if (low_ >= ExitWindows) {
        unstable_ = false;
        low_ = 0;
      }
    }
    out.baselineWindows = baselineCount_;
    out.baselineCv = baselineCv_;
    out.enterCv = enterCv_;
    out.exitCv = exitCv_;
    out.state = state();
    return out;
  }

  static const char* stateName(State value) {
    switch (value) {
      case State::Stable: return "STABLE";
      case State::Suspect: return "SUSPECT";
      case State::Unstable: return "UNSTABLE";
      case State::Waiting: return "WAITING_FOR_MOTION";
      default: return "CALIBRATING";
    }
  }

 private:
  Result snapshot() const {
    Result out;
    out.windowCount = count_;
    out.baselineWindows = baselineCount_;
    out.baselineCv = baselineCv_;
    out.enterCv = enterCv_;
    out.exitCv = exitCv_;
    out.state = state();
    return out;
  }
  float intervals_[WindowSize] = {};
  size_t count_ = 0;
  size_t baselineCount_ = 0;
  float baselineCvSum_ = 0;
  float baselineCv_ = 0;
  float enterCv_ = 0;
  float exitCv_ = 0;
  uint8_t high_ = 0;
  uint8_t low_ = 0;
  bool unstable_ = false;
  bool monitoringReady_ = false;
  bool hasPrevious_ = false;
  uint32_t previousMs_ = 0;
  bool hasAlert_ = false;
  uint32_t lastAlertMs_ = 0;
};
