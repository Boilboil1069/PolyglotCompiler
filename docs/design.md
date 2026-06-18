# PolyglotCompiler Design Specification

## 1. Document Control

| Field | Value |
|---|---|
| Document type | Kiro spec-style repository design |
| Scope | Entire PolyglotCompiler workspace |
| Audience | Requirements analysis, architecture review, implementation planning, QA, release owners |
| Source baseline | Current workspace under `/Volumes/extend/PolyglotCompiler` |
| Build system | CMake 3.20+, C++20, CTest |
| Product version source | Root `CMakeLists.txt`, generated `common/include/version.h`, `VERSION.txt` |
| Completion state | This document represents the repository design after reviewing code, build files, tests, samples, scripts, and documentation |

## 2. Product Definition

PolyglotCompiler is a self-hosted multi-language compiler, linker, runtime, language server, IDE, and sample/test suite. It parses first-party language frontends, lowers supported languages into a shared SSA-oriented IR, applies middle-end optimization, emits x86_64, ARM64, or WebAssembly artifacts, and packages final executables or libraries through the project linker.

The `.ploy` language is the cross-language orchestration layer. It describes imports, package constraints, virtual environment configuration, function-level linking, cross-language calls, class construction, method calls, attribute access, resource management, type mapping, control flow, async flow, error handling, and pipeline exports.

### 2.1 Product Goals

- Provide a single compilation architecture for C++, Python, Rust, Java, .NET, JavaScript, Ruby, Go, and `.ploy`.
- Keep source parsing and lowering inside first-party frontends rather than relying on external compilers as the primary implementation path.
- Validate cross-language signatures, arity, types, package constraints, ABI compatibility, and runtime bridge requirements before final packaging.
- Emit native and portable targets through x86_64, ARM64, and WebAssembly backends.
- Produce ELF, PE32+, Mach-O, Wasm, static library, and shared library outputs.
- Reuse diagnostics across CLI, JSON/progress output, `polyls`, `polyui`, tests, and documentation examples.
- Keep all extension surfaces explicit: frontend registry, backend registry, plugin C ABI, runtime C ABI, package manager registry, settings schema, LSP capability registry, and UI panel services.

### 2.2 Non-Goals

- `.ploy` is not a replacement for every supported language; it is the interoperability and orchestration language.
- `polyui` must not bypass compiler services by directly depending on parser internals for user-visible behavior.
- `polyld` and backend stages must not invent fallback symbols, synthetic object sections, or successful outputs when required compiler artifacts are missing.
- Plugin APIs must not expose unstable C++ implementation ABI as the host contract.

## 3. Workspace Inventory

### 3.1 Top-Level Directories

| Path | Responsibility | Main Contents |
|---|---|---|
| `common/` | Shared compiler foundation | Types, symbols, source locations, target triples, binary containers, debug utilities, plugin API and manager |
| `middle/` | Shared IR and optimization | IR context, CFG, SSA, verifier, parser/printer, data layout, templates, optimization passes, PGO, LTO |
| `frontends/` | First-party language frontends | Common frontend utilities plus C++, Python, Rust, `.ploy`, Java, .NET, JavaScript, Ruby, Go |
| `backends/` | Code generation | Backend registry, ABI helpers, object builders, debug emitters, x86_64, ARM64, Wasm |
| `runtime/` | Runtime services and interop | GC, allocation, FFI, marshalling, object lifecycle, language runtime bridges, async/error/reflection/threading/profile services |
| `tools/` | User-facing and internal tools | `polyc`, `polyld`, `polyasm`, `polyopt`, `polyrt`, `polyver`, `polydoc`, `polytopo`, `polyls`, `polybench`, `polyui` |
| `tests/` | Verification assets | Unit tests, integration tests, benchmarks, fixtures, categorized samples |
| `docs/` | Project documentation | API docs, specs, realization notes, tutorials, changelog, user guide, demand log, design docs |
| `scripts/` | Automation | Docs checks, sample builds, CI binary matrix, packaging scripts, dependency fetching |
| `deps/` | Local optional dependencies | Project-local Qt installation |
| `.vscode/`, `.idea/`, `.claude/` | Local/tooling metadata | Editor and assistant configuration |

### 3.2 Build and Configuration Assets

| Asset | Role |
|---|---|
| `CMakeLists.txt` | Project definition, C++ standard, version, shared library policy, sanitizer/coverage flags, subdirectory order |
| `Dependencies.cmake` | FetchContent dependency setup for fmt, nlohmann/json, Catch2, mimalloc |
| `VERSION.txt` | Generated plain-text version artifact for packaging |
| `LICENSE` | GPLv3 license |
| `tools/fixup_macos_bundle.cmake` | macOS `.app` dylib install-name fixup for `polyui` |
| `scripts/installer.nsi` | Windows NSIS installer definition |

## 4. Build Targets

### 4.1 Library Targets

