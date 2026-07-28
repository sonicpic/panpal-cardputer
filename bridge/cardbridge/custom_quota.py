from __future__ import annotations

import asyncio
from datetime import datetime
import ipaddress
import json
import logging
import time
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

from ._generated_version import AGENT_VERSION
from .agents import AgentStore
from .config import BridgeConfig


LOG = logging.getLogger("cardbridge.quota")
MAX_QUOTA_RESPONSE_BYTES = 64 * 1024
QUOTA_REQUEST_TIMEOUT_SECONDS = 10
_WINDOW_MINUTES = {
    "5h": 300,
    "five_hour": 300,
    "five-hour": 300,
    "primary": 300,
    "weekly": 10_080,
    "week": 10_080,
    "secondary": 10_080,
}


class QuotaResponseError(ValueError):
    pass


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):  # type: ignore[no-untyped-def]
        return None


def validate_quota_url(value: object) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ValueError("custom quota URL is required")
    url = value.strip()
    parsed = urlsplit(url)
    if parsed.username or parsed.password:
        raise ValueError("custom quota URL must not contain credentials")
    if not parsed.hostname or parsed.fragment:
        raise ValueError("custom quota URL is invalid")
    if parsed.scheme == "https":
        return url
    if parsed.scheme != "http":
        raise ValueError("custom quota URL must use HTTPS")
    host = parsed.hostname.casefold()
    if host == "localhost":
        return url
    try:
        if ipaddress.ip_address(host).is_loopback:
            return url
    except ValueError:
        pass
    raise ValueError("custom quota URL must use HTTPS (HTTP is allowed only on localhost)")


def _percent(value: object, name: str) -> int | None:
    if value is None:
        return None
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise QuotaResponseError(f"{name} must be a number")
    rounded = int(float(value) + 0.5)
    if rounded < 0 or rounded > 100:
        raise QuotaResponseError(f"{name} must be between 0 and 100")
    return rounded


def _reset_timestamp(value: object) -> int | None:
    if value is None:
        return None
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        timestamp = int(value)
        return timestamp // 1000 if timestamp >= 10_000_000_000 else timestamp
    if isinstance(value, str):
        try:
            return int(datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp())
        except ValueError as exc:
            raise QuotaResponseError("reset_at must be an ISO-8601 timestamp") from exc
    raise QuotaResponseError("reset_at must be an ISO-8601 timestamp")


def normalize_custom_quota(
    payload: object, account_label: str = ""
) -> tuple[dict[str, Any], dict[str, str]]:
    """Average real weekly quotas and mirror them into legacy 5h + weekly slots."""

    if not isinstance(payload, dict):
        raise QuotaResponseError("quota response must be a JSON object")
    accounts = payload.get("accounts")
    if not isinstance(accounts, list) or not accounts:
        raise QuotaResponseError("quota response has no accounts")
    candidates = [item for item in accounts if isinstance(item, dict)]
    if not candidates:
        raise QuotaResponseError("quota response has no valid account")
    requested = account_label.strip()
    if requested:
        selected = next(
            (
                item
                for item in candidates
                if str(item.get("label") or "").casefold() == requested.casefold()
            ),
            None,
        )
        if selected is None:
            raise QuotaResponseError(f"account label not found: {requested}")
        selected_accounts = [selected]
    else:
        selected_accounts = candidates

    remaining_values: list[int] = []
    reset_values: list[int] = []
    plans: set[str] = set()
    for account in selected_accounts:
        plan = str(account.get("plan") or "")
        limits = account.get("limits")
        if not isinstance(limits, list):
            continue
        weekly = next(
            (
                raw
                for raw in limits
                if isinstance(raw, dict)
                and _WINDOW_MINUTES.get(
                    str(raw.get("window") or "").strip().casefold()
                )
                == 10_080
            ),
            None,
        )
        if weekly is None:
            continue
        used = _percent(weekly.get("used_percent"), "used_percent")
        remaining = _percent(weekly.get("remaining_percent"), "remaining_percent")
        if used is None and remaining is None:
            continue
        if remaining is None:
            remaining = 100 - int(used)
        if used is not None and abs((100 - used) - remaining) > 1:
            raise QuotaResponseError("used_percent and remaining_percent disagree")
        remaining_values.append(remaining)
        if plan:
            plans.add(plan)
        reset = _reset_timestamp(weekly.get("reset_at"))
        if reset is not None:
            reset_values.append(reset)
    if not remaining_values:
        raise QuotaResponseError("selected accounts have no weekly limit")

    average_remaining = int(sum(remaining_values) / len(remaining_values) + 0.5)
    average_used = 100 - average_remaining
    # Aggregate accounts do not share one reset instant. For a single selected
    # account, retaining its weekly reset remains useful; the legacy 5h slot
    # mirrors the same value only so older firmware can keep its two-row UI.
    reset_at = reset_values[0] if len(selected_accounts) == 1 and reset_values else None
    result = {
        "rateLimits": {
            "primary": {
                "usedPercent": average_used,
                "windowDurationMins": 300,
                "resetsAt": reset_at,
            },
            "secondary": {
                "usedPercent": average_used,
                "windowDurationMins": 10_080,
                "resetsAt": reset_at,
            },
        }
    }
    metadata = {
        "account_label": (
            str(selected_accounts[0].get("label") or requested or "default")
            if requested
            else f"全部 {len(remaining_values)} 个账户（平均）"
        ),
        "plan": next(iter(plans)) if len(plans) == 1 else "mixed",
        "updated_at": str(payload.get("updated_at") or ""),
    }
    return result, metadata


