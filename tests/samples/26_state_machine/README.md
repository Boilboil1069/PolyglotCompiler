# Sample `26_state_machine`

> Finite state machine.

| Field | Value |
| --- | --- |
| Languages | C++, Java |
| Entry | `state_machine.ploy` |
| Theme | Finite state machine |
| Expected stdout | `26_state_machine: ok\r\n` |

## Files

- `state_machine.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `transition_table.cpp` — host source file
- `FsmRunner.java` — host source file

## Build

```powershell
polyc state_machine.ploy --emit-obj=build/state_machine.obj --quiet
polyld build/state_machine.obj -o build/state_machine.exe
./build/state_machine.exe
```

## Expected runtime output

```
26_state_machine: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
