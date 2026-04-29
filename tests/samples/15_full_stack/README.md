# Sample `15_full_stack`

Five-language full-stack analytics demo (C++, Python, Rust, Java, C#) wired through PIPELINE / LINK.

| Field | Value |
| --- | --- |
| Languages | C++, Python, Rust, Java, C# |
| Keywords  | all |
| Entry     | `full_stack.ploy` |

## Build

```powershell
polyc 15_full_stack\full_stack.ploy --emit-obj=full_stack.pobj --obj-format=pobj
polyld full_stack.pobj -o full_stack.exe
```

## Expected runtime output

Bytes pinned in `expected_output.txt` (CR LF line ending):

```
15_full_stack: ok
```

The `build_all_samples` harness under `scripts/` runs the produced
executable and byte-compares its stdout against `expected_output.txt`.
The cross-language behaviour described above is exercised by the unit
and end-to-end suites under `tests/unit/` and `tests/integration/`;
the trailing `PRINTLN` marker is the only stdout currently verified
end-to-end on the host loader.

## Chinese version

See [`README_zh.md`](./README_zh.md) for the Chinese counterpart.
