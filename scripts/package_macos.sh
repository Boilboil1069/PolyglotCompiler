#!/usr/bin/env bash
# ===========================================================================
# PolyglotCompiler — macOS Release Packaging Script
#
# Builds the project in Release mode, runs macdeployqt on polyui.app, and
# produces a portable tar.gz archive.
#
# Usage:
#   ./scripts/package_macos.sh [--skip-build] [--qt-root /path/to/qt]
#
# Requirements:
#   - CMake >= 3.20, Ninja (or Make)
#   - Xcode Command Line Tools (Apple Clang with C++20)
#   - Qt6 (optional, only needed for polyui)
#   - tar, gzip
# ===========================================================================

set -euo pipefail

# ============================================================================
# Constants
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION="1.0.0"
PRODUCT_NAME="PolyglotCompiler"
ARCH="$(uname -m)"  # arm64 or x86_64
ARCHIVE_BASE="${PRODUCT_NAME}-${VERSION}-macos-${ARCH}"
BUILD_DIR="build-release"
OUTPUT_DIR="dist"

SKIP_BUILD=false
QT_ROOT=""

# Tool executables produced by the build
TOOL_EXES=(polyc polyld polyasm polyopt polyrt polybench)

# ============================================================================
# Argument parsing
# ============================================================================
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --qt-root)
            QT_ROOT="$2"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--skip-build] [--qt-root <path>] [--build-dir <dir>] [--output-dir <dir>]"
            exit 1
            ;;
    esac
done

# ============================================================================
# Helper Functions
# ============================================================================
step() {
    echo ""
    echo "========================================"
    echo " $1"
    echo "========================================"
}

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: Required command not found: $1"
        exit 1
    fi
}

# ============================================================================
# Step 0 — Pre-flight checks
# ============================================================================
step "Pre-flight checks"

check_cmd cmake
check_cmd tar

# Prefer ninja if available, fall back to make
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi

echo "[OK] Generator : $GENERATOR"
echo "[OK] Arch      : $ARCH"

# Determine Qt cmake args
QT_CMAKE_ARGS=""
MACDEPLOYQT=""
if [[ -n "$QT_ROOT" ]]; then
    QT_VER_DIR=$(find "$QT_ROOT" -maxdepth 1 -type d -name '[0-9]*' 2>/dev/null | sort -V | tail -1)
    if [[ -n "$QT_VER_DIR" ]]; then
        QT_KIT_DIR=$(find "$QT_VER_DIR" -maxdepth 1 -type d -name 'macos' | head -1)
        if [[ -z "$QT_KIT_DIR" ]]; then
            QT_KIT_DIR=$(find "$QT_VER_DIR" -maxdepth 1 -type d -name 'clang_64' | head -1)
        fi
        if [[ -z "$QT_KIT_DIR" ]]; then
            QT_KIT_DIR=$(find "$QT_VER_DIR" -maxdepth 1 -type d | grep -v "^$QT_VER_DIR$" | head -1)
        fi
        if [[ -n "$QT_KIT_DIR" ]]; then
            QT_CMAKE_ARGS="-DCMAKE_PREFIX_PATH=${QT_KIT_DIR} -DQT_ROOT=${QT_ROOT}"
            echo "[OK] Qt prefix : $QT_KIT_DIR"

            # Locate macdeployqt
            if [[ -x "${QT_KIT_DIR}/bin/macdeployqt" ]]; then
                MACDEPLOYQT="${QT_KIT_DIR}/bin/macdeployqt"
            fi
        fi
    fi
fi

# Also check if macdeployqt is in PATH
if [[ -z "$MACDEPLOYQT" ]] && command -v macdeployqt &>/dev/null; then
    MACDEPLOYQT="$(command -v macdeployqt)"
fi

if [[ -n "$MACDEPLOYQT" ]]; then
    echo "[OK] macdeployqt: $MACDEPLOYQT"
fi

