# PolyglotCompiler 教材配套示例 / Textbook Companion Examples

本目录与 `../POLYGLOT_COMPILER_COMPLETE_TUTORIAL.md` 配套。每个示例都包含源码、状态说明和可核对的输出；教材中的章节链接会指向这里。

This directory accompanies `../POLYGLOT_COMPILER_COMPLETE_TUTORIAL.md`. Every example contains source, a status note, and an observable result.

## 状态 / Status

| 标签 / Label | 含义 / Meaning |
|---|---|
| `RUNNABLE` | 当前工具链可执行；输出是否符合语言语义由各示例分别说明 / executable now; each example states whether output matches the language semantics |
| `FRONTEND` | 当前验证范围是 lexer/parser/Sema；不宣称 Runtime 完整 / frontend validation only |
| `LAYERED` | 源码展示真实边界，但执行需要宿主 SDK/Bridge / requires host SDKs or a bridge |
| `FIXTURE` | 输入或输出契约样本，不是独立程序 / data-contract fixture, not a standalone program |
| `BUILDABLE` | 可独立编译的组件，但需要 host 才能观察完整生命周期 / independently buildable component |

## 目录 / Index

| 目录 | 章节 | 状态 | 主要结果 |
|---|---:|---|---|
| `00_hello` | 4 | `RUNNABLE` | verified stdout: `hello from Poly` and exit 0 |
| `01_control_flow` | 8–10 | `RUNNABLE` | current backend output and semantic target output are both recorded |
| `02_errors_async` | 12, 19 | `FRONTEND` | frontend exits 0 with two warnings; intended Runtime result documented separately |
| `03_cpp_bridge` | 5, 15–17 | `LAYERED` | stdout: `rows=3` after the bridge is linked |
| `04_polydoc` | 13, 25 | `RUNNABLE` | Markdown and JSON API documents |
| `05_topology` | 14, 25 | `RUNNABLE` | topology summary/JSON |
| `06_diagnostics` | 4, 28 | `RUNNABLE` negative case | non-zero check plus structured diagnostic |
| `07_profile_fixture` | 24, 29, 37 | `FIXTURE` | current nested profile JSON and NDJSON |
| `08_plugin` | 39, 41 | `RUNNABLE` | shared library exports plus a verified host lifecycle and activation log |
| `09_language_tour` | 6–13 | `FRONTEND` | one-file Poly language tour with empty frontend diagnostics |

## 快速验证 / Quick verification

在仓库根目录运行：

```sh
bash docs/POLYGLOT_COMPILER_COMPLETE_TUTORIAL_EXAMPLES/run_verified.sh
```

Runner 只把临时产物写入 `build/tutorial-examples/`，并为编译命令传入 `--no-aux`。它不会修改 Tutorial、Spec、API 或示例源码；缺少某个已构建工具或当前 host 不在可执行矩阵内时会明确跳过相应步骤。

The runner writes temporary artifacts only under `build/tutorial-examples/` and passes `--no-aux` to compilation commands. It does not modify documentation or example source, and reports missing tools or an unsupported executable host as skips.

## 输出约定 / Output convention

- `expected_stdout.txt`：成功执行时的 byte-oriented stdout；末尾通常含 newline。
- `expected_result.md`：不能只用 stdout 表达的 exit code、diagnostic、artifact 或分层限制。
- `expected_*.json`：稳定字段示例；绝对路径、timestamp 等机器相关字段用说明排除。

- `expected_stdout.txt` is byte-oriented standard output for a successful run.
- `expected_result.md` records exit status, diagnostics, artifacts, or layered limitations.
- `expected_*.json` records stable fields and documents machine-dependent exclusions.

所有 checked-in 输出均来自本仓库当前构建的工具。包含 `<SOURCE>` 的文件是把机器相关绝对路径正规化后的真实输出，不是虚构字段。

All checked-in outputs were obtained from the tools currently built in this repository. Files containing `<SOURCE>` are real outputs with machine-specific absolute paths normalised, not invented payloads.