| Target | Source Layer | Purpose |
|---|---|---|
| `polyglot_common` | `common/` | Shared type system, symbol table, plugin manager, debug helpers, target/container utilities |
| `middle_ir` | `middle/` | IR, SSA, optimizer, PGO, LTO |
| `frontend_common` | `frontends/common/` | Token pool, string arena, identifier table, lexer base, preprocessor, registry, language versions |
| `frontend_python` | `frontends/python/` | Python lexer/parser/sema/lowering/frontend and `.pyi` loader |
| `frontend_cpp` | `frontends/cpp/` | C++ lexer/parser/sema/lowering/frontend and constexpr support |
| `frontend_rust` | `frontends/rust/` | Rust lexer/parser/sema/lowering/frontend and crate loader |
| `frontend_ploy` | `frontends/ploy/` | `.ploy` lexer/parser/sema/lowering/frontend, package discovery, config registry |
| `frontend_java` | `frontends/java/` | Java lexer/parser/sema/lowering/frontend and class file reader |
| `frontend_dotnet` | `frontends/dotnet/` | .NET lexer/parser/sema/lowering/frontend and metadata reader |
| `frontend_javascript` | `frontends/javascript/` | JavaScript lexer/parser/sema/lowering/frontend and import resolver |
| `frontend_ruby` | `frontends/ruby/` | Ruby lexer/parser/sema/lowering/frontend and import resolver |
| `frontend_go` | `frontends/go/` | Go lexer/parser/sema/lowering/frontend and import resolver |
| `backend_common` | `backends/common/` | Backend registry, target interface, ABI, object file builders, debug emission |
| `backend_x86_64` | `backends/x86_64/` | x86_64 machine IR, instruction selection, scheduling, calling convention, register allocation |
| `backend_arm64` | `backends/arm64/` | ARM64 machine IR, instruction selection, scheduling, calling convention, register allocation |
| `backend_wasm` | `backends/wasm/` | Wasm lowering, type mapping, section emission, LEB128 encoding, WAT printing |
| `runtime` | `runtime/` | Runtime C ABI, GC, interop, bridges, services, allocation |
| `polyglot_tools_settings` | `tools/common/` | Three-layer settings loader for CLI tools and `polyui` |
| `polyc_lib` | `tools/polyc/` | Shared staged compiler pipeline used by driver and tests |
| `linker_lib` | `tools/polyld/` | Linker core and container writers shared by `polyld` and tests |
| `lsp_lib` | `tools/ui/common/lsp/` | Qt-free JSON-RPC and LSP client/session infrastructure |
| `polyls_core` | `tools/polyls/` | Language server core features, grammar descriptor, symbol index |
| `topo_lib` | `tools/polytopo/` | Topology graph analysis, validation, printing, code generation |

### 4.2 Executable Targets

| Target | Role | Key Dependencies |
|---|---|---|
| `polyc` | Compiler driver | `polyc_lib`, settings |
| `polyld` | Linker and container writer | `linker_lib`, common, `.ploy` frontend, settings |
| `polyasm` | Assembly-to-object tool | Backends and middle IR |
| `polyopt` | Standalone IR optimizer | Middle IR and backends |
| `polyrt` | Runtime launcher/control utility | Runtime, common, settings |
| `polyver` | Toolchain/package-manager detector | Common, JSON |
| `polydoc` | `.ploy` doc-comment extractor | `.ploy` frontend, common |
| `polyls` | Self-hosted language server | `polyls_core` |
| `polybench` | Performance benchmark runner | Frontends, backends, runtime, settings |
| `polytopo` | Topology analysis CLI | `topo_lib`, common, `.ploy` frontend, settings |
| `polyui` | Qt desktop IDE | Frontends, backends, runtime, topology, settings, LSP, language server core |
| `pe_smoke` | PE writer harness | `linker_lib`, common |

## 5. Architecture

### 5.1 Dependency Direction

```text
polyglot_common
  -> middle_ir
  -> frontend_common
  -> frontend_<language>
  -> backend_common -> backend_<target>
  -> runtime
  -> linker_lib
  -> polyc_lib
  -> tools/tests
```

Top-level CMake includes subdirectories in this order: `common`, `middle`, `frontends`, `backends`, `runtime`, `tools`, `tests`. This is the architectural dependency rule. Reverse dependencies should be treated as design defects unless they are isolated test-only code.

### 5.2 End-to-End Compilation Flow

```text
Source files and .ploy orchestration
  -> language detection or explicit --lang
  -> frontend registry dispatch
  -> lexer/parser/sema/lowering
  -> SemanticDatabase
  -> MarshalPlan for cross-language boundaries
  -> BridgeGenerationOutput and descriptor metadata
  -> IR verification and optimization
  -> target backend emission
  -> object/container packaging
  -> runtime bridge resolution
  -> final executable, Wasm module, static library, or shared library
```

### 5.3 Runtime Execution Flow

```text
Compiled entry point
  -> generated bridge stubs when crossing language boundaries
  -> runtime C ABI for allocation, GC roots, strings, containers, exceptions, async, and profiles
  -> language bridge tables for Python/C++/Rust/Java/.NET/Go/JavaScript/Ruby
  -> profile/call-trace sinks for CLI, files, or UI panels
```

## 6. Core Compiler Components

### 6.1 Common Core

