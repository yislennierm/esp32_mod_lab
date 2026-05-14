# Ant Design UI Plan

Purpose: define how the browser workbench should adopt Ant Design without breaking the current GBC live capture path.

Status: canonical UI direction.

Last updated: 2026-05-10.

## 1. Objective

Adopt Ant Design as the UI system for the ESP32-P4 Signal Lab browser workbench.

This matters because the project is becoming a serious investigation instrument, not a one-off viewer. Ant Design is well matched to dense, operational tools: navigation, tables, forms, status badges, descriptions, drawers, alerts, tabs, layout, and data-heavy dashboards.

## 2. Current Understanding

Ant Design is a React component library for enterprise web applications. It provides TypeScript definitions, high-quality components, theming, internationalization, and strong layout/navigation primitives.

The current workbench UI is embedded HTML/CSS/JS inside `host/live_lcdcam_stream_viewer.py`. That worked for fast iteration, but it is becoming the wrong place for a structured instrument UI.

Current hypothesis: keep the Python server and API endpoints, but move the browser UI into a React + TypeScript frontend using Ant Design.

Confidence level: high for Ant Design as a UI system; medium for exact frontend build structure until we decide whether to use Vite, Umi, or another tool.

Generic lab panels must not hardcode the current GBC proof target or its active pixel format.

UI wording policy:

- Dashboard, Graph, Live, Source, Processing, and Destination panels should use neutral labels such as source, destination, frame stream, profile, frame format, and project runtime.
- The top-level navigation should be project/workspace oriented, not the older source-processing-destination phase list. Current labels should read as Projects, Graph, SDK, Signals, Runtime, I/O, Monitor, Artifacts, Profiles, and Logs.
- Global telemetry such as FPS belongs in the Monitor page, not in the persistent header, because not every selected project is a live frame-capture project.
- Target-specific names belong in selected project/profile data, evidence docs, or explicit target modules.
- Pixel formats such as RGB565/RGB666 may appear in technical metadata, debug JSON, decoder code, and target profiles, but should not be repeated as generic panel titles or primary status badges.
- Compatibility API endpoints and firmware commands may keep historical names until a migration layer exists.

## 3. Unknowns

- Whether the frontend should be served by the Python workbench server or run as a separate dev server during development.
- Whether to use Vite or Umi. Vite is simpler; Umi is part of the Ant ecosystem and may be useful later.
- Whether to use Ant Design Pro Components for tables/forms or stay with base `antd`.
- How much of the current embedded UI should be ported at once.
- Whether offline/no-network operation should bundle all frontend assets locally.

## 4. Experiment Results

2026-05-10: Official docs reviewed.

Relevant findings:

- Ant Design’s React implementation is designed for enterprise-class web applications and includes TypeScript support, theming, and many ready components.
- The official docs recommend npm/yarn/pnpm/bun plus a build tool instead of loading the entire browser bundle.
- `Layout` provides `Header`, `Sider`, `Content`, and `Footer`, matching our desired persistent status/header + navigation + content layout.
- `Menu` is intended for top or side navigation.
- `Tabs` are useful for secondary views inside a page, not necessarily for the whole app once navigation grows.
- The component catalog includes the exact primitives this tool needs: `Badge`, `Tag`, `Alert`, `Descriptions`, `Table`, `List`, `Card`, `Collapse`, `Drawer`, `Statistic`, `Timeline`, `Tooltip`, `Form`, `InputNumber`, `Select`, `Slider`, `Switch`, `Segmented`, `Progress`, `Spin`, and `Result`.

## 5. Next Steps

- Add a React frontend skeleton under `host/workbench/frontend/`.
- Keep current Python endpoints unchanged.
- Rebuild the current method-aligned UI in Ant Design:
  - Project
  - Source
  - Processing
  - Destination
  - Live
  - Artifacts
  - Profile
  - Logs
- During migration, keep `host/live_lcdcam_stream_viewer.py` serving the current embedded UI as a fallback.
- Only switch the default browser UI after GBC live capture is verified through the Ant Design frontend.

## Component Mapping

### App Shell

Use:

- `Layout`
- `Layout.Header`
- `Layout.Sider`
- `Layout.Content`
- `Menu`
- `ConfigProvider`

Purpose:

- persistent top status strip
- side navigation for Project, Source, Processing, Destination, Live, Artifacts, Profile, Logs
- main content area for each module
- optional right-side control drawer/panel

Recommended structure:

```text
Layout
  Header: app title, profile, source state, FPS, port, safe state
  Layout
    Sider: method navigation
    Content: active module
    optional right Drawer/Sider: contextual controls
```

### Status And Telemetry

Use:

- `Badge`
- `Tag`
- `Statistic`
- `Descriptions`
- `Alert`
- `Progress`
- `Tooltip`

Use cases:

- source state: `LIVE`, `NO SOURCE`, `STOPPED`, `WAITING`
- profile status: `PROFILE OK`
- safety status: `DANGER`, `UNPROVEN`, `HISTORICAL`
- FPS, frame age, errors, capture latency
- GPIO32 warning

### Source Investigation

Use:

