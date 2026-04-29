# Sample `19_file_io`

> Streaming file I/O.

| Field | Value |
| --- | --- |
| Languages | Python, C++ |
| Entry | `file_io.ploy` |
| Theme | Streaming file I/O |
| Expected stdout | `19_file_io: ok\r\n` |

## Files

- `file_io.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `binary_reader.cpp` — host source file
- `text_decoder.py` — host source file

## Build

```powershell
polyc file_io.ploy --emit-obj=build/file_io.obj --quiet
polyld build/file_io.obj -o build/file_io.exe
./build/file_io.exe
```

## Expected runtime output

```
19_file_io: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
