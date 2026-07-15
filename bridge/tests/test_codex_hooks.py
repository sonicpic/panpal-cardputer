from __future__ import annotations

import unittest
from pathlib import Path

from install_codex_hooks import EVENTS, transform


class CodexHookInstallerTests(unittest.TestCase):
    def test_install_is_idempotent_and_preserves_other_hooks(self) -> None:
        reporter = Path("/project/bridge/hooks/cardbridge_codex.py")
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
        command = f"python {reporter}"
        once = transform(original, command=command, reporter=reporter, install=True)
        twice = transform(once, command=command, reporter=reporter, install=True)
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
                if str(reporter) in hook.get("command", "")
            ]
            self.assertEqual(len(owned), 1)

    def test_uninstall_removes_only_cardbridge_entries(self) -> None:
        reporter = Path("/project/bridge/hooks/cardbridge_codex.py")
        command = f"python {reporter}"
        installed = transform({}, command=command, reporter=reporter, install=True)
        removed = transform(installed, command=command, reporter=reporter, install=False)
        self.assertEqual(removed, {})


if __name__ == "__main__":
    unittest.main()
