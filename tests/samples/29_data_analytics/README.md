# Sample `29_data_analytics`

> Data analytics.

| Field | Value |
| --- | --- |
| Languages | Python, Java |
| Entry | `data_analytics.ploy` |
| Theme | Data analytics |
| Expected stdout | `29_data_analytics: ok\r\n` |

## Files

- `data_analytics.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `record_loader.py` — host source file
- `Aggregator.java` — host source file

## Build

```powershell
polyc data_analytics.ploy --emit-obj=build/data_analytics.obj --quiet
polyld build/data_analytics.obj -o build/data_analytics.exe
./build/data_analytics.exe
```

## Expected runtime output

```
29_data_analytics: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
