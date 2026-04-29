# 示例 `19_file_io`

> 流式文件 I/O。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、C++ |
| 入口 | `file_io.ploy` |
| 主题 | 流式文件 I/O |
| 预期 stdout | `19_file_io: ok\r\n` |

## 文件

- `file_io.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `binary_reader.cpp` — 宿主语言源文件
- `text_decoder.py` — 宿主语言源文件

## 构建

```powershell
polyc file_io.ploy --emit-obj=build/file_io.obj --quiet
polyld build/file_io.obj -o build/file_io.exe
./build/file_io.exe
```

## 预期运行输出

```
19_file_io: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
