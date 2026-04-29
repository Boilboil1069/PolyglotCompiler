# Sample `27_plugin_system`

> Plugin system.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Entry | `plugin_system.ploy` |
| Theme | Plugin system |
| Expected stdout | `27_plugin_system: ok\r\n` |

## Files

- `plugin_system.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `plugin_host.cpp` — host source file
- `sample_plugin.py` — host source file

## Build

```powershell
polyc plugin_system.ploy --emit-obj=build/plugin_system.obj --quiet
polyld build/plugin_system.obj -o build/plugin_system.exe
./build/plugin_system.exe
```

## Expected runtime output

```
27_plugin_system: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
