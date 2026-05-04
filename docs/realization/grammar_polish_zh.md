# 语法瑕疵收尾包（自 v1.18.0 起）

本文档记录 v1.18.0 P3 收尾包：一组面向从 Rust / Python / TypeScript
迁移用户的语法细节打磨。

英文对照见 [`grammar_polish.md`](grammar_polish.md)。

## 1. `IF` / `WHILE` / `FOR` 外层括号可选

`IF (cond) { … }` 与 `IF cond { … }` 现在等价；`WHILE` 与 `FOR` 同理。

`IF` 与 `WHILE` 不需要解析器修改，因为表达式语法本身已接受
`(expr)` 作为分组表达式。`FOR` 显式吸收一个可选的前导 `(`，并
在见到时要求迭代值后有匹配的 `)`。

```ploy
IF (n > 0) { … }            // 等价于 IF n > 0 { … }
WHILE (running) { … }       // 等价于 WHILE running { … }
FOR (x IN xs) { … }         // 等价于 FOR x IN xs { … }
```

## 2. `IF LET` 解构 `OPTION<T>`

新增语句形式，将 `OPTION<T>` 解包并把内部值绑定到 THEN 体作用域：

```ploy
IF LET Some(x) = opt { use(x); }
IF LET Some(x) = opt { use(x); } ELSE { fallback(); }
IF LET None    = opt { … }       // 无绑定
```

新增 AST 节点 `IfLetStatement`，包含 `ctor`、`bindings`、`scrutinee`、
`then_body` 和 `else_body`。Sema 强制：

- 被检查值类型必须为 `OPTION<T>`（跨语言未解析时退化为 `Any` /
  `Unknown`）；
- `Some` 必须有且仅有一个绑定名；
- `None` 不接受绑定；
- 绑定以 `T`（`OPTION` 的内部类型实参）类型注册为新的 `kVariable`
  符号。

降级阶段当前以被检查值真值性进行分支（MVP）；完整的 tag 判别码降级
随专门的 OPTION 降级工作轨道交付。

## 3. `NULL` ↔ `OPTION<T>` 诊断

`NULL` 仅用于裸指针互操作。用它构造 `OPTION<T>` 现在会被 sema
拒绝并给出明确建议：

```ploy
LET o: OPTION<i32> = NULL;
// error: cannot initialise OPTION<T> with NULL; use 'None' instead
```

## 4. `///` 文档注释

恰好以三个斜杠开头的行是*文档注释*。词法器维护实例级缓冲
`pending_doc_`（去掉一个可选的前导空格、去掉行尾 CR），并通过
`TakePendingDoc()` 暴露给解析器。

`ParseFuncDecl`、`ParseStructDecl`、`ParseVarDecl` 在入口处拉取
缓冲并写入对应 AST 节点。普通 `//` 行注释与 `////`（四斜杠）横幅
仍为普通行注释。

```ploy
/// 计算两个整数之和。
/// 溢出按 2^64 取模。
FUNC add(a: i64, b: i64) -> i64 { RETURN a + b; }
```

## 5. `polydoc` 抽取工具

新增可执行文件 `polydoc`（`tools/polydoc/`），遍历一个或多个
`.ploy` 文件，收集所有带文档的顶层声明，输出 Markdown 或 JSON：

```sh
polydoc src/foo.ploy                 # Markdown 到 stdout
polydoc --json src/foo.ploy          # JSON 到 stdout
polydoc -o api.md src/foo.ploy       # 写入 api.md
```

Markdown 形式为每个条目合成一行签名（`FUNC name(params) -> R`、
`STRUCT Name`、`LET name: T`）。JSON 形式记录同样的数据并附上源
文件路径，便于下游工具链索引。

## 6. `LIST<T>` 命名澄清

`LIST<T>` 是连续序列容器，等价于 Rust `Vec<T>` 与 C++
`std::vector<T>`，**不是**链表。规范、本文以及 IDE 悬浮提示均使用
统一表述，避免与链表 API 混淆。

## 7. 测试

- `tests/unit/frontends/ploy/polish_grammar_test.cpp`（11 例）：
  IF/WHILE/FOR 可选括号、`IF LET Some` / `IF LET None`、
  NULL-with-OPTION 诊断、FUNC / STRUCT / LET 的 `///` 捕获，
  以及 `//` 与 `///` 的边界判定。
- `tests/samples/41_grammar_polish/` 端到端演示新构造，并已在
  samples README 中引用。
