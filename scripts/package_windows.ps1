<#
.SYNOPSIS
    PolyglotCompiler Windows Release Packaging Script
.DESCRIPTION
    Builds the project in Release mode, deploys Qt dependencies via windeployqt,
    and produces both a portable ZIP archive and an NSIS installer executable.
.PARAMETER QtRoot
    Root directory of the Qt installation (default: D:\Qt).
.PARAMETER BuildDir
    Build output directory (default: build-release).
.PARAMETER OutputDir
    Directory where final packages are placed (default: dist).
.PARAMETER SkipBuild
    If set, skip the CMake configure+build step and package an existing build.
.PARAMETER SkipInstaller
    If set, skip NSIS installer generation (only produce portable ZIP).
#>

[CmdletBinding()]
param(
    [string]$QtRoot    = "D:\Qt",
    [string]$BuildDir  = "build-release",
    [string]$OutputDir = "dist",
    [switch]$SkipBuild,
    [switch]$SkipInstaller
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ============================================================================
# Constants
# ============================================================================
$ProjectRoot   = Split-Path -Parent $PSScriptRoot
$Version       = "1.0.0"
$ProductName   = "PolyglotCompiler"
$ArchiveBase   = "${ProductName}-${Version}-windows-x64"
$StageDir      = Join-Path (Join-Path $OutputDir "stage") $ArchiveBase
$NsisScript    = Join-Path $PSScriptRoot "installer.nsi"

# List of tool executables produced by the build
$ToolExes = @(
    "polyc.exe",
    "polyld.exe",
    "polyasm.exe",
    "polyopt.exe",
    "polyrt.exe",
    "polybench.exe"
)

# ============================================================================
# Helper Functions
# ============================================================================
function Write-Step([string]$msg) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host " $msg" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Assert-Command([string]$cmd) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        Write-Error "Required command not found: $cmd"
        exit 1
    }
}

# ============================================================================
# Step 0 — Pre-flight checks
# ============================================================================
Write-Step "Pre-flight checks"

Assert-Command "cmake"

# Determine build generator — prefer Ninja, fall back to default (Visual Studio)
$Generator = $null
if (Get-Command "ninja" -ErrorAction SilentlyContinue) {
    $Generator = "Ninja"
    Write-Host "[OK] Generator : Ninja"
} else {
    Write-Host "[OK] Generator : CMake default (Visual Studio / NMake)"
}

# Locate Qt
$QtVersionDirs = Get-ChildItem -Directory $QtRoot -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.\d+' } |
    Sort-Object Name -Descending
if (-not $QtVersionDirs) {
    Write-Error "No Qt version directories found under $QtRoot"
    exit 1
}
$QtVersionDir = $QtVersionDirs[0].FullName

# Find MSVC kit directory (e.g. msvc2022_64)
$MsvcKit = Get-ChildItem -Directory $QtVersionDir |
    Where-Object { $_.Name -match 'msvc\d+_64' } |
    Select-Object -First 1
if (-not $MsvcKit) {
    Write-Error "No MSVC x64 kit found under $QtVersionDir"
    exit 1
}
$QtPrefixPath = $MsvcKit.FullName
$WinDeployQt = Join-Path (Join-Path $QtPrefixPath "bin") "windeployqt.exe"

if (-not (Test-Path $WinDeployQt)) {
    # Fallback: try windeployqt6.exe
    $WinDeployQt = Join-Path (Join-Path $QtPrefixPath "bin") "windeployqt6.exe"
}
if (-not (Test-Path $WinDeployQt)) {
    Write-Error "windeployqt not found in $QtPrefixPath\bin"
    exit 1
}

Write-Host "[OK] Qt prefix : $QtPrefixPath"
Write-Host "[OK] windeployqt: $WinDeployQt"

# ============================================================================
# Step 1 — Build in Release mode
# ============================================================================
if (-not $SkipBuild) {
    Write-Step "Configuring CMake (Release)"
    Push-Location $ProjectRoot

    $cmakeArgs = @("-S", ".", "-B", $BuildDir,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_PREFIX_PATH=$QtPrefixPath",
        "-DQT_ROOT=$QtRoot")
    if ($Generator) {
        $cmakeArgs = @("-S", ".", "-B", $BuildDir, "-G", $Generator,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_PREFIX_PATH=$QtPrefixPath",
            "-DQT_ROOT=$QtRoot")
    }
    & cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { Pop-Location; exit 1 }

    Write-Step "Building project"
    cmake --build $BuildDir --config Release
    if ($LASTEXITCODE -ne 0) { Pop-Location; exit 1 }
    Pop-Location
} else {
    Write-Host "[SKIP] Build step skipped (--SkipBuild)"
}

$BuildPath = Join-Path $ProjectRoot $BuildDir

