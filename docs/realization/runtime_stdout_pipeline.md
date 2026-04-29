# Runtime stdout pipeline

This document describes the end-to-end pipeline that turns a `.ploy`
`PRINTLN "literal";` statement into bytes on the host process's standard
output.  The pipeline is built incrementally as eight stages B1鈥揃8 tracked by
demand `2026-04-28-49`; stages **B1 through B4** are shipped, the rest are
referenced here only to anchor the contracts the shipped layers must keep
satisfying.

The Chinese counterpart of this document lives at
[`runtime_stdout_pipeline_zh.md`](runtime_stdout_pipeline_zh.md).

---

## 1. Layer map

```
+------------------+   PRINTLN "hi";       (.ploy source)
|  B2  ploy front  |
+------------------+
        |  PrintlnStmt AST node + sema validation
        v
+------------------+
|  B3  ploy lower  |
+------------------+
        |  IR:  @str.<hash> = constant [N x i8] c"..."
        |       call void @polyrt_println(i8* ptr, i64 len)
        v
+------------------+
|  B4  PE writer   |    BuildPrintlnSequencePE(call_messages)
+------------------+
        |  AMD64 .text  +  .rdata payload  +  .idata kernel32 imports
        v
+------------------+
|  Windows loader  |   GetStdHandle  +  WriteFile  +  ExitProcess
+------------------+
```

Each stage is independently unit-tested and the contract between adjacent
stages is byte-stable so later stages can be retargeted without disturbing
earlier ones.

---

## 2. B2 鈥?ploy front-end (shipped in v1.5.3)

The lexer recognises the keyword `PRINTLN`, the parser produces a
`PrintlnStmt` AST node carrying the **raw** literal bytes (escape sequences
left untouched), and the sema layer validates that the literal is a
well-formed quoted-string token and that the statement is terminated by `;`.

The deliberate decision to keep escape sequences raw at the front-end is
load-bearing: the IR lowering layer (B3) is the **single source of truth** for
the decoding table (`\n`, `\r`, `\t`, `\\`, `\"`, `\0`, `\xHH`).  The
interpreter, the IR text round-trip and the codegen all consume the decoded
bytes, so any future addition to the decoding table only needs to be made in
one place.

---

## 3. B3 鈥?IR lowering (shipped in v1.5.4)

A `PrintlnStmt` lowers to two IR artefacts:

1. **An interned global** of type `[N x i8]` holding the *decoded* message
   bytes.  The global is content-keyed and deduplicated through
   `IRBuilder::MakeStringLiteral`, so a program that PRINTLNs the same
   literal twice still pays for only one global.
2. **A direct call** `call void @polyrt_println(i8* msg, i64 len)` emitted
   into the active basic block.  The callee is intentionally external; later
   stages resolve it through the linker's runtime DLL import path.

The `(ptr, len)` convention rather than NUL-termination lets embedded `\0`
bytes round-trip cleanly and lets the empty-literal case
(`PRINTLN "";`) lower to a single `polyrt_println(ptr, 0)` call with no
special-casing downstream.

Top-level `PRINTLN` lands in the auto-created `entry_fn` per the existing
`IRContext::DefaultBlock` convention; `PRINTLN` inside a `FUNC` body lands in
that function's basic block.

---

## 4. B4 鈥?`BuildPrintlnSequencePE` (shipped in v1.5.5)

`polyglot::linker::pe::BuildPrintlnSequencePE(call_messages)` produces a
runnable, self-contained PE32+ image that issues one `kernel32!WriteFile`
call per entry of `call_messages` (in the supplied order) against the standard
output handle, then terminates the process via `kernel32!ExitProcess(0)`.

This is the first stage of the pipeline that emits **real AMD64 machine code**
driving Windows stdout.  It reuses the 3-section layout
(`.text` / `.rdata` / `.idata`), the import-descriptor builder and the
`BuildPE32PlusImage` plumbing introduced for `BuildHelloWorldPE`, so all
section-header and IAT invariants exercised by the v1.5.1 PE-writer tests
continue to hold.

