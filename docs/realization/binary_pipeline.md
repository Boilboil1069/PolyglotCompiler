# Binary pipeline: `.ploy` -> `.obj` -> `.exe` -> exit code

This note documents the end-to-end native binary pipeline that turns a
`.ploy` source file into a Windows AMD64 process whose exit code is the
value the source program returned from `main`.  It complements
[`compilation_model.md`](compilation_model.md) (which covers the
front-end / IR side) and [`runtime_stdout_pipeline.md`](runtime_stdout_pipeline.md)
(which covers the orthogonal `polyrt_println` stdout path).

The Chinese counterpart is maintained at
[`binary_pipeline_zh.md`](binary_pipeline_zh.md).

---

## Stages

```
   .ploy source
       |
       |  polyc (frontend + IR + backend)
       v
   COFF object  (IMAGE_FILE_HEADER + sections + symbols + relocations)
       |
       |  polyld (load -> resolve -> layout -> emit)
       v
   PE32+ image  (MZ + PE\0\0 + Optional Header + .text + .idata)
       |
       |  Windows loader spawns process; entry shim runs
       v
   process exit code  (== value returned by user main)
```

Each arrow is a contract enforced by a dedicated unit or integration
test; the contracts are listed below stage-by-stage.

---

## Stage 1: `polyc --emit-obj=<path> --obj-format=coff`

The backend phase of `polyc` ends in
`tools/polyc/src/compilation_pipeline.cpp::BuildNativeObjectBinary`,
which dispatches the requested object-file format to one of three
`polyglot::backends::ObjectFileBuilder` subclasses:

| `--obj-format` | builder         | output       |
|----------------|-----------------|--------------|
| `elf`          | `ELFBuilder`    | ELF64        |
| `macho`        | `MachOBuilder`  | Mach-O 64    |
| `coff`         | `COFFBuilder`   | raw COFF     |

`COFFBuilder` lives in `backends/common/{include,src}/object_file.{h,cpp}`
and emits, in this order:

1. `IMAGE_FILE_HEADER` (20 bytes) with `Machine = 0x8664` (AMD64) or
   `0xAA64` (ARM64), `NumberOfSections`, `PointerToSymbolTable` and
   `NumberOfSymbols` filled in once the symbol table position is known.
2. One `IMAGE_SECTION_HEADER` (40 bytes) per `Section` added, with
   characteristics derived from the section name
   (`.text`/`.code`/`__text` -> `IMAGE_SCN_CNT_CODE | MEM_EXECUTE | MEM_READ`,
   `.rdata`/`.rodata`/`__const` -> `IMAGE_SCN_CNT_INITIALIZED_DATA |
   MEM_READ`, `.bss`/`__bss` -> `IMAGE_SCN_CNT_UNINITIALIZED_DATA |
   MEM_READ | MEM_WRITE`, anything else -> initialised R/W data).
3. Raw section bytes, then per-section `IMAGE_RELOCATION` records for
   each `Relocation` carried by the input `Section`.
4. The COFF symbol table.  Each section contributes a `static` section
   symbol with its 18-byte aux record, followed by every user symbol.
   Symbol names of 8 bytes or fewer are packed into the
   `Name.ShortName` slot; longer names are stored in the trailing
   string table and referenced through `Name.LongName.{Zeroes=0,
   Offset}`.
5. The string table: a 4-byte total-size header followed by
   NUL-terminated long names.

The contract pinned by `tests/unit/backends/coff_builder_test.cpp` is
that the produced bytes are accepted byte-for-byte by Microsoft
`link.exe`: the previous "LNK1107: invalid or corrupt file" error
disappears, and any subsequent linker error (e.g. `LNK2001` for an
unresolved CRT symbol when `polyc` invokes `link.exe` to package an
`a.out`) is therefore a legitimate symbol-resolution diagnostic rather
than a format diagnostic.

---

## Stage 2: `polyld <object> -o <exe>` -- format detection

`tools/polyld/src/linker.cpp::DetectObjectFormat` examines the leading
bytes of an input file and dispatches to one of `LoadELF`, `LoadMachO`,
`LoadCOFF` or `LoadPOBJ`.  The COFF rule is two-armed:

* If the file starts with the MZ DOS stub (`'M', 'Z'`) it is treated
  as a fully-linked PE image; the loader follows `e_lfanew` to reach
  the embedded `IMAGE_FILE_HEADER`.
* Otherwise the first two bytes are interpreted as the
  `IMAGE_FILE_HEADER.Machine` field.  Six values trigger the COFF
  loader: `0x8664` (AMD64), `0xAA64` (ARM64), `0x014C` (i386),
  `0x01C0` (ARM), `0x01C4` (ARMNT) and `0x0200` (IA64).  Everything
  else falls through to the next detector (Mach-O or POBJ).

