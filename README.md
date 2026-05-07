# PolyglotCompiler

<p align="center">
  <strong>A self-hosted multi-language compiler with cross-language interoperability</strong><br/>
  <strong>自举式多语言编译器，原生支持跨语言互操作</strong>
</p>

<p align="center">
  <img alt="C++20"    src="https://img.shields.io/badge/C%2B%2B-20-blue.svg"/>
  <img alt="CMake"    src="https://img.shields.io/badge/CMake-3.20+-green.svg"/>
  <img alt="License"  src="https://img.shields.io/badge/License-GPLv3-blue.svg"/>
<!-- BEGIN:test_badge -->
  <img alt="Tests"    src="https://img.shields.io/badge/CTest-30%20targets-brightgreen.svg"/>
<!-- END:test_badge -->
  <img alt="Version"  src="https://img.shields.io/badge/Version-1.45.2-informational.svg"/>
  <img alt="Platform" src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg"/>
</p>

---

## Overview / 项目概述

PolyglotCompiler compiles **C++**, **Python**, **Rust**, **Java**, **C# (.NET)**, **JavaScript**, **Ruby** and **Go** sources into a single SSA-form intermediate representation, then emits native code for **x86_64**, **ARM64** and **WebAssembly**. The **`.ploy`** domain-specific language sits on top as the cross-language glue layer that links functions, classes, methods, attributes and resources across language boundaries.

The toolchain is **self-hosted**: every supported language is parsed and lowered by a first-party frontend (`frontend_*`), the linker (`polyld`) emits ELF / PE32+ / Mach-O / Wasm directly, and only the optional system linker may be invoked at the very end of the pipeline.

PolyglotCompiler 将 **C++**、**Python**、**Rust**、**Java**、**C#（.NET）**、**JavaScript**、**Ruby** 与 **Go** 源代码编译为统一的 SSA 形式中间表示（IR），并为 **x86_64**、**ARM64**、**WebAssembly** 生成原生代码。**`.ploy`** 领域特定语言作为跨语言粘合层，描述函数、类、方法、属性、资源在语言边界之间的链接。

整条工具链是**自举的**：所有支持的语言都由项目自带的前端（`frontend_*`）解析与降级，链接器 `polyld` 直接生成 ELF / PE32+ / Mach-O / Wasm，仅在最终成品阶段可选地调用系统链接器。

### Key Features / 核心特性

- **9 first-party frontends** — `frontend_cpp`, `frontend_python`, `frontend_rust`, `frontend_java`, `frontend_dotnet`, `frontend_go`, `frontend_javascript`, `frontend_ruby`, `frontend_ploy`，统一由 `FrontendRegistry` 注册与分发。
- **Shared SSA IR** — 所有语言降级到同一份 IR，复用 `middle/` 目录下的 25+ 优化 Pass（含 PGO、LTO、循环优化、去虚化、GVN、DCE 等）。
- **Cross-Language Linking** — `.ploy` DSL 提供 `LINK` / `CALL` / `NEW` / `METHOD` / `GET` / `SET` / `WITH` / `DELETE` / `EXTEND` 等关键字，实现函数级与对象级互操作；签名/元数/类型在 `.ploy` 语义分析与 `polyld` 链接阶段双重校验。
- **Triple Backend** — `backends/x86_64`（含 SSE/AVX 调度）、`backends/arm64`（含 NEON）、`backends/wasm`（含影子栈、WAT/二进制双输出）。
- **Container Matrix** — `polyld` 直接生成 ELF（Linux）、PE32+（Windows）、Mach-O（macOS，arm64 段按 16 KiB 对齐、`__DATA_CONST` 携带 `SG_READ_ONLY`）与 Wasm；`tests/integration/binary_matrix/` 在 CI 守护整张矩阵。
- **Package Manager Integration** — `IMPORT … PACKAGE …` 自动识别 pip / conda / uv / pipenv / poetry / cargo / NuGet / Maven / Gradle / pkg-config / `go.mod` / `node_modules` / `package.json` / Gemfile。
- **Runtime System** — 4 种 GC 算法、FFI 桥、容器自适应 marshalling（dict 渐进式扩容）；按语言提供 `python_rt` / `cpp_rt` / `rust_rt` / `java_rt` / `dotnet_rt` / `go_rt` / `javascript_rt` / `ruby_rt` 运行时桥。
- **Plugin System** — 稳定 C ABI 插件接口，可扩展前端、Pass、后端、CLI 工具、IDE 面板、Linter / Formatter / Debugger / Completion。
- **Shared Frontend Token Pool** — 基于 `StringArena` 的 lexeme 存储、开放寻址 `IdentifierTable` 标识符内化、解析回溯快照、跨前端线程安全的 `SharedTokenPool`；`polyc --dump-token-pool` 可导出统计。
- **VS Code-style Settings** — 三层 JSON 设置（默认 / 用户 / `<workspace>/.polyglot/settings.json`），含 schema 校验、热重载、命令面板（`Ctrl+Shift+P`）、`keybindings.json`；同一份 `settings.json` 通过 `--settings` / `--print-effective-settings` 被所有 CLI 工具共享。
- **External Theme System** — VS Code 风格 `.polytheme.json` 主题（可附 `.qss`）按内置 qrc / 用户 / 工作区三层发现，5 套内置主题，提供图形 Theme Manager 与 `--theme` / `--list-themes` / `--validate-theme` / `--screenshot` CLI。
- **Debug Info** — 统一发射 DWARF 5、PDB（Windows）与 JSON source map。
- **Self-hosted Language Server** — `polyls`（stdio JSON-RPC）服务 polyui 与第三方编辑器，提供补全、诊断、跳转、符号索引；`test_polyls` / `test_lsp` / `test_completion_ranker` 在 CI 中守护。

