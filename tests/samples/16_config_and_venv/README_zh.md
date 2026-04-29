# 示例 `16_config_and_venv`

CONFIG VENV 与 IMPORT PACKAGE 版本约束驱动 Python 数据科学任务，再交给 C#。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | Python, C# |
| 关键字   | CONFIG VENV, IMPORT PACKAGE, CONVERT |
| 入口文件 | `config_and_venv.ploy` |

## 编译

```powershell
polyc 16_config_and_venv\config_and_venv.ploy --emit-obj=config_and_venv.pobj --obj-format=pobj
polyld config_and_venv.pobj -o config_and_venv.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
16_config_and_venv: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
