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

## Limitations

`EXTEND` is intentionally restricted to **dynamic host languages**
(`python`, `ruby`, `javascript` and their tag aliases).  The two
`EXTEND` blocks in this sample target `python` because:

* the override is installed by patching the foreign runtime's method
  dispatch table at load time;
* the foreign object **does not** enter the Ploy static type system,
  so an out-of-source subclass cannot break the host's soundness.

Writing `EXTEND(cpp, ...)`, `EXTEND(rust, ...)`, `EXTEND(java, ...)`
or any other statically-typed target is rejected by sema with the
diagnostic
`EXTEND is not allowed on statically-typed language '<lang>'`.
The recommended alternative is a local Ploy `FUNC` wrapper that
uses `CALL` / `METHOD` to invoke the foreign API; see sample
[`35_extend_dynamic`](../35_extend_dynamic/) for the full migration
pattern.