---

## Architecture / 架构设计

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  Sources: C++ │ Python │ Rust │ Java │ C# (.NET) │ JavaScript │ Ruby │ Go │ .ploy│
└──────────────┬─────────────────────────────────────────────────────┬────────────┘
               ▼                                                     ▼
┌──────────────────────────────────────┐                  ┌────────────────────┐
│ Language Frontends (FrontendRegistry)│                  │   Ploy Frontend    │
│  cpp • python • rust • java • dotnet │                  │ (orchestration DSL)│
│  go  • javascript • ruby             │                  └─────────┬──────────┘
└───────────────────┬──────────────────┘                            │
                    ▼                                               ▼
              ┌───────────┐                                 ┌─────────────┐
              │ Shared IR │ ──── 25+ Optimisation Passes ──▶│  Polyglot   │
              │   (SSA)   │      (PGO • LTO • loop • …)    │   Linker    │
              └─────┬─────┘                                 └──────┬──────┘
        ┌───────────┼─────────────────┐                            │
        ▼           ▼                 ▼                            ▼
   ┌───────────┐ ┌───────────┐ ┌───────────┐                ┌─────────────┐
   │  x86_64   │ │   ARM64   │ │   WASM    │                │ FFI Glue +  │
   │  Backend  │ │  Backend  │ │  Backend  │                │ Marshalling │
   └─────┬─────┘ └─────┬─────┘ └─────┬─────┘                └──────┬──────┘
         └─────────────┼─────────────┘                             │
                       ▼                                           ▼
                ┌──────────────────────────────────────────────────────┐
                │  polyld → ELF / PE32+ / Mach-O / Wasm executable     │
                └──────────────────────────────────────────────────────┘
```

> **Important / 重要：** PolyglotCompiler 不依赖 MSVC / GCC / rustc / CPython / javac / dotnet / go / node / ruby 进行代码生成。`polyc` 驱动可选地调用系统链接器（`polyld` 或 `clang`）仅用于最终链接阶段；`polyld` 默认按相对 `polyc` 的可执行路径自动定位。

---

## Quick Start / 快速开始

### Prerequisites / 环境要求

- **C++20** compiler — MSVC 2022+, GCC 12+, Clang 15+, Apple Clang 15+
- **CMake** ≥ 3.20
- **Ninja** (recommended) or Make

All third-party C++ libraries (fmt ≥ 11.2.0, nlohmann/json, Catch2, mimalloc) are fetched automatically by CMake `FetchContent`.

**Optional / 可选：**
- **Qt 6 (recommended) / Qt 5.15+** — required only for the `polyui` desktop IDE.
  - CMake auto-discovers Qt under `D:\Qt` (Windows), `deps/qt/` (project-local), or the system path; pass `-DQT_ROOT=<path>` to override.
  - If Qt is missing, run `tools/ui/setup_qt.sh` (macOS/Linux) or `tools/ui/setup_qt.ps1` (Windows) to install pre-built Qt 6 via `aqtinstall`; otherwise the IDE target is silently skipped.

### Build / 构建

```bash
git clone https://github.com/user/PolyglotCompiler.git
cd PolyglotCompiler

# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# Build everything (compiler driver, tools, frontends, runtime, IDE)
cmake --build build

# Run the full CTest matrix (30 targets)
cd build && ctest --output-on-failure
```

**Windows (Visual Studio 2022+):**

```cmd
rem MSVC toolchain — MUST use -arch=amd64
call "C:\...\VsDevCmd.bat" -arch=amd64
cmake -B build -G Ninja
cmake --build build
cd build && ctest --output-on-failure
```

### Usage / 使用

```bash
# Compile a single source
polyc --lang=cpp -O2 -o output input.cpp

# Compile a .ploy cross-language specification
polyc --lang=ploy input.ploy

# Cross-target compilation
polyc --target=aarch64-apple-darwin --container=macho -o app main.cpp
polyc --target=wasm32-wasi          --container=wasm  -o app.wasm main.cpp

# Link, assemble, optimise
polyld -o program file1.o file2.o
polyasm input.s -o output.o
polyopt -O3 input.ir -o optimised.ir
```

---

## The .ploy Language / .ploy 跨语言链接语言

`.ploy` is the orchestration DSL that describes interoperability between every supported language. It exposes function calls, class instantiation, method invocation, attribute access, resource management (`WITH`), object destruction, class extension, and data-flow pipelines as first-class syntax.

### Example / 示例

```ploy
IMPORT python PACKAGE torch >= 2.0;
IMPORT python PACKAGE numpy >= 1.20 AS np;
IMPORT cpp::image_processing;

PIPELINE ml_pipeline {
    FUNC preprocess(path: STRING) -> LIST(f64) {
        LET raw    = CALL(cpp,    image_processing::load, path);
        LET tensor = CALL(python, np::array, raw);
        RETURN tensor;
    }

    FUNC train(data: LIST(f64)) -> INT {
        LET model     = NEW(python, torch::nn::Linear, 784, 10);
        LET optimizer = NEW(python, torch::optim::Adam,
                            METHOD(python, model, parameters), 0.001);

        LET output   = METHOD(python, model, forward, data);
        LET loss_val = METHOD(python, output, mean);
        METHOD(python, loss_val, backward);
        METHOD(python, optimizer, step);

        SET(python, model, training, FALSE);
        RETURN 0;
    }

    FUNC evaluate(model_path: STRING) -> FLOAT {
        LET f = NEW(python, open, model_path);
        WITH(python, f) AS handle {
            LET data = METHOD(python, handle, read);
            RETURN CALL(python, np::mean, data);
        }
    }
}

