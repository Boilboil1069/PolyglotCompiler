# Sample `25_event_loop`

> Event loop simulation.

| Field | Value |
| --- | --- |
| Languages | Python, JavaScript |
| Entry | `event_loop.ploy` |
| Theme | Event loop simulation |
| Expected stdout | `25_event_loop: ok\r\n` |

## Files

- `event_loop.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `scheduler.js` — host source file
- `dispatcher.py` — host source file

## Build

```powershell
polyc event_loop.ploy --emit-obj=build/event_loop.obj --quiet
polyld build/event_loop.obj -o build/event_loop.exe
./build/event_loop.exe
```

## Expected runtime output

```
25_event_loop: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
