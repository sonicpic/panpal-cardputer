# CardBridge for Cardputer ADV

CardBridge turns an M5Stack Cardputer ADV into a WiFi microphone and keyboard for macOS. The firmware streams authenticated 16 kHz PCM audio and explicit key down/up events to the companion service in `bridge/`. Claude and Codex pages are phase-two placeholders; their protocol hooks are already reserved.

The authoritative product requirements and acceptance criteria are in [`docs/GOAL.md`](docs/GOAL.md). Mac installation, BlackHole and Typeless setup, simulator use, and service operation are documented in [`bridge/README.md`](bridge/README.md).

## Build firmware

```sh
cd "/Users/chaos/m5 cardputer"
export PATH="$HOME/Library/Python/3.10/bin:$PATH"
pio run
```

Do not run `pio run -t upload` during the development phase. Flashing and physical-device acceptance are intentionally left to the designated hardware-validation workflow.

## Device controls

- On the main screen, ordinary keys are sent to the Mac. Use `Fn+I/K` to move through the menu and `Fn+Enter` to open a page.
- `Fn+Space` is the dedicated Typeless hold key. It defaults to F13 and can be changed to F14–F16 in Settings.
- In settings lists, use `I/K`, `Enter`, and `Backspace`. `Fn+Backspace` forgets a WiFi network or deletes a Mac pairing.
- WiFi setup always starts from a scan list; only the password is typed.
