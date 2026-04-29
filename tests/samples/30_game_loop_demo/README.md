# Sample `30_game_loop_demo`

> Game loop skeleton.

| Field | Value |
| --- | --- |
| Languages | C++, Rust |
| Entry | `game_loop_demo.ploy` |
| Theme | Game loop skeleton |
| Expected stdout | `30_game_loop_demo: ok\r\n` |

## Files

- `game_loop_demo.ploy` — `.ploy` entry that wires the host sources together.
- `expected_output.txt` — byte-exact runtime stdout the regression harness compares against.
- `tick_scheduler.cpp` — host source file
- `physics_step.rs` — host source file

## Build

```powershell
polyc game_loop_demo.ploy --emit-obj=build/game_loop_demo.obj --quiet
polyld build/game_loop_demo.obj -o build/game_loop_demo.exe
./build/game_loop_demo.exe
```

## Expected runtime output

```
30_game_loop_demo: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