| Area | Files | Design Responsibility |
|---|---|---|
| Types and symbols | `common/include/core/types.h`, `common/include/core/symbols.h`, `common/src/core/type_system.cpp`, `common/src/core/symbol_table.cpp` | Shared type and symbol representation used by frontends, IR, diagnostics, and linker metadata |
| Source/config | `common/include/core/source_loc.h`, `common/include/core/config.h` | Stable source positions and shared configuration structures |
| Target/container | `common/include/target_triple.h`, `common/include/binary_container.h` | Target triple parsing, host triple detection, output container selection |
| Debug | `common/include/debug/`, `common/src/debug/dwarf5.cpp` | Debug metadata builder/adapters and DWARF 5 support |
| Plugins | `common/include/plugins/plugin_api.h`, `common/include/plugins/plugin_manager.h`, `common/src/plugins/plugin_manager.cpp` | Stable plugin C ABI and host-side plugin loading |
| Utilities | `common/include/utils/` | Arena, hashing, logging, string pool |
| Version | `common/include/version.h`, `common/include/version.h.in` | Generated product version contract |

### 6.2 Frontend Common

| Component | Files | Contract |
|---|---|---|
| Diagnostics | `frontends/common/include/diagnostics.h` | Shared parse/sema diagnostics with source location and severity |
| Registry | `frontend_registry.h/.cpp` | Thread-safe frontend registration, name/alias/extension lookup, language detection |
| Frontend interface | `language_frontend.h` | Common frontend adapter surface |
| Lexer/parser base | `lexer_base.h/.cpp`, `parser_base.h` | Shared token and parser infrastructure |
| Preprocessor | `preprocessor.h/.cpp` | Shared preprocessing layer |
| Token storage | `token_pool.h/.cpp`, `string_arena.h/.cpp`, `identifier_table.h/.cpp` | Interning and memory-efficient token/identifier management |
| Language versions | `language_versions.h/.cpp` | Version gating and per-language feature control |
| Sema context | `sema_context.h` | Common semantic context primitives |

### 6.3 Language Frontend Matrix

| Language | Library | File Extensions | Implementation Units | Special Scope |
|---|---|---|---|---|
| C++ | `frontend_cpp` | `.cpp`, `.cxx`, `.cc`, `.h`, `.hpp` | AST, lexer, parser, sema, lowering, frontend, constexpr | C++ language model and constant expression support |
| Python | `frontend_python` | `.py`, `.pyi` | AST, lexer, parser, sema, lowering, frontend, advanced features, `.pyi` loader | Python 3.8+ style feature gates and stub loading |
| Rust | `frontend_rust` | `.rs` | AST, lexer, parser, sema, lowering, frontend, advanced features, crate loader | Rust editions, crate discovery, borrow/type checks |
| `.ploy` | `frontend_ploy` | `.ploy` | AST, lexer, parser, sema, lowering, frontend, package cache, command runner, package indexer, config registry | Cross-language orchestration, package discovery, bridge descriptors |
| Java | `frontend_java` | `.java`, class metadata | AST, lexer, parser, sema, lowering, frontend, class file reader | Java 8/17/21/23 feature handling and class metadata |
| .NET | `frontend_dotnet` | `.cs`, `.vb`, metadata | AST, lexer, parser, sema, lowering, frontend, metadata reader | .NET 6/7/8/9 metadata and language feature gates |
| JavaScript | `frontend_javascript` | `.js` | AST, lexer, parser, sema, lowering, frontend, import resolver | ES feature parsing and module import resolution |
| Ruby | `frontend_ruby` | `.rb` | AST, lexer, parser, sema, lowering, frontend, import resolver | Ruby import and dynamic language interop model |
| Go | `frontend_go` | `.go` | AST, lexer, parser, sema, lowering, frontend, import resolver | Go module import and generic feature handling |

### 6.4 `.ploy` Language Capability Inventory

| Capability | Design Contract |
|---|---|
| Function linking | `LINK` declarations define cross-language function bindings and signatures |
| Function calls | `CALL(language, symbol, args...)` creates explicit cross-language call sites |
| Object construction | `NEW(language, class, args...)` constructs foreign objects and returns typed handles when schema is known |
| Method invocation | `METHOD(language, object, method, args...)` invokes foreign methods |
| Attribute access | `GET` and `SET` map to foreign attribute/property access |
| Resource management | `WITH` models enter/exit resource lifetimes |
| Object deletion | `DELETE` maps to language-specific release/drop/dispose hooks |
| Type mapping | `MAP_TYPE`, `MAP_FUNC`, `CONVERT`, and typed handles map data across languages |
| Packages | `IMPORT ... PACKAGE`, version constraints, selective imports, package manager config |
| Config | venv, conda, uv, pipenv, poetry, cargo, npm, maven, gradle, NuGet, bundler, Go module style configs |
| Control flow | `IF`, `ELSE`, `WHILE`, `FOR`, `MATCH`, `CASE`, `BREAK`, `CONTINUE` |
| Error flow | `TRY`, `CATCH`, `FINALLY`, `THROW`, `ERROR`, option-style propagation |
| Async flow | `ASYNC`, `AWAIT`, runtime async bridge |
| Visibility/attributes | `PUB`, `PRIVATE`, annotation catalog |
| Literals and types | Strings, raw strings, template strings, numeric widths, containers, structs, generics |

