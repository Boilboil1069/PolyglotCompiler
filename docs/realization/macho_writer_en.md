# Mach-O Writer (Real Layout)

This note documents the Mach-O image writer that lives in
`tools/polyld/src/linker_macho.cpp` and `tools/polyld/include/linker_macho.h`.
It complements [binary_containers_en.md](binary_containers_en.md)
(which covers the polyld dispatch layer) by zooming in on the bytes that
the writer emits for `MH_EXECUTE`, `MH_DYLIB`, and `MH_BUNDLE` images on
both `x86_64` and `arm64`.

The writer is independent of the `Linker` class data flow: it consumes a
`BuildRequest` value type and returns a `BuildResult` carrying the
finished image plus a few useful offsets. The three `Linker::GenerateMachO*`
member methods at the bottom of the implementation are thin wrappers that
project `Linker` state into `BuildRequest`, call `BuildMachOImage`, and
write the result to disk.

## Header

The first 32 bytes are a `mach_header_64`:

| Offset | Field         | Value                                                                |
| -----: | ------------- | -------------------------------------------------------------------- |
|      0 | `magic`       | `0xFEEDFACF` (`MH_MAGIC_64`)                                         |
|      4 | `cputype`     | `0x01000007` for x86_64, `0x0100000C` for arm64                      |
|      8 | `cpusubtype`  | `3` (`CPU_SUBTYPE_ALL`)                                              |
|     12 | `filetype`    | `2` (executable), `6` (dylib), or `8` (bundle)                       |
|     16 | `ncmds`       | Number of load commands following the header                         |
|     20 | `sizeofcmds`  | Cumulative `cmdsize` of every load command                           |
|     24 | `flags`       | `MH_NOUNDEFS|MH_DYLDLINK|MH_TWOLEVEL|MH_PIE` plus optional flags     |
|     28 | `reserved`    | `0` for 64-bit                                                       |

## Load command roster

The writer always plans the load commands in this canonical order so
that `otool -l` and `lldb` see the same shape Apple's toolchain emits:

| Order | Command              | Purpose                                                              |
| ----: | -------------------- | -------------------------------------------------------------------- |
|     1 | `LC_SEGMENT_64` (`__PAGEZERO`) | Executables only; 4 GiB unmapped guard segment            |
|     2 | `LC_SEGMENT_64` × N  | One per user-supplied `SegmentDesc` (`__TEXT`, `__DATA_CONST`, ...) |
|     3 | `LC_SEGMENT_64` (`__LINKEDIT`) | Holds symtab / strtab / indirect / code-signature regions  |
|     4 | `LC_SYMTAB`          | Offset / nsyms / stroff / strsize                                    |
|     5 | `LC_DYSYMTAB`        | Local / extdef / undef counts; rest zeroed                           |
|     6 | `LC_LOAD_DYLINKER`   | `/usr/lib/dyld`                                                      |
|     7 | `LC_LOAD_DYLIB`      | `/usr/lib/libSystem.B.dylib` with `current=1.0.0`, `compat=1.0.0`    |
|     8 | `LC_UUID`            | 16-byte SHA-256-truncated digest of segment content                  |
|     9 | `LC_BUILD_VERSION`   | `platform=macOS`, `minos`, `sdk` (3-byte triplets)                   |
|    10 | `LC_SOURCE_VERSION`  | Packed `A.B.C.D.E` from `BuildRequest::source_version`               |
|    11 | `LC_MAIN` *or* `LC_ID_DYLIB` | Executable: entry offset; dylib: install name                |
|    12 | `LC_CODE_SIGNATURE`  | Optional ad-hoc placeholder (4 KiB SuperBlob window)                 |

The bundle filetype omits `__PAGEZERO`, omits `LC_MAIN`, and does not
emit `LC_ID_DYLIB`. It otherwise shares the layout with executables so
the writer can stay one code path.

## `__LINKEDIT` layout