The previous implementation only had the MZ arm, which silently
misclassified raw COFF objects.  The regression is locked by
`tests/unit/linker/object_format_detect_test.cpp` (9 cases, including
an ELF-magic case that pins "an `.obj` containing ELF bytes is **not**
COFF").

---

## Stage 3: `polyld` -- load, resolve, layout

`Linker::LoadCOFF` parses each `IMAGE_SECTION_HEADER`, sets
`SectionFlags::kAlloc` for every section and `kExecInstr` for
`IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE`.  The flags are required
by `Linker::MergeInputSections`, which otherwise drops sections under
`--strip-debug` / `--strip-all`.

`Linker::MergeInputSections` groups input sections by name and
concatenates their bytes into one `OutputSection` per name, recording a
`OutputSection::Contribution {object_index, section_index,
input_offset, output_offset, size}` per merged input.  This map is the
single source of truth for translating an input-side `(obj, sec,
offset)` triple into its final output offset.

`tests/unit/linker/section_merge_test.cpp` (2 cases) pins:

* A single COFF object whose `.text` carries a known sentinel byte
  sequence yields an `output_sections_['.text']` whose first
  `Sentinel().size()` bytes are the sentinel byte-for-byte.
* Two COFF objects whose `.text` sections carry distinct sentinels
  yield an `output_sections_['.text']` containing **both** sequences
  contiguously (in either order); the linker never overwrites or
  silently drops one input.

---

## Stage 4: `polyld` -- PE writer entry-point selection

`Linker::GeneratePEExecutable` collects the merged `.text` bytes and
then searches the resolved symbol table for a user entry symbol in
priority order:

1. `_start`
2. `__ploy_main`
3. `main`

The first match whose `is_defined == true` is translated to its
merged-`.text` offset by walking the contribution map of the output
`.text` section: for each contribution `c` whose `(object_index,
section_index)` matches the symbol's source-side coordinates, the
merged offset is `c.output_offset + symbol.offset`.

When a user entry is found, the PE writer is driven through
`pe::BuildExeWithUserEntry(user_text, user_entry_offset)`; otherwise
the historical `pe::BuildExitZeroPE(user_text)` path is taken so the
image still terminates cleanly.  The dispatch is observable via the
verbose-log `path_tag` (`"user entry"` / `"exit-zero shim"`).

The order also accommodates the `polyrt_println`-driven stdout path
shipped in v1.5.5: when any input object carries `polyrt_println`
relocation traces, `BuildPrintlnSequencePE` wins over both the
user-entry and the exit-zero paths.

---

## Stage 5: PE writer -- the 18-byte user-entry shim

`pe::BuildExeWithUserEntry` lays out `.text` as
`[user_text][user-entry shim]` and points `AddressOfEntryPoint` at the
shim, not at the user main directly.  The shim itself is 18 bytes,
emitted by `pe::BuildUserMainExitShim`:

```
off    bytes  asm
0x00   4      sub  rsp, 0x28                       ; shadow space + 16-byte align
0x04   5      call user_main                       ; E8 disp32
0x09   2      mov  ecx, eax                        ; arg0 = user return value
0x0B   6      call qword ptr [rip + d_ExitProcess] ; FF 15 disp32
0x11   1      int3                                 ; unreachable
```

Both `disp32` fields are encoded against `shim_rva`:

* `disp_user = user_main_rva - (shim_rva + 4 + 5)` (RIP after the
  E8 instruction is at `shim_rva + 9`).
* `disp_exit = exit_iat_rva - (shim_rva + 0x0B + 6)` (RIP after the
  FF 15 instruction is at `shim_rva + 0x11`).

The Win64 ABI `sub rsp, 0x28` reserves 32 bytes of shadow space plus
8 bytes of re-alignment after the implicit return-address push by the
OS-level call into the entry point (so RSP is 16-byte aligned across
both inner CALLs).

The image embeds an `.idata` import directory carrying exactly one
import: `kernel32.dll!ExitProcess`.  The IAT slot RVA is resolved at
build time so the shim's `disp32` can be encoded before the bytes are
emitted, matching the `disp32`-after-layout pattern used by the rest
of the writer.

---

## Stage 6: end-to-end exit code

`tests/integration/ploy_e2e_real_exit_code_test.cpp` is the live
contract:

| `.ploy` body                                | observed `GetExitCodeProcess` |
|---------------------------------------------|--------------------------------|
| `FUNC main() -> i32 { RETURN 42; }`         | `42`                           |
| `FUNC main() -> i32 { RETURN 0;  }`         | `0`                            |
| `FUNC main() -> i32 { RETURN 7;  }`         | `7`                            |

The test compiles each source through the actual `polyc.exe` and
`polyld.exe` binaries (located in the same directory as the test
process via `GetModuleFileNameA`), spawns the resulting `.exe` through
`std::system`, and asserts the returned exit code.

This is the level of guarantee future stages build on: anything that
breaks this chain (frontend, IR lowering, COFF emission, format
detection, section merging, entry-point selection, PE writer,
relocation encoding, IAT resolution) is caught before reaching the
release branch.
