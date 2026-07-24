#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$EXAMPLE_DIR/build/polyc"
AUX_DIR="$EXAMPLE_DIR/aux"
mkdir -p "$BUILD_DIR"

"$EXAMPLE_DIR/run.sh" > "$BUILD_DIR/actual_output.txt"
diff -u "$EXAMPLE_DIR/expected_output.txt" "$BUILD_DIR/actual_output.txt"

TRAIN_ROWS="$(grep -c '^train,' "$EXAMPLE_DIR/data/generated_samples.csv")"
TEST_ROWS="$(grep -c '^test,' "$EXAMPLE_DIR/data/generated_samples.csv")"
if [[ "$TRAIN_ROWS" -ne 8 || "$TEST_ROWS" -ne 4 ]]; then
  echo "expected an 8/4 generated-data split, found $TRAIN_ROWS/$TEST_ROWS" >&2
  exit 1
fi

for artifact in \
  "$AUX_DIR/python_data_generator.o" \
  "$AUX_DIR/cpp_sufficient_statistics.o" \
  "$AUX_DIR/rust_linear_regression.o" \
  "$AUX_DIR/go_model_service.o" \
  "$AUX_DIR/house_price_ml_foreign_aliases.pobj" \
  "$AUX_DIR/house_price_ml_link_descriptors.paux" \
  "$BUILD_DIR/house_price_ml"; do
  if [[ ! -s "$artifact" ]]; then
    echo "missing or empty compiler artifact: $artifact" >&2
    exit 1
  fi
done

for descriptor_row in \
  "IMPORT python data_generator" \
  "IMPORT cpp sufficient_statistics" \
  "IMPORT rust linear_regression" \
  "IMPORT go model_service"; do
  grep -Fq "$descriptor_row" "$AUX_DIR/house_price_ml_link_descriptors.paux"
done

DESCRIPTOR_SYMBOL_COUNT="$(grep -c '^SYMBOL ' "$AUX_DIR/house_price_ml_link_descriptors.paux")"
if [[ "$DESCRIPTOR_SYMBOL_COUNT" -ne 12 ]]; then
  echo "expected 12 packaged foreign symbols, found $DESCRIPTOR_SYMBOL_COUNT" >&2
  exit 1
fi

CALL_COUNT="$(grep -o 'CALL(' "$EXAMPLE_DIR/house_price_ml.poly" | wc -l | tr -d ' ')"
if [[ "$CALL_COUNT" -ne 12 ]]; then
  echo "expected 12 distinct cross-language CALL sites, found $CALL_COUNT" >&2
  exit 1
fi

POLYC_COMMAND_COUNT="$(grep -c '^PATH=/nonexistent "\$POLYC" ' "$EXAMPLE_DIR/run.sh")"
if [[ "$POLYC_COMMAND_COUNT" -ne 1 ]]; then
  echo "run.sh must contain exactly one guarded polyc build command" >&2
  exit 1
fi
grep -Fq -- '--polyld="$POLYLD"' "$EXAMPLE_DIR/run.sh"
grep -Fq -- '--no-package-index' "$EXAMPLE_DIR/run.sh"

if grep -Eq '(^|[[:space:]])(clang|clang\+\+|gcc|g\+\+|c\+\+|python3?|rustc|cargo|go)([[:space:]]|$)' \
  "$EXAMPLE_DIR/run.sh"; then
  echo "run.sh contains a forbidden external language tool invocation" >&2
  exit 1
fi

if command -v nm >/dev/null 2>&1; then
  SYMBOLS="$(nm -g "$BUILD_DIR/house_price_ml")"
  for symbol in \
    generate_feature generate_noise generate_target \
    stat_square stat_cross_product \
    ols_numerator ols_denominator fit_slope fit_intercept \
    model_predict model_absolute_error model_mean_absolute_error; do
    grep -Eq "(_)?${symbol}$" <<<"$SYMBOLS"
  done
else
  echo "warning: nm unavailable; skipped final symbol-presence audit" >&2
fi

echo "house_price_ml: compiler-native training audit passed" >&2
