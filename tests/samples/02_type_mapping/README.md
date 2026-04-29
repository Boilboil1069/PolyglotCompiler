# Sample `02_type_mapping`

Per-parameter MAP_TYPE plus STRUCT and CONVERT showing how complex types cross the bridge.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | MAP_TYPE, STRUCT, CONVERT |
| Entry     | `type_mapping.ploy` |

## Build

```powershell
polyc 02_type_mapping\type_mapping.ploy --emit-obj=type_mapping.pobj --obj-format=pobj
polyld type_mapping.pobj -o type_mapping.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
02_type_mapping: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
