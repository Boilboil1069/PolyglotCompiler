# Four-language order application: native execution and acceptance

`order_risk.poly` orchestrates C++ pricing, Python fraud assessment, Rust inventory/payment, and Go logistics. One `polyc` invocation discovers sources and four project-local package manifests, compiles them with the built-in frontends, and links one native executable with `polyld`.

Building the application does not invoke official language compilers or interpreters. The acceptance harness separately runs the original module implementations with official tools to provide an independent comparison.

## Build and run

Build `polyc` and `polyld` in the repository first:

```sh
cd examples/order_risk_analyzer
POLYGLOT_BUILD_DIR=build-release ./run.sh
```

The script has one compiler invocation, equivalent to:

```sh
../../build-release/polyc --strict --no-package-index -O0 --regalloc=linear \
  --build-report=build/polyc/build-report.json \
  -o build/polyc/order_risk order_risk.poly
build/polyc/order_risk data/orders.csv order-results.txt
```

Use `POLY_OPT_LEVEL=0..3` and `POLY_REGALLOC=linear|graph` with `run.sh`. Its first and second arguments select the input and output paths, defaulting to `data/orders.csv` and `order-results.txt`. Success exits **0**. stdout and the result file contain identical bytes:

```text
ORDER 1001 116
ORDER 1002 113
ORDER 1003 20
ORDER 1004 40
ORDER 1005 30
ORDER 1006 20
ORDER 1007 114
ORDER 1008 30
SUMMARY 8 3 2 1 2 483
```

The summary contains row count, approvals, reviews, fraud rejections, inventory rejections and the decision checksum. Counts are computed at runtime; neither eight rows nor checksum 483 is hard-coded.

## Input and errors

The eight fields are `order_id,unit_price,quantity,recent_orders,failed_payments,available_units,delivery_zone,expected_decision`. IDs must be positive; price is `1..1000000`, quantity `1..1000`, recent orders and available stock `0..1000000`, failed payments `0..1000`, and delivery zone `1..3`.

The runtime reads an integer stream, skipping header text and delimiters. It is not a complete CSV syntax parser and does not reject every possible quoting or delimiter error. Empty inputs, incomplete records, invalid business ranges and incorrect expected decisions fail without a success summary. Earlier valid output rows may remain after a later failure.

Exit codes: `220` input open failure; `221` close failure; `222` empty/truncated/invalid input; `225` expected-result mismatch, including object consistency failures; `226` unclassified decision; `227` output open/write failure; `228` summary allocation failure.

## Actual object and package paths

All 16 foreign calls remain part of the real per-order computation. Each language compares independent free-function rules with an object method result:

| Module | Vendored package and object |
| --- | --- |
| C++ `pricing_engine.cpp` | `order_policy`: four-field `OrderPricingSession`, constructor, field mutation, methods, destructor |
| Python `fraud_engine.py` | `fraud_policy`: `FraudAssessment`, initialization, mutation, classification, explicit `close()` |
| Rust `fulfillment_engine.rs` | `fulfillment_policy`: `FulfillmentSession`, named-field construction, `&self` / `&mut self` methods |
| Go `logistics_engine.go` | `logistics_policy`: `LogisticsSession`, construction and pointer receiver mutation/read |

`IMPORT <language> PACKAGE <name> >= 1.0` resolves `poly.package.toml` under the project's `packages/`. C++ declares an include root; the other packages declare source units merged into their consumer. `--no-package-index` prevents host package-manager/network-index queries. Signature extraction and actual compilation use the same package sources.

Objects stay in their own language modules and cross-language calls use integer scalars. This checks manifest-driven local source/header vendoring, not the complete pip/Cargo/Go modules ecosystem. Explicit Python/Rust cleanup does not claim Python GC or Rust `Drop`; Go has no destructor claim.

## Full regression

From the repository root:

```sh
python3 tests/native_programs/mixed_regression.py \
  --polyc build-release/polyc --output-root build-release/mixed-regression \
  --require-reference
```

Alternatively run `./test.sh --require-reference` from this directory. The default matrix is **O0–O3 × linear/graph: eight build configurations**. Each executable is reused for ten scenarios: baseline; changed quantity with updated expectation; 24 rows; reversed rows; changed quantity with stale expectation; truncated record; invalid quantity; empty input; missing input; output path that is a directory.

Success checks exact stdout, exact file content, empty stderr and exit 0. Negative cases check the specified nonzero exit and output, so a crash is not accepted as a successful rejection. The `polyglot.native-build.v1` report must contain successful Poly/C++/Python/Rust/Go modules, module timing and object paths, and the requested optimization and allocator settings for every module.

Official reference adapters preserve the original foreign source/package bodies and add standard entry/I/O code. A Python harness composes their outputs and checks them against a separate integer-rule oracle and the native application. Missing official tools remain coverage gaps; `--require-reference` makes them fail. Poly has no independent official implementation.

## Performance protocol

After regression passes, reserve a quiet CPU window:

```sh
python3 scripts/benchmark_mixed_native.py --polyc build-release/polyc \
  --output build-release/mixed-performance/run-001 \
  --warmups 1 --repetitions 7 --row-counts 8 800 8000 --require-reference
```

The output directory must be new. A recorded random seed shuffles configuration order each round. Every compile uses a fresh application copy and artifact directory; source copying and official reference builds are outside measured native compilation. OS caches are not flushed.

The default protocol has 64 fresh builds: eight configurations × (one warmup + seven measured samples). First-launch latency is retained separately. After the ten acceptance scenarios, each executable independently runs 8, 800 and 8000 rows in a seeded shuffled order; no acceptance timing is reused. Each size starts a fresh process from an executable that has already been launched. Larger inputs repeat the same rule mix with unique IDs and recompute every decision and summary through the independent integer oracle. All output bytes and the result file must match at every size. Official source comparison covers the four smaller acceptance scenarios; larger sizes measure input-volume scaling, not additional business-rule coverage.

The report retains total and per-module compile time, runtime distributions by row count, available peak RSS, executable size, raw samples, commands, versions and hashes. RSS uses per-child `wait4` resource records, not simultaneous aggregate process-tree memory. Runtime includes startup, input, stdout and file writes. Any failed acceptance or scale case suppresses that configuration's performance aggregates. Warmups are retained but excluded. A one-sample precheck is not a formal performance result.

## CI and reference tools

The main CI calls `.github/workflows/native-validation.yml`, also available through manual dispatch. Linux/macOS jobs explicitly install reference tools and Qt, then run native differential/runtime/entry tests, the mixed matrix and the actual Qt workspace smoke. macOS ARM64 also executes native trace and numeric ABI regressions and checks real runtime values in the UI. Linux x86 runs the trace model tests; native tracing is explicitly outside its supported scope. Missing references, missing Qt targets and output mismatches fail. CI's one-sample benchmark checks collection only.

See [official Rust installation](https://rust-lang.org/tools/install/) and [official Go installation](https://go.dev/doc/install). Discovered paths and versions are retained. Workflow configuration alone is not evidence that remote CI has run or passed.

中文：[README_zh.md](README_zh.md)
