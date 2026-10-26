# Develop StrideGuard in desktop VS Code

This is a React + TypeScript web app for desktop and mobile browsers. It supports simulated sessions and Web Serial input. It is not packaged as a native Android or iOS app yet.

## First-time setup

1. Install Git, VS Code, and Node.js 24 LTS.
2. Open a terminal in the directory where you want to store the project:

   ```sh
   git clone --branch main https://github.com/Aicula7/StrideGuard.git
   cd StrideGuard
   code StrideGuard.code-workspace
   ```

   If the `code` command is unavailable, use **File > Open Workspace from File** in VS Code and select `StrideGuard.code-workspace`.

3. The workspace opens the whole repository, with its terminal starting in `app/`. Choose **Terminal > New Terminal**, then run:

   ```sh
   npm ci
   npm run dev
   ```

4. Open `http://localhost:4173` in Chrome or Edge on your computer. Keep the terminal running. Edit `src/App.tsx` or `src/index.css` and save to see updates. Press Ctrl+C to stop the server.

If Windows PowerShell blocks `npm.ps1`, use `npm.cmd ci` and `npm.cmd run dev`, or switch your terminal to Command Prompt.

If you already have a clone, review and save your local changes first. Run `git fetch origin` and `git switch main` in that clone, then open the workspace instead of cloning again.

## Checks and debugging

```sh
npm test
npm run build
```

You can also select **Terminal > Run Task** and choose `App: dev`, `App: test`, or `App: build`. For breakpoint debugging, start the dev server first, then press F5 and select `App: Chrome (start dev server first)`. Chrome must be installed on your computer.

## Project map

- `src/App.tsx`: screens, sessions, and USB serial integration.
- `src/index.css`: styling and responsive layouts.
- `src/core.js`: packet parsing, timing, and summaries.
- `tests/core.test.mjs`: existing automated tests.
- `firmware/`: the two original full Arduino hardware-test sketches.
- `algorithm_demo/`: the separate gait timing simulation and detector tests.

## Hardware integration status

Demo sessions run without a board. USB mode requires newline-delimited JSON containing processed gait metrics; see [README](README.md) for the format. Existing firmware mainly outputs raw IMU readings and debug text, so it still needs an adapter. Opening a USB connection alone will not supply cadence or other processed metrics. BLE and wristband vibration control are not integrated into the app yet.

## Commit changes

Review your changes in VS Code Source Control, enter a commit message, commit, and push to your current branch. Keep `node_modules`, `dist`, and secret `.env` files out of Git. Uploading source code to GitHub does not automatically deploy a website.

Keep repository filenames, documentation, source text, comments, and workspace configuration in English.
