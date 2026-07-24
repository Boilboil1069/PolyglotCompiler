#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$EXAMPLE_DIR/../.." && pwd)"
REQUESTED_TOOLCHAIN="${POLYGLOT_BUILD_DIR:-$REPO_ROOT/build}"

if [[ "$REQUESTED_TOOLCHAIN" != /* ]]; then
  REQUESTED_TOOLCHAIN="$REPO_ROOT/$REQUESTED_TOOLCHAIN"
fi
if [[ ! -d "$REQUESTED_TOOLCHAIN" ]]; then
  echo "PolyglotCompiler build directory not found: $REQUESTED_TOOLCHAIN" >&2
  echo "Build the repository first, or set POLYGLOT_BUILD_DIR to a local build directory." >&2
  exit 1
fi

TOOLCHAIN_DIR="$(cd "$REQUESTED_TOOLCHAIN" && pwd)"
case "$TOOLCHAIN_DIR" in
  "$REPO_ROOT"/*) ;;
  *)
    echo "Refusing non-local toolchain directory: $TOOLCHAIN_DIR" >&2
    exit 1
    ;;
esac

POLYC="$TOOLCHAIN_DIR/polyc"
if [[ ! -x "$POLYC" ]]; then
  echo "Expected executable local compiler at $POLYC" >&2
  exit 1
fi

BUILD_DIR="$EXAMPLE_DIR/build/polyc"
mkdir -p "$BUILD_DIR"

# This is deliberately the only build command.  Imports in order_risk.poly
# drive source discovery, per-language frontend compilation, alias generation,
# and the final local polyld link.
"$POLYC" --strict --no-package-index -O0 \
  -o "$BUILD_DIR/order_risk" "$EXAMPLE_DIR/order_risk.poly"

if [[ ! -x "$BUILD_DIR/order_risk" ]]; then
  echo "polyld did not produce an executable: $BUILD_DIR/order_risk" >&2
  exit 1
fi

set +e
ulimit -t 10 2>/dev/null || true
(
  cd "$EXAMPLE_DIR"
  "$BUILD_DIR/order_risk"
)
STATUS=$?
set -e

# main streams eight CSV records and validates every cross-language decision.
# The decisions total 483; a successful POSIX exit is 483 - 251 = 232.
if [[ "$STATUS" -ne 232 ]]; then
  echo "unexpected data-driven audit code: expected 232, got $STATUS" >&2
  exit 1
fi

echo "order_risk_analyzer: rows=8 approved=3 review=2 fraud_reject=1 inventory_reject=2 oop_models=4 vendored_packages=4 checksum=483 exit=$STATUS"
