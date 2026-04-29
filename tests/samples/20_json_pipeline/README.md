# Sample `20_json_pipeline`

> JSON ingest pipeline.

| Field | Value |
| --- | --- |
| Languages | Python, Java |
| Entry | `json_pipeline.ploy` |
| Theme | JSON ingest pipeline |
| Expected stdout | `20_json_pipeline: ok\r\n` |

## Files

- `json_pipeline.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `json_parser.py` — host source file
- `SchemaValidator.java` — host source file

## Build

```powershell
polyc json_pipeline.ploy --emit-obj=build/json_pipeline.obj --quiet
polyld build/json_pipeline.obj -o build/json_pipeline.exe
./build/json_pipeline.exe
```

## Expected runtime output

```
20_json_pipeline: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