EXPORT ml_pipeline AS "train_model";
```

### Core Syntax / 核心语法

| Feature                | Syntax                                                 | Description                                  |
|------------------------|--------------------------------------------------------|----------------------------------------------|
| Function Link          | `LINK(cpp, python, f, g);`                             | Cross-language function binding              |
| Package Import         | `IMPORT python PACKAGE numpy >= 1.20;`                 | Import with version constraints              |
| Selective Import       | `IMPORT python PACKAGE torch::(tensor, no_grad);`      | Import specific symbols                      |
| Function Call          | `CALL(python, np::mean, data)`                         | Cross-language function call                 |
| Class Instantiation    | `NEW(python, torch::nn::Linear, 784, 10)`              | Create foreign class instance                |
| Method Call            | `METHOD(python, model, forward, data)`                 | Call method on foreign object                |
| Attribute Get / Set    | `GET(python, obj, attr)` / `SET(python, obj, k, v)`    | Read / write foreign attribute               |
| Resource Management    | `WITH(python, resource) AS r { ... }`                  | Auto `__enter__` / `__exit__`                |
| Object Destruction     | `DELETE(python, obj)`                                  | Destroy foreign object                       |
| Class Extension        | `EXTEND(python, Base) AS Derived { ... }`              | Cross-language class inheritance             |
| Type Conversion        | `CONVERT(value, FLOAT)`                                | Explicit type conversion                     |
| Type Mapping           | `MAP_TYPE(cpp::int, python::int);`                     | Cross-language type mapping                  |
| Pipeline               | `PIPELINE name { ... }`                                | Multi-stage processing pipeline              |
| Package Manager Config | `CONFIG CONDA "env_name";`                             | Configure package discovery                  |
| Type Annotation        | `LET model: python::nn::Module = NEW(...);`            | Qualified type annotations                   |

For the full grammar, semantics and 54 reserved keywords see [`docs/specs/ploy_language_spec.md`](docs/specs/ploy_language_spec.md) (English) and [`docs/specs/ploy_language_spec_zh.md`](docs/specs/ploy_language_spec_zh.md) (中文).

---

## Toolchain / 工具链

| Tool              | Binary       | Purpose                                                                                                                                |
|-------------------|--------------|----------------------------------------------------------------------------------------------------------------------------------------|
| Compiler Driver   | `polyc`      | Source → IR → native code. Supports `--progress=json`, `--clean-cache`, incremental cache, `--target` / `--container` cross-emit.       |
| Linker            | `polyld`     | Object → ELF / PE32+ / Mach-O / Wasm + cross-language glue; sets `0755` mode on POSIX; arm64 Mach-O uses 16 KiB alignment.              |
| Assembler         | `polyasm`    | Assembly → object file.                                                                                                                |
| Optimiser         | `polyopt`    | Standalone IR optimisation pipeline (`-O0` … `-O3`, PGO, LTO, custom Pass selection).                                                  |
| Runtime Tool      | `polyrt`     | GC tuning, FFI registration, thread management, runtime statistics dump.                                                               |
| Topology Analyser | `polytopo`   | Function I/O topology graph, link validation, text / DOT / JSON export, `--view-mode` and `--filter-language`.                          |
| Language Server   | `polyls`     | Self-hosted LSP over stdio JSON-RPC: completion, diagnostics, navigation, symbol index; consumed by `polyui` and any LSP-aware editor. |
| Doc Extractor     | `polydoc`    | Walks `.ploy` files, extracts `///` doc-comment blocks attached to top-level `FUNC` / `STRUCT` / `LET` / `VAR`; emits Markdown or JSON. |
| Toolchain Probe   | `polyver`    | Detects host toolchains (compilers, linkers, package managers) and writes the result into the toolchain database used by `polyc`.       |
| Benchmark         | `polybench`  | Performance evaluation suite (micro + macro, fast / full modes).                                                                       |
| IDE               | `polyui`     | Qt desktop IDE: highlighting, real-time diagnostics, Topology / Profiler (`Ctrl+Alt+P`) / Call Analyzer (`Ctrl+Alt+G`) panels, templates, build tasks. |

### Supported targets and binary containers / 支持的目标与容器

`polyc --target=<triple>` and `polyc --container=<auto|elf|pe|macho|wasm>` let one toolchain emit native binaries for every cell in the matrix. Without `--target` the driver falls back to `common::HostTriple()`; without `--container` the OS family in column 3 is used. A mismatch between the `-o` suffix and the resolved container raises `polyc-warn-W2101`.

| Triple                          | Container | Default exec suffix |
|---------------------------------|-----------|---------------------|
| `x86_64-pc-windows-msvc`        | PE32+     | `.exe`              |
| `aarch64-pc-windows-msvc`       | PE32+     | `.exe`              |
| `x86_64-unknown-linux-gnu`      | ELF       | (none)              |
| `aarch64-unknown-linux-gnu`     | ELF       | (none)              |
| `x86_64-apple-darwin`           | Mach-O    | (none)              |
| `aarch64-apple-darwin`          | Mach-O    | (none)              |
| `wasm32-wasi`                   | Wasm      | `.wasm`             |

