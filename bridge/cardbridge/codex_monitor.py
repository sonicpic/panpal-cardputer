from __future__ import annotations

import asyncio
import json
import logging
import os
import shutil
from collections.abc import Awaitable, Callable
from pathlib import Path
from typing import Any

from ._generated_version import AGENT_VERSION

from .agents import AgentStore

LOG = logging.getLogger("cardbridge.codex")
APP_SERVER_STREAM_LIMIT = 4 * 1024 * 1024


def find_codex_candidates(
    *,
    path_lookup: Callable[[str], str | None] | None = None,
    home: Path | None = None,
    bundled: Path | None = None,
) -> list[str]:
    """Return usable Codex executables in compatibility-first order.

    The ChatGPT-bundled binary can use a different model-provider contract from
    the user's CLI installation. Prefer the CLI that owns the active config,
    while retaining the bundled binary as a fallback for OAuth installations.
    """

    lookup = path_lookup or shutil.which
    user_home = home or Path.home()
    bundled_path = bundled or Path("/Applications/ChatGPT.app/Contents/Resources/codex")
    raw_candidates: list[str | Path | None] = [
        lookup("codex"),
        user_home / ".npm-global" / "bin" / "codex",
        user_home / ".local" / "bin" / "codex",
        Path("/opt/homebrew/bin/codex"),
        Path("/usr/local/bin/codex"),
        bundled_path,
    ]
    candidates: list[str] = []
    seen: set[str] = set()
    for candidate in raw_candidates:
        if candidate is None:
            continue
        path = Path(candidate).expanduser()
        if not path.is_file() or not os.access(path, os.X_OK):
            continue
        identity = os.path.realpath(path)
        if identity in seen:
            continue
        seen.add(identity)
        candidates.append(str(path))
    return candidates


def find_codex() -> str | None:
    candidates = find_codex_candidates()
    return candidates[0] if candidates else None


def quota_available_from_account(result: dict[str, Any]) -> bool:
    """ChatGPT subscription windows apply only to active ChatGPT auth."""

    account = result.get("account")
    return (
        result.get("requiresOpenaiAuth") is True
        and isinstance(account, dict)
        and account.get("type") == "chatgpt"
    )


