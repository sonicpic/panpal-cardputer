[CmdletBinding()]
param(
    [string]$PythonExe = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$buildVenv = Join-Path $PSScriptRoot ".venv-build"

if (-not $PythonExe) {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        $PythonExe = $py.Source
        $pythonArguments = @("-3")
    } else {
        $python = Get-Command python -ErrorAction Stop
        $PythonExe = $python.Source
        $pythonArguments = @()
    }
} else {
    $pythonArguments = @()
}

& $PythonExe @pythonArguments -c "import sys; assert sys.version_info >= (3, 10), sys.version"
if (-not (Test-Path -LiteralPath $buildVenv)) {
    & $PythonExe @pythonArguments -m venv $buildVenv
}

$venvPython = Join-Path $buildVenv "Scripts\python.exe"
& $venvPython -m pip install --upgrade pip
& $venvPython -m pip install -e (Join-Path $root "bridge") pyinstaller platformio

Write-Host "Windows build environment ready: $venvPython"
