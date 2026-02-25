# ============================================================================
# PolyglotCompiler Sample Environment Cleanup Script (Windows PowerShell)
#
# This script removes isolated environments created by setup_env.ps1:
# - tests/samples/env/
# - per-sample env junction/directory or env_path.txt files
#
# Usage:
#   cd tests/samples
#   .\remove_env.ps1
#   .\remove_env.ps1 -Force
#
# The script only operates inside tests/samples to avoid accidental deletion.
# ============================================================================

param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$EnvDir = Join-Path $ScriptDir "env"

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " PolyglotCompiler Sample Environment Cleanup" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# ---------------------------------------------------------------------------
# 1. Safety guard
# ---------------------------------------------------------------------------
$NormalizedEnvDir = ($EnvDir -replace "\\", "/")
if ($NormalizedEnvDir -notmatch "/tests/samples/env$") {
    Write-Host "[ERR] Safety check failed. Refusing to operate on: $EnvDir" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# 2. Confirmation
# ---------------------------------------------------------------------------
if (-not $Force) {
    Write-Host "This will remove:"
    Write-Host "  - $EnvDir"
    Write-Host "  - env junction/directory (or env_path.txt) under each sample folder"
    Write-Host ""

    $confirm = Read-Host "Continue? [y/N]"
    if ($confirm -notin @("y", "Y", "yes", "YES")) {
        Write-Host "[OK] Cancelled" -ForegroundColor Yellow
        exit 0
    }
}

# ---------------------------------------------------------------------------
# 3. Remove per-sample links/files
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- Cleaning sample directory links ---" -ForegroundColor Cyan

$SampleDirs = Get-ChildItem -Path $ScriptDir -Directory | Where-Object {
    $_.Name -match "^\d+_"
}

foreach ($dir in $SampleDirs) {
    $envLink = Join-Path $dir.FullName "env"
    $envPathFile = Join-Path $dir.FullName "env_path.txt"

    if (Test-Path $envLink) {
        try {
            Remove-Item $envLink -Recurse -Force -ErrorAction Stop
            Write-Host "[OK] Removed: $($dir.Name)/env" -ForegroundColor Green
        } catch {
            Write-Host "[WARN] Failed to remove $($dir.Name)/env: $($_.Exception.Message)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[OK] No env link in: $($dir.Name)" -ForegroundColor DarkGray
    }

    if (Test-Path $envPathFile) {
        Remove-Item $envPathFile -Force -ErrorAction SilentlyContinue
        Write-Host "[OK] Removed: $($dir.Name)/env_path.txt" -ForegroundColor Green
    }
}

# ---------------------------------------------------------------------------
# 4. Remove env directory
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- Removing env directory ---" -ForegroundColor Cyan

if (Test-Path $EnvDir) {
    Remove-Item $EnvDir -Recurse -Force
    Write-Host "[OK] Removed: $EnvDir" -ForegroundColor Green
} else {
    Write-Host "[OK] env directory does not exist: $EnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 5. Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Cleanup Complete!" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Removed environment root: $EnvDir"
Write-Host "Removed sample links/files under: $ScriptDir\\[0-9]*_*\\"
