# 示例 `03_pipeline`

多阶段 PIPELINE 把 C++ 图像预处理与 Python ML 推理串起来，并演示 IF / WHILE / FOR / MATCH 控制流。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python |
| 关键字   | PIPELINE, IF, WHILE, FOR, MATCH |
| 入口文件 | `pipeline.ploy` |

## 编译

```powershell
polyc 03_pipeline\pipeline.ploy --emit-obj=pipeline.pobj --obj-format=pobj
polyld pipeline.pobj -o pipeline.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
03_pipeline: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
