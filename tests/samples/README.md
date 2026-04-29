# `.ploy` Sample Programs

This directory holds the canonical PolyglotCompiler sample matrix.  Every
folder contains:

- A `.ploy` entry file demonstrating one feature theme.
- One or more host-language source files (C++, Python, Rust, Java, C#, Go,
  JavaScript) that the `.ploy` file references.
- An `expected_output.txt` file — byte-exact runtime stdout used by the
  regression harness.
- Bilingual `README.md` (English) and `README_zh.md` (Chinese).

The harness `scripts/build_all_samples.ps1` (POSIX twin:
`scripts/build_all_samples.sh`) walks every folder, runs `polyc` then
`polyld`, executes the binary, captures stdout, and compares it byte-for-byte
against `expected_output.txt`.  Each sample is classified into one of:

- `OK` — stdout matches expected output.
- `OUTPUT_MISMATCH` — stdout differs from expected output.
- `EMPTY_STDOUT` — process exited 0 but produced no stdout.
- `RUN_FAIL` — produced binary failed at runtime.
- `LINK_FAIL` — `polyld` failed.
- `COMPILE_FAIL` — `polyc` failed.
- `SKIP` — sample folder lacked a `.ploy` entry.

The harness writes `build/samples_report.json` and exits 0 by default so the
report can document toolchain maturity without gating the build.  Pass
`-FailOnMismatch` (PowerShell) or `--fail-on-mismatch` (bash) to flip into
strict gating mode.

## Directory matrix

| Folder | Languages | Theme | Description |
| --- | --- | --- | --- |
| `01_basic_linking/` | C++, Python | Cross-language linking | LINK / CALL / IMPORT / EXPORT basics. |
| `02_type_mapping/` | C++, Python | Type mapping | MAP_TYPE with structs and containers. |
| `03_pipeline/` | C++, Python | Control flow | PIPELINE with IF / WHILE / FOR / MATCH. |
| `04_package_import/` | C++, Python | Package import | IMPORT PACKAGE with version constraints. |
| `05_class_instantiation/` | C++, Python | Object model | Cross-language NEW + METHOD. |
| `06_attribute_access/` | C++, Python | Object model | Cross-language GET / SET on attributes. |
| `07_resource_management/` | C++, Python | Object model | WITH-driven resource management. |
| `08_delete_extend/` | C++, Python | Object model | DELETE + EXTEND on foreign classes. |
| `09_mixed_pipeline/` | C++, Python, Rust | Full pipeline | End-to-end ML pipeline combining every keyword. |
| `10_error_handling/` | C++, Python | Diagnostics | Error scenarios surfaced by the front end. |
| `11_java_interop/` | Java, Python | Object model | Java NEW + METHOD via JVM bridge. |
| `12_dotnet_interop/` | C#, Python | Object model | .NET NEW + METHOD via CLR bridge. |
| `13_generic_containers/` | C++, Java, Python | Type mapping | Generic container interop (vector / ArrayList / list). |
| `14_async_pipeline/` | C++, Rust, Python | Full pipeline | Multi-stage signal processing pipeline. |
| `15_full_stack/` | C++, Python, Rust, Java, C# | Full pipeline | Five-language full-stack analytics demo. |
| `16_config_and_venv/` | Python, C# | Package import | CONFIG VENV + IMPORT PACKAGE + CONVERT. |
| `17_string_processing/` | Python, Rust | String processing pipeline | Tokenize + case-fold across Rust and Python. |
| `18_numeric_kernels/` | C++, Rust | Numeric kernels (BLAS-style) | AXPY + dot/mean reductions. |
| `19_file_io/` | Python, C++ | Streaming file I/O | Binary chunk reader + UTF-8 decoder. |
| `20_json_pipeline/` | Python, Java | JSON ingest pipeline | JSON parse + schema normalisation. |
| `21_image_processing/` | C++, Rust | Image processing kernels | Greyscale conversion + 3x3 box blur. |
| `22_database_access/` | Python, Java | Database access layer | In-memory connection + DAO mapping. |
| `23_http_client/` | Python, Go | HTTP client demo | Request-line builder + response decoder. |
| `24_concurrency/` | C++, Rust | Concurrency primitives | Atomic counter + parallel reduction. |
| `25_event_loop/` | Python, JavaScript | Event loop simulation | Microtask scheduler + dispatcher. |
| `26_state_machine/` | C++, Java | Finite state machine | Transition table + iterative runner. |
| `27_plugin_system/` | C++, Python | Plugin system | Plugin host + Python plugin contract. |
| `28_ml_inference/` | Python, Rust | ML inference pipeline | Tokenizer + softmax scorer. |
| `29_data_analytics/` | Python, Java | Data analytics | Loader + count/min/max/mean aggregator. |
| `30_game_loop_demo/` | C++, Rust | Game loop skeleton | Tick scheduler + Euler integrator. |

