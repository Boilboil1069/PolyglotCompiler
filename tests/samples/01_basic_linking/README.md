# Sample `01_basic_linking`

> **Legacy form.** This sample uses the original comma-style `LINK(target_lang, source_lang, ...)` syntax.
> The semantic analyzer now reports this form with a deprecation warning.
> A mirror sample using the recommended **signed** form is available at
> [`../01_basic_linking_v2/`](../01_basic_linking_v2/).

Basic LINK / CALL / IMPORT / EXPORT directives between C++ math operations and Python string utilities.

| Field | Value |
| --- | --- |
| Languages | C++, Python |
| Keywords  | LINK, CALL, IMPORT, EXPORT, MAP_TYPE |
| Entry     | `basic_linking.ploy` |

## Build

```powershell
polyc 01_basic_linking\basic_linking.ploy --emit-obj=basic_linking.pobj --obj-format=pobj
polyld basic_linking.pobj -o basic_linking.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
01_basic_linking: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
