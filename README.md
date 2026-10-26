# StrideGuard

ECE 590 running-gait monitoring prototype: a shoe-mounted IMU and XIAO ESP32-C3 send data over BLE to an Adafruit ESP32-S3 Feather wrist unit with a DRV2605L haptic driver. The intended system monitors sustained deviation from a runner's initial baseline and presents the same processed information on a mobile-friendly dashboard.

## Current projects

| Directory | Purpose | Current status |
| --- | --- | --- |
| `firmware/foot_hand_motion_test/` | Real foot IMU gesture events sent over BLE | Sensitive bench-test transmitter with trigger diagnostics; uses board-default I2C; C3 and Feather S3 compilation verified |
| `firmware/wrist_hand_motion_test/` | Real-event timing baseline, deviation detection, and haptics | Current bench-test receiver; Feather ESP32-S3 No PSRAM compilation verified |
| `firmware/foot_full_ble_test/` | Foot IMU acquisition and BLE notification server | Original full hardware-test source preserved |
| `firmware/wrist_full_system_test/` | BLE receiver, wrist IMU, and manual vibration test | Original full hardware-test source preserved; no automatic gait detector integrated |
| `algorithm_demo/gait_stability_sim/` | Independent baseline-relative stride-timing simulation | Software detector tested on generated events; no real IMU input |
| `app/` | Web dashboard (React + TypeScript) | Demo sessions and USB processed-metric input; not yet connected to the firmware's raw output |

## Start here: move the real foot module by hand

Follow the [two-window hand-motion guide](firmware/HAND_MOTION_TEST.md). Open `firmware/foot_hand_motion_test/foot_hand_motion_test.ino` in the foot window and `firmware/wrist_hand_motion_test/wrist_hand_motion_test.ino` in the wrist window. Upload both to their actual boards. Keep all accompanying headers in their respective sketch folders.

This test reads the real foot IMU and sends gesture events over BLE; it does not require a command to generate abnormal data. After `LINK_READY`, send `r` to the wrist, perform about 31 evenly spaced move-and-hold gestures to establish 30 timing intervals, then vary the gesture timing to test the alert. Use 115200 baud. It is a hand-motion bench test, not validated running-gait detection. Pauses and missing events now preserve calibration progress and the baseline; only valid intervals count. Use `r` to deliberately start over. If updating the older downloaded revision by hand, see the [copy/paste calibration fix](firmware/CALIBRATION_QUICK_FIX.md).

## Optional synthetic-input algorithm simulation

Open `algorithm_demo/gait_stability_sim/gait_stability_sim.ino` in Arduino IDE. Keep `GaitStabilityDetector.h` in the same folder. Upload to the actual wrist board using the team's validated board/USB settings. No foot node, IMU, or motor is needed with the default settings.

Open Serial Monitor at **115200 baud**, wait about **22.5 seconds** for `BASELINE_READY`, then send `u` for sustained irregular timing, `n` for recovery, or `s` for a single disturbance. See the [algorithm guide](algorithm_demo/README.md) for parameters, commands, validation, and optional motor output.

This is a separate sketch. Uploading it replaces the wrist's running BLE firmware. To restore the hardware test, upload `firmware/wrist_full_system_test/wrist_full_system_test.ino` again. Keep every sketch independent; never combine their `setup()` and `loop()` functions.

## Original hardware tests

Open each `.ino` from its own matching folder under `firmware/`. Select the actual board and port separately. The foot source assumes XIAO ESP32-C3 with D4/D5 for I2C. The wrist uses the selected board's default I2C pins. Both require an IMU at `0x6A`; the wrist also requires its DRV2605L before starting BLE.

Start the foot first, then reset the wrist. The wrist scans once for five seconds. Use **115200 baud** and send `v` to the wrist for the existing manual vibration test. The current full firmware streams raw six-axis readings, with no gait-event extraction or automatic alert logic. Reliable reconnection, real feature extraction, and dashboard integration remain to be implemented.

## Web app and VS Code

Open `StrideGuard.code-workspace` for the whole repository. Its default terminal opens in `app/`. With Node.js 24 LTS installed:

```sh
npm ci
npm run dev
```

The workspace also provides app build/test tasks and browser debugging. See the [desktop development guide](app/VS_CODE.md) and [app data format](app/README.md).

## Repository cleanup

The nine early component-only sketches were removed from the current tree. Their source remains in Git history. The two full hardware-test sketches were moved into Arduino-compatible directories without changing their contents. Keep repository filenames, code, comments, documentation, and workspace configuration in English.
