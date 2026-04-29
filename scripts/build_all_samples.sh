#!/usr/bin/env bash
# ============================================================================
# build_all_samples.sh — POSIX counterpart to build_all_samples.ps1.
#
# Walks every sample folder under tests/samples/, runs polyc then polyld on
# each .ploy entry file, executes the produced binary, captures stdout and
# byte-compares it against the sibling expected_output.txt.  Per-sample
# status mirrors the PowerShell harness (OK / OUTPUT_MISMATCH / RUN_FAIL /
# EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP).
#
# Exit code is 0 once the matrix has finished, regardless of per-sample
# pass/fail, so the integration test layer can assert on report
# well-formedness rather than on toolchain maturity.  Pass --fail-on-mismatch
# to switch to strict mode.
#
# Usage:
#   ./scripts/build_all_samples.sh [--samples-dir DIR] [--build-dir DIR]
#                                  [--report PATH] [--fail-on-mismatch]
# ============================================================================
set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SAMPLES_DIR="$REPO_ROOT/tests/samples"
BUILD_DIR="$REPO_ROOT/build"
REPORT=""
FAIL_ON_MISMATCH=0
REQUIRE_MIN_OK=-1

while [ $# -gt 0 ]; do
    case "$1" in
        --samples-dir)       SAMPLES_DIR="$2"; shift 2 ;;
        --build-dir)         BUILD_DIR="$2";   shift 2 ;;
        --report)            REPORT="$2";      shift 2 ;;
        --fail-on-mismatch)  FAIL_ON_MISMATCH=1; shift ;;
        --require-min-ok)    REQUIRE_MIN_OK="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,18p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$REPORT" ]; then REPORT="$BUILD_DIR/samples_report.json"; fi

# Pick the platform-appropriate tool names.
case "$(uname -s 2>/dev/null || echo Windows)" in
    MINGW*|MSYS*|CYGWIN*|Windows*) EXE_SUFFIX=".exe" ;;
    *)                              EXE_SUFFIX="" ;;
esac
POLYC="$BUILD_DIR/polyc${EXE_SUFFIX}"
POLYLD="$BUILD_DIR/polyld${EXE_SUFFIX}"

if [ ! -x "$POLYC" ];  then echo "polyc not found at $POLYC — build the project first." >&2; exit 1; fi
if [ ! -x "$POLYLD" ]; then echo "polyld not found at $POLYLD — build the project first." >&2; exit 1; fi
if [ ! -d "$SAMPLES_DIR" ]; then echo "samples dir not found: $SAMPLES_DIR" >&2; exit 1; fi

WORK_ROOT="$BUILD_DIR/samples_work"
mkdir -p "$WORK_ROOT"

# Aggregate state.
declare -a STATUSES
declare -a NAMES
declare -a POLYC_RC POLYLD_RC EXE_RC STDOUT_BYTES EXPECTED_BYTES DIFF_OFF
TOTAL=0

run_sample() {
    local dir="$1"
    local name; name="$(basename "$dir")"
    local ploy; ploy="$(find "$dir" -maxdepth 1 -name '*.ploy' | head -n 1)"
    local status="SKIP" prc="" lrc="" erc="" sb=0 eb=0 doff=-1

    if [ -n "$ploy" ]; then
        local stem; stem="$(basename "$ploy" .ploy)"
        local work="$WORK_ROOT/$name"
        rm -rf "$work" && mkdir -p "$work"
        local obj="$work/$stem.obj" exe="$work/$stem.exe" out="$work/$stem.stdout"
        local exp="$dir/expected_output.txt"
        if [ -f "$exp" ]; then eb=$(wc -c < "$exp" | tr -d ' '); fi

        # 1) polyc
        "$POLYC" "$ploy" "--emit-obj=$obj" --quiet \
            >"$work/polyc.log" 2>"$work/polyc.log.err"
        prc=$?
        if [ "$prc" -ne 0 ] || [ ! -f "$obj" ]; then
            status="COMPILE_FAIL"
        else
            # 2) polyld
            "$POLYLD" "$obj" -o "$exe" \
                >"$work/polyld.log" 2>"$work/polyld.log.err"
            lrc=$?
            if [ "$lrc" -ne 0 ] || [ ! -f "$exe" ]; then
                status="LINK_FAIL"
            else
                # 3) Run and capture.
                "$exe" >"$out" 2>/dev/null
                erc=$?
                if [ -f "$out" ]; then sb=$(wc -c < "$out" | tr -d ' '); fi
                if [ "$erc" -ne 0 ]; then
                    status="RUN_FAIL"
                elif [ "$sb" -eq 0 ] && [ "$eb" -gt 0 ]; then
                    status="EMPTY_STDOUT"
                elif [ -f "$exp" ] && cmp -s "$out" "$exp"; then
                    status="OK"
                else
                    # Find first diff offset.
                    if [ -f "$exp" ]; then
                        doff=$(cmp -l "$out" "$exp" 2>/dev/null | head -n 1 | awk '{print $1-1}')
                        if [ -z "$doff" ]; then doff=$sb; fi
                    fi
                    status="OUTPUT_MISMATCH"
                fi
            fi
        fi
    fi

    NAMES+=("$name");           STATUSES+=("$status")
    POLYC_RC+=("$prc");         POLYLD_RC+=("$lrc");      EXE_RC+=("$erc")
    STDOUT_BYTES+=("$sb");      EXPECTED_BYTES+=("$eb");  DIFF_OFF+=("$doff")
    TOTAL=$((TOTAL + 1))
    printf '[samples] %-32s %s\n' "$name" "$status"
}