def fetch_custom_quota(settings: dict[str, object]) -> tuple[dict[str, Any], dict[str, str]]:
    url = validate_quota_url(settings.get("url"))
    headers = {
        "Accept": "application/json",
        "User-Agent": f"PanPal/{AGENT_VERSION}",
    }
    api_key = settings.get("api_key")
    if isinstance(api_key, str) and api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    request = Request(url, headers=headers, method="GET")
    opener = build_opener(_NoRedirect())
    try:
        with opener.open(request, timeout=QUOTA_REQUEST_TIMEOUT_SECONDS) as response:
            content_type = str(response.headers.get("Content-Type") or "")
            if "json" not in content_type.casefold():
                raise QuotaResponseError("quota endpoint did not return JSON")
            body = response.read(MAX_QUOTA_RESPONSE_BYTES + 1)
    except HTTPError as exc:
        raise QuotaResponseError(f"quota endpoint returned HTTP {exc.code}") from exc
    except URLError as exc:
        raise QuotaResponseError(f"quota endpoint is unavailable: {exc.reason}") from exc
    if len(body) > MAX_QUOTA_RESPONSE_BYTES:
        raise QuotaResponseError("quota response is larger than 64 KiB")
    try:
        payload = json.loads(body.decode("utf-8-sig"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise QuotaResponseError("quota endpoint returned invalid JSON") from exc
    return normalize_custom_quota(payload, str(settings.get("account_label") or ""))


class CustomQuotaMonitor:
    def __init__(self, store: AgentStore, config: BridgeConfig) -> None:
        self.store = store
        self.config = config
        self.settings_public = config.quota_settings()
        self.task: asyncio.Task[None] | None = None
        self._stopping = False
        self.last_success_ms = 0
        self.last_error = ""
        self.account_label = ""
        self.plan = ""
        self.upstream_updated_at = ""

    @property
    def enabled(self) -> bool:
        return self.settings_public.get("source") == "custom"

    async def start(self) -> None:
        if not self.enabled or self.task is not None:
            return
        self._stopping = False
        self.task = asyncio.create_task(self._run())

    async def stop(self) -> None:
        self._stopping = True
        if self.task is not None:
            self.task.cancel()
            try:
                await self.task
            except asyncio.CancelledError:
                pass
            self.task = None

    async def refresh(self, settings: dict[str, object] | None = None) -> dict[str, object]:
        effective = settings or self.config.quota_settings(include_secret=True)
        limits, metadata = await asyncio.to_thread(fetch_custom_quota, effective)
        self.store.set_quota_mode("subscription")
        self.store.update_rate_limits(limits)
        self.last_success_ms = int(time.time() * 1000)
        self.last_error = ""
        self.account_label = metadata["account_label"]
        self.plan = metadata["plan"]
        self.upstream_updated_at = metadata["updated_at"]
        return self.snapshot()

    async def _run(self) -> None:
        while not self._stopping:
            settings = self.config.quota_settings(include_secret=True)
            try:
                await self.refresh(settings)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                # Keep the last successful windows visible during a temporary
                # proxy outage; the status snapshot marks them as stale.
                self.last_error = str(exc)
                LOG.warning("custom quota refresh failed: %s", exc)
            await asyncio.sleep(int(settings.get("poll_seconds") or 120))

    def snapshot(self) -> dict[str, object]:
        settings = self.settings_public
        return {
            "source": settings["source"],
            "url_configured": bool(settings["url"]),
            "api_key_present": bool(settings["api_key_present"]),
            "account_label": self.account_label or settings["account_label"],
            "plan": self.plan,
            "last_success_ms": self.last_success_ms or None,
            "last_error": self.last_error,
            "stale": bool(self.last_success_ms and self.last_error),
            "upstream_updated_at": self.upstream_updated_at,
        }
