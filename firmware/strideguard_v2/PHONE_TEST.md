# Test the phone link

Upload only `wrist_gait_monitor/wrist_gait_monitor.ino` from this branch to the Adafruit Feather ESP32-S3 No PSRAM. The V2 foot pod uses the existing foot firmware. Power both boards and open the wrist serial monitor at 115200 baud.

The wrist prints `build=V2_PHONE_BRIDGE` and `# PHONE ADVERTISING`. In the app, scan for the wrist service below, connect, discover the characteristic, and enable notifications.

| App setting | Value |
| --- | --- |
| Wrist device name | `StrideGuard_Wrist_Gait_V2` |
| Service UUID | `87654321-4321-4321-4321-BA0987654321` |
| Characteristic UUID | `FEDCBAFE-4321-4321-4321-FEDCBAFEDCBA` |
| Characteristic properties | Read and Notify |

Once the wrist is receiving valid foot data, the app should receive a 20-byte V2 Status packet (kind 4) about once per second. Its first four bytes are `47 53 02 04` in hex. The first requested notification is logged as `# PHONE NOTIFY_REQUESTED bytes=20`. Confirm reception on the phone itself.

The wrist forwards contact, baseline, status, alert, and acknowledgement packets. Use [the V2 header](wrist_gait_monitor/GaitEventProtocol.h) to decode them. The raw IMU stream remains on USB. For the end-to-end check, verify that a hard landing produces two wrist pulses and a kind 6 packet in the app.

Host tests check forwarding and the existing wrist behavior. Verify Arduino compilation and the dual BLE link on the physical boards.
