# Real foot-module hand-motion test

Use this version when the foot module can only be moved by hand. It validates the path **real foot IMU -> gesture extraction -> BLE -> timing-deviation detector -> wrist haptic command**. It does not synthesize sensor input or classify actual running gait. The wrist IMU is deliberately not used for this test.

## The two Arduino windows

| Window | Open this sketch | Upload to |
| --- | --- | --- |
| Foot window | `firmware/foot_hand_motion_test/foot_hand_motion_test.ino` | XIAO ESP32-C3, shown on COM3 in the team's setup |
| Wrist window | `firmware/wrist_hand_motion_test/wrist_hand_motion_test.ino` | Adafruit Feather ESP32-S3 No PSRAM, shown on COM4 in the team's setup |

Download and fully extract the repository ZIP first. In each Arduino IDE window, close the old Serial Monitor, then use **File > Open** for the corresponding new `.ino`. The IDE may open another window; upload only from the new hand-test windows. Keep every `.h` beside its matching `.ino`. Do not copy all headers or both sketches into one folder.

Both boards need their new hand-test sketch. These sketches use a separate BLE UUID from the original full-system tests and will not connect to an old raw-data sketch. Windows Bluetooth pairing is not required; the two firmwares connect directly.

Original sketches remain under `foot_full_ble_test/` and `wrist_full_system_test/` and can be uploaded again to restore the earlier hardware tests. `algorithm_demo/gait_stability_sim/` is the older synthetic-input experiment, not the real-foot test.

## Wiring and dependencies

Keep the team's working wiring. The foot source uses `Wire.begin()` and prints the selected board's default SDA/SCL GPIO numbers. In core 2.0.17, XIAO ESP32-C3 defaults are SDA=GPIO6 (D4), SCL=GPIO7 (D5); Adafruit Feather ESP32-S3 No PSRAM defaults are SDA=GPIO3, SCL=GPIO4. Wire the actual board's SDA/SCL pins accordingly. If existing wiring uses different GPIOs, specify those actual GPIO numbers in `Wire.begin(sdaGpio, sclGpio)`; do not guess from a different board's labels. The selected board must match the physical MCU, even if a program compiles for both targets.

The foot requires an LSM6DS3TR-C at address `0x6A`, uses 4 g accelerometer range and 500 degrees/second gyroscope range. The IMU output data rates are 104 Hz and the firmware polls approximately every 10 ms. Timing is best-effort, not a precision synchronized acquisition system.

The wrist uses `Wire.begin()` with the selected board's default SDA/SCL, matching its original hardware test. Its DRV2605L drives the existing ERM motor. If the driver is missing, the program continues with serial alerts and explicitly reports no motor command.

Verified compilation configuration:

- Arduino CLI 1.3.1, Espressif Arduino core **2.0.17**.
- Foot: both `esp32:esp32:XIAO_ESP32C3` and `esp32:esp32:adafruit_feather_esp32s3_nopsram`.
- Wrist: `esp32:esp32:adafruit_feather_esp32s3_nopsram`, matching the No PSRAM selection shown in the team's IDE screenshot. Use the actual board variant if different.
- Adafruit LSM6DS 4.7.4, Adafruit DRV2605 Library 1.2.4, Adafruit Unified Sensor 1.1.15, Adafruit BusIO 1.17.4.

`Wire` and the ESP32 BLE library are included with the ESP32 board package. Do not install an unrelated library with the same BLE header names. Compilation with other core versions has not been checked.

## Upload and connection

1. Upload the foot sketch to the foot board's actual COM port. Open Serial Monitor at **115200 baud**. Expect `FOOT_READY` and periodic `SENSOR` messages.
2. Keep the foot powered and upload the wrist sketch to its separate COM port. Open its Serial Monitor at **115200 baud**. Expect `LINK_READY`. The wrist automatically retries discovery after connection loss.
3. In the wrist monitor, send `v` to check the motor separately. `HAPTIC_COMMAND_SENT` means the driver command was sent; observe the physical motor to confirm motion.
4. Hold the foot module still for at least half a second. Then perform one quick, moderate tilt or short movement, followed by a still hold. Do not pull on its USB cable or continuously shake it.
5. Confirm **one intended movement gives one `HAND_EVENT`** on the foot and one `REAL_HAND_EVENT` on the wrist. These are bench gestures, not detected running foot strikes.

