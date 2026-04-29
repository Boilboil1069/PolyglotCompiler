# PolyglotCompiler Changelog

All notable changes to PolyglotCompiler are documented in this file.

The Chinese counterpart is maintained at [`CHANGELOG_zh.md`](CHANGELOG_zh.md).
For day-to-day usage instructions see [`USER_GUIDE.md`](USER_GUIDE.md); for
build / API contracts see [`api/api_reference.md`](api/api_reference.md) and
the per-feature notes under [`realization/`](realization/).

The version range covered below is **v0.1.0 (2026-01-15) → v1.8.0 (2026-04-29)**.
Newer entries appear first.  Each `### vX.Y.Z (YYYY-MM-DD)` block lists the
shipped behaviour, not the underlying tracking item.

---

## v1.8.0 (2026-04-29)

**Ploy syntax cleanup: a canonical / signed `LINK` form is now the
recommended cross-language linking syntax, and `STAGE` is promoted to
a reserved keyword that may only appear inside a `PIPELINE` body.**

### Added

- Canonical signed `LINK` form:

  ```ploy
  LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type>;
  LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type> {
      MAP_TYPE(<target>, <source>);
  }
  ```

  Parsed by a new `PloyParser::ParseSignedLinkDecl()` and dispatched from
  the existing `ParseLinkDecl()` based on whether `(` follows the `LINK`
  keyword (legacy form) or an identifier does (signed form).

- New `STAGE` keyword (canonical keyword count rises from 71 → 72).
  `ParseStatement` accepts `STAGE [name] CALL <lang>::<module>::<func>;`
  only when the parser is currently inside a `PIPELINE` body
  (`PloyParser::in_pipeline_context_`); it reports a parse diagnostic
  otherwise.

- `LinkDecl::is_legacy_form` boolean discriminator and a new
  `StageDecl{name, call_target, language}` AST node.

- Mirror sample [`tests/samples/01_basic_linking_v2/`](../tests/samples/01_basic_linking_v2/)
  rewriting the v1 cross-language linking sample with the signed form;
  the v1 README is annotated as *legacy form*.

- Two new unit tests under `tests/unit/frontends/ploy/`:
  `link_deprecation_test.cpp` (verifies the `kDeprecatedKeyword` warning
  is emitted) and `stage_misuse_test.cpp` (verifies `STAGE` outside a
  `PIPELINE` is rejected by the parser).

### Changed

- `PloySema::AnalyzeLinkDecl` now emits a `kDeprecatedKeyword` warning
  whenever it encounters a `LinkDecl` whose `is_legacy_form` flag is
  true.
- `docs/realization/ploy_language_spec.md` (and `_zh.md`) §4.2 now
  documents both forms with the signed form listed first as the
  recommendation.
- `docs/USER_GUIDE.md` (and `_zh.md`): top-of-file notice describing the
  deprecation; keyword quick-reference table updated to use the signed
  `LINK` example and to list the new `STAGE` keyword.
- `tests/samples/README.md` (and `_zh.md`): index now lists the v2
  mirror sample and tags the v1 sample as *legacy form*.

### Compatibility

- All 19 per-module test binaries remain green (1215 cases, ~89k
  assertions) on Windows / MSVC.  No source-level break for existing
  Ploy programs: legacy-form `LINK(...)` continues to compile, only
  with an additional warning diagnostic.

---

## v1.7.0 (2026-04-29)

**Ploy gains explicit-width primitive types, `TYPE` aliases, and
compile-time `CONST` declarations with semantic constant folding and
width-mismatch warnings.**

### Added

- 14 new Ploy keywords: `i8` / `i16` / `i32` / `i64`, `u8` / `u16` /
  `u32` / `u64`, `f32` / `f64`, `usize` / `isize`, plus `TYPE` and
  `CONST`.  All keywords participate in the existing case-insensitive
  lexer fold; the canonical keyword count rises from 56 to 71.
- Width-aware primitive resolution in `PloySema::ResolveType`, mapping
  the new keywords to `core::Type::Int(N, sign)` / `core::Type::Float(N)`
  factories that already shipped with the type system.
- `TYPE <name> = <type_expr>;` declarations register named aliases that
  participate in lookup alongside structs.  The reverse map
  `formatted_type → alias_name` lets diagnostics render messages such as
  `Pixel (alias of i32)`.
- `CONST <name>: <type> = <expr>;` declarations are folded by a new
  recursive evaluator covering literals, references to previously
  declared `CONST`s, unary `-` / `!` / `NOT`, and binary arithmetic,
  comparison, and logical operators (string concatenation via `+` is
  also folded).  Width mismatches between the declared type and the
  folded value emit a warning at the `kTypeMismatch` level.
- `INT` and `FLOAT` are now official aliases of `i64` and `f64`
  respectively; existing programs continue to compile unchanged.
- New sample `tests/samples/31_explicit_widths/` exercising `i32` /
  `u32` / `i64` cross-language linking with companion C++ kernel,
  bilingual `README.md` / `README_zh.md`, and the standard
  `expected_output.txt` regression artefact.
- New unit suites `type_alias_test.cpp`, `const_decl_test.cpp`,
  `width_mismatch_diag_test.cpp` (18 cases / 73 assertions) plus an
  integration test `explicit_widths_e2e_test.cpp` that drives the full
  lexer + parser + sema pipeline against an inline program.

### Changed

- `PloySema` now exposes `TypeAliases()`, `ConstantCount()`, and
  `LookupConstantText(name)` accessors so IDE / tooling layers can
  surface alias and constant tables without reflection.
- `PloyLowering` forwards `ConstDecl` through the immutable `VarDecl`
  path and treats `TypeAliasDecl` as a non-executable declaration in
  the synthetic-`main` classifier.
- `docs/specs/language_spec.md{,_zh}` and
  `docs/realization/ploy_language_spec.md{,_zh}` document the new
  keyword set, alias rules, constant-folding contract, and updated
  primitive type table.
