# Backend Registry & ITargetBackend

> Status: shipped in PolyglotCompiler 1.3.3 (sub-need 2026-04-28-2a of the backend layer refactor).
> Audience: backend authors, toolchain integrators, and anyone touching `polyc` / `polyasm` dispatch.

## 1. Why this exists

Until 1.3.2, every tool that needed a code generator (`polyc`, `polyasm`, future `polyld` plug-ins)
hard-coded an `if/else` chain on the `--arch` string and instantiated `X86Target`, `Arm64Target`
or `WasmTarget` directly. That layout had three concrete problems:

1. Adding a new backend (RISC-V, LoongArch, …) required editing every consumer.
2. The available capabilities of a backend (object emission, debug info, bitcode, regalloc menu)
   were not introspectable — users had no way to ask "what targets do I have?".
3. The frontend layer had already solved the same problem with `polyglot::frontends::FrontendRegistry`.
   The asymmetry was a long-standing inconsistency.

`polyglot::backends::ITargetBackend` + `BackendRegistry` close that gap. They mirror the frontend
registry shape so any contributor familiar with `REGISTER_FRONTEND` can register a backend with
zero new concepts.

## 2. Public surface at a glance

| Header | Provides |
|---|---|
| `backends/common/include/target_backend.h` | `ITargetBackend`, `TargetOptions`, `TargetArtifacts`, `BackendCapabilities`, `BackendInfo`, `MCRelocation`, `MCSymbol`, `MCSection`, `CompileStats`, `BackendDiagnostic`, `CompileResult`, `MakeBackendInfo`, `AsciiToLower` |
| `backends/common/include/backend_registry.h` | `BackendRegistry`, `RegisterStatus`, `BackendRegistrar`, `REGISTER_TARGET_BACKEND`, `ToJson`, `ToHumanReadable` |

All names live in `polyglot::backends`. Adapters for the bundled targets live in nested
namespaces: `polyglot::backends::x86_64`, `polyglot::backends::arm64`, `polyglot::backends::wasm`.

## 3. The `ITargetBackend` contract

```cpp
class ITargetBackend {
 public:
    virtual ~ITargetBackend() = default;

    virtual std::string TargetTriple() const = 0;
    virtual std::string Description() const = 0;
    virtual std::vector<std::string> Aliases() const = 0;
    virtual bool IsAvailable() const = 0;
    virtual BackendCapabilities Capabilities() const = 0;

    virtual CompileResult Compile(const middle::ir::Module& module,
                                  const TargetOptions& options) = 0;

    virtual CompileResult EmitAssembly(const middle::ir::Module& module,
                                       const TargetOptions& options);
    virtual CompileResult EmitObject  (const middle::ir::Module& module,
                                       const TargetOptions& options);
    virtual CompileResult EmitBitcode (const middle::ir::Module& module,
                                       const TargetOptions& options);
};
```

* `TargetTriple()` — canonical triple, e.g. `x86_64-unknown-elf`. Must match exactly one entry
  in the registry; alias lookup is performed separately.
* `Aliases()` — every accepted spelling (case-insensitive). Conventions:
  * include the canonical triple at index 0;
  * include short forms users actually type (`amd64`, `x64`, `aarch64`, `wasm32`);
  * include common platform-specific triples (`x86_64-pc-windows-msvc`, `aarch64-apple-darwin`,
    `wasm32-wasi`, …).
* `Capabilities()` — feature flags consumed by `polyc`, settings UI, and `--print-targets`. The
  bundled backends report:

  | Capability | x86_64 | arm64 | wasm |
  |---|:---:|:---:|:---:|
  | `emits_object` | ✅ | ✅ | ✅ |
  | `emits_assembly` | ✅ | ✅ | ✅ (text `.wat` placeholder) |
  | `emits_bitcode` | — | — | — |
  | `supports_debug_info` | ✅ | ✅ | — |
  | `supports_pic` | ✅ | ✅ | n/a |
  | `register_allocators` | linear-scan, graph-coloring | linear-scan, graph-coloring | n/a |

* `Compile()` is the canonical entry point. `EmitAssembly` / `EmitObject` default to calling
  `Compile()` and slicing the corresponding field of `TargetArtifacts`. `EmitBitcode` defaults
  to returning a single `BackendDiagnostic{Severity::kError, "bitcode emission unsupported"}` so
  callers always see a typed error instead of silent zero-byte output (this default will be lifted
  by sub-need 2026-04-28-2e when the LLVM bitcode emitter lands).

### `TargetOptions`

Plain-value bag passed to every `Compile` call. Stable subset (will only grow, never shrink):

* `EmitKind emit` — `kObject`, `kAssembly`, `kBitcode`, `kLLVMText`.
* `RegAllocStrategy reg_alloc` — `kLinearScan`, `kGraphColoring`.
* `SchedulerStrategy scheduler` — `kList`, `kNone`.
* `VerifyLevel verify` — `kOff`, `kOn`, `kStrict`.
* `DebugInfoLevel debug_info` — `kNone`, `kLine`, `kFull`.
* `optimization_level` (0–3), `position_independent`, `relocation_model`, `cpu`, `features`.
* `module_name`, `source_path` — used only for diagnostic provenance.

