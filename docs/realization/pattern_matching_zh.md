# 模式匹配

本文档介绍 `.ploy` 中 `MATCH` 语句的模式语法、前端在每个分支上执行
的语义检查，以及 IR 生成器在两种降级（lowering）策略之间的选择。
英文版位于
[`pattern_matching.md`](pattern_matching.md)。

## 1. 表面语法

```
match_stmt    ::= 'MATCH' expr '{' (case_arm | default_arm)* '}'
case_arm      ::= 'CASE' pattern ('IF' expr)? arm_body
default_arm   ::= 'DEFAULT' arm_body
arm_body      ::= ('->' | '=>')? '{' statement* '}'
```

模式（或守卫）与分支体之间的箭头是可选的；为了兼容早期的
规范草稿，`->` 与 `=>` 均被接受。当前规范的标准写法不带箭头：

```ploy
MATCH value {
    CASE 0 { RETURN "zero"; }
    CASE _ { RETURN "other"; }
}
```

`MATCH` 不会贯穿（fall through）：每个分支执行完毕后，控制流
跳转到 `MATCH` 之后的汇合点。`MATCH` 本身是语句而非表达式 ——
若需返回值，每个分支应使用 `RETURN`，或写入外层捕获的 `VAR`。

## 2. 模式语法

```
pattern         ::= pattern_primary ('|' pattern_primary)*

pattern_primary ::= '_'                                          # 通配
                  | literal_with_optional_minus
                  | literal '..'  literal                        # 半开范围
                  | literal '..=' literal                        # 闭合范围
                  | '(' pattern (',' pattern)* ')'               # 元组
                  | Identifier '(' pattern (',' pattern)* ')'    # 构造子
                  | Identifier '{' field_pattern (',' field_pattern)* (',' '..')? '}'
                  | Identifier '@' pattern_primary               # 绑定
                  | Identifier ':' Type                          # 类型守卫
                  | 'None'                                       # OPTION 单元构造子
                  | Identifier                                   # 裸标识符绑定

field_pattern   ::= Identifier (':' pattern)?
```

裸标识符 `None` 会被解析为零参数的 `ConstructorPattern`，对应 OPTION
的单元变体；这样 `CASE None` 才能参与 OPTION 的详尽性判定，而不会
被误当作"将整个被检值绑到名字 `None`"。

### 2.1 模式语义

| 模式                  | 是否可拒绝 | 引入的绑定                          |
| --------------------- | ---------- | ----------------------------------- |
| `_`                   | 否         | 无                                  |
| `42`、`"x"`、`TRUE`   | 是         | 无                                  |
| `1..10`、`0..=255`    | 是         | 无                                  |
| `(a, b)`              | 任一元素可拒绝即整体可拒绝 | 元素绑定的并集 |
| `Point { x, y, .. }`  | 任一字段子模式可拒绝即整体可拒绝 | 字段绑定的并集 |
| `1 \| 2 \| 3`         | 所有备选均可拒绝即整体可拒绝 | 取首个备选的绑定（所有备选必须以兼容类型绑定相同的名字） |
| `n @ 0..=100`         | 子模式可拒绝即整体可拒绝 | `n` 加上子模式绑定 |
| `n: i32`              | 否（编译期细化）   | 以细化后的类型绑定 `n` |
| `Some(x)`             | 是         | `x` 的绑定（按其子模式递归） |
| `None`                | 是         | 无                                  |
| `name`                | 否         | 将 `name` 绑定到被检值              |

当一个模式接受被检值静态类型下的所有取值时，称其为
**不可拒绝**（irrefutable）。不可拒绝模式可以让 `MATCH` 在没有
显式 `DEFAULT` 或 `CASE _` 的情况下达成详尽。

### 2.2 歧义消解

裸标识符 `Name` 后接 `{` 在语法上有歧义：可能是结构体模式
（`Point { x, y }`），也可能是裸标识符模式且 `{` 是分支体的开括号
（`CASE None { RETURN ...; }`）。解析器会越过 `{` 做一字符前瞻：
若下一个 token 是标识符、`..` 或紧跟的 `}`，提交为
`StructPattern`；否则将 `{` 视为分支体的开始。