- `tests/samples/README.md{,_zh}` index lists the new sample under the
  width-aware-numeric-types theme.

### Compatibility

- Backward compatible.  All pre-existing programs that used `INT` /
  `FLOAT` continue to compile and produce identical IR; the new
  surface keywords resolve through additional lookup paths only.

---

## v1.6.0 (2026-04-29)

**IDE gains a Performance Profiler and a Call Analyzer panel powered by a
shared `ProfileSession`, runtime call-trace + profile sink services and
`polyc`/`polyrt` emitters.**

### What's new

- Runtime services
  - `runtime/include/services/call_trace.h` — `CallTracer` singleton with
    `Enter` / `Exit` / `Drain` / `Peek` / `SerializeJson`, plus C-ABI
    hooks `__ploy_rt_call_enter` / `__ploy_rt_call_exit` / `_enable` /
    `_is_enabled`.  Disabled by default; relaxed-load short-circuit so
    LTO can dead-strip the calls.
  - `runtime/include/services/profile_sink.h` — `ProfileSink::Open(path,
    stream_mode)` with both document mode (`polyglot.profile.v1`) and
    NDJSON streaming mode.
- Middle-end
  - `InstrumentCallTrace` IR pass inserts the C-ABI hooks around every
    non-bridge function when requested.
- polyc CLI
  - `--emit=call-graph:<path>` writes a `polyglot.callgraph.v1` document.
  - `--emit=profile-symbols:<path>` writes the parallel id ↔ qualified
    name map.
  - `--profile-instrument` enables the IR pass above.
- polyrt CLI
  - `polyrt profile [--json|--stream] --duration-ms ... --interval-ms ...`
    samples the running process via the call tracer.
  - `polyrt calltrace --json <path>` drains the current snapshot.
- IDE (`polyui`)
  - New **Profiler** dock panel — Flame / Hotspots / Timeline / Languages
    / Log tabs, driven by `ProfileSession`.  Toggle with `Ctrl+Alt+P`.
  - New **Call Analyzer** dock panel — callers/callees trees + layered
    DAG graph view + nodes table + language-pair filter + bounded DFS
    path search.  Toggle with `Ctrl+Alt+G`.
  - Both panels share a single `ProfileSession`, so loading a profile
    automatically overlays runtime call counts onto the Call Analyzer.
  - Three new shared data models: `FlameTreeModel`, `CallGraphModel`,
    `TimelineModel` (under `tools/ui/common/include/data_models/`).
- Tests
  - `tests/unit/runtime/call_trace_runtime_test.cpp` — 6 cases covering
    enter/exit, nested timing, gating, JSON schema, drain semantics and
    profile sink doc/stream modes.
  - `tests/unit/tools/call_graph_model_test.cpp` — 6 cases for the
    adjacency, DFS and runtime-overlay semantics.
  - `tests/unit/tools/profile_session_test.cpp` — 3 cases for the JSON
    loaders.
  - `tests/unit/tools/profiler_panel_smoke_test.cpp` — 3 cases that
    instantiate both panels and confirm session sharing.
  - `tests/integration/profiler_e2e_test.cpp` — 2 cases that drive the
    real `polyc` binary against `09_mixed_pipeline` and `15_full_stack`.
- Documentation
  - `docs/realization/{profiler,call_analyzer}{,_zh}.md`
  - `docs/specs/{call_graph_schema,profile_stream_schema}{,_zh}.md`
  - `docs/api/profile_api{,_zh}.md`
  - `docs/tutorial/{profiling,call_analyzer}_quickstart{,_zh}.md`

### Compatibility

- Schemas: `polyglot.calltrace.v1`, `polyglot.profile.v1`,
  `polyglot.callgraph.v1`, `polyglot.profilesymbols.v1`.  Adding fields
  is non-breaking; bumping the suffix is reserved for layout changes.
- Shortcuts intentionally avoid `Ctrl+Shift+P/G` (which the IDE already
  binds to the command palette and the git panel).

---

## v1.5.8 (2026-04-29)

**Sample matrix grows to 30 themed programs and ships a regression harness
plus a Catch2 integration test.**

### What's new

- Every existing sample under `tests/samples/` (`01_basic_linking` through
  `16_config_and_venv`) gained:
  - A trailing `PRINTLN "<folder>: ok\r\n";` marker statement so each
    program emits a deterministic, byte-comparable line on stdout.
  - An `expected_output.txt` file containing exactly that line.
  - Bilingual `README.md` / `README_zh.md` describing the languages,
    keywords, build commands and expected runtime output.
- Fourteen brand-new themed samples were authored (`17_string_processing`,
  `18_numeric_kernels`, `19_file_io`, `20_json_pipeline`,
  `21_image_processing`, `22_database_access`, `23_http_client`,
  `24_concurrency`, `25_event_loop`, `26_state_machine`,
  `27_plugin_system`, `28_ml_inference`, `29_data_analytics`,
  `30_game_loop_demo`).  Each contains a `.ploy` entry, two host-language
  source files (real, compilable code — no placeholders), bilingual
  READMEs and `expected_output.txt`.
- `scripts/build_all_samples.ps1` and `scripts/build_all_samples.sh` walk
  every sample folder, run `polyc --emit-obj=…` then `polyld … -o …`,
  execute the binary, capture stdout, byte-compare it against the
  sample's `expected_output.txt`, and write `build/samples_report.json`.
  Per-sample status is one of
  `OK / OUTPUT_MISMATCH / EMPTY_STDOUT / RUN_FAIL / LINK_FAIL / COMPILE_FAIL / SKIP`.
  The harness exits 0 by default; pass `-FailOnMismatch` (PowerShell) or
  `--fail-on-mismatch` (bash) to switch to strict gating.