class CodexAppServerClient:
    def __init__(
        self,
        executable: str,
        notification_handler: Callable[[str, dict[str, Any]], Awaitable[None]] | None = None,
    ) -> None:
        self.executable = executable
        self.notification_handler = notification_handler
        self.process: asyncio.subprocess.Process | None = None
        self._reader_task: asyncio.Task[None] | None = None
        self._stderr_task: asyncio.Task[None] | None = None
        self._pending: dict[int, asyncio.Future[dict[str, Any]]] = {}
        self._next_id = 1

    async def start(self) -> None:
        if self.process is not None:
            return
        self.process = await asyncio.create_subprocess_exec(
            self.executable,
            "app-server",
            "--stdio",
            stdin=asyncio.subprocess.PIPE,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
            # thread/list is one JSONL record and can exceed asyncio's 64 KiB
            # default even though CardBridge later trims it to eight sessions.
            limit=APP_SERVER_STREAM_LIMIT,
        )
        self._reader_task = asyncio.create_task(self._read_stdout())
        self._stderr_task = asyncio.create_task(self._read_stderr())
        await self.request(
            "initialize",
            {
                "clientInfo": {
                    "name": "cardbridge",
                    "title": "CardBridge Agent Monitor",
                    "version": AGENT_VERSION,
                }
            },
        )
        await self.notify("initialized", {})

    async def stop(self) -> None:
        process = self.process
        self.process = None
        if process is not None and process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), 2)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
        for task in (self._reader_task, self._stderr_task):
            if task is not None:
                task.cancel()
        self._reader_task = None
        self._stderr_task = None

    async def request(self, method: str, params: object) -> dict[str, Any]:
        if self.process is None or self.process.stdin is None:
            raise RuntimeError("Codex App Server is not running")
        request_id = self._next_id
        self._next_id += 1
        loop = asyncio.get_running_loop()
        future: asyncio.Future[dict[str, Any]] = loop.create_future()
        self._pending[request_id] = future
        await self._write({"method": method, "id": request_id, "params": params})
        try:
            response = await asyncio.wait_for(future, 20)
        finally:
            self._pending.pop(request_id, None)
        if "error" in response:
            raise RuntimeError(f"{method}: {response['error']}")
        result = response.get("result", {})
        if not isinstance(result, dict):
            raise RuntimeError(f"{method}: unexpected response")
        return result

    async def notify(self, method: str, params: object) -> None:
        await self._write({"method": method, "params": params})

    async def _write(self, message: dict[str, object]) -> None:
        assert self.process is not None and self.process.stdin is not None
        self.process.stdin.write(
            (json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
        )
        await self.process.stdin.drain()

    async def _read_stdout(self) -> None:
        assert self.process is not None and self.process.stdout is not None
        try:
            while line := await self.process.stdout.readline():
                try:
                    message = json.loads(line)
                except json.JSONDecodeError:
                    continue
                request_id = message.get("id")
                if isinstance(request_id, int) and request_id in self._pending:
                    future = self._pending[request_id]
                    if not future.done():
                        future.set_result(message)
                    continue
                method = message.get("method")
                params = message.get("params")
                if (
                    isinstance(method, str)
                    and isinstance(params, dict)
                    and self.notification_handler is not None
                ):
                    await self.notification_handler(method, params)
        finally:
            error = RuntimeError("Codex App Server closed")
            for future in self._pending.values():
                if not future.done():
                    future.set_exception(error)

    async def _read_stderr(self) -> None:
        assert self.process is not None and self.process.stderr is not None
        while line := await self.process.stderr.readline():
            LOG.debug("app-server: %s", line.decode(errors="replace").rstrip())


class CodexMonitor:
    def __init__(self, store: AgentStore, executable: str | None = None) -> None:
        self.store = store
        self.executables = [executable] if executable else find_codex_candidates()
        self.executable = self.executables[0] if self.executables else None
        self.client: CodexAppServerClient | None = None
        self.task: asyncio.Task[None] | None = None
        self._stopping = False

    async def start(self) -> None:
        if not self.executables:
            LOG.warning("Codex executable not found; agent monitor disabled")
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
        if self.client is not None:
            await self.client.stop()
            self.client = None

    async def refresh(self) -> None:
        if self.client is None:
            return
        threads = await self.client.request(
            "thread/list",
            {
                "limit": 32,
                "sortKey": "updated_at",
                "sortDirection": "desc",
                "useStateDbOnly": True,
            },
        )
        data = threads.get("data")
        if isinstance(data, list):
            self.store.update_threads([item for item in data if isinstance(item, dict)])

        # Session history is local and remains useful in API/custom-provider
        # mode. Subscription quota is a separate, optional ChatGPT-only view.
        try:
            account = await self.client.request(
                "account/read", {"refreshToken": False}
            )
        except Exception as exc:
            LOG.debug("Codex account mode unavailable: %s", exc)
            self.store.set_quota_available(False)
            return

        quota_available = quota_available_from_account(account)
        self.store.set_quota_available(quota_available)
        if not quota_available:
            return

        try:
            limits = await self.client.request("account/rateLimits/read", None)
        except Exception as exc:
            # A quota endpoint failure must never take Session Pet offline.
            LOG.debug("Codex rate limits unavailable: %s", exc)
            return
        self.store.update_rate_limits(limits)

    async def _run(self) -> None:
        while not self._stopping:
            for executable in self.executables:
                if self._stopping:
                    return
                self.executable = executable
                try:
                    self.client = CodexAppServerClient(executable, self._notification)
                    await self.client.start()
                    # Validate history access before considering this candidate
                    # connected. A provider-incompatible binary often completes
                    # initialize and then exits on the first real request.
                    await self.refresh()
                    LOG.info("Codex agent monitor connected via %s", executable)
                    while not self._stopping:
                        await asyncio.sleep(30)
                        await self.refresh()
                except asyncio.CancelledError:
                    raise
                except Exception as exc:
                    LOG.warning("Codex monitor failed via %s: %s", executable, exc)
                finally:
                    if self.client is not None:
                        await self.client.stop()
                        self.client = None
            if not self._stopping:
                await asyncio.sleep(10)

    async def _notification(self, method: str, params: dict[str, Any]) -> None:
        if method == "account/rateLimits/updated":
            rate_limits = params.get("rateLimits")
            if self.store.quota_available and isinstance(rate_limits, dict):
                self.store.update_rate_limits({"rateLimits": rate_limits})
        elif method == "account/updated":
            # A non-ChatGPT login can be hidden immediately. ChatGPT is only
            # enabled after account/read also confirms the active provider
            # requires OpenAI auth, avoiding stale cached-account metadata.
            auth_mode = params.get("authMode")
            if isinstance(auth_mode, str) and auth_mode != "chatgpt":
                self.store.set_quota_available(False)


class CodexHookProtocol(asyncio.DatagramProtocol):
    def __init__(self, store: AgentStore) -> None:
        self.store = store

    def datagram_received(self, data: bytes, address: tuple[str, int]) -> None:
        try:
            payload = json.loads(data)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        if isinstance(payload, dict):
            self.store.apply_hook_event(payload)


async def start_hook_receiver(
    store: AgentStore, port: int = 7790
) -> tuple[asyncio.DatagramTransport, int]:
    loop = asyncio.get_running_loop()
    transport, _ = await loop.create_datagram_endpoint(
        lambda: CodexHookProtocol(store), local_addr=("127.0.0.1", port)
    )
    socket = transport.get_extra_info("socket")
    return transport, int(socket.getsockname()[1])
