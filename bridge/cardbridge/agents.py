from __future__ import annotations

import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable


AGENT_LIMIT = 8
_ATTENTION_ORDER = {
    "needs_input": 0,
    "blocked": 1,
    "ready": 2,
    "running": 3,
    "idle": 4,
    "offline": 5,
}


def _now_ms() -> int:
    return int(time.time() * 1000)


def _text(value: object, limit: int) -> str:
    if not isinstance(value, str):
        return ""
    value = " ".join(value.replace("\x00", "").split())
    return value[:limit]


@dataclass
class QuotaWindow:
    remaining_percent: int
    used_percent: int
    duration_mins: int | None
    resets_at: int | None

    def as_dict(self) -> dict[str, int | None]:
        return {
            "remaining": self.remaining_percent,
            "used": self.used_percent,
            "duration_mins": self.duration_mins,
            "resets_at": self.resets_at,
        }


@dataclass
class AgentSession:
    id: str
    title: str = "Codex session"
    project: str = ""
    status: str = "idle"
    # Running is split into two visual phases without changing its priority:
    # thinking between hooks, or actively executing a tool.
    phase: str = ""
    activity: str = "Session ready"
    unread: bool = False
    updated_ms: int = 0

    def as_dict(self) -> dict[str, object]:
        return {
            "id": self.id,
            "title": self.title,
            "project": self.project,
            "status": self.status,
            "phase": self.phase,
            "activity": self.activity,
            "unread": self.unread,
            "updated_ms": self.updated_ms,
        }


