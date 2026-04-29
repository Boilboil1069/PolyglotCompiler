# Sample `08_delete_extend`

DELETE deterministically destroys a foreign object and EXTEND derives a polyglot subclass.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | DELETE, EXTEND |
| Entry     | `delete_extend.ploy` |

## Build

```powershell
polyc 08_delete_extend\delete_extend.ploy --emit-obj=delete_extend.pobj --obj-format=pobj
polyld delete_extend.pobj -o delete_extend.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
08_delete_extend: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
