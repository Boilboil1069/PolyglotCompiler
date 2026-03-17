#!/usr/bin/env bash
# setup_qt.sh — Download pre-built Qt 6 binaries for macOS/Linux using aqtinstall.
#
# Usage:
#   ./tools/ui/setup_qt.sh            # auto-detect platform
#   ./tools/ui/setup_qt.sh 6.10.2     # specify Qt version
#
# Qt is installed into deps/qt/ under the project root.
# This directory is already in .gitignore.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
QT_DIR="$PROJECT_ROOT/deps/qt"
QT_VERSION="${1:-6.10.2}"

echo "============================================"
echo " PolyglotCompiler — Qt Setup"
echo "============================================"
echo ""
echo "Qt version : $QT_VERSION"
echo "Install to : $QT_DIR"
echo ""

# Detect platform and architecture
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Darwin)
        PLATFORM="mac"
        QT_ARCH="clang_64"
        ;;
    Linux)
        PLATFORM="linux"
        QT_ARCH="gcc_64"
        ;;
    *)
        echo "[ERROR] Unsupported platform: $OS"
        echo "        On Windows, use setup_qt.ps1 instead."
        exit 1
        ;;
esac

echo "Platform   : $PLATFORM ($ARCH)"
echo "Qt arch    : $QT_ARCH"
echo ""

# Check if Qt is already installed
if [ -d "$QT_DIR/$QT_VERSION" ]; then
    echo "[OK] Qt $QT_VERSION is already installed at $QT_DIR/$QT_VERSION"
    echo "     Delete $QT_DIR/$QT_VERSION and re-run to reinstall."
    exit 0
fi

# Ensure python3 and pip are available
if ! command -v python3 &>/dev/null; then
    echo "[ERROR] python3 not found. Please install Python 3.7+ first."
    exit 1
fi

# Install aqtinstall if not available
if ! python3 -m aqt version &>/dev/null; then
    echo "[..] Installing aqtinstall..."
    python3 -m pip install --user aqtinstall 2>&1 | tail -3
    echo "[OK] aqtinstall installed"
fi

# Find the aqt command
AQT=""
if command -v aqt &>/dev/null; then
    AQT="aqt"
else
    # aqt may be installed in user site-packages bin
    USER_BIN="$(python3 -m site --user-base)/bin"
    if [ -x "$USER_BIN/aqt" ]; then
        AQT="$USER_BIN/aqt"
    else
        # Fall back to module invocation
        AQT="python3 -m aqt"
    fi
fi

echo "[..] Downloading Qt $QT_VERSION for $PLATFORM ($QT_ARCH)..."
mkdir -p "$QT_DIR"
(cd "$QT_DIR" && $AQT install-qt "$PLATFORM" desktop "$QT_VERSION" "$QT_ARCH")

if [ -d "$QT_DIR/$QT_VERSION" ]; then
    echo ""
    echo "[OK] Qt $QT_VERSION installed successfully at:"
    echo "     $QT_DIR/$QT_VERSION"
    echo ""
    echo "To build polyui, reconfigure CMake:"
    echo "  cd build && cmake .."
    echo "  cmake --build . --target polyui"
else
    echo ""
    echo "[ERROR] Qt installation failed."
    exit 1
fi
