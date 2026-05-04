# 示例 `04_package_import`

IMPORT PACKAGE 配合 semver 版本约束，以及字符串化的 `CONFIG <语言> "<包管理器>" "<路径>";` 声明锁定 Python 虚拟环境。

| 项目 | 值 |
| --- | --- |
| 涉及语言 | C++, Python（镜像样例：JavaScript、Rust、Java） |
| 关键字   | IMPORT PACKAGE, CONFIG（v1.12.0 起字符串形式） |
| 入口文件 | `package_import.ploy` |
| 镜像文件 | `package_import_npm.ploy`、`package_import_cargo.ploy`、`package_import_maven.ploy` |

## 编译

```powershell
polyc 04_package_import\package_import.ploy --emit-obj=package_import.pobj --obj-format=pobj
polyld package_import.pobj -o package_import.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
04_package_import: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。上面描述的其他跨语言行为
由 `tests/unit/` 与 `tests/integration/` 中的单元 / 端到端套件覆盖；
目前能在主机加载器上端到端验证的只有结尾的 `PRINTLN` 标记。

## 英文版本

英文版见 [`README.md`](./README.md)。
