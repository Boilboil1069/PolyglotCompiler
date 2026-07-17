# 00 Hello — Chapter 4 — `RUNNABLE`

这是教材第 4 章的最小程序。`PRINTLN` 不会自动添加换行，所以字符串中显式包含 `\n`。

This is the minimal Chapter 4 program. `PRINTLN` does not append a newline, so the literal contains `\n` explicitly.

```sh
build/polyc --check docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/00_hello/main.ploy
build/polyc docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/00_hello/main.ploy \
  --strict --no-aux \
  --emit-ir=build/tutorial-examples/hello.ir \
  --emit-asm=build/tutorial-examples/hello.s \
  --emit-obj=build/tutorial-examples/hello.o \
  -o build/tutorial-examples/hello
build/tutorial-examples/hello
```

这条命令同时保留 IR、汇编和对象文件，并由 staged pipeline 调用链接器生成 `-o` 指定的可执行文件。`--emit-obj` 单独使用且没有 `-o` 时仍是历史兼容的仅编译简写；显式 `-c` 或 `--mode=compile` 也始终只编译。

This command retains IR, assembly, and object sidecars while the staged pipeline invokes the linker to create the executable named by `-o`. `--emit-obj` without `-o` remains the historical compile-only shorthand; explicit `-c` or `--mode=compile` also always stops after compilation.

实测 stdout 见 `expected_stdout.txt`；正常退出码为 0。

Verified stdout is in `expected_stdout.txt`; the successful exit code is 0.
