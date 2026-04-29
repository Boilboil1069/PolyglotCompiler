# 示例 `14_async_pipeline`

多阶段信号处理 PIPELINE，融合 C++ DSP、Rust 异步加载与 Python 可视化。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Rust, Python |
| 关键字   | PIPELINE, IF / ELSE |
| 入口文件 | `async_pipeline.ploy` |

## 编译

```powershell
polyc 14_async_pipeline\async_pipeline.ploy --emit-obj=async_pipeline.pobj --obj-format=pobj
polyld async_pipeline.pobj -o async_pipeline.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
14_async_pipeline: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
