# Order Risk Analyzer: complete cross-language example

This is a runnable small business application, not a scaffold. It reads an order CSV, validates it, calculates risk and shipping for every order, and writes both a console report and structured JSON.

## Real execution path

```text
orders.csv
  -> Python: CSV parsing and validation
  -> C++: risk scoring and shipping calculation
  -> Python: decisions, aggregation, and JSON reporting
```

Python calls the compiled C++ shared library through `ctypes`. Every order crosses the C ABI; the tests also pass invalid business data into C++ and assert on its error contract.

| File | Responsibility |
| --- | --- |
| `cpp/risk_kernel.h/.cpp` | Stable C ABI, scoring rules, money rounding |
| `python/risk_bridge.py` | Typed `ctypes.Structure` mapping and errors |
| `python/report_pipeline.py` | CSV validation, orchestration, summary, JSON |
| `python/main.py` | CLI entry point |
| `order_risk.poly` | PolyglotCompiler contract for the same application |
| `tests/test_pipeline.py` | Happy path, C++ failure path, malformed CSV |
| `expected_output.txt` | End-to-end output oracle |

## Run

Python 3.10+ and a C++17 compiler are the only dependencies:

```bash
cd examples/order_risk_analyzer
./run.sh
```

This creates `build/librisk_kernel.*` and `build/report.json`. A custom input and output are also supported:

```bash
./run.sh data/orders.csv --json build/my-report.json
```

On Windows, build the DLL with the included `CMakeLists.txt`, then run `python python/main.py`.

## Test

```bash
./test.sh
```

The test script rebuilds C++, executes the full pipeline, byte-compares stdout, and runs the Python tests. The Poly contract can be checked separately with `../../build/polyc --no-package-index --check order_risk.poly`.

## Poly orchestration status

`order_risk.poly` uses `IMPORT`, `LINK`, `STRUCT`, and `PIPELINE` to describe the cross-language contract. The current signed-`LINK` implementation does not yet carry both language fields into semantic analysis, so the file uses the typed compatibility form and marks its migration point. The host-language frontends also cannot yet compile the complete standard C++ and Python subsets used here, so the executable path uses a `ctypes` bridge with the same ABI. It does not print a canned success marker in place of business work. Once those gaps land, the entry point can move to native Poly orchestration without changing the kernel or data model.

中文：[README_zh.md](README_zh.md)
