# Release Packaging Guide

This document describes how to build release packages for PolyglotCompiler on each supported platform.

## Overview

The `scripts/` directory contains platform-specific packaging scripts that automate:

1. Building the project in **Release** mode
2. Staging binaries, Qt runtime dependencies, and documentation
3. Producing distributable archives

| Platform | Script | Output |
|----------|--------|--------|
| **Windows** | `scripts/package_windows.ps1` | Portable `.zip` + NSIS installer `.exe` |
| **Linux** | `scripts/package_linux.sh` | Portable `.tar.gz` |
| **macOS** | `scripts/package_macos.sh` | Portable `.tar.gz` (with `.app` bundle) |

## Prerequisites

### All Platforms
- CMake >= 3.20
- Ninja (recommended) or platform-native build tool
- C++20 compiler

### Windows
- MSVC 2022 (Visual Studio or Build Tools)
- Qt6 installed (default: `D:\Qt`)
- [NSIS 3.x](https://nsis.sourceforge.io/) — only needed for installer generation

### Linux
- GCC 12+ or Clang 15+ with C++20 support
- Qt6 development libraries (optional, for polyui)

### macOS
- Xcode Command Line Tools (Apple Clang)
- Qt6 (optional, for polyui)

## Usage

### Windows

Open a PowerShell terminal in the project root:

```powershell
# Full build + portable zip + NSIS installer
.\scripts\package_windows.ps1

# Custom Qt root
.\scripts\package_windows.ps1 -QtRoot "C:\Qt"

# Skip build (package an existing build)
.\scripts\package_windows.ps1 -SkipBuild

# Portable zip only (no NSIS installer)
.\scripts\package_windows.ps1 -SkipInstaller
```

**Output:**
```
dist/
├── PolyglotCompiler-1.0.0-windows-x64-portable.zip
└── PolyglotCompiler-1.0.0-windows-x64-setup.exe
```

### Linux

```bash
chmod +x scripts/package_linux.sh

# Full build + portable tar.gz
./scripts/package_linux.sh

# With custom Qt root
./scripts/package_linux.sh --qt-root /opt/Qt

# Skip build
./scripts/package_linux.sh --skip-build
```

**Output:**
```
dist/
└── PolyglotCompiler-1.0.0-linux-x86_64-portable.tar.gz
```

### macOS

```bash
chmod +x scripts/package_macos.sh

# Full build + portable tar.gz
./scripts/package_macos.sh

# With custom Qt root
./scripts/package_macos.sh --qt-root ~/Qt

# Skip build
./scripts/package_macos.sh --skip-build
```

**Output:**
```
dist/
└── PolyglotCompiler-1.0.0-macos-arm64-portable.tar.gz
```

## Package Contents

### Portable Archive (all platforms)

```
PolyglotCompiler-1.0.0-<platform>/
├── bin/
│   ├── polyc(.exe)         # Compiler
│   ├── polyld(.exe)        # Linker
│   ├── polyasm(.exe)       # Assembler
│   ├── polyopt(.exe)       # Optimizer
│   ├── polyrt(.exe)        # Runtime tool
│   ├── polybench(.exe)     # Benchmark tool
│   ├── polyui(.exe)        # IDE (if Qt was available)
│   └── (Qt DLLs/SOs)       # Qt runtime (Windows/Linux only)
├── docs/                   # Documentation
├── README.md
└── LICENSE
```

### Windows Installer

The NSIS installer provides:
- Standard Windows install/uninstall workflow
- Optional PATH registration (adds `bin/` to system PATH)
- Start Menu shortcuts for the IDE
- Uninstaller with clean registry removal

## NSIS Installer Script

The installer is defined in `scripts/installer.nsi`. It is invoked automatically by `package_windows.ps1` with the correct defines. To build manually:

```cmd
makensis /DPRODUCT_VERSION=1.0.0 ^
         /DSTAGE_DIR=dist\stage\PolyglotCompiler-1.0.0-windows-x64 ^
         /DOUTPUT_FILE=dist\PolyglotCompiler-1.0.0-windows-x64-setup.exe ^
         scripts\installer.nsi
```

## Version Management

The project version (`1.0.0`) is defined in:

| Location | Purpose |
|----------|---------|
| `CMakeLists.txt` (`project(... VERSION 1.0.0)`) | Build system version |
| `tools/ui/*/main.cpp` | IDE version display |
| `tools/polyc/src/driver.cpp` | Compiler banner |
| `tools/polyrt/src/polyrt.cpp` (`kVersion`) | Runtime tool version |
| `scripts/package_*.ps1/sh` | Packaging scripts |
| `scripts/installer.nsi` | Installer metadata |
| `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md` | Documentation header |
| `README.md` | Repository badge |

When bumping the version, update all locations above. The CMake `PROJECT_VERSION` variable is used by the macOS bundle metadata automatically.
