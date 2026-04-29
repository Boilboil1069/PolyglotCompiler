# 示例 `13_generic_containers`

通过 MAP_TYPE 共享 java.util.ArrayList / HashMap、std::vector 与 Python list 等通用容器。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Java, Python |
| 关键字   | MAP_TYPE containers |
| 入口文件 | `generic_containers.ploy` |

## 编译

```powershell
polyc 13_generic_containers\generic_containers.ploy --emit-obj=generic_containers.pobj --obj-format=pobj
polyld generic_containers.pobj -o generic_containers.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
13_generic_containers: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
