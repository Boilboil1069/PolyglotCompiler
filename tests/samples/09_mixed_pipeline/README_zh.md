# 示例 `09_mixed_pipeline`

端到端 PIPELINE 在 C++ / Python / Rust 上汇集全部关键字，演示一个小型 ML 流程。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python, Rust |
| 关键字   | LINK, PIPELINE, NEW, METHOD, WITH, DELETE, EXTEND |
| 入口文件 | `mixed_pipeline.ploy` |

## 编译

```powershell
polyc 09_mixed_pipeline\mixed_pipeline.ploy --emit-obj=mixed_pipeline.pobj --obj-format=pobj
polyld mixed_pipeline.pobj -o mixed_pipeline.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
09_mixed_pipeline: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
