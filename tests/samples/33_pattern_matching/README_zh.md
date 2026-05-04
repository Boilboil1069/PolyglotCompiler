# 33 — 模式匹配

演示扩展后的 `MATCH` 模式语义：字面量、通配符、范围、元组、结构体、
OR 模式、绑定、类型守卫以及 `OPTION` 构造子模式，并由前端进行详尽性
（exhaustiveness）与可达性（reachability）检查。

## 你将看到

* `classify_int` —— 字面量、OR（`2 | 4 | 8 | 16`）、半开与闭合
  范围（`10..20` / `20..=29`）、绑定（`n @ 100..=199`）以及一个
  带类型守卫的分支（`n: i32 IF n > 1000`），最后由 `CASE _` 兜底。
* `classify_pair` —— 元组解构，带 "忽略某一元素" 的简写 `(_, b)`。
  在没有 `DEFAULT` 的情况下，最后一条不可拒绝的 `(a, b)` 使整个
  MATCH 达到详尽。
* `classify_point` —— 结构体按字段名解构，并使用 `..` 表示忽略
  其余字段。
* `classify_option` —— `OPTION` 的 `Some(x)` 与无括号的 `None`
  两种构造子；两者合在一起即详尽，无需 `_` 或 `DEFAULT`。

## 运行方式

```powershell
polyc 33_pattern_matching/pattern_matching.ploy --emit-obj=build/sample.obj --quiet
polyld build/sample.obj -o build/sample.exe
.\build\sample.exe
```

期望 stdout：`33_pattern_matching: ok`（末尾 `\r\n`）。

## 为什么重要

* **不会静默贯穿。** 每个分支执行完毕后立即退出 `MATCH`，不存在
  C 风格 `switch` 的 fall-through 风险。
* **编译期详尽性。** 对 `bool` 与 `OPTION` 必须覆盖所有变体；
  其他类型必须显式使用 `CASE _` 或 `DEFAULT`。
* **编译期可达性。** 紧跟在不可拒绝通配之后的分支以及重复字面量
  分支都会触发警告，死代码不会进入 lowering。
* **热路径的单一跳转表。** 当所有分支都是简单整数字面量
  （或 `CASE _`）时，lowering 会折叠成单条 `SwitchStatement`，
  后端可生成稠密跳转表。
