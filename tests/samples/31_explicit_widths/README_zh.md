# 示例 `31_explicit_widths`

> 显式宽度基本类型、TYPE 别名与 CONST 折叠。

| 字段 | 取值 |
| --- | --- |
| 涉及语言 | Ploy、C++ |
| 入口 | `explicit_widths.ploy` |
| 主题 | 显式宽度数值类型 + 编译期常量 |
| 期望 stdout | `31_explicit_widths: ok\r\n` |

## 文件清单

- `explicit_widths.ploy` —— 演示 `i32` / `u32` / `i64` / `f32` 等显式宽度类型，
  声明 `TYPE Pixel = i32` 与 `TYPE ChannelCount = u32`，并通过 ploy 语义分析器
  折叠 3 个 `CONST` 常量。
- `width_kernel.cpp` —— 宿主侧内核，消费宽度敏感的缓冲并返回 `i64` 累加结果。
- `expected_output.txt` —— 回归用例按字节比对的期望 stdout。

## 演示要点

此示例展示了 `v1.7.0` 引入的显式宽度基本类型族（详见 `CHANGELOG_zh.md`）：

| 表层关键字 | 底层类型 | 位宽 | 符号 |
| --- | --- | --- | --- |
| `i8` / `i16` / `i32` / `i64` | `core::Type::Int(N, true)` | 8 / 16 / 32 / 64 | 有符号 |
| `u8` / `u16` / `u32` / `u64` | `core::Type::Int(N, false)` | 8 / 16 / 32 / 64 | 无符号 |
| `f32` / `f64` | `core::Type::Float(N)` | 32 / 64 | 不适用 |
| `usize` / `isize` | 指针宽整数 | 平台原生 | 与名称一致 |

旧式 `INT` 与 `FLOAT` 继续兼容：`INT` 现在是 `i64` 的别名、`FLOAT` 是 `f64` 的别名。
当诊断信息涉及用户别名时会一并展示底层类型，例如 `Pixel (alias of i32)`。

## 构建

```powershell
polyc explicit_widths.ploy --emit-obj=build/explicit_widths.obj --quiet
polyld build/explicit_widths.obj -o build/explicit_widths.exe
./build/explicit_widths.exe
```

## 期望运行输出

```
31_explicit_widths: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本为
`scripts/build_all_samples.sh`）的样例矩阵执行，输出
`build/samples_report.json`，每个样例的状态为
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文姊妹文档：[README.md](./README.md)。
