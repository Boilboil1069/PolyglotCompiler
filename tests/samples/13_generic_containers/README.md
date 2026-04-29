# Sample `13_generic_containers`

Generic container interop: java.util.ArrayList / HashMap, std::vector and Python list shared through MAP_TYPE.

| Field | Value |
| --- | --- |
| Languages | C++, Java, Python |
| Keywords  | MAP_TYPE containers |
| Entry     | `generic_containers.ploy` |

## Build

```powershell
polyc 13_generic_containers\generic_containers.ploy --emit-obj=generic_containers.pobj --obj-format=pobj
polyld generic_containers.pobj -o generic_containers.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
13_generic_containers: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