## Why changing sensor values can still leave the baseline at zero

The supplied screenshot shows `connected=1` on the foot and `link=1` on the wrist. The first two real events occurred at foot timestamps `33001` and `49479` ms, separated by **16478 ms**. Between them the foot repeatedly reported `phase=NEED_QUIET armed=0`, despite large motion peaks. The wrist received both events but could not calculate a valid interval between them. Its pause handling correctly kept calibration at 0/15 in that earlier build. The current target is 30 intervals; the same event/rearm explanation applies.

There are three separate quantities:

| Output | What it counts | What to do |
| --- | --- | --- |
| `SENSOR` numbers | Live acceleration and rotation, including samples from the same movement | Do not count these lines as gestures |
| Foot `HAND_EVENT` / `events` | One detected gesture after quiet rearming | Verify five deliberate tilts give five increments first |
| Wrist `CALIBRATION intervals` | Valid time gaps between received gestures | Keep event spacing within 350-2500 ms; the first event is only a reference |

The `guided-baseline-v1` build adds immediate `MOVE_NOW` and `HOLD_STILL` prompts on the foot, `quiet_ms=.../120` in its status, and `INTERVAL_ACCEPTED ms=...` on the wrist. It retains the original extraction thresholds and calibration rules. It does not manufacture an event every second during continuous waving.

## A repeatable baseline exercise

1. Keep both modules powered with their correct sketches running. Confirm foot `connected=1` and wrist `link=1`.
2. Rest your hand or forearm on the desk, supporting the IMU-bearing module. Hold it still for about one second. Look for foot `MOVE_NOW` or `phase=READY armed=1`. Do not grip bare contacts or strain the wiring.
3. Tilt the module once by roughly **30-45 degrees in about 0.4 seconds**, then stop in that new position for about **one second**. These are starting suggestions, not measured angle/duration checks. The measured angular rate must actually cross 1.0 rad/s for 20 ms, or acceleration deviation must cross 2.5 m/s^2 for 20 ms.
4. Tilt back on the next cycle and stop again. Each direction is its own action: **tilt, hold; tilt back, hold**. Do not wave continuously or immediately return after the first tilt. The quiet hold has a minimum of 120 ms; using about one second makes it easier to perform reliably.
5. Try five cycles first. Foot `events` should increase by five and wrist should receive all five. There is no reason to change sensitivity if these match. If foot stays at `HOLD_STILL`, stop moving until `MOVE_NOW`; larger movements cannot rearm it. If it stays `READY` without counting your tilt, check motion peaks and use a more distinct tilt.
6. Once the action is reliable, send **`r` once to the wrist only**. Then perform the same tilt-and-hold cycle at about **1.5-second onset-to-onset spacing**. A 40 BPM metronome gives one tilt per beat. Do not read or copy logs between every cycle; keep the pattern going for approximately 45 seconds between the first and final events.
7. The first received event leaves `0/30`, the second gives `1/30`, the third `2/30`, and the thirty-first gives `30/30` followed by `BASELINE_READY`. The count refers to intervals, not events. A pause requires a new reference event but keeps previously accepted intervals. Do not send `r` repeatedly.

Expected wrist output after `r`, with approximately 1.5-second event spacing:

```text
# REFERENCE_EVENT: start timestamp only, no interval yet. Next FOOT event must be 350-2500 ms later.
# CALIBRATION intervals=0/30 baseline_ready=0
# INTERVAL_ACCEPTED ms=1500
# CALIBRATION intervals=1/30 baseline_ready=0
# INTERVAL_ACCEPTED ms=1500
# CALIBRATION intervals=2/30 baseline_ready=0
...
# CALIBRATION intervals=30/30 baseline_ready=1
# BASELINE_READY: now vary your HAND gesture intervals while keeping separate gestures.
```

After calibration, try alternating 1.2-second and 2.2-second event spacing for at least 10-15 further intervals, keeping the same distinct tilt and quiet hold. Two high-CV five-interval windows are required to alert, so a single delayed gesture is insufficient. A consistently slower rhythm can still be regular; it is timing variability that this prototype measures. The motor test command `v` checks the actuator separately and does not build a baseline.

