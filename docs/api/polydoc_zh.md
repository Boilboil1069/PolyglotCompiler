# `polydoc` — Ploy 文档注释抽取工具（自 v1.18.0 起）

`polydoc` 是一个小型命令行工具，遍历一个或多个 `.ploy` 源文件，
收集所有挂在顶层 `FUNC` / `STRUCT` / `LET` / `VAR` 声明上的 `///`
文档块，并以 Markdown 或 JSON 形式输出。

英文对照见 [`polydoc.md`](polydoc.md)。

## 用法

```text
polydoc [--json] [-o OUT] FILE [FILE ...]
```

| 选项        | 说明                                                     |
|-------------|----------------------------------------------------------|
| `--json`    | 输出 JSON 而非 Markdown                                  |
| `-o OUT`    | 将渲染结果写入 `OUT`（默认 stdout）                      |
| `-h / --help` | 显示用法并退出                                         |

成功退出码为 0；I/O 错误返回 1；参数错误返回 2。

## 文档注释表面语法

文档注释是恰好以**三个**斜杠开头的行：

```ploy
/// 第一行。
/// 第二行。
FUNC add(a: i32, b: i32) -> i32 { … }
```

普通 `//` 行注释和 `////`（四斜杠）横幅**不是**文档注释，会被
`polydoc` 忽略。词法器在存储前会去掉一个可选的前导空格与行尾 CR，
因此 `/// hello` 记录为 `hello`。

文档块挂到紧邻的下一条声明上。中间出现空行不影响；出现非注释
语句或其他非文档 token 会清空缓冲区。

`polydoc` 遍历已解析的模块，按源码顺序收集条目。

## Markdown 输出

```sh
polydoc src/api.ploy
```

对上例输出：

```markdown
# src/api.ploy

## `FUNC add(a: I32, b: I32) -> I32`

第一行。
第二行。
```

签名行由 AST 合成，内置原始类型采用规范大写形式。

## JSON 输出

```sh
polydoc --json src/api.ploy
```

输出单个对象：

```json
{
  "file": "src/api.ploy",
  "entries": [
    {
      "kind": "func",
      "name": "add",
      "signature": "FUNC add(a: I32, b: I32) -> I32",
      "doc": ["第一行。", "第二行。"]
    }
  ]
}
```

`kind` 为 `func` / `struct` / `let` / `var` 之一；`signature` 为
单行人类可读摘要；`doc` 为原样的文档行列表（按源码顺序）。

## 退出码

| 码 | 含义                                                       |
|----|------------------------------------------------------------|
| 0  | 全部输入文件解析与输出均成功                               |
| 1  | 源文件无法读取或输出文件无法写入                           |
| 2  | 参数错误（无输入文件、`-o` 后缺少值）                      |

解析错误不会改变退出码；`polydoc` 会针对已成功解析的声明输出文档。
完整诊断请使用 `polyc`。

## 相关

- [`docs/realization/grammar_polish_zh.md`](../realization/grammar_polish_zh.md)
  —— v1.18.0 收尾包全文
- [`docs/specs/language_spec_zh.md`](../specs/language_spec_zh.md)
  文档注释章节
