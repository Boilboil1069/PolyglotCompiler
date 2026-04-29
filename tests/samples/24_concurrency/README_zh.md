# 示例 `24_concurrency`

> 并发原语。

| 字段 | 值 |
| --- | --- |
| 语言 | C++、Rust |
| 入口 | `concurrency.ploy` |
| 主题 | 并发原语 |
| 预期 stdout | `24_concurrency: ok\r\n` |

## 文件

- `concurrency.ploy` — `.ploy` 入口文件，串联各宿主语言源文件。
- `expected_output.txt` — 回归脚本进行字节对比的预期 stdout。
- `atomic_counter.cpp` — 宿主语言源文件
- `worker_pool.rs` — 宿主语言源文件

## 构建

```powershell
polyc concurrency.ploy --emit-obj=build/concurrency.obj --quiet
polyld build/concurrency.obj -o build/concurrency.exe
./build/concurrency.exe
```

## 预期运行输出

```
24_concurrency: ok
```

## 回归脚本

该示例由 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）所驱动的矩阵覆盖。脚本会写出
`build/samples_report.json`，每个样例对应一条记录，状态取自
`OK / OUTPUT_MISMATCH / RUN_FAIL / EMPTY_STDOUT / LINK_FAIL / COMPILE_FAIL / SKIP`。

英文版：[README.md](./README.md)。