## Baseline, disturbance, and recovery

Send **`r` to the wrist** to clear timing history. Use the same distinct move-and-hold gesture that produced events in the earlier successful hardware test. Keep the timing reasonably even; the user's earlier accepted intervals were often around 1.2-1.5 seconds. A metronome may help, but gesture recognition does not require a fixed turn duration. Each gesture should include a still hold before the next movement.

- The first event provides a reference timestamp.
- The next **30 intervals** build the bench-test baseline: approximately 31 gestures; total time depends on the actual accepted gesture spacing. The first gesture is a time reference, not an interval.
- Wrist output explicitly shows `CALIBRATION intervals=30/30` and then `BASELINE_READY`. Periodic `STATUS` lines include `baseline=.../30`, received event count, pause count, and the most recent pause reason.
- Pauses preserve all valid calibration intervals already collected, including a partial five-interval calibration window. On resume, the first event establishes a new reference and does not count as an interval. Thirty valid intervals are still required; pauses are never filled with invented data.
- If `BASELINE_NOISY` appears, use `r` and repeat with more even timing. A noisy baseline raises the detection threshold.

Keep gesture shape and strength similar, but make the **intervals** uneven: for example alternate gesture-onset intervals of about 1.2 seconds and 2.2 seconds, with a quiet hold between movements. You may pause to inspect logs without losing the baseline. After resuming, use a fresh uninterrupted sequence of gestures: two high-variability, non-overlapping five-interval windows are still required before wrist output becomes `UNSTABLE` and prints `HAND_TIMING_ALERT`; an initialized driver also receives the vibration command.

Return to an even gesture rhythm that reliably produces one event per intended movement. Expect detector recovery after two complete low-variability windows; allow about 10-15 regular gestures if a transition window is mixed. The automatic haptic alarm is latched separately from detector state: it continues until acknowledged with `s`, even when the detector recovers. Alerts are generated on episode entry with cooldown.

## Continuous alarm and acknowledgement

In wrist build `latched-alarm-30-v1`, `HAND_TIMING_ALERT` calls `startAlarm()`. An available DRV2605L switches to real-time playback mode and holds RTP level `0x40` for continuous ERM drive. It prints `ALARM_LATCHED` and `HAPTIC_COMMAND_SENT mode=CONTINUOUS`. This is a motor command, not physical confirmation of vibration. Existing wiring, motor response, and drive level still need bench verification.

- Send **`s` to the wrist** to stop the motor and clear only the alarm latch. It writes RTP zero and restores internal-trigger mode. The baseline and detector state are retained. Uppercase `S` also works.
- `s` acknowledges the current episode. The detector does not immediately re-alert while it remains in that same `UNSTABLE` episode. A future alert still requires fresh detector entry, subject to its cooldown.
- Send `r` to stop the alarm and clear/rebuild the baseline. Wrist reset/power loss also clears the in-memory state; startup initializes the driver with zero RTP.
- `v` remains a single short motor test. While an alarm is already latched, `v` leaves it running and asks for `s` instead.
- Timing recovery, motion pauses, and Foot disconnection do not automatically silence a latched alarm. If the link is lost while latched, reconnection attempts wait until acknowledgement so blocking BLE scans cannot delay `s` processing.
- If the driver is unavailable, the software latch and serial alert still work, but the log explicitly says no motor command was sent. `STATUS` includes `alarm_latched=0/1` when the link is active. `alarm_latched=1` can coexist with timing state `STABLE` after recovery.

For a full bench check: verify `v` gives one short click, establish the baseline, produce two consecutive high-CV windows, confirm continuous motor vibration, resume even gestures and observe that the latch remains active, then send `s` and confirm physical stopping with the baseline still at 30/30. Finally verify `v` works again. Do not infer that five gestures alone should trigger the motor: five intervals form one window, and two high windows are required.

