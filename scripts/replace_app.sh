#!/bin/zsh
set -euo pipefail

if (( $# != 1 )); then
  print -u2 "Usage: replace_app.sh /path/to/CardBridge.app"
  exit 2
fi

source_app=${1:A}
target_app=/Applications/CardBridge.app
[[ -d "${source_app}" ]] || { print -u2 "App not found: ${source_app}"; exit 1; }

stage=$(mktemp -d /tmp/CardBridge-app.XXXXXX)
backup="${target_app}.previous.$$"
trap 'rm -rf "${stage}" "${backup}"' EXIT
ditto "${source_app}" "${stage}/CardBridge.app"
if [[ -e "${target_app}" ]]; then
  mv "${target_app}" "${backup}"
fi
if ! ditto "${stage}/CardBridge.app" "${target_app}"; then
  rm -rf "${target_app}"
  if [[ -e "${backup}" ]]; then
    mv "${backup}" "${target_app}"
  fi
  exit 1
fi
rm -rf "${backup}"
print "Installed ${target_app}"
