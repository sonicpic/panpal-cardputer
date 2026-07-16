# CardBridge for Cardputer ADV

CardBridge turns an M5Stack Cardputer ADV into a WiFi microphone, keyboard, and Codex session companion for macOS. The firmware streams authenticated 16 kHz PCM audio and explicit key down/up events to the companion service in `bridge/`; its Codex page shows the latest user-focused task, animated state, and up to eight switchable sessions. ChatGPT OAuth also shows weekly/5-hour subscription limits, while API key and custom-provider modes hide that quota HUD. Claude remains a placeholder.

The authoritative product requirements and acceptance criteria are in [`docs/GOAL.md`](docs/GOAL.md). Mac installation, BlackHole and Typeless setup, simulator use, and service operation are documented in [`bridge/README.md`](bridge/README.md).

## Use the Mac menu bar app

`CardBridge.app` is the normal Mac entry point. It bundles its own signed Bridge Agent, starts bridging immediately, appears only in the menu bar, reconnects paired M5 devices automatically, and does not require Python, a virtual environment, or a terminal.

For a local Apple Silicon build:

```sh
macos/scripts/build_app.sh
ditto macos/dist/CardBridge.app /Applications/CardBridge.app
open /Applications/CardBridge.app
```

On first launch, enable `CardBridge` in **System Settings → Privacy & Security → Accessibility** for keyboard forwarding. The native App requests this permission before starting its bundled Agent so the correct item is registered. Install BlackHole 2ch for microphone bridging. Existing `~/.cardbridge` identity and pairing data are migrated without re-pairing; pairing secrets move to the macOS Keychain.

The menu shows live M5, protocol, local-network, Accessibility, audio, and Codex health. Settings manages login launch, audio gain, paired devices, Codex Hooks, automatic updates, and redacted diagnostics.

## Build firmware

```sh
cd /path/to/m5-cardputer
pio run
```

Install PlatformIO Core first if `pio` is not already on `PATH`.

When hardware is connected, Codex may run `pio run -t upload`, use the USB serial port, and perform physical-device validation. Let PlatformIO auto-detect `/dev/cu.usbmodem*` whenever possible because the port name can change after a reset.

## Release and protocol versions

[`version.json`](version.json) is the single version source for the Mac app, Python agent, firmware, local Agent API, device protocol, configuration schema, and capability list. Regenerate the language-specific constants after changing it:

```sh
python3 tools/generate_versions.py
```

CI and local validation should use `python3 tools/generate_versions.py --check` to reject stale generated Python, C++, or Swift constants. A protocol-major mismatch produces an explicit `upgrade_required` response; missing protocol fields remain compatible as legacy protocol v1 during migration.

Run the complete local release gate with:

```sh
CODE_SIGN_IDENTITY="Apple Development: …" macos/scripts/release.sh
```

It tests Swift/Python, builds firmware, packages and validates the App/Agent, signs the Sparkle archive, and writes checksums plus release manifests under `macos/dist/release-<version>/`. Public distribution additionally requires a Developer ID Application certificate and Apple Notarization credentials; see [`release/README.md`](release/README.md).

## Build pet animation assets

The firmware ships with a deterministic Codex-themed development mascot. Rebuild it with the bundled offline packer:

```sh
cd /path/to/m5-cardputer
python3 tools/pack_pet.py --demo --output-dir src
```

To use a desktop Codex v2 pet created by the official `hatch-pet` workflow, point the same adapter at its installed package:

```sh
python3 tools/pack_pet.py \
  --pet-dir "$HOME/.codex/pets/my-pet" \
  --output-dir src
```

The adapter accepts both the 1536×1872, 8×9 app atlas and the 1536×2288, 8×11 v2 atlas. It selects only Idle, Failed, Waiting, Running, and Review; scales frames to 72×72; quantizes them to a shared 16-colour palette; and writes row-safe RLE into `src/pet_assets.*`. The Cardputer decodes those runs directly from flash and allocates no per-frame image buffer.

## Build the Chinese UI font

The generated `assets/fonts/cardbridge-ui-15.bff` embeds a 15px, 4-bit anti-aliased GB2312 font derived from Source Han Sans CN Medium 2.005R. Rebuild it with:

```sh
python3 tools/build_ui_font.py
```

The generator verifies the pinned source-font checksum and invokes `lv_font_conv` 1.5.3 through `npx`. Source Han Sans is distributed under the SIL Open Font License 1.1; the required notice is in `assets/fonts/LICENSE-SourceHanSans.txt`.

## Device controls

- BtnA toggles keyboard forwarding. The keyboard icon at the far left of the status bar shows whether forwarding is on; toggling it never changes the current page.
- With keyboard forwarding on, `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` send Up, Left, Down, and Right; `Fn+\`` sends Escape. Shift is attached to the target key as a macOS modifier; Ctrl/Cmd/Option retain normal down/up events.
- With keyboard forwarding off, use the printed arrow keys (`; . , /`) or `I/J/K/L` to navigate, `Enter` to confirm, and the backtick/ESC key to go back.
- On the Codex page, left/right changes the displayed session and `Enter` marks its Cardputer-only completion/blocked reminder as seen. A newer user prompt automatically moves the pet back to that session.
- In WiFi and paired-Mac lists, `Backspace` forgets or deletes the selected saved item. No Fn chord is required.
- Password entry preserves case and shifted symbols: hold `Shift` while typing uppercase letters or symbols. `Backspace` edits and the backtick/ESC key cancels.
- WiFi setup always starts from a scan list; only the password is typed.