## 3. 语义检查

### 3.1 类型相容性

每个模式都会针对被检值的静态类型做检查：数字字面量按可赋值性
检查，范围模式要求被检值是整型或浮点型，结构体模式按已注册的
结构体 schema 检查（未知字段是硬错误），构造子模式按 OPTION
schema 检查，元组模式必须与被检值元数匹配。

### 3.2 绑定与作用域

模式引入的绑定会在分支体及其 `IF` 守卫期间加入符号表，并在退出
分支时弹出。因此，不同分支中相同名字的绑定不会互相泄漏，同一
个标识符可以在多个分支中以不同类型出现。

### 3.3 详尽性

当 `MATCH` 既无 `DEFAULT` 也无不可拒绝分支时，前端会检查所有
分支模式的并集是否覆盖被检值类型：

| 被检值类型     | 必须覆盖                                              |
| -------------- | ----------------------------------------------------- |
| `bool`         | `TRUE` 与 `FALSE` 两个字面量分支                      |
| `OPTION(T)`    | `Some(_)` 与 `None` 两个分支                          |
| 其他类型       | 一个不可拒绝分支（`CASE _` / `CASE name` / 全为不可拒绝子模式的元组或结构体 / `name: Type`）或 `DEFAULT` |

不详尽的 MATCH 报告为硬错误（`E_TYPE_MISMATCH`），并附带建议
（`CASE _` 或具体缺失的变体）。

### 3.4 可达性

每记录一个分支后，分析器会将以下分支标记为**不可达**：

* 出现在不可拒绝分支或 `DEFAULT` 之后的分支；
* 字面量值与之前已记录的字面量重复的分支。

不可达分支只产生警告而非错误；lowering 仍会保留它们，使行号与
诊断 UI 保持稳定。

### 3.5 守卫

`IF expr` 在模式绑定的作用域内被检查为 `bool` 表达式。带守卫的
分支即便其模式本身不可拒绝，也会变得可拒绝，因此该分支无法
单独满足详尽性。

## 4. 降级（Lowering）

`LowerMatchStatement` 在两条路径间二选一：

### 4.1 快路径 —— 稠密 switch 表

当所有 CASE 模式都是简单整数字面量（或通配 `_`），且没有 `IF`
守卫时，lowering 直接生成单条 `ir::SwitchStatement`：默认目标
是显式的 `DEFAULT` / `CASE _` 块，或一个合成的 `match.merge`
unreachable 块。每个分支独立成块，后端可将其物化为稠密跳转表。

### 4.2 级联路径 —— 结构化 if/else 链

对其他形态（范围、元组、结构体、OR、绑定、类型守卫、构造子，或
任何带守卫的分支），lowering 会通过 `lower_predicate` 为每个
分支合成 `i1` 谓词，并按
`match.try.N` → `match.body.N` → `match.merge` 的形式串联：

```
                 ┌──────────────────┐
                 │ entry            │
                 │ scrutinee = …    │──┐
                 └──────────────────┘  │
                                       ▼
                       ┌──────────────────┐    miss     ┌──────────────────┐
                       │ match.try.0      │ ──────────► │ match.try.1      │ ─► …
                       │ pred(p0)         │             │ pred(p1)         │
                       └──────────────────┘             └──────────────────┘
                                │ hit                          │ hit
                                ▼                              ▼
                       ┌──────────────────┐             ┌──────────────────┐
                       │ match.body.0     │             │ match.body.1     │
                       │ bind(p0); body0  │             │ bind(p1); body1  │
                       └──────────────────┘             └──────────────────┘
                                │ no terminator                │
                                └─────────────► match.merge ◄──┘
```

模式绑定在分支体之前通过复用被检值的 SSA 值（或其元组 / 结构体
投影）来物化；`.ploy` 默认值不可变，所以无需复制。