- `tests/integration/samples_regression_test.cpp` (Catch2 tag
  `[samples][b6][integration]`, registered in `integration_tests`) drives
  the harness and asserts the produced JSON report is well-formed:
  every sample is represented, every status string is one of the seven
  enum values above, and the recorded total matches the on-disk folder
  count.
- The root `tests/samples/README.md` (and Chinese twin) was rebuilt to
  list all 30 samples in a directory matrix, plus by-theme and
  by-language-combination indices.

### Notes

- The harness is intentionally tolerant by default because parts of the
  end-to-end backend pipeline are still maturing; the JSON report itself
  is the contract under test.  As more samples reach `OK` the strict mode
  becomes the natural release gate.
- Existing test suites (`test_linker`, `test_frontend_ploy`, `test_e2e`,
  `integration_tests`) are unaffected by this release.

---

## v1.5.7 (2026-04-29)

**`polyld` recovers `polyrt_println` call sites from input objects and
emits a multi-message Windows PE.**

### What's new

- New public free function
  `polyglot::linker::CollectPolyrtPrintlnSequence(const std::vector<ObjectFile> &)`
  performs a pure analysis pass over the linker's loaded object state and
  returns the source-order vector of decoded message payloads that the
  IR-layer `polyrt_println` lowering left as `(lea message-global,
  call polyrt_println)` reloc pairs inside every executable input section.
  The pass:
  - Walks each `ObjectFile` and every section flagged
    `SectionFlags::kExecInstr`.
  - Sorts each section's relocations by `offset` so loaders that surface
    relocs in file order rather than instruction order still produce a
    correct sequence.
  - Tracks the most-recent `println.msg<N>(.ptr)?` reloc as the pending
    message cursor and pairs it with the next `polyrt_println` reloc;
    the trailing `.ptr` GEP-alias suffix introduced by the IR string
    interner is stripped before symbol lookup.
  - Resolves the message global by linear-scanning every loaded
    `ObjectFile` (cross-translation-unit sharing works), reads
    `data[symbol.offset .. +symbol.size)` from its containing section,
    and appends the decoded bytes to the result.
  - Silently skips orphan calls (no preceding message reloc) and
    unresolvable symbols, leaving the rest of the recovered sequence
    intact.
  - Faithfully mirrors call-order semantics — duplicate payloads from
    an interned global are emitted at every call site; the
    `BuildPrintlnSequencePE` layer is the one that re-deduplicates the
    underlying `.rdata` storage.

- `polyglot::linker::Linker::GeneratePEExecutable` now invokes the new
  pass; when the recovered vector is non-empty it routes through
  `pe::BuildPrintlnSequencePE` to produce a real multi-line stdout
  binary, otherwise it preserves the legacy
  `pe::BuildExitZeroPE(user_text)` path bit-for-bit so non-PRINTLN
  programs are unaffected.

### Observable behaviour

A `.ploy` source as small as

```ploy
PRINTLN "alpha\r\n";
PRINTLN "beta\r\n";
PRINTLN "alpha\r\n";
```

now produces, after `polyc … && polyld … -o demo.exe`, an executable
whose captured stdout is exactly

```
alpha
beta
alpha
```

with three real `WriteFile` calls into `STD_OUTPUT_HANDLE` followed by
`ExitProcess(0)`.  The integration test
`tests/integration/pe_runtime_smoke_test.cpp` (`[b5]` tag) builds the
ObjectFile shape `polyc` would emit, runs the recovered image on the
host loader, and pins both the exit code and the captured byte
sequence.

### New public APIs

- `polyglot::linker::CollectPolyrtPrintlnSequence`
  (`tools/polyld/include/linker.h`).

### Tests

- `tests/unit/linker/polyrt_println_collect_test.cpp` — 10 Catch2 cases
  covering empty input, no-PRINTLN object, single call, multi-call
  ordering, interned-duplicate emission, `.ptr` GEP-alias resolution,
  cross-object data globals, unsorted-reloc auto-sort, defensive
  filtering of mis-flagged sections, and orphan-call skipping.
- `tests/integration/pe_runtime_smoke_test.cpp` — new `[b5]` end-to-end
  case asserts exit code 0 and exact stdout match for a three-call
  duplicate-bearing program after running it on the host Windows
  loader.

### Verified suites

`test_linker` (337 assertions / 68 cases), `test_frontend_ploy` (1907
assertions / 310 cases), `test_e2e` (171 assertions / 54 cases),
`integration_tests` (`[b5]` filter, 6 assertions / 1 case) — all
green.

---

## v1.5.6 (2026-04-29)

**`.ploy` -> `.obj` -> `.exe` -> live process exit code is now real.**

### What's new

- `polyc` now emits genuine AMD64 / ARM64 COFF object files when invoked
  with `--obj-format=coff` (previously the writer silently fell through
  to the ELF builder, which produced bytes the Microsoft linker
  rejected with `LNK1107: invalid or corrupt file`).  The new
  `polyglot::backends::COFFBuilder` lays down a complete `IMAGE_FILE_HEADER`,
  per-section headers (`.text`, `.rdata`, `.bss`, …), raw section
  contents, per-section relocations, a COFF symbol table (with
  short-name and long-name string-table forms) and a string table that
  MS `link.exe` accepts byte-for-byte.
- `polyld`'s `DetectObjectFormat` now identifies a raw COFF object via
  its `IMAGE_FILE_HEADER.Machine` field (`0x8664`, `0xAA64`, `0x014C`,
  `0x01C0`, `0x01C4`, `0x0200`).  The earlier MZ-stub heuristic
  recognised only fully-linked PE images and silently misclassified raw
  `.obj` files, leaving the loader to fall through to the POBJ branch
  and discard the section contents.