The `__LINKEDIT` segment is laid out in this order:

```
+0                 nlist_64[] symbol table     (16 bytes per entry)
+nsyms*16          char[]     string table     (NUL-terminated, ASCII)
+strtab+strsize    pad to 16 bytes
+...               LC_CODE_SIGNATURE region    (only if requested)
```

The `LC_CODE_SIGNATURE` window is 4 KiB and starts with an empty
`CS_SuperBlob` header (`magic=0xFADE0CC0`, `length=8`, `count=0`) so
that `codesign --display` recognises the file. A real signature can be
injected post-link with `codesign --force --sign -`.

## Relocation translation

Relocations from upstream IR are mapped through
`TranslateRelocations(arch, in, out, errors)`. The function returns the
on-disk `relocation_info` byte shape (8 bytes per record) and reports
unknown `r_type` values with `polyld-err-E3220`.

| `MachOArch` | Type codes recognised |
| ----------- | ---------------------- |
| `kX86_64`   | `UNSIGNED`, `SIGNED`, `BRANCH`, `GOT_LOAD`, `GOT`, `SUBTRACTOR`, `SIGNED_1/2/4`, `TLV` |
| `kArm64`    | `UNSIGNED`, `SUBTRACTOR`, `BRANCH26`, `PAGE21`, `PAGEOFF12`, `GOT_LOAD_PAGE21`, `GOT_LOAD_PAGEOFF12`, `POINTER_TO_GOT`, `TLVP_PAGE21`, `TLVP_PAGEOFF12`, `ADDEND` |

Records that translate cleanly are kept even when other records in the
same batch fail, so callers see the whole bad-record set in one pass.

## UUID derivation

`Uuid16FromContent(bytes)` SHA-256-hashes its argument and truncates the
digest to 16 bytes. The version nibble is forced to `4` and the variant
bits to `10`, matching RFC 4122 expectations and the constraints
Apple's `uuidgen` enforces. Two builds with byte-identical user-segment
content therefore share a UUID, which is what reproducible-build
verifiers expect.

## Architecture differences

| Aspect                   | x86_64                                | arm64                                  |
| ------------------------ | ------------------------------------- | -------------------------------------- |
| `cputype`                | `0x01000007`                          | `0x0100000C`                           |
| Branch reloc             | `X86_64_RELOC_BRANCH`                 | `ARM64_RELOC_BRANCH26`                 |
| GOT load pair            | `GOT_LOAD` (single)                   | `GOT_LOAD_PAGE21` + `GOT_LOAD_PAGEOFF12` |
| Page reloc family        | n/a                                   | `PAGE21` / `PAGEOFF12`                 |
| Default `__PAGEZERO` size | 4 GiB                                | 4 GiB                                  |
| Tiny `exit(0)` body      | `B8 01 00 00 02 31 FF 0F 05`          | `00 00 80 52 30 00 80 52 01 10 00 D4`  |

## Debugging cheat sheet

* `otool -hlLR <image>` shows header, load commands, libraries, and
  relocations.
* `otool -tv <image>` disassembles `__TEXT,__text` for visual diff
  against the input bytes.
* `dyldinfo -bind <image>` (deprecated but still useful) verifies that
  no stale binds leak into the file.
* `lldb -b -o "image dump symtab" <image>` prints the parsed symbol
  table, which is the easiest way to spot off-by-one `n_sect` bugs.
* On macOS the e2e smoke test loads the dylib variant via `dlopen` and
  asserts that the loader complains with a Mach-O-shaped diagnostic
  rather than rejecting the file as a non-Mach-O blob.

## Error codes

| Code               | Trigger                                                         |
| ------------------ | --------------------------------------------------------------- |
| `polyld-err-E3220` | Relocation `r_type` unknown for the requested `MachOArch`       |
| `polyld-err-E3230` | `BuildMachOImage` produced an empty image (invalid `BuildRequest`) |
