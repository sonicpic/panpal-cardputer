#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import plistlib
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, check=True, capture_output=True, text=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate a packaged CardBridge release")
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--require-developer-id", action="store_true")
    parser.add_argument("--require-notarized", action="store_true")
    args = parser.parse_args()

    versions = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    compatibility = json.loads(
        (ROOT / "release" / "compatibility.json").read_text(encoding="utf-8")
    )
    app = args.app.resolve()
    agent = app / "Contents/Helpers/CardBridgeAgent.app"
    app_info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    agent_info = plistlib.loads((agent / "Contents/Info.plist").read_bytes())

    expected = versions["mac_app"]
    assert app_info["CFBundleIdentifier"] == "com.voltwake.cardbridge"
    assert agent_info["CFBundleIdentifier"] == "com.voltwake.cardbridge.agent"
    assert app_info["CFBundleShortVersionString"] == expected["version"]
    assert int(app_info["CFBundleVersion"]) == expected["build"]
    assert agent_info["CFBundleShortVersionString"] == expected["version"]
    assert int(agent_info["CFBundleVersion"]) == expected["build"]
    assert compatibility["release"] == versions["release"]

    executable = app / "Contents/MacOS/CardBridge"
    agent_executable = agent / "Contents/MacOS/CardBridgeAgent"
    audio_driver = (
        app
        / "Contents/Resources/AudioDriver/CardBridgeMicrophone.driver"
    )
    licenses = app / "Contents/Resources/Licenses"
    driver_info = plistlib.loads((audio_driver / "Contents/Info.plist").read_bytes())
    assert driver_info["CFBundleIdentifier"] == "com.voltwake.cardbridge.microphone.driver"
    for required_license in (
        "CardBridge-MIT.txt",
        "CardBridgeMicrophone-GPL-3.0.txt",
        "CardBridgeMicrophone-NOTICE.md",
        "SourceHanSans-OFL.txt",
        "Sparkle-LICENSE.txt",
        "THIRD_PARTY_NOTICES.md",
        "Python-dependencies.json",
    ):
        assert (licenses / required_license).is_file(), f"missing license: {required_license}"
    assert "arm64" in run("lipo", "-archs", str(executable)).stdout.split()
    assert "arm64" in run("lipo", "-archs", str(agent_executable)).stdout.split()
    driver_archs = run(
        "lipo", "-archs", str(audio_driver / "Contents/MacOS/CardBridgeMicrophone")
    ).stdout.split()
    assert {"arm64", "x86_64"}.issubset(driver_archs)
    run("codesign", "--verify", "--deep", "--strict", str(app))
    # Static signature validation did not catch ad-hoc library-validation
    # mismatches between the PyInstaller executable and Python.framework.
    run(str(agent_executable), "--help")

    signature = run("codesign", "-dvvv", str(app)).stderr
    if args.require_developer_id:
        assert "Authority=Developer ID Application:" in signature, (
            "release requires a Developer ID Application identity"
        )
    if args.require_notarized:
        run("xcrun", "stapler", "validate", str(app))
        run("spctl", "--assess", "--type", "execute", "--verbose=2", str(app))

    if "Authority=Developer ID Application:" in signature:
        signature_kind = "Developer ID"
    elif "Authority=Apple Development:" in signature:
        signature_kind = "Apple Development"
    else:
        signature_kind = "ad-hoc"

    print(
        f"Validated CardBridge {expected['version']} ({expected['build']}), "
        f"arm64, signature={signature_kind}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
