#!/bin/zsh
set -euo pipefail

script_dir=${0:A:h}
root_dir=${script_dir:h}
venv_python=${root_dir}/bridge/.venv/bin/python
pio_bin=${root_dir}/tools/.venv/bin/pio

[[ -x "${venv_python}" ]] || {
  print -u2 "bridge/.venv is missing. Run ./scripts/bootstrap.sh first."
  exit 1
}

cd "${root_dir}"
"${venv_python}" tools/check_project.py
"${venv_python}" tools/generate_versions.py --check
PYTHONPATH=bridge:. "${venv_python}" -m unittest discover -s bridge/tests -v
swift test --package-path macos
if [[ -x "${pio_bin}" ]]; then
  "${pio_bin}" run
elif command -v pio >/dev/null 2>&1; then
  pio run
else
  print -u2 "PlatformIO is required for the firmware test."
  exit 1
fi
git diff --check
print "All CardBridge checks passed."
