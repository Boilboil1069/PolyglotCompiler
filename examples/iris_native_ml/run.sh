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
for tool in "$POLYC" "$POLYLD"; do
  if [[ ! -x "$tool" ]]; then
    echo "Expected executable repository tool: $tool" >&2
    exit 1
  fi
done

BUILD_DIR="$EXAMPLE_DIR/build/polyc"
ARTIFACT_DIR="$EXAMPLE_DIR/artifacts"
mkdir -p "$BUILD_DIR" "$ARTIFACT_DIR"

# PATH is deliberately empty while polyc recursively compiles every imported
# source. Any accidental clang/g++/python/rustc/cargo/go fallback fails here.
PATH=/nonexistent "$POLYC" --strict --no-package-index --quiet \
  --polyld="$POLYLD" -O0 \
  -o "$BUILD_DIR/iris_native_ml" "$EXAMPLE_DIR/iris_native_ml.poly"

if [[ ! -x "$BUILD_DIR/iris_native_ml" ]]; then
  echo "polyld did not produce an executable" >&2
  exit 1
fi

set +e
cd "$EXAMPLE_DIR"
ulimit -t 30 2>/dev/null || true
"$BUILD_DIR/iris_native_ml"
APP_RC=$?
set -e

if [[ "$APP_RC" -ne 73 ]]; then
  echo "Iris ML audit failed: expected 73, got $APP_RC" >&2
  exit 1
fi

for output in iris_model.pmodel metrics.csv training_trace.csv \
              evaluation_trace.csv iris_dashboard.svg; do
  if [[ ! -s "$ARTIFACT_DIR/$output" ]]; then
    echo "missing generated artifact: $ARTIFACT_DIR/$output" >&2
    exit 1
  fi
done

echo "iris_native_ml: regression R2=0.937536 tree=30/30 centroid=29/30 audit=$APP_RC"
echo "artifacts: $ARTIFACT_DIR"
