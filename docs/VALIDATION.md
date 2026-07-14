# CardBridge validation handoff

`GOAL.md` remains the product source of truth. This file records the repeatable development checks and the physical acceptance sequence for the Claude/user handoff.

## Development checks (safe without hardware)

```sh
cd "/Users/chaos/m5 cardputer"
export PATH="$HOME/Library/Python/3.10/bin:$PATH"
pio run
PYTHONPATH=bridge python3 -m unittest discover -s bridge/tests -v
git diff --check
```

Expected results:

- PlatformIO builds `firmware.bin` for `m5stack-stamps3` without PSRAM flags or APIs.
- Python tests cover token persistence, JSON framing, audio HMAC rejection, jitter startup, silence loss concealment, six-digit pairing, authenticated/unauthenticated TCP behavior, unknown phase-two messages, explicit F13 down/up, persisted-token reconnect, and the shipped fake device's UDP sine stream.
- No upload or serial-monitor command is part of this sequence.

## Physical M1–M4 acceptance

1. Install/configure the Mac service using `bridge/README.md`, then start it before powering the device.
2. Flash through the designated hardware-validation workflow (not the development workflow).
3. M1: with no credentials, confirm automatic 2.4 GHz scan; choose an SSID, type only its password, pair using the displayed six-digit Mac code, reboot, and confirm automatic WiFi/Mac reconnect plus status-bar fields.
4. M2: in a macOS text field type mixed lower/uppercase letters, digits, and punctuation. Verify physical Alt+C/V act as Cmd+C/V, Shift works, Fn+I/J/K/L are arrows, and holding/releasing Fn+Space produces F13 down/up for Typeless. Cycle the Typeless-key setting once and confirm F14 is emitted instead.
5. M3: select BlackHole 2ch in QuickTime and record speech; then select it in Typeless and perform hold-to-record. Confirm the device's level bar follows speech and mute stops the stream.
6. M4: separately interrupt WiFi, stop/restart CardBridge, and sleep/wake the Mac. Confirm recovery within 30 seconds. Pair a second Mac and confirm selecting one never sends keys/audio to the other. Verify brightness and each screen-off setting.

If microphone quality or timing needs hardware-specific tuning, retain the 320-sample/20 ms queue boundary and adjust only M5.Mic configuration values in `src/audio_tx.cpp`.
