# Sample `18_numeric_kernels`

> Numeric kernels (BLAS-style).

| Field | Value |
| --- | --- |
| Languages | C++, Rust |
| Entry | `numeric_kernels.ploy` |
| Theme | Numeric kernels (BLAS-style) |
| Expected stdout | `18_numeric_kernels: ok\r\n` |

## Files

- `numeric_kernels.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `blas_kernels.cpp` — host source file
- `reduce_kernels.rs` — host source file

## Build

```powershell
polyc numeric_kernels.ploy --emit-obj=build/numeric_kernels.obj --quiet
polyld build/numeric_kernels.obj -o build/numeric_kernels.exe
./build/numeric_kernels.exe
```

## Expected runtime output

```
18_numeric_kernels: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
