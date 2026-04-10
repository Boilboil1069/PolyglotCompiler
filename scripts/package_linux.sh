#!/usr/bin/env bash
# ===========================================================================
# PolyglotCompiler — Linux Release Packaging Script
#
# Builds the project in Release mode and produces a portable tar.gz archive
# containing all tool binaries, documentation, and (optionally) Qt runtime
# libraries for polyui.
#
# Usage:
#   ./scripts/package_linux.sh [--skip-build] [--qt-root /path/to/qt]
#
# Requirements:
#   - CMake >= 3.20, Ninja (or Make)
#   - GCC or Clang with C++20 support
#   - Qt6 (optional, only needed for polyui)
#   - tar, gzip
# ===========================================================================

set -euo pipefail

# ============================================================================
# Constants
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION="$(sed -n '1p' "$PROJECT_ROOT/VERSION.txt")"
PRODUCT_NAME="$(sed -n '2p' "$PROJECT_ROOT/VERSION.txt")"
ARCH="$(uname -m)"
ARCHIVE_BASE="${PRODUCT_NAME}-${VERSION}-linux-${ARCH}"
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
    BUILD_CMD="ninja"
else
    GENERATOR="Unix Makefiles"
    BUILD_CMD="make"
fi

echo "[OK] Generator : $GENERATOR"
echo "[OK] Arch      : $ARCH"

# Determine Qt cmake args if Qt root is specified
QT_CMAKE_ARGS=""
if [[ -n "$QT_ROOT" ]]; then
    # Find the newest Qt version directory
    QT_VER_DIR=$(find "$QT_ROOT" -maxdepth 1 -type d -name '[0-9]*' | sort -V | tail -1)
    if [[ -n "$QT_VER_DIR" ]]; then
        # Find gcc_64 or similar kit
        QT_KIT_DIR=$(find "$QT_VER_DIR" -maxdepth 1 -type d -name 'gcc_64' | head -1)
        if [[ -z "$QT_KIT_DIR" ]]; then
            QT_KIT_DIR=$(find "$QT_VER_DIR" -maxdepth 1 -type d | grep -v "^$QT_VER_DIR$" | head -1)
        fi
        if [[ -n "$QT_KIT_DIR" ]]; then
            QT_CMAKE_ARGS="-DCMAKE_PREFIX_PATH=${QT_KIT_DIR} -DQT_ROOT=${QT_ROOT}"
            echo "[OK] Qt prefix : $QT_KIT_DIR"
        fi
    fi
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
    cmake --build "$BUILD_DIR" --config Release -- -j"$(nproc)"
else
    echo "[SKIP] Build step skipped (--skip-build)"
fi

BUILD_PATH="${PROJECT_ROOT}/${BUILD_DIR}"

# ============================================================================
# Step 2 — Stage portable directory
# ============================================================================
step "Staging portable distribution"

STAGE_DIR="${PROJECT_ROOT}/${OUTPUT_DIR}/stage/${ARCHIVE_BASE}"
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

# Copy project shared libraries (.so) produced by the build.
# These are the non-Qt libraries that our executables depend on.
for so in "$BUILD_PATH"/lib*.so*; do
    if [[ -f "$so" ]]; then
        soname="$(basename "$so")"
        # Skip Qt/system libraries — they are handled separately
        if [[ "$soname" =~ ^libQt|^libicu ]]; then
            continue
        fi
        cp "$so" "$STAGE_DIR/lib/"
        echo "  [+] lib/$soname"
    fi
done

# Copy polyui if built
if [[ -f "${BUILD_PATH}/polyui" ]]; then
    cp "${BUILD_PATH}/polyui" "$STAGE_DIR/bin/"
    chmod +x "$STAGE_DIR/bin/polyui"
    echo "  [+] bin/polyui"

    # Bundle essential Qt libraries alongside polyui
    # Use ldd to find Qt shared objects and copy them
    if command -v ldd &>/dev/null; then
        QT_LIBS=$(ldd "$STAGE_DIR/bin/polyui" 2>/dev/null | \
            grep -oP '/\S+libQt[56]\S+\.so\S*' | sort -u || true)
        for lib in $QT_LIBS; do
            if [[ -f "$lib" ]]; then
                cp "$lib" "$STAGE_DIR/lib/"
                echo "  [+] lib/$(basename "$lib")"
            fi
        done
    fi

    # Copy Qt platform plugins if available
    if [[ -n "$QT_CMAKE_ARGS" && -n "${QT_KIT_DIR:-}" ]]; then
        PLUGINS_SRC="${QT_KIT_DIR}/plugins/platforms"
        if [[ -d "$PLUGINS_SRC" ]]; then
            mkdir -p "$STAGE_DIR/plugins/platforms"
            cp "$PLUGINS_SRC"/libqxcb.so "$STAGE_DIR/plugins/platforms/" 2>/dev/null || true
            echo "  [+] plugins/platforms/libqxcb.so"
        fi
    fi

    # Create a wrapper script that sets library and plugin paths
    cat > "$STAGE_DIR/bin/polyui.sh" << 'WRAPPER'
#!/usr/bin/env bash
# Wrapper script for polyui — sets up Qt library and plugin paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
export LD_LIBRARY_PATH="${DIST_ROOT}/lib:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="${DIST_ROOT}/plugins:${QT_PLUGIN_PATH:-}"
exec "$SCRIPT_DIR/polyui" "$@"
WRAPPER
    chmod +x "$STAGE_DIR/bin/polyui.sh"
    echo "  [+] bin/polyui.sh (wrapper)"
else
    echo "  [!] polyui not found — Qt may not have been available during build"
fi

# Copy top-level files (README and LICENSE only, docs excluded)
for f in README.md LICENSE; do
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

cd "${PROJECT_ROOT}/${OUTPUT_DIR}/stage"
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
