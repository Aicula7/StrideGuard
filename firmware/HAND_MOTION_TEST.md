# Real Foot IMU: Calibration and Vigorous-Motion Demo

Start with [QUICK_TEST.md](QUICK_TEST.md). This release changes **post-calibration detection** from gesture timing variability to sustained IMU intensity. This demo uses 20 valid timing intervals and preserves the original gesture extraction and timing CV calculation. Fixed intensity thresholds are independent of that timing baseline; this release does not claim baseline-relative intensity or validated gait instability.

## Arduino files and hardware

Keep the existing seven Arduino tabs. This update changes the two `.ino` files and the Wrist `GaitStabilityDetector.h`; the other four headers stay unchanged from the preceding protocol-v2 release. See [ARDUINO_UPDATE.md](ARDUINO_UPDATE.md) for the copy/paste update map. No new Arduino tab is required: the independent `VigorousMotionDetector` class is appended to the Wrist `GaitStabilityDetector.h` after the timing class.

- Foot: XIAO ESP32-C3 (COM3 in this setup), LSM6DS3TR-C at `0x6A`.
- Wrist: Adafruit Feather ESP32-S3 No PSRAM (COM4), DRV2605L and existing ERM motor.
- Both monitors: 115200 baud. Both matching protocol-v2 sketches must be uploaded.
- I2C still uses `Wire.begin()` with board defaults. Core 2.0.17: XIAO C3 SDA=GPIO6/D4, SCL=GPIO7/D5; Feather S3 No PSRAM SDA=GPIO3, SCL=GPIO4. Keep actual working wiring.
- Libraries: Adafruit LSM6DS 4.7.4, Adafruit DRV2605 Library 1.2.4, Adafruit Unified Sensor 1.1.15, Adafruit BusIO 1.17.4. Wire and BLE come from the ESP32 board package.

## Calibration: 20 valid intervals

The Foot gesture extractor polls the real IMU about every 10 ms, with a 104 Hz sensor rate, 4 g acceleration range, and 500 degrees/second gyro range. It computes `dev = sqrt(ax^2+ay^2+az^2)-9.80665` and angular-rate magnitude.

A gesture arms after 120 ms with `|dev| < 0.8 m/s^2` AND angular rate < 0.30 rad/s. An armed gesture triggers after at least 20 ms with `|dev| > 2.5 m/s^2` OR angular rate > 1.0 rad/s, and at least 350 ms since the previous event. Another quiet hold is needed for another event. `MOVE_NOW`, `HOLD_STILL`, `armed`, and `events` relate to this calibration extractor.

Foot starts with continuous sample transmission OFF. Send `c` to Foot before sending `r` to Wrist. Wrist `r` stops the motor, clears calibration, and starts again. The first event is a time reference. Twenty accepted intervals form four non-overlapping five-interval windows; at least 21 detected and received gesture events are needed. Calibration accepts 350-2500 ms intervals. Gaps/no event for 2800 ms break timing continuity without erasing valid progress. Missed gesture sequences also break continuity. Baseline timing CV is the mean of the four window CVs and remains in RAM until `r`, reset, power loss, or upload.

On `BASELINE_READY`, Wrist stops feeding gesture intervals into the timing detector. Send `m` to Foot to enable continuous sample transmission. The learned timing CV remains available, but is not converted into acceleration/gyro thresholds. Gesture pauses/loss after calibration do not interrupt continuous motion analysis.

## New continuous data path

When enabled with Foot `m`, Foot sends actual magnitude samples about every **20 ms (50 Hz)**, independently of gesture events. Foot `c` stops only continuous sample transmission; local IMU sampling, gesture extraction, and heartbeats continue. After a gesture notification, the sample and heartbeat timers restart so they do not notify in the same loop iteration. No random motion values, forced events, or synthetic alarm inputs are generated. `esp_random()` is used only for a boot identity.

Both `HandTestProtocol.h` copies use version 2 and the same 20-byte little-endian packet:

| Field | Bytes | Meaning |
| --- | --- | --- |
| magic, version, type | 4 | Magic `0x5347`, version 2; gesture=1, heartbeat=2, motion sample=3 |
| bootSession | 4 | Restart identity |
| sequence | 4 | Gesture sequence for gestures/heartbeats; independent sample sequence for motion samples |
| timestampMs | 4 | Foot clock, not BLE arrival time |
| accelerationDeviationCenti | 2 | Signed `(|a|-g)*100`, m/s^2 |
| angularRateCenti | 2 | Unsigned angular-rate magnitude *100, rad/s |

UUIDs are unchanged. A v1 sketch is incompatible with v2 packets: update both boards, not just Wrist. The receiver logs `NO_COMPATIBLE_FOOT_DATA_CHECK_BOTH_V2` if compatible traffic is missing. Motion samples bypass gesture-sequence bookkeeping and cannot advance calibration.

