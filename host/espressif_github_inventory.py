#!/usr/bin/env python3
"""Inventory Espressif public GitHub repositories for lab research.

The output is intentionally generated JSON. It is a research index, not a
vendored copy of Espressif source code.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_ORG = "espressif"
DEFAULT_OUTPUT = Path("inventories/espressif_github_repositories.json")
GITHUB_API = "https://api.github.com"


CATEGORY_RULES: list[tuple[str, list[str]]] = [
    ("core_sdk", ["esp-idf", "idf", "sdk"]),
    ("ai_agent", ["claw", "openclaw", "agent", "mcp"]),
    ("ai_ml", ["ai", "dl", "who", "sr", "skainet", "model", "speech", "face"]),
    ("display_camera_video", ["lcd", "display", "camera", "dsi", "rgb", "jpeg", "mipi", "video"]),
    ("usb_transport", ["usb", "tinyusb", "tusb", "ncm", "cdc"]),
    ("wireless_network", ["wifi", "wi-fi", "bluetooth", "ble", "mesh", "matter", "thread", "zigbee", "esp-now"]),
    ("tooling_ide_ci", ["vscode", "eclipse", "ide", "ci", "build", "tools", "pytest", "esptool", "openocd", "flash"]),
    ("examples_components", ["example", "examples", "component", "components", "registry", "solution", "solutions", "iot library"]),
    ("rust", ["rust", "cargo", "hal"]),
    ("linux_hosted", ["linux", "hosted", "host", "adf"]),
    ("hardware_boards", ["board", "devkit", "box", "eye", "korvo", "pcb", "hardware"]),
    ("audio_media", ["audio", "adf", "speech", "codec", "mp3", "wav", "media"]),
]

HIGH_VALUE_CATEGORIES = {
    "core_sdk",
    "ai_agent",
    "display_camera_video",
    "usb_transport",
    "tooling_ide_ci",
    "examples_components",
    "hardware_boards",
    "audio_media",
}


def utc_now() -> str:
    return dt.datetime.now(dt.UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def request_json(url: str, token: str | None = None) -> tuple[Any, dict[str, str]]:
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "esp32-mod-lab-inventory",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"
    req = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=30) as response:
            raw_headers = {k.lower(): v for k, v in response.headers.items()}
            return json.loads(response.read().decode("utf-8")), raw_headers
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API error {exc.code} for {url}: {body}") from exc


def parse_last_page(link_header: str | None) -> int:
    if not link_header:
        return 1
    for part in link_header.split(","):
        section = part.strip()
        if 'rel="last"' not in section:
            continue
        start = section.find("<")
        end = section.find(">")
        if start == -1 or end == -1:
            continue
        parsed = urllib.parse.urlparse(section[start + 1 : end])
        query = urllib.parse.parse_qs(parsed.query)
        page = query.get("page", ["1"])[0]
        try:
            return int(page)
        except ValueError:
            return 1
    return 1


def classify_repo(repo: dict[str, Any]) -> tuple[list[str], str, list[str]]:
    text = " ".join(
        str(repo.get(key) or "")
        for key in ("name", "full_name", "description", "language", "topics")
    ).lower()
    categories: list[str] = []
    matched_terms: list[str] = []
    for category, terms in CATEGORY_RULES:
        hits = [term for term in terms if term in text]
        if hits:
            categories.append(category)
            matched_terms.extend(hits)

    if not categories:
        categories.append("unclassified")

    score = 0
    score += min(int(repo.get("stargazers_count") or 0) // 500, 3)
    score += min(int(repo.get("forks_count") or 0) // 100, 2)
    score += sum(2 for category in categories if category in HIGH_VALUE_CATEGORIES)
    if repo.get("archived"):
        score -= 2
    if repo.get("disabled"):
        score -= 3

    if score >= 5:
        relevance = "high"
    elif score >= 2:
        relevance = "medium"
    else:
        relevance = "track"
    return categories, relevance, sorted(set(matched_terms))


def compact_repo(repo: dict[str, Any]) -> dict[str, Any]:
    categories, relevance, matched_terms = classify_repo(repo)
    return {
        "name": repo.get("name"),
        "full_name": repo.get("full_name"),
        "html_url": repo.get("html_url"),
        "description": repo.get("description"),
        "homepage": repo.get("homepage"),
        "language": repo.get("language"),
        "topics": repo.get("topics") or [],
        "visibility": repo.get("visibility"),
        "archived": bool(repo.get("archived")),
        "disabled": bool(repo.get("disabled")),
        "fork": bool(repo.get("fork")),
        "stars": repo.get("stargazers_count"),
        "forks": repo.get("forks_count"),
        "open_issues": repo.get("open_issues_count"),
        "default_branch": repo.get("default_branch"),
        "created_at": repo.get("created_at"),
        "updated_at": repo.get("updated_at"),
        "pushed_at": repo.get("pushed_at"),
        "size_kb": repo.get("size"),
        "categories": categories,
        "relevance": relevance,
        "matched_terms": matched_terms,
        "research_notes": "",
    }


def fetch_org_repos(org: str, token: str | None = None, sleep_s: float = 0.0) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    repos: list[dict[str, Any]] = []
    first_url = f"{GITHUB_API}/orgs/{org}/repos?per_page=100&page=1&type=public&sort=full_name"
    first_page, headers = request_json(first_url, token)
    total_pages = parse_last_page(headers.get("link"))
    repos.extend(first_page)
    rate = {k: headers.get(k) for k in ("x-ratelimit-limit", "x-ratelimit-remaining", "x-ratelimit-reset")}

    for page in range(2, total_pages + 1):
        if sleep_s:
            time.sleep(sleep_s)
        url = f"{GITHUB_API}/orgs/{org}/repos?per_page=100&page={page}&type=public&sort=full_name"
        page_repos, headers = request_json(url, token)
        repos.extend(page_repos)
        rate = {k: headers.get(k) for k in ("x-ratelimit-limit", "x-ratelimit-remaining", "x-ratelimit-reset")}
    return repos, {"pages": total_pages, "rate_limit": rate}


def summarize(repos: list[dict[str, Any]]) -> dict[str, Any]:
    by_category: dict[str, int] = {}
    by_relevance: dict[str, int] = {}
    for repo in repos:
        by_relevance[repo["relevance"]] = by_relevance.get(repo["relevance"], 0) + 1
        for category in repo["categories"]:
            by_category[category] = by_category.get(category, 0) + 1

    high_value = [
        {
            "name": repo["full_name"],
            "relevance": repo["relevance"],
            "categories": repo["categories"],
            "stars": repo["stars"],
            "description": repo["description"],
        }
        for repo in sorted(
            repos,
            key=lambda item: (
                {"high": 2, "medium": 1, "track": 0}.get(item["relevance"], 0),
                int(item["stars"] or 0),
            ),
            reverse=True,
        )[:40]
    ]
    return {
        "repo_count": len(repos),
        "by_category": dict(sorted(by_category.items())),
        "by_relevance": dict(sorted(by_relevance.items())),
        "top_research_candidates": high_value,
    }


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Inventory Espressif GitHub repositories.")
    parser.add_argument("--org", default=DEFAULT_ORG)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--token", default=None, help="Optional GitHub token for higher rate limits.")
    parser.add_argument("--sleep", type=float, default=0.0, help="Delay between GitHub API pages.")
    args = parser.parse_args(argv)

    raw_repos, fetch = fetch_org_repos(args.org, args.token, args.sleep)
    repos = [compact_repo(repo) for repo in raw_repos]
    repos.sort(key=lambda item: item["full_name"].lower())

    inventory = {
        "schema": "espressif_github_inventory.v1",
        "generated_at": utc_now(),
        "source": {
            "org": args.org,
            "api": f"{GITHUB_API}/orgs/{args.org}/repos",
            "fetch": fetch,
        },
        "classification": {
            "method": "keyword_and_activity_heuristic",
            "rule_categories": {category: terms for category, terms in CATEGORY_RULES},
            "relevance_values": ["high", "medium", "track"],
            "note": "All repositories remain tracked. Relevance only controls research priority.",
        },
        "summary": summarize(repos),
        "repositories": repos,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(inventory, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Wrote {len(repos)} repositories to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
