# PolyglotCompiler Changelog

All notable changes to PolyglotCompiler are documented in this file.

The Chinese counterpart is maintained at [`CHANGELOG_zh.md`](CHANGELOG_zh.md).
For day-to-day usage instructions see [`USER_GUIDE.md`](USER_GUIDE.md); for
build / API contracts see [`api/api_reference.md`](api/api_reference.md) and
the per-feature notes under [`realization/`](realization/).

The version range covered below is **v0.1.0 (2026-01-15) → v1.4.0 (2026-04-28)**.
Newer entries appear first.  Each `### vX.Y.Z (YYYY-MM-DD)` block lists the
shipped behaviour, not the underlying tracking item.

---

## v1.4.0 (2026-04-28)

**Debug emitter normalization & umbrella close-out**

- ✅ All twelve `// Placeholder` / `// (placeholder)` / `// Simplified`
  comments inside `backends/common/src/debug_emitter.cpp` and
  `backends/common/src/dwarf_builder.cpp` rewritten to spell out the actual
  DWARF / System V ABI / ELF gABI contract — every "reserved length, patched
  at section close" slot now references the matching `Patch32` call site.
- ✅ `DebugLineInfo` gained a fourth field `std::uint64_t address{0}`
  (default-initialized; all existing aggregate initializers continue to
  compile and behave identically).
- ✅ `DwarfSectionBuilder::EncodeLineStatements` now emits explicit
  `DW_LNS_advance_pc ULEB128(delta)` opcodes per row, honouring
  `DebugLineInfo::address` and falling back to a one-unit step per row when
  callers omit the field — the line program is therefore strictly monotonic
  and individually addressable.
- ✅ `kDwLnsAdvancePc` standard opcode promoted from `[[maybe_unused]]` to
  active emission.
- ✅ New unit test file `tests/unit/backends/debug_emitter_normalization_test.cpp`
  (4 cases): `.debug_info unit_length` patch round-trip; `.debug_line`
  `unit_length` + `header_length` patch round-trip; `DW_LNS_advance_pc`
  presence after monotonic-PC input; PDB GUID RFC 4122 v4 / variant 1 bit
  compliance and entropy.
- ✅ Bilingual docs `docs/realization/debug_emitter_normalization.md` /
  `_zh.md`; new `api_reference` §7.14 (English) / §7.13 (Chinese).
- ✅ `test_backends`: 433 assertions / 62 cases → **461 / 66**; other four
  suites unchanged (`test_core` 357/41, `test_middle` 292/80, `test_runtime`
  35199/101, `test_linker` 171/35).
- ✅ Closes the four-week backend rewrite series that began with v1.3.3 (the
  RISC-V backend is deferred to a future minor release).

## v1.3.7 (2026-04-28)

**`ITargetBackend::EmitBitcode` enabled by default**

- ✅ Default `ITargetBackend::EmitBitcode` no longer reports an "unsupported"
  diagnostic.  It now serializes the input `IRContext` into the project's
  native polyglot bitcode (the same UTF-8 stream that `LTOModule` already
  uses) and writes the bytes into `TargetArtifacts::bitcode_bytes`.
- ✅ New `LTOModule` API: `SerializeBitcode()`, `DeserializeBitcode(string_view)`,
  and `static FromIRContext(ctx, name)`.  The legacy `SaveBitcode` /
  `LoadBitcode` overloads become thin wrappers that call the new pair —
  byte-for-byte identical output, no public-signature break.
- ✅ Three target adapters (`x86_target_backend.cpp`, `arm64_target_backend.cpp`,
  `wasm_target_backend.cpp`) flip `Capabilities().emits_bitcode` from `false`
  to `true`; they keep the default implementation, no override added.
- ✅ New unit test `tests/unit/backends/emit_bitcode_roundtrip_test.cpp`
  (3 cases): empty `IRContext` round-trip; three-backend payload byte-equality;
  block + instruction + operand + entry-points topology preservation.
- ✅ `target_backend_registry_test.cpp` flipped: the prior "unsupported
  diagnostic" assertion is replaced by a 2-function `IRContext` round-trip
  through `EmitBitcode` + `DeserializeBitcode`.
- ✅ Bilingual docs `docs/realization/bitcode_emission.md` / `_zh.md`;
  `api_reference` §7.13 (English) / §7.12 (Chinese).
- ✅ `tools/polyld` continues to consume LLVM bitcode (`BC\xC0\xDE` magic) and
  polyglot bitcode (`m` prefix) without ambiguity.

## v1.3.6 (2026-04-28)

**WASM backend translation-unit split**