正是级联路径让守卫、含混合备选的 OR 模式、OPTION 变体以及任意
嵌套模式都能被降级为验证器已经能识别的普通基本块跳转。

## 5. 示例

### 5.1 布尔派发 —— 无需 DEFAULT 即可详尽

```ploy
FUNC describe(b: bool) -> STRING {
    MATCH b {
        CASE TRUE  { RETURN "yes"; }
        CASE FALSE { RETURN "no"; }
    }
}
```

### 5.2 OPTION 解包

```ploy
FUNC unwrap_or(opt: OPTION(i32), fallback: i32) -> i32 {
    MATCH opt {
        CASE Some(x) { RETURN x; }
        CASE None    { RETURN fallback; }
    }
}
```

### 5.3 范围 / OR / 绑定 / 类型守卫，全部出现在同一个 MATCH 中

```ploy
FUNC classify(value: i32) -> i32 {
    MATCH value {
        CASE 0                    { RETURN 100; }
        CASE 2 | 4 | 8 | 16       { RETURN 102; }
        CASE 10..20               { RETURN 103; }
        CASE n @ 100..=199        { RETURN n + 200; }
        CASE n: i32 IF n > 1000   { RETURN n - 1000; }
        CASE _                    { RETURN -1; }
    }
}
```

### 5.4 元组与结构体解构

```ploy
STRUCT Point { x: i32, y: i32, label: STRING }

FUNC pair_kind(p: TUPLE(i32, i32)) -> i32 {
    MATCH p {
        CASE (0, 0) { RETURN 200; }
        CASE (_, 0) { RETURN 201; }
        CASE (0, _) { RETURN 202; }
        CASE (a, b) { RETURN a + b; }
    }
}

FUNC point_kind(pt: Point) -> i32 {
    MATCH pt {
        CASE Point { x: 0, y: 0, .. } { RETURN 300; }
        CASE Point { x, y, .. }       { RETURN x + y; }
    }
}
```

完整可运行的示例位于
[`tests/samples/33_pattern_matching/`](../../tests/samples/33_pattern_matching/)。

## 6. 诊断一览

| 级别 | 触发条件                                            | 建议 |
| ---- | --------------------------------------------------- | ---- |
| 错误 | 对 `bool` 的 MATCH 不详尽                            | 补齐缺失的 `TRUE` / `FALSE` 分支 |
| 错误 | 对 `OPTION` 的 MATCH 不详尽                          | 补齐 `Some(_)` 与 / 或 `None` |
| 错误 | 对其他类型的 MATCH 不详尽                            | 添加 `CASE _` 或 `DEFAULT` |
| 错误 | OR 模式的备选绑定的名字不一致                        | 让所有备选绑定相同的名字 |
| 错误 | 范围模式作用于非数值被检值                           | 仅对整型 / 浮点型使用范围 |
| 错误 | 元组模式元数不匹配                                   | 调整为与被检值一致的元数 |
| 错误 | 结构体模式包含未知字段                               | 检查结构体 schema |
| 错误 | 守卫表达式不是 `bool`                                | 用 `==` / `<` / `AND` 包装守卫 |
| 警告 | 紧跟在不可拒绝分支或 `DEFAULT` 之后的分支            | 把不可达分支挪到兜底之前，或删除它 |
| 警告 | 字面量分支重复                                       | 删除其中一份重复 |

## 7. 测试覆盖

* 单元：[`tests/unit/frontends/ploy/pattern_matching_test.cpp`](../../tests/unit/frontends/ploy/pattern_matching_test.cpp)
  覆盖每种模式形态、详尽性规则与可达性警告。
* 降级：[`tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp`](../../tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp)
  锁定快 / 级联两条路径的选择以及对应的基本块结构。
* 样例：[`tests/samples/33_pattern_matching/`](../../tests/samples/33_pattern_matching/)
  是一个端到端可运行的演示，附带确定性的 stdout 标记。
