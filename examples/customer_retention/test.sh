#!/usr/bin/env bash
set -euo pipefail

EXAMPLE_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$EXAMPLE_DIR/../.." && pwd)"
TOOLCHAIN_DIR="${POLYGLOT_BUILD_DIR:-$REPO_ROOT/build}"
if [[ "$TOOLCHAIN_DIR" != /* ]]; then
  TOOLCHAIN_DIR="$REPO_ROOT/$TOOLCHAIN_DIR"
fi
POLYC="$TOOLCHAIN_DIR/polyc"
BUILD_DIR="$EXAMPLE_DIR/build/polyc"
AUX_DIR="$EXAMPLE_DIR/aux"
mkdir -p "$BUILD_DIR"

"$EXAMPLE_DIR/run.sh" > "$BUILD_DIR/actual_output.txt"
diff -u "$EXAMPLE_DIR/expected_output.txt" "$BUILD_DIR/actual_output.txt"

for web_asset in \
  "$EXAMPLE_DIR/index.html" \
  "$EXAMPLE_DIR/styles.css" \
  "$EXAMPLE_DIR/app.js" \
  "$EXAMPLE_DIR/data/generated_accounts.js"; do
  if [[ ! -s "$web_asset" ]]; then
    echo "missing or empty browser asset: $web_asset" >&2
    exit 1
  fi
done

EXPECTED_BROWSER_DATA="window.CUSTOMER_RETENTION_DATA={version:1,source:'Python to Java to JavaScript, compiled by PolyglotCompiler',weights:{session:2,purchase:5,complaint:4,inactivity:2},thresholds:{healthy:30,watch:10},accounts:[    { id: 1001, sessions: 12, purchases: 5, complaints: 0, daysSinceLogin: 1, activity: 49, health: 47, segment: 1 },    { id: 1002, sessions: 10, purchases: 4, complaints: 1, daysSinceLogin: 4, activity: 36, health: 28, segment: 2 },    { id: 1003, sessions: 8, purchases: 3, complaints: 2, daysSinceLogin: 7, activity: 23, health: 9, segment: 3 },    { id: 1004, sessions: 6, purchases: 2, complaints: 3, daysSinceLogin: 10, activity: 10, health: -10, segment: 3 },    { id: 1005, sessions: 4, purchases: 1, complaints: 4, daysSinceLogin: 13, activity: -3, health: -29, segment: 3 },],checksum:45};"
grep -Fxq "$EXPECTED_BROWSER_DATA" "$EXAMPLE_DIR/data/generated_accounts.js"
if [[ "$(wc -c < "$EXAMPLE_DIR/data/generated_accounts.js" | tr -d ' ')" -ne "${#EXPECTED_BROWSER_DATA}" ]]; then
  echo "generated browser data contains unexpected extra or missing bytes" >&2
  exit 1
fi
if grep -Fq '\n' "$EXAMPLE_DIR/data/generated_accounts.js"; then
  echo "generated browser data contains a literal \\n escape" >&2
  exit 1
fi

grep -Fqx '<!doctype html>' "$EXAMPLE_DIR/index.html"
grep -Fq '<html lang="zh-CN">' "$EXAMPLE_DIR/index.html"
DATA_SCRIPT_REF="$(grep -nF '<script defer src="data/generated_accounts.js"></script>' "$EXAMPLE_DIR/index.html")"
POLICY_SCRIPT_REF="$(grep -nF '<script defer src="javascript/retention_policy.js"></script>' "$EXAMPLE_DIR/index.html")"
APP_SCRIPT_REF="$(grep -nF '<script defer src="app.js"></script>' "$EXAMPLE_DIR/index.html")"
DATA_SCRIPT_LINE="${DATA_SCRIPT_REF%%:*}"
POLICY_SCRIPT_LINE="${POLICY_SCRIPT_REF%%:*}"
APP_SCRIPT_LINE="${APP_SCRIPT_REF%%:*}"
if [[ -z "$DATA_SCRIPT_LINE" || -z "$POLICY_SCRIPT_LINE" || -z "$APP_SCRIPT_LINE" ]] ||
   (( DATA_SCRIPT_LINE >= POLICY_SCRIPT_LINE || POLICY_SCRIPT_LINE >= APP_SCRIPT_LINE )); then
  echo "browser scripts must load generated data, policy, then app.js" >&2
  exit 1
fi

if grep -Eiq "type[[:space:]]*=[[:space:]]*[\"']module[\"']|fetch[[:space:]]*\\(|https?://" \
  "$EXAMPLE_DIR/index.html" "$EXAMPLE_DIR/styles.css" "$EXAMPLE_DIR/app.js"; then
  echo "browser frontend must stay zero-build, file:// compatible, and offline" >&2
  exit 1
fi

for javascript_source in \
  "$EXAMPLE_DIR/data/generated_accounts.js" \
  "$EXAMPLE_DIR/javascript/retention_policy.js" \
  "$EXAMPLE_DIR/app.js"; do
  check_name="${javascript_source##*/}.check.json"
  PATH=/nonexistent "$POLYC" --check --lang=javascript --ecma=es2026 \
    "$javascript_source" > "$BUILD_DIR/$check_name"
  grep -Fq '"diagnostics":[]' "$BUILD_DIR/$check_name"
done

DATA_ROWS="$(grep -c '^100[1-5],' "$EXAMPLE_DIR/data/generated_accounts.csv")"
if [[ "$DATA_ROWS" -ne 5 ]]; then
  echo "expected five generated account rows, found $DATA_ROWS" >&2
  exit 1
fi
grep -Fxq \
  "account_id,sessions,purchases,complaints,days_since_login,activity_signal,health_score,segment" \
  "$EXAMPLE_DIR/data/generated_accounts.csv"
NONEMPTY_ROWS="$(grep -cve '^$' "$EXAMPLE_DIR/data/generated_accounts.csv")"
if [[ "$NONEMPTY_ROWS" -ne 6 ]]; then
  echo "expected one header and five data rows, found $NONEMPTY_ROWS non-empty lines" >&2
  exit 1
fi
for expected_row in \
  "1001,12,5,0,1,49,47,healthy" \
  "1002,10,4,1,4,36,28,watch" \
  "1003,8,3,2,7,23,9,risk" \
  "1004,6,2,3,10,10,-10,risk" \
  "1005,4,1,4,13,-3,-29,risk"; do
  grep -Fxq "$expected_row" "$EXAMPLE_DIR/data/generated_accounts.csv"
done

for artifact in \
  "$AUX_DIR/python_telemetry_data.o" \
  "$AUX_DIR/java_SignalRules.o" \
  "$AUX_DIR/javascript_retention_policy.o" \
  "$AUX_DIR/customer_retention_foreign_aliases.pobj" \
  "$AUX_DIR/customer_retention_link_descriptors.paux" \
  "$BUILD_DIR/customer_retention"; do
  if [[ ! -s "$artifact" ]]; then
    echo "missing or empty compiler artifact: $artifact" >&2
    exit 1
  fi
done

for descriptor_row in \
  "IMPORT python telemetry_data" \
  "IMPORT java SignalRules" \
  "IMPORT javascript retention_policy"; do
  grep -Fq "$descriptor_row" "$AUX_DIR/customer_retention_link_descriptors.paux"
done

SYMBOL_COUNT="$(grep -c '^SYMBOL ' "$AUX_DIR/customer_retention_link_descriptors.paux")"
if [[ "$SYMBOL_COUNT" -ne 6 ]]; then
  echo "expected six packaged foreign symbols, found $SYMBOL_COUNT" >&2
  exit 1
fi

POLYC_COMMAND_COUNT="$(grep -c '^PATH=/nonexistent "\$POLYC" ' "$EXAMPLE_DIR/run.sh")"
if [[ "$POLYC_COMMAND_COUNT" -ne 1 ]]; then
  echo "run.sh must contain exactly one guarded polyc build command" >&2
  exit 1
fi
grep -Fq -- '--polyld="$POLYLD"' "$EXAMPLE_DIR/run.sh"
grep -Fq -- '--no-package-index' "$EXAMPLE_DIR/run.sh"
grep -Fq -- '--strict' "$EXAMPLE_DIR/run.sh"

for descriptor_symbol in \
  "SYMBOL telemetry_data::telemetry_sessions python telemetry_data::telemetry_sessions" \
  "SYMBOL telemetry_data::telemetry_purchases python telemetry_data::telemetry_purchases" \
  "SYMBOL telemetry_data::telemetry_complaints python telemetry_data::telemetry_complaints" \
  "SYMBOL telemetry_data::telemetry_days_since_login python telemetry_data::telemetry_days_since_login" \
  "SYMBOL SignalRules::java_activity_signal java SignalRules::java_activity_signal" \
  "SYMBOL retention_policy::js_retention_score javascript retention_policy::js_retention_score"; do
  grep -Fqx "$descriptor_symbol" "$AUX_DIR/customer_retention_link_descriptors.paux"
done

CALL_COUNT="$(grep -o 'CALL(' "$EXAMPLE_DIR/customer_retention.poly" | wc -l | tr -d ' ')"
if [[ "$CALL_COUNT" -ne 6 ]]; then
  echo "expected six distinct cross-language CALL sites, found $CALL_COUNT" >&2
  exit 1
fi

if grep -Eq '(^|[[:space:]])(javac|java|node|npm|npx|pnpm|yarn|bun|deno|tsc|mvn|gradle|python3?|clang|clang\+\+|gcc|g\+\+|c\+\+|cc|rustc|cargo|go|dotnet|ruby)([[:space:]]|$)' \
  "$EXAMPLE_DIR/run.sh"; then
  echo "run.sh contains a forbidden external language tool invocation" >&2
  exit 1
fi

if command -v nm >/dev/null 2>&1; then
  SYMBOLS="$(nm -g "$BUILD_DIR/customer_retention")"
  for symbol in \
    telemetry_sessions telemetry_purchases telemetry_complaints telemetry_days_since_login \
    java_activity_signal js_retention_score; do
    grep -Eq "(_)?${symbol}$" <<<"$SYMBOLS"
  done
else
  echo "warning: nm unavailable; skipped final symbol-presence audit" >&2
fi

echo "customer_retention: compiler-native cross-language audit passed" >&2
