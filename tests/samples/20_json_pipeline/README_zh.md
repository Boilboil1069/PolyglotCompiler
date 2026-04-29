# 示例 `20_json_pipeline`

> JSON 摄取流水线。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、Java |
| 入口 | `json_pipeline.ploy` |
| 主题 | JSON 摄取流水线 |
| 预期 stdout | `20_json_pipeline: ok\r\n` |

## 文件

- `json_pipeline.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `json_parser.py` — 宿主语言源文件
- `SchemaValidator.java` — 宿主语言源文件

## 构建

```powershell
polyc json_pipeline.ploy --emit-obj=build/json_pipeline.obj --quiet
polyld build/json_pipeline.obj -o build/json_pipeline.exe
./build/json_pipeline.exe
```

## 预期运行输出

```
20_json_pipeline: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