- ✅ The 1500+-line monolithic `backends/wasm/src/wasm_target.cpp` is split
  into purpose-named TUs along the module / type / instruction / runtime
  boundaries used by the emitter (each new TU stays under the team's per-file
  size guideline and links into the existing `backend_wasm` target with no
  CMake target-graph reshuffling).
- ✅ Public entry point `WasmTargetBackend::Compile` and the binary / WAT
  output bytes are byte-identical before and after the split (verified by
  the existing WASM e2e cases).
- ✅ New `tests/unit/backends/wasm_split_smoke_test.cpp` smoke-tests that the
  per-TU helpers stay reachable from the emitter façade and that the public
  `Capabilities()` matrix is unchanged.

## v1.3.5 (2026-04-28)

**ABI / relocation model rewrite**

- ✅ Single `RelocationKind` enum + `ABIDescriptor` struct shared by x86_64,
  ARM64, and WASM emitters; per-backend ad-hoc reloc enums removed.
- ✅ `tests/unit/backends/abi_relocation_test.cpp` and `abi_calling_convention_test.cpp`
  cover GOT / PLT / PC-relative / absolute / thread-local relocations across
  the three platforms with the same parametrized matrix.
- ✅ ELF / Mach-O / COFF emitters consume the unified descriptor; cross-format
  divergence is contained inside the per-format writers.

## v1.3.4 (2026-04-28)

**MachineIR verifier**

- ✅ New `MachineIRVerifier` library validates: prefix-register usage, stack
  frame closure (entry / exit balance), cross-basic-block live-range
  consistency, and operand-kind compatibility against each backend's
  `MachineInstrTemplate`.
- ✅ `tests/unit/backends/machine_ir_verifier_test.cpp` and
  `machine_ir_template_test.cpp` exercise the verifier on hand-rolled
  malformed cases plus output from the production isel passes.
- ✅ Verifier runs as an opt-in pass behind a per-backend flag so existing
  CI configurations are unaffected; future backends can flip the flag to
  fail-fast on emission bugs.

## v1.3.3 (2026-04-28)

**Backend registry — `ITargetBackend` + `BackendRegistry`**

- ✅ New abstract base `ITargetBackend` formalises the
  `Capabilities()` / `Compile()` / `EmitAssembly()` / `EmitObject()` /
  `EmitBitcode()` contract; replaces the implicit duck-typed dispatch in the
  driver.
- ✅ `BackendRegistry` discovers and orders backends by triple, alias, and
  priority; CLI `--target=` resolution now goes through the registry, so
  `--target=x86-64` and friends remain valid aliases without ad-hoc strncmp
  in the driver.
- ✅ Three adapters (`x86_target_backend.cpp`, `arm64_target_backend.cpp`,
  `wasm_target_backend.cpp`) wrap the existing emitters; the driver depends
  only on `ITargetBackend`.
- ✅ `tests/unit/backends/target_backend_registry_test.cpp` covers
  registration, alias resolution, priority ordering, capability inspection,
  and the default `EmitBitcode` diagnostic (later flipped in v1.3.7 once the
  default implementation became real).

---

## v1.0.6 (2026-03-19)

**CI Quality Gates (umbrella series 03-17-8)**
- ✅ New `.clang-tidy` configuration: bugprone, cppcoreguidelines, modernize, performance, readability checks with project-specific exclusions
- ✅ New CMake options: `POLYGLOT_ENABLE_ASAN`, `POLYGLOT_ENABLE_UBSAN`, `POLYGLOT_ENABLE_COVERAGE` for sanitizer and coverage builds
- ✅ CI `format-check` job: enforces `.clang-format` style on all C/C++ source files via `clang-format-17`
- ✅ CI `clang-tidy` job: static analysis with `clang-tidy-17` on all project `.cpp` sources (no 50-file cap)
- ✅ CI `sanitizers` job: ASan + UBSan run on non-benchmark suites (`ctest -LE benchmark`) for stable signal
- ✅ CI `coverage` job: collects line coverage via lcov/gcov from non-benchmark suites and uploads filtered report
- ✅ CI `benchmark-smoke` job: runs benchmarks in fast mode to catch performance regressions
- ✅ CI concurrency control: cancels in-progress runs for the same branch/PR
- ✅ Platform builds now depend on `format-check` gate passing first
- ✅ Documentation updated: README, USER_GUIDE (EN/ZH) with quality gate tables and local usage instructions

**Cross-Language Linking Closure**
- ✅ `polyc` now automatically synthesizes `CrossLangSymbol` entries from LINK declarations and CALL descriptors before invoking `ResolveLinks()`, closing the gap where the linker had descriptors but no symbol table to resolve against
- ✅ Parameter descriptors are populated from sema's known function signatures when available
- ✅ Compilation model documentation updated to reflect the automated symbol registration flow