# ============================================================================
# Step 2 — Stage portable directory
# ============================================================================
Write-Step "Staging portable distribution"

if (Test-Path $StageDir) {
    Remove-Item -Recurse -Force $StageDir
}
New-Item -ItemType Directory -Force $StageDir | Out-Null
New-Item -ItemType Directory -Force (Join-Path $StageDir "bin")   | Out-Null

# Copy tool executables
foreach ($exe in $ToolExes) {
    $src = Join-Path $BuildPath $exe
    if (Test-Path $src) {
        Copy-Item $src (Join-Path (Join-Path $StageDir "bin") $exe)
        Write-Host "  [+] bin/$exe"
    } else {
        Write-Warning "  [!] $exe not found in build — skipping"
    }
}

# Copy polyui if built
$PolyuiExe = Join-Path $BuildPath "polyui.exe"
if (Test-Path $PolyuiExe) {
    Copy-Item $PolyuiExe (Join-Path (Join-Path $StageDir "bin") "polyui.exe")
    Write-Host "  [+] bin/polyui.exe"
} else {
    Write-Warning "  [!] polyui.exe not found — Qt may not have been available during build"
}

# Copy top-level files (README and LICENSE only, docs excluded)
$TopFiles = @("README.md", "LICENSE")
foreach ($f in $TopFiles) {
    $src = Join-Path $ProjectRoot $f
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $StageDir $f)
    }
}

# ============================================================================
# Step 3 — Run windeployqt on polyui
# ============================================================================
$StagedPolyui = Join-Path (Join-Path $StageDir "bin") "polyui.exe"
if (Test-Path $StagedPolyui) {
    Write-Step "Running windeployqt"
    & $WinDeployQt --release --no-translations --no-system-d3d-compiler `
        --no-opengl-sw --dir (Join-Path $StageDir "bin") $StagedPolyui
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "windeployqt returned non-zero — Qt DLLs may be incomplete"
    }
    Write-Host "[OK] Qt runtime dependencies deployed"
} else {
    Write-Host "[SKIP] polyui not present — skipping windeployqt"
}

# ============================================================================
# Step 4 — Create portable ZIP
# ============================================================================
Write-Step "Creating portable ZIP archive"

$ZipPath = Join-Path (Join-Path $ProjectRoot $OutputDir) "${ArchiveBase}-portable.zip"
if (Test-Path $ZipPath) { Remove-Item $ZipPath }

# Use .NET compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory(
    (Join-Path (Join-Path $ProjectRoot $OutputDir) "stage"),
    $ZipPath,
    [System.IO.Compression.CompressionLevel]::Optimal,
    $true  # include base directory name
)
Write-Host "[OK] Portable ZIP: $ZipPath"

# ============================================================================
# Step 5 — Build NSIS installer
# ============================================================================
if (-not $SkipInstaller) {
    Write-Step "Building NSIS installer"

    $makensis = Get-Command "makensis" -ErrorAction SilentlyContinue
    if (-not $makensis) {
        # Try common installation paths
        $NsisPaths = @(
            "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
            "${env:ProgramFiles}\NSIS\makensis.exe"
        )
        foreach ($p in $NsisPaths) {
            if (Test-Path $p) { $makensis = $p; break }
        }
    } else {
        $makensis = $makensis.Source
    }

    if (-not $makensis -or -not (Test-Path $makensis)) {
        Write-Warning "NSIS (makensis) not found — skipping installer generation."
        Write-Warning "Install NSIS from https://nsis.sourceforge.io/ and re-run."
    } else {
        if (-not (Test-Path $NsisScript)) {
            Write-Error "NSIS script not found: $NsisScript"
            exit 1
        }

        $InstallerPath = Join-Path (Join-Path $ProjectRoot $OutputDir) "${ArchiveBase}-setup.exe"

        & $makensis /V2 `
            /DPRODUCT_VERSION="$Version" `
            /DSTAGE_DIR="$StageDir" `
            /DOUTPUT_FILE="$InstallerPath" `
            "$NsisScript"

        if ($LASTEXITCODE -ne 0) {
            Write-Error "NSIS installer build failed"
            exit 1
        }
        Write-Host "[OK] Installer: $InstallerPath"
    }
} else {
    Write-Host "[SKIP] Installer step skipped (--SkipInstaller)"
}

# ============================================================================
# Done
# ============================================================================
Write-Step "Packaging complete"
Write-Host ""
Write-Host "Artifacts in: $(Join-Path $ProjectRoot $OutputDir)"
Get-ChildItem (Join-Path $ProjectRoot $OutputDir) -File | ForEach-Object {
    $sizeMB = [math]::Round($_.Length / 1MB, 2)
    Write-Host "  $($_.Name)  ($sizeMB MB)"
}
Write-Host ""
