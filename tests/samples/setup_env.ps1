# ============================================================================
# PolyglotCompiler Sample Environment Setup Script (Windows PowerShell)
#
# This script creates isolated environments for Python, Rust, Java, and .NET
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
        } else {
            Write-Host "[ERR] Failed to create Python venv" -ForegroundColor Red
        }
    }
} else {
    Write-Host "[OK] Python environment already exists at: $PythonEnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 3. Rust toolchain (auto-install rustup if not found)
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

    if (!$RustupAvailable) {
        Write-Host "[..] rustup not found. Downloading and installing rustup-init..." -ForegroundColor Yellow
        $RustupInitUrl = "https://static.rust-lang.org/rustup/dist/x86_64-pc-windows-msvc/rustup-init.exe"
        $RustupInitPath = Join-Path $EnvDir "rustup-init.exe"
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $RustupInitUrl -OutFile $RustupInitPath -UseBasicParsing
            Write-Host "[OK] Downloaded rustup-init.exe" -ForegroundColor Green

            # Install rustup with default options, no modification to system PATH
            $env:RUSTUP_HOME = Join-Path $RustEnvDir "rustup"
            $env:CARGO_HOME = Join-Path $RustEnvDir "cargo"
            & $RustupInitPath -y --no-modify-path --default-toolchain stable 2>&1
            if ($LASTEXITCODE -eq 0) {
                $RustupAvailable = $true
                Write-Host "[OK] rustup installed successfully" -ForegroundColor Green
            } else {
                Write-Host "[ERR] rustup installation failed" -ForegroundColor Red
            }

            # Clean up installer
            Remove-Item $RustupInitPath -ErrorAction SilentlyContinue

            # Reset env vars for the isolated install block below
            Remove-Item Env:\RUSTUP_HOME -ErrorAction SilentlyContinue
            Remove-Item Env:\CARGO_HOME -ErrorAction SilentlyContinue
        } catch {
            Write-Host "[ERR] Failed to download rustup-init: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "      Manual install: https://rustup.rs/" -ForegroundColor Red
        }
    }

    if ($RustupAvailable) {
        # Set RUSTUP_HOME and CARGO_HOME to isolate toolchain
        $env:RUSTUP_HOME = Join-Path $RustEnvDir "rustup"
        $env:CARGO_HOME = Join-Path $RustEnvDir "cargo"

        # Add cargo bin to PATH so rustup can find itself
        $CargoBin = Join-Path $RustEnvDir "cargo\bin"
        if (Test-Path $CargoBin) {
            $env:PATH = "$CargoBin;$env:PATH"
        }

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
    }
} else {
    Write-Host "[OK] Rust environment already exists at: $RustEnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 4. Java environment (auto-detect or download Adoptium JDK)
# ---------------------------------------------------------------------------
$JavaEnvDir = Join-Path $EnvDir "java"

Write-Host ""
Write-Host "--- Setting up Java environment ---" -ForegroundColor Cyan

if (!(Test-Path $JavaEnvDir)) {
    New-Item -ItemType Directory -Path $JavaEnvDir | Out-Null

    # Check if java is already available on the system
    $JavaAvailable = $false
    $JavaHome = $null
    try {
        $javaVer = & java -version 2>&1
        if ($LASTEXITCODE -eq 0 -or $javaVer -match "version") {
            $JavaAvailable = $true
            Write-Host "[OK] Found system Java: $($javaVer[0])" -ForegroundColor Green
        }
    } catch {}

    if (!$JavaAvailable) {
        Write-Host "[..] Java not found. Downloading Adoptium JDK 21 (LTS)..." -ForegroundColor Yellow
        $JdkVersion = "21"
        $JdkUrl = "https://api.adoptium.net/v3/binary/latest/$JdkVersion/ga/windows/x64/jdk/hotspot/normal/eclipse?project=jdk"
        $JdkZipPath = Join-Path $EnvDir "jdk.zip"
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $JdkUrl -OutFile $JdkZipPath -UseBasicParsing
            Write-Host "[OK] Downloaded Adoptium JDK $JdkVersion" -ForegroundColor Green

            Write-Host "[..] Extracting JDK..."
            Expand-Archive -Path $JdkZipPath -DestinationPath $JavaEnvDir -Force
            Remove-Item $JdkZipPath -ErrorAction SilentlyContinue

            # Find the extracted JDK directory (typically jdk-21.x.x+y)
            $JdkSubDir = Get-ChildItem -Path $JavaEnvDir -Directory | Where-Object { $_.Name -match "^jdk" } | Select-Object -First 1
            if ($JdkSubDir) {
                $JavaHome = $JdkSubDir.FullName
                $JavaAvailable = $true
                Write-Host "[OK] JDK extracted to: $JavaHome" -ForegroundColor Green
            } else {
                Write-Host "[ERR] Could not find extracted JDK directory" -ForegroundColor Red
            }
        } catch {
            Write-Host "[ERR] Failed to download JDK: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "      Manual install: https://adoptium.net/" -ForegroundColor Red
        }
    } else {
        # Use the system Java; store a reference
        try {
            if ($env:JAVA_HOME -and (Test-Path $env:JAVA_HOME)) {
                $JavaHome = $env:JAVA_HOME
            } else {
                $JavaExe = (Get-Command java -ErrorAction SilentlyContinue).Source
                if ($JavaExe) {
                    $JavaHome = (Split-Path (Split-Path $JavaExe -Parent) -Parent)
                }
            }
        } catch {}
    }

    if ($JavaAvailable) {
        # Create activation helper
        $ActivateJava = Join-Path $JavaEnvDir "activate.ps1"
        $JavaHomePath = if ($JavaHome) { $JavaHome } else { "$JavaEnvDir\jdk" }
        @"
# Activate isolated Java environment
`$jdkDir = Get-ChildItem -Path "$JavaEnvDir" -Directory | Where-Object { `$_.Name -match "^jdk" } | Select-Object -First 1
if (`$jdkDir) {
    `$env:JAVA_HOME = `$jdkDir.FullName
    `$env:PATH = "`$(`$jdkDir.FullName)\bin;`$env:PATH"
    Write-Host "[Java] Environment activated: `$(`$jdkDir.FullName)" -ForegroundColor Green
} elseif ("$JavaHome" -ne "" -and (Test-Path "$JavaHome")) {
    `$env:JAVA_HOME = "$JavaHome"
    `$env:PATH = "$JavaHome\bin;`$env:PATH"
    Write-Host "[Java] Environment activated (system): $JavaHome" -ForegroundColor Green
} else {
    Write-Host "[Java] Warning: No JDK directory found" -ForegroundColor Yellow
}
"@ | Out-File -Encoding utf8 $ActivateJava
        Write-Host "[OK] Java activation script: $ActivateJava" -ForegroundColor Green
    }
} else {
    Write-Host "[OK] Java environment already exists at: $JavaEnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 5. .NET SDK environment (auto-detect or install via dotnet-install.ps1)
# ---------------------------------------------------------------------------
$DotnetEnvDir = Join-Path $EnvDir "dotnet"

Write-Host ""
Write-Host "--- Setting up .NET environment ---" -ForegroundColor Cyan

if (!(Test-Path $DotnetEnvDir)) {
    New-Item -ItemType Directory -Path $DotnetEnvDir | Out-Null

    # Check if dotnet is already available on the system
    $DotnetAvailable = $false
    try {
        $dotnetVer = & dotnet --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            $DotnetAvailable = $true
            Write-Host "[OK] Found system .NET SDK: $dotnetVer" -ForegroundColor Green
        }
    } catch {}

    if (!$DotnetAvailable) {
        Write-Host "[..] .NET SDK not found. Downloading .NET 9 SDK..." -ForegroundColor Yellow
        $DotnetInstallUrl = "https://dot.net/v1/dotnet-install.ps1"
        $DotnetInstallScript = Join-Path $EnvDir "dotnet-install.ps1"
        try {
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -Uri $DotnetInstallUrl -OutFile $DotnetInstallScript -UseBasicParsing
            Write-Host "[OK] Downloaded dotnet-install.ps1" -ForegroundColor Green

            Write-Host "[..] Installing .NET 9 SDK to isolated directory..."
            & $DotnetInstallScript -Channel 9.0 -InstallDir $DotnetEnvDir -NoPath 2>&1
            if ($LASTEXITCODE -eq 0 -or (Test-Path (Join-Path $DotnetEnvDir "dotnet.exe"))) {
                $DotnetAvailable = $true
                Write-Host "[OK] .NET SDK installed at: $DotnetEnvDir" -ForegroundColor Green
            } else {
                Write-Host "[ERR] .NET SDK installation failed" -ForegroundColor Red
            }

            # Clean up installer
            Remove-Item $DotnetInstallScript -ErrorAction SilentlyContinue
        } catch {
            Write-Host "[ERR] Failed to download dotnet-install.ps1: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "      Manual install: https://dotnet.microsoft.com/download" -ForegroundColor Red
        }
    } else {
        # System dotnet is available; store a reference
        try {
            $DotnetExe = (Get-Command dotnet -ErrorAction SilentlyContinue).Source
            if ($DotnetExe) {
                # Create a symlink-like reference file
                $DotnetExe | Out-File -Encoding utf8 (Join-Path $DotnetEnvDir "system_dotnet_path.txt")
            }
        } catch {}
    }

    if ($DotnetAvailable) {
        # Create activation helper
        $ActivateDotnet = Join-Path $DotnetEnvDir "activate.ps1"
        @"
# Activate isolated .NET environment
if (Test-Path "$DotnetEnvDir\dotnet.exe") {
    `$env:DOTNET_ROOT = "$DotnetEnvDir"
    `$env:PATH = "$DotnetEnvDir;`$env:PATH"
    `$env:DOTNET_CLI_TELEMETRY_OPTOUT = "1"
    Write-Host "[.NET] Environment activated (isolated): $DotnetEnvDir" -ForegroundColor Green
} else {
    # Fall back to system dotnet
    Write-Host "[.NET] Using system .NET SDK" -ForegroundColor Green
}
"@ | Out-File -Encoding utf8 $ActivateDotnet
        Write-Host "[OK] .NET activation script: $ActivateDotnet" -ForegroundColor Green
    }
} else {
    Write-Host "[OK] .NET environment already exists at: $DotnetEnvDir" -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 6. Link environments to sample directories
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "--- Linking environments to sample directories ---" -ForegroundColor Cyan

$SampleDirs = Get-ChildItem -Path $ScriptDir -Directory | Where-Object {
    $_.Name -match "^\d+_" -and $_.Name -ne "env"
}

foreach ($dir in $SampleDirs) {
    $EnvLink = Join-Path $dir.FullName "env"
    if (!(Test-Path $EnvLink)) {
        try {
            # Create a directory junction (does not require admin privileges)
            cmd /c "mklink /J `"$EnvLink`" `"$EnvDir`"" 2>&1 | Out-Null
            if (Test-Path $EnvLink) {
                Write-Host "[OK] Linked env/ -> $($dir.Name)/env" -ForegroundColor Green
            } else {
                # Fallback: create a text file pointing to env/
                "$EnvDir" | Out-File -Encoding utf8 (Join-Path $dir.FullName "env_path.txt")
                Write-Host "[OK] Created env_path.txt in $($dir.Name)/" -ForegroundColor Green
            }
        } catch {
            Write-Host "[WARN] Could not link env/ to $($dir.Name)/: $($_.Exception.Message)" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[OK] $($dir.Name)/env already linked" -ForegroundColor DarkGray
    }
}

# ---------------------------------------------------------------------------
# 7. Summary
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Setup Complete!" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Python venv:      $PythonEnvDir"
Write-Host "Rust toolchain:   $RustEnvDir"
Write-Host "Java JDK:         $JavaEnvDir"
Write-Host ".NET SDK:         $DotnetEnvDir"
Write-Host ""
Write-Host "To activate Python:  & $PythonEnvDir\Scripts\Activate.ps1"
Write-Host "To activate Rust:    . $RustEnvDir\activate.ps1"
Write-Host "To activate Java:    . $JavaEnvDir\activate.ps1"
Write-Host "To activate .NET:    . $DotnetEnvDir\activate.ps1"
Write-Host ""
Write-Host "Note: The env/ folder is git-ignored." -ForegroundColor DarkGray
