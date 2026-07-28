from __future__ import annotations

import unittest
import sys
import io
from pathlib import Path
import tempfile

from cardbridge.codex_hooks import (
    EVENTS,
    _quote_executable,
    hook_command,
    hooks_installed,
    transform,
    update_hooks,
)
from cardbridge.hook_reporter import (
    _activity_from_hook,
    _public_message_activity,
    _read_hook_input,
)


class CodexHookInstallerTests(unittest.TestCase):
    def test_hook_reporter_reads_codex_json_from_standard_input(self) -> None:
        payload = _read_hook_input(
            io.StringIO('{"hook_event_name":"UserPromptSubmit","session_id":"abc"}')
        )
        self.assertEqual(payload["hook_event_name"], "UserPromptSubmit")
        self.assertEqual(payload["session_id"], "abc")

    def test_windows_hook_command_quotes_program_files_with_double_quotes(self) -> None:
        executable = r"C:\Program Files\Codex Deck\CardBridge.exe"
        self.assertEqual(
            _quote_executable(executable, platform="win32"),
            f'"{executable}"',
        )

    def test_reporter_derives_details_without_forwarding_sensitive_inputs(self) -> None:
        patch_activity = _activity_from_hook(
            {
                "tool_input": {
                    "patch": "*** Update File: /private/project/src/ui.cpp\n+API_KEY=do-not-send"
                }
            },
            "apply_patch",
        )
        self.assertEqual(patch_activity, "Editing ui.cpp")
        command_activity = _activity_from_hook(
            {
                "tool_input": {
                    "cmd": "OPENAI_API_KEY=do-not-send pio run -t upload"
                }
            },
            "exec_command",
        )
        self.assertEqual(command_activity, "Flashing firmware")
        self.assertNotIn("do-not-send", command_activity)
        final_activity = _public_message_activity(
            "## Completed\nUpdated the layout. API_KEY=do-not-send "
            "Bearer abcdefghijklmnop sk-abcdefghijklmnop"
        )
        self.assertIn("Completed", final_activity)
        self.assertIn("Updated the layout", final_activity)
        self.assertNotIn("do-not-send", final_activity)
        self.assertNotIn("abcdefghijklmnop", final_activity)

    def test_install_is_idempotent_and_preserves_other_hooks(self) -> None:
        original = {
            "hooks": {
                "Stop": [
                    {
                        "hooks": [
                            {"type": "command", "command": "notify-send done"}
                        ]
                    }
                ]
            }
        }
        command = "/Applications/CardBridgeAgent --cardbridge-codex-hook"
        once = transform(original, command=command, install=True)
        twice = transform(once, command=command, install=True)
        self.assertEqual(once, twice)
        self.assertEqual(
            once["hooks"]["Stop"][0]["hooks"][0]["command"],
            "notify-send done",
        )
        for event in EVENTS:
            owned = [
                hook
                for group in once["hooks"][event]
                for hook in group.get("hooks", [])
                if "--cardbridge-codex-hook" in hook.get("command", "")
            ]
            self.assertEqual(len(owned), 1)

    def test_uninstall_removes_only_cardbridge_entries(self) -> None:
        command = "/Applications/CardBridgeAgent --cardbridge-codex-hook"
        installed = transform({}, command=command, install=True)
        removed = transform(installed, command=command, install=False)
        self.assertEqual(removed, {})

    def test_atomic_install_and_uninstall_report_current_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hooks.json"
            self.assertFalse(hooks_installed(path))
            self.assertTrue(update_hooks(True, path))
            if sys.platform != "win32":
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertTrue(hooks_installed(path))
            self.assertFalse(update_hooks(False, path))

    def test_install_replaces_legacy_reporter_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hooks.json"
            legacy = "python /old/bridge/hooks/cardbridge_codex.py"
            path.write_text(
                __import__("json").dumps(
                    transform({}, command=legacy, install=True)
                ),
                encoding="utf-8",
            )
            self.assertFalse(hooks_installed(path))
            self.assertTrue(update_hooks(True, path))
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("cardbridge_codex.py", text)
            self.assertIn("--cardbridge-codex-hook", text)

    def test_stale_marker_command_is_not_reported_as_current(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hooks.json"
            stale = "'C:\\Program Files\\Codex Deck\\CardBridge.exe' --cardbridge-codex-hook"
            path.write_text(
                __import__("json").dumps(transform({}, command=stale, install=True)),
                encoding="utf-8",
            )
            self.assertNotEqual(stale, hook_command())
            self.assertFalse(hooks_installed(path))
            self.assertTrue(update_hooks(True, path))


if __name__ == "__main__":
    unittest.main()
