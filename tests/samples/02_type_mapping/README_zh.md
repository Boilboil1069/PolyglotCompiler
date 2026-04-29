# 示例 `02_type_mapping`

通过 MAP_TYPE / STRUCT / CONVERT 展示复杂类型如何跨越桥接边界。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python |
| 关键字   | MAP_TYPE, STRUCT, CONVERT |
| 入口文件 | `type_mapping.ploy` |

## 编译

```powershell
polyc 02_type_mapping\type_mapping.ploy --emit-obj=type_mapping.pobj --obj-format=pobj
polyld type_mapping.pobj -o type_mapping.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
02_type_mapping: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
