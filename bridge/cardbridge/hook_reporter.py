from __future__ import annotations

import json
import os
import socket
import sys
import time


def main() -> int:
    try:
        raw = json.load(sys.stdin)
        if not isinstance(raw, dict):
            return 0
        tool_name = raw.get("tool_name") or raw.get("toolName") or ""
        payload = {
            "event": raw.get("hook_event_name") or raw.get("hookEventName") or "",
            "session_id": raw.get("session_id") or "",
            "turn_id": raw.get("turn_id") or "",
            "cwd": raw.get("cwd") or "",
            "tool_name": tool_name if isinstance(tool_name, str) else "",
            "timestamp_ms": int(time.time() * 1000),
        }
        port = int(os.environ.get("CARDBRIDGE_HOOK_PORT", "7790"))
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as client:
            client.settimeout(0.05)
            client.sendto(
                json.dumps(payload, separators=(",", ":")).encode(),
                ("127.0.0.1", port),
            )
    except Exception:
        pass
    return 0