The latch revision compiled for Feather ESP32-S3 No PSRAM with core 2.0.17. A host check exercised the actual sketch's haptic functions with a mocked driver and the real timing detector: two high windows start the latch, recovery and interruption retain it, acknowledgement zeros RTP without clearing calibration, a subsequent manual click works, and an unavailable driver produces no motor writes. This does not verify physical motor strength or stopping. For the current 30-interval release, keep the updated wrist sketch and its accompanying headers together; replacing/uploading it restarts the wrist and requires a new RAM baseline.

The exact experimental thresholds are shared with the [synthetic-input detector](../algorithm_demo/README.md). For each five-interval window, `CV = standard deviation / mean * 100`. Baseline CV is the mean of six calibration-window CVs (30 valid intervals). This calibration is intended for the hand test; running-data calibration still needs evaluation. Entry is `max(2*baselineCV, baselineCV + 3 percentage points)` for two windows; recovery is `max(1.5*baselineCV, baselineCV + 1.5 percentage points)` for two windows. This tests timing consistency, not a universal normal/abnormal running threshold.

## What the gesture extractor does

The foot computes two real quantities:

- `dev_mps2 = acceleration magnitude - 9.80665`, in m/s^2. This is a simple magnitude-based motion proxy, not full gravity compensation or impact force.
- `gyro_rps = angular-rate magnitude`, in rad/s.

After a quiet hold of 120 ms (`|dev| < 0.8` and `gyro < 0.30`), the detector arms. The restored original bench settings require motion above `|dev| > 2.5` **or** `gyro > 1.0` for at least 20 ms to create one event. Another quiet hold and at least 350 ms since the previous event are required before another can trigger. This supports a deliberate move-and-hold bench gesture; it is not a validated foot-strike extractor.

The foot extractor has been restored to the first-working settings from commit `f629040`, while retaining I2C compatibility and diagnostics. The wrist still keeps its pause-retention fix and 30-interval target. Do not combine this change with another timing/threshold experiment before verifying the restored event count.

There is no enforced angle or 0.3-second deadline. The suggested tilt-and-hold exercise above is a practical way to cross the measured thresholds and allow rearming. The old user logs show accepted gyroscope magnitudes around 1.0-1.3 rad/s. A sufficiently slow, gentle rotation may remain below the restored activation threshold; this is expected. Every move out or back is a separate gesture. Moving only the MCU while a separately wired IMU stays still will not provide the intended input.

To restore an existing foot sketch by hand, replace the four corresponding constants in `HandMotionDetector.h` with:

```cpp
static constexpr float AccelHigh = 2.5f;
static constexpr float GyroHigh = 1.0f;
static constexpr float AccelQuiet = 0.8f;
static constexpr float GyroQuiet = 0.30f;
```

Keep the 120 ms quiet hold, 20 ms confirmation, and 350 ms minimum event spacing unchanged. Only upload the foot sketch after this edit. Once the wrist reconnects, send `r` once to establish a new baseline. First test five separate actions and check the event counter increments consistently; then perform the complete calibration.

Why the rollback: the intervening profile lowered `GyroQuiet` to 0.15 rad/s, making rearming stricter despite lowering activation thresholds. A regression fixture with 0.20 rad/s held-hand drift could count only one event under that profile but counted every movement with the original profile. This is a software reproduction, not a measurement of the user's present motion. The restored quiet/active thresholds are disjoint and match the original working classifier.

Check the foot `SENSOR` lines first if gestures are missed. `armed=0` during continuous movement means there has not been a sufficiently quiet hold. Very slow rotation can remain below the activation threshold. If one intended gesture generates multiple events, simplify it to one movement followed by a hold before tuning the timing algorithm. Threshold constants are in `HandMotionDetector.h`. The complete updated foot sketch also reports one-second `peak_dev_mps2` and `peak_gyro_rps`, so the periodic snapshot no longer hides brief movement peaks. `phase=READY` means it can accept a sufficiently large movement; `NEED_QUIET` means hold still before another gesture; `CONFIRMING` means the 20 ms high-duration check is in progress; `COOLDOWN` means the 350 ms minimum event spacing has not elapsed. Peaks alone do not establish that the required duration or rearm conditions were satisfied.

## Data integrity and stopping

