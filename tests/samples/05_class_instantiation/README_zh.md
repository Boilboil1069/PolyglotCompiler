# 示例 `05_class_instantiation`

通过 NEW / METHOD 在 .ploy 中实例化 Python 类，并经由 C++ 辅助函数转发调用。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python |
| 关键字   | NEW, METHOD |
| 入口文件 | `class_instantiation.ploy` |

## 编译

```powershell
polyc 05_class_instantiation\class_instantiation.ploy --emit-obj=class_instantiation.pobj --obj-format=pobj
polyld class_instantiation.pobj -o class_instantiation.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
05_class_instantiation: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