### 6.5 Middle-End

| Area | Files | Design Responsibility |
|---|---|---|
| IR ownership | `middle/include/ir/ir_context.h`, `middle/src/ir/ir_context.cpp` | Functions, globals, dialects, data layout |
| IR construction | `ir_builder.h`, `builder.cpp` | Programmatic IR creation |
| CFG and SSA | `cfg.h/.cpp`, `ssa.h/.cpp` | Basic blocks, control flow, SSA conversion |
| IR parser/printer | `ir_parser.h`, `ir_printer.h`, `parser.cpp`, `printer.cpp` | Textual IR roundtrip and tooling |
| Analysis | `analysis.h/.cpp`, `passes/analysis/alias.h`, `passes/analysis/dominance.h` | Data/control-flow analysis |
| Verification | `verifier.h/.cpp` | IR structural validation before backend emission |
| Templates/classes | `template_instantiator.h/.cpp`, `class_metadata.h` | Generic and class metadata support |
| Function passes | `middle/include/ir/passes/opt.h` | Constant fold, DCE, copy propagation, CFG canonicalization, phi simplification, CSE, Mem2Reg |
| Transform passes | `middle/include/passes/transform/` | Constant fold, DCE, CSE, inlining, devirtualization, loops, GVN, advanced opts, call trace instrumentation |
| PGO | `middle/include/pgo/profile_data.h`, `middle/src/pgo/profile_data.cpp` | Profile data model |
| LTO | `middle/include/lto/link_time_optimizer.h`, `middle/src/lto/link_time_optimizer.cpp` | Cross-module optimization |

### 6.6 Backend System

| Backend Area | Files | Contract |
|---|---|---|
| Target interface | `backends/common/include/target_backend.h` | Reentrant `ITargetBackend`, options, capabilities, artifacts, diagnostics |
| Registry | `backend_registry.h/.cpp` | Target lookup by triple/alias and capability discovery |
| ABI | `abi.h`, `abi/`, `relocation.h`, `target_machine.h` | Calling conventions, relocations, target machine metadata |
| Object builders | `object_file.h/.cpp` | ELF, COFF, Mach-O object construction support |
| Debug emission | `debug_info.h/.cpp`, `debug_emitter.h/.cpp`, `dwarf_builder.h/.cpp` | DWARF/debug info emission |
| x86_64 | `backends/x86_64/` | x86 target backend, registers, Machine IR, instruction selection, optimizations, scheduling, regalloc |
| ARM64 | `backends/arm64/` | ARM64 target backend, registers, Machine IR, instruction selection, scheduling, regalloc |
| Wasm | `backends/wasm/` | Wasm target, backend, type mapping, function/instruction lowering, sections, LEB128, WAT |

Backend outputs must include sections, relocations, symbols, unresolved symbols, object bytes when applicable, assembly/bitcode when requested, diagnostics, and per-stage timing statistics.

### 6.7 Runtime System

| Runtime Area | Files | Contract |
|---|---|---|
| GC | `runtime/include/gc/`, `runtime/src/gc/` | Mark-sweep, generational, copying, incremental strategies, root guards, heap/runtime APIs |
| Allocation | `runtime/include/memory/polyglot_alloc.h`, `runtime/src/memory/polyglot_alloc.cpp` | Runtime allocation surface |
| Interop | `runtime/include/interop/`, `runtime/src/interop/` | FFI, memory, marshalling, type mapping, calling conventions, container marshalling, object lifecycle |
| Base C ABI | `runtime/include/libs/base.h`, `runtime/src/libs/base.c`, `base_gc_bridge.cpp` | Stable generated-code runtime calls |
| Language bridges | `cpp_rt`, `python_rt`, `rust_rt`, `java_rt`, `dotnet_rt`, `go_rt`, `javascript_rt`, `ruby_rt` | Language-specific bridge entry tables and lifecycle functions |
| Services | `runtime/include/services/`, `runtime/src/services/` | Exception, error bridge, async bridge, event loop, reflection, threading, call trace, profile sink |
| Platform shim | `runtime/include/polyrt_linux.h`, `runtime/src/libs/polyrt_linux.c` | Linux runtime entry support |

Runtime-generated-code contract: generated code calls C-linkage symbols and treats foreign objects as opaque handles unless a marshalling rule defines a concrete layout.

## 7. Compiler Pipeline Data Contracts

`tools/polyc/include/compilation_pipeline.h` is the primary pipeline contract.

| Stage | Input | Output | Critical Fields |
|---|---|---|---|
| Frontend | `CompilationContext::Config` | `FrontendOutput` | AST, source file, language, token stream, parse diagnostics, source mtime, success |
| Semantic | `FrontendOutput` | `SemanticDatabase` | Validated AST, symbols, signatures, class schemas, link entries, type mappings, packages, venv configs, sema diagnostics |
| Marshal Plan | `SemanticDatabase` | `MarshalPlan` | Per-call marshal plans, helper dependencies, plan diagnostics |
| Bridge Generation | `MarshalPlan` and `SemanticDatabase` | `BridgeGenerationOutput` | Generated stubs, exported symbols, runtime dependencies, descriptor metadata JSON, diagnostics |
| Backend | Semantics, bridges, target config | `BackendOutput` | Compiled objects, target arch/OS/triple, diagnostics, assembly text |
| Packaging | `BackendOutput`, config | `PackagingOutput` | Binary data, format, path, entry point, needed libraries, exported symbols, diagnostics, checksum |

