# Sample `14_async_pipeline`

Multi-stage signal processing PIPELINE with C++ DSP, Rust async loader and Python visualisation.

| Field | Value |
| --- | --- |
| Languages | C++, Rust, Python |
| Keywords  | PIPELINE, IF / ELSE |
| Entry     | `async_pipeline.ploy` |

## Build

```powershell
polyc 14_async_pipeline\async_pipeline.ploy --emit-obj=async_pipeline.pobj --obj-format=pobj
polyld async_pipeline.pobj -o async_pipeline.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
14_async_pipeline: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
