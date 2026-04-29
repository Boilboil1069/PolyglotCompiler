# Sample `11_java_interop`

Cross-language NEW / METHOD against a Java class with Python feeding it analysis input.

| Field | Value |
| --- | --- |
| Languages | Java, Python |
| Keywords  | NEW, METHOD (Java) |
| Entry     | `java_interop.ploy` |

## Build

```powershell
polyc 11_java_interop\java_interop.ploy --emit-obj=java_interop.pobj --obj-format=pobj
polyld java_interop.pobj -o java_interop.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
11_java_interop: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