`scripts/ci/run_binary_matrix.{sh,ps1}` exercises every cell in CI; `tests/integration/binary_matrix/` keeps the matrix honest in the regression suite.

---

## Plugin System / 插件系统

PolyglotCompiler exposes a stable C ABI plugin interface. Plugins are shared libraries (`polyplug_*.so` / `polyplug_*.dylib` / `polyplug_*.dll`) discovered automatically from the search paths or loaded manually.

| Capability      | Description                                                       |
|-----------------|-------------------------------------------------------------------|
| Language        | Add new frontends via `FrontendRegistry` / `ILanguageFrontend`    |
| Optimiser       | Add custom optimisation passes                                    |
| Backend         | Add new code-generation targets                                    |
| Tool            | Add CLI tools or pipeline stages                                  |
| UI Panel        | Add IDE panels / dock widgets                                     |
| Syntax Theme    | Add syntax highlighting themes                                    |
| File Type       | Register new file types                                           |
| Code Action     | Add quick-fix / refactoring actions                               |
| Formatter       | Add code formatters                                               |
| Linter          | Add code linters                                                  |
| Debugger        | Add debugger integrations                                         |
| Completion      | Add per-language editor completion providers                      |
| Diagnostic      | Add per-language editor diagnostic providers                      |
| Template        | Add file template providers for "New From Template"               |
| Topology Proc   | Add topology graph post-processors                                |

**Sandbox & version constraints / 沙箱与版本约束：** Plugins declare `min_host_version` in their metadata; the host enforces version compatibility at load time and rejects incompatible plugins. A circuit-breaker mechanism auto-disables a plugin after repeated callback failures (configurable via `SandboxPolicy`).

See [`docs/specs/plugin_specification.md`](docs/specs/plugin_specification.md) / [`docs/specs/plugin_specification_zh.md`](docs/specs/plugin_specification_zh.md) for the full specification.

---

## Project Structure / 项目结构

```
PolyglotCompiler/
├── frontends/
│   ├── common/         # Shared frontend infrastructure (token pool, preprocessor, diagnostics)
│   ├── cpp/            # C++ frontend (lexer, parser, sema, lowering, constexpr)
│   ├── python/         # Python frontend
│   ├── rust/           # Rust frontend
│   ├── java/           # Java frontend (Java 8 / 17 / 21 / 23)
│   ├── dotnet/         # .NET (C#) frontend (.NET 6 / 7 / 8 / 9)
│   ├── go/             # Go frontend
│   ├── javascript/     # JavaScript frontend
│   ├── ruby/           # Ruby frontend
│   └── ploy/           # .ploy cross-language frontend
├── middle/             # SSA IR, CFG, optimisation passes, PGO, LTO
├── backends/
│   ├── common/         # Shared backend (debug info, DWARF, PDB, object emission)
│   ├── x86_64/         # x86_64 backend (isel, regalloc, asm_printer, scheduler)
│   ├── arm64/          # ARM64 backend (isel, regalloc, asm_printer)
│   └── wasm/           # WebAssembly backend
├── runtime/
│   └── src/libs/       # python_rt, cpp_rt, rust_rt, java_rt, dotnet_rt, go_rt, javascript_rt, ruby_rt
├── common/             # Type system, symbol table, DWARF 5, host triple, settings
├── tools/              # polyc, polyld, polyasm, polyopt, polyrt, polytopo, polyls,
│                       # polydoc, polyver, polybench, ui (polyui)
├── tests/
│   ├── unit/           # Per-module unit tests (Catch2)
│   ├── integration/    # Full-pipeline / binary-matrix integration tests
│   ├── benchmarks/     # Micro + macro performance benchmarks
│   └── samples/        # 49 categorised sample programs (.ploy/.cpp/.py/.rs/.java/.cs/.go/.js/.rb)
├── scripts/            # CI helpers, packaging, sample harness, doc-sync gates
└── docs/               # Bilingual documentation (Chinese + English)
    ├── api/            # API reference
    ├── specs/          # Language & IR specifications, ABI, plugin spec
    ├── realization/    # Implementation details
    ├── tutorial/       # Tutorials (.ploy + project)
    └── demand/         # Demand log driving the project
```

---

## Testing / 测试

