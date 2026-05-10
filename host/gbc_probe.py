#!/usr/bin/env python3
"""Host control tool for the ESP32-P4 GBC LCD probe firmware."""

from __future__ import annotations

import argparse
import fcntl
import json
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

try:
    import serial
    import serial.tools.list_ports
except ImportError as exc:  # pragma: no cover - depends on host environment
    raise SystemExit(
        "pyserial is required. Use the ESP-IDF environment or install pyserial."
    ) from exc


DEFAULT_BAUD = 115200
DEFAULT_TIMEOUT_S = 1.0
READY_DRAIN_S = 0.5
PHASE1_BLOCKED_COMMANDS = [
    "MEASURE_CLOCKS",
    "CAPTURE_TIMING 1000",
    "CAPTURE_RAW 64",
    "CAPTURE_FRAME",
    "SET_TRIGGER DCLK rising",
    "DUMP_BUFFER",
]


class ProbeError(RuntimeError):
    """Raised when the probe transport or firmware response is invalid."""


@dataclass(frozen=True)
class ProbeResponse:
    command: str
    raw: str
    data: dict[str, Any]


@dataclass(frozen=True)
class ProbeBinaryResponse:
    command: str
    raw_header: str
    data: dict[str, Any]
    payload: bytes


def list_ports() -> list[str]:
    return [port.device for port in serial.tools.list_ports.comports()]


def autodetect_port() -> str:
    ports = list_ports()
    preferred = [
        port for port in ports if "usbmodem" in port or "usbserial" in port
    ]
    if not preferred:
        raise ProbeError(
            "no USB serial probe port found; pass --port explicitly"
        )
    if len(preferred) > 1:
        raise ProbeError(
            "multiple USB serial ports found; pass --port explicitly: "
            + ", ".join(preferred)
        )
    return preferred[0]


