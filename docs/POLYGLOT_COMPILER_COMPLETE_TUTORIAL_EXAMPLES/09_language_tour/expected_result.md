# Expected result / 预期结果

Frontend command / 前端检查命令：

```sh
build/polyc --check \
  docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/09_language_tour/main.poly
```

Stable normalised payload / 稳定的正规化结果：

```json
{"uri":"file://<SOURCE>/09_language_tour/main.poly","diagnostics":[]}
```

The command exits with status `0`. `<SOURCE>` replaces the machine-dependent
absolute repository path.

命令退出码为 `0`；`<SOURCE>` 代替每台机器不同的仓库绝对路径。

If control flow is executed according to the language semantics, the two
selected literal-output sites are:

按语言语义执行控制流时，只会选择以下两个字面量输出位置：

```text
reading accepted
clamp matched
```

This semantic stdout is not presented as current backend proof: the present
native stdout synthesis recovers static print sites without fully proving
branch or `MATCH` exclusivity. The empty frontend diagnostic set is the
checked-in executable contract for this example.

这里不把语义 stdout 冒充当前 Backend 证据：现有 native stdout 合成会恢复静态
print site，但不能完整证明分支或 `MATCH` 的互斥执行。本示例当前固定的可执行
契约是空前端诊断。
