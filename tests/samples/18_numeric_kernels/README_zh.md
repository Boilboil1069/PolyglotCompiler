# 示例 `18_numeric_kernels`

> 数值内核（BLAS 风格）。

| 字段 | 值 |
| --- | --- |
| 语言 | C++、Rust |
| 入口 | `numeric_kernels.ploy` |
| 主题 | 数值内核（BLAS 风格） |
| 预期 stdout | `18_numeric_kernels: ok\r\n` |

## 文件

- `numeric_kernels.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `blas_kernels.cpp` — 宿主语言源文件
- `reduce_kernels.rs` — 宿主语言源文件

## 构建

```powershell
polyc numeric_kernels.ploy --emit-obj=build/numeric_kernels.obj --quiet
polyld build/numeric_kernels.obj -o build/numeric_kernels.exe
./build/numeric_kernels.exe
```

## 预期运行输出

```
18_numeric_kernels: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
