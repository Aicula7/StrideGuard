# Minimal copy/paste calibration fix for e246574

These five edits are for the exact downloaded `e246574` wrist sketch. They fix the reported calibration resets without changing foot firmware or manufacturing interval samples. Do not apply them on top of the current complete version, which already includes the fix plus enhanced status diagnostics and an explicit waiting state.

Keep a copy of your local files before editing. All edits are inside `firmware/wrist_hand_motion_test/`. Only upload the wrist sketch afterward; its BLE protocol is unchanged.

## 1. Add public methods

In `GaitStabilityDetector.h`, paste immediately below the existing `bool calibrated() const` method, inside the public section. Do not add a second copy if these methods already exist.

```cpp
  void interrupt() {
    hasPrevious_ = false;
    high_ = 0;
    low_ = 0;
    unstable_ = false;
    if (calibrated()) count_ = 0;
  }

  size_t baselineIntervals() const {
    return baselineCount_ * WindowSize + (calibrated() ? 0 : count_);
  }
```

## 2. Exclude a long interval without clearing calibration

In that header, replace the whole `if (interval > MaxStrideMs)` block inside `observeStride()` with:

```cpp
    if (interval > MaxStrideMs) {
      // Exclude the pause without erasing valid baseline intervals.
      interrupt();
      previousMs_ = timestampMs;
      hasPrevious_ = true;
      out = snapshot();
      out.intervalMs = interval;
      out.gapReset = true;
      return out;
    }
```

## 3. Keep automatic pause handling from clearing baseline

In `wrist_hand_motion_test.ino`, replace the entire original `resetTiming` function with:

```cpp
void resetTiming(const char*) {
  timing.interrupt();
  eventSeen = false;
  Serial.printf("# TIMING_PAUSED baseline_kept=%u/30\n",
      static_cast<unsigned>(timing.baselineIntervals()));
}
```

## 4. Preserve explicit reset behavior

In the same `.ino`, replace the single-line serial `r` handler with:

```cpp
    if (command == 'r' || command == 'R') {
      timing.reset();
      resetReception("# RESET: build a new normal HAND-motion baseline.");
    }
```

## 5. Print the final 30/30 progress

Replace the original `if (!timing.calibrated())` progress-print block with:

```cpp
  if (!timing.calibrated() || result.justCalibrated) {
    Serial.printf("# CALIBRATION intervals=%u/30\n",
        static_cast<unsigned>(timing.baselineIntervals()));
  }
```

## Validate

Select the actual Adafruit Feather ESP32-S3 variant and wrist port, verify/upload, and open Serial Monitor at 115200 baud. Send `r` once to begin. Accumulate 30 valid motion intervals. A pause should show `TIMING_PAUSED baseline_kept=.../30`, then continue from that count. The first event after an interrupted time reference does not count as an interval. An overlong interval is excluded rather than counted or replaced with a fabricated value.

The exact edits above were applied automatically to the original e246574 sources. The patched wrist sketch compiled for `esp32:esp32:adafruit_feather_esp32s3_nopsram` with Arduino ESP32 core 2.0.17, and a regression check verified retention of 11 intervals across the reported 2681 ms gap, continued accumulation to 30, preservation after a pause, and explicit reset to zero. These checks do not replace physical board validation.

The full current version additionally prints pause reasons and baseline progress on periodic status lines, preserves alert cooldown across same-clock interruptions, and handles a restarted foot clock explicitly.
