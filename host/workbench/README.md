# Browser Workbench

## 1. Objective

Define the future home for browser workbench server code and UI assets.

This matters because the browser is becoming the main human-facing lab instrument, not just a GBC image viewer.

## 2. Current Understanding

Current browser implementation lives in `host/live_lcdcam_stream_viewer.py`.

The workbench should preserve:

- a reliable live monitor
- start/stop/recover controls
- safe-idle behavior
- profile-aware safety display
- pin-level and timing views
- artifact links and export controls
- target-specific decode presets without hardcoding them into the UI core

Confidence level: high that a workbench split is needed; medium for exact frontend/backend file boundaries.

## 3. Unknowns

- Whether the workbench should remain a single Python file for a while or split into server, API, static HTML, CSS, and JS.
- How much state should be saved as project artifacts versus browser local storage.
- How to support multiple targets without making the UI too abstract too early.

## 4. Experiment Results

2026-05-10: Directory added as a non-destructive placeholder. The active viewer remains at `host/live_lcdcam_stream_viewer.py`.

2026-05-10: Added first React + TypeScript + Ant Design frontend skeleton in `host/workbench/frontend/`.

2026-05-10: The Python live backend now serves the built Ant frontend for all non-API routes. The old embedded Python UI has been removed from the served path and source.

## 5. Next Steps

- Extract static assets only after the current UI is stable.
- Keep `/api/frame.bin`, `/api/start`, `/api/stop`, and `/api/status` compatible.
- Add target profile selection after the GBC profile schema is validated.

## Ant Design Frontend

Development flow:

```sh
cd host/workbench/frontend
npm install
npm run dev
```

Production/local backend flow:

```sh
cd host/workbench/frontend
npm run build
```

The Python viewer backend serves `host/workbench/frontend/dist/` at `http://127.0.0.1:8791/` and keeps `/api/*` endpoints on the same origin.

The Vite dev server can still proxy `/api/*` to the Python backend during frontend development, but it is no longer required for the normal live workbench.
