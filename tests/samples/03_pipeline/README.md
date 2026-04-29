# Sample `03_pipeline`

Multi-stage PIPELINE composing C++ image preprocessing with a Python ML model under IF / WHILE / FOR / MATCH control flow.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | PIPELINE, IF, WHILE, FOR, MATCH |
| Entry     | `pipeline.ploy` |

## Build

```powershell
polyc 03_pipeline\pipeline.ploy --emit-obj=pipeline.pobj --obj-format=pobj
polyld pipeline.pobj -o pipeline.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
03_pipeline: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
