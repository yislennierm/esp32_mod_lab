# Host Experiment Scripts

## 1. Objective

Define where one-off or historically important host scripts should eventually live.

This matters because prototype scripts are part of the reverse engineering evidence trail. They should be classified before moving or deleting them.

## 2. Current Understanding

Likely experiment/prototype candidates from `host/`:

| Script | Why it is experimental or historical |
|---|---|
| `capture_red_dclk.py` | early red-only DCLK investigation |
| `capture_rg_line_bursts.py` | early red/green line-burst workflow |
| `capture_rgb666_line_bursts.py` | RGB666 hypothesis experiment |
| `capture_dvp_raw.py` | DVP-style capture attempts |
| `capture_isp_dvp_raw.py` | ISP/DVP capture attempts |
| `debug_dvp_capture_viewer.py` | debugging viewer |
| `live_dvp_viewer.py` | older DVP live path |
| `live_red_viewer.py` | early red-only viewer |
| `postprocess_dvp_rg44.py` | historical decode experiment |
| `postprocess_rg_line_bursts.py` | historical RG line-burst decoder |
| `render_dvp_raw.py` | DVP render experiment |
| `render_dvp_stride_scan.py` | stride scan experiment |
| `render_dvp_variants.py` | variant render experiment |
| `render_raw_stream_widths.py` | width sweep renderer |
| `render_red_diagnostic.py` | red diagnostic renderer |
| `deskew_raw_stream.py` | deskew hypothesis tool |
| `extract_stream_frames.py` | frame extraction hypothesis tool |
| `capture_gallery.py` | capture artifact gallery utility |

Confidence level: medium. Some of these may become stable tools after they are generalized.

## 3. Unknowns

- Which scripts are still needed by current docs or browser links.
- Which scripts should be preserved exactly for reproducibility.
- Which scripts should be replaced by a general hypothesis engine.

## 4. Experiment Results

2026-05-10: Directory added as an index only. No scripts were moved.

## 5. Next Steps

- Add a script inventory document with stable, target-specific, and experimental classifications.
- Before moving any script, add a wrapper or update all docs that reference it.
- Keep the original experiment filenames in artifact manifests where possible.