- `polyld`'s Win32 PE writer now resolves a user entry symbol
  (`_start` -> `__ploy_main` -> `main`, in priority order), translates
  it through the section-contribution map to its merged-`.text` offset,
  and produces an `.exe` whose `AddressOfEntryPoint` runs an 18-byte
  Win64-ABI shim that invokes the user `main` and forwards its `int`
  return value (low 32 bits of `RAX`) to `kernel32!ExitProcess`.  The
  previous build always pointed entry at the legacy
  `ExitProcess(0)` shim regardless of what the source program returned.

### Observable behaviour

A `.ploy` source as small as

```ploy
FUNC main() -> i32 { RETURN 42; }
```

now produces, after `polyc audit_hello.ploy --emit-obj=audit_hello.obj
--obj-format=coff && polyld audit_hello.obj -o audit_hello.exe`, an
executable whose Windows `GetExitCodeProcess` returns **42**.  The same
contract holds for `RETURN 0` and `RETURN 7`; the integration test
`tests/integration/ploy_e2e_real_exit_code_test.cpp` pins all three.

### New public APIs

- `polyglot::backends::COFFBuilder` — `ObjectFileBuilder` subclass that
  emits AMD64 (`is_arm64=false`) or ARM64 (`is_arm64=true`) raw COFF
  objects.
- `polyglot::linker::pe::BuildExeWithUserEntry(user_text_bytes,
  user_main_offset_in_text)` — wraps caller-supplied `.text` with a
  user-entry-then-`ExitProcess(eax)` shim.
- `polyglot::linker::pe::BuildUserMainExitShim(shim_rva, user_main_rva,
  exit_process_iat_rva)` — exposes the 18-byte AMD64 shim encoder for
  unit testing.

### Test coverage

- `tests/unit/linker/object_format_detect_test.cpp` (9 cases): pins
  `DetectObjectFormat`'s handling of raw COFF (all six recognised
  Machine values), MZ-stubbed PE images, ELF, Mach-O and POBJ magic.
- `tests/unit/backends/coff_builder_test.cpp` (4 cases): byte-level
  validation of AMD64 + ARM64 headers, external symbol layout in the
  COFF symbol table, and long-section-name encoding via the string
  table.
- `tests/unit/linker/section_merge_test.cpp` (2 cases): regression
  pinning that user `.text` bytes from a real COFF object survive the
  load -> resolve -> layout pipeline byte-for-byte and that
  multi-object `.text` inputs are concatenated with both blobs
  preserved.
- `tests/integration/ploy_e2e_real_exit_code_test.cpp` (3 cases): full
  `.ploy` -> `.obj` -> `.exe` -> spawn -> `GetExitCodeProcess`
  smoke for the literals 42, 0 and 7.

### Compatibility

- `BuildExitZeroPE` is still exported and is the fallback used when no
  user entry symbol is defined; existing callers that relied on the
  previous "always exits 0" behaviour are unaffected.
- The COFF detection change is a strict superset of the previous MZ
  rule, so any input that was previously recognised as a PE image still
  is.

---

## v1.5.5 (2026-04-29)

**`BuildPrintlnSequencePE` ships — Stage B4 of the runtime-stdout pipeline (demand `2026-04-28-49`).**

### What's new

- New public PE writer entry point `polyglot::linker::pe::BuildPrintlnSequencePE(const std::vector<std::string> &call_messages)`
  produces a runnable, self-contained PE32+ image that issues one
  `kernel32!WriteFile` call per entry of `call_messages` (in the supplied
  order) against `STD_OUTPUT_HANDLE`, then terminates the process via
  `kernel32!ExitProcess(0)`.  This is the first stage of the pipeline that
  emits **real AMD64 machine code driving Windows stdout** for the
  artefacts produced by the IR-layer `polyrt_println(i8*, i64)` calls
  shipped in v1.5.4.

### Win64 ABI shim contract

- The `.text` payload is laid out as `prologue (0x25 B) + N × per-message
  block (0x1D B) + epilogue (0x09 B)`.  All offsets are byte-stable and
  unit-tested.
- Prologue: `sub rsp, 0x38` reserves Win64-mandated 32-byte shadow space
  + an 8-byte `lpOverlapped` slot (zeroed once) + an 8-byte
  `lpNumberOfBytesWritten` slot (zeroed once) + an 8-byte stdout-handle
  cache.  Then `GetStdHandle(-11)` is invoked once and the returned
  handle is parked in `[rsp+0x30]` so subsequent WriteFile calls can
  reload it without spilling additional callee-saved registers.
- Per-message block: `mov rcx, [rsp+0x30]; lea rdx, [rip+msg_i];
  mov r8d, len_i; lea r9, [rsp+0x28]; call qword ptr [rip+WriteFile]`.
  All RIP-relative displacements are computed from the block's own RVA
  so the shim is position-correct regardless of how many messages
  precede it.
- Epilogue: `xor ecx, ecx; call qword ptr [rip+ExitProcess]; int3`
  (the `int3` is unreachable but pads the shim to a deterministic length).

### Dedup & layout contract

- Identical message bytes share their `.rdata` storage (linear scan over
  the unique-payload table), exactly mirroring the IR-layer
  `IRBuilder::MakeStringLiteral` interning contract from v1.5.4.  A
  program that calls `PRINTLN "hello\n";` ten times pays for ten
  `WriteFile` blocks but only one `.rdata` payload.
- `call_messages.empty()` forwards to `BuildExitZeroPE({})`, producing a
  byte-identical image — a degenerate empty shim is never emitted.
- Each message size is bounded by `0xFFFFFFFFu` (WriteFile's `nNumberOfBytesToWrite`
  DWORD); oversized entries cause the call to return an empty `BuildResult`.
- The 3-section layout (`.text` + `.rdata` + `.idata`) and the
  `BuildPE32PlusImage` plumbing are reused verbatim from
  `BuildHelloWorldPE`, so all the section-header / IAT / import-descriptor
  invariants exercised by the existing v1.5.1 tests continue to hold.

### Tests added

