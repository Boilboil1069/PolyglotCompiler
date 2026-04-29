# 示例 `12_dotnet_interop`

对 .NET (C#) 类的 NEW / METHOD 跨语言调用，由 Python 统计工具提供数据。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C#, Python |
| 关键字   | NEW, METHOD (.NET) |
| 入口文件 | `dotnet_interop.ploy` |

## 编译

```powershell
polyc 12_dotnet_interop\dotnet_interop.ploy --emit-obj=dotnet_interop.pobj --obj-format=pobj
polyld dotnet_interop.pobj -o dotnet_interop.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
12_dotnet_interop: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