### 7.1 Configuration Surface

`CompilationContext::Config` includes source file/text/language, output file, target arch/OS/triple, container, subsystem, entry symbol, mode, object format, linker path, optimization level, strict/force flags, aux dir, package indexing, IR/ASM/object emission paths, additional libraries, include/system include paths, defines, Python stub paths, Java classpath, .NET references, Rust crate settings, and `.ploy` descriptor path.

### 7.2 Marshal Strategies

| Strategy | Use Case |
|---|---|
| `kDirectCopy` | ABI-compatible primitive or identical representation |
| `kIntToFloat` | Integer-to-floating conversion |
| `kFloatToInt` | Floating-to-integer conversion |
| `kStringEncode` | UTF/string representation conversion |
| `kContainerCopy` | List/tuple/dict/container conversion |
| `kPointerToHandle` | Native pointer wrapped as runtime handle |
| `kStructFieldByField` | Structured data conversion |
| `kOpaquePtr` | Opaque pointer pass-through |

## 8. Linker and Packaging Design

### 8.1 Linker Components

| Component | Files | Responsibility |
|---|---|---|
| Core linker | `tools/polyld/include/linker.h`, `tools/polyld/src/linker.cpp` | Symbols, sections, relocations, generic link flow |
| ELF writer | `linker_elf.h/.cpp` | Linux ELF container emission |
| PE writer | `linker_pe.h/.cpp`, `pe_writer.h/.cpp` | Windows PE32+ emission and writer hardening |
| Mach-O writer | `linker_macho.h/.cpp` | macOS Mach-O emission |
| Wasm linker | `linker_wasm.h/.cpp` | WebAssembly linking |
| Polyglot linker | `polyglot_linker.h/.cpp` | Cross-language symbol resolution, ABI validation, glue stub generation, descriptor loading |
| CLI | `tools/polyld/src/main.cpp` | User-facing linker driver |

### 8.2 Cross-Language Linker Contract

`PolyglotLinker` accepts `.ploy` call descriptors, validated link entries, cross-language symbols, descriptor files, and aux directory discovery. It resolves language-symbol pairs, validates ABI compatibility, generates glue stubs, emits relocations, tracks errors/warnings, and exposes generated stubs to the main linker.

Hard-failure conditions:

- Required symbol cannot be found.
- ABI mismatch cannot be adapted.
- Descriptor file is required but missing or invalid.
- Marshalling helper relocation is required but not emitted.
- Duplicate strong symbols cannot be resolved.
- Target/container mismatch is not explicitly permitted.

## 9. Tool Suite Design

| Tool | Primary Inputs | Primary Outputs | Design Responsibility |
|---|---|---|---|
| `polyc` | Source files, `.ploy`, CLI flags, settings | Objects, aux artifacts, binaries/libraries, diagnostics | Full compilation pipeline driver |
| `polyld` | Objects, descriptor files, libraries | ELF/PE/Mach-O/Wasm outputs | Linking and container emission |
| `polyasm` | Assembly | Object files | Assembly path for backend artifacts |
| `polyopt` | Textual IR | Optimized IR | Standalone middle-end testing and optimization |
| `polyrt` | Runtime commands/config | Runtime actions/statistics | Runtime utility |
| `polyver` | Host environment | Toolchain database | Toolchain/package manager discovery |
| `polydoc` | `.ploy` source | Markdown/JSON docs | Doc-comment extraction |
| `polytopo` | `.ploy` and descriptors | Text/DOT/JSON topology | Cross-language topology analysis |
| `polyls` | LSP JSON-RPC | LSP responses/notifications | Editor integration and diagnostics |
| `polybench` | Bench suites/settings | Benchmark results | Performance validation |
| `polyui` | Workspace/project files | IDE UX, panels, diagnostics | Desktop IDE |

## 10. IDE, LSP, and Tooling UI Design

### 10.1 Language Server

| Area | Files | Responsibility |
|---|---|---|
| Server lifecycle | `tools/polyls/polyls_core/polyls_server.h/.cpp` | initialize, shutdown, exit, document sync |
| Feature dispatch | `polyls_features.cpp`, `polyls_navigation.cpp`, `polyls_refactor.cpp`, `polyls_semantic.cpp` | Completion, hover, signature help, navigation, refactor, semantic tokens |
| Symbol index | `symbol_index.h/.cpp` | Workspace/document symbol model |
| Grammar descriptor | `tools/polyls/grammar/` | Grammar metadata for tooling |
| Transport CLI | `tools/polyls/polyls.cpp` | stdio JSON-RPC driver |
| Shared LSP client | `tools/ui/common/lsp/` | Qt-free LSP messages, sessions, capability registry, client |

