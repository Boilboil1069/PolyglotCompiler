# 示例 `01_basic_linking_v2`

[`01_basic_linking`](../01_basic_linking/) 的镜像版本，使用**带签名 / 标准 `LINK` 形式**。

| 项目     | 值 |
| ---      | --- |
| 涉及语言 | C++, Python |
| 关键字   | LINK（带签名形式）, CALL, IMPORT, EXPORT, MAP_TYPE |
| 入口文件 | `basic_linking.ploy` |

## 与 v1 的差异

v1 示例使用历史悠久的逗号形式：

```ploy
LINK(cpp, python, math_ops::add, string_utils::concat) RETURNS cpp::int { ... }
```

本 v2 示例改用推荐的**带签名**形式，函数签名作为语法的一部分被显式嵌入：

```ploy
LINK cpp::math_ops::add AS FUNC(cpp::int, cpp::int) -> cpp::int { ... }
```

两种形式当前都能解析，但语义分析器会对逗号形式发出弃用警告
（诊断码 `kDeprecatedKeyword`）。**新代码应使用带签名形式。**

## 编译

```powershell
polyc 01_basic_linking_v2\basic_linking.ploy --emit-obj=basic_linking.pobj --obj-format=pobj
polyld basic_linking.pobj -o basic_linking.exe
```

## 运行时预期输出

`expected_output.txt` 中固定的字节序列（CR LF 行尾）：

```
01_basic_linking_v2: ok
```

`scripts/` 下的 `build_all_samples` 回归脚本会运行编译产物，并把
stdout 与 `expected_output.txt` 做字节级比对。

## 英文版本

英文版见 [`README.md`](./README.md)。
