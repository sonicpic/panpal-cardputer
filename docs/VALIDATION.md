# CardBridge validation handoff

`GOAL.md` remains the product source of truth. This file records the repeatable development checks and the physical acceptance sequence for the Codex/user workflow.

## Development checks (safe without hardware)

```sh
cd /path/to/m5-cardputer
python3 tools/generate_versions.py --check
pio run
PYTHONPATH=bridge python3 -m unittest discover -s bridge/tests -v
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test --package-path macos
git diff --check
```

Expected results:

- PlatformIO builds `firmware.bin` for `m5stack-stamps3` without PSRAM flags or APIs.
- Python tests cover version/config generation and Keychain migration, device compatibility negotiation, token persistence, UTF-8 JSON framing/size, audio HMAC rejection, jitter startup, silence loss concealment, six-digit pairing, authenticated/unauthenticated TCP behavior, the owner-only local menu bar status/control API, snapshot token redaction, network-address refresh, missing-BlackHole degradation, agent focus/quota state, safe Hook installation, explicit F13 down/up, persisted-token reconnect, and the shipped fake device's UDP sine stream.
- Swift tests cover App/Agent build locking, live snapshot decoding, and diagnostic token/path/code redaction.
- These checks do not require hardware, but Codex may also upload and use the serial monitor when a device is connected.

Current 2026-07-16 build result: 2,458,986 bytes Flash (73.6% of the app partition) and 64,980 bytes static RAM (19.8% of the 327,680-byte linker budget). All 46 Python tests and all 4 Swift tests pass. The generated 32-frame pet payload is 73,270 RLE bytes.

## 2026-07-16 macOS 1.0 RC acceptance

- Xcode 26.6 builds `CardBridge.app` 1.0.0 (4) with embedded `CardBridgeAgent.app` 1.0.0 (4), Sparkle 2.9.4, a standard `.icns`, English/Chinese resources, and Hardened Runtime.
- Installing over the previous App migrated the existing device token from plaintext config into macOS Keychain; the real M5 reconnected with no pairing in 3.03 seconds.
- A forced Agent `SIGKILL` was recovered by the App supervisor; a new Agent PID and the real M5 connection were healthy again in 9.69 seconds with pairing intact.
- The signed Sparkle 1.0.0 archive verifies with the generated EdDSA key, extracts cleanly, passes nested `codesign --verify`, and ships with compatibility JSON, firmware 0.2.0 (1), release manifest, and SHA-256 checksums.
- Login launch is registered and enabled through `SMAppService`. Public distribution still requires the external Developer ID Application certificate and Notarization credentials; local RC validation uses the installed Apple Development identity.
- Final build 4 keeps a visible `CardBridge` text label beside a verified SF Symbol in the menu bar. The native App now registers the Accessibility request before launching its Agent; after granting `CardBridge`, an Agent restart reported `accessibility: true`, and a subsequent overwrite installation retained that permission while the real M5 reconnected with no issues.

## 2026-07-16 protocol v2 physical smoke test

- Uploaded firmware `0.2.0` build 1 to the Cardputer ADV over `/dev/cu.usbmodem1401`; esptool verified every written image hash and reset the device successfully.
- Serial boot output reported firmware `0.2.0` build 1 and device protocol `2.0`. A 15-second controlled reboot capture completed without the former WiFi-scan UDP send errors.
- The existing paired-device token survived the upgrade. The device automatically reconnected to Bridge Agent `0.2.0` build 1 without pairing again.
- The owner-only local Agent API reported `compatibility: ok`, protocol `2.0`, firmware build 1, all five negotiated capabilities, working Accessibility/audio/Codex health, and continuously increasing authenticated audio packets.
- The same live status snapshot was checked to contain no pairing token.

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

For a real-device soak test (24 hours for a public Beta):

```sh
python3 tools/soak_test.py \
  --duration 86400 \
  --interval 5 \
  --require-device \
  --output cardbridge-soak.json
```
