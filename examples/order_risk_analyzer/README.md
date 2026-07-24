# Data-driven order authorization and fulfillment engine

This is an end-to-end `polyc` business fixture: one `.poly` entry, one build command, four built-in language frontends, four vendored packages with object models, runtime CSV input, and one native executable produced by the local `polyld`.

```text
data/orders.csv
       |
       v
Poly file I/O, recursive stream processing, aggregation, assertions
       |
       +--> C++ order_policy: construction, state, methods, destruction
       +--> Python fraud_policy: construction, state, methods, explicit cleanup
       +--> Rust fulfillment_policy: struct construction, &self/&mut self methods
       +--> Go logistics_policy: struct construction and pointer receivers
       |
       v
8 rows + category totals + checksum 483 -> process status 232
```

The user does not invoke clang/GCC, CPython, rustc, Go, `polyld`, a package manager, or a manual `-I` step.

## Package resolution and object paths in all four languages

The Poly entry declares four project-vendored packages:

```poly
IMPORT cpp PACKAGE order_policy >= 1.0;
IMPORT python PACKAGE fraud_policy >= 1.0;
IMPORT rust PACKAGE fulfillment_policy >= 1.0;
IMPORT go PACKAGE logistics_policy >= 1.0;
```

The Poly frontend parses each dependency name and constraint. `polyc` locates its `poly.package.toml`, validates `name`, `language`, and manifest `version` against `>=`/`<=`/`==`/`>`/`<`/`~=`, and rejects absolute paths, `..`, or symlinks escaping the project's canonical `packages/` root. With `--no-package-index`, missing, duplicate, wrong-language, source-package-without-`source`, or version-incompatible declarations fail closed; C++ may instead declare a header-only package through `include_dir`. That include root is passed to the built-in preprocessor, so the following works without command-line include flags:

```cpp
#include <order_policy/pricing_session.hpp>
```

The Python, Rust, and Go manifests contribute declared source files that are deterministically merged with their consumers. The same bundle is used by foreign-signature extraction and final object compilation. Go package declarations are normalized to the consumer's `package main`. No pip, Cargo, Go modules, package index, or network fetch is invoked.

Each language executes an object path for every row: C++ `OrderPricingSession` has four fields, a constructor, stateful methods, and a destructor; Python `FraudAssessment` has `__init__`, four fields, mutating/read methods, and explicit `close`; Rust `FulfillmentSession` uses named-field construction plus `&self` and `&mut self` methods; Go `LogisticsSession` uses stack allocation, a constructor function, and pointer-receiver mutation/read methods. Each object wrapper is checked against independent free-function rules, so errors return `60`–`63` instead of remaining dead sample code.

These are real aggregate states lowered through the language frontends and IR/backend. In particular, the C++ object's x86_64 fields are addressed at byte offsets `0/4/8/12` through aggregate GEP, loads, stores, and address calculation.

## Runtime data processing

The executable opens `data/orders.csv` at runtime; its eight rows are not embedded as constants. `polyc` injects a small repository-owned native runtime only when these APIs are referenced:

- `file_open_ints(path)` opens the input;
- `file_next_int(fd, eof)` skips CSV headers/delimiters and parses the next signed integer;
- `file_close(fd)` closes the descriptor.

`process_order_stream` recursively reads until actual EOF and carries row count, decision checksum, and category totals through normal native ABI arguments. It is not an unrolled sequence of eight reads. The test copies the CSV, changes the first order's quantity while keeping its expected decision unchanged, then runs the same already-compiled executable and requires the controlled validation status `225`. This proves that a real business input field reaches the compiled pricing, inventory, and logistics calculation.

The current file runtime uses direct x86_64 Linux/macOS syscalls and does not depend on libc or CPython.

## Sixteen cross-language business calls

- C++: `pricing_subtotal`, `pricing_discount`, `pricing_payable`, `pricing_session_payable`
- Python: `fraud_velocity_points`, `fraud_amount_points`, `fraud_risk_band`, `fraud_session_band`
- Rust: `inventory_reservable`, `payment_authorization`, `fulfillment_gate`, `fulfillment_session_gate`
- Go: `logistics_base_days`, `logistics_capacity_delay`, `logistics_decision`, `logistics_session_decision`

