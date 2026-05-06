# 00_minimal — print_then_exit

整个工具链最小的端到端冒烟样例。源文件 `print_then_exit.ploy` 中声明
了一个 `main` 函数：先打印一行 `ok`，再返回退出码 `0`。

本样例同时被 macOS Mach-O 的 execve 冒烟测试
（`tests/integration/macho_exec_smoke_test.cpp`）、Linux ELF 的 execve
冒烟测试，以及 Windows PE-7 的 PRINTLN 冒烟测试复用；所有受支持的目标
平台都应当能产出退出码为 `0` 且向标准输出写入 `ok\n` 的产物。

## 构建与运行

```sh
polyc print_then_exit.ploy -o /tmp/print_then_exit
/tmp/print_then_exit
echo "exit=$?"
```

预期控制台输出：

```
ok
```

预期退出状态：`0`。

## 文件清单

| 文件 | 作用 |
|------|------|
| `print_then_exit.ploy` | 单一函数的源文件。 |
| `expected_output.txt`  | 真实运行得到的单行 `ok`，由样例回归框架读取比对。 |
| `README.md` / `README_zh.md` | 中英双语文档。 |
