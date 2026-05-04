# 示例 40：扩展字符串字面量

演示 v1.17.0 引入的四种字符串字面量形式。

| 形式      | 语法                                  | 说明                                |
| --------- | ------------------------------------- | ----------------------------------- |
| 普通      | `"hello\n"`                           | 支持标准反斜杠转义                  |
| 原始      | `r"C:\path\no\escape"`                | 反斜杠按字面字符处理                |
| 带填充原始| `r#"contains "quotes""#`              | `#` 填充使内部可包含 `"`            |
| 多行      | `"""line1\nline2"""`                  | 换行字符按字面保留                  |
| 模板      | `f"x = {x + y}"`                      | 大括号包裹的表达式插值              |

## 构建

```bash
polyc string_literals.ploy -o string_literals
./string_literals
```

## v1.17.0 MVP 范围

词法器、解析器、AST 与 sema 已端到端打通。**所有插值表达式均为
`Literal` 时**，下沉层在编译期完成模板字符串拼接；运行时变量插值
作为后续工作记录于 `docs/realization/string_literals_zh.md`。
