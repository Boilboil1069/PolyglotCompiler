# 示例 `15_full_stack`

五语言全栈分析样例（C++ / Python / Rust / Java / C#），通过 PIPELINE / LINK 串联。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python, Rust, Java, C# |
| 关键字   | all |
| 入口文件 | `full_stack.ploy` |

## 编译

```powershell
polyc 15_full_stack\full_stack.ploy --emit-obj=full_stack.pobj --obj-format=pobj
polyld full_stack.pobj -o full_stack.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
15_full_stack: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