The project uses **Catch2** with a per-module CTest matrix of **30 targets**. The historical monolithic `unit_tests` binary is preserved as a meta-target while every logical area has its own binary so failures are easy to localise.

| #  | CTest target              | Scope                                                   |
|----|---------------------------|---------------------------------------------------------|
| 1  | `test_core`               | Common utilities, type system, symbol table             |
| 2  | `test_plugins`            | Plugin loader, sandbox, version contract                |
| 3  | `test_frontend_common`    | Token pool, preprocessor, diagnostics                   |
| 4  | `test_frontend_python`    | Python frontend                                         |
| 5  | `test_frontend_cpp`       | C++ frontend                                            |
| 6  | `test_frontend_rust`      | Rust frontend                                           |
| 7  | `test_frontend_ploy`      | `.ploy` frontend                                        |
| 8  | `test_frontend_java`      | Java frontend                                           |
| 9  | `test_frontend_dotnet`    | .NET (C#) frontend                                      |
| 10 | `test_frontend_javascript`| JavaScript frontend                                     |
| 11 | `test_frontend_ruby`      | Ruby frontend                                           |
| 12 | `test_frontend_go`        | Go frontend                                             |
| 13 | `test_middle`             | SSA IR, optimisation passes, PGO, LTO                   |
| 14 | `test_backends`           | x86_64 / ARM64 / Wasm code generation                   |
| 15 | `test_runtime`            | GC, FFI, marshalling, threading                         |
| 16 | `test_linker`             | `polyld` ELF / PE / Mach-O / Wasm writers, glue codegen |
| 17 | `test_topology`           | Topology graph, link validation                         |
| 18 | `test_settings`           | Three-layer settings, schema, hot reload                |
| 19 | `test_lsp`                | LSP framing, JSON-RPC, capability negotiation           |
| 20 | `test_polyls`             | `polyls` server: completion, navigation, symbol index   |
| 21 | `test_problems`           | Diagnostic surfacing, problems panel contract           |
| 22 | `test_completion_ranker`  | Completion ranking heuristics                           |
| 23 | `test_topology_ui`        | Topology panel UI bindings                              |
| 24 | `test_e2e`                | End-to-end pipelines                                    |
| 25 | `unit_tests`              | Aggregate compatibility binary                          |
| 26 | `integration_tests`       | Full pipeline & cross-language interop                  |
| 27 | `benchmark_tests`         | Benchmark suite                                         |
| 28 | `samples_regression`      | Drives `tests/samples/**` through `polyc` + `polyld`    |
| 29 | `benchmark_fast`          | Fast (`[fast]`) Catch2 subset for CI smoke              |
| 30 | `benchmark_full`          | Full benchmark run                                      |

```bash
cd build && ctest --output-on-failure              # run everything
cd build && ctest -R test_frontend_ploy            # one target
cd build && ./test_frontend_ploy [parser]          # by Catch2 tag
cd build && ctest -L benchmark                     # by label
```

### CI Quality Gates / CI 质量闸门

`.github/workflows/ci.yml` enforces the following gates on every push and PR:

| Gate              | Tool                                          | Description                                                                                              |
|-------------------|-----------------------------------------------|----------------------------------------------------------------------------------------------------------|
| Docs Lint         | `docs_lint.py` / `docs_sync_check.py`         | Path references, core bilingual doc sync (`USER_GUIDE`, API), heading structure, version consistency     |
| Format Check      | `clang-format-17`                             | Enforces `.clang-format` style on all C/C++ source files                                                 |
| Static Analysis   | `clang-tidy-17`                               | Runs across all `.cpp` sources under `common/middle/frontends/backends/runtime/tools` (no 50-file cap)   |
| Sanitizers        | ASan + UBSan                                  | Runs all non-benchmark suites (`-LE benchmark`)                                                          |
| Code Coverage     | `lcov` / `gcov`                               | Line coverage from non-benchmark tests; report uploaded as CI artifact                                   |
| Benchmark Smoke   | `benchmark_fast` (`[fast]`)                   | Catches obvious performance regressions                                                                  |
| Binary Matrix     | `scripts/ci/run_binary_matrix.{sh,ps1}`       | Validates the `--target` × `--container` matrix on Linux / macOS / Windows runners                       |
| Samples Harness   | `samples_regression` + `build_all_samples.*`  | Drives `tests/samples/**` and pins stdout via `expected_output.txt`; honours `expected_output.skip`      |

Local invocation:

```bash
# Format check (dry-run)
find common middle frontends backends runtime tools \
  -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) \
  -not -path '*/deps/*' -not -path '*/.cache/*' -print0 \
  | xargs -0 clang-format --dry-run --Werror --style=file

# Bilingual doc sync gate
python scripts/docs_sync_check.py --ci --scope core

# Sanitizer build
cmake -B build-san -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DPOLYGLOT_ENABLE_ASAN=ON \
  -DPOLYGLOT_ENABLE_UBSAN=ON \
  -DBUILD_SHARED_LIBS=OFF
cmake --build build-san
cd build-san && ctest --output-on-failure -LE benchmark
```

---

## Dependencies / 依赖

Managed automatically via CMake `FetchContent`:

<!-- BEGIN:dependencies_table -->
| Dependency                                              | Purpose                                                                                                  |
|---------------------------------------------------------|----------------------------------------------------------------------------------------------------------|
| [fmt](https://github.com/fmtlib/fmt)                    | Formatted output (≥ 11.2.0 required for Apple Clang 21)                                                   |
| [nlohmann/json](https://github.com/nlohmann/json)       | JSON processing                                                                                          |
| [Catch2](https://github.com/catchorg/Catch2)            | Unit testing framework                                                                                   |
| [mimalloc](https://github.com/microsoft/mimalloc)       | High-performance memory allocator (`mi_*` APIs only; global malloc override disabled across dylib bounds) |
<!-- END:dependencies_table -->

**Optional / 可选（不由 CMake 拉取，需预先安装）：**

| Dependency                                       | Purpose                                                                                                                                  |
|--------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------|
| [Qt 6 (recommended) / Qt 5.15+](https://www.qt.io/) | Desktop IDE (`polyui`). Auto-discovered under `D:\Qt`, `deps/qt/`, or system path. Run `tools/ui/setup_qt.{sh,ps1}` to install via `aqtinstall`; skipped if not found. |

---

## Documentation / 文档

All documentation is bilingual (中文 + English) under [`docs/`](docs/):

| Document                                                              | Description                                                                |
|-----------------------------------------------------------------------|----------------------------------------------------------------------------|
| [`USER_GUIDE.md`](docs/USER_GUIDE.md) / [`USER_GUIDE_zh.md`](docs/USER_GUIDE_zh.md) | Complete user guide                                                       |
| [`CHANGELOG.md`](docs/CHANGELOG.md) / [`CHANGELOG_zh.md`](docs/CHANGELOG_zh.md)     | Release history (every version since v0.1.0)                              |
| [`docs/api/`](docs/api/)                                              | API reference                                                              |
| [`docs/specs/`](docs/specs/)                                          | Language & IR specifications, runtime ABI, plugin specification, packaging |
| [`docs/realization/`](docs/realization/)                              | Implementation details                                                     |
| [`docs/tutorial/`](docs/tutorial/)                                    | Tutorials for the `.ploy` language and the project as a whole              |

---

## Release Packaging / 发布打包

Pre-built release bundles can be assembled with the scripts under [`scripts/`](scripts/):

```bash
# Windows (PowerShell) — portable zip + NSIS installer
.\scripts\package_windows.ps1

# Linux — portable tar.gz
./scripts/package_linux.sh

# macOS — portable tar.gz with .app bundle
./scripts/package_macos.sh
```

Full details in [`docs/specs/release_packaging.md`](docs/specs/release_packaging.md) / [`docs/specs/release_packaging_zh.md`](docs/specs/release_packaging_zh.md).

---

## License / 许可证

This project is licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE) for details.  
本项目遵循 **GNU 通用公共许可证 v3.0**，详见 [LICENSE](LICENSE)。

---

<!-- BEGIN:version_footer_en -->
*Maintained by the PolyglotCompiler Team*  
*Last Updated: 2026-05-07*  
*Document Version: v1.45.2*
<!-- END:version_footer_en -->
