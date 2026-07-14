from __future__ import annotations

import hmac
import json
import os
import secrets
import socket
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from .protocol import ProtocolError, token_bytes


class BridgeConfig:
    """Thread-safe persistent bridge identity and paired-device token store."""

    def __init__(self, path: Path | None = None) -> None:
        self.path = path or Path.home() / ".cardbridge" / "config.json"
        self._lock = threading.RLock()
        self.data: dict[str, Any] = {}
        self._load()

    @property
    def bridge_id(self) -> str:
        return str(self.data["bridge_id"])

    @property
    def mac_name(self) -> str:
        return str(self.data["mac_name"])

    def _defaults(self) -> dict[str, Any]:
        hostname = socket.gethostname().split(".")[0] or "Mac"
        return {
            "version": 1,
            "bridge_id": uuid.uuid4().hex,
            "mac_name": hostname,
            "devices": {},
        }

    def _load(self) -> None:
        with self._lock:
            if self.path.exists():
                try:
                    loaded = json.loads(self.path.read_text(encoding="utf-8"))
                except (OSError, json.JSONDecodeError) as exc:
                    raise RuntimeError(f"cannot read bridge config {self.path}: {exc}") from exc
                if not isinstance(loaded, dict):
                    raise RuntimeError(f"bridge config {self.path} is not a JSON object")
                self.data = self._defaults()
                self.data.update(loaded)
                if not isinstance(self.data.get("devices"), dict):
                    self.data["devices"] = {}
            else:
                self.data = self._defaults()
                self.save()

    def save(self) -> None:
        with self._lock:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_suffix(".tmp")
            temporary.write_text(
                json.dumps(self.data, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            os.chmod(temporary, 0o600)
            os.replace(temporary, self.path)
            os.chmod(self.path, 0o600)

    def pair(self, device_id: str, device_name: str = "Cardputer") -> str:
        token = secrets.token_hex(32)  # 32 cryptographically-random bytes.
        with self._lock:
            self.data["devices"][device_id] = {
                "name": device_name,
                "token": token,
                "paired_at": int(time.time()),
            }
            self.save()
        return token

    def token_for(self, device_id: str) -> str | None:
        with self._lock:
            record = self.data["devices"].get(device_id)
            if not isinstance(record, dict):
                return None
            token = record.get("token")
            if not isinstance(token, str):
                return None
            try:
                token_bytes(token)
            except ProtocolError:
                return None
            return token

    def validate(self, device_id: str, token: object) -> bool:
        if not isinstance(token, str):
            return False
        expected = self.token_for(device_id)
        return expected is not None and hmac.compare_digest(expected, token)