class AgentStore:
    """Small, privacy-trimmed view of Codex sessions for the Cardputer."""

    def __init__(self, on_change: Callable[[], None] | None = None) -> None:
        self.sessions: dict[str, AgentSession] = {}
        self.focus_id = ""
        self.focus_seq = 0
        self.weekly: QuotaWindow | None = None
        self.five_hour: QuotaWindow | None = None
        self.quota_available = False
        self.seq = 0
        self._on_change = on_change

    def set_on_change(self, callback: Callable[[], None] | None) -> None:
        self._on_change = callback

    def _changed(self) -> None:
        self.seq += 1
        if self._on_change is not None:
            self._on_change()

    def update_threads(self, threads: list[dict[str, Any]]) -> None:
        changed = False
        for thread in threads:
            session_id = _text(thread.get("id"), 64)
            if not session_id:
                continue
            session = self.sessions.get(session_id)
            if session is None:
                session = AgentSession(id=session_id)
                self.sessions[session_id] = session
                changed = True
            title = _text(thread.get("name") or thread.get("preview"), 48)
            cwd = _text(thread.get("cwd"), 256)
            project = _text(Path(cwd).name if cwd else "", 24)
            updated = thread.get("updatedAt")
            updated_ms = int(updated) * 1000 if isinstance(updated, (int, float)) else 0
            if title and title != session.title:
                session.title = title
                changed = True
            if project != session.project:
                session.project = project
                changed = True
            if updated_ms > session.updated_ms:
                session.updated_ms = updated_ms
                changed = True
        if not self.focus_id and threads:
            candidate = _text(threads[0].get("id"), 64)
            if candidate:
                self.focus_id = candidate
                self.focus_seq += 1
                changed = True
        self._trim()
        if changed:
            self._changed()

    def apply_hook_event(self, payload: dict[str, Any]) -> None:
        session_id = _text(payload.get("session_id"), 64)
        if not session_id:
            return
        event = _text(payload.get("event") or payload.get("hook_event_name"), 48)
        event_key = event.replace("_", "").lower()
        cwd = _text(payload.get("cwd"), 256)
        tool_name = _text(payload.get("tool_name"), 80)
        session = self.sessions.get(session_id)
        if session is None:
            session = AgentSession(id=session_id)
            self.sessions[session_id] = session
        if cwd:
            session.project = _text(Path(cwd).name, 24)
        session.updated_ms = int(payload.get("timestamp_ms") or _now_ms())

        if event_key == "sessionstart":
            session.status = "idle"
            session.phase = ""
            session.activity = "Session ready"
            session.unread = False
            if not self.focus_id:
                self.focus_id = session_id
        elif event_key == "userpromptsubmit":
            session.status = "running"
            session.phase = "thinking"
            session.activity = "Understanding the task..."
            session.unread = False
            self.focus_id = session_id
            # Increment even if the same session receives another prompt; a
            # Cardputer user may currently be browsing a different session
            # and should be returned to this newly operated one.
            self.focus_seq += 1
        elif event_key == "permissionrequest":
            session.status = "needs_input"
            session.phase = ""
            session.activity = "Waiting for your approval"
            session.unread = True
        elif event_key in {"pretooluse", "posttooluse"}:
            asks_user = any(
                marker in tool_name.lower()
                for marker in ("request_user_input", "askuserquestion", "elicitation")
            )
            if event_key == "pretooluse" and asks_user:
                session.status = "needs_input"
                session.phase = ""
                session.activity = "Waiting for your answer"
                session.unread = True
            elif event_key == "posttooluse":
                # PostToolUse means the command has returned and Codex is
                # reasoning over its result. Do not leave the device claiming
                # that the command is still running.
                session.status = "running"
                session.phase = "thinking"
                session.activity = "Thinking..."
                session.unread = False
            else:
                session.status = "running"
                session.phase = "tool"
                session.activity = _activity_for_tool(tool_name)
                session.unread = False
        elif event_key in {"stop", "subagentstop"}:
            session.status = "ready"
            session.phase = ""
            session.activity = "Task completed"
            session.unread = True
        elif event_key in {"error", "systemerror", "failed"}:
            session.status = "blocked"
            session.phase = ""
            session.activity = "Task encountered a problem"
            session.unread = True
        else:
            return
        self._trim()
        self._changed()

    def set_quota_available(self, available: bool) -> None:
        available = bool(available)
        changed = available != self.quota_available
        if changed or not available:
            # Never carry subscription windows across an auth-mode change.
            changed = changed or self.weekly is not None or self.five_hour is not None
            self.weekly = None
            self.five_hour = None
        self.quota_available = available
        if changed:
            self._changed()

    def update_rate_limits(self, result: dict[str, Any]) -> None:
        if not self.quota_available:
            return
        buckets = result.get("rateLimitsByLimitId")
        snapshot: dict[str, Any] | None = None
        if isinstance(buckets, dict):
            candidate = buckets.get("codex")
            if isinstance(candidate, dict):
                snapshot = candidate
            else:
                snapshot = next((v for v in buckets.values() if isinstance(v, dict)), None)
        if snapshot is None and isinstance(result.get("rateLimits"), dict):
            snapshot = result["rateLimits"]
        if snapshot is None:
            return

        weekly: QuotaWindow | None = None
        five_hour: QuotaWindow | None = None
        for key in ("primary", "secondary"):
            raw = snapshot.get(key)
            if not isinstance(raw, dict) or not isinstance(raw.get("usedPercent"), int):
                continue
            used = max(0, min(100, int(raw["usedPercent"])))
            duration = raw.get("windowDurationMins")
            duration = int(duration) if isinstance(duration, int) else None
            window = QuotaWindow(
                remaining_percent=100 - used,
                used_percent=used,
                duration_mins=duration,
                resets_at=int(raw["resetsAt"]) if isinstance(raw.get("resetsAt"), int) else None,
            )
            if duration is not None and 280 <= duration <= 320:
                five_hour = window
            elif duration is not None and 9_500 <= duration <= 10_500:
                weekly = window
            elif weekly is None:
                weekly = window
            elif five_hour is None:
                five_hour = window
        if weekly != self.weekly or five_hour != self.five_hour:
            self.weekly = weekly
            self.five_hour = five_hour
            self._changed()

    def acknowledge(self, session_id: str) -> bool:
        session = self.sessions.get(session_id)
        if session is None:
            return False
        changed = session.unread or session.status in {"ready", "blocked"}
        session.unread = False
        if session.status in {"ready", "blocked"}:
            session.status = "idle"
            session.phase = ""
            session.activity = "Session ready"
        if changed:
            self._changed()
        return changed

    def snapshot(self, limit: int = AGENT_LIMIT) -> dict[str, object]:
        sessions = sorted(
            self.sessions.values(),
            key=lambda item: (_ATTENTION_ORDER.get(item.status, 9), -item.updated_ms),
        )
        selected = sessions[: max(1, min(AGENT_LIMIT, limit))]
        # The attention sort must never evict the session that owns the pet.
        # Otherwise the focus_id would point at an item the Cardputer cannot
        # render or select.
        focused = self.sessions.get(self.focus_id)
        if focused is not None and focused not in selected:
            selected[-1] = focused
        return {
            "seq": self.seq,
            "focus_id": self.focus_id,
            "focus_seq": self.focus_seq,
            "quota": {
                "available": self.quota_available,
                "weekly": self.weekly.as_dict()
                if self.quota_available and self.weekly
                else None,
                "five_hour": self.five_hour.as_dict()
                if self.quota_available and self.five_hour
                else None,
            },
            "items": [session.as_dict() for session in selected],
        }

    def _trim(self) -> None:
        if len(self.sessions) <= 32:
            return
        keep = sorted(self.sessions.values(), key=lambda item: item.updated_ms, reverse=True)[:32]
        self.sessions = {item.id: item for item in keep}


def _activity_for_tool(tool_name: str) -> str:
    lowered = tool_name.lower()
    if any(token in lowered for token in ("apply_patch", "edit", "write")):
        return "Editing project files..."
    if any(token in lowered for token in ("bash", "shell", "exec", "terminal")):
        return "Running a command..."
    if any(token in lowered for token in ("web", "search", "browser")):
        return "Searching references..."
    if "image" in lowered:
        return "Working with an image..."
    if "test" in lowered:
        return "Running tests..."
    return "Working on the task..."