### 4.1 Win64 ABI shim layout

The `.text` payload is laid out as **prologue (0x25 B)** + N 脳 **per-message
block (0x1D B)** + **epilogue (0x09 B)**.  All offsets are byte-stable and
unit-tested.

| Region | Size | Effect |
| --- | --- | --- |
| Prologue | 0x25 B | `sub rsp, 0x38` reserves the Win64-mandated 32-byte shadow space + an 8-byte `lpOverlapped` slot (zeroed once) + an 8-byte `lpNumberOfBytesWritten` slot (zeroed once) + an 8-byte stdout-handle cache.  Then `GetStdHandle(STD_OUTPUT_HANDLE)` is invoked once and the returned handle is parked in `[rsp+0x30]`. |
| Per-message block | 0x1D B | `mov rcx, [rsp+0x30]; lea rdx, [rip+msg_i]; mov r8d, len_i; lea r9, [rsp+0x28]; call qword ptr [rip+WriteFile]`.  All RIP-relative displacements are computed from the block's own RVA so the shim is position-correct regardless of how many messages precede it. |
| Epilogue | 0x09 B | `xor ecx, ecx; call qword ptr [rip+ExitProcess]; int3`.  The `int3` is unreachable but pads the shim to a deterministic length. |

The handle is cached in a stack slot rather than a callee-saved register so the
prologue / epilogue do not need to push and pop RBX / R12 / R13 鈥?keeping the
byte-counted invariants above easy to maintain and unit-test.

### 4.2 Dedup and rdata layout

Identical message bytes share their `.rdata` storage (linear scan over the
unique-payload table), exactly mirroring the IR-layer `MakeStringLiteral`
interning contract from B3.  A program that calls `PRINTLN "hello\n";` ten
times pays for ten WriteFile blocks but only one `.rdata` payload.

### 4.3 Edge cases

