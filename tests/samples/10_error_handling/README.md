# Sample `10_error_handling`

Diagnostic surface for malformed LINK / MAP_TYPE / IMPORT — exercised via deliberately broken declarations.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | diagnostics, error recovery |
| Entry     | `error_handling.ploy` |

## How to run

> **Current version skip**: depends on TRY / CATCH / THROW lowering and the cross-language error bridge.  The harness reads `expected_output.skip` and routes this sample to the SKIP bucket; rename back to `expected_output.txt` once the dependency lands.

## Build

```powershell
polyc 10_error_handling\error_handling.ploy --emit-obj=error_handling.pobj --obj-format=pobj
polyld error_handling.pobj -o error_handling.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
10_error_handling: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
