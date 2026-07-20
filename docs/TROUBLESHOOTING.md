# Troubleshooting

## Start with diagnostics

```sh
./scripts/doctor.sh
./scripts/healthcheck.sh --json
```

Redact tokens, Wi-Fi passwords, Codex content, and personal paths before
sharing output.

## App does not start

- Confirm `/Applications/CardBridge.app` exists and its version matches
  `version.json`.
- Run `codesign --verify --deep --strict /Applications/CardBridge.app`.
- Re-run `./scripts/install.sh` after quitting the old menu bar process.

## Keyboard forwarding does not work

Open **System Settings → Privacy & Security → Accessibility** and allow
CardBridge. Restart the App after changing the grant. Audio and discovery can
continue to work without this permission, but Quartz key injection cannot.

## Microphone is missing

Open CardBridge Settings → Audio and approve the administrator installation.
Then check Audio MIDI Setup for both `CardBridge Microphone` and
`CardBridge Microphone Feed`. Restarting `coreaudiod` may be required after a
manual driver change. BlackHole 2ch is a supported fallback.

## Cardputer is not discovered

- Allow Local Network access for CardBridge.
- Confirm the Mac and Cardputer are on the same 2.4 GHz network.
- Check that ports 7788/TCP and 7789/UDP are not blocked on the local network.
- Start the App before opening the Cardputer pairing screen.

## Pairing fails

Start a fresh **Add new computer** flow and use the current six-digit code.
Do not copy a token from a config file. If a previous pairing is stale, remove
that device from the App Settings and pair it again.

## Source build fails

Run `./scripts/doctor.sh --json` and fix the first item reported as `error`.
Most failures are an old Python, a missing Xcode selection, or a missing
PlatformIO executable. The project does not require Homebrew.