**Tighten Degradation Paths**
- ✅ `lowering.cpp`: LINK stub with no signature info now emits an error in strict mode (default) instead of silently falling back to a single opaque i64 argument
- ✅ `driver.cpp`: minimal main synthesis is now blocked in strict mode; only allowed with `--force` in permissive mode, with clear DEGRADED BUILD warnings
- ✅ `driver.cpp`: post-optimization IR verification failure is now a hard error in strict mode; previously it was silently swallowed (only logged in verbose mode)
- ✅ Backend empty-section stub injection already gated behind `--force` + non-strict; added consistent DEGRADED BUILD messaging

**Pipeline Refactoring**
- ✅ Extracted `PassManager` from internal `.cpp` class to public header `middle/include/passes/pass_manager.h`
- ✅ `PassManager` supports O0/O1/O2/O3 levels with named `PassEntry` stages, custom pass injection, and verbose per-function logging
- ✅ `driver.cpp` optimization pipeline (~40 lines of inline pass calls) replaced with `PassManager::Build()` + `RunOnModule()` — a single 5-line invocation
- ✅ Pass pipeline is now inspectable, extensible, and reusable by CLI, UI, tests, and plugins

**Test Decoupling**
- ✅ Split monolithic `unit_tests` into 14 per-module test binaries (`test_core`, `test_plugins`, `test_frontend_python`, `test_frontend_cpp`, `test_frontend_rust`, `test_frontend_ploy`, `test_frontend_java`, `test_frontend_dotnet`, `test_frontend_common`, `test_middle`, `test_backends`, `test_runtime`, `test_linker`, `test_e2e`)
- ✅ Each module binary links only the libraries it actually needs, reducing link overhead and isolating dylib failures
- ✅ CTest now has 19 test entries (14 per-module + combined `unit_tests` + `integration_tests` + 3 benchmark targets) with labels (`unit`, `frontend`, `middle`, `backend`, `runtime`, `linker`, `e2e`, `benchmark`)
- ✅ Combined `unit_tests` target preserved for backward compatibility
- ✅ Top-level `enable_testing()` added to `CMakeLists.txt` so per-module tests are discoverable from the build root

**Module Boundary Fixes**
- ✅ Extracted `backends/common/` sources (debug_info, debug_emitter, dwarf_builder, object_file) from `polyglot_common` into a new `backend_common` library
- ✅ `backend_x86_64`, `backend_arm64`, `backend_wasm` now depend on `backend_common` instead of backend sources compiled into `polyglot_common`
- ✅ Eliminated the common → backends layer inversion (correct direction: backends → common)
- ✅ `polyld` CLI consolidation: merged the 170-line duplicate `main` from `linker.cpp` (with `--ploy-desc`, `--aux-dir`, `--allow-adhoc-link` options) into `main.cpp`, removed the duplicate entry point from `linker.cpp`

## v1.0.5 (2026-03-17)

**Documentation Single-Sourcing & Auto-Verification**
- ✅ Fixed path references: README.md, USER_GUIDE.md, USER_GUIDE_zh.md, `setup_qt.sh`, `setup_qt.ps1` all now reference correct `tools/ui/setup_qt.*` paths (was `scripts/setup_qt.*`)
- ✅ Fixed dead links: `plugin_specification.md` / `plugin_specification_zh.md` links in USER_GUIDE now resolve correctly
- ✅ Fixed `language_spec.md` / `language_spec_zh.md`: runtime bridge header paths updated to `runtime/include/libs/*.h`
- ✅ Fixed `project_tutorial.md` / `project_tutorial_zh.md`: driver path updated to `tools/polyc/src/driver.cpp`
- ✅ New `scripts/docs_lint.py`: comprehensive documentation linter with 9 check categories:
  - DL001: Path reference validation (backtick-quoted paths must exist in repo)
  - DL002/DL003: Bilingual pairing — every `*.md` must have `*_zh.md` counterpart
  - DL004: Heading structure sync — EN/ZH heading counts per level must match
  - DL005: Dead link detection — relative Markdown links must resolve
  - DL006: Version consistency — version strings across docs must agree
  - DL007: Orphan detection — docs not referenced from README or USER_GUIDE
  - DL008: TODO/FIXME markers in published docs
  - DL009: Placeholder text detection
