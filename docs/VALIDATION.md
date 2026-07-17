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

Current 2026-07-16 Codex-history build result (`0.2.0` build 7): 2,271,674 bytes Flash (68.0% of the app partition) and 65,068 bytes static RAM (19.9% of the 327,680-byte linker budget). The uploadable `firmware.bin` is 2,272,080 bytes with SHA-256 `44393dfdc9bfda3f96e3166dce28e3e7a0570598fded09ad0eee3e3415396f01`. All 59 Python tests and all 4 Swift tests pass. The generated 32-frame pet payload is 73,270 RLE bytes; the deterministic native 13px GB2312 BFF contains 7,543 glyphs in 662,008 bytes with SHA-256 `816974b5154623a91f9e94c60d7ef1a8e8c81aa36b38d8d79b65d974911b7040`.

## 2026-07-16 Codex 1:1 UI RC acceptance

- The final Bridge reducer consumes public App Server agent messages and safe file/tool summaries while explicitly ignoring reasoning notifications. Regression fixtures confirm that `turn/started` plus the matching `userMessage item/started` advances device focus only once, unknown auth modes never become Unlimited, failed rate-limit refreshes clear stale values, and Bearer/`sk-…` secrets are redacted before Hook delivery.
- The authenticated device protocol test exercises `unknown → api → subscription → unknown` end to end. Its true worst-case eight-session CJK record is 3,839/4,096 bytes, and the encoder rejects any larger record.
- The signed CardBridge 1.0.0 (5) bundle was rebuilt with the final helper, installed in `/Applications`, and passed nested strict code-sign verification. Its owner-only control API reported Agent build 5 healthy, Codex App Server connected, Hooks installed/listening, and the current account correctly classified as `quota_mode: api`.
- A live control subscription around a real Codex tool call advanced the installed Agent build 5 status sequence from 12 to 15, proving the configured PreToolUse/PostToolUse Hook path reaches the running reducer rather than merely passing isolated tests.
- The already-paired Cardputer reconnected over WiFi without pairing again. An 8-second live check sampled it online 100% of the time with zero outage/errors and 351 additional authenticated audio packets.
- The final firmware compiles with compile-time assertions for the 4+114+4+114+4 geometry, 100×100 pet bounds, 106×18 title, 106×70 activity area, and compact quota rows.
- Build 2 was flashed over `/dev/cu.usbmodem21201` with every image hash verified. Physical inspection then exposed an ArduinoJson 6 edge case: `variant | nullptr` returned null for the valid string `"api"`, so the device degraded the Bridge's API mode to Unknown. A host reproduction proved the conversion bug; build 3 switched to `.as<const char*>()`, was reflashed, and again passed every written-image hash check.
- A controlled build 3 boot reported firmware `0.2.0` build 3/protocol 2.0, a 16-bit UI canvas, all 7,543 Chinese glyphs loaded, and no NVS/audio/UI startup error. The preserved WiFi and pairing data automatically restored `chaoshome`/`JerrydeMac-mini` without pairing again.
- The build 3 device console itself (not only the Mac snapshot) reported `online=1`, `quota_mode=api`, eight parsed sessions, the exact current focus ID, `running/tool`, and a safe concrete activity. The console then opened the Codex page for visual acceptance. A final 12-second live check sampled the real device online 100% of the time with zero outage/errors and 556 additional authenticated audio packets.
- Physical review of build 4 replaced the fast segmented unlimited animation with a low-saturation, continuously interpolated rainbow on a roughly ten-second cycle, removed the sparkle, increased the compact bar from 6px to 8px, and drew two complete infinity loops with a dark halo. The same review found a 12px activity face too small.
- Build 5 tested 13px activity text by fractionally scaling the 15px BFF. Physical review correctly exposed uneven/cramped character spacing caused by M5GFX rounding each scaled glyph advance. Build 6 instead embeds a native 13px Source Han Sans CN BFF and renders it at 1:1 with a 17px line pitch; a clean rebuild confirmed the new linker symbols and reduced Flash from 73.7% to 68.0% without changing static RAM.
- Every build 6 flash region passed esptool's written-image hash verification. The device itself then reported `fw=0.2.0/6`, `bridge=1.0.0/5`, WiFi and Bridge connected, `quota_mode=api`, eight live sessions, the exact current focus, and the controlled mixed CJK/English activity. The user physically accepted the final native-font spacing and complete layout as “非常好了”.
- Build 7 adds a no-Hook history fallback: Agent build 6 polls `thread/list` every two seconds, uses `recencyAt` to follow only a newer user prompt, and orders the latest eight sessions by user-interaction time. Background tool/output updates do not reorder history or steal focus; Hooks remain the higher-fidelity source for immediate status and activity text.
- Regression tests cover the independent-App-Server case, including discovery of a new desktop prompt without Hooks, stable history under background output, and the faster history poll with a separate 30-second account poll. A real eight-thread snapshot selected the newly prompted “发送问候” task as `1/8` without Hook delivery.
- The signed CardBridge 1.0.0 (6) bundle with Agent build 6 was installed in `/Applications`, passed strict nested code-sign verification, and reported healthy from PID 58805. Firmware build 7 was flashed through `/dev/cu.usbmodem21201`; every written region passed hash verification, and the device reported `fw=0.2.0/7`, `bridge=1.0.0/6`, `quota_mode=api`, eight sessions, and the expected current focus.
- The build 7 session badge is a 24×12px `index/count` pill centered at `x=49, y=6` above the left-side pet. The keyboard icon remains at the far left, the pet starts at `y=18`, and the right title keeps its full width.

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
8. Codex: open at least two Codex tasks without enabling Hooks. Submit a prompt in task B and confirm polling follows B within roughly two seconds, places it at `1/N`, and does not switch away when another task only produces background output. Use left/right to browse the latest eight tasks without changing desktop focus; confirm the centered badge advances (`1/8`, `2/8`, …), and that a later user prompt resumes automatic following. Then explicitly trust the CardBridge Hooks and verify the more immediate Waiting/Running/Ready activity sequence. Confirm the full top status bar is absent only on this page; the 100×100 pet occupies the left half; the right half shows a scrolling title, up to four lines of safe public activity, and compact `WEEKLY`/`5H` rows. Verify tool/file summaries never expose raw commands or secrets, Running appears only during tools, and `Enter` clears only the local reminder. Exercise subscription (real fills), API/custom provider (animated full unlimited fills), and failed account lookup (gray `--`) separately.

If microphone quality or timing needs hardware-specific tuning, retain the 320-sample/20 ms queue boundary and adjust only M5.Mic configuration values in `src/audio_tx.cpp`.

For a real-device soak test (24 hours for a public Beta):

```sh
python3 tools/soak_test.py \
  --duration 86400 \
  --interval 5 \
  --require-device \
  --output cardbridge-soak.json
```
