#!/bin/zsh
set -euo pipefail

script_dir=${0:A:h}
root_dir=${script_dir:h}
pio_bin=${root_dir}/tools/.venv/bin/pio

cd "${root_dir}"
if [[ -x "${pio_bin}" ]]; then
  "${pio_bin}" run
elif command -v pio >/dev/null 2>&1; then
  pio run
else
  print -u2 "PlatformIO is missing. Run ./scripts/bootstrap.sh first."
  exit 1
fi
macos/scripts/build_app.sh
"${root_dir}/bridge/.venv/bin/python" tools/validate_release.py --app macos/dist/CardBridge.app
print "Build and package validation passed."