### `TargetArtifacts`

Returned in `CompileResult::artifacts` on success. Fields:

* `assembly_text` — UTF-8 string when `emit==kAssembly` or as a side product.
* `object_bytes` — raw bytes of the produced object/blob (ELF/Mach-O/COFF/wasm/bitcode).
* `relocations` — `vector<MCRelocation>` (symbol, section, offset, type, addend, is_pcrel).
* `symbols` — `vector<MCSymbol>` with `is_global` / `is_defined` / `is_weak` flags.
* `sections` — `vector<MCSection>` with size/align/flags.
* `debug_sections` — opaque per-target debug payload (DWARF/PDB/none).
* `stats` — `CompileStats` (isel/regalloc/sched/emit timings + peak memory + counters).

> Field naming convention: booleans are spelled with the `is_` prefix (`is_global`, `is_defined`,
> `is_bss`, `is_weak`) to remain conflict-free with reserved keywords across the toolchain.

### `CompileResult` and diagnostics

```cpp
struct CompileResult {
    std::optional<TargetArtifacts>    artifacts;
    std::vector<BackendDiagnostic>    diagnostics;
    bool ok() const { return artifacts.has_value(); }
};
```

A backend may return diagnostics on success (warnings/notes) or on failure (errors). Severity
mapping into the front-end `Diagnostics` sink is:

| `BackendDiagnostic::Severity` | Front-end channel |
|---|---|
| `kError` | `Diagnostics::ReportError` |
| `kWarning` | `Diagnostics::ReportWarning` |
| `kInfo` | `Diagnostics::ReportWarning` (degraded — `Diagnostics` exposes no info channel today) |

## 4. Registry semantics

### 4.1 Lifetime

`BackendRegistry::Instance()` returns a Meyers singleton guarded by a `std::mutex`. All public
methods are thread-safe. Order of registration is not significant: `List()` always returns
backends sorted by canonical triple for reproducible output.

### 4.2 Registration

```cpp
namespace polyglot::backends::arm64 {
REGISTER_TARGET_BACKEND([] { return std::make_unique<Arm64TargetBackend>(); });
}
```

Under the hood, `REGISTER_TARGET_BACKEND` defines a translation-unit-local
`BackendRegistrar` whose constructor calls `BackendRegistry::Instance().Register(...)`. The macro
is the **only** sanctioned registration path — direct calls to `Register()` are reserved for
tests and `Clear()` flows.

Registration returns one of:

| `RegisterStatus` | Meaning |
|---|---|
| `kOk` | Backend accepted; canonical triple and every alias indexed. |
| `kNullBackend` | Caller passed an empty `unique_ptr`. |
| `kDuplicateTriple` | Another backend already owns this canonical triple. |
| `kAliasConflict` | An alias collides with the canonical triple or alias of an existing backend. |

The macro asserts on anything other than `kOk` in debug builds and logs a warning in release.
Tests cover all four paths.

### 4.3 Lookup

```cpp
ITargetBackend* be = BackendRegistry::Instance().Find("amd64");           // case-insensitive
ITargetBackend* be = BackendRegistry::Instance().FindOrDiagnose(triple, &diag);
```

`Find` returns `nullptr` for unknown triples. `FindOrDiagnose` is the production path used by
both `polyc` and `polyasm`; on miss it appends a single error diagnostic of the form

```
no backend registered for target 'mips'. Available backends:
  arm64-unknown-elf (aliases: arm64, aarch64, armv8, …)
  wasm32-unknown-unknown (aliases: wasm, wasm32, wasm64, …)
  x86_64-unknown-elf (aliases: x86_64, x86-64, amd64, x64, …)
```

so users always get an actionable hint without re-running with a verbose flag.

### 4.4 Enumeration

`List()` returns a sorted `std::vector<BackendInfo>`. Each `BackendInfo` carries triple,
description, aliases, capabilities, and an `is_available` flag. Two free helpers serialize the
list:

* `ToHumanReadable(const std::vector<BackendInfo>&) -> std::string`
* `ToJson(const std::vector<BackendInfo>&) -> std::string`

The JSON shape is stable and mechanically parseable:

```json
[
  {
    "triple": "arm64-unknown-elf",
    "description": "ARM64 (AArch64) backend",
    "aliases": ["arm64", "aarch64", "armv8", "..."],
    "available": true,
    "capabilities": {
      "emits_object": true,
      "emits_assembly": true,
      "emits_bitcode": false,
      "supports_debug_info": true,
      "supports_pic": true,
      "register_allocators": ["linear-scan", "graph-coloring"]
    }
  }
]
```

## 5. Tool integration

### 5.1 `polyc`

`tools/polyc/src/compilation_pipeline.cpp` no longer branches on the architecture string. The
relevant excerpt:

