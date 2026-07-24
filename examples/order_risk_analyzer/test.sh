#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$EXAMPLE_DIR/build/polyc"
AUX_DIR="$EXAMPLE_DIR/aux"
mkdir -p "$BUILD_DIR"

"$EXAMPLE_DIR/run.sh" > "$BUILD_DIR/actual_output.txt"
diff -u "$EXAMPLE_DIR/expected_output.txt" "$BUILD_DIR/actual_output.txt"

for artifact in \
  "$AUX_DIR/cpp_pricing_engine.o" \
  "$AUX_DIR/python_fraud_engine.o" \
  "$AUX_DIR/rust_fulfillment_engine.o" \
  "$AUX_DIR/go_logistics_engine.o" \
  "$AUX_DIR/python_fraud_engine_vendored.py" \
  "$AUX_DIR/rust_fulfillment_engine_vendored.rs" \
  "$AUX_DIR/go_logistics_engine_vendored.go" \
  "$AUX_DIR/order_risk_link_descriptors.paux" \
  "$BUILD_DIR/order_risk"; do
  if [[ ! -s "$artifact" ]]; then
    echo "missing or empty compiler artifact: $artifact" >&2
    exit 1
  fi
done

# The Poly entry must exercise all sixteen business functions, including one
# free-function/object consistency check per language.
CALL_COUNT="$(grep -o 'CALL(' "$EXAMPLE_DIR/order_risk.poly" | wc -l | tr -d ' ')"
if [[ "$CALL_COUNT" -ne 16 ]]; then
  echo "expected 16 cross-language CALL sites, found $CALL_COUNT" >&2
  exit 1
fi

POLYC_COMMAND_COUNT="$(grep -c '^"\$POLYC" ' "$EXAMPLE_DIR/run.sh")"
if [[ "$POLYC_COMMAND_COUNT" -ne 1 ]]; then
  echo "run.sh must contain exactly one polyc build command" >&2
  exit 1
fi

for descriptor_row in \
  "IMPORT cpp pricing_engine" \
  "IMPORT python fraud_engine" \
  "IMPORT rust fulfillment_engine" \
  "IMPORT go logistics_engine"; do
  grep -Fq "$descriptor_row" "$AUX_DIR/order_risk_link_descriptors.paux"
done

DESCRIPTOR_SYMBOL_COUNT="$(grep -c '^SYMBOL ' "$AUX_DIR/order_risk_link_descriptors.paux")"
if [[ "$DESCRIPTOR_SYMBOL_COUNT" -ne 16 ]]; then
  echo "expected 16 packaged foreign symbols, found $DESCRIPTOR_SYMBOL_COUNT" >&2
  exit 1
fi

# All four vendored packages are declared in Poly and resolved from manifests.
# The C++ package contributes an include root; Python/Rust/Go contribute source
# units which are visible in both signature analysis and final compilation.
grep -Fq '#include <order_policy/pricing_session.hpp>' \
  "$EXAMPLE_DIR/cpp/pricing_engine.cpp"
for dependency in \
  'cpp PACKAGE order_policy' \
  'python PACKAGE fraud_policy' \
  'rust PACKAGE fulfillment_policy' \
  'go PACKAGE logistics_policy'; do
  grep -Fq "IMPORT $dependency" "$EXAMPLE_DIR/order_risk.poly"
done
for bundled in \
  "$AUX_DIR/python_fraud_engine_vendored.py:fraud_policy" \
  "$AUX_DIR/rust_fulfillment_engine_vendored.rs:fulfillment_policy" \
  "$AUX_DIR/go_logistics_engine_vendored.go:logistics_policy"; do
  file="${bundled%%:*}"
  package="${bundled##*:}"
  grep -Fq "polyc vendored package: $package" "$file"