- ✅ New `scripts/docs_generate.py`: single-source documentation generator with template markers (`<!-- BEGIN:section_name -->` / `<!-- END:section_name -->`); supports `--apply` / `--check` modes; patches test badges, Qt paths, dependency tables, version footers from `docs/_variables.json`
- ✅ New `scripts/docs_sync_check.py`: bilingual synchronisation checker comparing heading structures and content length divergence between EN/ZH pairs
- ✅ New `scripts/check_include_deps.py`: include dependency layer constraint enforcer (middle→common/ir, backends→frontends, frontends→backends)
- ✅ New `docs/_variables.json`: single source of truth for version numbers, test counts, paths, dependency info, tool names — eliminates manual multi-file updates
- ✅ Template markers inserted in README.md (`test_badge`, `qt_setup_en`, `dependencies_table`, `version_footer_en`) and USER_GUIDE footers (`version_footer_en`, `version_footer_zh`)
- ✅ CI integration: added `docs-lint` job to `.github/workflows/ci.yml` running `docs_lint.py --ci`, `docs_sync_check.py --ci`, and `docs_generate.py --check` on every push/PR
- ✅ Updated test badge from 813 → 808 (accurate count); version footer updated to v1.0.4 / 2026-03-17
- ✅ All 808 unit tests passing (34085 assertions); 51/52 integration tests passing

## v1.0.4 (2026-03-17)

**Plugin System & UI Extensibility**
- ✅ Plugin host callbacks fully implemented: `emit_diagnostic` forwards diagnostics to IDE output panel and fires `DIAGNOSTIC` event to subscribers; `open_file` delegates to registered `OpenFileCallback` (wired to IDE tab manager); `register_file_type` populates central file-type registry
- ✅ Event subscription system: plugins can subscribe to 8 event types (`FILE_OPENED`, `FILE_SAVED`, `FILE_CLOSED`, `BUILD_STARTED`, `BUILD_FINISHED`, `DIAGNOSTIC`, `WORKSPACE_CHANGED`, `THEME_CHANGED`) via `subscribe_event` / `unsubscribe_event` host services
- ✅ `PluginManager::FireEvent()`: thread-safe dispatch — snapshots subscribers under lock, delivers events outside lock to avoid re-entry deadlocks
- ✅ Convenience fire helpers: `FireFileOpened()`, `FireFileSaved()`, `FireBuildStarted()`, `FireBuildFinished()`, `FireWorkspaceChanged()`, `FireThemeChanged()`
- ✅ File type registry: `RegisterFileType()` / `GetLanguageForExtension()` / `GetRegisteredFileTypes()` — plugins can add new file-type associations at runtime
- ✅ Menu contribution system: `RegisterMenuItem()` / `UnregisterMenuItem()` / `GetMenuContributions()` / `ExecuteMenuAction()` — plugins can add IDE menu items with callbacks
- ✅ `PolyglotEvent` struct and `PFN_polyglot_plugin_on_event` export — plugins implement a single event handler for all subscribed events
- ✅ `PolyglotMenuContribution` struct with `menu_path`, `action_id`, `label`, `shortcut`, and `callback`
- ✅ Extended `PolyglotHostServices` with 4 new function pointers: `subscribe_event`, `unsubscribe_event`, `register_menu_item`, `unregister_menu_item`
- ✅ New `ActionManager` class: centralized action/keybinding management extracted from MainWindow; supports plugin-contributed actions with `RegisterPluginAction()` / `RemovePluginActions()`; keybinding persistence via `LoadKeybindings()` / `SaveKeybindings()`
- ✅ New `PanelManager` class: manages bottom panel tabs; supports plugin-contributed panels via `RegisterPluginPanel()` / `RemovePluginPanels()`; unified `TogglePanel()` / `ShowPanel()` / `IsPanelActive()` API replacing scattered toggle logic
- ✅ MainWindow refactored: panel toggle/show logic delegates to `PanelManager`; keybinding management delegates to `ActionManager`; plugin lifecycle events wired to file open/save/compile/workspace/theme changes
- ✅ Plugin diagnostic forwarding: `SetDiagnosticCallback()` wired to IDE output panel
- ✅ Plugin menu item forwarding: `SetMenuItemRegisteredCallback()` wired to `ActionManager`
- ✅ Plugin file-type forwarding: `SetFileTypeRegisteredCallback()` wired to IDE output log
- ✅ 12 new unit tests: event type distinctness, event struct zero-init, menu contribution struct, subscribe/unsubscribe safety, fire-event-no-subscribers, file type registry CRUD, menu contributions empty, ExecuteMenuAction unknown, DispatchOpenFile with/without callback, FileType registered callback, diagnostic callback
- ✅ Test count: 796 → 808 test cases; assertions: 34056 → 34085 — all passing

## v1.0.3 (2026-03-17)

