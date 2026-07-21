#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$EXAMPLE_DIR/build"
CXX_BIN="${CXX:-c++}"

mkdir -p "$BUILD_DIR"
case "$(uname -s)" in
  Darwin) LIB_NAME="librisk_kernel.dylib"; SHARED_FLAGS=(-dynamiclib) ;;
  Linux)  LIB_NAME="librisk_kernel.so";    SHARED_FLAGS=(-shared -fPIC) ;;
  *) echo "Unsupported host; use CMake on Windows." >&2; exit 2 ;;
esac

"$CXX_BIN" -std=c++17 -O2 -Wall -Wextra -Werror "${SHARED_FLAGS[@]}" \
  "$EXAMPLE_DIR/cpp/risk_kernel.cpp" -o "$BUILD_DIR/$LIB_NAME"

PYTHONDONTWRITEBYTECODE=1 python3 "$EXAMPLE_DIR/python/main.py" "$@"
