# Sample `28_ml_inference`

> ML inference pipeline.

| Field | Value |
| --- | --- |
| Languages | Python, Rust |
| Entry | `ml_inference.ploy` |
| Theme | ML inference pipeline |
| Expected stdout | `28_ml_inference: ok\r\n` |

## Files

- `ml_inference.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `tokenizer.py` — host source file
- `scorer.rs` — host source file

## Build

```powershell
polyc ml_inference.ploy --emit-obj=build/ml_inference.obj --quiet
polyld build/ml_inference.obj -o build/ml_inference.exe
./build/ml_inference.exe
```

## Expected runtime output

```
28_ml_inference: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