**Test Quality Improvements**
- ✅ Replaced `REQUIRE(true)` in `lto_test.cpp` with optimizer statistics behavior assertions (all stats == 0 for empty module, module survives all optimization stages)
- ✅ Replaced `REQUIRE(true)` in `gc_algorithms_test.cpp` with GC stats verification: `collections >= 10`, `total_allocations >= 500` for multi-cycle test; `total_allocations >= 200` for incremental collection
- ✅ Replaced commented-out performance benchmarks with real throughput assertions (`total_allocations >= 10000`, `collections >= 1`)
- ✅ Replaced `SUCCEED()` in `java_test.cpp` and `dotnet_test.cpp` with semantic analysis behavior checks (verifying diagnostics are type-mapping related, not crashes)
- ✅ Replaced `REQUIRE(true)` in `threading_services_test.cpp` with phase-tracking atomics for barrier reset and exact-sum verification for large workloads
- ✅ Strengthened Java/DotNet enum and struct lowering tests with positive diagnostic-state assertions
- ✅ New `compilation_behavior_test.cpp`: 15 new behavior-level and failure-path test cases:
  - C++ single/multiple function IR output correctness
  - C++ conditional produces multiple basic blocks
  - x86_64 assembly contains function label and `ret` instruction
  - x86_64 object code has `.text` section with non-zero bytes and function symbol
  - ARM64 assembly output contains function label
  - IR verification passes on well-formed IR; catches malformed (empty function) IR
  - Ploy `LINK` produces correct cross-language call descriptors
  - Ploy `FUNC` produces named IR function
  - Failure paths: parse error diagnostics, sema error on undefined variable, lowering invalid code returns failure
- ✅ Test count: 781 → 796 test cases; assertions: 3985 → 34056 — all passing

## v1.0.2 (2026-03-17)

**Package Discovery Refactoring**
- ✅ `CommandResult` struct: structured output with `stdout_output`, `exit_code`, `timed_out`, `failed`, `Ok()` convenience method
- ✅ `ICommandRunner::RunWithResult()`: new primary interface with configurable per-command timeout; `Run()` kept for backward compatibility and delegates to `RunWithResult()`
- ✅ `DefaultCommandRunner`: timeout support via `std::async` + `std::future::wait_for()`; configurable default timeout (10s)
- ✅ `PackageIndexer` class: explicit pre-compilation package-index phase, decoupled from sema; runs discovery commands with timeouts and retries, populates shared `PackageDiscoveryCache`
- ✅ `PackageIndexerOptions`: configurable `command_timeout`, `max_retries`, `verbose` flag
- ✅ `PackageIndexer::Stats`: tracks `languages_indexed`, `commands_executed`, `commands_timed_out`, `commands_failed`, `packages_found`, `total_duration`
- ✅ `PackageIndexer::SetProgressCallback()`: optional per-language progress reporting during indexing
- ✅ `PloySemaOptions::enable_package_discovery` default changed from `true` to `false` — sema never shells out; callers use `PackageIndexer` as pre-phase
- ✅ `polyc` driver: PackageIndexer wired as Phase 2.5 before sema; scans AST for referenced languages and indexes only those
- ✅ CLI flags: `--package-index` (default), `--no-package-index`, `--pkg-timeout=<ms>`
- ✅ 10 new test cases: `CommandResult::Ok()`, `MockCommandRunner` structured result, timeout simulation, `PackageIndexer` cache population, cache skip, timeout handling, multi-language indexing, pre-phase → sema cache flow, progress callback, defaults verification

**Build System Modularization**
- ✅ Top-level `CMakeLists.txt` reduced from ~690 lines to ~80 lines — project setup, compiler flags, dependencies, and `add_subdirectory()` calls only
- ✅ `common/CMakeLists.txt`: `polyglot_common` library (type system, symbol table, plugins, debug info)
- ✅ `middle/CMakeLists.txt`: `middle_ir` library (IR, optimization passes, PGO, LTO)
- ✅ `frontends/CMakeLists.txt`: `frontend_common` + 6 language frontend libraries
- ✅ `backends/CMakeLists.txt`: `backend_x86_64`, `backend_arm64`, `backend_wasm`
- ✅ `runtime/CMakeLists.txt`: `runtime` library (GC, FFI, interop, services)
- ✅ `tools/CMakeLists.txt`: `polyc`, `polyasm`, `polyld`, `polyopt`, `polyrt`, `polybench`, `polyui` (with full Qt detection logic)
- ✅ `tests/CMakeLists.txt`: explicit per-module source lists replacing `GLOB_RECURSE` for unit tests; `integration_tests` and `benchmark_tests` retained with scoped globs
- ✅ Test count: 781 test cases / 3985 assertions — all passing

