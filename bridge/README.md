# CardBridge development

`CardBridge` is the internal Windows service used by PanPal. It receives
authenticated Cardputer control and audio traffic over Wi-Fi or Bluetooth LE,
writes microphone audio to VB-CABLE, injects keyboard shortcuts with
`SendInput`, and sends a privacy-trimmed Codex task snapshot back to the
device.

## Setup

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\bootstrap.ps1
```

The build environment is stored in `windows/.venv-build`. It does not change
packages in the system Python installation.

## Run from source

```powershell
$env:PYTHONPATH = "$PWD\bridge"
.\windows\.venv-build\Scripts\python.exe -m cardbridge --dry-run --no-audio -v
```

Remove `--dry-run` to enable key injection and remove `--no-audio` to write to
the configured `CABLE Input` playback endpoint. Changing the Windows default
microphone requires an explicit voice action from the Cardputer.

## Codex status sources

Local session monitoring is always enabled unless the program starts with
`--no-codex`. It launches a compatible local `codex.exe app-server`, reads the
shared task list every three seconds, and inspects only lifecycle markers in the
associated rollout files.

Hooks are optional. They add immediate tool, permission, and user-input events
through UDP `127.0.0.1:7790`. The settings button controls only Hooks; task
history and basic running/completed state continue without them.

```powershell
.\CardBridge.exe --install-hooks
.\CardBridge.exe --uninstall-hooks
```

## Tests and build

```powershell
$env:PYTHONPATH = "$PWD\bridge"
.\windows\.venv-build\Scripts\python.exe -m unittest discover -s bridge\tests -v
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows\build.ps1
```

The complete build creates the tray program, installer, firmware binaries, and
`dist/SHA256SUMS.txt`.

## Protocol summary

- TCP `7788`: authenticated JSON control records.
- UDP `7789`: authenticated PCM16 mono audio in Wi-Fi mode.
- BLE GATT: control records and IMA ADPCM audio in Bluetooth mode.
- UDP `127.0.0.1:7790`: optional local Codex Hook events.

Pairing tokens use Windows DPAPI. Logs and device status records must not
contain tokens, Wi-Fi passwords, prompts, conversation text, reasoning, raw
commands, tool arguments, tool output, or recordings.
