# 示例 `10_error_handling`

通过故意构造的非法 LINK / MAP_TYPE / IMPORT 演示诊断输出表面。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python |
| 关键字   | diagnostics, error recovery |
| 入口文件 | `error_handling.ploy` |

## 编译

```powershell
polyc 10_error_handling\error_handling.ploy --emit-obj=error_handling.pobj --obj-format=pobj
polyld error_handling.pobj -o error_handling.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
10_error_handling: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
