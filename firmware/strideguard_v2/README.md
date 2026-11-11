# StrideGuard V2 walking firmware

Use this build for shoe-mounted testing and app integration. The foot pod sends V2 gait packets to the wrist, which drives the motor and logs live data over USB. The latest wrist build was observed receiving live foot data in Walking mode.

## Flash and check

| Device | Arduino sketch | Board |
| --- | --- | --- |
| Foot pod | `foot_gait_monitor/foot_gait_monitor.ino` | Seeed XIAO ESP32C3 |
| Wrist | `wrist_gait_monitor/wrist_gait_monitor.ino` | Adafruit Feather ESP32-S3 No PSRAM |

Keep the headers beside their respective sketches. An existing V2 foot pod works with this wrist build. Open the wrist serial monitor at 115200 baud and check for `build=V2_BLE_FIX`, `FOOT_CONNECTED protocol=V2`, and `acquisition=RUNNING`. Confirm remote control with `s` and a successful ACK. See [the walking test instructions](README_CN.md) for calibration and axis selection.

## App integration

Connect the phone to a phone-facing BLE service on the wrist. The wrist-to-phone service is the integration step still to implement in this build. Agree on its service and notification characteristic UUIDs with the app owner.

Use [GaitEventProtocol.h](wrist_gait_monitor/GaitEventProtocol.h) as the V2 packet reference. Each packet is 20 bytes. Start by forwarding the three alert kinds, then forward status, baseline, and contact packets for the live session and summary. Reuse the alert event the wrist already handles for haptics.

| Alert | V2 kind | Wrist pulses |
| --- | --- | --- |
| Timing deviation | 5 | 1 |
| Hard landing | 6 | 2 |
| Suspected sudden roll | 7 | 3 |

For the first integration check, a hard landing should produce two wrist pulses, one live app alert, and one hard-landing entry in the session summary. Use `bootSession` and `sequence` to deduplicate alerts.