### 10.2 PolyUI Subsystems

| Subsystem | Paths | Responsibility |
|---|---|---|
| Core shell | `tools/ui/common/src/mainwindow.cpp`, `panel_manager`, `action_manager`, shell directory | Main window, panels, action routing, notifications, session/recent/bookmarks/todo |
| Editor | `code_editor`, `syntax_highlighter`, `editor_group`, `editor_grid`, `semantic_tokens_client` | Multi-document editing, syntax and semantic highlighting |
| Editing features | `editing/`, `outline/`, `search/`, `quickopen/`, `inlay_hints/` | Format, snippets, folding, multi-cursor, config, outline, search, quick open, inlay hints |
| Compiler integration | `compiler_service`, `build_panel`, `output_panel`, `terminal_widget` | Compile/build commands, output display, terminal integration |
| Problems and code assist | `problems_aggregator`, `problems_panel`, `code_assist`, `completion_ranker`, `lsp_bridge` | Diagnostics, completion, code assistance, LSP bridge |
| Debug/task/runtime | `dap/`, `tasks/`, `runtime/`, `debug_panel` | DAP client/session, launch config, task config/runner, hot reload, run/debug picker |
| SCM | `scm/`, `git_panel` | SCM provider, Git provider, diff, merge resolver |
| Testing | `testing/` | Test model, inline test lens, coverage model |
| Packages | `packages/` | Package manager, dependency graph, vulnerability scanner |
| Cross-language panels | `cross_language/`, `pipeline/`, `topology_live/`, `samples/` | Navigator, bridge panel, marshalling view, pipeline inspector, IR/ASM viewers, topology live, sample browser |
| Profiling/calls | `profiler_panel`, `profile_session`, call graph/timeline/flame models, `call_analyzer_panel` | Runtime profile and call graph analysis |
| Workspace/remote | `workspace/`, `remote/`, `file_browser`, `workspace_scanner` | Workspace model, remote sessions, dev containers, file sync |
| AI/collab/ext | `ai/`, `collab/`, `ext/` | AI provider, inline suggestions, refactor diff, collaboration provider, extension API, marketplace |
| Settings/theme/i18n/a11y | `settings_*`, `theme_*`, resources, `i18n/`, `a11y/` | Settings schema/service/page, theme service/manager, localization, accessibility |
| Viewers/data tools | `viewer/`, `dbclient/`, `markdown_viewer` | Image viewer, hex viewer, binary inspector, SQL console, Markdown viewing |
| Platform entry | `tools/ui/linux`, `tools/ui/macos`, `tools/ui/windows` | Platform-specific `polyui` startup and resources |

## 11. Settings, Themes, Plugins, and Extensions

| System | Assets | Contract |
|---|---|---|
| Settings | `tools/common/include/effective_settings_loader.h`, `tools/ui/common/resources/default_settings.json`, `settings_schema.json` | Default/user/workspace layered JSON settings shared by CLI and UI |
| Themes | `theme_schema.json`, built-in `.polytheme.json` files | VS Code-style theme discovery, validation, preview, and UI application |
| Plugin API | `common/include/plugins/plugin_api.h`, `docs/specs/plugin_specification.md` | Stable C ABI, capability flags, host services, lifecycle |
| Extension UI | `tools/ui/common/ext/extension_api.*`, `marketplace.*` | UI-facing extension and marketplace management |
| Docs generation | `scripts/docs_generate.py`, `polydoc` | Generated docs and `.ploy` doc extraction |

## 12. Samples and Fixtures

### 12.1 Sample Categories

| Range | Samples | Feature Coverage |
|---|---|---|
| 00 | `00_minimal` | Minimal print/exit path |
| 01-04 | `01_basic_linking`, `01_basic_linking_v2`, `02_type_mapping`, `03_pipeline`, `04_package_import` | Linking, type mapping, pipeline, packages |
| 05-10 | `05_class_instantiation`, `06_attribute_access`, `07_resource_management`, `08_delete_extend`, `09_mixed_pipeline`, `10_error_handling` | Object interop, attributes, resources, delete/extend, mixed pipeline, errors |
| 11-15 | `11_java_interop`, `12_dotnet_interop`, `13_generic_containers`, `14_async_pipeline`, `15_full_stack` | Java/.NET, containers, async, full-stack flow |
| 16-20 | `16_config_and_venv`, `17_string_processing`, `18_numeric_kernels`, `19_file_io`, `20_json_pipeline` | Environments, strings, numeric kernels, file IO, JSON |
| 21-30 | `21_image_processing` through `30_game_loop_demo` | Image, database, HTTP, concurrency, event loop, state machine, plugin system, ML, analytics, game loop |
| 31-41 | `31_explicit_widths` through `41_grammar_polish` | Numeric widths, typed handles, pattern matching, default args, dynamic extend, try/catch, async/await, generics, visibility, strings, grammar polish |

Each sample directory is expected to contain a README pair, a `.ploy` file, supporting language files where applicable, and an expected output file unless the scenario is intentionally environment-dependent.

### 12.2 Fixtures