- `tests/unit/linker/pe_writer_test.cpp` (+4 cases, +30 assertions) —
  empty-input forwarding, single-message structural shape, three-message
  unrolled `.text` size, and dedup `.rdata` shrinkage.
- `tests/integration/pe_runtime_smoke_test.cpp` (+1 case, +5 assertions) —
  spawns a 3-message PE with one duplicated payload, redirects stdout to
  a temp file, and byte-compares against the expected `"alpha\r\nbeta\r\nalpha\r\n"`
  concatenation.
- `tools/polyld/src/pe_writer_smoke.cpp` (+1 manual harness block) —
  end-to-end visual confirmation: the produced `pe_smoke_println.exe`
  prints all three lines and exits 0 on the host loader.

### Regression results

- `test_linker`: 56 cases / 284 assertions ✅ (up from 52 cases in v1.5.4).
- `test_frontend_ploy`: 310 cases / 1907 assertions ✅ (unchanged).
- `test_e2e`: 54 cases / 171 assertions ✅ (unchanged).
- `integration_tests`: 130 cases / 552 assertions ✅ (up from 129 in v1.5.4).

### What's next

- **B5**: teach polyld to extract `polyrt_println` callsites and their
  interned string payloads from object files and feed them as
  `call_messages` into `BuildPrintlnSequencePE`, replacing the dummy
  exit-zero shim it emits today.
- **B6**: end-to-end `.ploy → .obj → .exe` pipeline; extend the
  demand-04 expected_output harness to byte-compare actual stdout.

---

## v1.5.4 (2026-04-29)

**`PrintlnStmt` is now lowered to IR — Stage B3 of the runtime-stdout pipeline (demand `2026-04-28-49`).**

### Lowering contract

- `PRINTLN "literal";` lowers to two IR artefacts:
  1. An interned global of type `[N x i8]` holding the **decoded** bytes
     (the front-end deliberately preserves backslash escapes, so the
     decoding table — `\n`, `\r`, `\t`, `\\`, `\"`, `\0`, `\xHH` — lives
     here in the lowering layer, the single source of truth that the
     interpreter, IR text round-trip, and codegen all agree on).
  2. A direct `call void @polyrt_println(i8* msg, i64 len)` instruction
     emitted into the active basic block.  The callee is intentionally
     external; B5 will resolve it through polyld's runtime DLL import.
- The pointer + length convention (rather than NUL-termination) lets
  embedded `\0` bytes round-trip cleanly and lets the empty-literal case
  (`PRINTLN "";`) lower to a single `polyrt_println(ptr, 0)` call with
  no special-casing downstream.
- Identical literals share their interned global thanks to
  `IRBuilder::MakeStringLiteral` content-keyed deduplication, so
  `.rdata` stays compact across all object formats.
- Top-level `PRINTLN` lands in the auto-created `entry_fn` per the
  existing `IRContext::DefaultBlock` convention; `PRINTLN` inside a
  `FUNC` body lands in that function's basic block, which is the
  property B4 codegen depends on for stack-frame correctness.
- Unknown / malformed escape sequences (e.g. `\q`, `\xZZ`) raise a
  `kGenericWarning` diagnostic and the offending bytes are preserved
  verbatim — lowering never aborts on bad escapes so the rest of the
  module can still be inspected.

### Implementation

- `frontends/ploy/include/ploy_lowering.h` — new
  `LowerPrintlnStatement(const std::shared_ptr<PrintlnStmt> &)` declaration.
- `frontends/ploy/src/lowering/lowering.cpp`:
  - `LowerStatement` dispatcher recognises `PrintlnStmt`.
  - New `DecodePrintlnLiteral(...)` helper (anonymous namespace) owns the
    escape-decoding table; designed so future additions (`\u{…}`,
    `\u00XX`, `\b`, `\f`) are a single `case` away.
  - `LowerPrintlnStatement` interns the decoded bytes via
    `builder_.MakeStringLiteral(...)` and emits the `polyrt_println` call.
- No header surface change beyond the one new method; binary-compat for
  existing front-end consumers preserved.

### Tests

- `tests/unit/frontends/ploy/println_lowering_test.cpp` (5 cases / 21
  assertions): IR-level decoding contract, empty-message corner, global
  deduplication, in-function basic-block placement, and the
  warning-but-no-abort path for unknown escapes.
- Full ploy frontend suite stays green: 310 cases / 1907 assertions.
- `test_linker` (43/245), `test_e2e` (54/171), `integration_tests`
  (129/547) all green — `polyrt_println` is currently an unresolved
  external in object files, but no existing test exercises an end-to-end
  link of a PRINTLN-bearing module yet (that wiring is B5).

### Demand tracking

- demand `2026-04-28-49` Stage B3 marked `[done]`.  Stage B4 (AMD64
  codegen for `polyrt_println` callsites + Win64 ABI shadow space) is
  the next milestone.

---

## v1.5.3 (2026-04-28)

**Ploy front-end gains the `PRINTLN "literal";` statement — Stage B2 of the runtime-stdout pipeline (demand `2026-04-28-49`).**

### Language

- New top-level / in-block statement: `PRINTLN STRING ';'` writes a single
  string literal to the host's standard output.  This is intentionally the
  smallest possible runtime-IO primitive; expressions, concatenation and
  formatting will land in later stages.  The trailing `';'` is mandatory.
- The literal's bytes are stored verbatim on the AST node — surrounding
  double-quotes are stripped, but backslash escapes (`\r`, `\n`, `\t`,
  `\\`, `\"`, …) are *not* decoded by the front-end.  The single canonical
  decoder will live in the codegen stage (B4) so that interpreters,
  IR-text round-trips and the .rdata payload all agree on one
  interpretation.
- The `PRINTLN` keyword follows the same case-insensitive rule as every
  other Ploy keyword (`println`, `Println`, `PRINTLN` are equivalent),
  thanks to the lexer canonicalisation introduced in v1.5.2.

