# Sample `09_mixed_pipeline`

End-to-end PIPELINE combining every keyword across C++, Python and Rust for a small ML workflow.

| Field | Value |
| --- | --- |
| Languages | C++, Python, Rust |
| Keywords  | LINK, PIPELINE, NEW, METHOD, WITH, DELETE, EXTEND |
| Entry     | `mixed_pipeline.ploy` |

## Build

```powershell
polyc 09_mixed_pipeline\mixed_pipeline.ploy --emit-obj=mixed_pipeline.pobj --obj-format=pobj
polyld mixed_pipeline.pobj -o mixed_pipeline.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
09_mixed_pipeline: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
