# 示例 `11_java_interop`

对 Java 类的 NEW / METHOD 跨语言调用，并由 Python 提供分析输入。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | Java, Python |
| 关键字   | NEW, METHOD (Java) |
| 入口文件 | `java_interop.ploy` |

## 编译

```powershell
polyc 11_java_interop\java_interop.ploy --emit-obj=java_interop.pobj --obj-format=pobj
polyld java_interop.pobj -o java_interop.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
11_java_interop: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
