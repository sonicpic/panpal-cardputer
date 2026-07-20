# Agent instructions for Codex Deck

Codex Deck is the public product name. `CardBridge` is the current internal
compatibility name for the App, Agent, Python package, protocol, and audio
devices. Follow [`docs/BRANDING.md`](docs/BRANDING.md) before renaming any
technical identifier.

## Read first

1. `README.md` for the user-facing overview.
2. `docs/INSTALL.md` for the canonical installation flow.
3. `docs/DEVELOPMENT.md` for source builds and tests.
4. `docs/SECURITY.md` and `SECURITY.md` before changing permissions, pairing,
   Codex data handling, or update behavior.

Do not treat historical planning or validation logs as current instructions.
`docs/README.md` labels the current and archived documents.

## Canonical commands

```sh
./scripts/install-release.sh              # prebuilt public release
./scripts/doctor.sh
./scripts/bootstrap.sh
./scripts/test.sh
./scripts/build.sh
./scripts/install.sh
./scripts/healthcheck.sh
```

Use `--json` on `doctor.sh` and `healthcheck.sh` when an automated caller must
parse the result. Commands are safe to rerun unless explicitly named
`uninstall` or `clean`.

## Permission boundaries

An agent may prepare, build, verify, and launch Codex Deck. It must stop and
ask the user to approve macOS administrator, Accessibility, Local Network,
Microphone, Keychain, or Codex Hook trust prompts. Never bypass a consent
dialog, scrape a password, or disable Gatekeeper.

## Generated files and secrets

Never hand-edit generated version files. Run
`python3 tools/generate_versions.py` after changing `version.json`.

Never read, print, commit, or transmit pairing tokens, Wi-Fi passwords, API
keys, Codex `auth.json`, transcripts, command output, or audio recordings.

## Completion criteria

Installation is complete only when the installed App version matches
`version.json`, the App and Agent are running, the driver status is reported,
and `./scripts/healthcheck.sh` succeeds or clearly reports the user action
still required. Hardware flashing is a separate, explicit operation.
