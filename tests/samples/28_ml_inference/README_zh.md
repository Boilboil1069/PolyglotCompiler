# 示例 `28_ml_inference`

> 机器学习推理流水线。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、Rust |
| 入口 | `ml_inference.ploy` |
| 主题 | 机器学习推理流水线 |
| 预期 stdout | `28_ml_inference: ok\r\n` |

## 文件

- `ml_inference.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `tokenizer.py` — 宿主语言源文件
- `scorer.rs` — 宿主语言源文件

## 构建

```powershell
polyc ml_inference.ploy --emit-obj=build/ml_inference.obj --quiet
polyld build/ml_inference.obj -o build/ml_inference.exe
./build/ml_inference.exe
```

## 预期运行输出

```
28_ml_inference: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
