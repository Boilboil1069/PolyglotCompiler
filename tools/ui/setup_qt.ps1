# setup_qt.ps1 — Download pre-built Qt 6 binaries for Windows using aqtinstall.
#
# Usage:
#   .\scripts\setup_qt.ps1             # default Qt 6.10.2
#   .\scripts\setup_qt.ps1 -QtVersion 6.10.2
#
# Qt is installed into deps\qt\ under the project root.

param(
    [string]$QtVersion = "6.10.2"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir
$QtDir = Join-Path $ProjectRoot "deps\qt"

Write-Host "============================================"
Write-Host " PolyglotCompiler - Qt Setup (Windows)"
Write-Host "============================================"
Write-Host ""
Write-Host "Qt version : $QtVersion"
Write-Host "Install to : $QtDir"
Write-Host ""

# Check if already installed
$QtInstallDir = Join-Path $QtDir $QtVersion
if (Test-Path $QtInstallDir) {
    Write-Host "[OK] Qt $QtVersion is already installed at $QtInstallDir"
    Write-Host "     Delete $QtInstallDir and re-run to reinstall."
    exit 0
}

# Check Python
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $python) {
    Write-Host "[ERROR] Python not found. Please install Python 3.7+ first."
    exit 1
}
$PY = $python.Source

# Install aqtinstall if needed
try {
    & $PY -m aqt version 2>&1 | Out-Null
} catch {
    Write-Host "[..] Installing aqtinstall..."
    & $PY -m pip install --user aqtinstall 2>&1 | Select-Object -Last 3
    Write-Host "[OK] aqtinstall installed"
}

# Determine architecture — prefer MSVC
$QtArch = "win64_msvc2022_64"

Write-Host "[..] Downloading Qt $QtVersion for Windows ($QtArch)..."
if (-not (Test-Path $QtDir)) {
    New-Item -ItemType Directory -Path $QtDir -Force | Out-Null
}

Push-Location $QtDir
try {
    & $PY -m aqt install-qt windows desktop $QtVersion $QtArch
} finally {
    Pop-Location
}

if (Test-Path $QtInstallDir) {
    Write-Host ""
    Write-Host "[OK] Qt $QtVersion installed successfully at:"
    Write-Host "     $QtInstallDir"
    Write-Host ""
    Write-Host "To build polyui, reconfigure CMake:"
    Write-Host "  cd build && cmake .."
    Write-Host "  cmake --build . --target polyui"
} else {
    Write-Host ""
    Write-Host "[ERROR] Qt installation failed."
    exit 1
}