- Only gesture packets feed the timing detector. Heartbeats do not become fake steps.
- A 20-byte versioned packet carries a boot identity, event sequence, foot timestamp, and measured motion magnitudes. The boot identity is random only to identify a restarted foot; motion data are not random.
- Sequence gaps, a foot restart, a receive-queue overflow, or disconnection clear the event-time reference and pending alert evidence while preserving valid calibration progress and the learned baseline. No interval is calculated across missing data.
- No valid packet for 2.5 seconds triggers reconnection, not a gait alert.
- No gestures for 2.8 seconds marks a pause and preserves calibration. An observed interval over 2.5 seconds is excluded, also without clearing calibration. Resume regular gestures to continue collecting valid intervals. The wrist can still receive healthy heartbeats while the foot is stationary.
- Once calibrated, a pause produces `WAITING_FOR_MOTION`; the next reference event and five valid intervals are needed for a fresh monitoring window. Evidence from opposite sides of a gap cannot be combined into an alert.
- Only `r` or restarting/reflashing the wrist clears its RAM baseline. It is not stored across wrist power loss. Recalibrate deliberately if the intended normal motion pattern changes.
- The baseline uses foot event timestamps, not BLE arrival timing.

To change the target later, edit `BaselineWindows` in `GaitStabilityDetector.h`; `WindowSize=5` and `BaselineWindows=6` produce 30 intervals. The current complete sketch derives progress text from this target. Keep display counts derived from `BaselineIntervalTarget`; do not globally replace numbers in the sketch, since that can corrupt unrelated timing constants.

Wrist commands are **`s`** (stop/acknowledge the continuous alarm, keep baseline), **`r`** (stop and recalibrate), **`v`** (one short manual motor test), and **`?`** (help). There is no `u` command to manufacture abnormal input in this real-foot version. The synthetic sketch uses different commands; its `s` means a single synthetic disturbance, not alarm acknowledgement.

## Validation and remaining limits

Both hand-test sketches have been compiled for the primary targets above. Twenty-five portable C++ tests cover stationary noise, gesture extraction, spike rejection, rearming, a regular/irregular sensor-waveform pipeline, heartbeat handling, missing events, restarts, packet validation, nonfinite inputs, subthreshold rejection under the original profile, diagnostic phases, original gesture counting, slow-motion rejection, held-hand rearming drift, and exact completion after 30 intervals. New fixtures reproduce varying large motion without a full quiet hold, verify the quiet-progress diagnostic, and complete a 30-interval baseline with thirty-one alternating 1.5-second tilt-and-hold cycles. Those waveform fixtures are synthetic test inputs, not recorded human measurements. The shared detector's 16 tests include the reported 2681 ms gap, segmented calibration, baseline retention, explicit reset, and alert-evidence isolation across pauses. A separate minimal patch for the downloaded e246574 revision is documented in [CALIBRATION_QUICK_FIX.md](CALIBRATION_QUICK_FIX.md).

Run the hand-motion tests from the repository root on a machine with a C++11 compiler:

```sh
g++ -std=c++11 -Wall -Wextra -Werror -pedantic firmware/tests/hand_motion_test.cpp -o /tmp/hand_motion_test
/tmp/hand_motion_test
```

Team logs confirm basic real-foot event reception. The updated pause handling and physical motor behavior still need verification on the actual devices. This version does not implement stance/swing duration, impact analysis, bilateral comparison, the dashboard adapter, or running foot-strike extraction. An actual running evaluation must replace/validate the bench gesture extractor and retune the research parameters using real running data.

## Compilation troubleshooting

- `D4` / `D5` not declared: an older foot sketch used XIAO-specific aliases. Update to this revision, or replace `Wire.begin(D4, D5)` with `Wire.begin()` in that sketch, and confirm the actual wiring matches the selected board's defaults.
- `Adafruit_DRV2605.h: No such file or directory`: install **Adafruit DRV2605 Library** by Adafruit from Arduino Library Manager, including dependencies. Do not change the include filename.
- `Adafruit_LSM6DS3TRC.h: No such file or directory`: install **Adafruit LSM6DS** by Adafruit, including Adafruit Unified Sensor and Adafruit BusIO dependencies.
- A successful serial monitor connection alone does not establish board identity or a successful upload. Confirm the actual board model or the chip reported by the upload tool.
