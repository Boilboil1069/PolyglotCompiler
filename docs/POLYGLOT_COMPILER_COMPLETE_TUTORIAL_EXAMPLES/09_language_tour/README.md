# 09 Poly 语言导览 / Poly language tour

状态 / Status: `FRONTEND`

这个示例把教材第 6–13 章的核心语法放在一个文件中，目的是验证词法、类型、
声明、默认参数与命名参数、`STRUCT`、`OPTION`、赋值、循环、短路逻辑、模式匹配、
文档注释与 `PRINTLN` 字面量都能通过当前的词法分析、语法分析和语义分析阶段。

This example combines the Chapters 6–13 language fundamentals in one source
file. It validates the current lexer, parser, and Sema paths for types,
declarations, defaults and named arguments, structs, options, assignment,
loops, short-circuit logic, patterns, docs, and literal output.

`keep_positive` 与 `classify` 接收一个已经存在的 `OPTION<i32>`，用来验证解包、
模式与守卫语法。当前语义分析器尚未把值位置的 `Some(value)` 与 `None` 注册为
内建构造器，所以示例不会在 `main` 中自行构造可选值；这个实现边界在教材
第 10 章中有专门说明。

`keep_positive` and `classify` accept an existing `OPTION<i32>` so the example
can verify unwrapping, patterns, and guards. The current Sema does not yet
register value-position `Some(value)` / `None` as built-in constructors, so
`main` does not construct an Option. Chapter 10 documents this implementation
boundary explicitly.

## 为什么标为“仅前端” / Why it is frontend-only

当前的本机标准输出合成路径会恢复静态 `PRINTLN` 调用点，但不能证明
`WHILE` 的迭代次数或 `MATCH` 分支的互斥性。因此，这里只把 `polyc --check`
产生的空诊断作为纳入版本控制的契约；控制流的目标语义记录在下面，但不冒充
当前后端的端到端证据。

The current native stdout synthesis recovers static print sites without proving
loop counts or exclusive match execution. The checked contract is therefore an
empty frontend diagnostic set, while the semantic target below is explicitly
not presented as current backend evidence.

## 验证 / Verification

```sh
build/polyc --check \
  docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/09_language_tour/main.poly
```

Expected stable payload / 预期稳定结果：

```json
{"uri":"file://<SOURCE>/09_language_tour/main.poly","diagnostics":[]}
```

按语言语义，只有以下两行应被执行：

By language semantics, only these two lines should execute:

```text
reading accepted
clamp matched
```

逐项解释见完整教材第 6–13 章；本文件用于复制、修改和回归验证，不替代正文的
设计说明。

See Chapters 6–13 for the purpose, grammar, anatomy, implementation layers, and
failure modes of every construct used here.
