#!/usr/bin/env python3
"""Offline browser viewer for saved DVP RAW8 red/green diagnostic captures."""

from __future__ import annotations

import argparse
import json
import signal
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse


INDEX_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>DVP Capture Debug Viewer</title>
<style>
:root {
  color-scheme: dark;
  font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  background: #101418;
  color: #e7edf2;
}
body { margin: 0; min-height: 100vh; background: #101418; }
main { display: grid; grid-template-columns: minmax(0, 1fr) 320px; gap: 14px; padding: 14px; }
.viewer { min-width: 0; }
.panel { background: #141b21; border: 1px solid #29333d; border-radius: 6px; padding: 12px; }
h1 { margin: 0 0 10px; font-size: 18px; font-weight: 650; }
.meta { color: #9aabb7; font-size: 12px; overflow-wrap: anywhere; }
canvas { display: block; width: 100%; image-rendering: pixelated; background: #050607; }
#frame { height: calc(100vh - 28px); min-height: 480px; }
.controls { display: grid; gap: 12px; }
label { display: grid; gap: 6px; font-size: 13px; color: #d7e0e6; }
.row { display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.value { color: #9fb0bc; font-variant-numeric: tabular-nums; }
input[type="range"] { width: 100%; }
input[type="checkbox"] { width: 18px; height: 18px; }
button {
  border: 1px solid #3a4753;
  background: #1d2730;
  color: #e7edf2;
  border-radius: 6px;
  padding: 8px 10px;
  font: inherit;
  cursor: pointer;
}
button:hover { background: #26313a; }
.buttons { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
@media (max-width: 900px) {
  main { grid-template-columns: 1fr; }
  #frame { height: auto; min-height: 0; }
}
</style>
</head>
<body>
<main>
  <section class="viewer">
    <canvas id="frame" width="__WIDTH__" height="__HEIGHT__"></canvas>
  </section>
  <aside class="panel">
    <h1>DVP Capture Debug</h1>
    <p class="meta" id="meta">loading</p>
    <div class="controls">
      <label>
        <span class="row"><span>X shift</span><span class="value" id="xValue">0</span></span>
        <input id="xShift" type="range" min="-160" max="160" value="0">
      </label>
      <label>
        <span class="row"><span>Y shift</span><span class="value" id="yValue">0</span></span>
        <input id="yShift" type="range" min="-144" max="144" value="0">
      </label>
      <label>
        <span class="row"><span>Line skew</span><span class="value" id="lineValue">0</span></span>
        <input id="lineShift" type="range" min="-16" max="16" value="0">
      </label>
      <label>
        <span class="row"><span>Fine line skew</span><span class="value" id="fineValue">0</span></span>
        <input id="fineLineShift" type="range" min="-100" max="100" value="0">
      </label>
      <label class="row">Invert intensity <input id="invert" type="checkbox"></label>
      <label class="row">Reverse 4-bit order <input id="reverseBits" type="checkbox"></label>
      <label class="row">Swap red/green <input id="swapChannels" type="checkbox"></label>
      <div class="buttons">
        <button id="reset">Reset</button>
        <button id="copy">Copy State</button>
      </div>
      <p class="meta" id="stateText"></p>
    </div>
  </aside>
</main>
<script>
const width = __WIDTH__;
const height = __HEIGHT__;
const capturePath = "__CAPTURE_PATH__";
const canvas = document.getElementById("frame");
const ctx = canvas.getContext("2d");
let raw = null;

const controls = {
  xShift: document.getElementById("xShift"),
  yShift: document.getElementById("yShift"),
  lineShift: document.getElementById("lineShift"),
  fineLineShift: document.getElementById("fineLineShift"),
  invert: document.getElementById("invert"),
  reverseBits: document.getElementById("reverseBits"),
  swapChannels: document.getElementById("swapChannels"),
};

function mod(value, divisor) {
  return ((value % divisor) + divisor) % divisor;
}

function reverse4(value) {
  let out = 0;
  for (let bit = 0; bit < 4; bit++) {
    if (value & (1 << bit)) out |= 1 << (3 - bit);
  }
  return out;
}

function decode(value, invert, reverseBits, swapChannels) {
  let r = value & 15;
  let g = (value >> 4) & 15;
  if (reverseBits) {
    r = reverse4(r);
    g = reverse4(g);
  }
  if (invert) {
    r = 15 - r;
    g = 15 - g;
  }
  if (swapChannels) {
    const tmp = r;
    r = g;
    g = tmp;
  }
  return [r * 17, g * 17, 0];
}

function state() {
  return {
    xShift: Number(controls.xShift.value),
    yShift: Number(controls.yShift.value),
    lineShift: Number(controls.lineShift.value),
    fineLineShift: Number(controls.fineLineShift.value) / 100,
    invert: controls.invert.checked,
    reverseBits: controls.reverseBits.checked,
    swapChannels: controls.swapChannels.checked,
  };
}

function render() {
  if (!raw) return;
  const s = state();
  document.getElementById("xValue").textContent = s.xShift;
  document.getElementById("yValue").textContent = s.yShift;
  document.getElementById("lineValue").textContent = s.lineShift;
  document.getElementById("fineValue").textContent = s.fineLineShift.toFixed(2);
  document.getElementById("stateText").textContent = JSON.stringify(s);

  const image = ctx.createImageData(width, height);
  for (let y = 0; y < height; y++) {
    const srcY = mod(y + s.yShift, height);
    const rowShift = s.xShift + Math.round(srcY * (s.lineShift + s.fineLineShift));
    for (let x = 0; x < width; x++) {
      const srcX = mod(x + rowShift, width);
      const value = raw[srcY * width + srcX];
      const [r, g, b] = decode(value, s.invert, s.reverseBits, s.swapChannels);
      const offset = (y * width + x) * 4;
      image.data[offset] = r;
      image.data[offset + 1] = g;
      image.data[offset + 2] = b;
      image.data[offset + 3] = 255;
    }
  }
  ctx.putImageData(image, 0, 0);
}

async function load() {
  const response = await fetch("/api/raw", {cache: "no-store"});
  const buffer = await response.arrayBuffer();
  raw = new Uint8Array(buffer);
  document.getElementById("meta").textContent = `${capturePath} | ${width}x${height} | ${raw.length} bytes`;
  render();
}

for (const input of Object.values(controls)) {
  input.addEventListener("input", render);
  input.addEventListener("change", render);
}

document.getElementById("reset").addEventListener("click", () => {
  controls.xShift.value = 0;
  controls.yShift.value = 0;
  controls.lineShift.value = 0;
  controls.fineLineShift.value = 0;
  controls.invert.checked = false;
  controls.reverseBits.checked = false;
  controls.swapChannels.checked = false;
  render();
});

document.getElementById("copy").addEventListener("click", async () => {
  const text = JSON.stringify(state());
  try {
    await navigator.clipboard.writeText(text);
    document.getElementById("stateText").textContent = `copied ${text}`;
  } catch {
    document.getElementById("stateText").textContent = text;
  }
});

load().catch((err) => {
  document.getElementById("meta").textContent = `error: ${err.message}`;
});
</script>
</body>
</html>
"""


def make_handler(raw: bytes, page: bytes) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:
            return

        def send_body(self, status: int, content_type: str, body: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path == "/":
                self.send_body(200, "text/html; charset=utf-8", page)
                return
            if path == "/api/raw":
                self.send_body(200, "application/octet-stream", raw)
                return
            self.send_body(404, "text/plain; charset=utf-8", b"not found\n")

    return Handler


def run(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    expected = args.width * args.height
    if len(raw) != expected:
        raise ValueError(f"expected {expected} bytes for {args.width}x{args.height}, got {len(raw)}")

    page = (
        INDEX_HTML.replace("__WIDTH__", str(args.width))
        .replace("__HEIGHT__", str(args.height))
        .replace("__CAPTURE_PATH__", str(args.input))
        .encode("utf-8")
    )
    server = ThreadingHTTPServer((args.listen_host, args.listen_port), make_handler(raw, page))

    def stop(_signum: int, _frame: Any) -> None:
        server.shutdown()

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    url = f"http://{args.listen_host}:{server.server_port}/"
    print(json.dumps({"ok": True, "url": url, "input": str(args.input)}, sort_keys=True), flush=True)
    try:
        server.serve_forever()
    finally:
        server.server_close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--width", type=int, default=160)
    parser.add_argument("--height", type=int, default=144)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8767)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