for d in "$SAMPLES_DIR"/*/; do
    [ -d "$d" ] && run_sample "$d"
done

# ----------------------------------------------------------------------------
# Emit a JSON report with the same shape as the PowerShell harness.
# ----------------------------------------------------------------------------
mkdir -p "$(dirname "$REPORT")"
{
    printf '{\n'
    printf '  "generated_utc": "%s",\n' "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    printf '  "samples_dir":   "%s",\n' "$SAMPLES_DIR"
    printf '  "build_dir":     "%s",\n' "$BUILD_DIR"
    printf '  "polyc":         "%s",\n' "$POLYC"
    printf '  "polyld":        "%s",\n' "$POLYLD"
    printf '  "total":         %d,\n'   "$TOTAL"
    printf '  "samples": [\n'
    for ((i = 0; i < TOTAL; i++)); do
        sep=','; if [ $((i + 1)) -eq "$TOTAL" ]; then sep=''; fi
        printf '    {"name":"%s","status":"%s","polyc_rc":"%s","polyld_rc":"%s","exe_rc":"%s","stdout_bytes":%d,"expected_bytes":%d,"diff_first_off":%d}%s\n' \
            "${NAMES[i]}" "${STATUSES[i]}" "${POLYC_RC[i]}" "${POLYLD_RC[i]}" "${EXE_RC[i]}" \
            "${STDOUT_BYTES[i]}" "${EXPECTED_BYTES[i]}" "${DIFF_OFF[i]}" "$sep"
    done
    printf '  ]\n}\n'
} > "$REPORT"
echo
echo "[samples] report -> $REPORT"

# Bucket summary.
echo "[samples] summary:"
for s in OK OUTPUT_MISMATCH EMPTY_STDOUT RUN_FAIL LINK_FAIL COMPILE_FAIL SKIP; do
    cnt=0
    for st in "${STATUSES[@]}"; do
        if [ "$st" = "$s" ]; then cnt=$((cnt + 1)); fi
    done
    printf '  %-16s %d\n' "$s" "$cnt"
done

if [ "$FAIL_ON_MISMATCH" -eq 1 ]; then
    bad=0
    for st in "${STATUSES[@]}"; do
        if [ "$st" != "OK" ] && [ "$st" != "SKIP" ]; then bad=$((bad + 1)); fi
    done
    if [ "$bad" -gt 0 ]; then
        echo "[samples] --fail-on-mismatch: $bad sample(s) not OK"
        exit 1
    fi
fi

# --require-min-ok: enforce a minimum number of samples in the OK bucket.
# Exits 1 when the OK count is strictly below the requested threshold.
if [ "$REQUIRE_MIN_OK" -ge 0 ]; then
    ok_count=0
    for st in "${STATUSES[@]}"; do
        if [ "$st" = "OK" ]; then ok_count=$((ok_count + 1)); fi
    done
    if [ "$ok_count" -lt "$REQUIRE_MIN_OK" ]; then
        echo "[samples] --require-min-ok $REQUIRE_MIN_OK not met: only $ok_count OK"
        exit 1
    fi
fi
exit 0
