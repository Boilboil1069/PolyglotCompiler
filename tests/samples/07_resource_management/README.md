# Sample `07_resource_management`

WITH binding the Python __enter__ / __exit__ protocol so resources are released even on early return.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | WITH |
| Entry     | `resource_management.ploy` |

## Build

```powershell
polyc 07_resource_management\resource_management.ploy --emit-obj=resource_management.pobj --obj-format=pobj
polyld resource_management.pobj -o resource_management.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
07_resource_management: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
