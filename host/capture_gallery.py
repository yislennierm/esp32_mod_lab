#!/usr/bin/env python3
"""Browser gallery for comparing generated capture PNG artifacts."""

from __future__ import annotations

import argparse
import html
import json
import mimetypes
import signal
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse


INDEX_HTML = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GBC Capture Gallery</title>
<style>
:root { color-scheme: dark; font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif; background: #0f1317; color: #e8edf1; }
body { margin: 0; background: #0f1317; }
header { position: sticky; top: 0; z-index: 2; display: grid; gap: 10px; padding: 12px; background: #121920; border-bottom: 1px solid #2b3742; }
h1 { margin: 0; font-size: 18px; }
.bar { display: grid; grid-template-columns: 1fr auto auto auto; gap: 8px; align-items: center; }
input, select, button { border: 1px solid #3a4753; background: #18222b; color: #e8edf1; border-radius: 6px; padding: 8px 10px; font: inherit; }
button { cursor: pointer; }
button:hover { background: #22303a; }
main { display: grid; grid-template-columns: repeat(auto-fill, minmax(var(--tile-w, 260px), 1fr)); gap: 12px; padding: 12px; }
figure { margin: 0; min-width: 0; background: #151d24; border: 1px solid #2a3641; border-radius: 6px; overflow: hidden; }
figure.selected { outline: 2px solid #73c2ff; }
.imgwrap { display: grid; place-items: center; background: #07090b; min-height: 160px; }
img { display: block; max-width: 100%; height: auto; image-rendering: pixelated; }
figcaption { padding: 8px; font-size: 12px; color: #b7c4ce; overflow-wrap: anywhere; }
.name { color: #f0f5f8; }
.meta { margin-top: 4px; color: #8fa0ac; }
.count { color: #9eb0bc; font-size: 12px; }
dialog { width: min(96vw, 1280px); max-height: 96vh; padding: 0; background: #0b0e11; border: 1px solid #3a4753; border-radius: 6px; color: #e8edf1; }
dialog::backdrop { background: rgba(0,0,0,0.75); }
.modalbar { display: flex; justify-content: space-between; gap: 8px; padding: 10px; background: #121920; border-bottom: 1px solid #2b3742; }
.modalbody { display: grid; place-items: center; padding: 10px; }
.modalbody img { max-width: 94vw; max-height: 82vh; }
@media (max-width: 760px) { .bar { grid-template-columns: 1fr; } }
</style>
</head>
<body>
<header>
  <div class="bar">
    <h1>GBC Capture Gallery</h1>
    <span class="count" id="count"></span>
    <select id="sort">
      <option value="name">Name</option>
      <option value="mtime" selected>Newest</option>
      <option value="dir">Folder</option>
    </select>
    <button id="smaller">Smaller</button>
    <button id="larger">Larger</button>
  </div>
  <input id="filter" placeholder="Filter filenames or folders, e.g. sw320 x160 y32 skewp0p00">
</header>
<main id="grid"></main>
<dialog id="dialog">
  <div class="modalbar">
    <span id="modalName"></span>
    <button id="close">Close</button>
  </div>
  <div class="modalbody"><img id="modalImg" alt=""></div>
</dialog>
<script>
const items = __ITEMS__;
const root = "__ROOT__";
const grid = document.getElementById("grid");
const filter = document.getElementById("filter");
const sort = document.getElementById("sort");
const count = document.getElementById("count");
const dialog = document.getElementById("dialog");
const modalImg = document.getElementById("modalImg");
const modalName = document.getElementById("modalName");
let tileWidth = 260;

function itemUrl(path) {
  return "/file?path=" + encodeURIComponent(path);
}

function filteredItems() {
  const terms = filter.value.toLowerCase().split(/\\s+/).filter(Boolean);
  let out = items.filter(item => {
    const hay = (item.path + " " + item.name + " " + item.parent).toLowerCase();
    return terms.every(term => hay.includes(term));
  });
  if (sort.value === "mtime") out.sort((a, b) => b.mtime - a.mtime || a.path.localeCompare(b.path));
  if (sort.value === "name") out.sort((a, b) => a.name.localeCompare(b.name));
  if (sort.value === "dir") out.sort((a, b) => a.parent.localeCompare(b.parent) || a.name.localeCompare(b.name));
  return out;
}

function render() {
  const out = filteredItems();
  count.textContent = `${out.length} / ${items.length}`;
  document.documentElement.style.setProperty("--tile-w", `${tileWidth}px`);
  grid.textContent = "";
  for (const item of out) {
    const fig = document.createElement("figure");
    const wrap = document.createElement("div");
    wrap.className = "imgwrap";
    const img = document.createElement("img");
    img.loading = "lazy";
    img.src = itemUrl(item.path);
    img.alt = item.name;
    wrap.appendChild(img);
    const cap = document.createElement("figcaption");
    cap.innerHTML = `<div class="name">${escapeHtml(item.name)}</div><div class="meta">${escapeHtml(item.parent)}</div>`;
    fig.appendChild(wrap);
    fig.appendChild(cap);
    fig.addEventListener("click", () => {
      document.querySelectorAll("figure.selected").forEach(el => el.classList.remove("selected"));
      fig.classList.add("selected");
      modalImg.src = itemUrl(item.path);
      modalName.textContent = item.path;
      dialog.showModal();
    });
    grid.appendChild(fig);
  }
}

function escapeHtml(text) {
  return text.replace(/[&<>"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[ch]));
}

filter.addEventListener("input", render);
sort.addEventListener("change", render);
document.getElementById("smaller").addEventListener("click", () => { tileWidth = Math.max(120, tileWidth - 40); render(); });
document.getElementById("larger").addEventListener("click", () => { tileWidth = Math.min(720, tileWidth + 40); render(); });
document.getElementById("close").addEventListener("click", () => dialog.close());
document.addEventListener("keydown", event => { if (event.key === "/" && document.activeElement !== filter) { event.preventDefault(); filter.focus(); } });
render();
</script>
</body>
</html>
"""


def collect_images(root: Path, pattern: str, limit: int | None) -> list[dict[str, object]]:
    files = sorted(root.rglob(pattern), key=lambda path: path.stat().st_mtime, reverse=True)
    if limit is not None:
        files = files[:limit]
    items = []
    for path in files:
        if path.suffix.lower() not in {".png", ".jpg", ".jpeg", ".gif", ".webp"}:
            continue
        rel = path.relative_to(root).as_posix()
        stat = path.stat()
        items.append({
            "path": rel,
            "name": path.name,
            "parent": path.parent.relative_to(root).as_posix(),
            "mtime": stat.st_mtime,
            "size": stat.st_size,
        })
    return items


def make_handler(root: Path, page: bytes) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: object) -> None:
            return

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path == "/":
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(page)))
                self.end_headers()
                self.wfile.write(page)
                return
            if parsed.path == "/file":
                query = parse_qs(parsed.query)
                rel = unquote(query.get("path", [""])[0])
                target = (root / rel).resolve()
                try:
                    target.relative_to(root)
                except ValueError:
                    self.send_error(403)
                    return
                if not target.is_file():
                    self.send_error(404)
                    return
                data = target.read_bytes()
                content_type = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
                self.send_response(200)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
                return
            self.send_error(404)
    return Handler


def run(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    items = collect_images(root, args.pattern, args.limit)
    page = INDEX_HTML.replace("__ITEMS__", json.dumps(items)).replace("__ROOT__", html.escape(str(root))).encode()
    server = ThreadingHTTPServer((args.listen_host, args.listen_port), make_handler(root, page))
    signal.signal(signal.SIGINT, lambda _sig, _frame: server.shutdown())
    url = f"http://{args.listen_host}:{server.server_port}/"
    print(f"gallery={url}")
    print(f"root={root}")
    print(f"images={len(items)}")
    server.serve_forever()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("captures/decoded/lcdcam_raw"))
    parser.add_argument("--pattern", default="*.png")
    parser.add_argument("--limit", type=int, default=500)
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, default=8766)
    return parser


def main() -> int:
    return run(build_parser().parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
