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
POLYLD="$TOOLCHAIN_DIR/polyld"
if [[ ! -x "$POLYC" ]]; then
  echo "Expected executable local compiler at $POLYC" >&2
  exit 1
fi
if [[ ! -x "$POLYLD" ]]; then
  echo "Expected executable local linker at $POLYLD" >&2
  exit 1
fi

BUILD_DIR="$EXAMPLE_DIR/build/polyc"
mkdir -p "$BUILD_DIR"

# PATH is intentionally empty during compilation. The absolute local polyc
# recursively runs its built-in language frontends and the explicit local
# polyld; an accidental system-tool fallback therefore fails immediately.
PATH=/nonexistent "$POLYC" --strict --no-package-index --quiet \
  --polyld="$POLYLD" -O0 \
  -o "$BUILD_DIR/house_price_ml" "$EXAMPLE_DIR/house_price_ml.poly"

if [[ ! -x "$BUILD_DIR/house_price_ml" ]]; then
  echo "polyld did not produce an executable: $BUILD_DIR/house_price_ml" >&2
  exit 1
fi

set +e
ulimit -t 10 2>/dev/null || true
"$BUILD_DIR/house_price_ml"
APP_RC=$?
set -e

# main derives this from learned slope=3, intercept=7, and holdout MAE=1.
if [[ "$APP_RC" -ne 142 ]]; then
  echo "unexpected ML audit code: expected 142, got $APP_RC" >&2
  exit 1
fi

echo "house_price_ml: trained slope=3 intercept=7 holdout_mae=1 audit=$APP_RC"
