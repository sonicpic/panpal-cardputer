#!/bin/zsh
set -euo pipefail

script_dir=${0:A:h}
macos_dir=${script_dir:h}
repo_dir=${macos_dir:h}
configuration=${CONFIGURATION:-release}
app_dir=${macos_dir}/dist/CardBridge.app
contents_dir=${app_dir}/Contents
agent_app=${contents_dir}/Helpers/CardBridgeAgent.app
agent_contents=${agent_app}/Contents

"${macos_dir}/scripts/bootstrap_sparkle.sh"
"${macos_dir}/scripts/bootstrap_build_env.sh"
"${macos_dir}/scripts/build_icon.sh"
python3 "${repo_dir}/tools/generate_versions.py" --check
"${repo_dir}/bridge/.venv/bin/pyinstaller" \
  --noconfirm \
  --clean \
  --distpath "${macos_dir}/.build/agent-dist" \
  --workpath "${macos_dir}/.build/pyinstaller" \
  "${repo_dir}/bridge/packaging/CardBridgeAgent.spec"
swift build --package-path "${macos_dir}" -c "${configuration}"
bin_dir=$(swift build --package-path "${macos_dir}" -c "${configuration}" --show-bin-path)

rm -rf "${app_dir}"
mkdir -p \
  "${contents_dir}/Frameworks" \
  "${contents_dir}/MacOS" \
  "${contents_dir}/Resources" \
  "${contents_dir}/Helpers"
ditto "${bin_dir}/CardBridgeApp" "${contents_dir}/MacOS/CardBridge"
ditto "${bin_dir}/Sparkle.framework" "${contents_dir}/Frameworks/Sparkle.framework"
ditto "${macos_dir}/App/Info.plist" "${contents_dir}/Info.plist"
ditto "${macos_dir}/App/Resources" "${contents_dir}/Resources"
ditto "${macos_dir}/App/CardBridge.icns" "${contents_dir}/Resources/CardBridge.icns"
ditto "${macos_dir}/.build/agent-dist/CardBridgeAgent.app" "${agent_app}"
ditto "${macos_dir}/App/Agent-Info.plist" "${agent_contents}/Info.plist"
mkdir -p "${agent_contents}/Resources"
ditto "${macos_dir}/App/Resources/en.lproj" "${agent_contents}/Resources/en.lproj"
ditto "${macos_dir}/App/Resources/zh-Hans.lproj" "${agent_contents}/Resources/zh-Hans.lproj"
chmod 755 "${contents_dir}/MacOS/CardBridge"
install_name_tool -add_rpath @executable_path/../Frameworks "${contents_dir}/MacOS/CardBridge"

identity=${CODE_SIGN_IDENTITY:--}
timestamp=${CODE_SIGN_TIMESTAMP:---timestamp=none}

# PyInstaller ships extension modules and dylibs beside the helper executable.
# Sign every Mach-O leaf before sealing its helper bundle and the outer app.
while IFS= read -r candidate; do
  if /usr/bin/file -b "${candidate}" | /usr/bin/grep -q 'Mach-O'; then
    codesign --force --options runtime "${timestamp}" --sign "${identity}" "${candidate}"
  fi
done < <(find "${agent_contents}" -type f -print)
codesign --force --options runtime "${timestamp}" --sign "${identity}" "${agent_app}"
# Sparkle contains its updater helper and XPC services. Xcode's normal
# "Code Sign On Copy" behavior is reproduced here for the SwiftPM bundle.
codesign --force --deep --options runtime "${timestamp}" --sign "${identity}" \
  "${contents_dir}/Frameworks/Sparkle.framework"
codesign --force --options runtime "${timestamp}" --sign "${identity}" "${app_dir}"
codesign --verify --deep --strict --verbose=2 "${app_dir}"

echo "Built ${app_dir}"
