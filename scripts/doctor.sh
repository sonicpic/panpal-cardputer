#!/bin/zsh
set -euo pipefail

root_dir=${0:A:h:h}
python_bin=${PYTHON_BIN:-python3}
if ! command -v "${python_bin}" >/dev/null 2>&1; then
  print -u2 "Python is required for diagnostics. Set PYTHON_BIN to Python 3.10+."
  exit 1
fi
exec "${python_bin}" "${root_dir}/tools/doctor.py" "$@"
