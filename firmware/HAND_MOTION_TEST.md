# Real foot-module hand-motion test

Use this version when the foot module can only be moved by hand. It validates the path **real foot IMU -> gesture extraction -> BLE -> timing-deviation detector -> wrist haptic command**. It does not synthesize sensor input or classify actual running gait. The wrist IMU is deliberately not used for this test.

## The two Arduino windows

| Window | Open this sketch | Upload to |
| --- | --- | --- |
| Left / foot | `firmware/foot_hand_motion_test/foot_hand_motion_test.ino` | Actual foot board: proposal uses XIAO ESP32-C3; Feather ESP32-S3 compilation is also supported |
| Right / wrist | `firmware/wrist_hand_motion_test/wrist_hand_motion_test.ino` | Actual Adafruit ESP32-S3 Feather variant |

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

## Baseline, disturbance, and recovery

Send **`r` to the wrist** to clear timing history. Make separate gestures at a steady rhythm, about once per second. A one-second metronome can help. Each gesture should include a still hold before the next movement.

- The first event provides a reference timestamp.
- The next **30 intervals** build the baseline: approximately 31 gestures / 30 seconds at this pace.
- Wrist output shows `CALIBRATION intervals=.../30` and then `BASELINE_READY`.
- If `BASELINE_NOISY` appears, use `r` and repeat with more even timing. A noisy baseline raises the detection threshold.

Keep gesture shape and strength similar, but make the **intervals** uneven: for example alternate about 0.5 seconds and 1.4 seconds. Do not stop for several seconds. After two high-variability, non-overlapping five-interval windows, wrist output becomes `UNSTABLE` and prints `HAND_TIMING_ALERT`; an initialized driver also receives the vibration command.

Return to even, roughly one-second gestures. Expect recovery after two complete low-variability windows; allow about 10-15 regular gestures if a transition window is mixed. Continued irregularity does not repeatedly vibrate; alerts occur on entry with cooldown.

The exact experimental thresholds are shared with the [synthetic-input detector](../algorithm_demo/README.md). For each five-interval window, `CV = standard deviation / mean * 100`. Baseline CV is the mean of six calibration-window CVs. Entry is `max(2*baselineCV, baselineCV + 3 percentage points)` for two windows; recovery is `max(1.5*baselineCV, baselineCV + 1.5 percentage points)` for two windows. This tests timing consistency, not a universal normal/abnormal running threshold.

## What the gesture extractor does

The foot computes two real quantities:

- `dev_mps2 = acceleration magnitude - 9.80665`, in m/s^2. This is a simple magnitude-based motion proxy, not full gravity compensation or impact force.
- `gyro_rps = angular-rate magnitude`, in rad/s.

After a quiet hold of 120 ms (`|dev| < 0.8` and `gyro < 0.30`), the detector arms. Motion above `|dev| > 2.5` **or** `gyro > 1.0` for at least 20 ms creates one event. Another quiet hold and at least 350 ms since the previous event are required before another can trigger. This supports a deliberate move-and-hold bench gesture; it is not a validated foot-strike extractor.

Check the foot `SENSOR` lines first if gestures are missed. `armed=0` during continuous movement means there has not been a sufficiently quiet hold. Very slow rotation can remain below the activation threshold. If one intended gesture generates multiple events, simplify it to one movement followed by a hold before tuning the timing algorithm. Threshold constants are in `HandMotionDetector.h`.

## Data integrity and stopping

- Only gesture packets feed the timing detector. Heartbeats do not become fake steps.
- A 20-byte versioned packet carries a boot identity, event sequence, foot timestamp, and measured motion magnitudes. The boot identity is random only to identify a restarted foot; motion data are not random.
- Sequence gaps, a foot restart, a receive-queue overflow, or disconnection reset timing calibration.
- No valid packet for 2.5 seconds triggers reconnection, not a gait alert.
- No gestures for 2.8 seconds, or an event interval over 2.5 seconds, resets the baseline. Resume a regular sequence to calibrate again. The wrist can still receive healthy heartbeat packets while the foot is stationary.
- The baseline uses foot event timestamps, not BLE arrival timing.

Wrist commands are **`r`** (recalibrate), **`v`** (manual motor test), and **`?`** (help). There is no `u` command to manufacture abnormal input in this real-foot version.

## Validation and remaining limits

Both new sketches successfully compiled for the listed ESP32 targets. Fourteen additional portable C++ tests cover stationary noise, gesture extraction, spike rejection, rearming, a regular/irregular sensor-waveform pipeline, heartbeat handling, missing events, restarts, packet validation, and nonfinite inputs. Those waveform fixtures are synthetic test inputs, not recorded human measurements. The original detector's 11 tests remain separate.

Run the hand-motion tests from the repository root on a machine with a C++11 compiler:

```sh
g++ -std=c++11 -Wall -Wextra -Werror -pedantic firmware/tests/hand_motion_test.cpp -o /tmp/hand_motion_test
/tmp/hand_motion_test
```

Physical BLE reconnection, gesture repeatability, and motor output still require bench validation. This version does not implement stance/swing duration, impact analysis, bilateral comparison, the dashboard adapter, or running foot-strike extraction. An actual running evaluation must replace/validate the bench gesture extractor and retune the research parameters using real running data.

## Compilation troubleshooting

- `D4` / `D5` not declared: an older foot sketch used XIAO-specific aliases. Update to this revision, or replace `Wire.begin(D4, D5)` with `Wire.begin()` in that sketch, and confirm the actual wiring matches the selected board's defaults.
- `Adafruit_DRV2605.h: No such file or directory`: install **Adafruit DRV2605 Library** by Adafruit from Arduino Library Manager, including dependencies. Do not change the include filename.
- `Adafruit_LSM6DS3TRC.h: No such file or directory`: install **Adafruit LSM6DS** by Adafruit, including Adafruit Unified Sensor and Adafruit BusIO dependencies.
- A successful serial monitor connection alone does not establish board identity or a successful upload. Confirm the actual board model or the chip reported by the upload tool.
