# 示例 `08_delete_extend`

DELETE 确定性地销毁外部对象，EXTEND 派生跨语言子类。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python |
| 关键字   | DELETE, EXTEND |
| 入口文件 | `delete_extend.ploy` |

## 编译

```powershell
polyc 08_delete_extend\delete_extend.ploy --emit-obj=delete_extend.pobj --obj-format=pobj
polyld delete_extend.pobj -o delete_extend.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
08_delete_extend: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