## By theme

- **Concurrency primitives** — `24_concurrency`
- **Control flow** — `03_pipeline`
- **Cross-language linking** — `01_basic_linking`
- **Data analytics** — `29_data_analytics`
- **Database access layer** — `22_database_access`
- **Diagnostics** — `10_error_handling`
- **Event loop simulation** — `25_event_loop`
- **Finite state machine** — `26_state_machine`
- **Full pipeline** — `09_mixed_pipeline`, `14_async_pipeline`, `15_full_stack`
- **Game loop skeleton** — `30_game_loop_demo`
- **HTTP client demo** — `23_http_client`
- **Image processing kernels** — `21_image_processing`
- **JSON ingest pipeline** — `20_json_pipeline`
- **ML inference pipeline** — `28_ml_inference`
- **Numeric kernels (BLAS-style)** — `18_numeric_kernels`
- **Object model** — `05_class_instantiation`, `06_attribute_access`, `07_resource_management`, `08_delete_extend`, `11_java_interop`, `12_dotnet_interop`
- **Package import** — `04_package_import`, `16_config_and_venv`
- **Plugin system** — `27_plugin_system`
- **Streaming file I/O** — `19_file_io`
- **String processing pipeline** — `17_string_processing`
- **Type mapping** — `02_type_mapping`, `13_generic_containers`

## By language combination

- **C#, C++, Java, Python, Rust** — `15_full_stack`
- **C#, Python** — `12_dotnet_interop`, `16_config_and_venv`
- **C++, Java** — `26_state_machine`
- **C++, Java, Python** — `13_generic_containers`
- **C++, Python** — `01_basic_linking`, `02_type_mapping`, `03_pipeline`, `04_package_import`, `05_class_instantiation`, `06_attribute_access`, `07_resource_management`, `08_delete_extend`, `10_error_handling`, `19_file_io`, `27_plugin_system`
- **C++, Python, Rust** — `09_mixed_pipeline`, `14_async_pipeline`
- **C++, Rust** — `18_numeric_kernels`, `21_image_processing`, `24_concurrency`, `30_game_loop_demo`
- **Go, Python** — `23_http_client`
- **Java, Python** — `11_java_interop`, `20_json_pipeline`, `22_database_access`, `29_data_analytics`
- **JavaScript, Python** — `25_event_loop`
- **Python, Rust** — `17_string_processing`, `28_ml_inference`

## Build a single sample

```powershell
polyc 09_mixed_pipeline/mixed_pipeline.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
./build/sample.exe
```

## Build every sample

```powershell
# Windows
./scripts/build_all_samples.ps1

# POSIX
./scripts/build_all_samples.sh
```

The integration test `samples_regression_test.cpp` (registered under the
`integration_tests` Catch2 binary, tag `[samples][b6]`) drives the harness
and asserts that the produced JSON report is well-formed.

Bilingual sibling: [README_zh.md](./README_zh.md).
