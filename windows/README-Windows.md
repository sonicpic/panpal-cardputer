# PanPal for Windows

PanPal runs in the Windows user session, accepts authenticated Cardputer
keyboard/audio traffic over Wi-Fi or Bluetooth, and hides to the system tray.

## Before pairing

1. Install [VB-CABLE](https://vb-audio.com/Cable/) separately.
2. Open PanPal settings and choose **CABLE Input** as the feed output and
   **CABLE Output** as the temporary virtual microphone.
3. Configure the target voice shortcut, held/toggle mode, microphone restore
   behavior. Automatic Enter is controlled on the Cardputer with `Fn+Enter`.
4. On the Cardputer select Wi-Fi or Bluetooth, then open
   **Computers > Add computer** and enter the Windows six-digit code.

Hold G0 or `Fn+Space` to speak. Double-click G0 to lock a long recording and
press it once to stop. Closing the window leaves the Bridge running; use the
tray menu to open, restart, or exit it.

When automatic Enter is enabled on the Cardputer, Bridge waits one second after ending voice
input before submitting Enter. Starting another utterance during that second
cancels the pending Enter.

The microphone icon turns green only after the Bridge confirms that microphone
routing and the configured voice shortcut started successfully. `Fn+Tab` only
switches between local controls and keyboard forwarding; it does not establish
a connection. A cyan keyboard means Remote mode with a live Bridge connection,
while a red/slashed keyboard means Remote mode was selected but the connection
is unavailable.

After the first successful Bluetooth pairing, Bridge automatically reconnects
after either Windows or the Cardputer restarts. If Windows has cached an older,
incomplete GATT database, Bridge refreshes the Windows bond and silently pairs
again while preserving the application token. The six-digit application code
is only needed after pairing data is explicitly removed or reset.

## Codex status

The Bridge uses the official app-server from the VS Code Codex extension,
ChatGPT/Codex desktop app, or CLI to read their shared session database. It
does not infer status from window titles. Optional shared lifecycle Hooks can
be installed or removed in the settings UI; restart Codex and approve its
trust prompt yourself.

CLI equivalents remain available:

```powershell
.\CardBridge.exe --install-hooks
.\CardBridge.exe --uninstall-hooks
```

## Diagnostics

- Log: `%LOCALAPPDATA%\CodexDeck\logs\bridge.log`
- Settings: `%APPDATA%\CodexDeck\config.json`
- DPAPI pairing secrets: `%APPDATA%\CodexDeck\pairing-secrets`

```powershell
.\CardBridge.exe --dry-run --no-audio -v
```

Do not publish Wi-Fi credentials, pairing tokens, Codex content, or recordings.
