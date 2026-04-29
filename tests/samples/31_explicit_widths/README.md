# Sample `31_explicit_widths`

> Explicit-width primitives, TYPE aliases, and CONST folding.

| Field | Value |
| --- | --- |
| Languages | Ploy, C++ |
| Entry | `explicit_widths.ploy` |
| Theme | Width-aware numeric types + compile-time constants |
| Expected stdout | `31_explicit_widths: ok\r\n` |

## Files

- `explicit_widths.ploy` — `.ploy` entry that exercises `i32` / `u32` / `i64` / `f32`,
  declares `TYPE Pixel = i32` and `TYPE ChannelCount = u32`, and folds three
  `CONST` declarations through the ploy semantic analyser.
- `width_kernel.cpp` — host kernel that consumes the width-aware buffer and
  returns an `i64` accumulator.
- `expected_output.txt` — byte-exact runtime stdout the regression harness
  compares against.

## What it demonstrates

This sample showcases the explicit-width primitive type family introduced in
`v1.7.0` (see `CHANGELOG.md`):

| Surface | Underlying type   | Width | Sign     |
| ------- | ----------------- | ----- | -------- |
| `i8` / `i16` / `i32` / `i64` | `core::Type::Int(N, true)`  | 8 / 16 / 32 / 64 | signed   |
| `u8` / `u16` / `u32` / `u64` | `core::Type::Int(N, false)` | 8 / 16 / 32 / 64 | unsigned |
| `f32` / `f64`                | `core::Type::Float(N)`      | 32 / 64          | n/a      |
| `usize` / `isize`            | pointer-width integer       | platform-native  | as named |

Legacy `INT` and `FLOAT` continue to work; `INT` is now an alias of `i64` and
`FLOAT` is an alias of `f64`.  Diagnostics that touch a user alias render the
underlying type for clarity, e.g. `Pixel (alias of i32)`.

## Build

```powershell
polyc explicit_widths.ploy --emit-obj=build/explicit_widths.obj --quiet
polyld build/explicit_widths.obj -o build/explicit_widths.exe
./build/explicit_widths.exe
```

## Expected runtime output

```
31_explicit_widths: ok
```

## Regression harness

The sample participates in the matrix exercised by
`scripts/build_all_samples.ps1` (and its POSIX twin
`scripts/build_all_samples.sh`).  The harness writes
`build/samples_report.json` containing one entry per sample with status
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`.

Bilingual sibling: [README_zh.md](./README_zh.md).