`tests/fixtures/external_packages/` contains package fixtures for JavaScript, Python stubs, Rust Cargo packages, Go modules, Ruby gems, and other package/import integration paths. These fixtures are used by external package discovery tests and sample regression flows.

## 13. Documentation System

| Documentation Area | Files | Purpose |
|---|---|---|
| User guide | `docs/USER_GUIDE.md`, `docs/USER_GUIDE_zh.md` | End-user guide |
| API docs | `docs/api/` | API reference, extension API, `polyls`, `polydoc`, profile API |
| Specs | `docs/specs/` | Language/IR, namespace, optimization, runtime ABI, plugin spec, packaging, LSP, schemas, attributes |
| Realization notes | `docs/realization/` | Implementation notes for compiler, runtime, UI, interop, language features, packaging, settings, themes, tools |
| Tutorials | `docs/tutorial/` | `.ploy`, project, LSP, profiling, problems panel, shell, viewers, call analyzer tutorials |
| Demand log | `docs/demand/demand.md` | Historical requirement stream and completion markers |
| Changelog | `docs/CHANGELOG.md`, `docs/CHANGELOG_zh.md` | Release history |
| Design | `docs/design.md`, `docs/design_zh.md` | This repository-wide design spec |

Documentation rules:

- User-facing docs should be bilingual unless the existing naming convention is explicitly `_en.md`.
- Cross-linked docs must refer to existing files.
- Design-impacting implementation changes must update docs in the same change set.
- Demand-log items retain completion markers; design docs should not duplicate raw demand text.

## 14. Scripts and Automation

| Script | Responsibility |
|---|---|
| `scripts/build_all_samples.sh`, `scripts/build_all_samples.ps1` | Build sample matrix |
| `scripts/check_include_deps.py` | Include dependency checks |
| `scripts/docs_generate.py` | Documentation generation |
| `scripts/docs_lint.py` | Markdown path/link/bilingual/version lint |
| `scripts/docs_sync_check.py` | Core bilingual documentation sync |
| `scripts/fetch_deps.sh`, `scripts/fetch_deps.ps1` | Dependency fetching |
| `scripts/ci/run_binary_matrix.sh`, `scripts/ci/run_binary_matrix.ps1` | Target/container binary matrix checks |
| `scripts/package_linux.sh`, `scripts/package_macos.sh`, `scripts/package_windows.ps1` | Release package assembly |
| `scripts/installer.nsi` | Windows installer |

## 15. Error Handling and Diagnostics

### 15.1 Error Categories

| Category | Examples | Required Behavior |
|---|---|---|
| CLI/config | Unknown flag, invalid target triple, invalid settings JSON | Fail before compilation with actionable message |
| Lex/parse | Unknown token, malformed syntax, unterminated string | Stop sema for that source and publish parse diagnostics |
| Semantic | Missing symbol, wrong arity, incompatible type, invalid package manager | Stop marshal planning and backend emission |
| Package discovery | Missing package, version mismatch, timed-out index command | Report package diagnostic with manager/environment context |
| Marshal plan | Unsupported conversion, unknown handle type, missing type map | Stop bridge generation |
| Bridge generation | Missing helper, relocation omission, unsupported calling transition | Stop backend/link path |
| IR verification | Invalid CFG, malformed SSA, invalid type operation | Stop backend unless explicit force mode is active |
| Backend | Unsupported instruction, register allocation failure, empty required section | Stop packaging |
| Linker | Missing symbol, unresolved relocation, duplicate strong symbol, invalid descriptor | Fail `polyld` |
| Runtime | Foreign exception, null handle, GC root misuse, async bridge failure | Surface through runtime status/error bridge/call trace |
| UI/LSP | Invalid document sync, unsupported request, stale workspace cache | Return JSON-RPC error or UI diagnostic without crashing service |

### 15.2 Diagnostic Contract

Diagnostics should carry severity, stable code where applicable, source file/URI, line, column, span, message, optional note/help, and related locations for cross-language tracebacks.

The same diagnostic object model should support:

- Human-readable CLI output.
- JSON progress output.
- LSP `publishDiagnostics`.
- `polyui` Problems panel.
- Unit and integration test assertions.
- Generated documentation examples.

## 16. Security, Reliability, and Safety

| Area | Design Requirement |
|---|---|
| Plugins | Validate API version, required exports, capability flags, and lifecycle return codes before activation |
| Package discovery | Use bounded timeouts and explicit manager configuration; do not run arbitrary project scripts as discovery by default |
| Path writes | Generated files must stay in output or aux locations unless the user supplies an absolute path |
| Runtime handles | Validate null/invalid handles before object lifecycle or method operations |
| Marshalling | Validate container length, element size, alignment, and ownership before allocation/copy |
| LSP/UI | Treat workspace text and metadata as untrusted until compiler services validate it |
| Linker | Treat unresolved symbol/relocation/descriptor as hard errors |
| Tests | Regression tests must accompany fixes that change parser, sema, lowering, backend, linker, runtime, or UI model contracts |

## 17. Performance Design

