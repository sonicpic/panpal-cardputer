#!/usr/bin/env python3
"""Install/remove CardBridge's fail-open Codex lifecycle reporters.

The installer merges only its own command into ~/.codex/hooks.json and leaves
unrelated hooks untouched. Codex will ask the user to trust the changed hook
configuration; that trust step is intentionally never bypassed here.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import sys
import tempfile
from pathlib import Path
from typing import Any


EVENTS = (
    "SessionStart",
    "UserPromptSubmit",
    "PreToolUse",
    "PermissionRequest",
    "PostToolUse",
    "Stop",
)


def hook_command(python: Path, reporter: Path) -> str:
    return f"{shlex.quote(str(python))} {shlex.quote(str(reporter))}"


def is_ours(hook: object, reporter: Path) -> bool:
    return (
        isinstance(hook, dict)
        and hook.get("type") == "command"
        and str(reporter) in str(hook.get("command", ""))
    )


def transform(
    document: dict[str, Any], *, command: str, reporter: Path, install: bool
) -> dict[str, Any]:
    root = document.setdefault("hooks", {})
    if not isinstance(root, dict):
        raise ValueError("top-level 'hooks' must be an object")
    for event in EVENTS:
        groups = root.setdefault(event, [])
        if not isinstance(groups, list):
            raise ValueError(f"hooks.{event} must be a list")
        cleaned = []
        for group in groups:
            if not isinstance(group, dict):
                cleaned.append(group)
                continue
            values = group.get("hooks")
            if not isinstance(values, list):
                cleaned.append(group)
                continue
            values = [value for value in values if not is_ours(value, reporter)]
            if values:
                replacement = dict(group)
                replacement["hooks"] = values
                cleaned.append(replacement)
        if install:
            cleaned.append(
                {
                    "hooks": [
                        {
                            "type": "command",
                            "command": command,
                            "timeout": 2,
                        }
                    ]
                }
            )
        if cleaned:
            root[event] = cleaned
        else:
            root.pop(event, None)
    if not root:
        document.pop("hooks", None)
    return document


def write_atomic(path: Path, document: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix="hooks.", suffix=".json", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            json.dump(document, output, ensure_ascii=False, indent=2)
            output.write("\n")
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("install", "uninstall", "show"))
    parser.add_argument("--config", type=Path, default=Path.home() / ".codex" / "hooks.json")
    parser.add_argument("--python", type=Path, default=Path(sys.executable).resolve())
    args = parser.parse_args()
    reporter = (Path(__file__).parent / "hooks" / "cardbridge_codex.py").resolve()
    document: dict[str, Any] = {}
    if args.config.exists():
        loaded = json.loads(args.config.read_text(encoding="utf-8"))
        if not isinstance(loaded, dict):
            raise SystemExit(f"{args.config}: root must be an object")
        document = loaded
    transformed = transform(
        document,
        command=hook_command(args.python, reporter),
        reporter=reporter,
        install=args.action != "uninstall",
    )
    if args.action == "show":
        print(json.dumps(transformed, ensure_ascii=False, indent=2))
        return
    write_atomic(args.config, transformed)
    print(f"{args.action}ed CardBridge Codex hooks in {args.config}")
    print("Restart/reload Codex, review the hook trust prompt, and approve only if the path is expected.")


if __name__ == "__main__":
    main()
