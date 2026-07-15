# CardBridge Mac service

The bridge advertises `_cardbridge._tcp` over mDNS, pairs Cardputers with a six-digit code, injects authenticated TCP key events with Quartz, writes authenticated UDP microphone audio into **BlackHole 2ch**, and publishes a privacy-trimmed view of local Codex sessions. It is a background service with no application UI.

## 1. Install BlackHole and configure Typeless

1. Download and install the **BlackHole 2ch** macOS package from [Existential Audio](https://existential.audio/blackhole/). Homebrew is not required.
2. Open **Audio MIDI Setup** and confirm that `BlackHole 2ch` exists. Leave its format at a supported default such as 48,000 Hz; CardBridge resamples the Cardputer's 16 kHz stream in software.
3. In Typeless, select `BlackHole 2ch` as the microphone and configure its hold-to-record shortcut as **F13** (the device default). The device setting can instead use F14–F16.
4. For an independent check, open QuickTime Player → New Audio Recording and select `BlackHole 2ch` as the microphone.

The bridge writes to the BlackHole output side; Typeless and QuickTime read the same virtual device as an input. Do not create an Aggregate Device for this path.

## 2. Create the Python 3.10 environment

```sh
cd /path/to/m5-cardputer/bridge
/usr/bin/python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip
python -m pip install -e .
```

If `/usr/bin/python3` is not Python 3.10 or newer, invoke the installed Python 3.10 executable explicitly. No Homebrew package is required.

## 3. First run and pairing

```sh
cd /path/to/m5-cardputer/bridge
source .venv/bin/activate
cardbridge
```

At first launch, macOS asks for Accessibility access. If necessary, open **System Settings → Privacy & Security → Accessibility** and enable the Python executable used by this virtual environment, then restart CardBridge. Without this permission audio still works, but CGEvent keyboard injection is blocked.

Select **Settings → Computers → Add new computer** on the Cardputer. The bridge prints a six-digit code and also posts a macOS notification. Enter that code on the device. The generated 32-byte random token is stored with mode `0600` in `~/.cardbridge/config.json`; later boots authenticate and reconnect automatically.

Useful diagnostic run (no key injection and no sound-device requirement):

```sh
cardbridge --dry-run --no-audio -v
```

## 4. Run as a login service

Run the installer from the activated virtual environment so the LaunchAgent records that exact Python executable:

```sh
cd /path/to/m5-cardputer/bridge
source .venv/bin/activate
python install_launch_agent.py install
```

Logs are written to `~/.cardbridge/bridge.log` and `~/.cardbridge/bridge-error.log`. Remove the service with:

```sh
python install_launch_agent.py uninstall
```

## 5. Enable Codex live status

Session names/projects come from a separate read-only official Codex App Server in every local authentication mode. ChatGPT OAuth additionally exposes its weekly/5-hour subscription limits; API key and custom-provider modes keep Session Pet active but hide the quota HUD. Real-time state comes from official lifecycle Hooks, reported by a fail-open local script over UDP `127.0.0.1:7790`. No prompt text, transcript, tool arguments, or `auth.json` contents are sent to CardBridge.

Preview the merged user-level hook configuration first:

```sh
python install_codex_hooks.py show
```

Install it when the paths look correct:

```sh
python install_codex_hooks.py install
```

Restart or reload Codex and review its hook-trust prompt. Do not bypass that trust check. To remove only CardBridge's hook commands while preserving unrelated hooks:

```sh
python install_codex_hooks.py uninstall
```

Use `cardbridge --no-codex` to disable both the App Server monitor and local hook receiver, or `--hook-port` to choose another loopback port (set the same `CARDBRIDGE_HOOK_PORT` for the reporter).

## 6. End-to-end simulator

Terminal 1:

```sh
cd /path/to/m5-cardputer/bridge
source .venv/bin/activate
cardbridge --dry-run --no-audio -v
```

Terminal 2:

```sh
cd /path/to/m5-cardputer/bridge
source .venv/bin/activate
python fake_device.py
```

Enter the pairing code printed in terminal 1. The simulator authenticates, sends English/shift/punctuation key down+up events, holds/releases F13, sends the reserved phase-two request, and streams a real-time 440 Hz sine wave as authenticated 20 ms UDP frames. Its local token cache is `.fake_device.json` and is git-ignored.

Run all dependency-free tests with:

```sh
cd /path/to/m5-cardputer/bridge
PYTHONPATH=. python -m unittest discover -s tests -v
```

## Protocol and recovery behavior

- TCP `7788`: newline-delimited UTF-8 JSON capped at 4096 bytes, five-second ping/pong, disconnect after three misses. Every post-handshake message carries the session token; unknown authenticated message types are ignored for forward compatibility.
- UDP `7789`: network-order `seq(u32) + timestamp_ms(u32) + HMAC8`, followed by exactly 640 bytes of little-endian PCM16 mono audio.
- Playback starts at a configurable 100 ms jitter depth. Missing sequences become silence; packets are not retransmitted.
- Firmware stops microphone capture and UDP sending whenever muted or disconnected. Reconnect uses exponential backoff capped at 30 seconds.
- A Cardputer maintains exactly one selected Mac control connection, so key and microphone data are never broadcast.