```cpp
auto* backend =
    backends::BackendRegistry::Instance().FindOrDiagnose(config.target_arch, &lookup_diag);
if (!backend) {
    diagnostics.ReportError(lookup_diag.message);
    return Result::Failure;
}

backends::TargetOptions opts;
opts.emit          = backends::EmitKind::kObject;
opts.reg_alloc     = config.reg_alloc;
opts.scheduler     = config.scheduler;
opts.debug_info    = config.debug_info;
opts.module_name   = ir_module.name();
opts.source_path   = config.source_path;

auto compile_result = backend->Compile(ir_module, opts);
absorb_artifacts(compile_result, &compiled_object);   // adapter into legacy types
```

`absorb_artifacts` translates `TargetArtifacts` into the existing `CompiledObject` /
`linker::Symbol` / `linker::Relocation` shapes so that the rest of the linker/driver pipeline did
not change in 2a (it will be reshaped in later sub-needs).

### 5.2 `polyasm`

`tools/polyasm/src/assembler.cpp` follows the same pattern. The legacy WASM-binary fast path is
preserved (it bypasses the registry only when the triple starts with `wasm` and the user
requested raw `.wasm` output).

### 5.3 New CLI flags on `polyc`

| Flag | Effect |
|---|---|
| `--print-targets` | Print the human-readable backend list and exit 0. |
| `--print-targets=text` | Same as above (explicit form). |
| `--print-targets=json` | Print the JSON snapshot of the registry and exit 0. |
| `--print-target-info=<triple>` | Print a single backend's info in human-readable form. |
| `--print-target-info=<triple>:json` | Same as above, JSON form. |

Lookup is alias-aware: `--print-target-info=amd64` resolves to the x86_64 backend.

Failure exit codes:

* `0` — backend found, info printed.
* `2` — alias not registered. The "available backends" diagnostic is written to stderr.

These flags are evaluated at the very top of `main()` before normal argument parsing, so they
work even if other flags are malformed.

## 6. Migrating from pre-1.3.3 dispatch

If you maintain a custom tool that used to do:

```cpp
if (arch == "wasm") { /* WasmTarget */ }
else if (arch == "arm64" || arch == "aarch64") { /* Arm64Target */ }
else { /* X86Target */ }
```

replace it with:

```cpp
auto* be = polyglot::backends::BackendRegistry::Instance().FindOrDiagnose(arch, &diag);
if (!be) { /* surface diag.message */ return; }
auto result = be->Compile(ir_module, options);
```

Direct construction of `X86Target`, `Arm64Target`, `WasmTarget` is still legal (they are
unchanged in 2a) but is **discouraged**. Future sub-needs (2b–2d) will reshape these classes;
code that goes through the registry is unaffected.

## 7. Adding a new backend

Minimum checklist:

1. Implement `ITargetBackend` (typically as an adapter wrapping your code generator).
2. Register at namespace scope:
   ```cpp
   namespace polyglot::backends::myarch {
   REGISTER_TARGET_BACKEND([] { return std::make_unique<MyArchBackend>(); });
   }
   ```
3. Add the `.cpp` file to the right CMake target so the static initializer is linked in. With
   `BUILD_SHARED_LIBS=ON` (the default) this means linking against `polyc` / `polyasm` /
   `test_backends`, exactly like `backend_x86_64` / `backend_arm64` / `backend_wasm` already do.
4. Add a unit test in `tests/unit/backends/<myarch>_target_backend_test.cpp` that asserts the
   backend is reachable through both its canonical triple and at least one alias, and that
   `Capabilities()` matches what the docs claim.
5. Update `--print-targets` baseline tests if you add a new entry to the bundled set (the test
   asserts the registry size).

## 8. Troubleshooting

* **`no backend registered for target 'X'`** — your alias is misspelled, or the backend's
  translation unit was stripped by the linker (missing `whole-archive` link option). Check that
  the corresponding `.cpp` is in the link line; with shared libs this is automatic.
* **`backend registration rejected: alias conflict`** — two backends declare the same alias.
  Aliases must be globally unique; pick a target-prefixed form.
* **`bitcode emission unsupported`** — expected on x86_64/arm64/wasm in 1.3.3. Lifted by
  sub-need 2026-04-28-2e.
* **Mixed-case triple does not resolve** — the registry lower-cases the input via
  `AsciiToLower`. If you compare triples manually, do the same.

## 9. Related work

* Sub-need 2026-04-28-2b will lift `MachineFunction`, `LinearScanAllocate`,
  `GraphColoringAllocate` and `ScheduleFunction` into `backends/common/` and add a MachineIR
  Verifier that runs through this same registry.
* Sub-need 2026-04-28-2c rewrites the ABI and relocation models; the registry exposure of
  `RelocationTraits` will land then.
* Sub-need 2026-04-28-2e introduces a real `EmitBitcode` implementation guarded by
  `POLYGLOT_ENABLE_LLVM_BITCODE`.

The registry interface and the macro shape are considered stable and will not change in any of
those sub-needs; only new optional methods may be added with default implementations.
