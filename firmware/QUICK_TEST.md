# StrideGuard: Vigorous-Motion Demo

Real Foot IMU -> BLE -> sustained motion intensity -> Wrist vibration.
This is a fixed-threshold bench demo, not a gait/fall diagnosis.

## Setup

| Device | Sketch under `firmware/` | Board / port in this setup |
| --- | --- | --- |
| Foot | `foot_hand_motion_test/foot_hand_motion_test.ino` | XIAO_ESP32C3 / COM3 |
| Wrist | `wrist_hand_motion_test/wrist_hand_motion_test.ino` | Adafruit Feather ESP32-S3 No PSRAM / COM4 |

Upload **both matching v2 sketches and headers**. Open both Serial Monitors at **115200 baud**. Expect `vigorous-motion-v2 protocol=2`, Foot `connected=1`, and Wrist `link=1`.

## 1. Reset and calibrate as before

In **Wrist**, enter **`r`** once. Expect `0/30`. Optional: `v` tests one short vibration.

Use separate tilt-and-hold gestures: tilt once, stop until `MOVE_NOW`, tilt back, stop again. One direction is one event. Aim for an even 1.5-second rhythm (40 BPM), as before.

- First event is a reference: `0/30`.
- The next 30 valid intervals complete `30/30`; normally about 31 gestures.
- Pauses retain progress; calibration intervals remain 350-2500 ms.
- Expect `BASELINE_READY` and `Switching to CONTINUOUS VIGOROUS MOTION detection`.

The original timing baseline is retained. Its CV is **not** used to set the new fixed motion thresholds. `BASELINE_NOISY` reports timing quality only and does not block the motion demo.

**Do not send `r` again unless you want to erase and repeat calibration.**

## 2. Show stable motion

Hold the Foot module still or move it gently for 2-3 seconds. Continuous samples are sent even when `events` stays fixed.

Expect `data_state=COLLECTING_WINDOW` initially, then half-second `MOTION_WINDOW` lines with `state=STABLE`. Stillness with valid samples is stable, not missing data.

## 3. Show vigorous motion

Hold the IMU-bearing module securely and move it back and forth continuously for about 3-4 seconds, without pulling its wires. **No quiet holds, metronome, or event timing are needed after baseline.**

Each non-overlapping 500 ms window calculates:

- `accel_rms_mps2`: RMS of acceleration magnitude minus gravity.
- `gyro_rms_rps`: RMS of angular-rate magnitude.

A high window has acceleration RMS **> 3.0 m/s^2 OR** gyro RMS **> 3.0 rad/s**. Four consecutive high windows (about 2 seconds of qualifying data, allowing a transition window) produce:

```text
state=SUSPECT high_windows=1/4
state=SUSPECT high_windows=2/4
state=SUSPECT high_windows=3/4
state=UNSTABLE high_windows=4/4
VIGOROUS_MOTION_ALERT
ALARM_LATCHED
HAPTIC_COMMAND_SENT mode=CONTINUOUS
```

Confirm physical motor vibration. A short shake may make `SUSPECT` without an alarm. Merely changing gesture timing no longer triggers this detector.

## 4. Stop and recover

Enter **`s` in Wrist**: vibration stops and baseline remains `30/30`. This acknowledges the alarm; it does not force `STABLE`.

Return to stillness/gentle motion. Four consecutive windows with **both** RMS values below **1.5** recover to `STABLE`. An unacknowledged motor alarm continues even after recovery. A fresh episode after recovery can trigger again.

## Read the diagnostics

| Output | Meaning |
| --- | --- |
| `state=CALIBRATING` | Original timing baseline is incomplete |
| `data_state=NO_DATA` / `state=WAITING_FOR_DATA` | No current usable motion data; not a stable judgment |
| `data_state=COLLECTING_WINDOW` | Samples are arriving; first full window is not ready |
| `data_state=READY` | A completed window has produced a judgment |
| `event_activity=NO_RECENT_EVENTS` | No recent tilt event; normal during continuous shaking |
| `MOTION_DATA_GAP` | Incomplete coverage/burst loss; pending evidence restarted |
| `motion_samples_rx` | Received continuous sample count, independent of `events_rx` |
| `s` / `r` / `v` | Stop alarm / stop and recalibrate / one short motor test |

## Tune only after reading actual windows

Starting thresholds are engineering demo values, not validated or learned intensity limits. In the **Wrist** `GaitStabilityDetector.h`, find `class VigorousMotionDetector` and adjust `AccelEnter` / `GyroEnter`. Use quiet/gentle and vigorous `MOTION_WINDOW` measurements to choose a separation. A normal fast rotation may also be high intensity.

Missing motion samples, disconnection, or queue overflow reset pending intensity evidence and retain baseline/alarm. A long sampling gap cannot count toward a sustained alarm. Wrist reset/power loss/upload clears the RAM baseline.
