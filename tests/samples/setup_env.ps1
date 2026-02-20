# ============================================================================
# PolyglotCompiler Sample Environment Setup Script (Windows PowerShell)
#
# This script creates a Python virtual environment and a Rust toolchain
# inside tests/samples/env/ for running sample programs.
#
# Usage:
#   cd tests/samples
#   .\setup_env.ps1
#
# The env/ folder is git-ignored and not committed to the repository.
# ============================================================================

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$EnvDir = Join-Path $ScriptDir "env"

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " PolyglotCompiler Sample Environment Setup" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# ---------------------------------------------------------------------------
# 1. Create env/ directory
# ---------------------------------------------------------------------------
if (!(Test-Path $EnvDir)) {
    New-Item -ItemType Directory -Path $EnvDir | Out-Null
    Write-Host "[OK] Created env/ directory" -ForegroundColor Green
} else {
    Write-Host "[OK] env/ directory already exists" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 2. Python virtual environment
# ---------------------------------------------------------------------------
$PythonEnvDir = Join-Path $EnvDir "python"

Write-Host ""
Write-Host "--- Setting up Python environment ---" -ForegroundColor Cyan

if (!(Test-Path $PythonEnvDir)) {
    Write-Host "[..] Creating Python virtual environment..."

    # Try python, python3, py in order
    $PythonCmd = $null
    foreach ($cmd in @("python", "python3", "py")) {
        try {
            $ver = & $cmd --version 2>&1
            if ($LASTEXITCODE -eq 0) {
                $PythonCmd = $cmd
                Write-Host "[OK] Found Python: $ver" -ForegroundColor Green
                break
            }
        } catch {
            continue
        }
    }

    if ($null -eq $PythonCmd) {
        Write-Host "[ERR] Python not found. Please install Python 3.8+ and add to PATH." -ForegroundColor Red
        Write-Host "      Download: https://www.python.org/downloads/" -ForegroundColor Red
    } else {
        & $PythonCmd -m venv $PythonEnvDir 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "[OK] Python venv created at: $PythonEnvDir" -ForegroundColor Green

            # Use the venv's own python executable for pip operations
            $VenvPython = Join-Path $PythonEnvDir "Scripts\python.exe"

            Write-Host "[..] Installing sample dependencies..."
            # Upgrade pip using the venv's python -m pip (avoids "To modify pip" error)
            & $VenvPython -m pip install --upgrade pip 2>&1 | Out-Null
            & $VenvPython -m pip install numpy typing-extensions 2>&1 | Out-Null

            if ($LASTEXITCODE -eq 0) {
                Write-Host "[OK] Python packages installed" -ForegroundColor Green
            } else {
                Write-Host "[WARN] Some Python packages failed to install (non-critical)" -ForegroundColor Yellow
            }

            # # Optionally install PyTorch (large package, may take time)
            # Write-Host "[..] Installing PyTorch (this may take a while)..."
            # & $VenvPython -m pip install torch --index-url https://download.pytorch.org/whl/cpu 2>&1 | Out-Null
            # if ($LASTEXITCODE -eq 0) {
            #     Write-Host "[OK] PyTorch installed" -ForegroundColor Green
            # } else {
            #     Write-Host "[WARN] PyTorch install failed (non-critical, some samples may not work)" -ForegroundColor Yellow
            # }
        } else {
            Write-Host "[ERR] Failed to create Python venv" -ForegroundColor Red
        }
    }
} else {
    Write-Host "[OK] Python environment already exists at: $PythonEnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 3. Rust toolchain
# ---------------------------------------------------------------------------
$RustEnvDir = Join-Path $EnvDir "rust"

Write-Host ""
Write-Host "--- Setting up Rust environment ---" -ForegroundColor Cyan

if (!(Test-Path $RustEnvDir)) {
    New-Item -ItemType Directory -Path $RustEnvDir | Out-Null

    # Check if rustup is available
    $RustupAvailable = $false
    try {
        $rustupVer = & rustup --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            $RustupAvailable = $true
            Write-Host "[OK] Found rustup: $rustupVer" -ForegroundColor Green
        }
    } catch {}

    if ($RustupAvailable) {
        # Set RUSTUP_HOME and CARGO_HOME to isolate toolchain
        $env:RUSTUP_HOME = Join-Path $RustEnvDir "rustup"
        $env:CARGO_HOME = Join-Path $RustEnvDir "cargo"

        Write-Host "[..] Installing Rust stable toolchain (isolated)..."
        & rustup install stable 2>&1
        & rustup default stable 2>&1

        if ($LASTEXITCODE -eq 0) {
            Write-Host "[OK] Rust toolchain installed at: $RustEnvDir" -ForegroundColor Green
        } else {
            Write-Host "[WARN] Rust toolchain install had issues" -ForegroundColor Yellow
        }

        # Create activation helper
        $ActivateRust = Join-Path $RustEnvDir "activate.ps1"
        @"
# Activate isolated Rust environment
`$env:RUSTUP_HOME = "$($RustEnvDir)\rustup"
`$env:CARGO_HOME = "$($RustEnvDir)\cargo"
`$env:PATH = "$($RustEnvDir)\cargo\bin;`$env:PATH"
Write-Host "[Rust] Environment activated" -ForegroundColor Green
"@ | Out-File -Encoding utf8 $ActivateRust

        Write-Host "[OK] Rust activation script: $ActivateRust" -ForegroundColor Green

        # Reset env vars
        Remove-Item Env:\RUSTUP_HOME -ErrorAction SilentlyContinue
        Remove-Item Env:\CARGO_HOME -ErrorAction SilentlyContinue
    } else {
        Write-Host "[WARN] rustup not found. Please install Rust:" -ForegroundColor Yellow
        Write-Host "       https://rustup.rs/" -ForegroundColor Yellow
        Write-Host "       After installing, re-run this script." -ForegroundColor Yellow

        # Create placeholder readme
        @"
Rust environment not set up.
Install rustup from https://rustup.rs/ and re-run setup_env.ps1
"@ | Out-File -Encoding utf8 (Join-Path $RustEnvDir "README.txt")
    }
} else {
    Write-Host "[OK] Rust environment already exists at: $RustEnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 4. Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Setup Complete!" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Python venv:  $PythonEnvDir"
Write-Host "Rust toolchain: $RustEnvDir"
Write-Host ""
Write-Host "To activate Python:  & $PythonEnvDir\Scripts\Activate.ps1"
Write-Host "To activate Rust:    . $RustEnvDir\activate.ps1"
Write-Host ""
Write-Host "Note: The env/ folder is git-ignored." -ForegroundColor DarkGray
