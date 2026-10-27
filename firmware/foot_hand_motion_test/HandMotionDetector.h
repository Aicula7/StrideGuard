#pragma once
#include <math.h>
#include <stdint.h>

// Bench-test gesture detector, NOT a running foot-strike detector.
// Each gesture must be separated by a quiet hold before another can trigger.
class HandMotionDetector {
 public:
  static constexpr float Gravity = 9.80665f;
  // Original bench settings that produced real events in the user's logs.
  // Keep extraction behavior fixed while evaluating wrist calibration.
  static constexpr float AccelHigh = 2.5f;  // |magnitude - gravity|, m/s^2
  static constexpr float GyroHigh = 1.0f;   // angular-rate magnitude, rad/s
  static constexpr float AccelQuiet = 0.8f;
  static constexpr float GyroQuiet = 0.30f;
  static constexpr uint32_t QuietHoldMs = 120;
  static constexpr uint32_t ActiveHoldMs = 20;
  static constexpr uint32_t RefractoryMs = 350;
  static constexpr uint32_t MaxSampleGapMs = 100;
  enum class Phase { Invalid, NeedQuiet, Ready, Confirming, Cooldown, Event };
  struct Result {
    bool valid = false;
    bool event = false;
    bool armed = false;
    uint32_t quietHoldMs = 0;  // Diagnostic only; never changes event extraction.
    Phase phase = Phase::Invalid;
    float accelerationDeviation = 0;
    float angularRateMagnitude = 0;
  };

  void reset() {
    armed_ = false;
    quietTracking_ = false;
    activeTracking_ = false;
    hasSample_ = false;
    hasEvent_ = false;
  }

  Result update(uint32_t timeMs, float ax, float ay, float az,
                float gx, float gy, float gz) {
    Result out;
    if (!isfinite(ax) || !isfinite(ay) || !isfinite(az) ||
        !isfinite(gx) || !isfinite(gy) || !isfinite(gz)) {
      reset();
      return out;
    }
    if (hasSample_ && timeMs - lastSampleMs_ > MaxSampleGapMs) {
      armed_ = false;
      quietTracking_ = false;
      activeTracking_ = false;
    }
    hasSample_ = true;
    lastSampleMs_ = timeMs;
    out.valid = true;
    out.accelerationDeviation = sqrtf(ax * ax + ay * ay + az * az) - Gravity;
    out.angularRateMagnitude = sqrtf(gx * gx + gy * gy + gz * gz);
    const float deviation = fabsf(out.accelerationDeviation);
    const bool quiet = deviation < AccelQuiet && out.angularRateMagnitude < GyroQuiet;
    const bool active = deviation > AccelHigh || out.angularRateMagnitude > GyroHigh;

    if (quiet) {
      activeTracking_ = false;
      if (!quietTracking_) {
        quietTracking_ = true;
        quietSinceMs_ = timeMs;
      }
      if (timeMs - quietSinceMs_ >= QuietHoldMs) armed_ = true;
    } else {
      quietTracking_ = false;
      if (armed_ && active) {
        if (!activeTracking_) {
          activeTracking_ = true;
          activeSinceMs_ = timeMs;
        }
        if (timeMs - activeSinceMs_ >= ActiveHoldMs &&
            (!hasEvent_ || timeMs - lastEventMs_ >= RefractoryMs)) {
          out.event = true;
          hasEvent_ = true;
          lastEventMs_ = timeMs;
          armed_ = false;
          activeTracking_ = false;
        }
      } else {
        activeTracking_ = false;
      }
    }
    out.armed = armed_;
    if (quietTracking_) {
      const uint32_t heldMs = timeMs - quietSinceMs_;
      out.quietHoldMs = heldMs < QuietHoldMs ? heldMs : QuietHoldMs;
    }
    if (out.event) out.phase = Phase::Event;
    else if (!armed_) out.phase = Phase::NeedQuiet;
    else if (hasEvent_ && timeMs - lastEventMs_ < RefractoryMs) out.phase = Phase::Cooldown;
    else if (activeTracking_) out.phase = Phase::Confirming;
    else out.phase = Phase::Ready;
    return out;
  }

  static const char* phaseName(Phase phase) {
    switch (phase) {
      case Phase::NeedQuiet: return "NEED_QUIET";
      case Phase::Ready: return "READY";
      case Phase::Confirming: return "CONFIRMING";
      case Phase::Cooldown: return "COOLDOWN";
      case Phase::Event: return "EVENT";
      default: return "INVALID";
    }
  }

 private:
  bool armed_ = false;
  bool quietTracking_ = false;
  bool activeTracking_ = false;
  bool hasSample_ = false;
  bool hasEvent_ = false;
  uint32_t quietSinceMs_ = 0;
  uint32_t activeSinceMs_ = 0;
  uint32_t lastSampleMs_ = 0;
  uint32_t lastEventMs_ = 0;
};
