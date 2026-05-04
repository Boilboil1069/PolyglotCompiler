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

## 使用限制

`EXTEND` 被有意限制为仅在**动态宿主语言**上可用（`python`、
`ruby`、`javascript` 及其标签别名）。本样例中两个 `EXTEND` 都
选择 `python`，原因如下：

* 覆写是在加载时向外部运行时的方法表打补丁实现的；
* 外部对象**不会**进入 ploy 的静态类型系统，因此从外部插入子类
  也不会破坏宿主的类型健全。

书写 `EXTEND(cpp, ...)`、`EXTEND(rust, ...)`、`EXTEND(java, ...)`
以及其他静态类型语言作为目标都会被 sema 拒绝，诊断信息为
`EXTEND is not allowed on statically-typed language '<lang>'`。
推荐的替代方案是用一个本地 ploy `FUNC` 包装该接口，再用
`CALL` / `METHOD` 调用外部 API；完整的迁移示例参见
[`35_extend_dynamic`](../35_extend_dynamic/)。
