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

from ._generated_version import CONFIG_SCHEMA
from .protocol import ProtocolError, token_bytes
from .token_store import TokenStore, WindowsDpapiTokenStore


VOICE_DEFAULTS: dict[str, object] = {
    "feed_output_device": "CABLE Input",
    "virtual_input_device": "CABLE Output",
    "restore_mode": "previous",
    "restore_input_device": "",
    "hotkey": "f13",
    "trigger_mode": "hold",
    "send_enter_on_stop": False,
}
BRIDGE_TRANSPORTS = frozenset({"both", "wifi", "bluetooth"})
QUOTA_DEFAULTS: dict[str, object] = {
    "source": "official",
    "url": "",
    "account_label": "",
    "poll_seconds": 120,
}
QUOTA_SOURCES = frozenset({"official", "custom"})
_QUOTA_SECRET_ID = "custom-quota-api-key"


def default_config_path() -> Path:
    app_data = os.environ.get("APPDATA")
    if app_data:
        return Path(app_data) / "CodexDeck" / "config.json"
    return Path.home() / "AppData" / "Roaming" / "CodexDeck" / "config.json"


class BridgeConfig:
    """Thread-safe persistent bridge identity and paired-device token store."""

    def __init__(
        self,
        path: Path | None = None,
        token_store: TokenStore | None = None,
        quota_token_store: TokenStore | None = None,
    ) -> None:
        production_config = path is None
        self.path = path or default_config_path()
        self._production_config = production_config
        if production_config:
            self._secure_production_directory()
        self._token_store = token_store
        self._quota_token_store = quota_token_store
        if production_config and token_store is None:
            self._token_store = WindowsDpapiTokenStore()
        if production_config and quota_token_store is None:
            self._quota_token_store = WindowsDpapiTokenStore(
                self.path.parent / "quota-secrets",
                purpose="PanPal quota access key",
                secret_label="quota access key",
            )
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
        hostname = socket.gethostname().split(".")[0] or "Windows PC"
        return {
            "config_schema": CONFIG_SCHEMA,
            "bridge_id": uuid.uuid4().hex,
            "mac_name": hostname,
            "devices": {},
            "voice": dict(VOICE_DEFAULTS),
            "quota": dict(QUOTA_DEFAULTS),
            "bridge_transport": "both",
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
                schema = loaded.get("config_schema", loaded.get("version", 1))
                if not isinstance(schema, int) or isinstance(schema, bool) or schema < 1:
                    raise RuntimeError(f"invalid config schema in {self.path}: {schema!r}")
                if schema > CONFIG_SCHEMA:
                    raise RuntimeError(
                        f"config schema {schema} is newer than supported {CONFIG_SCHEMA}"
                    )
                migrated = schema != CONFIG_SCHEMA or "version" in loaded
                loaded.pop("version", None)
                self.data = self._defaults()
                self.data.update(loaded)
                self.data["config_schema"] = CONFIG_SCHEMA
                if not isinstance(self.data.get("devices"), dict):
                    self.data["devices"] = {}
                if not isinstance(self.data.get("voice"), dict):
                    self.data["voice"] = dict(VOICE_DEFAULTS)
                    migrated = True
                else:
                    voice = dict(VOICE_DEFAULTS)
                    voice.update(self.data["voice"])
                    if voice != self.data["voice"]:
                        migrated = True
                    self.data["voice"] = voice
                if self.data.get("bridge_transport") not in BRIDGE_TRANSPORTS:
                    self.data["bridge_transport"] = "both"
                    migrated = True
                raw_quota = self.data.get("quota")
                quota = dict(QUOTA_DEFAULTS)
                if isinstance(raw_quota, dict):
                    quota.update(raw_quota)
                else:
                    migrated = True
                normalized_quota = self._normalize_quota_settings(quota)
                if normalized_quota != raw_quota:
                    migrated = True
                self.data["quota"] = normalized_quota
                if self._token_store is not None:
                    migrated = self._migrate_tokens_to_store() or migrated
                if self._quota_token_store is not None:
                    migrated = self._migrate_quota_key_to_store() or migrated
                if migrated:
                    self.save()
            else:
                self.data = self._defaults()
                self.save()

    def save(self) -> None:
        with self._lock:
            if self._production_config:
                self._secure_production_directory()
            else:
                self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_suffix(".tmp")
            temporary.write_text(
                json.dumps(self.data, indent=2, sort_keys=True) + "\n", encoding="utf-8"
            )
            os.chmod(temporary, 0o600)
            os.replace(temporary, self.path)
            os.chmod(self.path, 0o600)

    def _secure_production_directory(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # DPAPI binds pairing tokens to the current Windows user profile.

    def pair(self, device_id: str, device_name: str = "Cardputer") -> str:
        token = secrets.token_hex(32)  # 32 cryptographically-random bytes.
        with self._lock:
            record = {
                "name": device_name,
                "paired_at": int(time.time()),
            }
            if self._token_store is None:
                record["token"] = token
            else:
                self._token_store.put(device_id, token)
            self.data["devices"][device_id] = record
            self.save()
        return token

    def token_for(self, device_id: str) -> str | None:
        with self._lock:
            record = self.data["devices"].get(device_id)
            if not isinstance(record, dict):
                return None
            token = (
                self._token_store.get(device_id)
                if self._token_store is not None
                else record.get("token")
            )
            if not isinstance(token, str):
                return None
            try:
                token_bytes(token)
            except ProtocolError:
                return None
            return token

    def paired_devices(self) -> list[dict[str, object]]:
        with self._lock:
            result: list[dict[str, object]] = []
            for device_id, record in self.data["devices"].items():
                if not isinstance(device_id, str) or not isinstance(record, dict):
                    continue
                result.append(
                    {
                        "id": device_id,
                        "name": str(record.get("name") or "Cardputer"),
                        "paired_at": int(record.get("paired_at") or 0),
                    }
                )
            return sorted(result, key=lambda item: int(item["paired_at"]), reverse=True)

    def unpair(self, device_id: str) -> bool:
        with self._lock:
            if device_id not in self.data["devices"]:
                return False
            del self.data["devices"][device_id]
            self.save()
            if self._token_store is not None:
                self._token_store.delete(device_id)
            return True

    def _migrate_tokens_to_store(self) -> bool:
        assert self._token_store is not None
        migrated = False
        for device_id, record in self.data["devices"].items():
            if not isinstance(device_id, str) or not isinstance(record, dict):
                continue
            token = record.get("token")
            if token is None:
                continue
            if not isinstance(token, str):
                raise RuntimeError(f"invalid pairing token for {device_id}")
            try:
                token_bytes(token)
            except ProtocolError as exc:
                raise RuntimeError(f"invalid pairing token for {device_id}") from exc
            self._token_store.put(device_id, token)
            del record["token"]
            migrated = True
        return migrated

    def _migrate_quota_key_to_store(self) -> bool:
        assert self._quota_token_store is not None
        quota = self.data.get("quota")
        if not isinstance(quota, dict) or "api_key" not in quota:
            return False
        api_key = quota.pop("api_key")
        if isinstance(api_key, str) and api_key:
            self._validate_ascii_secret(api_key)
            self._quota_token_store.put(_QUOTA_SECRET_ID, api_key)
        return True

    def validate(self, device_id: str, token: object) -> bool:
        if not isinstance(token, str):
            return False
        expected = self.token_for(device_id)
        return expected is not None and hmac.compare_digest(expected, token)

    def voice_settings(self) -> dict[str, object]:
        """Return normalized, non-secret voice-routing preferences."""

        with self._lock:
            raw = self.data.get("voice")
            result = dict(VOICE_DEFAULTS)
            if isinstance(raw, dict):
                result.update(raw)
            if result.get("restore_mode") not in {"previous", "fixed", "none"}:
                result["restore_mode"] = "previous"
            if result.get("trigger_mode") not in {"hold", "toggle"}:
                result["trigger_mode"] = "hold"
            for key in (
                "feed_output_device",
                "virtual_input_device",
                "restore_input_device",
                "hotkey",
            ):
                if not isinstance(result.get(key), str):
                    result[key] = str(VOICE_DEFAULTS[key])
            result["send_enter_on_stop"] = bool(result.get("send_enter_on_stop"))
            return result

    def update_voice_settings(self, values: dict[str, object]) -> dict[str, object]:
        allowed = set(VOICE_DEFAULTS)
        with self._lock:
            current = self.voice_settings()
            current.update({key: value for key, value in values.items() if key in allowed})
            self.data["voice"] = current
            normalized = self.voice_settings()
            self.data["voice"] = normalized
            self.save()
            return dict(normalized)

    @staticmethod
    def _validate_ascii_secret(value: str) -> None:
        try:
            value.encode("ascii")
        except UnicodeEncodeError as exc:
            raise ValueError("quota access key must contain ASCII characters only") from exc

    @staticmethod
    def _normalize_quota_settings(values: dict[str, object]) -> dict[str, object]:
        source = str(values.get("source") or "official")
        if source not in QUOTA_SOURCES:
            source = "official"
        url = values.get("url")
        account_label = values.get("account_label")
        poll_seconds = values.get("poll_seconds")
        if not isinstance(poll_seconds, int) or isinstance(poll_seconds, bool):
            poll_seconds = int(QUOTA_DEFAULTS["poll_seconds"])
        return {
            "source": source,
            "url": str(url).strip() if isinstance(url, str) else "",
            "account_label": (
                str(account_label).strip() if isinstance(account_label, str) else ""
            ),
            "poll_seconds": max(120, min(3600, poll_seconds)),
            **(
                {"api_key": values["api_key"]}
                if isinstance(values.get("api_key"), str) and values["api_key"]
                else {}
            ),
        }

    def quota_settings(self, *, include_secret: bool = False) -> dict[str, object]:
        """Return normalized custom-quota preferences without leaking its key."""

        with self._lock:
            raw = self.data.get("quota")
            normalized = self._normalize_quota_settings(
                raw if isinstance(raw, dict) else {}
            )
            stored_key = (
                self._quota_token_store.get(_QUOTA_SECRET_ID)
                if self._quota_token_store is not None
                else normalized.get("api_key")
            )
            result = {key: value for key, value in normalized.items() if key != "api_key"}
            result["api_key_present"] = bool(stored_key)
            if include_secret:
                result["api_key"] = str(stored_key or "")
            return result

    def update_quota_settings(self, values: dict[str, object]) -> dict[str, object]:
        allowed = set(QUOTA_DEFAULTS)
        with self._lock:
            requested_source = values.get("source")
            if requested_source is not None and (
                not isinstance(requested_source, str)
                or requested_source not in QUOTA_SOURCES
            ):
                raise ValueError("quota source must be official or custom")
            current = self._normalize_quota_settings(
                self.data.get("quota") if isinstance(self.data.get("quota"), dict) else {}
            )
            current.update({key: value for key, value in values.items() if key in allowed})
            normalized = self._normalize_quota_settings(current)
            api_key = values.get("api_key")
            if isinstance(api_key, str) and api_key:
                self._validate_ascii_secret(api_key)
                if self._quota_token_store is not None:
                    self._quota_token_store.put(_QUOTA_SECRET_ID, api_key)
                else:
                    normalized["api_key"] = api_key
            elif values.get("clear_api_key") is True:
                if self._quota_token_store is not None:
                    self._quota_token_store.delete(_QUOTA_SECRET_ID)
                normalized.pop("api_key", None)
            elif self._quota_token_store is None and isinstance(
                self.data.get("quota"), dict
            ):
                existing = self.data["quota"].get("api_key")
                if isinstance(existing, str) and existing:
                    normalized["api_key"] = existing
            self.data["quota"] = normalized
            self.save()
            return self.quota_settings()

    def bridge_transport(self) -> str:
        value = self.data.get("bridge_transport")
        return str(value) if value in BRIDGE_TRANSPORTS else "both"

    def set_bridge_transport(self, value: str) -> str:
        if value not in BRIDGE_TRANSPORTS:
            raise ValueError("bridge transport must be both, wifi, or bluetooth")
        with self._lock:
            self.data["bridge_transport"] = value
            self.save()
        return value
