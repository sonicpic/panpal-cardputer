#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Write hashes for CardBridge release artifacts")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("artifacts", nargs="+", type=Path)
    args = parser.parse_args()
    versions = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    manifest = {
        "schema": 1,
        "product": "PanPal",
        "release": versions["release"],
        "artifacts": [
            {
                "name": artifact.name,
                "bytes": artifact.stat().st_size,
                "sha256": sha256(artifact),
            }
            for artifact in args.artifacts
        ],
    }
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
