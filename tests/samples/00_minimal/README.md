# 00_minimal — print_then_exit

The simplest possible end-to-end smoke sample for the polyglot
toolchain. The single source file `print_then_exit.ploy` declares a
`main` function that prints `ok` followed by a newline and returns
exit code `0`.

This sample is shared by the macOS Mach-O execve smoke
(`tests/integration/macho_exec_smoke_test.cpp`), the Linux ELF
execve smoke, and the Windows PE-7 PRINTLN smoke; every supported
host should produce a binary that exits with status `0` and writes
the line `ok\n` to standard output.

## Build & run

```sh
polyc print_then_exit.ploy -o /tmp/print_then_exit
/tmp/print_then_exit
echo "exit=$?"
```

Expected console output:

```
ok
```

The expected exit status is `0`.

## Files

| File | Purpose |
|------|---------|
| `print_then_exit.ploy` | The single-function source. |
| `expected_output.txt`  | Single-line `ok` produced by a real run; consumed by the samples regression harness. |
| `README.md` / `README_zh.md` | Bilingual documentation. |