## v1.0.1 (2026-03-17)
- ✅ Two explicit compilation modes: **strict** and **permissive** (`--strict` / `--permissive` CLI flags)
- ✅ Release builds default to strict mode via `POLYC_DEFAULT_STRICT` compile definition
- ✅ Strict mode: semantic analysis reports placeholder-type fallbacks as errors instead of warnings
- ✅ Strict mode: IR verifier rejects functions with unresolved placeholder I64 return types
- ✅ Strict mode: lowering rejects cross-language calls with unknown return types
- ✅ Strict mode: degraded stubs via `--force` are blocked; `--strict` and `--force` are mutually exclusive
- ✅ Permissive mode: all placeholder-type fallbacks visible as warnings (previously silent)
- ✅ `ReportStrictDiag()` helper: routes diagnostics to error (strict) or warning (permissive)
- ✅ `IRType::is_placeholder` flag distinguishes genuine I64 from unresolved fallback I64

## v1.0.1 (2026-04-09)

**Staged compilation pipeline & polyui icon embedding**
- ✅ Implemented a concrete 6-stage compilation pipeline for `.ploy` in `tools/polyc/src/compilation_pipeline.cpp`
- ✅ Stage breakdown is now explicit and data-structured: `frontend -> semantic db -> marshal plan -> bridge generation -> backend -> packaging`
- ✅ Added runtime pipeline orchestration (`CompilationPipeline::RunAll()` and per-stage runners) with stage timing capture
- ✅ Wired `polyc` driver to execute the staged pipeline for `.ploy` as the primary path
- ✅ Added package-index integration in the staged semantic flow via `PackageIndexer` + shared `PackageDiscoveryCache`
- ✅ Added bridge generation stage integration with `PolyglotLinker` and resolved stub injection before backend emission
- ✅ Added packaging stage output for deterministic `.pobj` emission and optional `polyld` link invocation in `link` mode
- ✅ `polyui` Windows executable now embeds `tools/ui/common/resources/icon.ico` via CMake resource script (`tools/ui/windows/polyui.rc`), so generated `polyui.exe` ships with the project icon
- ✅ `polyui` now explicitly sets runtime title-bar/window icons: Windows uses embedded `:/icons/icon.ico`, Linux/macOS use embedded `:/icons/icon.png`

> Note: two independent v1.0.1 entries exist above because the project ran
> two parallel maintenance branches in March / April 2026; both shipped under
> the same patch number before the v1.1.x line was opened.

## v1.0.0 (2026-03-15)
- ✅ Project version unified to **1.0.0** (all previous v5.x/v4.x versions renumbered to v0.5.x/v0.4.x)
- ✅ Release packaging scripts for all 3 platforms (`scripts/package_windows.ps1`, `scripts/package_linux.sh`, `scripts/package_macos.sh`)
- ✅ Windows: portable ZIP archive + NSIS installer (`scripts/installer.nsi`) with PATH registration and Start Menu shortcuts
- ✅ Linux: portable `.tar.gz` with Qt library bundling and wrapper script
- ✅ macOS: portable `.tar.gz` with `macdeployqt` integration for `.app` bundle
- ✅ Packaging documentation (`docs/specs/release_packaging.md` / `_zh.md`)
- ✅ Version references updated across CMakeLists.txt, all tool source files, README.md, and USER_GUIDE (EN/ZH)

## v0.5.5 (2026-03-15)
- ✅ Unified theme system via `ThemeManager` singleton — 4 built-in colour schemes (Dark, Light, Monokai, Solarized Dark); all panels (main window, build, debug, git, settings) apply styles from a single source
- ✅ Compile & Run (`Ctrl+R`): QProcess-based workflow compiles current file, locates output binary, and launches it with stdout/stderr streamed to the log panel
- ✅ Stop (`Ctrl+Shift+R`): terminates a running process and cancels an active build
- ✅ Debug panel: full call-stack frame parsing for both lldb and GDB/MI output (module, function, file, line); variable type/value extraction; watch expression evaluation with live result updates
- ✅ Debug panel: watch context menu (Remove / Remove All / Evaluate) wired to `OnRemoveWatch` and `OnEvaluateWatch`
- ✅ Debug panel: configurable debugger path and break-on-entry option from Settings
- ✅ Custom keybindings: editable via `QKeySequenceEdit` in Settings → Key Bindings page; 28 default actions; custom shortcuts persisted in `QSettings` and applied at startup
- ✅ Settings linkage: CMake path, build directory, and debugger path automatically propagate to `BuildPanel` and `DebugPanel` on apply
- ✅ New source file `theme_manager.cpp` / `theme_manager.h` added to `tools/ui/common/`

