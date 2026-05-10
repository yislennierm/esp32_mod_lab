#!/usr/bin/env python3
"""Browser live viewer for experimental DVP RAW8 captures."""

from __future__ import annotations

import argparse
import json
import signal
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
from urllib.parse import urlparse

from gbc_probe import DEFAULT_BAUD, ProbeClient, autodetect_port


INDEX_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GBC DVP Live Viewer</title>
<style>
:root {
  color-scheme: dark;
  font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  background: #101418;
  color: #e7edf2;
}
body { margin: 0; min-height: 100vh; background: #101418; }
main { display: grid; grid-template-rows: auto auto 1fr; gap: 12px; padding: 16px; }
.toolbar { display: flex; align-items: center; justify-content: space-between; gap: 12px; }
h1 { margin: 0; font-size: 18px; font-weight: 650; }
.status { color: #9aabb7; font-size: 13px; text-align: right; }
.stats { display: grid; grid-template-columns: repeat(6, minmax(80px, 1fr)); gap: 8px; }
.stat { background: #182027; border: 1px solid #2a3540; border-radius: 6px; padding: 8px 10px; }
.label { display: block; color: #8fa0ad; font-size: 11px; }
.value { display: block; margin-top: 3px; font-size: 16px; font-variant-numeric: tabular-nums; }
.layout { display: grid; grid-template-columns: minmax(0, 1fr) 260px; gap: 12px; min-height: 0; }
.panel { background: #141b21; border: 1px solid #29333d; border-radius: 6px; padding: 10px; min-width: 0; }
.caption { margin: 0 0 8px; color: #aab6bf; font-size: 12px; }
canvas { display: block; width: 100%; image-rendering: pixelated; background: #050607; }
#frame { height: calc(100vh - 210px); min-height: 420px; }
#hist { height: 260px; }
.controls { display: grid; gap: 10px; }
label { display: flex; align-items: center; justify-content: space-between; gap: 10px; color: #d7e0e6; font-size: 13px; }
input[type="checkbox"] { width: 18px; height: 18px; }
@media (max-width: 860px) {
  .stats { grid-template-columns: repeat(2, minmax(80px, 1fr)); }
  .layout { grid-template-columns: 1fr; }
}
</style>
</head>
<body>
<main>
  <div class="toolbar">
    <h1>GBC DVP Live Viewer</h1>
    <div class="status" id="status">starting</div>
  </div>
  <section class="stats">
    <div class="stat"><span class="label">frames</span><span class="value" id="frames">0</span></div>
    <div class="stat"><span class="label">fps</span><span class="value" id="fps">0.00</span></div>
    <div class="stat"><span class="label">checksum</span><span class="value" id="checksum">-</span></div>
    <div class="stat"><span class="label">transitions</span><span class="value" id="transitions">-</span></div>
    <div class="stat"><span class="label">min</span><span class="value" id="min">-</span></div>
    <div class="stat"><span class="label">max</span><span class="value" id="max">-</span></div>
  </section>
  <section class="layout">
    <div class="panel">
      <p class="caption">live 160x144 RAW8 red/green diagnostic render</p>
      <canvas id="frame" width="160" height="144"></canvas>
    </div>
    <div class="panel">
      <p class="caption">render controls</p>
      <div class="controls">
        <label>invert intensity <input id="invert" type="checkbox"></label>
        <label>reverse R bit order <input id="reverse" type="checkbox"></label>
      </div>
      <p class="caption" style="margin-top:16px">RAW8 histogram</p>
      <canvas id="hist" width="240" height="260"></canvas>
    </div>
  </section>
</main>
<script>
const frame = document.getElementById("frame");
const frameCtx = frame.getContext("2d");
const hist = document.getElementById("hist");
const histCtx = hist.getContext("2d");
const invertBox = document.getElementById("invert");
const reverseBox = document.getElementById("reverse");
let frames = 0;
let lastT = performance.now();
let running = false;

function setText(id, value) { document.getElementById(id).textContent = value; }

function reverse4(v) {
  let out = 0;
  for (let bit = 0; bit < 4; bit++) {
    if (v & (1 << bit)) out |= 1 << (3 - bit);
  }
  return out;
}

function rg44(v) {
  let r = v & 15;
  let g = (v >> 4) & 15;
  if (reverseBox.checked) {
    r = reverse4(r);
    g = reverse4(g);
  }
  if (invertBox.checked) {
    r = 15 - r;
    g = 15 - g;
  }
  return [r, g];
}

function drawFrame(values) {
  const image = frameCtx.createImageData(160, 144);
  const counts = new Array(256).fill(0);
  for (let i = 0; i < values.length; i++) {
    const raw = values[i] & 255;
    const [r4, g4] = rg44(raw);
    counts[raw] += 1;
    const o = i * 4;
    image.data[o] = r4 * 17;
    image.data[o + 1] = g4 * 17;
    image.data[o + 2] = 0;
    image.data[o + 3] = 255;
  }
  frameCtx.putImageData(image, 0, 0);
  drawHist(counts);
}

function drawHist(counts) {
  histCtx.fillStyle = "#050607";
  histCtx.fillRect(0, 0, hist.width, hist.height);
  const maxCount = Math.max(1, ...counts);
  const barW = hist.width / 256;
  for (let i = 0; i < 256; i++) {
    const h = Math.round((counts[i] / maxCount) * (hist.height - 20));
    const [r4, g4] = rg44(i);
    histCtx.fillStyle = `rgb(${r4 * 17},${g4 * 17},0)`;
    histCtx.fillRect(i * barW, hist.height - h, Math.max(1, barW - 1), h);
  }
}

async function poll() {
  if (running) return;
  running = true;
  const started = performance.now();
  try {
    const response = await fetch("/api/frame", {cache: "no-store"});
    const data = await response.json();
    if (!data.ok) throw new Error(data.error || "capture failed");
    drawFrame(data.bytes);
    frames += 1;
    const now = performance.now();
    const fps = 1000 / Math.max(1, now - lastT);
    lastT = now;
    setText("frames", frames);
    setText("fps", fps.toFixed(2));
    setText("checksum", data.checksum);
    setText("transitions", data.raw8_transitions ?? data.lower6_transitions);
    setText("min", data.min_value);
    setText("max", data.max_value);
    document.getElementById("status").textContent = `live, capture ${Math.round(performance.now() - started)} ms`;
  } catch (err) {
    document.getElementById("status").textContent = `error: ${err.message}`;
  } finally {
    running = false;
  }
}

poll();
setInterval(poll, __INTERVAL_MS__);
</script>
</body>
</html>
"""


class DvpCaptureState:
    def __init__(
        self,
        port: str,
        baud: int,
        timeout: float,
        de: str,
        capture_timeout_ms: int,
        vsync_invert: bool,
        de_invert: bool,
        pclk_invert: bool,
    ) -> None:
        self._lock = threading.Lock()
        self._client = ProbeClient(port, baud, timeout)
        self._client.drain_startup()
        self.de = de
        self.capture_timeout_ms = capture_timeout_ms
        self.vsync_invert = vsync_invert
        self.de_invert = de_invert
        self.pclk_invert = pclk_invert

    def close(self) -> None:
        self._client.close()

    def capture(self) -> dict[str, Any]:
        command = (
            f"DVP_CAPTURE_RAW {self.de} {self.capture_timeout_ms} "
            f"{int(self.vsync_invert)} {int(self.de_invert)} {int(self.pclk_invert)}"
        )
        with self._lock:
            response = self._client.command(command)
        data = dict(response.data)
        if data.get("ok"):
            raw = bytes.fromhex(data.pop("data_hex"))
            data["bytes"] = list(raw)
        return data


def make_handler(state: DvpCaptureState, interval_ms: int) -> type[BaseHTTPRequestHandler]:
    page = INDEX_HTML.replace("__INTERVAL_MS__", str(interval_ms))

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
                self.send_body(200, "text/html; charset=utf-8", page.encode("utf-8"))
                return
            if path == "/api/frame":
                try:
                    body = json.dumps(state.capture()).encode("utf-8")
                    self.send_body(200, "application/json", body)
                except Exception as exc:
                    self.send_body(500, "application/json", json.dumps({"ok": False, "error": str(exc)}).encode("utf-8"))
                return
            self.send_body(404, "text/plain; charset=utf-8", b"not found\n")

    return Handler


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    state = DvpCaptureState(
        port,
        args.baud,
        args.timeout,
        args.de,
        args.capture_timeout_ms,
        args.vsync_invert,
        args.de_invert,
        args.pclk_invert,
    )
    server = ThreadingHTTPServer((args.listen_host, args.listen_port), make_handler(state, args.interval_ms))

    def stop(_signum: int, _frame: Any) -> None:
        server.shutdown()

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    url = f"http://{args.listen_host}:{server.server_port}/"
    print(json.dumps({
        "ok": True,
        "url": url,
        "serial_port": port,
        "de": args.de,
        "vsync_invert": args.vsync_invert,
        "de_invert": args.de_invert,
        "pclk_invert": args.pclk_invert,
        "interval_ms": args.interval_ms,
    }, sort_keys=True), flush=True)

    try:
        server.serve_forever()
    finally:
        state.close()
        server.server_close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14401")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--de", choices=["SPL", "LP"], default="SPL")
    parser.add_argument("--capture-timeout-ms", type=int, default=1500)
    parser.add_argument("--vsync-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--de-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--pclk-invert", action=argparse.BooleanOptionalAction, default=False)
    parser.add_argument("--interval-ms", type=int, default=900)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8766)
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
