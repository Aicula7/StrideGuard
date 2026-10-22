# StrideGuard — React development build

A Windows-friendly gait-monitoring interface. Built with React, TypeScript and Vite. This is a new implementation of the agreed session flow, informed by the seven paper screens in APP_Paper.docx and the supplied prototype screenshots. It is not an export of the inaccessible Figma Make source.

## Run on Windows in VS Code

Install Node.js 22.13 or later (Node.js 24 LTS is suitable), then open this folder in VS Code. Open Terminal → New Terminal and run:

```sh
npm ci
npm run dev
```

Open **http://localhost:4173** in Chrome or Edge. You do not need Android Studio, Kotlin, Expo Go, a phone or an OpenAI API key to run this project. If PowerShell blocks npm.ps1, run the same commands with `npm.cmd` or use Command Prompt; you do not need to change your computer's execution policy.

## Try the demo

1. Home → Start demo session.
2. The active timer, simulated cadence, stride timing, impact and estimated steps update.
3. Enable **Irregular timing** under Demo controls. After variability stays above the selected threshold for five seconds, an on-screen timing alert appears. Alerts have a 30-second cooldown. You can dismiss or pause from the alert.
4. Pause / resume, then End session → Save session.
5. Open History to review, export JSON / CSV, or delete a session. Refresh to verify persistence.

Demo data is labeled everywhere. History starts empty. No demo values are presented as measurements from hardware. Wristband control is not implemented, and the app never claims to send vibration commands.

## Receive real data from an ESP32

This build implements Web Serial directly in the browser. Hardware integration has **not** been tested with your physical board. USB serial requires a supported desktop browser and a secure context (HTTPS or localhost); it may be restricted inside an embedded preview, so open a standalone browser window.

1. Connect the ESP32 using a data-capable USB cable.
2. Close Arduino Serial Monitor and any other app that owns the COM port.
3. Devices → USB sensor. Set the baud rate to match the firmware (default 115200).
4. Click Connect sensor and select the board's port.
5. Wait for valid JSON lines, then start a session from Home.

The expected stream is one **processed metrics JSON object followed by a newline**:

```json
{"cadence":112,"strideTime":1.07,"impact":1.8,"variability":3.2,"steps":42,"alert":false}
```

| Field | Meaning / unit | Optional alias |
| --- | --- | --- |
| cadence | steps per minute; range 0–400 | cadence_spm |
| strideTime | seconds between consecutive strikes of the **same foot**; range 0–10 | stride_time_s |
| impact | acceleration magnitude in **g**; range 0–100 | impact_g |
| variability | timing variability in **percent**; range 0–100 | variability_pct |
| steps | cumulative non-negative integer counter | step_count |
| alert | boolean true = sensor reports an alert | — |

All metric fields are optional, but at least one must be present and valid. Missing values show a dash. Debug text and malformed or out-of-range packets are ignored. JSON objects must be newline-delimited; one object may span multiple incoming USB chunks. Individual lines are capped at 32 KiB. The latest packet replaces displayed metrics rather than carrying forward absent fields. Values more than three seconds old are hidden.

### Minimal firmware output example

Call this using the values your analyzer actually produces. This snippet is only the **serialization adapter**; it does not calculate or validate gait metrics. Do not insert invented numbers into production sensor output.

```cpp
void sendGaitMetrics(float cadenceSpm, float strideTimeSeconds,
                     float impactG, float timingVariabilityPercent,
                     unsigned long totalStepCount, bool timingAlert) {
  Serial.printf("{\"cadence\":%.2f,\"strideTime\":%.4f,\"impact\":%.3f,"
                "\"variability\":%.2f,\"steps\":%lu,\"alert\":%s}\n",
                cadenceSpm, strideTimeSeconds, impactG,
                timingVariabilityPercent, totalStepCount,
                timingAlert ? "true" : "false");
}
```

Confirm the units first. For a sensor mounted on one foot, cadence in steps/min is approximately `120 / strideTimeSeconds`, rather than `60 / strideTimeSeconds`. This relationship depends on what your analyzer calls a stride; check its event definition before using it. Convert m/s² to g before supplying impact if needed. The website does not infer cadence from raw ax/ay/az or gx/gy/gz values. Send only fields your analyzer actually has.

## Session semantics and limitations

- Pausing stops session sampling and excludes paused time. The USB stream may continue to update the device state.
- Hardware steps are counted relative to the counter at session start / the first received count. Counter resets do not add pre-session steps. Hardware steps show a dash when no step counter is provided.
- Demo steps are estimates obtained by integrating simulated cadence.
- The prototype timing threshold defaults to 10%, configurable in Me. An alert requires reported variability above the threshold for five seconds, or a rising sensor `alert:true` flag. It is not a validated health or injury classifier. One alert at most every 30 active wall-clock seconds. No wristband command is sent.
- Sessions record up to 7,200 metric samples, at most one sample per second for USB input; chart shows the latest 90. Summary averages describe the retained samples, not a validated complete raw signal archive. Step updates are processed independently of this sampling limit.
- History and preferences use localStorage in the current browser, with the latest 30 saved sessions retained. There is no account, cloud sync or server database. If storage fails, the app reports it and offers export from the session summary. Closing/reloading loses an active, unsaved session.
- Bluetooth / BLE and wristband commands are not included. Add them after the team's service UUIDs, characteristic UUIDs and packet format are known.
- Starting another session before saving the previous summary discards the unsaved summary.

## Project map

| File | Purpose |
| --- | --- |
| src/App.tsx | Screens, session lifecycle and USB serial integration |
| src/core.js | Packet validation, stream framing, timing and summary helpers |
| src/index.css | Desktop and mobile layouts |
| src/main.tsx | React entry point |
| tests/core.test.mjs | Parser, framing, pause accounting, step-counter and summary checks |

```sh
npm test
npm run build
```

## Collaborating with Codex

Open the entire project folder in VS Code. Install the official OpenAI Codex extension if you want a local coding assistant to read and edit these files. A browser chat does not automatically see your local VS Code folder or ESP32. Use your own GitHub repository to share changes with teammates and future coding sessions. Keep node_modules and dist out of Git; package.json / the lockfile describe the dependencies.

## Credits and storage

Icons: Lucide (ISC license), installed via npm. Google Fonts are optional network requests; system fonts are the fallback. The hosted development website is private by default. Local runs work without a login; only the development server is required.
