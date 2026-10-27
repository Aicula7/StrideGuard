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
  static constexpr size_t BaselineWindows = 6;  // Thirty valid intervals for bench calibration.
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

// Independent post-calibration demo; the timing baseline above is unchanged.
// Fixed-threshold bench demonstration, not a gait or fall diagnosis.
// Input is continuous real IMU magnitudes, never gesture timestamps.
class VigorousMotionDetector {
 public:
  // Tune these engineering starting values from actual MOTION_WINDOW logs.
  static constexpr float AccelEnter = 3.0f;  // RMS of |a|-g, m/s^2
  static constexpr float GyroEnter = 3.0f;   // RMS angular-rate magnitude, rad/s
  static constexpr float AccelExit = 1.5f;
  static constexpr float GyroExit = 1.5f;
  static constexpr uint32_t WindowMs = 500;
  static constexpr uint32_t MaxSampleGapMs = 80;
  static constexpr uint32_t NoDataTimeoutMs = 500;
  static constexpr size_t MinimumSamples = 20;  // Nominally 25 samples at 50 Hz.
  static constexpr uint8_t EnterWindows = 4;
  static constexpr uint8_t ExitWindows = 4;

  enum class State { WaitingForData, Collecting, Stable, Suspect, Unstable };
  struct Result {
    bool accepted = false;
    bool started = false;
    bool windowComplete = false;
    bool streamReset = false;
    bool alert = false;
    bool high = false;
    bool low = false;
    size_t samples = 0;
    uint32_t durationMs = 0;
    float accelRms = 0;
    float gyroRms = 0;
    uint8_t highWindows = 0;
    uint8_t lowWindows = 0;
    uint32_t missedSamples = 0;
    const char* resetReason = "NONE";
    State state = State::WaitingForData;
  };

  void reset() {
    hasSample_ = false;
    hasDecision_ = false;
    unstable_ = false;
    high_ = 0;
    low_ = 0;
    count_ = 0;
    accelSquared_ = 0;
    gyroSquared_ = 0;
  }

  State state() const {
    if (!hasSample_) return State::WaitingForData;
    if (!hasDecision_) return State::Collecting;
    if (unstable_) return State::Unstable;
    return high_ ? State::Suspect : State::Stable;
  }
  size_t windowSamples() const { return count_; }
  uint32_t windowElapsedMs() const { return hasSample_ ? lastTimeMs_ - windowStartMs_ : 0; }

  Result observe(uint32_t timestampMs, uint32_t sequence,
                 float accelerationDeviation, float angularRateMagnitude) {
    Result out;
    out.state = state();
    if (!isfinite(accelerationDeviation) || !isfinite(angularRateMagnitude) || angularRateMagnitude < 0) {
      reset();
      out.streamReset = true;
      out.resetReason = "INVALID_SAMPLE";
      out.state = state();
      return out;
    }
    if (hasSample_) {
      const uint32_t sequenceDelta = sequence - lastSequence_;
      // Ignore repeated/out-of-order packets; they do not refresh evidence.
      if (sequenceDelta == 0 || sequenceDelta >= 0x80000000UL) return out;
      out.missedSamples = sequenceDelta - 1;
      const uint32_t timeDelta = timestampMs - lastTimeMs_;
      // Small losses are allowed only with sufficient window coverage. A burst
      // or a long timestamp gap breaks the sustained-motion evidence.
      if (sequenceDelta > 4 || timeDelta == 0 || timeDelta > MaxSampleGapMs) {
        reset();
        out.streamReset = true;
        out.resetReason = sequenceDelta != 1 ? "SAMPLE_SEQUENCE_GAP" : "SAMPLE_TIME_GAP";
      }
    }
    if (!hasSample_) {
      startWindow(timestampMs);
      out.started = true;
    } else if (timestampMs - windowStartMs_ >= WindowMs) {
      out.samples = count_;
      out.durationMs = timestampMs - windowStartMs_;
      if (count_ < MinimumSamples || out.durationMs > WindowMs + MaxSampleGapMs) {
        reset();
        out.streamReset = true;
        out.resetReason = "INSUFFICIENT_WINDOW_COVERAGE";
        out.started = true;
      } else {
        out.windowComplete = true;
        out.accelRms = sqrtf(static_cast<float>(accelSquared_ / count_));
        out.gyroRms = sqrtf(static_cast<float>(gyroSquared_ / count_));
        out.high = out.accelRms > AccelEnter || out.gyroRms > GyroEnter;
        out.low = out.accelRms < AccelExit && out.gyroRms < GyroExit;
        hasDecision_ = true;
        if (!unstable_) {
          high_ = out.high ? high_ + 1 : 0;
          low_ = 0;
          if (high_ >= EnterWindows) {
            unstable_ = true;
            out.alert = true;
          }
        } else {
          low_ = out.low ? low_ + 1 : 0;
          if (low_ >= ExitWindows) {
            unstable_ = false;
            high_ = 0;
          }
        }
      }
      // The boundary sample belongs only to the next, non-overlapping window.
      startWindow(timestampMs);
    }
    hasSample_ = true;
    lastTimeMs_ = timestampMs;
    lastSequence_ = sequence;
    ++count_;
    accelSquared_ += static_cast<double>(accelerationDeviation) * accelerationDeviation;
    gyroSquared_ += static_cast<double>(angularRateMagnitude) * angularRateMagnitude;
    out.accepted = true;
    out.highWindows = high_;
    out.lowWindows = low_;
    out.state = state();
    return out;
  }

  static const char* stateName(State value) {
    switch (value) {
      case State::Collecting: return "COLLECTING_WINDOW";
      case State::Stable: return "STABLE";
      case State::Suspect: return "SUSPECT";
      case State::Unstable: return "UNSTABLE";
      default: return "WAITING_FOR_DATA";
    }
  }
  const char* dataStateName() const {
    if (!hasSample_) return "NO_DATA";
    return hasDecision_ ? "READY" : "COLLECTING_WINDOW";
  }

 private:
  void startWindow(uint32_t timestampMs) {
    windowStartMs_ = timestampMs;
    count_ = 0;
    accelSquared_ = 0;
    gyroSquared_ = 0;
  }
  bool hasSample_ = false;
  bool hasDecision_ = false;
  bool unstable_ = false;
  uint8_t high_ = 0;
  uint8_t low_ = 0;
  size_t count_ = 0;
  double accelSquared_ = 0;
  double gyroSquared_ = 0;
  uint32_t windowStartMs_ = 0;
  uint32_t lastTimeMs_ = 0;
  uint32_t lastSequence_ = 0;
};
