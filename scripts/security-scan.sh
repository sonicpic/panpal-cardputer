#!/bin/zsh
set -euo pipefail
root_dir=${0:A:h:h}
exec python3 "${root_dir}/tools/scan_secrets.py"
