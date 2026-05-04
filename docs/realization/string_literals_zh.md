# 实现说明 — 扩展字符串字面量（v1.17.0）

需求：`2026-04-28-17`。为 `.ploy` 源码新增原始（raw）、多行
（multiline）、模板（template）三类字符串字面量形式。

## 1. 表层语法

| 形式      | 语法                                  |
| --------- | ------------------------------------- |
| 普通      | `"hello\n"`                           |
| 原始      | `r"C:\path\no\escape"`                |
| 带填充原始| `r#"contains "quotes""#` / `r##"..."##` |
| 多行      | `"""line1\nline2"""`                  |
| 模板      | `f"x = {expr}"` / `f"""..."""`        |

仅当 `r` / `f` 之后紧跟 `"`（raw 还允许 `#`）时才识别为字符串字面量
前缀；`result`、`foo` 等普通标识符仍按既有语义处理。

## 2. 词法器

- `frontends/ploy/src/lexer/lexer.cpp` 新增辅助方法
  `LexRawString`、`LexMultilineString`、`LexTemplateString`；
  `LexString` 自身吸收三引号情形，保持「一字符串一 token」契约。
- 四种扩展形式均产出 `kString` token。raw 与多行字面量的内容**会**被
  重新编码成规范的 `"..."` 形式（换行 → `\n`、内嵌 `"` → `\"` 等），
  以便既有的转义解码管线（`DecodePrintlnLiteral` 及其同类）无需修改即可
  继续工作。
- 模板字面量在输出 lexeme 中保留 `f"..."` 前缀（多行时为
  `f"""..."""`），以便解析器按首字符分派。

## 3. AST

- 新增节点 `polyglot::ploy::TemplateString`
  （`frontends/ploy/include/ploy_ast.h`）。
- 每个 `TemplateString` 持有 `parts: vector<Part>`；每个 `Part` 要么
  是字面文本片段，要么是一个被插值的 `Expression`。

## 4. 解析器

- `BuildStringExpression(lexeme, loc)`
  （`frontends/ploy/src/parser/parser.cpp`）集中处理字符串字面量。当
  lexeme 以 `f"` 开头时，截取主体并按字符遍历，在 `{` / `}` 上切分
  （支持 `{{` / `}}` 字面大括号转义），并启用一对全新的
  `PloyLexer` + `PloyParser` 实例对每个插值表达式做子解析。
- `}` 缺失等诊断通过外层解析器的 diagnostics sink 传递。

## 5. Sema

- `PloySema::AnalyzeExpression` 增加 `TemplateString` 分支。
- 每个插值表达式必须具备**可格式化**类型 —— `Int`、`Float`、
  `String` 或 `Bool`（同时接受 `Any` / `Unknown`，以避免未解析的
  跨语言引用阻塞编译）。
- 整体表达式类型恒为 `String`。

## 6. 下沉（MVP 范围）

- `PloyLowering::LowerExpression` 增加 `TemplateString` 分支。
- 当所有插值表达式均为常量 `Literal` 时，下沉层在编译期完成字符串拼
  接，并通过 `IRBuilder::MakeStringLiteral` 发出一个驻留的全局常量 ——
  与普通字符串字面量走同一条路径。
- 一旦模板引用运行时变量，下沉层只拼接字面文本片段，并发出
  `kGenericWarning` 警告；运行时格式化辅助函数列入后续工作。

## 7. 跨语言传输

- 模板插值仅在 `.ploy` 侧展开；宿主语言收到的是已格式化的普通字符串，
  全部既有 marshalling / NUL 终止转换路径不需改动。
- 原始与多行字面量便于内嵌 SQL / JSON / 跨语言代码片段
  （见 `tests/samples/22_database_access`、`20_json_pipeline`）。

## 8. 未来工作

- 引入运行时辅助函数（如 `polyrt_format_to_string(...)`），让模板
  字符串可在不损失 IR 保真度的前提下插入运行时值；下沉层警告随之
  下线。
- 支持**缩进感知多行**：
  `"""\n  line one\n  line two\n"""` —— 去除每行公共前导空白以及
  首尾多余换行。
- 支持模板大括号内的**格式说明**后缀（`{value:.3f}`）。
- 允许在插值段内嵌套模板字符串。
