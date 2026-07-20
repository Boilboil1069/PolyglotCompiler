# 01 Control flow — Chapters 8–10 — `RUNNABLE`

示例把可变绑定、`WHILE` 和 `MATCH` 放入一个入口函数。它既是语法示例，也是当前 x86_64 control-flow lowering 的回归探针。

The example combines mutable bindings, `WHILE`, and `MATCH`. It is both a syntax example and a regression probe for the current x86_64 control-flow lowering.

```sh
build/polyc --check docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/01_control_flow/main.poly
build/polyc docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/01_control_flow/main.poly \
  --strict --no-aux \
  --emit-ir=build/tutorial-examples/control_flow.ir \
  --emit-asm=build/tutorial-examples/control_flow.s \
  --emit-obj=build/tutorial-examples/control_flow.o \
  -o build/tutorial-examples/control_flow
build/tutorial-examples/control_flow
```

`-o` 明确要求最终产物，因此三个 `--emit-*` 只写出可检查的旁路产物，不会把 pipeline 截停在 object 阶段。显式 `-c` 或 `--mode=compile` 才会覆盖链接模式。

Because `-o` explicitly requests a final product, the three `--emit-*` options write inspectable sidecars without stopping the pipeline at the object stage. Explicit `-c` or `--mode=compile` still overrides link mode.

当前 macOS x86_64 构建的**实测 stdout** 见 `expected_stdout.txt`：`polyld` 报告把四个恢复出的 `polyrt_println` call sites 合成进 Mach-O `__text`，结果每个静态 print site 各出现一次。这条合成 stdout 路径不能证明 `WHILE`/`MATCH` 的真实 CFG 语义；按语言语义应有的输出另存为 `expected_semantic_stdout.txt`。

The **observed stdout** from the current macOS x86_64 build is in `expected_stdout.txt`: `polyld` reports synthesising four recovered `polyrt_println` call sites into Mach-O `__text`, so every static print site appears once. This synthetic stdout path cannot prove the real `WHILE`/`MATCH` CFG semantics; the language-semantic output is in `expected_semantic_stdout.txt`.
