# 示例 41 — 语法瑕疵收尾（自 v1.18.0 起）

演示 v1.18.0 收尾包引入的小语法改进：

- `IF` / `WHILE` / `FOR` 外层括号可选。`IF (cond) { … }` 与
  `IF cond { … }` 语义一致；`WHILE` 与 `FOR (i IN xs) { … }` 同理。
- `IF LET Some(x) = opt { … } ELSE { … }` 用于解构 `OPTION<T>`；
  `IF LET None = opt { … }` 同样支持，且不接受绑定。
- `///` 文档注释在紧邻 `FUNC` / `STRUCT` / `LET` / `VAR` 声明时
  会被收集。`polydoc` 工具会遍历 `.ploy` 源码，将文档块渲染为
  Markdown 或 JSON。
- 提示：`LIST<T>` 是连续序列容器，等价于 Rust `Vec<T>` 或 C++
  `std::vector<T>`，**不是**链表。形式定义见语言规范。
- 提示：`NULL` 仅用于裸指针互操作。**不要**用 `NULL` 构造 `OPTION<T>`；
  sema 会发出明确诊断并建议改用 `None`。

构建与运行：

```sh
polyc 41_grammar_polish/grammar_polish.ploy -o grammar_polish
./grammar_polish

polydoc 41_grammar_polish/grammar_polish.ploy           # Markdown
polydoc --json 41_grammar_polish/grammar_polish.ploy    # JSON
```
