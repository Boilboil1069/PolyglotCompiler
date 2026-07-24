#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
ARTIFACT_DIR="$EXAMPLE_DIR/artifacts"

"$EXAMPLE_DIR/run.sh"

cmp -s "$EXAMPLE_DIR/expected_model.pmodel" "$ARTIFACT_DIR/iris_model.pmodel"
cmp -s "$EXAMPLE_DIR/expected_metrics.csv" "$ARTIFACT_DIR/metrics.csv"
cmp -s "$EXAMPLE_DIR/expected_training_trace.csv" "$ARTIFACT_DIR/training_trace.csv"
cmp -s "$EXAMPLE_DIR/expected_evaluation_trace.csv" "$ARTIFACT_DIR/evaluation_trace.csv"

for object in cpp_fixed_statistics.o python_data_contract.o \
              rust_minibatch_regression.o go_classifiers.o; do
  if [[ ! -s "$EXAMPLE_DIR/aux/$object" ]]; then
    echo "missing built-in frontend object: $object" >&2
    exit 1
  fi
done

ROW_COUNT="$(tail -n +2 "$EXAMPLE_DIR/data/iris_q10.csv" | wc -l | tr -d ' ')"
if [[ "$ROW_COUNT" != "150" ]]; then
  echo "expected 150 generated Iris rows, got $ROW_COUNT" >&2
  exit 1
fi

EXPECTED_SHA="0fed2a99db77ec533a62dc66894d3ec6df3b58b6a8f3cf4a6b47e4086b7f97dc"
if command -v shasum >/dev/null 2>&1; then
  ACTUAL_SHA="$(shasum -a 256 "$EXAMPLE_DIR/data/source/bezdekIris.data" | awk '{print $1}')"
else
  ACTUAL_SHA="$(sha256sum "$EXAMPLE_DIR/data/source/bezdekIris.data" | awk '{print $1}')"
fi
if [[ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]]; then
  echo "unexpected source-data SHA-256: $ACTUAL_SHA" >&2
  exit 1
fi

SCATTER_POINTS="$(grep -o "r='3.2'" "$ARTIFACT_DIR/iris_dashboard.svg" | wc -l | tr -d ' ')"
if [[ "$SCATTER_POINTS" != "150" ]]; then
  echo "expected 150 SVG data points, got $SCATTER_POINTS" >&2
  exit 1
fi
grep -q "weights = \[75, -64, 148\]" "$ARTIFACT_DIR/iris_dashboard.svg"
grep -q "Tree 30 / 30" "$ARTIFACT_DIR/iris_dashboard.svg"
grep -q "Centroid 29 / 30" "$ARTIFACT_DIR/iris_dashboard.svg"

# A second build to the identical pathname is intentional: it guards the
# Darwin signed-vnode replacement regression in polyld.
"$EXAMPLE_DIR/run.sh" >/dev/null
cmp -s "$EXAMPLE_DIR/expected_model.pmodel" "$ARTIFACT_DIR/iris_model.pmodel"

echo "iris_native_ml tests: PASS"
