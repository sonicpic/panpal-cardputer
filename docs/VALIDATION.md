# CardBridge validation handoff

`GOAL.md` remains the product source of truth. This file records the repeatable development checks and the physical acceptance sequence for the Codex/user workflow.

## Development checks (safe without hardware)

```sh
cd /path/to/m5-cardputer
pio run
PYTHONPATH=bridge python3 -m unittest discover -s bridge/tests -v
git diff --check
```

Expected results:

- PlatformIO builds `firmware.bin` for `m5stack-stamps3` without PSRAM flags or APIs.
- Python tests cover token persistence, UTF-8 JSON framing/size, audio HMAC rejection, jitter startup, silence loss concealment, six-digit pairing, authenticated/unauthenticated TCP behavior, agent focus/quota state, safe Hook installation, explicit F13 down/up, persisted-token reconnect, and the shipped fake device's UDP sine stream.
- These checks do not require hardware, but Codex may also upload and use the serial monitor when a device is connected.

Current 2026-07-15 build result: 2,457,142 bytes Flash (73.5% of the app partition) and 64,924 bytes static RAM (19.8% of the 327,680-byte linker budget). The generated 32-frame pet payload is 73,270 RLE bytes.

## 2026-07-15 physical Codex smoke test

- Uploaded the completed firmware to the connected Cardputer ADV at `/dev/cu.usbmodem21201`.
- Installed CardBridge as a `RunAtLoad`/`KeepAlive` LaunchAgent and confirmed an authenticated TCP connection from the device.
- The device parsed eight live Codex tasks, selected the current task by its exact session ID, and displayed 33% weekly quota with the unavailable five-hour quota represented as `--`.
- Installed handlers for six official Hook events, then sent representative event payloads through the local reporter to exercise Waiting (prompt/thinking) -> Running (tool execution) -> Waiting (post-tool thinking) -> Ready end to end.
- Automatic Hook delivery still requires the user to explicitly trust the installed command in Codex. macOS Accessibility approval is separately required for keyboard injection from the LaunchAgent; neither permission is bypassed by the installer.

## Physical M1–M4 acceptance

1. Install/configure the Mac service using `bridge/README.md`, then start it before powering the device.
2. Build and flash with `pio run -t upload`. Allow the USB CDC port to reappear after reset and let PlatformIO auto-detect it when possible.
3. M1: with no credentials, confirm automatic 2.4 GHz scan; choose an SSID, type only its password, pair using the displayed six-digit Mac code, reboot, and confirm automatic WiFi/Mac reconnect plus status-bar fields.
4. M2: confirm the status-bar keyboard icon starts off. Navigate to a non-main page, press BtnA, and confirm the icon turns on without changing the page. In a macOS text field type mixed lower/uppercase letters, digits, and punctuation; confirm the device page cannot be operated until BtnA turns keyboard forwarding off. Verify physical Alt+C/V act as Cmd+C/V, Shift modifies the target key without a separate modifier event, `Fn+; , . /` send Up/Left/Down/Right, and `Fn+\`` sends Escape.
5. WiFi UI: confirm scan results use signal icons rather than RSSI numbers. A saved network is forgotten with `Backspace` alone, ESC returns, and password entry preserves lowercase, Shift-uppercase, and shifted symbols.
6. M3: select BlackHole 2ch in QuickTime and record speech. Typeless validation is currently out of scope because it filters out the BlackHole virtual device.
7. M4: separately interrupt WiFi, stop/restart CardBridge, and sleep/wake the Mac. Confirm recovery within 30 seconds. Pair a second Mac and confirm selecting one never sends keys/audio to the other. Verify brightness and each screen-off setting.
8. Codex: install and explicitly trust the CardBridge Hooks, then open two Codex tasks. Submit a prompt in task B and confirm the device follows B; use left/right to view A/B without changing desktop focus. Confirm Waiting while Codex thinks, the laptop-holding Running animation only while a tool executes, Waiting again after the tool returns, Needs input/Ready states, the weekly HP bar, gray `MP 5H --` when no five-hour limit exists, long-title scrolling, and `Enter` clearing only the local reminder.

If microphone quality or timing needs hardware-specific tuning, retain the 320-sample/20 ms queue boundary and adjust only M5.Mic configuration values in `src/audio_tx.cpp`.
