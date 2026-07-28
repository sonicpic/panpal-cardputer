# PanPal

[English](README.md) | [简体中文](README.zh-CN.md)

> **PanPal** is a personal voice and coding companion for OpenAI Codex,
> turning the M5Stack Cardputer ADV into a wireless keyboard, microphone, and
> live agent-status display for macOS.

PanPal is not an official OpenAI product and is not endorsed by OpenAI.

PanPal is derived from
[`voltwake/cardputer-codex-deck`](https://github.com/voltwake/cardputer-codex-deck)
and preserves its MIT license, copyright notice, and Git history.

The current 1.x macOS package and bridge retain the internal `CardBridge`
compatibility name so existing pairings, permissions, and audio settings can
upgrade without being recreated.

## Highlights

- **Wireless keyboard:** sends explicit key-down and key-up events, including
  Shift, Control, Command, Option, navigation keys, and configurable function keys.
- **Wireless microphone:** streams authenticated 16 kHz PCM audio and exposes it
  as a native macOS input device through the bundled Core Audio driver.
- **Live Codex dashboard:** shows the latest user-focused task, agent phase,
  animated pet, and up to eight switchable Codex sessions on the Cardputer.
- **Quota visibility:** displays real ChatGPT weekly and five-hour limits when
  available, with explicit unlimited and indeterminate states for other providers.
- **Native macOS companion:** bundles its own Bridge Agent, lives in the menu bar,
  launches at login, reconnects paired devices, and requires no system Python.
- **Secure and automation-ready:** uses pairing, authenticated local traffic,
  Keychain-backed secrets, reproducible builds, machine-readable setup metadata,
  health checks, and end-to-end tests that an AI coding agent can run.

## Screenshots

<table>
  <tr>
    <td align="center"><strong>Home</strong></td>
    <td align="center"><strong>Codex detail</strong></td>
  </tr>
  <tr>
    <td><img src="docs/images/device-home.png" alt="Cardputer home screen with the Codex card selected"></td>
    <td><img src="docs/images/codex-detail.png" alt="Codex detail screen while a tool is running"></td>
  </tr>
</table>

These deterministic 4× previews use the current firmware layout and safe
example text. See [`docs/DEVICE_UI.md`](docs/DEVICE_UI.md) for all seven visual
states, their exact labels and typical copy, empty states, quota styles, and the
regeneration command.

## Documentation

Installation and development documentation starts at
[`docs/README.md`](docs/README.md). Product requirements and historical
acceptance records remain in `docs/`, but they are not the canonical install
path.

The repository slug is `panpal-cardputer`; the current hardware target is
the M5Stack Cardputer ADV.

## Install a release

Download the signed/notarized PanPal App and matching firmware from GitHub
Releases. Verify `SHA256SUMS`, move `CardBridge.app` to `/Applications`, launch
it, and follow the one-time macOS permission prompts. See
[`docs/INSTALL.md`](docs/INSTALL.md) for the complete flow.

From a checkout, `./scripts/install-release.sh` downloads the release, verifies
its checksum, mounts the DMG, and installs the App automatically.

The packaged target is Apple Silicon on macOS 13 or newer. PanPal requires a
Cardputer ADV on a 2.4 GHz Wi-Fi network. Installing the Mac App and flashing
the Cardputer firmware are separate operations.

## Windows custom build

This fork additionally contains a Windows Bridge, an Inno Setup installer, and
the Spear Rib firmware asset. It uses Windows `SendInput` for keyboard events,
Windows DPAPI for pairing tokens, and a separately installed VB-CABLE virtual
audio endpoint for microphone routing. See [`docs/WINDOWS.md`](docs/WINDOWS.md)
for the reproducible build, installer, pairing, and enterprise Wi-Fi workflow.
The Windows build supports Wi-Fi or BLE, semantic push-to-talk with G0 or
`Fn+Space`, a configurable microphone/shortcut tray UI, and Codex status from
VS Code or the ChatGPT/Codex desktop app.

## Use the macOS menu bar App

`CardBridge.app` is the internal bundle name for the current 1.x line and is
displayed publicly as PanPal. It bundles its signed Bridge Agent, starts
bridging immediately, appears only in the menu bar, reconnects paired M5 devices
automatically, and does not require Python, a virtual environment, or a terminal.

Build from source on Apple Silicon:

```sh
./scripts/doctor.sh
./scripts/bootstrap.sh
./scripts/test.sh
./scripts/build.sh
./scripts/install.sh
./scripts/healthcheck.sh
```

On first launch, PanPal (`CardBridge.app`) offers to install its bundled
`CardBridge Microphone` HAL driver with one macOS administrator prompt, then
requests **System Settings → Privacy & Security → Accessibility** for keyboard
forwarding. The microphone publishes an input-only USB-compatible Core Audio
device and a separate output-only feed used by the Agent; an existing BlackHole
2ch installation remains a fallback. Existing `~/.cardbridge` identity and
pairing data are migrated without re-pairing, and pairing secrets move to the
macOS Keychain.

The menu shows live M5, protocol, local-network, Accessibility, audio, and Codex
health. Settings manages login launch, audio gain, paired devices, Codex Hooks,
automatic updates, and redacted diagnostics.

An automation agent may run every command above, but it must pause for explicit
user approval when macOS requests administrator, Accessibility, Local Network,
Keychain, or Codex Hook trust. See [`AGENTS.md`](AGENTS.md) and the machine-readable
[`project-install.json`](project-install.json).

## Build firmware

```sh
cd /path/to/panpal-cardputer
pio run
```

Install PlatformIO Core first if `pio` is not already on `PATH`. When hardware is
connected, Codex may run `pio run -t upload`, use the USB serial port, and perform
physical-device validation. Let PlatformIO auto-detect `/dev/cu.usbmodem*` because
the port name can change after a reset.

## Release and protocol versions

[`version.json`](version.json) is the single version source for the Mac App,
Python Agent, firmware, local Agent API, device protocol, configuration schema,
and capability list. Regenerate language-specific constants after changing it:

```sh
python3 tools/generate_versions.py
```

CI and local validation should use `python3 tools/generate_versions.py --check`
to reject stale generated Python, C++, or Swift constants. A protocol-major
mismatch produces an explicit `upgrade_required` response; missing protocol
fields remain compatible as legacy protocol v1 during migration.

Run the complete local release gate with:

```sh
CODE_SIGN_IDENTITY="Apple Development: …" macos/scripts/release.sh
```

It tests Swift/Python, builds firmware, packages and validates the App/Agent,
signs the Sparkle archive, and writes checksums plus release manifests under
`macos/dist/release-<version>/`. Public distribution additionally requires a
Developer ID Application certificate and Apple notarization credentials; see
[`release/README.md`](release/README.md).

## Build pet animation assets

The firmware ships with a deterministic Codex-themed development mascot. Rebuild
it with the bundled offline packer:

```sh
python3 tools/pack_pet.py --demo --output-dir src
```

To use a desktop Codex v2 pet created by the official `hatch-pet` workflow:

```sh
python3 tools/pack_pet.py \
  --pet-dir "$HOME/.codex/pets/my-pet" \
  --output-dir src
```

The adapter accepts both the 1536×1872 8×9 App atlas and the 1536×2288 8×11 v2
atlas. It selects Idle, Failed, Waiting, Running, and Review; packs frames at
72×72; quantizes them to a shared 16-colour palette; and writes row-safe RLE into
`src/pet_assets.*`. The Cardputer decodes those runs directly from flash, scales
them to 100×100 on the Codex detail page, and allocates no per-frame image buffer.

## Build the Chinese UI font

The generated `assets/fonts/cardbridge-ui-13.bff` embeds a native 13px, 4-bit
anti-aliased GB2312 font derived from Source Han Sans CN Medium 2.005R. The native
size keeps small-screen glyph advances even; it is not a fractionally scaled 15px
face. Rebuild it with:

```sh
python3 tools/build_ui_font.py
```

The generator verifies the pinned source-font checksum and invokes `lv_font_conv`
1.5.3 through `npx`. Source Han Sans is distributed under the SIL Open Font
License 1.1; the required notice is in `assets/fonts/LICENSE-SourceHanSans.txt`.

## Device controls

- G0/BtnA is push-to-talk: hold and release for a normal utterance, double-click
  to latch a long utterance, then press once to stop. `Fn+Space` is the keyboard
  push-to-talk alternative.
- `Fn+Tab` toggles keyboard forwarding. A filled cyan keyboard means Remote
  mode is selected and the Bridge is connected; a red/slashed keyboard means
  Remote mode is selected but no usable computer connection exists.
- The separate microphone icon turns green only after the Bridge confirms that
  microphone routing and the configured voice shortcut started successfully.
- With keyboard forwarding on, `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` send Up, Left,
  Down, and Right; `Fn+\`` sends Escape. Shift is attached to the target key as a
  macOS modifier; Ctrl/Cmd/Option retain normal down/up events.
- With keyboard forwarding off, use the printed arrow keys (`; . , /`) or
  `I/J/K/L` to navigate, `Enter` to confirm, and the backtick/ESC key to go back.
- On the Codex page, left/right changes the displayed session and `Enter` marks
  its Cardputer-only completion/blocked reminder as seen. A newer user prompt
  automatically moves the pet back to that session.
- In Wi-Fi and paired-Mac lists, `Backspace` forgets or deletes the selected
  saved item. No Fn chord is required.
- Password entry preserves case and shifted symbols: hold `Shift` while typing
  uppercase letters or symbols. `Backspace` edits and the backtick/ESC key cancels.
- Wi-Fi setup always starts from a scan list. WPA2-Enterprise networks ask for
  a username and password and use EAP-PEAP/MSCHAPv2; ordinary encrypted
  networks only ask for a password.

## Project policy

The main project is available under the MIT License. The BlackHole-derived audio
driver in `driver/` is GPLv3 and retains its own license and notices. Before
redistributing, read [`NOTICE.md`](NOTICE.md), [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md),
and [`assets/ASSET_SOURCES.md`](assets/ASSET_SOURCES.md). Contributions, security
reports, and support requests are described in [`CONTRIBUTING.md`](CONTRIBUTING.md),
[`SECURITY.md`](SECURITY.md), and [`SUPPORT.md`](SUPPORT.md).
- `Fn+Enter` toggles the firmware-owned automatic Enter after voice input; the
  small return-arrow indicator is green while enabled.
