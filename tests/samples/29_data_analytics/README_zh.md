# 示例 `29_data_analytics`

> 数据分析。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、Java |
| 入口 | `data_analytics.ploy` |
| 主题 | 数据分析 |
| 预期 stdout | `29_data_analytics: ok\r\n` |

## 文件

- `data_analytics.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `record_loader.py` — 宿主语言源文件
- `Aggregator.java` — 宿主语言源文件

## 构建

```powershell
polyc data_analytics.ploy --emit-obj=build/data_analytics.obj --quiet
polyld build/data_analytics.obj -o build/data_analytics.exe
./build/data_analytics.exe
```

## 预期运行输出

```
29_data_analytics: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