done
# The generated source units must contain the package-defined object models,
# not merely a package marker followed by the original consumer.
grep -Fq 'class FraudAssessment:' "$AUX_DIR/python_fraud_engine_vendored.py"
grep -Fq 'def apply_failure_penalty' "$AUX_DIR/python_fraud_engine_vendored.py"
grep -Fq 'pub struct FulfillmentSession' "$AUX_DIR/rust_fulfillment_engine_vendored.rs"
grep -Fq 'pub fn close(&mut self)' "$AUX_DIR/rust_fulfillment_engine_vendored.rs"
grep -Fq 'type LogisticsSession struct' "$AUX_DIR/go_logistics_engine_vendored.go"
grep -Fq 'func (session *LogisticsSession) AdjustETA' \
  "$AUX_DIR/go_logistics_engine_vendored.go"
if grep -Eq '(^|[[:space:]])-I' "$EXAMPLE_DIR/run.sh"; then
  echo "run.sh must not manually provide a package include path" >&2
  exit 1
fi

for runtime_call in file_open_ints file_next_int file_close; do
  grep -Fq "$runtime_call" "$EXAMPLE_DIR/order_risk.poly"
done

# Confirm that the final executable retains every rule function from every
# built-in language frontend; matching tolerates Mach-O's leading underscore.
if command -v nm >/dev/null 2>&1; then
  SYMBOLS="$(nm -g "$BUILD_DIR/order_risk")"
  for symbol in \
    pricing_subtotal pricing_discount pricing_payable pricing_session_payable \
    fraud_velocity_points fraud_amount_points fraud_risk_band fraud_session_band \
    inventory_reservable payment_authorization fulfillment_gate fulfillment_session_gate \
    logistics_base_days logistics_capacity_delay logistics_decision logistics_session_decision \
    polyrt_open_read polyrt_read_i64_or polyrt_close_read; do
    grep -Eq "(_)?${symbol}$" <<<"$SYMBOLS"
  done
  # Package-defined constructors/methods must survive frontend lowering and
  # final polyld linking as native symbols in the executable.
  for object_symbol in \
    'OrderPricingSession::OrderPricingSession' \
    'OrderPricingSession::apply_discount' \
    'OrderPricingSession::~OrderPricingSession' \
    'FraudAssessment.__init__' \
    'FraudAssessment.apply_failure_penalty' \
    'FraudAssessment.band' \
    'FraudAssessment.close' \
    'FulfillmentSession.gate' \
    'FulfillmentSession.close' \
    'NewLogisticsSession' \
    'LogisticsSession.AdjustETA' \
    'LogisticsSession.Decision'; do
    grep -Fq "$object_symbol" <<<"$SYMBOLS"
  done
else
  echo "warning: nm unavailable; skipped final symbol-presence audit" >&2
fi

# Prove that decisions come from CSV business inputs at runtime.  Run the same
# executable from a temporary working directory after changing the first
# order's quantity from 7 to 8 while leaving its expected decision unchanged.
# The real pricing/inventory/logistics pipeline then computes 117 instead of
# 116 and returns the controlled data-validation status 225.  A constant-
# embedded/fake reader or a pipeline that ignored the input field would still
# return 232 and fail this check.
PROBE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/order-risk-data-probe.XXXXXX")"
trap 'rm -rf "$PROBE_DIR"' EXIT
mkdir -p "$PROBE_DIR/data"
sed 's/^1001,18,7,5,1,18,2,116$/1001,18,8,5,1,18,2,116/' \
  "$EXAMPLE_DIR/data/orders.csv" > "$PROBE_DIR/data/orders.csv"
set +e
(
  cd "$PROBE_DIR"
  "$BUILD_DIR/order_risk"
)
PROBE_STATUS=$?
set -e
if [[ "$PROBE_STATUS" -ne 225 ]]; then
  echo "business-input probe: expected controlled status 225, got $PROBE_STATUS" >&2
  exit 1
fi

echo "order_risk_analyzer: one-command OOP/package/data/four-language audit passed" >&2
