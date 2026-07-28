# PanPal changelog

## Unreleased

## 1.4.6 / firmware 0.6.5

- Add a custom read-only quota URL with Bearer authentication, multi-account
  selection, cached failure handling, and DPAPI-protected access keys.
- Set the automatic Enter delay after voice input to 1400 ms.
- Keep the Windows action bar visible at every display scale and add a
  compact tabbed settings layout.
- Prefer Microsoft YaHei UI for consistent Simplified Chinese glyphs on
  high-DPI Windows displays.
- Label local task monitoring and optional Hooks as separate status sources.
- Replace internal setting values with readable connection, microphone, and
  shortcut choices.
- Remove NumPy from the audio path and narrow the packaged modules to reduce
  installed size and memory use.
- Remove unused platform code, retired packaging paths, and historical planning
  documents.
- Make the repository, CI, build scripts, and documentation Windows-only.

## 1.4.5 / firmware 0.6.5

- Restore authenticated Wi-Fi reconnection after firmware restarts by enlarging
  the post-pairing hello and control-message buffers.
- Ignore stale asynchronous discovery results so they cannot replace a healthy
  authenticated PC connection.
- Detect ChatGPT Desktop, VS Code, and CLI task lifecycle from shared rollout
  metadata while retaining optional Hooks for detailed activity states.
- Publish the independent `sonicpic/panpal-cardputer` repository identity and
  keep the original project history, MIT notice, and upstream attribution.

## 1.4.2 / firmware 0.6.2

- Keep one authoritative Windows Bridge identity/config instance across the UI
  and background runtime.
- Let a newly authenticated Wi-Fi session replace a stale connection after a
  firmware or PC restart.
- Recover a missing `last_mac` target and migrate a saved pairing when a buggy
  earlier Windows build changed its advertised Bridge ID.
- Remove obsolete CodexDeck/PanPal startup shortcuts during upgrades so only
  one Bridge can bind TCP 7788.

## 1.4.1 / firmware 0.6.1

- Configure the Cardputer ADV speaker explicitly on I2S1 before previewing alerts.
- Suppress stale completion alerts during the first three seconds of a new link.
- Replace the home footer label/dot with a full-width animated status-color bar.
- Report Hook configuration, listener health, command path, and last observed event in the Windows UI.

## 1.4.0 / firmware 0.6.0

- Renamed the public product to PanPal while preserving CardBridge compatibility IDs.
- Replaced the firmware mascot with Dapan and added waving/jumping interactions.
- Moved automatic Enter control to firmware (`Fn+Enter`) with an on-device indicator.
- Added selectable notification tones and volume for needs-input, ready, and disconnect transitions.
- Reworked the home pet card with a taller clipped stage and breathing status indicator.

The release version is defined in [`version.json`](version.json). Generated
version constants must be refreshed with `python tools/generate_versions.py`.
