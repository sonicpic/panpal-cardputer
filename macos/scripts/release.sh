#!/bin/zsh
set -euo pipefail

script_dir=${0:A:h}
macos_dir=${script_dir:h}
repo_dir=${macos_dir:h}
sparkle_dir=${macos_dir}/.deps/Sparkle
sparkle_account=${SPARKLE_ACCOUNT:-com.voltwake.cardbridge}
identity=${CODE_SIGN_IDENTITY:--}
require_notarization=${REQUIRE_NOTARIZATION:-0}

read -r release_version app_build firmware_version <<EOF
$(python3 - "${repo_dir}/version.json" <<'PY'
import json
import sys
data = json.load(open(sys.argv[1], encoding="utf-8"))
print(data["release"], data["mac_app"]["build"], data["firmware"]["version"])
PY
)
EOF

output_dir=${RELEASE_OUTPUT_DIR:-${macos_dir}/dist/release-${release_version}}
archive=${output_dir}/CardBridge-${release_version}.zip
dmg=${output_dir}/CardBridge-${release_version}.dmg
firmware=${output_dir}/cardputer-adv-firmware-${firmware_version}.bin

cd "${repo_dir}"
python3 tools/generate_versions.py --check
"${repo_dir}/scripts/bootstrap.sh"
PYTHONPATH=bridge:. bridge/.venv/bin/python -m unittest discover -s bridge/tests -v
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift test --package-path macos

if [[ -x "${repo_dir}/tools/.venv/bin/pio" ]]; then
  "${repo_dir}/tools/.venv/bin/pio" run
elif command -v pio >/dev/null 2>&1; then
  pio run
elif [[ -x "${HOME}/.platformio/penv/bin/pio" ]]; then
  "${HOME}/.platformio/penv/bin/pio" run
else
  print -u2 "PlatformIO is required for a release build."
  exit 1
fi

if [[ "${identity}" == Developer\ ID\ Application:* ]]; then
  export CODE_SIGN_TIMESTAMP=--timestamp
else
  export CODE_SIGN_TIMESTAMP=--timestamp=none
fi
CODE_SIGN_IDENTITY="${identity}" "${macos_dir}/scripts/build_app.sh"

validation=(python3 tools/validate_release.py --app "${macos_dir}/dist/CardBridge.app")
if [[ "${require_notarization}" == "1" ]]; then
  validation+=(--require-developer-id)
fi
"${validation[@]}"

mkdir -p "${output_dir}"
find "${output_dir}" -mindepth 1 -maxdepth 1 -delete

if [[ "${require_notarization}" == "1" ]]; then
  pre_notary=$(mktemp "/tmp/CardBridge-${release_version}.XXXXXX.zip")
  ditto -c -k --sequesterRsrc --keepParent "${macos_dir}/dist/CardBridge.app" "${pre_notary}"
  if [[ -n "${NOTARY_PROFILE:-}" ]]; then
    xcrun notarytool submit "${pre_notary}" --keychain-profile "${NOTARY_PROFILE}" --wait
  elif [[ -n "${NOTARY_KEY:-}" && -n "${NOTARY_KEY_ID:-}" && -n "${NOTARY_ISSUER_ID:-}" ]]; then
    xcrun notarytool submit "${pre_notary}" \
      --key "${NOTARY_KEY}" \
      --key-id "${NOTARY_KEY_ID}" \
      --issuer "${NOTARY_ISSUER_ID}" \
      --wait
  else
    print -u2 "Notarization requires NOTARY_PROFILE or App Store Connect API key variables."
    exit 1
  fi
  xcrun stapler staple "${macos_dir}/dist/CardBridge.app"
  python3 tools/validate_release.py \
    --app "${macos_dir}/dist/CardBridge.app" \
    --require-developer-id \
    --require-notarized
fi

ditto -c -k --sequesterRsrc --keepParent "${macos_dir}/dist/CardBridge.app" "${archive}"
"${macos_dir}/scripts/create_dmg.sh" "${macos_dir}/dist/CardBridge.app" "${dmg}"
ditto ".pio/build/cardputer/firmware.bin" "${firmware}"
ditto "release/compatibility.json" "${output_dir}/compatibility.json"
ditto "release/RELEASE_NOTES.md" "${output_dir}/CardBridge-${release_version}.md"
ditto "release/appcast.xml" "${output_dir}/appcast.xml"
ditto "LICENSE" "${output_dir}/LICENSE"
ditto "NOTICE.md" "${output_dir}/NOTICE.md"
ditto "THIRD_PARTY_NOTICES.md" "${output_dir}/THIRD_PARTY_NOTICES.md"
"${repo_dir}/bridge/.venv/bin/python" tools/generate_sbom.py \
  --output "${output_dir}/CardBridge-${release_version}.spdx.json"

download_prefix="https://github.com/sonicpic/panpal-cardputer/releases/download/v${release_version}/"
appcast_args=(
  --download-url-prefix "${download_prefix}"
  --embed-release-notes
  --versions "${app_build}"
  -o "${output_dir}/appcast.xml"
  "${output_dir}"
)
if [[ -n "${SPARKLE_ED_KEY:-}" ]]; then
  print -r -- "${SPARKLE_ED_KEY}" | \
    "${sparkle_dir}/bin/generate_appcast" --ed-key-file - "${appcast_args[@]}"
else
  "${sparkle_dir}/bin/generate_appcast" --account "${sparkle_account}" "${appcast_args[@]}"
fi

artifacts=(
  "${archive}"
  "${dmg}"
  "${firmware}"
  "${output_dir}/compatibility.json"
  "${output_dir}/CardBridge-${release_version}.spdx.json"
)
python3 tools/write_release_manifest.py \
  --output "${output_dir}/release-manifest.json" \
  "${artifacts[@]}"
checksum_files=(
  "${archive:t}"
  "${dmg:t}"
  "${firmware:t}"
  compatibility.json
  "CardBridge-${release_version}.md"
  "CardBridge-${release_version}.spdx.json"
  appcast.xml
  LICENSE
  NOTICE.md
  THIRD_PARTY_NOTICES.md
  release-manifest.json
)
(
  cd "${output_dir}"
  shasum -a 256 "${checksum_files[@]}" > SHA256SUMS
)

if [[ "${UPDATE_REPOSITORY_APPCAST:-0}" == "1" ]]; then
  ditto "${output_dir}/appcast.xml" "${repo_dir}/release/appcast.xml"
fi

print "Release candidate ready: ${output_dir}"