## Post-calibration decision

`VigorousMotionDetector` uses non-overlapping **500 ms** windows of real samples. Nominal coverage is 25 samples; at least 20 are required. RMS values are computed over the samples in each window. The boundary sample belongs only to the next window.

Initial adjustable engineering thresholds:

| Parameter | Value |
| --- | --- |
| AccelEnter | 3.0 m/s^2 RMS of `|a|-g` |
| GyroEnter | 3.0 rad/s RMS of angular-rate magnitude |
| AccelExit / GyroExit | 1.5 in the corresponding units |
| EnterWindows | 4 consecutive high windows |
| ExitWindows | 4 consecutive low windows |

A window is high if **either** RMS exceeds its entry limit. A window is low if **both** RMS values are below their recovery limits. Four consecutive high windows enter `UNSTABLE` and issue one alarm. Four low windows recover; intermediate values retain an ongoing abnormal state. In the pre-alarm phase, a non-high window clears consecutive-high evidence. Fixed limits should be tuned from actual ordinary/vigorous `MOTION_WINDOW` logs. Smooth fast motion can also be high intensity; this is not a classifier specific to irregular shaking.

## Data status and decision status

| `state` | Meaning |
| --- | --- |
| CALIBRATING | Timing baseline is not complete |
| WAITING_FOR_DATA | No usable post-calibration sample stream |
| COLLECTING_WINDOW | First adequate half-second window is still being assembled |
| STABLE | No confirmed vigorous-motion episode |
| SUSPECT | One to three consecutive high windows |
| UNSTABLE | Four consecutive high windows confirmed the episode |

`data_state` is separate: NO_DATA, STREAMING (during calibration), COLLECTING_WINDOW, or READY. `event_activity=NO_RECENT_EVENTS` does not mean missing IMU samples. Quiet real data can correctly be STABLE. Each `MOTION_WINDOW` prints measured RMS values, entry limits, and consecutive-window counts; `STATUS` prints window progress, sample count, sample loss, and alarm latch.

## Missing data and alarm acknowledgement

Duplicate/out-of-order samples do not add evidence. Small losses are tolerated only if timing and coverage remain adequate. A sample-time gap >80 ms, a sequence jump >4, or inadequate window coverage clears pending evidence and starts a fresh window. If continuous samples stop for >500 ms, data becomes unavailable even if heartbeats continue. Missing compatible packets for 2500 ms triggers reconnection. Queue overflow, disconnection, and boot changes invalidate current motion evidence and retain the completed timing baseline. Evidence cannot span a data interruption.

An automatic alarm switches the DRV2605L to real-time playback at `0x40` until acknowledged. `s` writes RTP zero, stops waveform playback, restores internal-trigger mode, and retains calibration/decision state. It does not force STABLE or immediately retrigger the same ongoing episode. `r` additionally clears calibration. `v` is one short motor test and leaves an existing latched alarm alone. Recovery/data loss does not silence the latch. While disconnected with a latched alarm, reconnection scans wait for acknowledgement so they cannot block `s`.

## Verification

The preceding protocol-v2 release was compiled with Arduino CLI 1.3.1, ESP32 core 2.0.17, and the listed libraries for the primary boards. The current 20-interval edits have been checked with host simulation; a fresh ESP32 target build is not available in this environment. The independent timing suite and the updated gesture suite cover their respective calibration targets. Eighteen vigorous-motion tests cover RMS, persistence, hysteresis, transients, coverage, losses, rollover, sample/gesture separation, and continuous shaking with only one gesture event. A host integration check runs the actual Wrist sketch through calibration, continuous alarm, s/r/v, recovery, queue overflow, missing samples with healthy heartbeats, and disconnected stop using mocked hardware APIs. The integration check also verifies 20-interval calibration using only gesture events and heartbeats before testing interleaved continuous samples. These tests do not verify physical motor strength, the user's actual gesture RMS, or ESP32 radio behavior.

```sh
g++ -std=c++11 -Wall -Wextra -Werror -pedantic firmware/tests/vigorous_motion_test.cpp -o /tmp/vigorous_test
/tmp/vigorous_test
g++ -std=c++11 -Wall -Wextra -Werror -pedantic -Ifirmware/tests/host firmware/tests/motion_demo_flow_test.cpp -o /tmp/motion_flow_test
/tmp/motion_flow_test
```

Original raw-IMU sketches remain in `foot_full_ble_test/` and `wrist_full_system_test/`. The optional `algorithm_demo/gait_stability_sim/` remains a separate synthetic timing experiment with its own commands. The dashboard adapter, actual running-event validation, and gait instability classification are not implemented by this hand-motion demo.
