#!/bin/zsh
set -euo pipefail

script_dir=${0:A:h}
repo_dir=${script_dir:h:h}
venv=${repo_dir}/bridge/.venv

if [[ ! -x "${venv}/bin/python" ]]; then
  python3 -m venv "${venv}"
fi
"${venv}/bin/python" -m pip install --disable-pip-version-check \
  -e "${repo_dir}/bridge" \
  -r "${repo_dir}/bridge/requirements-build.txt"
