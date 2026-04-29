# 示例 `07_resource_management`

用 WITH 绑定 Python 的 __enter__ / __exit__ 协议，确保提前返回也能释放资源。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python |
| 关键字   | WITH |
| 入口文件 | `resource_management.ploy` |

## 编译

```powershell
polyc 07_resource_management\resource_management.ploy --emit-obj=resource_management.pobj --obj-format=pobj
polyld resource_management.pobj -o resource_management.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
07_resource_management: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