## v0.5.4 (2026-03-11)
- ✅ Qt-based desktop IDE (`polyui`): syntax highlighting, real-time diagnostics, file browser, tabbed editor, output panel, bracket matching, dark theme
- ✅ IDE uses compiler frontend tokenizers for accurate, language-aware highlighting across all 6 supported languages
- ✅ IDE keyboard shortcuts: Ctrl+B compile, Ctrl+Shift+B analyze, Ctrl+N/O/S/W file management
- ✅ CMake Qt6/Qt5 auto-discovery: prefers standalone Qt installation at `D:\Qt` over bundled copies; pass `-DQT_ROOT=<path>` to override
- ✅ Default `ninja` build now generates all executables (polyc, polyld, polyasm, polyopt, polyrt, polybench, polyui)
- ✅ Platform-separated `polyui` source layout: shared code in `tools/ui/common/`, platform-specific entry points in `tools/ui/windows/`, `tools/ui/linux/`, `tools/ui/macos/`; CMake auto-selects the correct `main.cpp` per OS
- ✅ macOS support: `MACOSX_BUNDLE` with `macdeployqt` post-build; Linux support: `xcb` platform default, `.desktop` integration
- ✅ Integrated terminal in the IDE: embedded shell (PowerShell/bash/zsh), ANSI colour parsing, command history, multiple instances, `Ctrl+\`` toggle
- ✅ Cleaned up legacy `tools/ui/ployui_windows/`, `tools/ui/src/`, and `tools/ui/include/` directories
- ✅ Full documentation audit and statistics refresh — 293 source files, 91,457 lines of code
- ✅ Test growth: 743 unit (was 734) + 52 integration (was 50) + 18 benchmark = **813 total** (was 802)
- ✅ Ploy frontend expanded to 6 compilation units (added `command_runner.cpp`, `package_discovery_cache.cpp`)
- ✅ Updated all documentation (README, USER_GUIDE EN/ZH, tutorials) with current statistics

## v0.5.3 (2026-02-22)
- ✅ Broke `common` ↔ `middle` circular dependency — canonical IR headers now live in `middle/include/ir/`; `common/include/ir/` contains forwarding shims for backward compatibility
- ✅ Added CI include-lint script (`scripts/check_include_deps.py`) enforcing layer constraints: `middle/` must not include `common/include/ir/`, backends must not include frontends
- ✅ Unified debug-info modelling — added `common/include/debug/debug_info_adapter.h` with `ConvertToBackendDebugInfo()` bridging `polyglot::debug` (rich DWARF model) and `polyglot::backends` (flat emission model)
- ✅ Added optimisation pipeline & switch matrix documentation (`docs/specs/optimization_pipeline.md` / `_zh.md`)
- ✅ Added runtime C ABI reference documentation (`docs/specs/runtime_abi.md` / `_zh.md`)
- ✅ Unified tool-layer namespaces: `polyc` and `polybench` now use `namespace polyglot::tools`, matching `polyasm` / `polyopt` / `polyrt`

## v0.5.2 (2026-02-22)
- ✅ New `PloySemaOptions` configuration struct with `enable_package_discovery` switch
- ✅ `PloySema` constructor accepts options; backward-compatible default constructor preserved
- ✅ Session-level `PackageDiscoveryCache` — thread-safe, keyed by `language|manager|env_path`
- ✅ `ICommandRunner` abstraction — external command execution decoupled from `_popen`; test-mockable
- ✅ `DiscoverPackages` cache-first flow — hit → merge cached results; miss → run + store
- ✅ Benchmark suites disable package discovery, isolating compiler-only performance
- ✅ Lowering micro-benchmark timing fix — parse/sema excluded from timing window
- ✅ Benchmark fast/full tiers via `POLYBENCH_MODE` env var (`fast`/`full`/default)
- ✅ CTest: `benchmark_fast` and `benchmark_full` targets with labels
- ✅ Discovery unit tests: disabled-no-cmd, repeated-analyze-once, key-isolation, cache-roundtrip

## v0.5.1 (2026-02-22)
- ✅ Linker strong failure mode — unresolved symbols are hard errors; placeholder stubs no longer generated
- ✅ Linker main flow fatal when cross-language entries exist but resolution fails
- ✅ `polyc` and `polyasm` now accept `--arch=wasm` for WebAssembly target
- ✅ WASM backend: real function call index resolution via name→index map (no more hardcoded indices)
- ✅ WASM backend: alloca lowered to shadow stack model (mutable i32 global, grows downward from 65536)
- ✅ WASM backend: unsupported IR instructions emit `unreachable` + diagnostic error (no silent NOPs)
- ✅ `.ploy` sema strict mode (`SetStrictMode(true)`) — warns on untyped params, `Any` fallbacks, missing annotations
- ✅ `.ploy` lowering consults sema symbol table before I64 fallback; uses `KnownSignatures` for return/param types
- ✅ Debug emitter: FDE now has proper CFA instructions (push rbp / mov rbp,rsp frame setup)
- ✅ Debug emitter: PDB TPI stream emits LF_POINTER type records for pointer types
- ✅ Mach-O object file: per-section `reloff`/`nreloc` computed and written; relocation_info entries emitted
- ✅ Rust and Python frontend lowering: diagnostic warnings for unknown type → I64 fallbacks
- ✅ Rust advanced_features_test.cpp: all `REQUIRE(true)` replaced with behavioral parse + AST assertions
- ✅ E2E tests: added ARM64 object code emission test and WASM multi-function binary smoke test