- `Tabs` or `Segmented` for Source subviews
- `Table` for pin maps, edge counts, timing summaries
- `Descriptions` for selected signal details
- `Alert` for dangerous rails and safety warnings
- `Timeline` for experiment steps or events
- `Collapse` for detailed raw JSON/report sections
- `Button`, `InputNumber`, `Select`, `Switch` for actions and parameters

Source subviews:

- Safety
- Pin Map
- Activity
- Timing
- Hypotheses

### Live Monitor

Use:

- plain `canvas` inside an Ant Design layout
- `Card` only for small grouped controls, not nested visual clutter
- `Segmented` for capture modes such as `RGB565`, `RGB664 diagnostic`, `RGB666 diagnostic`
- `Collapse` for advanced decode controls
- `Slider`, `Switch`, `Select`, `InputNumber` for decode parameters

Important:

- Keep the image monitor visually dominant.
- Hide advanced decode sliders by default.
- Do not show stale frames as live.

### Artifacts

Use:

- `List` or `Table` for recent capture folders
- `Image` for thumbnails
- `Tag` for artifact type: `timing`, `line_clocks`, `boot_capture`, `power_monitor`
- `Descriptions` for manifest details
- `Drawer` for artifact preview/details
- `Empty` when no artifacts exist

Future:

- read `manifest.json`
- show representative PNGs
- generate AI review packs

### Profile

Use:

- `Descriptions`
- `Table`
- `Tree`
- `Collapse`
- `Alert`
- `Button`

Important:

- profile editing should create proposals, not silently rewrite hardware assumptions.
- dangerous rails and do-not-connect lists should be visually prominent.

### Processing And Destination

Use:

- `Result` or `Empty` for not-configured states
- `Steps` for setup workflow
- `Descriptions` for block/profile status
- `Table` for processing block telemetry
- `Card` for individual block summaries

Important:

- these pages should not look broken while empty.
- show the expected next action: define processing block, define destination profile, run test pattern.

## Migration Plan

### Phase 1: Frontend Skeleton

Add:

```text
host/workbench/frontend/
  package.json
  index.html
  src/
    main.tsx
    api.ts
    App.tsx
    components/
    pages/
```

Use:

- React
- TypeScript
- Vite
- Ant Design
- Ant Design Icons

Keep Python server endpoints unchanged.

### Phase 2: API Client

Add a typed API wrapper for current endpoints:

- `/api/status`
- `/api/profile`
- `/api/frame.bin`
- `/api/start`
- `/api/stop`
- `/api/recover`
- `/api/safe-idle`
- `/api/artifacts/recent`
- `/api/workbench/read-gpios`
- `/api/workbench/count-edges-all`
- `/api/workbench/measure-clock`
- `/api/workbench/capture-timing`
- `/api/workbench/line-clocks`

### Phase 3: Rebuild Current Views

Port existing behavior:

- Project page
- Source page
- Live page
- Artifacts page
- Profile page
- Logs page

Do not change firmware or Python capture behavior in this phase.

### Phase 4: Serve Built Frontend

After the Ant frontend works in dev mode, update the Python server to serve static build output.

Result:

- 2026-05-10: completed. `host/live_lcdcam_stream_viewer.py` now serves `host/workbench/frontend/dist/` for non-API routes.
- `/api/*` endpoints remain on the Python backend.
- The old embedded HTML UI was removed from the Python source after Ant live-frame verification.

### Phase 5: UI Cleanup And Feature Growth

Implement:

- persistent status strip
- thumbnails in Artifacts
- profile proposal workflow
- AI review-pack generation
- Processing/Destination real pages

## Design Rules For This Project

- Use Ant Design for controls and layout, but keep the live canvas custom.
- Avoid nested cards.
- Keep operational density high.
- Make safety states visually obvious.
- Prefer `Table`, `Descriptions`, `Tag`, and `Badge` for scan/debug data.
- Prefer `Collapse` for advanced controls and raw JSON.
- Prefer `Drawer` for detail inspection instead of taking over the whole page.
- Keep current capture behavior stable before improving visual polish.

## Current Implementation

2026-05-10: Added first Ant Design frontend skeleton at `host/workbench/frontend/`.

Implemented:

- Vite + React + TypeScript project.
- Ant Design and Ant Design Icons.
- API wrapper for current Python endpoints.
- Method-aligned pages: Project, Source, Processing, Destination, Live, Artifacts, Profile, Logs.
- Native React live canvas that consumes `/api/frame.bin` directly.
- Frame decoders for `RGB565`, `RGB664`, and `RGB666` diagnostic payloads. Historical `RGB332` decoding may remain available for archived artifacts, but it is not the GBC active path.
- Build verification with `npm run build`.

Current behavior: the Python viewer on `http://127.0.0.1:8791/` is the active backend and serves the built Ant frontend. The Ant frontend consumes `/api/frame.bin` directly for the native live canvas.

Run:

```sh
cd host/workbench/frontend
npm install
npm run build
```

Expected backend-served frontend:

```text
http://127.0.0.1:5173/
```

## References

- Ant Design React introduction: https://ant.design/docs/react/introduce/
- Components overview: https://ant-design.antgroup.com/components/overview/
- Layout component: https://ant.design/components/layout/
- Menu component: https://5x.ant.design/components/menu/
- Tabs component: https://ant.design/components/tabs/
