# Codex Deck

> **Codex Deck** is an independent pocket hardware companion for OpenAI Codex,
> turning the M5Stack Cardputer ADV into a wireless keyboard, microphone, and
> live agent-status display for macOS.

## 中文简介

**Codex Deck** 是一个面向 OpenAI Codex 的独立开源硬件伴侣。它把
M5Stack Cardputer ADV 变成 macOS 上的无线键盘、无线麦克风和实时 Agent
状态屏：设备通过 Wi-Fi 发送经过认证的按键与 16 kHz 音频，并显示最近的
Codex 会话、任务状态和宠物动画。

当前 1.x 版本的 macOS 安装包和内部桥接服务仍保留 `CardBridge` 技术名，
以保证已有配对、权限和音频设置可以平滑升级。Codex Deck 是独立项目，
不代表 OpenAI 官方产品或背书。

## English introduction

**Codex Deck** is an independent open-source hardware companion for OpenAI
Codex. It turns the M5Stack Cardputer ADV into a wireless keyboard, wireless
microphone, and live agent-status display for macOS. The device sends
authenticated key events and 16 kHz PCM audio over Wi-Fi and shows recent Codex
sessions, task state, and an animated pet.

The current 1.x macOS package and bridge retain the internal `CardBridge`
compatibility name so existing pairings, permissions, and audio settings can
upgrade without being recreated. Codex Deck is not an official OpenAI product
and is not endorsed by OpenAI.

Current installation and development instructions start at
[`docs/README.md`](docs/README.md). Product requirements and historical
acceptance records remain in `docs/`, but they are not the canonical install
path.

## Install a release

Normal users should download the signed/notarized Codex Deck App and matching firmware
from GitHub Releases. Verify `SHA256SUMS`, move `CardBridge.app` to
`/Applications`, launch it, and follow the one-time macOS permission prompts.
The complete flow is in [`docs/INSTALL.md`](docs/INSTALL.md).

From a checkout, `./scripts/install-release.sh` performs the download,
checksum verification, DMG mount, and App installation automatically.

The current packaged target is Apple Silicon on macOS 13 or newer. Codex Deck
requires a Cardputer ADV on a 2.4 GHz Wi-Fi network. Installing the Mac App and
flashing the Cardputer firmware are separate operations.

## Use the Codex Deck macOS menu bar app

`CardBridge.app` is the current internal bundle name for Codex Deck. It bundles
its own signed Bridge Agent, starts bridging immediately, appears only in the
menu bar, reconnects paired M5 devices automatically, and does not require
Python, a virtual environment, or a terminal.

For a source build on Apple Silicon:

```sh
./scripts/doctor.sh
./scripts/bootstrap.sh
./scripts/test.sh
./scripts/build.sh
./scripts/install.sh
./scripts/healthcheck.sh
```

On first launch, Codex Deck (`CardBridge.app`) offers to install its bundled `CardBridge Microphone` HAL driver with one macOS administrator prompt, then requests **System Settings → Privacy & Security → Accessibility** for keyboard forwarding. The microphone publishes an input-only USB-compatible Core Audio device and a separate output-only feed used by the Agent; an existing BlackHole 2ch installation remains a fallback. Existing `~/.cardbridge` identity and pairing data are migrated without re-pairing; pairing secrets move to the macOS Keychain.

The menu shows live M5, protocol, local-network, Accessibility, audio, and Codex health. Settings manages login launch, audio gain, paired devices, Codex Hooks, automatic updates, and redacted diagnostics.

An automation agent may run every command above. It must pause for explicit
user approval when macOS requests administrator, Accessibility, Local Network,
Keychain, or Codex Hook trust. See [`AGENTS.md`](AGENTS.md) and the
machine-readable [`project-install.json`](project-install.json).

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

The adapter accepts both the 1536×1872, 8×9 app atlas and the 1536×2288, 8×11 v2 atlas. It selects only Idle, Failed, Waiting, Running, and Review; packs frames at 72×72; quantizes them to a shared 16-colour palette; and writes row-safe RLE into `src/pet_assets.*`. The Cardputer decodes those runs directly from flash, scales them to 100×100 on the Codex detail page, and allocates no per-frame image buffer.

## Build the Chinese UI font

The generated `assets/fonts/cardbridge-ui-13.bff` embeds a native 13px, 4-bit anti-aliased GB2312 font derived from Source Han Sans CN Medium 2.005R. The native size keeps small-screen glyph advances even; it is not a fractionally scaled 15px face. Rebuild it with:

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

## Project policy

The main project is available under the MIT License. The BlackHole-derived
audio driver in `driver/` is GPLv3 and retains its own license and notices.
Before redistributing, read [`NOTICE.md`](NOTICE.md),
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and
[`assets/ASSET_SOURCES.md`](assets/ASSET_SOURCES.md).

Contributions, security reports, and support requests are described in
[`CONTRIBUTING.md`](CONTRIBUTING.md), [`SECURITY.md`](SECURITY.md), and
[`SUPPORT.md`](SUPPORT.md).
