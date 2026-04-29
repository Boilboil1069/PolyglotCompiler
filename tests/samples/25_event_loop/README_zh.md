# 示例 `25_event_loop`

> 事件循环模拟。

| 字段 | 值 |
| --- | --- |
| 语言 | Python、JavaScript |
| 入口 | `event_loop.ploy` |
| 主题 | 事件循环模拟 |
| 预期 stdout | `25_event_loop: ok\r\n` |

## 文件

- `event_loop.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `scheduler.js` — 宿主语言源文件
- `dispatcher.py` — 宿主语言源文件

## 构建

```powershell
polyc event_loop.ploy --emit-obj=build/event_loop.obj --quiet
polyld build/event_loop.obj -o build/event_loop.exe
./build/event_loop.exe
```

## 预期运行输出

```
25_event_loop: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
