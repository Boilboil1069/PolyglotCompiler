# Sample `21_image_processing`

> Image processing kernels.

| Field | Value |
| --- | --- |
| Languages | C++, Rust |
| Entry | `image_processing.ploy` |
| Theme | Image processing kernels |
| Expected stdout | `21_image_processing: ok\r\n` |

## Files

- `image_processing.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `greyscale_kernel.cpp` — host source file
- `box_blur.rs` — host source file

## Build

```powershell
polyc image_processing.ploy --emit-obj=build/image_processing.obj --quiet
polyld build/image_processing.obj -o build/image_processing.exe
./build/image_processing.exe
```

## Expected runtime output

```
21_image_processing: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
