[CmdletBinding()]
param(
    [switch]$SkipBootstrap,
    [switch]$SkipFirmware,
    [switch]$SkipInstaller
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$venvPython = Join-Path $PSScriptRoot ".venv-build\Scripts\python.exe"
$pio = Join-Path $PSScriptRoot ".venv-build\Scripts\pio.exe"

function Assert-NativeSuccess([string]$Step) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE"
    }
}

if (-not $SkipBootstrap -or -not (Test-Path -LiteralPath $venvPython)) {
    & (Join-Path $PSScriptRoot "bootstrap.ps1")
    Assert-NativeSuccess "Windows bootstrap"
}

Push-Location $root
try {
    $versionData = Get-Content -Raw version.json | ConvertFrom-Json
    & $venvPython tools\generate_versions.py --check
    Assert-NativeSuccess "Generated version check"
    $env:PYTHONPATH = (Join-Path $root "bridge")
    & $venvPython -m unittest discover -s bridge\tests -v
    Assert-NativeSuccess "Python tests"
    & $venvPython -m PyInstaller --noconfirm --clean `
        --distpath (Join-Path $root "dist\windows") `
        --workpath (Join-Path $root "build\pyinstaller") `
        bridge\packaging\CardBridgeWindows.spec
    Assert-NativeSuccess "Windows application package"
    if (-not $SkipFirmware) {
        & $pio run
        Assert-NativeSuccess "Firmware build"
        $firmwareSource = Join-Path $root ".pio\build\cardputer\firmware.bin"
        $firmwareTarget = Join-Path $root "dist\firmware\panpal-dapan.bin"
        $firmwareVersionedTarget = Join-Path $root (
            "dist\firmware\panpal-dapan-{0}-build{1}.bin" -f `
                $versionData.firmware.version, $versionData.firmware.build
        )
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $firmwareTarget) | Out-Null
        Copy-Item -LiteralPath $firmwareSource -Destination $firmwareTarget -Force
        Copy-Item -LiteralPath $firmwareSource -Destination $firmwareVersionedTarget -Force
    }
    if (-not $SkipInstaller) {
        $isccCommand = Get-Command iscc.exe -ErrorAction SilentlyContinue
        $isccPath = if ($isccCommand) { $isccCommand.Source } else {
            @(
                (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
                "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
                "C:\Program Files\Inno Setup 6\ISCC.exe"
            ) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        }
        if (-not $isccPath) {
            throw "Inno Setup 6 is required to create the installer. Install it with: winget install JRSoftware.InnoSetup"
        }
        & $isccPath "/DAppVersion=$($versionData.release)" windows\installer\CodexDeck.iss
        Assert-NativeSuccess "Windows installer"
    }
    $releaseFiles = @(
        (Join-Path $root "dist\firmware\panpal-dapan.bin"),
        (Join-Path $root (
            "dist\firmware\panpal-dapan-{0}-build{1}.bin" -f `
                $versionData.firmware.version, $versionData.firmware.build
        )),
        (Join-Path $root "dist\installer\PanPal-$($versionData.release)-setup.exe")
    ) | Where-Object { Test-Path -LiteralPath $_ }
    $checksums = foreach ($file in $releaseFiles) {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $file
        "{0} *{1}" -f $hash.Hash, (Split-Path -Leaf $file)
    }
    $checksums | Set-Content -LiteralPath (Join-Path $root "dist\SHA256SUMS.txt") -Encoding ascii
} finally {
    Pop-Location
}
