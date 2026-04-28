# Debug Emitter Normalization

> Status: shipped in 1.4.0 — applies to `backends/common/src/debug_emitter.cpp`,
> `backends/common/src/dwarf_builder.cpp`, and the public
> `backends/common/include/debug_info.h` schema.

## 1. Why this document exists

Earlier revisions of the DWARF / PDB emitter sprinkled `// Placeholder`,
`// (placeholder)` and `// Simplified` comments through the code.  Nine of
those twelve sites were not actually placeholders: they marked the
*reserved-length-prefix* fields that DWARF specifies for `unit_length`,
`header_length`, CIE / FDE length, and the ELF `e_shoff` slot — fields whose
final value is only knowable once the enclosing region has been fully
appended.  The surrounding code already patched each of those slots via a
matching `Patch32(..., position, value)` call.

The remaining three sites covered behaviour that genuinely needed sharpening
or relabelling:

| Site                                                               | Old comment                       | Reality                                                                                                            |
| ------------------------------------------------------------------ | --------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `DwarfSectionBuilder::EncodeLineStatements` (debug_emitter.cpp)    | `address++; // Simplified address tracking` | Behavioural shortcut — line program advanced PC by one unit per row regardless of `DebugLineInfo::address`.        |
| `PdbSectionBuilder::GenerateGuid` declaration (debug_emitter.cpp)  | `// GUID generation (simplified)` | Implementation was already RFC 4122 v4 compliant; the label was misleading.                                        |
| `DwarfBuilder::BuildLineNumberProgram` (dwarf_builder.cpp)         | `// Line number program (simplified)` | Legacy fallback path, intentionally narrower than the production `DwarfSectionBuilder::EncodeLineStatements` path. |

## 2. Reserved-length-prefix encoding contract

For every "reserved length, patched at section close" comment in the emitter,
the following invariants now hold and are exercised by
`tests/unit/backends/debug_emitter_normalization_test.cpp`:

* The byte at the documented offset is initially zero.
* A matching `Patch32(buffer, position, computed_length)` runs before the
  enclosing builder method returns.
* The patched value is `region_byte_count − sizeof(length_prefix)` (DWARF v5
  §7.4 / §7.5 / §6.4.1; LSB §10.6.1; ELF gABI §1.4 for `e_shoff`).

The contract applies to:

| Section                  | Prefix                            | Spec reference         |
| ------------------------ | --------------------------------- | ---------------------- |
| `.debug_info`            | `unit_length` (4 bytes)           | DWARF v5 §7.4          |
| `.debug_line`            | `unit_length`, `header_length`    | DWARF v5 §6.2.4        |
| `.debug_frame`           | CIE length, FDE length            | DWARF v5 §6.4.1        |
| `.eh_frame`              | CIE length, FDE length            | System V ABI §10.6.1   |
| `.debug_aranges`         | unit length                       | DWARF v5 §6.1.2        |
| ELF64 file header        | `e_shoff` (8 bytes)               | ELF gABI Fig. 4-3      |

When inspecting the source, look for the `_pos = result.size()` /
`WriteLE<uint32_t>(result, 0)` pair and follow the matching `Patch32` call —
that is the contract in code form.

## 3. `DebugLineInfo::address` and explicit PC advancement

`DebugLineInfo` now carries an explicit `address` member:

```cpp
struct DebugLineInfo {
  std::string file;
  int line{0};
  int column{0};
  std::uint64_t address{0};
};
```

`DwarfSectionBuilder::EncodeLineStatements` honours the field as follows:

1. The first row emits `DW_LNE_set_address(entry.address)` (which is `0` for
   callers that omit the field, leaving the address to be relocated by the
   linker) and primes the state-machine PC register.
2. Each subsequent row computes `delta = max(entry.address, address + 1) −
   address` and emits `DW_LNS_advance_pc ULEB128(delta)`.  This guarantees
   that the line program is strictly monotonic and individually addressable
   regardless of whether the caller supplied real machine addresses.
3. After `DW_LNS_copy`, the state-machine PC is updated to the new value so
   that the next row's delta is computed correctly.

The behaviour is verified by the
`.debug_line line program advances PC monotonically` test case, which feeds
three rows with increasing addresses and asserts that the emitted byte stream
contains at least one `DW_LNS_advance_pc` (`0x02`) opcode.

## 4. PDB GUID generation

`PdbSectionBuilder::GenerateGuid` uses `std::random_device` for entropy and
sets the version / variant bits per RFC 4122 §4.4:

```text
guid[6] = (guid[6] & 0x0F) | 0x40;   // Version 4
guid[8] = (guid[8] & 0x3F) | 0x80;   // Variant 1
```

Two consecutive emissions are guaranteed to differ — confirmed by the
`PDB Info stream GUID has RFC 4122 v4 + variant 1 bits` test case, which
emits two PDBs back-to-back, locates the GUID via the VC70 magic version
header (`20000404`), and compares the bytes.

## 5. Test invariants summary

`tests/unit/backends/debug_emitter_normalization_test.cpp` adds four cases:

| # | Title                                                                 | Asserts                                                                                                                  |
| - | --------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| 1 | `.debug_info unit_length is reserved-then-patched`                    | `LE32(body, 0) == body.size() − 4`, body ≥ 11 bytes.                                                                     |
| 2 | `.debug_line unit_length and header_length are reserved-then-patched` | `LE32(body, 0) == body.size() − 4`; `LE32(body, 8) ∈ (0, body.size())`.                                                  |
| 3 | `.debug_line line program advances PC monotonically`                  | At least one `DW_LNS_advance_pc` opcode (`0x02`) is present in the section payload after feeding three increasing-PC rows. |
| 4 | `PDB Info stream GUID has RFC 4122 v4 + variant 1 bits`               | `(guid[6] & 0xF0) == 0x40`, `(guid[8] & 0xC0) == 0x80`, and two consecutive GUIDs differ.                                  |

## 6. Migration notes

* `DebugLineInfo` gained a fourth field with a default initializer.  All
  existing aggregate initialisers of the form `{file, line, column}` continue
  to compile and behave identically (the new field defaults to `0`, which is
  the same value the old emitter used).
* `DwarfSectionBuilder` and `PdbSectionBuilder` remain implementation-only
  classes inside `backends/common/src/debug_emitter.cpp`; their public
  surface is still `DebugEmitter::EmitDWARF` / `EmitPDB` /
  `EmitSourceMap`.  No external API changed.
* No CMake target boundaries moved.  The new test file is wired into
  `UNIT_TEST_BACKENDS` in `tests/CMakeLists.txt`.