The policies include nested branches, early returns, cross-stage dependencies, and four final outcomes. Approved orders become `100 + risk band * 10 + ETA`; review, fraud rejection, and inventory rejection are `20`, `40`, and `30`.

## CSV acceptance data

| Order | Primary path | Decision |
| ---: | --- | ---: |
| 1001 | Standard approval, zone 2 | 116 |
| 1002 | Small-order approval | 113 |
| 1003 | Medium risk and high amount, manual review | 20 |
| 1004 | Three failed payments, fraud rejection | 40 |
| 1005 | Insufficient stock after safety reserve | 30 |
| 1006 | Ten-day ETA, SLA review | 20 |
| 1007 | Object pricing path and zone-1 approval | 114 |
| 1008 | Zero sellable stock | 30 |

The aggregate is 8 rows, 3 approvals, 2 reviews, 1 fraud rejection, 2 inventory rejections, and checksum `483`. Success returns `483 - 251 = 232`.

## One build command

`run.sh` contains one compiler invocation:

```bash
../../build/polyc --strict --no-package-index -O0 \
  -o build/polyc/order_risk order_risk.poly
```

`--no-package-index` prevents host package-manager probing; all four packages resolve from project-local manifests. Four source imports plus four package declarations drive built-in frontend compilation, sixteen `CALL` sites drive signature/link validation, the file runtime is injected on demand, and local `polyld` emits the executable.

## Files

| File | Responsibility |
| --- | --- |
| `order_risk.poly` | Four package dependencies, CSV streaming, 16 foreign calls, aggregation, assertions |
| `packages/order_policy/poly.package.toml` | Vendored package metadata and include-root declaration |
| `packages/order_policy/include/order_policy/pricing_session.hpp` | Four-field C++ class, constructor/destructor, and member methods |
| `packages/fraud_policy/{poly.package.toml,src/fraud_policy.py}` | Python package manifest and `FraudAssessment` class |
| `packages/fulfillment_policy/{poly.package.toml,src/fulfillment_policy.rs}` | Rust package manifest and `FulfillmentSession` type |
| `packages/logistics_policy/{poly.package.toml,src/logistics_policy.go}` | Go package manifest and `LogisticsSession` type |
| `cpp/pricing_engine.cpp` | C++ object adapter and three independent pricing rules |
| `python/fraud_engine.py` | Python object adapter and three fraud rules |
| `rust/fulfillment_engine.rs` | Rust object adapter and three inventory/payment rules |
| `go/logistics_engine.go` | Go object adapter and three logistics/decision rules |
| `data/orders.csv` | Runtime business input and expected decisions |
| `run.sh` | Sole build command, execution, and status check |
| `test.sh` | Artifact, symbol, package-resolution, and data-sensitivity audit |

## Run

Build `polyc` and `polyld` at the repository root, then:

```bash
cd examples/order_risk_analyzer
./run.sh
./test.sh
```

Use `POLYGLOT_BUILD_DIR=build-release ./test.sh` to select another build tree inside the repository. The scripts reject toolchains outside this repository.

On supported x86_64 POSIX hosts the same audit is registered with CTest:

```bash
ctest --test-dir build --output-on-failure -R '^example_order_risk_analyzer$'
```

## Current boundary

The cross-language ABI intentionally remains deterministic scalar integers; object state stays inside its owning language module rather than crossing as a handle. C++ has deterministic destruction. The current static Python/Rust subsets use explicit `close`, and Go has no destructor semantics, so this example does not claim GC, Rust `Drop`, or a Go destructor. This validates project-local, manifest-driven single-source header/source vendoring, not arbitrary pip/Cargo/Go-module ecosystems or remote downloads. The current vertical slice uses one consumer unit per language; independently objectifying one shared source package into multiple same-language consumers still needs a module model, and merged Go consumers do not yet reorganize arbitrary import blocks. The native file runtime currently supports x86_64 Linux/macOS; unsupported targets fail explicitly instead of falling back to a system compiler or interpreter.

中文：[README_zh.md](README_zh.md)