# ============================================================================
# Step 1 — Build in Release mode
# ============================================================================
if [[ "$SKIP_BUILD" == "false" ]]; then
    step "Configuring CMake (Release)"
    cd "$PROJECT_ROOT"

    cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        $QT_CMAKE_ARGS

    step "Building project"
    NPROC=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
    cmake --build "$BUILD_DIR" --config Release -- -j"$NPROC"
else
    echo "[SKIP] Build step skipped (--skip-build)"
fi

BUILD_PATH="${PROJECT_ROOT}/${BUILD_DIR}"

# ============================================================================
# Step 2 — Stage portable directory
# ============================================================================
step "Staging portable distribution"

STAGE_DIR="${PROJECT_ROOT}/${OUTPUT_DIR}/${ARCHIVE_BASE}"/${VERSION}
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/bin"
mkdir -p "$STAGE_DIR/lib"

# Copy tool executables
for exe in "${TOOL_EXES[@]}"; do
    src="${BUILD_PATH}/${exe}"
    if [[ -f "$src" ]]; then
        cp "$src" "$STAGE_DIR/bin/"
        chmod +x "$STAGE_DIR/bin/$exe"
        echo "  [+] bin/$exe"
    else
        echo "  [!] $exe not found in build — skipping"
    fi
done

# Copy project shared libraries (.dylib) produced by the build.
# These are the non-Qt libraries that our executables depend on.
for dylib in "$BUILD_PATH"/lib*.dylib; do
    if [[ -f "$dylib" ]]; then
        libname="$(basename "$dylib")"
        # Skip Qt/system libraries — they are handled by macdeployqt
        if [[ "$libname" =~ ^libQt|^libicu ]]; then
            continue
        fi
        cp "$dylib" "$STAGE_DIR/lib/"
        echo "  [+] lib/$libname"
    fi
done

# Copy polyui.app bundle if built
POLYUI_APP="${BUILD_PATH}/polyui.app"
if [[ -d "$POLYUI_APP" ]]; then
    # Run macdeployqt to bundle Qt frameworks
    if [[ -n "$MACDEPLOYQT" ]]; then
        step "Running macdeployqt"
        "$MACDEPLOYQT" "$POLYUI_APP" -verbose=1 2>&1 || true
        echo "[OK] Qt frameworks bundled into polyui.app"
    fi

    cp -R "$POLYUI_APP" "$STAGE_DIR/"
    echo "  [+] polyui.app/"
elif [[ -f "${BUILD_PATH}/polyui" ]]; then
    # Non-bundle build — copy raw executable
    cp "${BUILD_PATH}/polyui" "$STAGE_DIR/bin/"
    chmod +x "$STAGE_DIR/bin/polyui"
    echo "  [+] bin/polyui (non-bundle)"
else
    echo "  [!] polyui not found — Qt may not have been available during build"
fi

# Copy top-level files (README and LICENSE only, docs excluded)
for f in LICENSE; do
    src="${PROJECT_ROOT}/${f}"
    if [[ -f "$src" ]]; then
        cp "$src" "$STAGE_DIR/"
    fi
done

# ============================================================================
# Step 3 — Create portable tar.gz
# ============================================================================
step "Creating portable tar.gz archive"

TARBALL="${PROJECT_ROOT}/${OUTPUT_DIR}/${ARCHIVE_BASE}-portable.tar.gz"
rm -f "$TARBALL"

cd "${PROJECT_ROOT}/${OUTPUT_DIR}"
tar czf "$TARBALL" "$ARCHIVE_BASE"

echo "[OK] Portable archive: $TARBALL"

# ============================================================================
# Done
# ============================================================================
step "Packaging complete"
echo ""
echo "Artifacts in: ${PROJECT_ROOT}/${OUTPUT_DIR}/"
ls -lh "${PROJECT_ROOT}/${OUTPUT_DIR}/"*.tar.gz 2>/dev/null || true
echo ""
