# 04 polydoc — Chapters 13 and 25 — `RUNNABLE`

源码展示 `///` 如何附着到顶层函数和结构。运行 `polydoc` 后，stdout 是 Markdown 或单个 JSON object；正规化后的真实输出分别保存在 `expected_markdown.md` 和 `expected.json`，其中 `<SOURCE>` 只替代绝对路径。

The source shows triple-slash comments attached to top-level functions and structs. `polydoc` writes Markdown or one JSON object to stdout; normalised real outputs are stored in `expected_markdown.md` and `expected.json`, where `<SOURCE>` only replaces the absolute path.