### Front-end plumbing

- `frontends/ploy/include/ploy_ast.h` — new `PrintlnStmt : Statement`
  struct carrying a single `std::string message` field.
- `frontends/ploy/src/lexer/lexer.cpp` — added `"PRINTLN"` to the
  canonical keyword set.
- `frontends/ploy/src/parser/parser.cpp` — `ParseTopLevel` and
  `ParseStatement` both dispatch the new keyword to the new
  `ParsePrintlnStatement()` helper, which emits a `PrintlnStmt` and
  reports a diagnostic (and resyncs) if the next token is not a string.
  `Sync()`'s recovery keyword set now includes `PRINTLN`.
- `frontends/ploy/src/sema/sema.cpp` — `AnalyzeStatement` accepts
  `PrintlnStmt` as a no-op; the parser already enforces the shape and
  empty messages are intentionally legal.

### Tests

- `tests/unit/frontends/ploy/println_stmt_test.cpp` (6 cases, 37 assertions):
  top-level parsing, escape-byte preservation, case-insensitive keyword,
  in-function dispatch through `ParseStatement`, error recovery on
  non-string operand, and sema acceptance of the empty-message corner case.
- Full ploy frontend suite stays green (305 cases / 1886 assertions).
- Linker (43/245), end-to-end (54/171) and integration (129/547) regression
  suites continue to pass with no behaviour changes — `PrintlnStmt` is
  ignored by the lowering stage until B3 wires it into IR.

### Compatibility

- `PRINTLN` was previously parsed as an identifier; any program using it
  as a variable / function / pipeline name must rename that identifier.
  The bundled samples and test fixtures do not use it.
- No public API removed; `PrintlnStmt` is purely additive.

### Demand tracking

- demand `2026-04-28-49` Stage B2 marked `[done]`.  Stage B3 (lowering to
  IR) is the next milestone.

---

## v1.5.2 (2026-04-28)

**Ploy lexer cleanup — keywords are now recognised case-insensitively, the legacy `RETURNS` clause is deprecated, and `AND` / `OR` / `NOT` are formal aliases of `&&` / `||` / `!`.**

### Lexical rules

- The Ploy lexer accepts every reserved word in any letter-casing
  (`link`, `Link`, `LINK`, `LiNk`).  The token's `Token::lexeme` is
  always normalised to the canonical UPPER-case spelling so the parser
  stays a single string-comparison without per-call folding.  The
  user's original spelling is preserved on a new `Token::raw_lexeme`
  field and surfaced through `Token::SourceText()` so diagnostics and
  source-faithful formatters keep printing what the user typed.
- Identifiers remain *case-sensitive*: only the keyword set is folded.
  This is a deliberate trade-off to keep the parser logic uniform and
  the diagnostics actionable.
- **Reserved-word collisions.** Identifiers that previously differed
  from a keyword only by case (`config`, `array`, `get`, `set`,
  `pipeline`, `new`, …) are now reserved.  Migrate them to non-keyword
  names (for example `app_config`, `np_array`, `getter`).  The bundled
  unit / integration / benchmark test fixtures have been updated
  accordingly; user-facing samples under `tests/samples/` already use
  UPPER-case keywords and required no change.

### Operator aliases

- `AND` / `OR` / `NOT` keywords parse to *exactly* the same AST nodes
  as the symbolic `&&` / `||` / `!` operators (`BinaryExpression::op
  == "||"`, `UnaryExpression::op == "!"`).  No downstream stage has to
  branch on spelling.
- The symbolic forms are documented as the preferred style for new
  code; both forms remain permanent.

### `RETURNS` deprecation

- The legacy `LINK(...) RETURNS Type { ... }` syntax still parses and
  still populates `LinkDecl::return_type`, but the parser now emits a
  `kDeprecatedKeyword` warning (`ErrorCode = 3024`) that echoes the
  user's source spelling so the warning is greppable in their tree.
- New code should declare the return type via the canonical `-> Type`
  arrow on the LINK signature.  No automatic rewrite is performed; the
  warning is the migration signal.

### Compatibility

- Existing `tests/samples/01_basic_linking..16_*` programs continue to
  parse, semantically analyse and lower exactly as before — they all
  use UPPER-case keywords already.
- The shared `frontends/common::Token` struct gains a single new
  string field (`raw_lexeme`) at the *end* of the struct so every
  three- and four-argument aggregate-initialisation call site in the
  C++/Java/Python/Rust/.NET/JavaScript lexers compiles unchanged.
- The new `frontends::ErrorCode::kDeprecatedKeyword = 3024` slot sits
  between `kSignatureMissing = 3023` and `kGenericWarning = 3099` and
  is a non-fatal warning category.

### Tests

- `tests/unit/frontends/ploy/lexer_case_insensitive_test.cpp` — covers
  every one of the 56 keywords in lower / mixed / UPPER spellings,
  asserts identifiers stay case-sensitive, and verifies that words
  beginning with a keyword-prefix (`letter`, `iffy`, `linker`, …) are
  not split.
- `tests/unit/frontends/ploy/keyword_alias_test.cpp` — proves that
  `AND/OR/NOT` and `&&/||/!` produce structurally identical ASTs and
  that the `RETURNS` clause emits exactly one `kDeprecatedKeyword`
  warning whose message echoes the source spelling for both UPPER and
  lower-case inputs.

---

## v1.5.1 (2026-04-28)

**Runtime-IO milestone for the PE32+ writer — produced `.exe` images can now write to standard output before exiting.**

The v1.5.0 PE writer could only emit a self-terminating image whose entry
shim called `kernel32!ExitProcess(0)`.  Real samples need to drive at
least one user-visible side effect before terminating, so this patch
extends the writer with the minimum surface required to perform a
synchronous `WriteFile` against the host's standard output handle.

What ships:

