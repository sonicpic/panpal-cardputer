# PanPal

[English](README.md) | [简体中文](README.zh-CN.md)

PanPal connects an M5Stack Cardputer ADV to a Windows PC. The device
works as a wireless microphone, a compact remote keyboard, and a small Codex
status display with the Dapan animated pet.

The current release is PanPal 1.4.6 with firmware 0.6.5. PanPal is a community
project with no affiliation with OpenAI.

The project started from
[`voltwake/cardputer-codex-deck`](https://github.com/voltwake/cardputer-codex-deck).
Its Git history, MIT license, and copyright notice remain in this repository.
`CardBridge` is still used by some executables, config directories, protocol
names, and audio devices so existing installations can upgrade in place.

## Platform support

| Host | Connection | Microphone output | Desktop application |
| --- | --- | --- | --- |
| Windows 10/11 | Wi-Fi or Bluetooth LE | VB-CABLE | PanPal tray app and Inno Setup installer |

The Cardputer uses one connection mode at a time. Changing between Wi-Fi and
Bluetooth saves the selection and restarts the device.

## What works

- Hold G0/BtnA or `Fn+Space` to talk. Double-click G0 to keep the microphone
  open for a longer dictation, then press it once to stop.
- Route Cardputer audio to a Windows input device and trigger the
  voice shortcut configured on the computer.
- Forward ordinary keys and navigation keys to the connected computer.
- Show up to eight recent Codex tasks from ChatGPT Desktop, the VS Code Codex
  extension, or Codex CLI. The device receives titles and short lifecycle
  states, without conversation text or tool output.
- Read quota windows from the local Codex account or a user-configured,
  read-only HTTPS endpoint for proxy-provider accounts.
- Display running, waiting, ready, blocked, and disconnected states through
  the Dapan animation, colour bar, and optional alert tone.
- Connect to personal WPA/WPA2 networks or PEAP/MSCHAPv2 enterprise Wi-Fi.
- Use Bluetooth LE for control and IMA ADPCM microphone audio when the local
  Wi-Fi network does not allow peer-to-peer traffic.

## Windows quick start

1. Install [VB-CABLE](https://vb-audio.com/Cable/).
2. Install `PanPal-1.4.6-setup.exe` from the release assets.
3. Open PanPal settings. Select `CABLE Input` as the audio writer and
   `CABLE Output` as the temporary microphone.
4. Flash `panpal-dapan-0.6.5-build26.bin` to the Cardputer ADV.
5. Choose Wi-Fi or Bluetooth on the device, open **Computers > Add computer**,
   and enter the six-digit pairing code.

Closing the settings window leaves PanPal running in the system tray. Details
for shortcuts, BLE pairing, enterprise Wi-Fi, logs, and source builds are in
[`docs/WINDOWS.md`](docs/WINDOWS.md).

## Device controls

| Control | Action |
| --- | --- |
| Hold G0/BtnA | Talk while held |
| Double-click G0 | Lock microphone on; press once to stop |
| `Fn+Space` | Keyboard push-to-talk |
| `Fn+Enter` | Toggle Enter 1400 ms after dictation ends |
| `Fn+Tab` | Toggle local keys and computer key forwarding |
| Left / Right on task page | Browse the latest eight Codex tasks |
| Enter on task page | Mark the local ready or blocked reminder as seen |

The microphone icon turns green after the computer confirms that microphone
routing and the configured voice shortcut both started. The keyboard icon
shows whether remote key forwarding is selected and connected.

## How it is connected

```text
Cardputer microphone and keys
        │
        ├─ Wi-Fi: TCP control + UDP PCM16 audio
        └─ BLE: GATT control + IMA ADPCM audio
        │
        ▼
PanPal on Windows
        ├─ virtual microphone output
        ├─ keyboard and voice-shortcut injection
        └─ privacy-trimmed Codex task status
```

Pairing creates a random token used to authenticate later sessions. Wi-Fi
reconnects to the saved computer after either side restarts. A bonded BLE
device advertises again and the Windows app reconnects automatically.

## Build from source

Windows PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

Firmware only:

```sh
pio run
```

`version.json` contains the release, bridge, firmware, and protocol versions.
Run `python tools/generate_versions.py` after editing it.

## Privacy and licenses

PanPal sends task names and short status summaries to the Cardputer. Prompt
text, transcript content, reasoning, raw commands, tool arguments, tool output,
Wi-Fi passwords, and pairing tokens stay off the device status channel and out
of normal logs.

The project uses the MIT License. The enterprise Wi-Fi supplicant and visual
assets carry their own notices. Read [`NOTICE.md`](NOTICE.md),
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), and
[`assets/ASSET_SOURCES.md`](assets/ASSET_SOURCES.md) before redistribution.

Documentation starts at [`docs/README.md`](docs/README.md). Security reports
and contribution notes are in [`SECURITY.md`](SECURITY.md) and
[`CONTRIBUTING.md`](CONTRIBUTING.md).
