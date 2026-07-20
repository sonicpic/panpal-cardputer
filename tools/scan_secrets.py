#!/usr/bin/env python3
"""High-confidence secret scan for tracked text files."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PATTERNS = (
    re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----"),
    re.compile(r"\b(?:ghp|github_pat|sk|xox[baprs])-[-_A-Za-z0-9]{20,}\b"),
    re.compile(r"\bAKIA[0-9A-Z]{16}\b"),
    re.compile(
        r"(?i)\b(?:api[_-]?key|client[_-]?secret)\s*[:=]\s*[\"'][A-Za-z0-9+/=_-]{24,}[\"']"
    ),
)


def main() -> int:
    files = subprocess.run(
        ["git", "ls-files", "-z"], cwd=ROOT, capture_output=True, check=True
    ).stdout.split(b"\0")
    findings: list[str] = []
    for raw in files:
        if not raw:
            continue
        path = ROOT / raw.decode("utf-8", errors="surrogateescape")
        try:
            if path.stat().st_size > 2_000_000:
                continue
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for pattern in PATTERNS:
            match = pattern.search(text)
            if match:
                line = text.count("\n", 0, match.start()) + 1
                findings.append(f"{path.relative_to(ROOT)}:{line}: {pattern.pattern}")
    if findings:
        print("Potential secrets found:")
        print("\n".join(findings))
        return 1
    print("Secret scan: no high-confidence secrets found")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
