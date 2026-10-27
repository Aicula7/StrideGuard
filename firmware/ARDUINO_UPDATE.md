# Update the Existing Seven Arduino Tabs

This is the **baseline20-manual-stream-v1 / protocol 2** release. From the preceding protocol-v2 version, replace the Foot `.ino`, Wrist `.ino`, and Wrist `GaitStabilityDetector.h`. Keep the other four headers. No new Arduino tab is needed; do not mix v1/v2 protocol files.

## File map

| Window | Tab | Action |
| --- | --- | --- |
| Foot | `foot_hand_motion_test.ino` | Replace the entire tab with [this source](foot_hand_motion_test/foot_hand_motion_test.ino) |
| Foot | `HandMotionDetector.h` | Keep unchanged |
| Foot | `HandTestProtocol.h` | Keep the existing protocol-v2 header; the complete header is below |
| Wrist | `wrist_hand_motion_test.ino` | Replace the entire tab with [this source](wrist_hand_motion_test/wrist_hand_motion_test.ino) |
| Wrist | `GaitStabilityDetector.h` | Replace the entire tab with [this source](wrist_hand_motion_test/GaitStabilityDetector.h); it contains the 20-interval timing baseline and the existing independent motion detector |
| Wrist | `HandEventTracker.h` | Keep the existing protocol-v2 tracker; check its sample guard below |
| Wrist | `HandTestProtocol.h` | Keep the existing protocol-v2 header; it must match the Foot copy |

For each complete-source link: open it in GitHub, click **Raw**, press **Ctrl+A / Ctrl+C**, then select the correct Arduino tab and press **Ctrl+A / Ctrl+V / Ctrl+S**. This avoids missing braces or pasting only part of a function. Do not copy the Markdown page or backtick fences into a code tab.

## Shared HandTestProtocol.h: paste into BOTH copies

```cpp
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Both ESP32 nodes use this exact versioned 20-byte little-endian layout.
namespace HandTest {
static constexpr uint16_t Magic = 0x5347;
static constexpr uint8_t Version = 2;
static constexpr uint8_t Gesture = 1;
static constexpr uint8_t Heartbeat = 2;
// Independent 50 Hz stream; its sequence is not the gesture sequence.
static constexpr uint8_t MotionSample = 3;
static constexpr const char* ServiceUuid = "7dfed001-2b6a-4db6-ae70-8dc32f7e5900";
static constexpr const char* CharacteristicUuid = "7dfed002-2b6a-4db6-ae70-8dc32f7e5900";
struct Packet {
  uint16_t magic;
  uint8_t version;
  uint8_t type;
  uint32_t bootSession;
  uint32_t sequence;
  uint32_t timestampMs;
  int16_t accelerationDeviationCenti;
  uint16_t angularRateCenti;
};
static_assert(sizeof(Packet) == 20, "Hand-test packet must fit the default BLE payload");
inline bool decode(const uint8_t* data, size_t length, Packet& packet) {
  if (!data || length != sizeof(Packet)) return false;
  memcpy(&packet, data, sizeof(Packet));
  return packet.magic == Magic && packet.version == Version &&
         (packet.type == Gesture || packet.type == Heartbeat || packet.type == MotionSample);
}
}
```

## Wrist HandEventTracker.h: verify the existing guard

Find:

```cpp
Decision accept(const HandTest::Packet& packet) {
  Decision out;
```

Ensure this existing line immediately follows `Decision out;`. Do not add a duplicate:

```cpp
if (packet.type == HandTest::MotionSample) return out;
```

This prevents motion-sample sequence numbers from being counted as missing calibration events.

## Upload and check

1. Save all edited tabs. Upload Foot to XIAO_ESP32C3 / COM3 and Wrist to Adafruit Feather ESP32-S3 No PSRAM / COM4 (these are this setup's ports).
2. Open both monitors at 115200 baud. Both should identify `baseline20-manual-stream-v1 protocol=2`.
3. Uploading Wrist clears the RAM baseline; send `c` to Foot, then `r` to Wrist, and establish `20/20` with at least 21 evenly spaced tilt-and-hold gestures.
4. After `BASELINE_READY`, send `m` to Foot, then use continuous motion. Look at `MOTION_WINDOW`, `high_windows`, and `motion_samples_rx`; Foot `events` no longer controls the alarm.
5. For the full demonstration, follow [QUICK_TEST.md](QUICK_TEST.md).

## Adjustable limits

In Wrist `GaitStabilityDetector.h`, find **`class VigorousMotionDetector`**, then `AccelEnter`, `GyroEnter`, `AccelExit`, `GyroExit`, `WindowMs`, `EnterWindows`, and `ExitWindows`. The entry defaults are 3.0 m/s^2 acceleration RMS or 3.0 rad/s gyro RMS for four consecutive half-second windows. They are engineering starting values; use actual ordinary/vigorous logs to tune them. Do not edit the Foot gesture thresholds to tune post-calibration intensity detection.
