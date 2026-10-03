#!/usr/bin/env bash
set -euo pipefail
EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$EXAMPLE_DIR/../.." && pwd)"
REQUESTED_TOOLCHAIN="${POLYGLOT_BUILD_DIR:-$REPO_ROOT/build-release}"
if [[ "$REQUESTED_TOOLCHAIN" != /* ]]; then REQUESTED_TOOLCHAIN="$REPO_ROOT/$REQUESTED_TOOLCHAIN"; fi
if [[ ! -d "$REQUESTED_TOOLCHAIN" ]]; then
  echo "Build the repository first; missing $REQUESTED_TOOLCHAIN" >&2
  exit 1
fi
TOOLCHAIN_DIR="$(cd "$REQUESTED_TOOLCHAIN" && pwd)"
case "$TOOLCHAIN_DIR" in "$REPO_ROOT"/*) ;; *) echo "Expected a repository-local toolchain" >&2; exit 1 ;; esac
POLYC="$TOOLCHAIN_DIR/polyc"
if [[ ! -x "$POLYC" ]]; then echo "Missing executable: $POLYC" >&2; exit 1; fi
BUILD_DIR="$EXAMPLE_DIR/build/polyc"
mkdir -p "$BUILD_DIR"
# Imports and package manifests drive all four module builds and polyld.
"$POLYC" --strict --no-package-index --quiet "-O${POLY_OPT_LEVEL:-0}" \
  "--regalloc=${POLY_REGALLOC:-linear}" "--build-report=$BUILD_DIR/build-report.json" \
  -o "$BUILD_DIR/order_risk" "$EXAMPLE_DIR/order_risk.poly" > "$BUILD_DIR/compile.log" 2>&1 || {
    cat "$BUILD_DIR/compile.log" >&2
    exit 1
  }
cd "$EXAMPLE_DIR"
"$BUILD_DIR/order_risk" "${1:-data/orders.csv}" "${2:-order-results.txt}"
