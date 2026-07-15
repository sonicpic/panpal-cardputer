from __future__ import annotations

import asyncio
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from cardbridge.agents import AgentStore
from cardbridge.codex_monitor import (
    APP_SERVER_STREAM_LIMIT,
    CodexMonitor,
    find_codex_candidates,
    quota_available_from_account,
)


class CodexMonitorHelpersTests(unittest.TestCase):
    def test_account_mode_requires_active_chatgpt_auth_for_quota(self) -> None:
        self.assertTrue(
            quota_available_from_account(
                {
                    "requiresOpenaiAuth": True,
                    "account": {"type": "chatgpt", "planType": "plus"},
                }
            )
        )
        self.assertFalse(
            quota_available_from_account(
                {"requiresOpenaiAuth": True, "account": {"type": "apiKey"}}
            )
        )
        self.assertFalse(
            quota_available_from_account(
                {"requiresOpenaiAuth": False, "account": {"type": "chatgpt"}}
            )
        )
        self.assertFalse(
            quota_available_from_account(
                {"requiresOpenaiAuth": False, "account": None}
            )
        )

    def test_path_cli_precedes_common_install_and_bundled_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path_cli = root / "path-bin" / "codex"
            common_cli = root / "home" / ".npm-global" / "bin" / "codex"
            bundled_cli = root / "ChatGPT.app" / "codex"
            for executable in (path_cli, common_cli, bundled_cli):
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.touch()
                executable.chmod(0o700)

            candidates = find_codex_candidates(
                path_lookup=lambda _name: str(path_cli),
                home=root / "home",
                bundled=bundled_cli,
            )

        self.assertEqual(candidates[0], str(path_cli))
        self.assertEqual(candidates[1], str(common_cli))
        self.assertEqual(candidates[-1], str(bundled_cli))

    def test_app_server_stream_limit_allows_large_thread_list_records(self) -> None:
        self.assertGreater(APP_SERVER_STREAM_LIMIT, 64 * 1024)


class CodexMonitorFallbackTests(unittest.IsolatedAsyncioTestCase):
    async def test_monitor_falls_back_without_losing_api_mode_sessions(self) -> None:
        attempts: list[str] = []

        class FakeClient:
            def __init__(self, executable: str, notification_handler=None) -> None:
                self.executable = executable

            async def start(self) -> None:
                attempts.append(self.executable)
                if self.executable == "/bad/codex":
                    raise RuntimeError("provider-incompatible binary")

            async def stop(self) -> None:
                return None

            async def request(self, method: str, params: object) -> dict:
                if method == "thread/list":
                    return {
                        "data": [
                            {
                                "id": "api-session",
                                "name": "API mode still syncs",
                                "cwd": "/tmp/cardbridge",
                                "updatedAt": 123,
                            }
                        ]
                    }
                if method == "account/read":
                    return {"requiresOpenaiAuth": False, "account": None}
                raise AssertionError(f"unexpected request in API mode: {method}")

        store = AgentStore()
        with (
            patch(
                "cardbridge.codex_monitor.find_codex_candidates",
                return_value=["/bad/codex", "/good/codex"],
            ),
            patch("cardbridge.codex_monitor.CodexAppServerClient", FakeClient),
        ):
            monitor = CodexMonitor(store)
            await monitor.start()
            for _ in range(100):
                if "api-session" in store.sessions:
                    break
                await asyncio.sleep(0.01)
            await monitor.stop()

        self.assertEqual(attempts[:2], ["/bad/codex", "/good/codex"])
        self.assertIn("api-session", store.sessions)
        self.assertFalse(store.snapshot()["quota"]["available"])


if __name__ == "__main__":
    unittest.main()