class ProbeClient:
    def __init__(self, port: str, baud: int, timeout_s: float) -> None:
        self._port = port
        self._timeout_s = timeout_s
        self._lock_file = self._acquire_port_lock(port)
        try:
            self._serial = serial.Serial(port, baudrate=baud, timeout=timeout_s)
        except Exception:
            self._release_port_lock()
            raise

    def close(self) -> None:
        try:
            self._serial.close()
        finally:
            self._release_port_lock()

    @staticmethod
    def _lock_path_for_port(port: str) -> Path:
        safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", port.strip("/"))
        lock_dir = Path("/tmp/gbc_p4_probe_locks")
        lock_dir.mkdir(parents=True, exist_ok=True)
        return lock_dir / f"{safe_name}.lock"

    def _acquire_port_lock(self, port: str):
        lock_path = self._lock_path_for_port(port)
        lock_file = lock_path.open("w")
        try:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            lock_file.close()
            raise ProbeError(
                f"serial port {port} is already in use by another probe process; "
                "stop the live backend or existing command before opening it again"
            ) from exc
        lock_file.write(f"{Path(sys.argv[0]).name} {time.time():.3f}\n")
        lock_file.flush()
        return lock_file

    def _release_port_lock(self) -> None:
        lock_file = getattr(self, "_lock_file", None)
        if lock_file is None:
            return
        try:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)
        finally:
            lock_file.close()
            self._lock_file = None

    def drain_startup(self) -> list[str]:
        time.sleep(READY_DRAIN_S)
        available = self._serial.in_waiting
        if available <= 0:
            return []
        raw = self._serial.read(min(8192, available)).decode("utf-8", "replace")
        return [line for line in raw.splitlines() if line.strip()]

    def command(self, command: str) -> ProbeResponse:
        self._discard_stale_input()
        self._serial.write((command + "\n").encode("utf-8"))
        self._serial.flush()

        deadline = time.monotonic() + self._timeout_s
        raw_line = self._read_json_line(deadline)
        try:
            data = json.loads(raw_line)
        except json.JSONDecodeError as exc:
            raise ProbeError(f"invalid JSON response: {raw_line}") from exc
        return ProbeResponse(command=command, raw=raw_line, data=data)

    def command_binary(self, command: str) -> ProbeBinaryResponse:
        self._discard_stale_input()
        self._serial.write((command + "\n").encode("utf-8"))
        self._serial.flush()

        deadline = time.monotonic() + self._timeout_s
        raw_line = self._read_json_line(deadline)
        try:
            data = json.loads(raw_line)
        except json.JSONDecodeError as exc:
            self._discard_stale_input()
            raise ProbeError(f"invalid JSON response: {raw_line}") from exc
        binary_len = int(data.get("binary_len", 0) or 0)
        try:
            payload = self._read_exact(binary_len, deadline)
        except ProbeError:
            self._discard_stale_input()
            raise
        return ProbeBinaryResponse(
            command=command,
            raw_header=raw_line,
            data=data,
            payload=payload,
        )

    def command_binary_sequence(self, command: str, count: int):
        self._discard_stale_input()
        self._serial.write((command + "\n").encode("utf-8"))
        self._serial.flush()

        for _ in range(count):
            deadline = time.monotonic() + self._timeout_s
            raw_line = self._read_json_line(deadline)
            try:
                data = json.loads(raw_line)
            except json.JSONDecodeError as exc:
                self._discard_stale_input()
                raise ProbeError(f"invalid JSON response: {raw_line}") from exc
            binary_len = int(data.get("binary_len", 0) or 0)
            try:
                payload = self._read_exact(binary_len, deadline)
            except ProbeError:
                self._discard_stale_input()
                raise
            yield ProbeBinaryResponse(
                command=command,
                raw_header=raw_line,
                data=data,
                payload=payload,
            )

    def _discard_stale_input(self) -> None:
        try:
            self._serial.reset_input_buffer()
        except serial.SerialException:
            available = self._serial.in_waiting
            if available > 0:
                self._serial.read(available)

    def _read_json_line(self, deadline: float) -> str:
        lines: list[str] = []
        while time.monotonic() < deadline:
            raw_bytes = self._serial.readline()
            raw_line = raw_bytes.decode("utf-8", "replace").strip()
            if not raw_line:
                continue
            lines.append(raw_line)
            json_line = self._extract_json_object(raw_line)
            if json_line is not None:
                data = json.loads(json_line)
                if data.get("event") == "ready":
                    continue
                return json_line
        raise ProbeError(f"no JSON response; received: {lines!r}")

    @staticmethod
    def _extract_json_object(raw_line: str) -> str | None:
        # Binary frame reads can time out a few bytes short. Those leftover image
        # bytes may prefix the next JSON header, so scan for the first valid
        # object instead of requiring the line to start at column zero.
        close_index = raw_line.rfind("}")
        if close_index < 0:
            return None
        for open_index, char in enumerate(raw_line):
            if char != "{":
                continue
            candidate = raw_line[open_index : close_index + 1]
            try:
                json.loads(candidate)
            except json.JSONDecodeError:
                continue
            return candidate
        return None

    def _read_exact(self, size: int, deadline: float) -> bytes:
        chunks: list[bytes] = []
        remaining = size
        while remaining > 0 and time.monotonic() < deadline:
            chunk = self._serial.read(remaining)
            if not chunk:
                continue
            chunks.append(chunk)
            remaining -= len(chunk)
        if remaining:
            received = size - remaining
            raise ProbeError(f"short binary payload: received {received} of {size} bytes")
        return b"".join(chunks)


def print_json(value: Any) -> None:
    print(json.dumps(value, sort_keys=True))


