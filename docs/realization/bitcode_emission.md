# Polyglot Bitcode Emission

The backend interface defines an `EmitBitcode` entry point alongside
`EmitObject` and `EmitAssembly`.  Until this milestone, the default
implementation reported an unsupported diagnostic for every backend; the
matching `BackendCapabilities::emits_bitcode` flag was therefore false on
all three production targets.  This milestone wires the entry point up
to the project's existing polyglot bitcode serialiser (the byte stream
already in production use by the LTO / ThinLTO pipeline) and flips the
capability flag on the x86_64, arm64 and wasm32 backends.

The change is additive: backends that need a different format (e.g. real
LLVM bitcode for an LLVM-to-LLVM linker) override the virtual; backends
that have no special needs inherit the default and immediately gain a
real, round-trippable artifact.

## 1. Format

Polyglot bitcode is a UTF-8 text stream produced and consumed by
`polyglot::lto::LTOModule`.  It is not LLVM bitcode and uses a different
leading byte (`m`) than the LLVM `BC` magic, so detectors that already
key off the magic (`tools/polyld/src/linker.cpp` recognises the LLVM
`BC\xC0\xDE` magic for `kLLVMBitcode`) are not perturbed.

Grammar (whitespace-separated tokens, `\n`-delimited records):

```
module <module_name>
<fn_count> <gv_count>
( per-function block, repeated fn_count times:
    <fn_name>
    <block_count>
    ( per-block, repeated block_count times:
        <block_name> <inst_count>
        ( per-instruction, repeated inst_count times:
            <inst_name|_> <type_kind:int> <op_count> <ops...>
        )
    )
)
( per-global, repeated gv_count times:
    <gv_name>
)
<entry_point_count>
( per-entry-point, repeated entry_point_count times:
    <entry_point_name>
)
```

Empty names are encoded as the single character `_` and decoded back to
the empty string; this avoids ambiguity with the whitespace-separated
tokenisation.

## 2. APIs

### 2.1 `polyglot::lto::LTOModule`

```cpp
// In-memory variants — produce / consume bytes without touching disk.
std::string SerializeBitcode() const;
bool        DeserializeBitcode(std::string_view bytes);

// Disk variants — thin wrappers over the in-memory pair.
bool SaveBitcode(const std::string& filename) const;
bool LoadBitcode(const std::string& filename);

// Build an LTOModule from an in-memory IRContext.
static LTOModule FromIRContext(const polyglot::ir::IRContext& ctx,
                               std::string module_name);
```

`SaveBitcode` is now a one-liner over `SerializeBitcode` plus a binary
write; `LoadBitcode` reads the file into a `std::string` and calls
`DeserializeBitcode`.  Byte-for-byte the disk variants produce the
exact same artifact as before — guarded by the existing
`tests/unit/middle/lto_test.cpp` round-trip cases.

`FromIRContext` deep-copies every `Function` and `GlobalValue` from the
`IRContext` (so the resulting `LTOModule` does not alias the source) and
records every function name as an entry-point candidate.  Callers that
need a narrower export surface can post-process `entry_points` before
serialising.

### 2.2 `polyglot::backends::ITargetBackend::EmitBitcode`

```cpp
virtual CompileResult EmitBitcode(const polyglot::ir::IRContext& module,
                                  const TargetOptions& options);
```

Default behaviour:

1. Build an `LTOModule` from `module` using `TargetTriple()` as the
   bitcode module name.
2. Serialise via `LTOModule::SerializeBitcode`.
3. Move the resulting bytes into `result.artifacts.bitcode_bytes`.
4. Set `result.ok = true` and return with no diagnostics.

If serialisation produces an empty payload (which can only happen if
`SerializeBitcode` itself fails), the call returns `ok = false` with a
single error diagnostic in the `EmitBitcode` component.

Backends that need a different bitcode format override the virtual and
ignore the inherited path entirely.

## 3. Capability matrix

| Backend triple             | `emits_bitcode` (before) | `emits_bitcode` (now) |
|----------------------------|:------------------------:|:---------------------:|
| `x86_64-unknown-elf`       | false                    | true                  |
| `aarch64-unknown-elf`      | false                    | true                  |
| `wasm32-unknown-unknown`   | false                    | true                  |

None of the three adapters override the virtual; they all share the
default polyglot bitcode path.  The `target_backend_registry_test`
matrix assertions and the new `emit_bitcode_roundtrip_test` byte-equal
case both depend on this.

## 4. Interaction with `polyld`

`tools/polyld/src/linker.cpp` recognises four object formats by leading
bytes: ELF (`\x7FELF`), Mach-O (`\xFE\xED\xFA…`), COFF, and LLVM
bitcode (`BC\xC0\xDE`).  Polyglot bitcode begins with the literal ASCII
character `m` (`module ` header), which falls outside every recognised
magic and is therefore never misclassified as LLVM bitcode.

Consumers that want to round-trip a polyglot bitcode artifact through
`polyld` should treat it as the project's native LTO format and feed it
to the LTO pipeline (`CompileToBitcode` / `MergeBitcode`) rather than to
the object-file loader.  Mixing the two formats in a single linker
invocation is supported because the leading-byte spaces are disjoint.

## 5. Test coverage

| Test | Asserts |
|------|---------|
| `unit/middle/lto_test.cpp` (existing) | `SaveBitcode`/`LoadBitcode` byte equivalence on the on-disk format. |
| `unit/backends/target_backend_registry_test.cpp` (modified) | All three backends now advertise `emits_bitcode = true`; the previously named "unsupported diagnostic" case is replaced by an "emits polyglot bitcode bytes" case asserting `result.ok`, the leading `m` byte, and a 2-function round-trip via `DeserializeBitcode`. |
| `unit/backends/emit_bitcode_roundtrip_test.cpp` (new, 3 cases) | Empty-IRContext header validity; byte-identical payloads across all three backend triples (header line stripped to account for the per-triple module name); function/block/instruction topology preservation including operand strings and `entry_points` membership. |

## 6. Migration notes

- Existing callers that branched on `result.ok == false` after invoking
  `EmitBitcode` on x86_64/arm64/wasm will now see `ok == true` and a
  populated `bitcode_bytes` field.  Any code that relied on the
  "unsupported" diagnostic to skip a stage must instead consult
  `Capabilities().emits_bitcode` explicitly (this was always the
  documented contract).
- The on-disk format produced by `LTOModule::SaveBitcode` is unchanged
  byte-for-byte; the helper now routes through `SerializeBitcode` but
  emits the identical stream.
- Backends planning to emit real LLVM bitcode (e.g. via libLLVM) should
  override `EmitBitcode` and set `Capabilities().emits_bitcode = true`
  themselves — the default path never runs in that case.
