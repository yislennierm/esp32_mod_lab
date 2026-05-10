#!/usr/bin/env python3
"""Browser-based live viewer for exploratory red-bus DCLK captures."""

from __future__ import annotations

import argparse
import json
import signal
import sys
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
<title>GBC Red Bus Live Viewer</title>
<style>
:root {
  color-scheme: dark;
  font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
  background: #101418;
  color: #e7edf2;
}
body {
  margin: 0;
  min-height: 100vh;
  background: #101418;
}
main {
  display: grid;
  grid-template-rows: auto auto 1fr;
  gap: 12px;
  padding: 16px;
}
.toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
h1 {
  margin: 0;
  font-size: 18px;
  font-weight: 650;
}
.stats {
  display: grid;
  grid-template-columns: repeat(6, minmax(80px, 1fr));
  gap: 8px;
}
.stat {
  background: #182027;
  border: 1px solid #2a3540;
  border-radius: 6px;
  padding: 8px 10px;
}
.label {
  display: block;
  color: #8fa0ad;
  font-size: 11px;
}
.value {
  display: block;
  margin-top: 3px;
  font-size: 16px;
  font-variant-numeric: tabular-nums;
}
.status {
  color: #8fa0ad;
  font-size: 13px;
  text-align: right;
}
.view {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 220px;
  gap: 12px;
  min-height: 0;
}
.panel {
  background: #141b21;
  border: 1px solid #29333d;
  border-radius: 6px;
  padding: 10px;
  min-width: 0;
}
canvas {
  display: block;
  width: 100%;
  image-rendering: pixelated;
  background: #050607;
}
#strip {
  height: 160px;
}
#waterfall {
  height: calc(100vh - 310px);
  min-height: 260px;
}
#hist {
  height: 260px;
}
.caption {
  margin: 0 0 8px;
  color: #aab6bf;
  font-size: 12px;
}
@media (max-width: 820px) {
  .stats {
    grid-template-columns: repeat(2, minmax(80px, 1fr));
  }
  .view {
    grid-template-columns: 1fr;
  }
}
</style>
</head>
<body>
<main>
  <div class="toolbar">
    <h1>GBC Red Bus Live Viewer</h1>
    <div class="status" id="status">starting</div>
  </div>
  <section class="stats">
    <div class="stat"><span class="label">captures</span><span class="value" id="captures">0</span></div>
    <div class="stat"><span class="label">samples</span><span class="value" id="samples">0</span></div>
    <div class="stat"><span class="label">unique</span><span class="value" id="unique">0</span></div>
    <div class="stat"><span class="label">transitions</span><span class="value" id="transitions">0</span></div>
    <div class="stat"><span class="label">trigger</span><span class="value" id="trigger">-</span></div>
    <div class="stat"><span class="label">timeout</span><span class="value" id="timeout">-</span></div>
  </section>
  <section class="view">
    <div class="panel">
      <p class="caption">latest red6 capture, left to right</p>
      <canvas id="strip" width="1024" height="160"></canvas>
      <p class="caption" style="margin-top:12px">scrolling captures over time</p>
      <canvas id="waterfall" width="1024" height="420"></canvas>
    </div>
    <div class="panel">
      <p class="caption">red6 histogram</p>
      <canvas id="hist" width="220" height="260"></canvas>
    </div>
  </section>
</main>
<script>
const strip = document.getElementById("strip");
const stripCtx = strip.getContext("2d");
const waterfall = document.getElementById("waterfall");
const waterfallCtx = waterfall.getContext("2d");
const hist = document.getElementById("hist");
const histCtx = hist.getContext("2d");
let captures = 0;
let running = false;

function red8(value) {
  return Math.max(0, Math.min(255, Math.round((value & 63) * 255 / 63)));
}

function color(value) {
  const r = red8(value);
  return `rgb(${r},0,0)`;
}

function drawStrip(values) {
  stripCtx.clearRect(0, 0, strip.width, strip.height);
  if (!values.length) return;
  const step = strip.width / values.length;
  for (let i = 0; i < values.length; i++) {
    stripCtx.fillStyle = color(values[i]);
    stripCtx.fillRect(Math.floor(i * step), 0, Math.ceil(step), strip.height);
  }
}

function drawWaterfall(values) {
  const rowH = 4;
  const image = waterfallCtx.getImageData(0, 0, waterfall.width, waterfall.height - rowH);
  waterfallCtx.putImageData(image, 0, rowH);
  waterfallCtx.fillStyle = "#050607";
  waterfallCtx.fillRect(0, 0, waterfall.width, rowH);
  if (!values.length) return;
  const step = waterfall.width / values.length;
  for (let i = 0; i < values.length; i++) {
    waterfallCtx.fillStyle = color(values[i]);
    waterfallCtx.fillRect(Math.floor(i * step), 0, Math.ceil(step), rowH);
  }
}

