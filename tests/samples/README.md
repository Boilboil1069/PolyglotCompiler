# `.poly` Sample Programs

This directory holds the canonical PolyglotCompiler sample matrix.  Every
folder contains:

- A `.poly` entry file demonstrating one feature theme.
- One or more host-language source files (C++, Python, Rust, Java, C#, Go,
  JavaScript) that the `.poly` file references.
- An `expected_output.txt` file — byte-exact runtime stdout used by the
  regression harness.
- Bilingual `README.md` (English) and `README_zh.md` (Chinese).

PolyglotCompiler 1.48.0 standardises the public name as **Poly**, the
language identifier as `poly`, and sample sources as `.poly`. The 1.x
toolchain still accepts one historical `.ploy` entry as a compatibility
fallback (through the end of 1.x at minimum), but new and migrated samples
must use `.poly`; generated diagnostics and metadata use `poly`.

The tracked harness `scripts/build_all_samples.sh` walks every folder, runs `polyc` then
`polyld`, executes the binary, captures stdout, and compares it byte-for-byte
against `expected_output.txt`.  Each sample is classified into one of:

- `OK` — stdout matches expected output.
- `OUTPUT_MISMATCH` — stdout differs from expected output.
- `EMPTY_STDOUT` — process exited 0 but produced no stdout.
- `RUN_FAIL` — produced binary failed at runtime.
- `LINK_FAIL` — `polyld` failed.
- `COMPILE_FAIL` — `polyc` failed.
- `SKIP` — sample folder lacked an accepted Poly entry.

The harness writes `build/samples_report.json` and exits 0 by default so the
report can document toolchain maturity without gating the build. Pass
`--fail-on-mismatch` to flip into strict gating mode.

## Directory matrix

| Folder | Languages | Theme | Description |
| --- | --- | --- | --- |
| `01_basic_linking/` | C++, Python | Cross-language linking | LINK / CALL / IMPORT / EXPORT basics (legacy comma-form `LINK(...)`). |
| `01_basic_linking_v2/` | C++, Python | Cross-language linking | Same as `01_basic_linking` but uses the recommended signed `LINK ... AS FUNC(...) -> ...` form. |
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
| `16_config_and_venv/` | Python, C# | Package import | Stringified CONFIG (since v1.12.0) + IMPORT PACKAGE + CONVERT. |
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
| `31_explicit_widths/` | Poly, C++ | Width-aware numeric types + CONST | Demonstrates `i32` / `u32` / `i64`, `TYPE` aliases and folded `CONST`. |
| `32_typed_handles/` | Poly, Python, C++ | Statically-typed cross-language handles | `CLASS` schemas + `HANDLE<lang::T>` for type-checked `NEW` / `METHOD` / `GET` / `SET`. |
| `33_pattern_matching/` | Poly | Pattern matching dispatch | Literals, ranges, OR-patterns, bindings, type guards, tuple / struct destructuring and `OPTION` constructors in a single MATCH. |
| `34_default_args/` | Poly | Named-parameter default values | Trailing parameters with constant defaults; positional / named / mixed call sites; pure-call defaults. |
| `35_extend_dynamic/` | Poly, Python | EXTEND restricted to dynamic hosts | EXTEND is accepted on python / ruby / javascript only; static-language targets get a sema fix-it. |
| `36_try_catch/` | Poly | Structured exception handling | TRY / CATCH / FINALLY / THROW with the built-in `Error` handle and the cross-language runtime bridge. |
| `37_async_await/` | Poly | Cooperative async / await | `ASYNC FUNC` + `AWAIT` driving the cooperative event loop in `runtime/services/async_bridge.cpp`. |
| `38_generics/` | Poly | Generic FUNC / STRUCT | Type parameters with bounds + WHERE clause; type-erased MVP lowering. |
| `39_visibility_attrs/` | Poly | PUB / PRIVATE + `@name` attributes | Module-boundary visibility, EXPORT-requires-PUB rule, built-in attribute catalog (`@inline`, `@hot`, `@deprecated`, ...). |
| `40_string_literals/` | Poly | Raw / multiline / template string literals | `r"..."`, `r#"..."#`, `"""..."""`, and `f"..."` interpolation; sema validates formattable types. |
| `41_grammar_polish/` | Poly | Optional parens on IF/WHILE/FOR, `IF LET Some(x)`, `///` doc comments | v1.18.0 P3 polish bundle; `polydoc` extracts doc blocks to Markdown/JSON. |

## By theme

- **Concurrency primitives** — `24_concurrency`
- **Control flow** — `03_pipeline`
- **Cross-language linking** — `01_basic_linking` (legacy form), `01_basic_linking_v2` (signed form, recommended)
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
- **Width-aware numeric types + CONST** — `31_explicit_widths`
- **Statically-typed cross-language handles** — `32_typed_handles`
- **Pattern matching dispatch** — `33_pattern_matching`
- **Named-parameter default values** — `34_default_args`
- **EXTEND restricted to dynamic hosts** — `35_extend_dynamic`
- **Structured exception handling** — `36_try_catch`
- **Cooperative async / await** — `37_async_await`
- **Generic FUNC / STRUCT** — `38_generics`
- **Visibility (PUB / PRIVATE) and attributes (@name)** — `39_visibility_attrs`
- **Extended string literals (raw / multiline / template)** — `40_string_literals`
- **Grammar polish (optional parens, IF LET, /// docs)** — `41_grammar_polish`

## By language combination

- **C#, C++, Java, Python, Rust** — `15_full_stack`
- **C#, Python** — `12_dotnet_interop`, `16_config_and_venv`
- **C++, Java** — `26_state_machine`
- **C++, Java, Python** — `13_generic_containers`
- **C++, Python** — `01_basic_linking`, `01_basic_linking_v2`, `02_type_mapping`, `03_pipeline`, `04_package_import`, `05_class_instantiation`, `06_attribute_access`, `07_resource_management`, `08_delete_extend`, `10_error_handling`, `19_file_io`, `27_plugin_system`
- **C++, Python, Rust** — `09_mixed_pipeline`, `14_async_pipeline`
- **C++, Rust** — `18_numeric_kernels`, `21_image_processing`, `24_concurrency`, `30_game_loop_demo`
- **Go, Python** — `23_http_client`
- **Java, Python** — `11_java_interop`, `20_json_pipeline`, `22_database_access`, `29_data_analytics`
- **JavaScript, Python** — `25_event_loop`
- **Python, Rust** — `17_string_processing`, `28_ml_inference`
- **Poly, C++** — `31_explicit_widths`
- **Poly, Python, C++** — `32_typed_handles`
- **Poly** — `33_pattern_matching`, `34_default_args`, `36_try_catch`, `37_async_await`, `38_generics`, `39_visibility_attrs`, `40_string_literals`, `41_grammar_polish`
- **Poly, Python** — `35_extend_dynamic`

## Build a single sample

```powershell
polyc 09_mixed_pipeline/mixed_pipeline.poly --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
./build/sample.exe
```

## Build every sample

```bash
./scripts/build_all_samples.sh
```

The integration test `samples_regression_test.cpp` (registered under the
`integration_tests` Catch2 binary, tag `[samples][b6]`) drives the harness
and asserts that the produced JSON report is well-formed.

Bilingual sibling: [README_zh.md](./README_zh.md).