| Input | Behaviour |
| --- | --- |
| `call_messages.empty()` | Forwards to `BuildExitZeroPE({})`, producing a byte-identical image 鈥?a degenerate empty shim is never emitted. |
| Single message 鈮?4 GiB | Behavioural equivalent of `BuildHelloWorldPE` (different prologue / per-message split, same observable stdout + exit code). |
| Single message > 4 GiB | `BuildResult` is empty (WriteFile's `nNumberOfBytesToWrite` is a DWORD). |
| Duplicate payloads | `.rdata` shrinks to the unique-byte sum; `.text` still holds one block per *call*. |

### 4.4 Tests

* `tests/unit/linker/pe_writer_test.cpp` 鈥?four `[pe_writer][println][b4]`
  cases covering empty-input forwarding, single-message structural shape,
  three-message unrolled `.text` size, and dedup `.rdata` shrinkage.
* `tests/integration/pe_runtime_smoke_test.cpp` 鈥?one
  `[pe_writer][integration][windows][println][b4]` case that spawns a
  3-message PE with one duplicated payload, redirects stdout to a temp file,
  and byte-compares against the expected concatenation.
* `tools/polyld/src/pe_writer_smoke.cpp` 鈥?manual harness producing
  `pe_smoke_println.exe`, used during bring-up to confirm visually that the
  Windows loader accepts the image and the three lines reach the console.

---

## 5. B5 鈥?`polyld` callsite recovery (shipped in v1.5.7)

`polyld` no longer emits a dummy `ExitProcess(0)` shim when the input
objects already carry `polyrt_println` calls.  A new public free function

```cpp
namespace polyglot::linker {
std::vector<std::string>
CollectPolyrtPrintlnSequence(const std::vector<ObjectFile> &objects);
}
```

is invoked from `Linker::GeneratePEExecutable` immediately before the PE
writer dispatch.  When the recovered vector is non-empty the writer
routes through `pe::BuildPrintlnSequencePE(messages)` (the B4 entry); when
empty, the legacy `pe::BuildExitZeroPE(user_text)` path runs unchanged so
non-PRINTLN programs are unaffected.

### 5.1 Analysis pass contract

For every loaded `ObjectFile`, for every section flagged
`SectionFlags::kExecInstr`:

1. Sort the section's `relocations` by `offset` so loaders that surface
   relocs in file order (some POBJ / COFF emitters do) still produce a
   correct call sequence.
2. Walk the sorted relocs left-to-right with a two-state machine:
   - A reloc whose target symbol begins with `println.msg` (with an
     optional trailing `.ptr` GEP-alias suffix that the IR string
     interner introduces) sets the *pending message cursor*.
   - A reloc whose target symbol equals `polyrt_println` consumes the
     pending cursor: the message global is resolved by linear-scanning
     every loaded `ObjectFile`, the bytes
     `data[symbol.offset .. +symbol.size)` are read from its containing
     section, and the decoded payload is appended to the result.
3. Calls without a preceding pending cursor are silently skipped 鈥?they
   describe a runtime-computed message pointer that B5 does not yet
   handle (e.g. `PRINTLN string_var;`).
4. Pending cursors that reach the end of a section without a paired
   `polyrt_println` call are likewise discarded.

### 5.2 Interaction with the IR string interner

Per B3 the IR builder produces *two* globals per literal:
`println.msg<N>` (type `[N x i8]` storing the bytes) and
`println.msg<N>.ptr` (a `ConstantGEP` alias used as `i8*`).  The lowering
hands the `.ptr` alias to `MakeCall("polyrt_println", 鈥?`, so the
analysis pass strips a trailing `.ptr` before the symbol-table lookup.
Either spelling is accepted, so a back-end that collapses the alias into
a direct reference still works.

### 5.3 Cross-translation-unit message globals

The message-resolution scan walks *every* loaded `ObjectFile`, not just
the one hosting the call.  This keeps B5 correct in the future
multi-object pipeline where the IR interner has shared a payload across
translation units (e.g. via LTO or a shared runtime archive).

### 5.4 Faithfulness to call-order semantics

The recovered vector mirrors source-order semantics 鈥?duplicate payloads
from an interned global are emitted at every call site.  Storage
deduplication is the responsibility of `BuildPrintlnSequencePE` (B4),
which intern-folds the recovered messages into `.rdata` while still
issuing one `WriteFile` per call.

### 5.5 Tests

- `tests/unit/linker/polyrt_println_collect_test.cpp` 鈥?10 Catch2 cases
  tagged `[b5]` covering empty input, no-PRINTLN object, single call,
  multi-call ordering, interned-duplicate emission, `.ptr` GEP-alias
  resolution, cross-object data-global lookup, unsorted-reloc auto-sort,
  defensive filtering of mis-flagged `.text` sections, and orphan-call
  skipping.
- `tests/integration/pe_runtime_smoke_test.cpp` 鈥?new `[b5]` end-to-end
  case that builds the ObjectFile shape `polyc` would emit, runs the
  recovered image on the host Windows loader, and asserts both the exit
  code (0) and the captured stdout (`alpha\r\nbeta\r\nalpha\r\n`,
  including the duplicate).

---

## 6. B6 鈥?sample matrix supplements (shipped in v1.5.8)

The 16 pre-existing `tests/samples/<NN>_<name>/` folders each gained:

- A trailing `PRINTLN "<NN>_<name>: ok\r\n";` marker statement appended to
  the `.ploy` entry file.  The marker is the contract every sample is
  required to honour at runtime so that the harness can compare stdout
  byte-for-byte against the sibling `expected_output.txt`.
- A `expected_output.txt` file containing exactly the marker line, in
  ASCII, with no BOM and a CRLF terminator.
- Bilingual `README.md` (English) and `README_zh.md` (Chinese) describing
  the languages exercised, the keyword surface, the build commands and
  the expected runtime output.

The harness `scripts/build_all_samples.ps1` (POSIX twin
`scripts/build_all_samples.sh`) then walks every folder, runs
`polyc --emit-obj=鈥 followed by `polyld 鈥?-o 鈥, executes the binary
under the current shell's stdout-capture, and classifies each sample into
one of seven status buckets:

| Status | Meaning |
| --- | --- |
| `OK` | stdout matches `expected_output.txt` byte-for-byte. |
| `OUTPUT_MISMATCH` | binary ran cleanly but stdout differs from expected. |
| `EMPTY_STDOUT` | binary exited 0 but produced zero bytes on stdout. |
| `RUN_FAIL` | produced binary returned a non-zero exit code. |
| `LINK_FAIL` | `polyld` failed. |
| `COMPILE_FAIL` | `polyc` failed. |
| `SKIP` | folder lacked a `.ploy` entry. |

The summary lands in `build/samples_report.json` with a per-sample object
holding `{name, status, polyc_rc, polyld_rc, exe_rc, stdout_bytes,
expected_bytes, diff_first_off}`.  The harness exits 0 by default so the
report can document toolchain maturity without gating the build; the
`-FailOnMismatch` (PowerShell) / `--fail-on-mismatch` (bash) switch makes
it gate.

## 7. B7 鈥?extended sample matrix (shipped in v1.5.8)

Fourteen new themed samples were authored in
`tests/samples/17_string_processing/` through
`tests/samples/30_game_loop_demo/`.  Each folder follows the same
contract as the B6 supplements: a `.ploy` entry, two host-language source
files (real, compilable code 鈥?no placeholders), bilingual READMEs and an
`expected_output.txt` whose bytes match the closing PRINTLN marker.

| Sample | Languages | Theme |
| --- | --- | --- |
| `17_string_processing` | Python, Rust | String processing pipeline |
| `18_numeric_kernels` | C++, Rust | Numeric kernels (BLAS-style) |
| `19_file_io` | Python, C++ | Streaming file I/O |
| `20_json_pipeline` | Python, Java | JSON ingest pipeline |
| `21_image_processing` | C++, Rust | Image processing kernels |
| `22_database_access` | Python, Java | Database access layer |
| `23_http_client` | Python, Go | HTTP client demo |
| `24_concurrency` | C++, Rust | Concurrency primitives |
| `25_event_loop` | Python, JavaScript | Event loop simulation |
| `26_state_machine` | C++, Java | Finite state machine |
| `27_plugin_system` | C++, Python | Plugin system |
| `28_ml_inference` | Python, Rust | ML inference pipeline |
| `29_data_analytics` | Python, Java | Data analytics |
| `30_game_loop_demo` | C++, Rust | Game loop skeleton |

The matrix now covers seven host languages 鈥?C++, Python, Rust, Java,
C#, Go and JavaScript 鈥?across a deliberate spread of theme verticals so
any future backend regression that breaks one theme cannot silently
regress the rest.

The integration test
`tests/integration/samples_regression_test.cpp` (Catch2 tag
`[samples][b6][integration]`, registered under `integration_tests`) drives
the harness from C++ and asserts the produced JSON report is well-formed:
the recorded `total` matches the on-disk folder count, every status
string belongs to the seven-bucket enum, and no stray status values leak
through.

---

## 8. Future stages (not yet shipped)

| Stage | Contract |
| --- | --- |
| **B8** | Tighten the harness into a release gate once the backend can round-trip every PRINTLN literal end-to-end; bump the version with a coordinated `--end -done` on demand-04 + demand-49. |

The B2 鈥?B7 contracts are stable and will not be revisited.


---

## 9. PE-7 — real-backend `polyrt_println` round-trip

**Status:** shipped (v1.9.1, 2026-04-29).

PE-7 is the staging that closes the gap between B4 (`BuildPrintlnSequencePE`
synthesises a hand-rolled PE that prints a precomputed message vector) and
B7 (the harness expects the *real* backend pipeline to produce that PE for
arbitrary user PRINTLN bytes).  Before PE-7, `polyld` could only recover
the message bytes from a hand-crafted object set: feeding it a real
`polyc --emit-obj=…` produced no recovered messages and the linker
silently fell back to `BuildExitZeroPE`, leaving the program's stdout
empty.  PE-7 makes the recovery path unconditional for any
PRINTLN-bearing program emitted by the production lowering chain.

### 9.1  COFF on-disk reloc record stride

Each on-disk COFF relocation record is exactly **10 bytes**
(`u32 VirtualAddress + u32 SymbolTableIndex + u16 Type`), but the loader
in `tools/polyld/src/linker.cpp` was reading them via the C++ struct's
`sizeof`, which is **12 bytes** because of the trailing `u16`'s natural
alignment.  Every record after the first drifted by 2 bytes, so the
relocation table was almost entirely garbage past index 0.  The loader
now reads each field individually through `std::memcpy` against a fixed
`buf[10]`, matching the on-disk layout byte-for-byte.

### 9.2  Aux-record alignment in the COFF symbol table

COFF places one auxiliary record after each section symbol (the aux
carries the section's length and relocation count).  The loader skipped
each aux on disk but did **not** reserve a slot in `obj.symbols`, so the
in-memory vector was *compressed* while the on-disk relocation records
still cite *uncompressed* on-disk indices.  The relocation backpatch step
(`r.symbol = obj.symbols[r.symbol_index].name`) was therefore reading
shifted entries and assigning wrong symbol names to every reloc in the
file.  The loader now appends a placeholder `Symbol` (empty name,
`is_defined == false`) for every consumed aux record so the on-disk
index space lines up one-to-one with `obj.symbols`.  The placeholders
are guaranteed never to match a downstream lookup because all
recovery / resolution paths key off non-empty `name` and only consider
`is_defined == true` candidates.

### 9.3  Generalising `CollectPolyrtPrintlnSequence`

The recovery pass keyed exclusively off the legacy `println.msg<N>`
hint prefix that the early ploy lowering used.  Today
`IRBuilder::MakeStringLiteral` is generic and emits `str<N>` symbols for
any string literal (PRINTLN included), so the recovery returned an empty
sequence and the linker fell through to `BuildExitZeroPE`.  The pass
now:

- Accepts both `println.msg<N>` (legacy) and `str<N>` (current) prefixes,
  and requires a digit immediately after the prefix to keep the gate
  tight against unrelated user globals such as `stride` or
  `string_table`.
- Resolves a relocation symbol's bytes by stripping an optional `.ptr`
  GEP-alias suffix and locating the bare data symbol in any object's
  `.rdata`.
- Tolerates COFF objects that record a per-symbol size of zero by
  inferring the message length from the next defined sibling symbol's
  offset in the same section, falling back to a NUL-terminator scan and
  finally the section's end as the upper bound.

### 9.4  End-to-end gate

`tests/integration/printf_pipeline_e2e_test.cpp`
(`[printf][pe7][integration]`) writes a top-level two-`PRINTLN` `.ploy`
source, runs `polyc --emit-obj --obj-format=coff` and `polyld`, executes
the produced image, and captures stdout via `cmd /c >` (Win32) or
`fork/pipe/dup2` (POSIX).  The test fails unless the captured bytes
equal `"alpha\r\nbeta\r\n"` byte-for-byte.

The companion sample-harness gate `--require-min-ok N` /
`-RequireMinOk N` (added to `scripts/build_all_samples.{ps1,sh}`) gives
CI a release-gate hook: once the backend can round-trip every PRINTLN
literal in the sample matrix, raising the floor to `33` (the sample
count) makes any regression in the lowering or the recovery pass break
the build immediately.