| Area | Performance Approach |
|---|---|
| Build | Shared libraries by default to reduce repeated compilation and keep executables smaller |
| Frontends | String arena, identifier interning, shared token pool, reusable lexer/parser infrastructure |
| Middle-end | Optimization levels gate pass cost; function and context passes are separated |
| LTO/PGO | Pay-as-used metadata and cross-module optimization |
| Backends | Per-stage timing for instruction selection, regalloc, scheduling, and emission |
| Aux artifacts | Incremental and inspection-friendly outputs under configured aux directory |
| UI | Long-running compiler work belongs in services/models rather than blocking editor interaction |
| Samples/benchmarks | Dedicated benchmark tiers and sample regression tests catch functional and performance drift |

## 18. Test Strategy

### 18.1 CTest Targets

| Target | Scope |
|---|---|
| `test_core` | Common utilities, type system, target triples, DWARF |
| `test_plugins` | Plugin manager |
| `test_frontend_common` | Token pool, preprocessor, registry |
| `test_frontend_python` | Python frontend |
| `test_frontend_cpp` | C++ frontend |
| `test_frontend_rust` | Rust frontend |
| `test_frontend_ploy` | `.ploy` frontend |
| `test_frontend_java` | Java frontend |
| `test_frontend_dotnet` | .NET frontend |
| `test_frontend_javascript` | JavaScript frontend |
| `test_frontend_ruby` | Ruby frontend |
| `test_frontend_go` | Go frontend |
| `test_middle` | IR, SSA, LTO, optimization |
| `test_backends` | ABI, backend registry, machine IR, object emission |
| `test_runtime` | GC, FFI, runtime services |
| `test_linker` | Linker, object formats, PE/Mach-O/ELF/Wasm |
| `test_topology` | Topology graph and linker probe |
| `test_settings` | Settings loader |
| `test_lsp` | LSP client core |
| `test_polyls` | Language server features |
| `test_problems` | Problem aggregation |
| `test_completion_ranker` | Completion ranking |
| `test_topology_ui` | Qt topology UI, when Qt is available |
| `test_e2e` | End-to-end compilation behavior |
| `unit_tests` | Aggregate compatibility target |
| `integration_tests` | Full integration suite |
| `samples_regression` | Sample matrix via Catch2 `[samples]` |
| `benchmark_tests`, `benchmark_fast`, `benchmark_full` | Benchmark tiers |

### 18.2 Integration Coverage

Integration tests cover compile pipeline, interop, performance, E2E `polyc`, object formats, external packages, language versions, theme system, PE runtime smoke, real exit codes, sample regression, cross-platform sample consistency, profiler, explicit widths, printf pipeline, LSP diagnostics/navigation/refactor/semantic tokens, Mach-O/Wasm/triple propagation, binary matrix, Mach-O exec smoke, and ELF exec smoke.

### 18.3 Verification Commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

Targeted examples:

```bash
cmake --build build --target test_frontend_ploy
./build/test_frontend_ploy

cmake --build build --target integration_tests
./build/integration_tests

python3 scripts/docs_lint.py
python3 scripts/docs_sync_check.py --ci --scope core
```

## 19. Traceability Matrix

| Requirement Area | Implementation Surface | Test/Doc Surface |
|---|---|---|
| Multi-language frontends | `frontends/`, `FrontendRegistry` | `test_frontend_*`, `docs/specs/language_spec.md` |
| `.ploy` orchestration | `frontends/ploy/`, `tools/polyc`, `tools/polyld` | `test_frontend_ploy`, samples, `.ploy` tutorials |
| Shared IR/optimization | `middle/` | `test_middle`, optimization spec |
| Native/Wasm backends | `backends/` | `test_backends`, binary matrix, object format tests |
| Runtime interop | `runtime/` | `test_runtime`, interop integration tests, runtime ABI docs |
| Linking/packaging | `tools/polyld/`, packaging stage | `test_linker`, E2E smoke tests, release packaging docs |
| CLI tools | `tools/polyc`, `polyopt`, `polyasm`, `polyrt`, `polyver`, `polydoc`, `polytopo`, `polybench` | Tool tests, API docs, tutorials |
| LSP | `tools/polyls`, `tools/ui/common/lsp` | `test_lsp`, `test_polyls`, LSP integration docs |
| IDE | `tools/ui/` | `test_problems`, `test_topology_ui`, polyui realization docs |
| Settings/themes | `tools/common`, `tools/ui/common/resources` | `test_settings`, theme integration test, settings/theme docs |
| Samples | `tests/samples/` | `samples_regression`, sample README pairs |
| Release automation | `scripts/package_*`, `installer.nsi` | release packaging docs |

## 20. Change Control

Design-impacting changes must update this document when they alter:

- Build targets or dependency direction.
- Frontend/backend/runtime/linker contracts.
- `.ploy` syntax, semantics, diagnostics, or package behavior.
- Compiler pipeline stage inputs/outputs.
- CLI flags that affect emitted artifacts.
- UI/LSP user-visible behavior.
- Test target names or verification strategy.
- Documentation taxonomy.

Version changes are made only when implementation or release semantics change. Documentation-only normalization does not require a root `CMakeLists.txt` version bump.

--end -done
