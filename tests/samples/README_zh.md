# `.ploy` 样例程序

本目录承载 PolyglotCompiler 的标准样例矩阵。每个子目录都包含：

- 一个演示某一特性主题的 `.ploy` 入口文件。
- 一个或多个宿主语言源文件（C++、Python、Rust、Java、C#、Go、JavaScript）。
- `expected_output.txt`——回归脚本用作字节对比基准的预期 stdout。
- 双语 README：`README.md`（英文）与 `README_zh.md`（中文）。

回归脚本 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）会遍历每个目录、依次运行 `polyc` 与
`polyld`，执行产物并捕获 stdout，按字节与 `expected_output.txt` 比对，把结
果归入下列状态之一：

- `OK`——stdout 与预期一致。
- `OUTPUT_MISMATCH`——stdout 与预期不符。
- `EMPTY_STDOUT`——进程返回 0 但未输出任何字节。
- `RUN_FAIL`——产物在运行期失败。
- `LINK_FAIL`——`polyld` 失败。
- `COMPILE_FAIL`——`polyc` 失败。
- `SKIP`——目录缺少 `.ploy` 入口。

脚本默认写出 `build/samples_report.json` 并以 0 退出，从而既能如实记录工
具链成熟度又不会阻塞构建。如需严格门禁，可改用 PowerShell 的
`-FailOnMismatch` 或 bash 的 `--fail-on-mismatch` 开关。

## 目录矩阵

| Folder | Languages | Theme | Description |
| --- | --- | --- | --- |
| `01_basic_linking/` | C++, Python | Cross-language linking | LINK / CALL / IMPORT / EXPORT 基础（旧的逗号形式 `LINK(...)`，已弃用）。 |
| `01_basic_linking_v2/` | C++, Python | Cross-language linking | 与 `01_basic_linking` 等价，但使用推荐的带签名形式 `LINK ... AS FUNC(...) -> ...`。 |
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
| `16_config_and_venv/` | Python, C# | Package import | 字符串化 CONFIG（v1.12.0 起） + IMPORT PACKAGE + CONVERT。 |
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
| `31_explicit_widths/` | Ploy, C++ | 显式宽度数值类型 + CONST | 演示 `i32` / `u32` / `i64`、`TYPE` 别名与折叠后的 `CONST`。 |
| `32_typed_handles/` | Ploy, Python, C++ | 静态类型化的跨语言句柄 | `CLASS` 模式 + `HANDLE<lang::T>`，对 `NEW` / `METHOD` / `GET` / `SET` 进行编译期类型检查。 |
| `33_pattern_matching/` | Ploy | 模式匹配派发 | 在同一个 MATCH 中演示字面量、范围、OR 模式、绑定、类型守卫、元组 / 结构体解构以及 `OPTION` 构造子。 |
| `34_default_args/` | Ploy | 命名参数与默认值 | 带常量默认值的尾部参数；位置 / 命名 / 混合调用点；以及以纯调用作为默认值。 |
| `35_extend_dynamic/` | Ploy, Python | EXTEND 仅限动态宿主 | EXTEND 只在 python / ruby / javascript 上被接受；静态语言目标会得到 sema 给出的修正提示。 |
| `36_try_catch/` | Ploy | 结构化异常处理 | TRY / CATCH / FINALLY / THROW 以及内建 `Error` 句柄与跨语言运行时桥。 |
| `37_async_await/` | Ploy | 协作式异步 / await | `ASYNC FUNC` + `AWAIT` 驱动 `runtime/services/async_bridge.cpp` 中的协作事件循环。 |
| `38_generics/` | Ploy | 泛型 FUNC / STRUCT | 带 bound 的类型参数与 WHERE 子句；类型擦除的 MVP 下沉。 |
| `39_visibility_attrs/` | Ploy | PUB / PRIVATE 与 `@name` 注解 | 模块边界可见性、EXPORT 要求 PUB 规则、内建属性目录（`@inline`、`@hot`、`@deprecated` 等）。 |
| `40_string_literals/` | Ploy | 原始 / 多行 / 模板字符串字面量 | `r"..."`、`r#"..."#`、`"""..."""` 与 `f"..."` 插值；sema 校验可格式化类型。 |
| `41_grammar_polish/` | Ploy | IF/WHILE/FOR 可选括号、`IF LET Some(x)`、`///` 文档注释 | v1.18.0 P3 收尾包；`polydoc` 抽取文档块为 Markdown/JSON。 |

