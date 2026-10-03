#!/usr/bin/env bash
set -euo pipefail
EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$EXAMPLE_DIR/../.." && pwd)"
TOOLCHAIN_DIR="${POLYGLOT_BUILD_DIR:-$REPO_ROOT/build-release}"
if [[ "$TOOLCHAIN_DIR" != /* ]]; then TOOLCHAIN_DIR="$REPO_ROOT/$TOOLCHAIN_DIR"; fi
TOOLCHAIN_DIR="$(cd "$TOOLCHAIN_DIR" && pwd)"
case "$TOOLCHAIN_DIR" in "$REPO_ROOT"/*) ;; *) echo "Expected a repository-local toolchain" >&2; exit 1 ;; esac
exec "${PYTHON:-python3}" "$REPO_ROOT/tests/native_programs/mixed_regression.py" \
  --polyc "$TOOLCHAIN_DIR/polyc" --output-root "$EXAMPLE_DIR/build/regression" "$@"