function drawHist(counts) {
  histCtx.clearRect(0, 0, hist.width, hist.height);
  histCtx.fillStyle = "#050607";
  histCtx.fillRect(0, 0, hist.width, hist.height);
  const maxCount = Math.max(1, ...counts.map(item => item.count));
  const barW = hist.width / 64;
  for (const item of counts) {
    const h = Math.round((item.count / maxCount) * (hist.height - 24));
    histCtx.fillStyle = color(item.red6);
    histCtx.fillRect(item.red6 * barW, hist.height - h, Math.max(1, barW - 1), h);
  }
}

function setText(id, value) {
  document.getElementById(id).textContent = value;
}

async function poll() {
  if (running) return;
  running = true;
  const started = performance.now();
  try {
    const response = await fetch("/api/red", {cache: "no-store"});
    const data = await response.json();
    if (!data.ok) throw new Error(data.error || "capture failed");
    captures += 1;
    const values = data.samples.map(sample => sample.red6);
    drawStrip(values);
    drawWaterfall(values);
    drawHist(data.red_value_counts || []);
    setText("captures", captures);
    setText("samples", data.sample_count);
    setText("unique", data.unique_red_values);
    setText("transitions", data.transitions);
    setText("trigger", data.trigger_seen ? "yes" : "no");
    setText("timeout", data.timeout ? "yes" : "no");
    const elapsed = Math.round(performance.now() - started);
    document.getElementById("status").textContent = `live, last capture ${elapsed} ms`;
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


def summarize_capture(data: dict[str, Any]) -> dict[str, Any]:
    samples = data.get("samples", [])
    values = [int(sample.get("red6", 0)) for sample in samples]
    counts: dict[int, int] = {}
    transitions = 0
    for index, value in enumerate(values):
        counts[value] = counts.get(value, 0) + 1
        if index > 0 and value != values[index - 1]:
            transitions += 1

    data["unique_red_values"] = len(counts)
    data["transitions"] = transitions
    data["red_value_counts"] = [
        {"red6": value, "count": count}
        for value, count in sorted(counts.items(), key=lambda item: item[1], reverse=True)
    ]
    return data


class RedCaptureState:
    def __init__(self, port: str, baud: int, timeout: float, samples: int, capture_timeout_ms: int) -> None:
        self._lock = threading.Lock()
        self._client = ProbeClient(port, baud, timeout)
        self._client.drain_startup()
        self.samples = samples
        self.capture_timeout_ms = capture_timeout_ms

    def close(self) -> None:
        self._client.close()

    def capture(self) -> dict[str, Any]:
        with self._lock:
            response = self._client.command(f"CAPTURE_RED_DCLK {self.samples} {self.capture_timeout_ms}")
        return summarize_capture(response.data)


def make_handler(state: RedCaptureState, interval_ms: int) -> type[BaseHTTPRequestHandler]:
    page = INDEX_HTML.replace("__INTERVAL_MS__", str(interval_ms))

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:
            return

        def send_bytes(self, status: int, content_type: str, body: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            path = urlparse(self.path).path
            if path == "/":
                self.send_bytes(200, "text/html; charset=utf-8", page.encode("utf-8"))
                return
            if path == "/api/red":
                try:
                    body = json.dumps(state.capture()).encode("utf-8")
                    self.send_bytes(200, "application/json", body)
                except Exception as exc:
                    error = {"ok": False, "error": str(exc)}
                    self.send_bytes(500, "application/json", json.dumps(error).encode("utf-8"))
                return
            self.send_bytes(404, "text/plain; charset=utf-8", b"not found\n")

    return Handler


def run(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    state = RedCaptureState(port, args.baud, args.timeout, args.samples, args.capture_timeout_ms)
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
        "samples": args.samples,
        "capture_timeout_ms": args.capture_timeout_ms,
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
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--capture-timeout-ms", type=int, default=100)
    parser.add_argument("--interval-ms", type=int, default=350)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8765)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.samples <= 0 or args.samples > 2048:
        print("error: --samples must be between 1 and 2048", file=sys.stderr)
        return 2
    if args.capture_timeout_ms <= 0 or args.capture_timeout_ms > 1000:
        print("error: --capture-timeout-ms must be between 1 and 1000", file=sys.stderr)
        return 2
    if args.interval_ms < 100:
        print("error: --interval-ms must be at least 100", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