- **`.rdata` section support in `BuildPE32PlusImage`.**  `BuildRequest`
  gained an optional `rdata_bytes` field; `BuildResult` reports the
  resulting section's RVA back to the caller via `rdata_rva`.  When
  `rdata_bytes` is non-empty the writer lays out
  `[.text][.rdata][.idata]` (3 sections) instead of `[.text][.idata]`
  (2 sections).  `SizeOfInitializedData` accumulates both `.rdata` and
  `.idata` raw sizes per the PE/COFF spec.

- **New factory `BuildHelloWorldPE(message)`** in
  `tools/polyld/include/pe_writer.h`.  Produces a fully runnable PE32+
  whose entry executes the AMD64 sequence
  `GetStdHandle(STD_OUTPUT_HANDLE) → WriteFile(handle, msg, len, &n, NULL)
  → ExitProcess(0)`.  The 68-byte entry shim is constant-size so
  `.rdata` and `.idata` RVAs can be fully resolved before code emission.
  Stack frame `sub rsp, 0x38` provides the 32-byte shadow space, the
  WriteFile ARG5 slot at `[rsp+0x20]`, and the bytes-written DWORD slot
  at `[rsp+0x28]` while preserving the Win64 ABI's 16-byte alignment
  requirement before each child `CALL`.

- **Three-import support is wire-tested.**  `BuildImportSection` already
  supported N functions per DLL; this release exercises that path with
  `kernel32.dll!{GetStdHandle, WriteFile, ExitProcess}` and validates
  every IAT slot RVA is reported back through `BuildResult::iat_slot_rva`.

- **`pe_smoke` harness extended** to also build, write, spawn, and
  validate a hello-world image alongside the existing minimal-exit one.

- **New unit tests** in `tests/unit/linker/pe_writer_test.cpp`:
  3-section layout assertion, IAT slot enumeration for all three kernel32
  imports, on-disk byte-for-byte preservation of the `.rdata` payload,
  shim prologue (`48 83 EC 38`) and trailing `int3`, and an empty-text
  rejection guard for `BuildPE32PlusImage`.

- **New integration test** in
  `tests/integration/pe_runtime_smoke_test.cpp`: builds a hello-world PE
  in-process, writes it to a temp path, spawns it under `cmd /c` with
  stdout redirected to a temp `.out` file, and asserts both the exit code
  and the captured bytes equal the injected message.  Required an extra
  outer pair of quotes around the redirected command — `std::system`
  forwards to `cmd /c <string>`, which strips one layer of quotes before
  re-tokenising.

Validated regression: `test_linker` 43/43 (245 assertions),
`integration_tests` 129/129 (547 assertions), `test_e2e` 54/54 (171
assertions).  No public API of `Linker` changed; the new pe_writer surface
is purely additive.

Known not-yet-shipped: the `polyc → polyld` pipeline still passes an
empty `.text` to `BuildExitZeroPE` for `.ploy` inputs, so `polyc
hello.ploy -o hello.exe` produces an exit-zero image rather than a
hello-world image.  Connecting `BuildHelloWorldPE` (and a generalised
runtime-IO emitter) to the front end is the next milestone in the
multi-stage runtime-stdout roadmap.

---

## v1.5.0 (2026-04-28)

**Native Windows PE32+ executable emission — `polyc tiny.ploy -o tiny.exe` now produces a runnable `.exe`**

Before this release, the bundled `polyld` always emitted ELF executables,
even when the requested output had a `.exe` suffix and even when running
on a Windows host.  The result was a 423-byte ELF file masquerading as
`tiny.exe` that the Windows loader refused to execute (`'tiny.exe' is not
a valid Win32 application`).  Users who wanted a runnable `.exe` had to
install MSVC `link.exe` or LLVM `lld-link` and rely on the probe-then-
invoke fallback shipped in v1.4.1; on hosts without a system linker
there was no path forward at all.

This release ships an in-tree PE32+ image writer so the bundled `polyld`
can produce a real Windows AMD64 console executable end-to-end:

- New module `tools/polyld/{include,src}/pe_writer.{h,cpp}` lays out a
  fully valid PE32+ image: 64-byte DOS header with a conventional stub,
  `PE\0\0` signature, COFF File Header (Machine = `IMAGE_FILE_MACHINE_AMD64`,
  `Characteristics` = `RELOCS_STRIPPED | EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE`),
  240-byte PE32+ Optional Header (Magic = `0x020B`, Subsystem = Windows
  console, `DllCharacteristics` = `NX_COMPAT | TERMINAL_SERVER_AWARE`,
  16 data directories), section table, padded headers, `.text` (RX) and
  `.idata` (RW) sections.  The `.idata` layout includes the
  `IMAGE_IMPORT_DESCRIPTOR` array, ILT, IAT, `IMAGE_IMPORT_BY_NAME`
  records and the DLL name strings, with the data directory entries `[1]`
  (Import) and `[12]` (IAT) populated and pointing at the right RVAs.
- New entry shim `BuildExitProcessShim()` emits the canonical 13-byte
  AMD64 sequence
  `48 83 EC 28  31 C9  FF 15 disp32  CC`
  (`sub rsp, 0x28; xor ecx, ecx; call qword ptr [rip+disp32]; int3`).
  The `sub rsp, 0x28` reserves the 32-byte shadow space and re-aligns
  the stack to 16 bytes that the Windows x64 ABI requires before any
  call into an imported function.
- New helper `BuildExitZeroPE(user_text_bytes)` wraps a caller-supplied
  `.text` byte buffer with the entry shim appended at the end.  The
  produced image still embeds the user code on disk (so future linker
  iterations can re-target `AddressOfEntryPoint` at a real `main` once
  Win32 ABI translation is in place) but the entry currently runs the
  shim, guaranteeing the image terminates cleanly via
  `kernel32!ExitProcess(0)`.
