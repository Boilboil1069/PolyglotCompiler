# 示例 `17_string_processing`

> 字符串处理流水线。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、Rust |
| 入口 | `string_processing.ploy` |
| 主题 | 字符串处理流水线 |
| 预期 stdout | `17_string_processing: ok\r\n` |

## 文件

- `string_processing.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `str_tokenizer.rs` — 宿主语言源文件
- `case_folder.py` — 宿主语言源文件

## 构建

```powershell
polyc string_processing.ploy --emit-obj=build/string_processing.obj --quiet
polyld build/string_processing.obj -o build/string_processing.exe
./build/string_processing.exe
```

## 预期运行输出

```
17_string_processing: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
