# StrideGuard: Quick Bench Test

Real Foot IMU gestures -> BLE -> timing detection -> Wrist vibration. This is a bench test, not validated walking/running gait detection.

## Setup

| Device | Sketch under `firmware/` | Board / port in this setup |
| --- | --- | --- |
| Foot | `foot_hand_motion_test/foot_hand_motion_test.ino` | XIAO_ESP32C3 / COM3 |
| Wrist | `wrist_hand_motion_test/wrist_hand_motion_test.ino` | Adafruit Feather ESP32-S3 No PSRAM / COM4 |

Upload matching sketches with their headers. Keep both powered. Open both Serial Monitors at **115200 baud**. Confirm Foot `connected=1` and Wrist `link=1`.

## 1. Reset

In **Wrist**, enter **`r`** and press Enter once. Expect `0/30`; any continuous alarm stops. No Foot command is needed.

Optional: enter `v` in Wrist to check one short vibration. It does not build the baseline.

## 2. Make separate gestures

Support your forearm on the desk and hold the IMU-bearing Foot module still until `MOVE_NOW` or `READY armed=1`.

```text
Tilt to one side -> HOLD STILL -> tilt back -> HOLD STILL -> repeat
```

Each direction is one gesture. Start with a 30-45 degree tilt taking about 0.4 seconds; these are suggestions, not programmed angle requirements. After each tilt, stop until the next `MOVE_NOW`.

During calibration, check that each gesture gives one Foot `HAND_EVENT` and one Wrist `REAL_HAND_EVENT`. Continuous waving does not count as separate gestures.

## 3. Build baseline

Use a **40 BPM metronome**: start one tilt on each beat, about **1.5 seconds apart**. Hold still between tilts.

- First event: `0/30` (time reference only).
- Second event: `1/30`.
- With uninterrupted valid events, event 31 completes `30/30` and prints `BASELINE_READY`.
- First-to-last event span: about **45 seconds**.

Record `baseline_cv_pct` and `enter_cv_pct`. If `BASELINE_NOISY` appears, use `r` and repeat more evenly.

**Do not send `r` during the remaining tests.**

## 4. Regular timing

Continue even 1.5-second gestures for about 15 gestures. Expect complete `WINDOW` lines to report `STABLE`, with no automatic alarm.

## 5. Irregular timing

Keep the same gesture shape. Alternate **gesture-start intervals**:

```text
1.0 s, 2.2 s, 1.0 s, 2.2 s, ...
```

Each interval includes the tilt and hold. Do about 16 gestures continuously. Detected intervals must stay within **350-2500 ms**.

Two consecutive five-interval windows with `cv_pct > enter_cv_pct` trigger the alarm. A correctly detected 1.0/2.2-second pattern gives about 34-40% CV; compare this with the learned threshold.

Expect `HAND_TIMING_ALERT`, `ALARM_LATCHED`, and `HAPTIC_COMMAND_SENT mode=CONTINUOUS`. Confirm continuous physical vibration.

## 6. Stop and recover

Enter **`s` in Wrist**. Expect `ALARM_STOPPED`; confirm the motor stops and baseline remains `30/30`.

Resume regular 1.5-second gestures for about 15 gestures. Two low-CV windows recover to `STABLE`; a transition window may require extra gestures. A long pause does not demonstrate recovery.

Vibration stays latched even if timing recovers; acknowledge with `s`. It does not immediately retrigger the same ongoing episode.

## Commands and troubleshooting

| Item | Meaning |
| --- | --- |
| `s` in Wrist | Stop alarm; retain baseline |
| `r` in Wrist | Stop alarm; clear and rebuild baseline |
| `v` in Wrist | One short motor test |
| `NEED_QUIET armed=0` | Hold still until `MOVE_NOW` |
| Values change but `events` stays fixed | No new independent gesture was detected |
| Progress stalls | Check Foot events, Wrist reception, and valid intervals |
| `TIMING_PAUSED` / `WAITING_FOR_MOTION` | Baseline retained; next event is a new reference |
| `HAPTIC_UNAVAILABLE` | No driver command sent; check hardware and `v` |

Pauses retain progress. Wrist reset/power loss/upload clears its RAM baseline. A uniformly slower rhythm can still be regular: this algorithm measures timing variability.