- `OutputFormat::kPEExecutable` is added to the linker enum, with a
  new `Linker::GeneratePEExecutable()` that pulls the merged `.text`
  output section bytes through `BuildExitZeroPE` and writes the result
  to `config_.output_file`.
- `polyld` auto-selects `kPEExecutable` whenever the `-o` argument ends
  in `.exe` (case-insensitive); on Windows hosts it also defaults to
  PE even without the suffix.  Two new explicit flags are accepted:
  `--pe` / `--output-format=pe` and `--elf` / `--output-format=elf`.

Quality gates exercised before merge:

- New Catch2 unit suite `tests/unit/linker/pe_writer_test.cpp` (4 cases,
  47 assertions) validates MZ/PE magic, e_lfanew, COFF Characteristics,
  Optional Header Magic, AddressOfEntryPoint, Subsystem,
  `DllCharacteristics`, the 16-entry data directory, the populated
  Import + IAT directories, the 13-byte shim shape, the round-tripping
  of user `.text` bytes through `BuildExitZeroPE`, and the equivalence
  of `BuildExitZeroPE({})` to `BuildMinimalExitZeroImage()`.
- New Windows-only integration suite
  `tests/integration/pe_runtime_smoke_test.cpp` (2 cases, 8 assertions)
  builds the PE in-process, writes it to a temp file, spawns it via
  `std::system`, and asserts the exit code is 0 — both for the minimal
  image and for an image wrapping 256 bytes of arbitrary user code.
- End-to-end repro: `polyc tests/samples/01_basic_linking/basic_linking.ploy
  -o tests/samples/01_basic_linking/test_bin.exe` now produces a 1536-byte
  PE32+ (`MZ` magic, `dumpbin /imports` shows
  `kernel32.dll: ExitProcess` resolved cleanly) that exits with code 0.
- Existing regressions kept green: `test_linker` 39/39, `test_e2e` 54/54.

Known limitations of this iteration (tracked for a future release):

- No base relocation table is emitted.  `IMAGE_FILE_RELOCS_STRIPPED` is
  set so the loader honours the preferred `ImageBase` (`0x140000000`).
- No exports, resources, TLS or debug directories.
- `AddressOfEntryPoint` always points at the appended shim; the user's
  ELF-style `.text` bytes are embedded but not invoked.  Real `main`
  re-targeting will land together with the COFF object loader and the
  Win32 ABI translation layer.

## v1.4.1 (2026-04-28)

**Probe-then-invoke linker selection — silence raw shell noise**

Previously, the staged compilation pipeline and the legacy single-pass
driver in `polyc` both unconditionally invoked `link.exe` and then
`lld-link` via `std::system()`.  When neither was on `PATH` (the typical
case for users running `polyc` outside an MSVC "Developer Command
Prompt"), Windows CMD itself printed
`'link' is not recognized as an internal or external command` (or the
localized equivalent, e.g. `'link' 不是内部或外部命令`) to `stderr`,
leaking raw shell noise into `polyc` output even though the pipeline
reported success and the `.obj` was produced correctly.

- ✅ New shared module `tools/polyc/include/linker_probe.h` +
  `tools/polyc/src/linker_probe.cpp` exporting
  `polyglot::tools::linker_probe::{IsExecutableOnPath, ShellQuote,
  LinkerChoice, SelectAvailableLinker, ExpandLinkCommand}`.  The probe
  uses `where` (Windows) / `command -v` (POSIX) with both `stdout` and
  `stderr` redirected to the platform null sink, so the probe itself
  never produces visible output.
- ✅ Per-format selection priority lists:
  - `pobj`  → bundled `polyld` (canonical consumer; no native fallback);
  - `coff`  → MSVC `link` → LLVM `lld-link` → bundled `polyld`;
  - `macho` → `clang` → `ld` → bundled `polyld`;
  - `elf`   → `clang` → `gcc` → `ld` → bundled `polyld`.
  The bundled `polyld` is the universal fallback because its loader
  (`tools/polyld/src/linker.cpp`) detects COFF / ELF / Mach-O input by
  magic bytes and produces a matching native executable, so a `polyc`
  invocation can always link successfully on a host that ships only the
  Polyglot toolchain itself.
- ✅ Both call sites refactored to share the new module:
  `tools/polyc/src/compilation_pipeline.cpp` (staged pipeline,
  `mode == "link"` branch) and `tools/polyc/src/stage_packaging.cpp`
  (legacy single-pass driver, `emit_and_link` lambda).
- ✅ Verbose mode now logs the actually-selected linker:
  `[polyc] Invoking polyld (fallback) -> a.out` (was hard-coded
  `Invoking link.exe` even when `link.exe` was never invoked).
- ✅ `ShellQuote` handles paths with embedded spaces using
  platform-appropriate quoting (Windows: `"…"`; POSIX: `'…'` with
  embedded-quote escaping `'\''`).  Space-free paths are passed through
  untouched.
- ✅ New unit test file `tests/unit/tools/linker_probe_test.cpp`
  (7 cases, 23 assertions): `ShellQuote` quoting rules; rejection of
  empty / bogus executable names; acceptance of an existing absolute
  path via the stat-check fast path; empty `LinkerChoice` for `pobj`
  when `polyld` is bogus; non-empty `LinkerChoice` for `pobj` when
  `polyld` is reachable; `ExpandLinkCommand` placeholder substitution
  and polyld-only flag gating.
- ✅ Reproducer `polyc.exe tests/samples/03_pipeline/pipeline.ploy`
  now exits 0 with zero shell-noise lines on a host without
  `link.exe` / `lld-link.exe` on `PATH`; the link stage transparently
  falls back to bundled `polyld`.

> No source / ABI break: the project version stays at **1.4.0** in the
> root `CMakeLists.txt`; this is a bug-fix patch surfaced under the
> v1.4.1 changelog narrative because the user-visible behaviour has
> changed (the noise lines are gone).

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