def run_command(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    client = ProbeClient(port, args.baud, args.timeout)
    try:
        startup_lines = client.drain_startup()
        response = client.command(args.command)
    finally:
        client.close()

    if args.show_startup:
        for line in startup_lines:
            print(line, file=sys.stderr)
    if args.raw:
        print(response.raw)
    else:
        print_json(response.data)
    return 0


def run_binary(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    client = ProbeClient(port, args.baud, args.timeout)
    try:
        startup_lines = client.drain_startup()
        response = client.command_binary(args.command)
    finally:
        client.close()

    if args.output:
        Path(args.output).write_bytes(response.payload)
    result = dict(response.data)
    result["payload_len"] = len(response.payload)
    result["output"] = args.output or ""
    if args.show_startup:
        result["startup_lines"] = startup_lines
    print_json(result)
    return 0


def run_binary_sequence(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    client = ProbeClient(port, args.baud, args.timeout)
    started = time.monotonic()
    frames: list[dict[str, Any]] = []
    total_payload_len = 0
    try:
        startup_lines = client.drain_startup()
        for index, response in enumerate(client.command_binary_sequence(args.command, args.count)):
            payload = response.payload
            total_payload_len += len(payload)
            frame = dict(response.data)
            frame["index"] = index
            frame["payload_len"] = len(payload)
            frames.append(frame)
            if args.output_dir:
                output_dir = Path(args.output_dir)
                output_dir.mkdir(parents=True, exist_ok=True)
                (output_dir / f"frame_{index:04d}.bin").write_bytes(payload)
    finally:
        client.close()

    elapsed_s = time.monotonic() - started
    result = {
        "ok": len(frames) == args.count and all(frame.get("ok") for frame in frames),
        "command": args.command,
        "requested_count": args.count,
        "received_count": len(frames),
        "elapsed_s": round(elapsed_s, 3),
        "fps": round(len(frames) / elapsed_s, 3) if elapsed_s > 0 else 0,
        "total_payload_len": total_payload_len,
        "output_dir": args.output_dir or "",
        "frames": frames,
    }
    if args.show_startup:
        result["startup_lines"] = startup_lines
    print_json(result)
    return 0 if result["ok"] else 2


def run_smoke(args: argparse.Namespace) -> int:
    port = args.port or autodetect_port()
    client = ProbeClient(port, args.baud, args.timeout)
    try:
        startup_lines = client.drain_startup()
        responses = [
            client.command("PING"),
            client.command("GET_VERSION"),
            client.command("GET_PINMAP"),
            client.command("EXPORT_STATS"),
        ]
        blocked_responses = [
            client.command(command) for command in PHASE1_BLOCKED_COMMANDS
        ]
    finally:
        client.close()

    errors: list[str] = []
    if responses[0].data.get("response") != "PONG":
        errors.append("PING did not return PONG")
    if responses[1].data.get("phase") != "phase1-electrical-safety":
        errors.append("GET_VERSION returned unexpected phase")
    pinmap = responses[2].data
    if pinmap.get("capture_pin_count") != 0:
        errors.append("GET_PINMAP must report capture_pin_count 0 during Phase 1")
    if pinmap.get("pins") != []:
        errors.append("GET_PINMAP must report no configured pins during Phase 1")
    stats = responses[3].data.get("stats", {})
    if stats.get("capture_pin_count") != 0:
        errors.append("capture_pin_count must remain 0 during Phase 1")
    for response in blocked_responses:
        if response.data.get("ok") is not False:
            errors.append(f"{response.command} unexpectedly succeeded")
        if response.data.get("error") != "no_capture_pins_configured":
            errors.append(f"{response.command} returned unexpected error")

    result = {
        "ok": not errors,
        "port": port,
        "startup_lines": startup_lines,
        "responses": [response.data for response in responses],
        "blocked_responses": [response.data for response in blocked_responses],
        "errors": errors,
    }
    print_json(result)
    return 1 if errors else 0


def run_list_ports(_args: argparse.Namespace) -> int:
    for port in list_ports():
        print(port)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="serial port, for example /dev/cu.usbmodem14201")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S)

    subparsers = parser.add_subparsers(dest="subcommand", required=True)

    subparsers.add_parser("ports", help="list serial ports").set_defaults(
        func=run_list_ports
    )

    command_parser = subparsers.add_parser("command", help="send one firmware command")
    command_parser.add_argument("command")
    command_parser.add_argument("--raw", action="store_true", help="print raw JSON line")
    command_parser.add_argument(
        "--show-startup",
        action="store_true",
        help="print drained startup lines to stderr",
    )
    command_parser.set_defaults(func=run_command)

    binary_parser = subparsers.add_parser("binary", help="send one binary firmware command")
    binary_parser.add_argument("command")
    binary_parser.add_argument("-o", "--output", help="write binary payload to this path")
    binary_parser.add_argument(
        "--show-startup",
        action="store_true",
        help="include drained startup lines in the JSON output",
    )
    binary_parser.set_defaults(func=run_binary)

    sequence_parser = subparsers.add_parser(
        "binary-sequence",
        help="send one firmware command that returns multiple binary frames",
    )
    sequence_parser.add_argument("command")
    sequence_parser.add_argument("--count", type=int, required=True)
    sequence_parser.add_argument("--output-dir", help="write each binary payload to this directory")
    sequence_parser.add_argument(
        "--show-startup",
        action="store_true",
        help="include drained startup lines in the JSON output",
    )
    sequence_parser.set_defaults(func=run_binary_sequence)

    smoke_parser = subparsers.add_parser("smoke", help="run baseline firmware smoke test")
    smoke_parser.set_defaults(func=run_smoke)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return args.func(args)
    except ProbeError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
