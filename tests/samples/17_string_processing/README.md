# Sample `17_string_processing`

> String processing pipeline.

| Field | Value |
| --- | --- |
| Languages | Python, Rust |
| Entry | `string_processing.ploy` |
| Theme | String processing pipeline |
| Expected stdout | `17_string_processing: ok\r\n` |

## Files

- `string_processing.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `str_tokenizer.rs` — host source file
- `case_folder.py` — host source file

## Build

```powershell
polyc string_processing.ploy --emit-obj=build/string_processing.obj --quiet
polyld build/string_processing.obj -o build/string_processing.exe
./build/string_processing.exe
```

## Expected runtime output

```
17_string_processing: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
