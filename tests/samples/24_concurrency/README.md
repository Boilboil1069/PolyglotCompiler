# Sample `24_concurrency`

> Concurrency primitives.

| Field | Value |
| --- | --- |
| Languages | C++, Rust |
| Entry | `concurrency.ploy` |
| Theme | Concurrency primitives |
| Expected stdout | `24_concurrency: ok\r\n` |

## Files

- `concurrency.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `atomic_counter.cpp` — host source file
- `worker_pool.rs` — host source file

## Build

```powershell
polyc concurrency.ploy --emit-obj=build/concurrency.obj --quiet
polyld build/concurrency.obj -o build/concurrency.exe
./build/concurrency.exe
```

## Expected runtime output

```
24_concurrency: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
