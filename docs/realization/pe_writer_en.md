# PE32+ Writer (Hardened)

This note documents the multi-section, base-relocation and target-aware
features of `tools/polyld/src/pe_writer.cpp`.  It complements
`binary_containers_en.md` (which covers the dispatch layer) by zooming in on
the bytes that the PE32+ writer emits.

## Section roster

`BuildPE32PlusImage` plans sections in this canonical order; only those
whose corresponding request fields are non-empty are emitted:

| Name     | Source field                | Characteristics                                 | Notes                                       |
| -------- | --------------------------- | ----------------------------------------------- | ------------------------------------------- |
| `.text`  | `text_bytes`                | `CODE | EXEC | READ`                            | Always present.                             |
| `.data`  | `data_bytes`                | `INITIALIZED_DATA | READ | WRITE`               | Initialised writable globals.               |
| `.rdata` | `rdata_bytes`               | `INITIALIZED_DATA | READ`                       | Read-only constants / strings.              |
| `.bss`   | `bss_size`                  | `UNINITIALIZED_DATA | READ | WRITE`             | Zero-fill; `SizeOfRawData = 0`.             |
| `.pdata` | `pdata_bytes`               | `INITIALIZED_DATA | READ`                       | x64 `RUNTIME_FUNCTION[]`. Wired into Data Directory[3] (Exception). |
| `.xdata` | `xdata_bytes`               | `INITIALIZED_DATA | READ`                       | `UNWIND_INFO` blocks for `.pdata`.          |
| `.idata` | `imports`                   | `INITIALIZED_DATA | READ | WRITE`               | Import descriptors + IAT + hint/name table. |
| `.reloc` | `base_relocations`          | `INITIALIZED_DATA | DISCARDABLE | READ`         | Page-grouped IMAGE_BASE_RELOCATION blocks.  |

All sections are virtual-aligned to `0x1000` and file-aligned to `0x200`.
Section RVAs are strictly monotonic, matching the order above.

## Base relocation encoding

`BuildBaseRelocSection(relocs)` groups entries by their 4 KiB virtual page
and emits one block per page:

```
+0  PageRVA       (4 bytes, page_rva = rva & ~0xFFF)
+4  SizeOfBlock   (4 bytes, header + entries, padded to multiple of 4)
+8  WORD entries  (high 4 bits = type, low 12 bits = offset_in_page)
```

When the entry count for a page is odd, an `IMAGE_REL_BASED_ABSOLUTE`
(type=0) padding entry is appended so `SizeOfBlock` is 4-byte aligned per
spec.  `DecodeBaseRelocSection` is the inverse function used by the unit
tests to round-trip the encoder.

Type values currently exercised:

| Value | Name                                   | Architecture |
| ----- | -------------------------------------- | ------------ |
| 0     | `IMAGE_REL_BASED_ABSOLUTE`             | (padding)    |
| 3     | `IMAGE_REL_BASED_HIGHLOW`              | x86          |
| 3     | `IMAGE_REL_BASED_ARM64_BRANCH26`       | arm64        |
| 4     | `IMAGE_REL_BASED_ARM64_PAGEBASE_REL21` | arm64        |
| 7     | `IMAGE_REL_BASED_ARM64_PAGEOFFSET_12A` | arm64        |
| 9     | `IMAGE_REL_BASED_ARM64_PAGEOFFSET_12L` | arm64        |
| 10    | `IMAGE_REL_BASED_DIR64`                | x64          |

Presence of a non-empty `.reloc` section toggles two header bits:

* `IMAGE_FILE_RELOCS_STRIPPED` (COFF Characteristics bit 0) is **cleared**.
* `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE` is **set** (and
  `HIGH_ENTROPY_VA` for x64 / arm64) when `dll_characteristics == 0`.

## Machine / Subsystem / DllCharacteristics

| Request field          | COFF/Optional Header field | Default value                                                    |
| ---------------------- | -------------------------- | ---------------------------------------------------------------- |
| `machine`              | `Machine`                  | `IMAGE_FILE_MACHINE_AMD64` (0x8664)                              |
| `subsystem`            | `Subsystem`                | `IMAGE_SUBSYSTEM_WINDOWS_CUI` (3, console)                       |
| `dll_characteristics`  | `DllCharacteristics`       | `NX_COMPAT | TERMINAL_SERVER_AWARE` (+ `DYNAMIC_BASE | HIGH_ENTROPY_VA` when `.reloc` present) |
| `extra_file_characteristics` | `Characteristics` (OR-ed) | `0`. Use `0x2000` for `IMAGE_FILE_DLL`.                       |

`PEMachine::kI386` produces a PE32+ image (Optional Magic remains `0x20B`)
with `Machine = 0x014C`; intended for metadata-only tagging in pipelines
that need to reuse the writer for cross-format inspection.

## Data directory wiring

| Index | Directory          | Source                                  |
| ----- | ------------------ | --------------------------------------- |
| 1     | Import             | `BuildImportSection(imports)`           |
| 3     | Exception          | `.pdata` section RVA / size (or zero)   |
| 5     | Base Relocation    | `.reloc` section RVA / size (or zero)   |
| 12    | IAT                | Sub-region of `.idata`                  |

All other directory entries are zero.

## Debug cheat sheet

* **Sanity check the headers** – `dumpbin /headers polyc.exe`. Confirm
  `machine (x64)` (or `(ARM64)`), `subsystem (Windows CUI/GUI)`, and
  the `DLL characteristics` line lists `Dynamic base` plus `High Entropy
  Virtual Addresses` when relocations are present.
* **Inspect the section table** – `dumpbin /headers` lists the
  `Characteristics` flags per section. `.bss` should report
  `BSS / Read Write` with `0` raw size.
* **Validate base relocations** – `dumpbin /relocations polyc.exe`. Each
  page block should report `RVA` plus the entry count produced by the
  encoder.
* **Disassemble exception data** – `dumpbin /exports /imports
  /unwindinfo polyc.exe` cross-references `.pdata` and `.xdata`.
* **Round-trip in tests** – use `DecodeBaseRelocSection` to compare the
  encoder output back to the original `BaseRelocation` set.
