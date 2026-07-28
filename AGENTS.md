# Agent instructions for PanPal

PanPal is a Windows-only project for the M5Stack Cardputer ADV. `CardBridge`
remains in internal executable, package, protocol, and configuration names.
Read [`docs/BRANDING.md`](docs/BRANDING.md) before changing those identifiers.

## Read first

1. `README.md`
2. `docs/WINDOWS.md`
3. `docs/DEVELOPMENT.md`
4. `SECURITY.md`

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

Lower-level checks are listed in `docs/DEVELOPMENT.md`.

## User approval

Stop for Windows firewall, Bluetooth pairing, microphone-device changes,
administrator elevation, code-signing prompts, and Codex Hook trust. Do not
bypass consent dialogs or collect credentials.

## Generated files and secrets

Run `python tools/generate_versions.py` after changing `version.json`. Do not
edit generated version files by hand.

Never read, print, commit, or transmit pairing tokens, Wi-Fi passwords, API
keys, Codex `auth.json`, conversation text, command output, or recordings.

## Completion

A Windows build is complete when Python tests pass, generated files are current,
the installer is created, the firmware compiles, and `dist/SHA256SUMS.txt`
matches the release files. Flashing hardware remains a separate action.
