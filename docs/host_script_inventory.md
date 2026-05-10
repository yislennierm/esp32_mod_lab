# Host Script Inventory

## 1. Objective

Classify current host scripts before moving or deleting anything.

This matters because the scripts contain both working tools and historical experiment knowledge from the GBC investigation.

## 2. Current Understanding

The active policy is non-destructive organization first. Existing `host/*.py` entry points remain in place until wrappers and tests exist.

## 3. Unknowns

- Which prototype scripts are still referenced by old captures.
- Which scripts should be converted into generic commands.
- Which GBC-specific presets should move into `host/targets/gbc_lcd/`.

## 4. Experiment Results

2026-05-10 inventory:

| Script | Class | Notes |
|---|---|---|
| `gbc_probe.py` | stable compatibility | Serial probe CLI; later add generic alias. |
| `record_experiment.py` | stable candidate | Experiment folder recorder. |
| `validate_phase1_measurements.py` | stable candidate | Electrical safety validator. |
| `analyze_timing_edges.py` | stable candidate | Timing-edge capture and report tool. |
| `analyze_timing_relationships.py` | stable candidate | Timing relationship analysis. |
| `capture_timing_session.py` | stable candidate | Reproducible timing session runner. |
| `capture_line_clocks.py` | stable candidate | Line-clock investigation. |
| `export_pulseview.py` | stable candidate | VCD export for timing captures. |
| `capture_lcdcam_raw.py` | backend candidate | LCD_CAM raw capture command. |
| `decode_lcdcam_fast.py` | target-aware candidate | Fast decoder; current presets are GBC-specific. |
| `live_lcdcam_stream_viewer.py` | active workbench | Current browser workbench; do not move until wrappers exist. |
| `capture_gallery.py` | artifact utility | Useful for report/gallery workflow. |
| `capture_red_dclk.py` | experiment history | Early red-only investigation. |
| `capture_rg_line_bursts.py` | experiment history | Early red/green investigation. |
| `capture_rgb666_line_bursts.py` | experiment history | RGB666 hypothesis investigation. |
| `capture_dvp_raw.py` | experiment history | DVP-style attempt. |
| `capture_isp_dvp_raw.py` | experiment history | ISP/DVP attempt. |
| `debug_dvp_capture_viewer.py` | experiment history | Debug viewer. |
| `live_dvp_viewer.py` | experiment history | Older DVP viewer. |
| `live_red_viewer.py` | experiment history | Early red-only viewer. |
| `postprocess_dvp_rg44.py` | experiment history | Historical decode attempt. |
| `postprocess_rg_line_bursts.py` | experiment history | Historical line-burst decoder. |
| `render_dvp_raw.py` | experiment history | DVP render helper. |
| `render_dvp_stride_scan.py` | experiment history | Stride scan helper. |
| `render_dvp_variants.py` | experiment history | Variant render helper. |
| `render_raw_stream_widths.py` | experiment history | Width sweep helper. |
| `render_red_diagnostic.py` | experiment history | Red diagnostic renderer. |
| `deskew_raw_stream.py` | hypothesis helper | Useful for generic stream interpretation. |
| `extract_stream_frames.py` | hypothesis helper | Useful for generic stream interpretation. |

## 5. Next Steps

- Add wrappers before moving stable candidates.
- Move only experiment-history scripts after docs and artifact references are updated.
- Extract shared serial/profile/artifact code into `host/lab/`.
- Keep `live_lcdcam_stream_viewer.py` working throughout browser refactors.
