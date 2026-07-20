#!/bin/zsh
set -euo pipefail

root_dir=${0:A:h:h}
python_bin=${PYTHON_BIN:-python3}
exec "${python_bin}" "${root_dir}/tools/healthcheck.py" "$@"
