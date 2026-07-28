#!/usr/bin/env python3
"""Read-only Windows build and installation diagnostics for PanPal."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]


def command_version(command: str, *args: str) -> str | None:
    path = shutil.which(command)
    if not path:
        return None
    try:
        result = subprocess.run(
            [path, *args],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    lines = (result.stdout or result.stderr).strip().splitlines()
    return lines[0] if lines else path


def add(
    checks: list[dict[str, Any]],
    name: str,
    status: str,
    detail: str,
    hint: str | None = None,
) -> None:
    item: dict[str, Any] = {"name": name, "status": status, "detail": detail}
    if hint:
        item["hint"] = hint
    checks.append(item)


def first_existing(paths: list[Path]) -> Path | None:
    return next((path for path in paths if path.is_file()), None)


def main() -> int:
    parser = argparse.ArgumentParser(description="Check PanPal on Windows")
    parser.add_argument("--json", action="store_true", help="emit JSON")
    args = parser.parse_args()
    checks: list[dict[str, Any]] = []

    try:
        versions = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
        manifest = json.loads(
            (ROOT / "project-install.json").read_text(encoding="utf-8")
        )
        release = str(versions["release"])
        add(checks, "repository", "ok", f"PanPal {release} at {ROOT}")
    except (OSError, ValueError, KeyError) as exc:
        release = "unknown"
        manifest = {}
        add(checks, "repository", "error", f"cannot read metadata: {exc}")

    windows = platform.system() == "Windows"
    add(
        checks,
        "operating_system",
        "ok" if windows else "error",
        f"{platform.system()} {platform.release()}",
        None if windows else "Build and run PanPal on Windows 10 or Windows 11.",
    )
    architecture = platform.machine()
    architecture_ok = architecture.lower() in {"amd64", "x86_64"}
    add(
        checks,
        "architecture",
        "ok" if architecture_ok else "error",
        architecture,
        None if architecture_ok else "The packaged application targets Windows x64.",
    )

    python_ok = sys.version_info >= (3, 10)
    add(
        checks,
        "python",
        "ok" if python_ok else "error",
        platform.python_version(),
        None if python_ok else "Install Python 3.10 or newer.",
    )
    for command, label, version_args in (
        ("git", "git", ("--version",)),
        ("powershell", "powershell", ("-NoProfile", "-Command", "$PSVersionTable.PSVersion")),
    ):
        version = command_version(command, *version_args)
        add(checks, label, "ok" if version else "warning", version or "not found")

    local_app_data = Path(os.environ.get("LOCALAPPDATA", ""))
    inno = first_existing(
        [
            local_app_data / "Programs" / "Inno Setup 6" / "ISCC.exe",
            Path(r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"),
            Path(r"C:\Program Files\Inno Setup 6\ISCC.exe"),
        ]
    )
    add(
        checks,
        "inno_setup",
        "ok" if inno else "warning",
        str(inno) if inno else "not found",
        None if inno else "Install with: winget install JRSoftware.InnoSetup",
    )

    pio = shutil.which("pio") or str(
        ROOT / "windows" / ".venv-build" / "Scripts" / "pio.exe"
    )
    pio_exists = Path(pio).is_file()
    add(
        checks,
        "platformio",
        "ok" if pio_exists else "warning",
        pio if pio_exists else "not found",
        None if pio_exists else "Run windows\\bootstrap.ps1.",
    )

    installed = first_existing(
        [
            Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
            / "PanPal"
            / "CardBridge.exe",
            local_app_data / "Programs" / "PanPal" / "CardBridge.exe",
        ]
    )
    add(
        checks,
        "installed_app",
        "ok" if installed else "warning",
        str(installed) if installed else "not found in standard locations",
    )

    required = [
        "README.md",
        "LICENSE",
        "platformio.ini",
        "windows/bootstrap.ps1",
        "windows/build.ps1",
        "bridge/packaging/CardBridgeWindows.spec",
    ]
    missing = [relative for relative in required if not (ROOT / relative).exists()]
    add(
        checks,
        "project_contract",
        "ok" if not missing else "error",
        "required files present" if not missing else f"missing: {', '.join(missing)}",
    )

    errors = sum(item["status"] == "error" for item in checks)
    warnings = sum(item["status"] == "warning" for item in checks)
    result = {
        "project": manifest.get("name", "PanPal"),
        "release": release,
        "root": str(ROOT),
        "checks": checks,
        "summary": {"errors": errors, "warnings": warnings},
    }
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        for item in checks:
            marker = {"ok": "OK", "warning": "WARN", "error": "ERROR"}[
                item["status"]
            ]
            print(f"[{marker:5}] {item['name']}: {item['detail']}")
            if item.get("hint") and item["status"] != "ok":
                print(f"        hint: {item['hint']}")
        print(f"Summary: {errors} error(s), {warnings} warning(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
