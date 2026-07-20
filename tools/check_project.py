#!/usr/bin/env python3
"""Validate the public-project contract without third-party dependencies."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    errors: list[str] = []
    required = [
        "README.md",
        "README.zh-CN.md",
        "AGENTS.md",
        "LICENSE",
        "NOTICE.md",
        "THIRD_PARTY_NOTICES.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "project-install.json",
        "docs/INSTALL.md",
        "docs/BRANDING.md",
        "docs/DEVELOPMENT.md",
        "docs/ARCHITECTURE.md",
        "docs/PROTOCOL.md",
        "docs/TROUBLESHOOTING.md",
        "scripts/doctor.sh",
        "scripts/install-release.sh",
        "scripts/bootstrap.sh",
        "scripts/test.sh",
        "scripts/build.sh",
        "scripts/install.sh",
        "scripts/replace_app.sh",
        "scripts/healthcheck.sh",
        "scripts/security-scan.sh",
    ]
    for relative in required:
        if not (ROOT / relative).exists():
            errors.append(f"missing required public-project file: {relative}")

    try:
        versions = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
        manifest = json.loads((ROOT / "project-install.json").read_text(encoding="utf-8"))
        if manifest.get("name") != "Codex Deck":
            errors.append("project-install.json name must be Codex Deck")
        if versions["release"] != versions["mac_app"]["version"]:
            errors.append("version.json release and mac_app.version differ")
    except (OSError, ValueError, KeyError) as exc:
        errors.append(f"invalid project metadata: {exc}")

    result = subprocess.run(
        [sys.executable, "tools/generate_versions.py", "--check"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        errors.append("generated version constants are stale")
        if result.stderr.strip():
            errors.append(result.stderr.strip())

    if errors:
        print("Public project contract: FAILED")
        for error in errors:
            print(f"- {error}")
        return 1
    print("Public project contract: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
