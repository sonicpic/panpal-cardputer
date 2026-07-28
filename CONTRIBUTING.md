# Contributing to PanPal

PanPal contains the Windows Bridge, Cardputer ADV firmware, installer scripts,
and protocol tests. Read [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md) before
changing code.

Run these checks before opening a pull request:

```powershell
$env:PYTHONPATH = "$PWD\bridge"
.\windows\.venv-build\Scripts\python.exe -m unittest discover -s bridge\tests -v
.\windows\.venv-build\Scripts\python.exe tools\generate_versions.py --check
.\windows\.venv-build\Scripts\python.exe tools\check_project.py
.\windows\.venv-build\Scripts\pio.exe run
git diff --check
```

Generated version files come from `version.json`. Protocol changes must update
`docs/PROTOCOL.md`, compatibility tests, and the changelog in the same pull
request.

Do not commit pairing tokens, Wi-Fi credentials, API keys, Codex data, personal
paths, logs with private content, or audio recordings.