## v0.5.0 (2026-02-22)
- ✅ Full project documentation update — all docs refreshed to reflect current state
- ✅ WebAssembly (WASM) backend added (`backends/wasm/`)
- ✅ Unified DWARF builder compiled into `polyglot_common` library
- ✅ PDB emission: MSF block layout, TPI type records (LF_ARGLIST/LF_PROCEDURE), RFC 4122 v4 GUID
- ✅ CFA initialisation with register save rules and NOP alignment
- ✅ ELF: e_machine architecture switching (x86_64/ARM64), full SHT_RELA relocation sections
- ✅ Mach-O: complete LC_SEGMENT_64, LC_SYMTAB, nlist_64 symbol table, section mapping
- ✅ IR parser/printer symmetry: `fadd/fsub/fmul/fdiv/frem` parsing + global/const declarations
- ✅ Preprocessor test coverage: 18 dedicated test cases for macros, directives, token pool
- ✅ Debug tests Windows-compatible: `TmpPath()` via `std::filesystem::temp_directory_path()`
- ✅ Cross-language link chain fully connected: polyc → PolyglotLinker → glue code → binary
- ✅ Real IR lowering for all 6 frontends (no fallback stubs)
- ✅ E2E tests enabled: 29 end-to-end test cases
- ✅ Linker completeness: COFF/PE, ELF, Mach-O all implemented
- ✅ polyopt: reads IR files and runs optimisation pipeline
- ✅ polybench: full benchmark suite (compilation + E2E)
- ✅ polyrt: FFI subcommand, real GC/thread statistics
- ✅ Tutorial documentation added (`docs/tutorial/`)
- ✅ 16 sample programs (was 12)
- ✅ Total: 813 test cases across 3 suites (743 unit + 52 integration + 18 benchmark)

## v0.4.3 (2026-02-20)
- ✅ Added cross-language object destruction `DELETE` keyword
- ✅ Added cross-language class extension `EXTEND` keyword
- ✅ Enhanced diagnostics infrastructure: severity levels, error codes (1xxx-5xxx), traceback chains, suggestions
- ✅ Added parameter count mismatch checking in semantic analysis
- ✅ Added type mismatch checking in semantic analysis
- ✅ All error reports upgraded to use structured error codes
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation for DELETE/EXTEND
- ✅ 36 new test cases, total 207 test cases, 598 assertions
- ✅ Keywords count 52 → 54

## v0.4.2 (2026-02-20)
- ✅ Added cross-language attribute access `GET` and assignment `SET` keywords
- ✅ Added automatic resource management `WITH` keyword
- ✅ Added type annotations with qualified types
- ✅ Added interface mapping via `MAP_TYPE` for classes
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation for GET/SET/WITH
- ✅ 31 new test cases, total 171 test cases, 523 assertions
- ✅ Bilingual documentation (USER_GUIDE.md / USER_GUIDE_zh.md)
- ✅ Keywords count 49 → 52

## v0.4.1 (2026-02-20)
- ✅ Added cross-language class instantiation `NEW` and method call `METHOD` keywords
- ✅ AST / Lexer / Parser / Sema / IR Lowering full chain implementation
- ✅ 22 new test cases, total 140 test cases, 443 assertions
- ✅ Bilingual feature documentation (`class_instantiation.md` / `class_instantiation_zh.md`)
- ✅ Keywords count 47 → 49

## v0.4.0 (2026-02-19)
- ✅ Added complete .ploy cross-language linking frontend chapter
- ✅ Multi-package-manager support: CONFIG CONDA / UV / PIPENV / POETRY
- ✅ Version constraint verification (6 operators)
- ✅ Selective import
- ✅ 117+ test cases, 361+ assertions
- ✅ Comprehensive review and rewrite of the complete guide (v0.3.0 → v0.4.0)
- ✅ Precise description of compilation flow (PolyglotCompiler's own frontends, no external compilers)

## v0.3.0 (2026-02-01)
- ✅ Chapters 11-15 (implementation analysis / testing / advanced optimisations / achievements)
- ✅ 33+ optimisation passes, 4 GC algorithms
- ✅ PGO/LTO/DWARF5 support

## v0.2.0 (2026-01-29)
- ✅ Loop optimisation, GVN, constexpr, DWARF 5

## v0.1.0 (2026-01-15)
- ✅ Basic compilation chain: 3 frontends, 2 backends, basic optimisations

---

*Maintained by PolyglotCompiler Team*  
*Last updated: 2026-04-28*
