#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import socket
import time
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOCKET = (
    Path.home() / "Library/Application Support/CardBridge/run/agent.sock"
)


def snapshot(path: Path, version: str, build: int) -> dict[str, Any]:
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(2)
    try:
        client.connect(str(path))
        stream = client.makefile("rwb", buffering=0)
        stream.write(
            (
                json.dumps(
                    {
                        "t": "hello",
                        "api": {"major": 1, "minor": 0},
                        "app": {"version": version, "build": build},
                    },
                    separators=(",", ":"),
                )
                + "\n"
            ).encode()
        )
        hello = json.loads(stream.readline())
        if hello.get("t") != "hello_ok" or hello.get("agent") != {
            "version": version,
            "build": build,
        }:
            raise RuntimeError(f"unexpected Agent handshake: {hello}")
        stream.write(b'{"t":"snapshot_req"}\n')
        value = json.loads(stream.readline())
        if value.get("t") != "snapshot":
            raise RuntimeError(f"unexpected snapshot: {value}")
        return value
    finally:
        client.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Monitor CardBridge/M5 stability")
    parser.add_argument("--duration", type=float, default=300)
    parser.add_argument("--interval", type=float, default=2)
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--require-device", action="store_true")
    parser.add_argument("--max-outage", type=float, default=30)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    versions = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    version = versions["mac_app"]["version"]
    build = versions["mac_app"]["build"]

    started = time.monotonic()
    samples = 0
    unavailable_samples = 0
    device_samples = 0
    pids: set[int] = set()
    first_audio: int | None = None
    last_audio: int | None = None
    outage_started: float | None = None
    max_outage = 0.0
    errors: list[str] = []

    while time.monotonic() - started < args.duration:
        now = time.monotonic()
        samples += 1
        try:
            value = snapshot(args.socket, version, build)
            pids.add(int(value["agent"]["pid"]))
            devices = value.get("devices") or []
            if devices:
                device_samples += 1
                audio = sum(int(device.get("audio_packets", 0)) for device in devices)
                first_audio = audio if first_audio is None else first_audio
                last_audio = audio
            elif args.require_device:
                raise RuntimeError("M5 is not connected")
            if outage_started is not None:
                max_outage = max(max_outage, now - outage_started)
                outage_started = None
        except Exception as exc:
            unavailable_samples += 1
            if outage_started is None:
                outage_started = now
            if not errors or errors[-1] != str(exc):
                errors.append(str(exc))
        remaining = args.duration - (time.monotonic() - started)
        if remaining > 0:
            time.sleep(min(args.interval, remaining))

    if outage_started is not None:
        max_outage = max(max_outage, time.monotonic() - outage_started)
    elapsed = time.monotonic() - started
    report = {
        "schema": 1,
        "duration_seconds": round(elapsed, 2),
        "interval_seconds": args.interval,
        "samples": samples,
        "unavailable_samples": unavailable_samples,
        "device_online_percent": round(100 * device_samples / max(samples, 1), 2),
        "max_outage_seconds": round(max_outage, 2),
        "agent_pids": sorted(pids),
        "audio_packet_delta": None
        if first_audio is None or last_audio is None
        else last_audio - first_audio,
        "errors": errors[-10:],
    }
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    print(encoded, end="")
    if args.output is not None:
        args.output.write_text(encoded, encoding="utf-8")

    if unavailable_samples or max_outage > args.max_outage:
        return 1
    if args.require_device and device_samples != samples:
        return 1
    if args.require_device and (report["audio_packet_delta"] or 0) <= 0:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
