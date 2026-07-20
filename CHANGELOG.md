# Changelog

The release version is defined in [`version.json`](version.json). Generated
version constants must be refreshed with `python3 tools/generate_versions.py`.

## 1.1.0

- Added the bundled `CardBridge Microphone` HAL driver.
- Added the separate output-only microphone feed and retained BlackHole as a
  compatibility fallback.
- Updated Cardputer UI and Codex session monitoring behavior.

## 1.0.0

- Added the native macOS menu bar app and supervised Bridge Agent.
- Added protocol/capability negotiation, Keychain migration, diagnostics, and
  Sparkle update foundations.

Older development notes are preserved in `release/RELEASE_NOTES.md` and
`docs/archive/` where applicable.
