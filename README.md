# CardBridge for Cardputer ADV

CardBridge turns an M5Stack Cardputer ADV into a WiFi microphone, keyboard, and Codex session companion for macOS. The firmware streams authenticated 16 kHz PCM audio and explicit key down/up events to the companion service in `bridge/`; its Codex page shows the latest user-focused task, animated state, weekly/5-hour limits, and up to eight switchable sessions. Claude remains a placeholder.

The authoritative product requirements and acceptance criteria are in [`docs/GOAL.md`](docs/GOAL.md). Mac installation, BlackHole and Typeless setup, simulator use, and service operation are documented in [`bridge/README.md`](bridge/README.md).

## Build firmware

```sh
cd /path/to/m5-cardputer
pio run
```

Install PlatformIO Core first if `pio` is not already on `PATH`.

Do not run `pio run -t upload` during the development phase. Flashing and physical-device acceptance are intentionally left to the designated hardware-validation workflow.

## Build pet animation assets

The firmware ships with a deterministic Codex-themed development mascot. Rebuild it with the bundled offline packer:

```sh
cd /path/to/m5-cardputer
python3 tools/pack_pet.py --demo --output-dir src
```

To use a desktop Codex v2 pet created by the official `hatch-pet` workflow, point the same adapter at its installed package:

```sh
python3 tools/pack_pet.py \
  --pet-dir "$HOME/.codex/pets/my-pet" \
  --output-dir src
```

The adapter validates the 1536×2288, 8×11 v2 atlas, selects the relevant animation rows, scales frames to 72×72, quantizes them to a shared 16-colour palette, and writes row-safe RLE into `src/pet_assets.*`. The Cardputer decodes those runs directly from flash and allocates no per-frame image buffer.

## Device controls

- BtnA toggles keyboard forwarding. The keyboard icon at the far left of the status bar shows whether forwarding is on; toggling it never changes the current page.
- With keyboard forwarding on, `Fn+;`, `Fn+,`, `Fn+.`, and `Fn+/` send Up, Left, Down, and Right; `Fn+\`` sends Escape. Shift is attached to the target key as a macOS modifier; Ctrl/Cmd/Option retain normal down/up events.
- With keyboard forwarding off, use the printed arrow keys (`; . , /`) or `I/J/K/L` to navigate, `Enter` to confirm, and the backtick/ESC key to go back.
- On the Codex page, left/right changes the displayed session and `Enter` marks its Cardputer-only completion/blocked reminder as seen. A newer user prompt automatically moves the pet back to that session.
- In WiFi and paired-Mac lists, `Backspace` forgets or deletes the selected saved item. No Fn chord is required.
- Password entry preserves case and shifted symbols: hold `Shift` while typing uppercase letters or symbols. `Backspace` edits and the backtick/ESC key cancels.
- WiFi setup always starts from a scan list; only the password is typed.
