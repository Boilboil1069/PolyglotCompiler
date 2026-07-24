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
if [[ ! -x "$POLYC" || ! -x "$POLYLD" ]]; then
  echo "Expected local polyc and polyld in $TOOLCHAIN_DIR" >&2
  exit 1
fi

BUILD_DIR="$EXAMPLE_DIR/build/polyc"
AUX_DIR="$EXAMPLE_DIR/aux"
mkdir -p "$BUILD_DIR" "$AUX_DIR"

# Remove only this example's generated outputs so stale artifacts cannot make
# a failed rebuild look successful.
rm -f \
  "$BUILD_DIR/customer_retention" \
  "$AUX_DIR/python_telemetry_data.o" \
  "$AUX_DIR/java_SignalRules.o" \
  "$AUX_DIR/java_signal_rules.o" \
  "$AUX_DIR/javascript_retention_policy.o" \
  "$AUX_DIR/customer_retention_foreign_aliases.pobj" \
  "$AUX_DIR/customer_retention_link_descriptors.paux" \
  "$EXAMPLE_DIR/data/generated_accounts.js"

# The empty PATH makes accidental javac/node/python/system-compiler fallback
# impossible. The absolute local polyc recursively invokes only its built-in
# Java, JavaScript, and Python frontends plus the explicit local polyld.
PATH=/nonexistent "$POLYC" --strict --no-package-index --quiet \
  --polyld="$POLYLD" -O0 \
  -o "$BUILD_DIR/customer_retention" "$EXAMPLE_DIR/customer_retention.poly"

if [[ ! -x "$BUILD_DIR/customer_retention" ]]; then
  echo "polyld did not produce an executable: $BUILD_DIR/customer_retention" >&2
  exit 1
fi

set +e
ulimit -t 10 2>/dev/null || true
(
  cd "$EXAMPLE_DIR"
  "$BUILD_DIR/customer_retention"
)
APP_RC=$?
set -e

if [[ "$APP_RC" -ne 45 ]]; then
  echo "unexpected retention checksum: expected 45, got $APP_RC" >&2
  exit 1
fi
if [[ ! -s "$EXAMPLE_DIR/data/generated_accounts.js" ]]; then
  echo "cross-language program did not generate browser data" >&2
  exit 1
fi

echo "customer_retention: rows=5 healthy=1 watch=1 risk=3 checksum=$APP_RC"
echo "customer_retention: frontend=index.html data=data/generated_accounts.js"
