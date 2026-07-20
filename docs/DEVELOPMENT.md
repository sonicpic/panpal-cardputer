# Development guide

## Prerequisites

- macOS 13 or newer on Apple Silicon (the current packaged App target).
- Xcode and Command Line Tools; `swift --version` must work.
- Python 3.10 or newer.
- PlatformIO Core.
- Internet access for the pinned Sparkle archive and Python/PlatformIO inputs.

Check the machine without changing it:

```sh
./scripts/doctor.sh
```

## Bootstrap

```sh
./scripts/bootstrap.sh
```

This creates the isolated `bridge/.venv` for the packaged Agent and
`tools/.venv` for PlatformIO. Keeping build tools out of the Agent environment
prevents unrelated packages from being bundled into the App. Generated version
files are checked but never silently rewritten during a build.

## Tests and builds

```sh
./scripts/test.sh
./scripts/build.sh
```

The test script runs generated-version validation, Python tests, Swift tests,
and a firmware build when PlatformIO is available. The build script packages
the signed App, embedded Agent, Sparkle framework, and microphone driver under
`macos/dist/CardBridge.app`.

The lower-level commands remain available:

```sh
PYTHONPATH=bridge:. bridge/.venv/bin/python -m unittest discover -s bridge/tests -v
swift test --package-path macos
pio run
macos/scripts/build_app.sh
python3 tools/validate_release.py --app macos/dist/CardBridge.app
```

## Generated files

`version.json` is authoritative. After changing it:

```sh
python3 tools/generate_versions.py
python3 tools/generate_versions.py --check
```

Do not hand-edit `bridge/cardbridge/_generated_version.py`,
`src/generated_version.h`, `macos/Shared/GeneratedVersion.swift`, or
`release/compatibility.json`.

## Release build

The release gate is:

```sh
CODE_SIGN_IDENTITY="Developer ID Application: …" \
  REQUIRE_NOTARIZATION=1 \
  NOTARY_PROFILE=cardbridge-notary \
  macos/scripts/release.sh
```

Public distribution requires a Developer ID certificate, notarization
credentials, a Sparkle signing key, a tag matching `version.json`, and a
review of `THIRD_PARTY_NOTICES.md` and `assets/ASSET_SOURCES.md`.