## 按主题分组

- **Concurrency primitives**：`24_concurrency`
- **Control flow**：`03_pipeline`
- **Cross-language linking**：`01_basic_linking`（旧形式）、`01_basic_linking_v2`（带签名形式，推荐）
- **Data analytics**：`29_data_analytics`
- **Database access layer**：`22_database_access`
- **Diagnostics**：`10_error_handling`
- **Event loop simulation**：`25_event_loop`
- **Finite state machine**：`26_state_machine`
- **Full pipeline**：`09_mixed_pipeline`、`14_async_pipeline`、`15_full_stack`
- **Game loop skeleton**：`30_game_loop_demo`
- **HTTP client demo**：`23_http_client`
- **Image processing kernels**：`21_image_processing`
- **JSON ingest pipeline**：`20_json_pipeline`
- **ML inference pipeline**：`28_ml_inference`
- **Numeric kernels (BLAS-style)**：`18_numeric_kernels`
- **Object model**：`05_class_instantiation`、`06_attribute_access`、`07_resource_management`、`08_delete_extend`、`11_java_interop`、`12_dotnet_interop`
- **Package import**：`04_package_import`、`16_config_and_venv`
- **Plugin system**：`27_plugin_system`
- **Streaming file I/O**：`19_file_io`
- **String processing pipeline**：`17_string_processing`
- **Type mapping**：`02_type_mapping`、`13_generic_containers`
- **显式宽度数值类型 + CONST**：`31_explicit_widths`
- **静态类型化的跨语言句柄**：`32_typed_handles`
- **模式匹配派发**：`33_pattern_matching`
- **命名参数与默认值**：`34_default_args`
- **EXTEND 仅限动态宿主**：`35_extend_dynamic`
- **结构化异常处理**：`36_try_catch`
- **协作式异步 / await**：`37_async_await`
- **泛型 FUNC / STRUCT**：`38_generics`
- **可见性 PUB / PRIVATE 与属性 @name**：`39_visibility_attrs`
- **扩展字符串字面量（原始 / 多行 / 模板）**：`40_string_literals`
- **语法收尾（可选括号、IF LET、`///` 文档注释）**：`41_grammar_polish`

## 按语言组合分组

- **C#, C++, Java, Python, Rust**：`15_full_stack`
- **C#, Python**：`12_dotnet_interop`、`16_config_and_venv`
- **C++, Java**：`26_state_machine`
- **C++, Java, Python**：`13_generic_containers`
- **C++, Python**：`01_basic_linking`、`01_basic_linking_v2`、`02_type_mapping`、`03_pipeline`、`04_package_import`、`05_class_instantiation`、`06_attribute_access`、`07_resource_management`、`08_delete_extend`、`10_error_handling`、`19_file_io`、`27_plugin_system`
- **C++, Python, Rust**：`09_mixed_pipeline`、`14_async_pipeline`
- **C++, Rust**：`18_numeric_kernels`、`21_image_processing`、`24_concurrency`、`30_game_loop_demo`
- **Go, Python**：`23_http_client`
- **Java, Python**：`11_java_interop`、`20_json_pipeline`、`22_database_access`、`29_data_analytics`
- **JavaScript, Python**：`25_event_loop`
- **Python, Rust**：`17_string_processing`、`28_ml_inference`
- **Ploy, C++**：`31_explicit_widths`
- **Ploy, Python, C++**：`32_typed_handles`
- **Ploy**：`33_pattern_matching`、`34_default_args`、`36_try_catch`、`37_async_await`、`38_generics`、`39_visibility_attrs`、`40_string_literals`、`41_grammar_polish`
- **Ploy, Python**：`35_extend_dynamic`

## 构建单个样例

```powershell
polyc 09_mixed_pipeline/mixed_pipeline.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
./build/sample.exe
```

## 一次性构建全部样例

```powershell
# Windows
./scripts/build_all_samples.ps1

# POSIX
./scripts/build_all_samples.sh
```

集成测试 `samples_regression_test.cpp`（注册在 `integration_tests` 这一
Catch2 可执行文件下，标签 `[samples][b6]`）会驱动该脚本并断言生成的 JSON
报告结构合法。

英文版：[README.md](./README.md)。
