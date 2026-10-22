# StrideGuard

StrideGuard firmware and React app.

- Arduino firmware: the independent `.ino` sketches in this directory.
- React + TypeScript app: [`app/`](app/README.md).
- Desktop VS Code setup: [development guide](app/VS_CODE.md).

Open `StrideGuard.code-workspace` in VS Code, then run the following in its app terminal (Node.js 24 LTS recommended):

```sh
npm ci
npm run dev
```

The app includes a simulated session workflow and Web Serial input for processed gait metrics. Existing firmware output still needs an adapter; BLE and wristband controls are not integrated into the app yet.
