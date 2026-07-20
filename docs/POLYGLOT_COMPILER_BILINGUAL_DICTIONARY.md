# PolyglotCompiler 双语术语词典 / PolyglotCompiler Bilingual Terminology Dictionary

> 词典版本 / Dictionary version: 1.0.0<br>
> 适用教材 / Applies to: `POLYGLOT_COMPILER_COMPLETE_TUTORIAL.md`<br>
> 状态 / Status: normative editorial source of truth

## 1. 使用规则 / Usage rules

本词典是完整教材的翻译与编辑依据。正文中的中文和英文必须分别自然成句，不能把两种语言的普通词汇交错拼在同一句中。

This dictionary is the translation and editorial source of truth for the complete textbook. Chinese and English prose must each form natural, self-contained sentences; ordinary words from the two languages must not be interleaved within one sentence.

1. 中文段落使用“中文定稿”列中的术语；英文段落使用“English”列中的术语。
2. 源码关键字、类型名、函数名、命令、参数、文件名、诊断码和 ABI 符号保持原始拼写，并使用反引号，例如 `MATCH`、`OPTION<T>`、`PloySema::AnalyzeExpression`、`--strict`。
3. 产品名和语言名保持原名，例如 PolyglotCompiler、Ploy、C++、Rust、Python、Java、.NET、JavaScript、Qt。
4. 缩写首次出现时写成“中文全称（缩写）”，后续可只写缩写，例如“应用二进制接口（ABI）”。源码标识符中的缩写不展开。
5. 中文正文不得直接使用 parser、sema、lowering、runtime、backend、binding、warning、shape 等普通英文术语；若专指源码类或函数，必须放入反引号。
6. 每个中文说明段落后紧跟语义等价的英文段落。英文不能只概括中文的一部分，也不能增加中文没有的承诺。
7. 表格若同时面向两种语言，优先拆成中文表和英文表；若字段是机器协议或源码名称，可保留一张表，但自然语言单元格必须完整双语或由相邻段落逐项翻译。
8. 代码、命令、JSON、IR、终端输出和图只保留一份；其前后的解释必须成对出现。
9. “当前实现”“目标语义”“历史兼容形式”和“规划能力”必须使用相同状态词，不得在翻译时弱化限制。

1. Chinese prose uses the canonical Chinese term; English prose uses the canonical English term.
2. Source keywords, type names, functions, commands, options, filenames, diagnostic codes, and ABI symbols keep their exact spelling and use backticks.
3. Product and language names remain unchanged.
4. Introduce an acronym with its full Chinese name on first use; source identifiers are exempt.
5. Do not insert ordinary English words such as parser, lowering, runtime, binding, or warning into Chinese prose. Use backticks only when referring to an exact source identifier.
6. Every Chinese explanatory paragraph is followed by a semantically equivalent English paragraph.
7. Prefer separate Chinese and English tables when cells contain prose. Machine fields may remain in one shared table.
8. Code, commands, JSON, IR, terminal output, and diagrams appear once, with paired explanations around them.
9. Translation must preserve implementation-status distinctions exactly.

## 2. 编译器阶段 / Compiler stages

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 词法分析器 | lexer | lexer（源码类名除外） |
| 词法分析 | lexical analysis / tokenisation | lexing、tokenize |
| 词法单元 | token | token |
| 语法分析器 | parser | parser |
| 语法分析 | parsing | parse、parsing |
| 语义分析器 | semantic analyser | sema、semantic checker |
| 语义分析 | semantic analysis | semantic checking |
| 前端 | frontend | Frontend（产品或类名除外） |
| 中端 | middle end | Middle |
| IR 降低 | IR lowering | lowering |
| 降低器 | lowerer | lowerer |
| 后端 | backend | Backend（类名除外） |
| 代码生成 | code generation | codegen |
| 链接器 | linker | linker |
| 链接 | linking | link（命令/关键字除外） |
| 运行时系统 | runtime system | Runtime（组件名除外） |
| 桥接层 | bridge | Bridge（组件名除外） |
| 适配器 | adapter | adapter |
| 验证器 | verifier | verifier |
| 优化器 | optimiser | optimizer / optimiser |
| 驱动程序 | driver | driver |
| 工具链 | toolchain | toolchain |

## 3. 语法、类型与控制流 / Syntax, types, and control flow

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 抽象语法树（AST） | abstract syntax tree (AST) | AST 可在首次展开后使用 |
| 语法规则 | grammar rule | grammar |
| 规范形式 | canonical form | canonical form |
| 源码拼写 | source spelling | spelling |
| 标识符 | identifier | identifier |
| 关键字 | keyword | keyword |
| 字面量 | literal | literal |
| 表达式 | expression | expression |
| 语句 | statement | statement |
| 声明 | declaration | declaration |
| 函数 | function | function（源码签名除外） |
| 参数 | parameter | parameter |
| 实参 | argument | argument |
| 返回值 | return value | return value |
| 绑定 | binding | binding |
| 作用域 | scope | scope |
| 符号 | symbol | symbol（符号名除外） |
| 符号表 | symbol table | symbol table |
| 类型 | type | type（类型名除外） |
| 类型化 / 带类型的 | typed | typed |
| 类型推导 | type inference | inference |
| 类型兼容性 | type compatibility | compatibility |
| 类型参数 | type parameter | type parameter |
| 泛型 | generic | generic |
| 约束 | bound / constraint | bound（语法名除外） |
| 模式 | pattern | pattern |
| 模式匹配 | pattern matching | pattern matching |
| 守卫条件 | guard | guard |
| 匹配分支 | match arm | arm |
| 穷尽性 | exhaustiveness | exhaustiveness |
| 不可达性 | unreachability | reachability（反义语境除外） |
| 控制流图（CFG） | control-flow graph (CFG) | CFG 可在首次展开后使用 |
| 基本块 | basic block | block（源码块除外） |
| 合并块 | merge block | merge block |
| 前驱 | predecessor | predecessor |
| 后继 | successor | successor |
| 终结指令 | terminator | terminator |
| 短路求值 | short-circuit evaluation | short circuit |
| 真值判定 | truthiness test | truthiness |
| 结构形状 | structural shape | shape |
| 不透明类型 | opaque type | opaque type |
| 调用分派 | call dispatch | dispatch system |

## 4. IR、目标代码与链接 / IR, target code, and linking

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 中间表示（IR） | intermediate representation (IR) | IR 可在首次展开后使用 |
| 静态单赋值形式（SSA） | static single-assignment form (SSA) | SSA 可在首次展开后使用 |
| Phi 节点 | phi node | Phi node |
| 指令 | instruction | instruction |
| 操作数 | operand | operand |
| 目标 | target | target（参数名除外） |
| 目标三元组 | target triple | target triple |
| 机器中间表示 | machine IR | MachineIR（类型名除外） |
| 对象文件 | object file | object |
| 可执行文件 | executable | executable |
| 容器格式 | container format | container |
| 符号解析 | symbol resolution | symbol resolution |
| 重定位 | relocation | relocation |
| 调用约定 | calling convention | calling convention |
| 数据布局 | data layout | layout |
| 对齐 | alignment | alignment |
| 位宽 | bit width | width |
| 有符号 / 无符号 | signed / unsigned | signed / unsigned |
| 入口点 | entry point | entry |
| 辅助产物 | sidecar artifact | sidecar |
| 产物 | artifact | artifact |

## 5. 跨语言、ABI 与生命周期 / Cross-language, ABI, and lifetime

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 应用二进制接口（ABI） | application binary interface (ABI) | ABI 可在首次展开后使用 |
| 应用程序接口（API） | application programming interface (API) | API 可在首次展开后使用 |
| 外部函数接口（FFI） | foreign function interface (FFI) | FFI 可在首次展开后使用 |
| 宿主语言 | host language | host language |
| 宿主对象 | host object | host object |
| 外部对象 | foreign object | foreign object |
| 跨语言调用 | cross-language call | foreign call |
| 调用描述符 | call descriptor | descriptor |
| 类型描述符 | type descriptor | descriptor |
| 编组 | marshalling | marshal / marshalling |
| 解组 | unmarshalling | unmarshal / unmarshalling |
| 转换器 | converter | converter |
| 句柄 | handle | handle（类型名除外） |
| 指针 | pointer | pointer（类型名除外） |
| 所有权 | ownership | ownership |
| 生命周期 | lifetime | lifetime |
| 借用值 | borrowed value | borrowed value |
| 自有值 | owned value | owned value |
| 保留 | retain | retain（函数名除外） |
| 释放 | release | release（函数名除外） |
| 清理 | cleanup | cleanup |
| 清理边 | cleanup edge | cleanup edge |
| 回滚 | rollback | rollback |
| 引用计数 | reference counting | refcount |
| 垃圾回收（GC） | garbage collection (GC) | GC 可在首次展开后使用 |
| 垃圾回收根 | GC root | GC root |
| 线程亲和性 | thread affinity | thread affinity |
| 运行时附着 | runtime attachment | runtime attach |

## 6. 错误、异常与异步 / Errors, exceptions, and async

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 诊断 | diagnostic | diagnostic / diagnostics |
| 错误 | error | error（类型名、诊断码除外） |
| 警告 | warning | warning |
| 严重级别 | severity | severity |
| 严格模式 | strict mode | strict mode |
| 宽松模式 | permissive mode | permissive mode |
| 回退 | fallback | fallback |
| 降级桩 | degraded stub | degraded stub |
| 异常 | exception | exception |
| 异常拦截 | exception interception | exception interception |
| 异常处理（EH） | exception handling (EH) | EH 可在首次展开后使用 |
| 异常着陆块 | landing pad | landing pad |
| 抛出 / 重新抛出 | throw / rethrow | throw / rethrow（关键字除外） |
| 正常贯穿 | normal fallthrough | normal fallthrough |
| 提前返回 | early return | early return |
| 异步函数 | async function | async function（关键字除外） |
| 等待 | await | await（关键字除外） |
| 协作式调度 | cooperative scheduling | cooperative scheduling |
| 挂起 / 恢复 | suspend / resume | suspend / resume |
| 取消 | cancellation | cancel / cancellation |
| 未来值 | future value | Future（类型名除外） |

## 7. 模块、配置与版本 / Modules, configuration, and versions

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 模块 | module | module（模块名除外） |
| 包 | package | package（包名除外） |
| 导入 | import | import（关键字除外） |
| 导出 | export | export（关键字除外） |
| 别名 | alias | alias |
| 命名空间 | namespace | namespace |
| 路径 | path | path（实际路径除外） |
| 发现 | discovery | discovery |
| 包清单 | package inventory | inventory |
| 注册表 | registry | registry |
| 配置 | configuration | config（关键字除外） |
| 设置 | settings | settings |
| 工作区 | workspace | workspace |
| 版本约束 | version constraint | version constraint |
| 版本固定 | version pin | pin |
| 锁定文件 | lock file | lock file |
| 来源证明 | provenance | provenance |
| 兼容性 | compatibility | compatibility |
| 弃用 | deprecation | deprecated / deprecation |
| 迁移 | migration | migration |

## 8. 工具、测试与文档 / Tooling, tests, and documentation

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 命令行界面（CLI） | command-line interface (CLI) | CLI 可在首次展开后使用 |
| 集成开发环境（IDE） | integrated development environment (IDE) | IDE 可在首次展开后使用 |
| 语言服务器协议（LSP） | Language Server Protocol (LSP) | LSP 可在首次展开后使用 |
| 用户界面（UI） | user interface (UI) | UI 可在首次展开后使用 |
| 测试 | test | test / tests |
| 单元测试 | unit test | unit test |
| 集成测试 | integration test | integration test |
| 端到端测试 | end-to-end test | E2E（首次需展开） |
| 回归测试 | regression test | regression test |
| 冒烟测试 | smoke test | smoke test |
| 测试夹具 | test fixture | fixture |
| 断言 | assertion | assertion |
| 质量门禁 | quality gate | gate |
| 持续集成（CI） | continuous integration (CI) | CI 可在首次展开后使用 |
| 构建 | build | build（目录名或命令除外） |
| 发布 | release | release（版本名除外） |
| 候选发布版 | release candidate | release candidate |
| 快照 | snapshot | snapshot |
| 样例 | sample / example | sample（目录名除外） |
| 示例 | example | example |
| 预期结果 | expected result | expected result |
| 标准输出 | standard output | stdout（协议名除外） |
| 标准错误 | standard error | stderr（协议名除外） |
| 退出码 | exit code | exit code |
| 数据模式 | schema | schema（标识符除外） |
| 有效载荷 | payload | payload |
| 元数据 | metadata | metadata |
| 追溯矩阵 | traceability matrix | traceability matrix |
| 源文档 | source document | source document |
| 文档审查 | documentation review | review |

## 9. 性能、图与插件 / Performance, graphs, and plugins

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 性能剖析 | profiling | profile / profiling（命令名除外） |
| 性能数据 | profile data | profile data |
| 采样 | sampling | sampling |
| 自身时间 | self time | self time |
| 包含时间 | inclusive time | inclusive time |
| 调用图 | call graph | call graph |
| 节点 | node | node |
| 边 | edge | edge |
| 拓扑图 | topology graph | topology |
| 管线 | pipeline | pipeline（关键字除外） |
| 阶段 | stage | stage（关键字除外） |
| 插件 | plugin | plugin（产品名除外） |
| 扩展 | extension | extension（产品名除外） |
| 能力 | capability | capability |
| 清单文件 | manifest | manifest |
| 权限 | permission | permission |
| 会话 | session | session |
| 生产者 | producer | producer |
| 消费者 | consumer | consumer |

## 10. 实现、数据结构与协议 / Implementation, data structures, and protocols

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 构建器 | builder | builder |
| 访问器 | visitor | visitor |
| 读取器 / 写入器 | reader / writer | reader / writer |
| 序列化器 | serializer | serializer |
| 编解码器 | codec | codec |
| 渲染器 | renderer | renderer |
| 处理器 | handler | handler |
| 执行器 | runner | runner |
| 管理器 | manager | manager |
| 提供者 | provider | provider |
| 服务 | service | service |
| 存储 | store / storage | store / storage |
| 池 | pool | pool（类型名除外） |
| 分配区 | arena | arena（类型名除外） |
| 双端队列 | deque | deque（类型名除外） |
| 映射 | map / mapping | map / mapping（类型名除外） |
| 数组 / 标量 | array / scalar | array / scalar |
| 字段 / 属性 | field / property | field / property（标识符除外） |
| 读取函数 | getter | getter |
| 控件 | widget | widget |
| 模型 | model | model |
| 回调 | callback | callback |
| 钩子 | hook | hook |
| 线协议 | wire protocol | wire protocol |
| 传输层 | transport | transport |
| 数据帧格式 | framing | framing |
| 基线 | baseline | baseline |
| 框架 | framework | framework |
| 模板 | template | template（类型名除外） |
| 辅助函数 | helper | helper |
| 不变量 | invariant | invariant |
| 确定性 | determinism | deterministic（普通形容词） |
| 可重入 | reentrant | reentrant |
| 线程安全 | thread-safe | thread-safe |
| 线程局部 | thread-local | thread-local |
| 只读不改 | non-destructive | non-destructive |
| 深度合并 | deep merge | deep merge |
| 往返转换 | round trip | round trip |
| 冲突 | collision / conflict | collision / conflict |
| 检测 | detection | detection |
| 发现 | discovery | discovery |

## 11. 算法、优化与机器代码 / Algorithms, optimisation, and machine code

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 操作码 | opcode | opcode（枚举名除外） |
| 操作数 | operand | operand |
| 活跃性 / 活跃区间 | liveness / live interval | liveness / live interval |
| 线性扫描 | linear scan | linear scan |
| 图着色 | graph colouring | graph colouring |
| 调度 | scheduling | scheduling |
| 发射 / 输出 | emission | emission |
| 直接调用 / 间接调用 | direct / indirect call | direct / indirect |
| 使用—定义关系 | use-def relation | use-def |
| 可观察行为 | observable behaviour | observable |
| 副作用 | side effect | side effect |
| 变换前 / 变换后 | before / after transformation | before / after |
| 幂等性 | idempotence | idempotence |
| 不动点 | fixed point | fixed point |
| 失效规则 | invalidation rule | invalidation |
| 无损转换 | lossless conversion | lossless |
| 扩宽转换 | widening conversion | widening |
| 位转换 | bit cast | bitcast（指令名除外） |
| 有符号性 | signedness | signedness |
| 装箱 / 拆箱 | boxing / unboxing | box / boxed / unbox |
| 连续存储 | contiguous storage | contiguous |
| 字节步幅 | byte stride | stride |
| 溢出区 | spill area | spill |
| 被调用方保存 | callee-saved | callee-saved |
| 调用方保存 | caller-saved | caller-saved |
| 影子空间 | shadow space | shadow space |
| 热点路径 | hot path | hot path |
| 冷路径 | cold path | cold path |
| 阈值 | threshold | threshold |
| 提前终止 | early termination | early termination |
| 无用代码剥离 | dead stripping | dead stripping |

## 12. 工程状态与编辑用语 / Engineering status and editorial wording

| 中文定稿 | English | 中文正文中避免 / Avoid in Chinese prose |
|---|---|---|
| 标识 | identifier / id | id（字段名除外） |
| 特性 | feature | feature |
| 平台 | platform | platform |
| 精确 / 完全一致 | exact | exact |
| 隐式 / 显式 | implicit / explicit | implicit / explicit |
| 必需 / 可选 | mandatory / optional | mandatory / optional |
| 致命 | fatal | fatal |
| 成功 / 失败 | success / failure | success / failure |
| 空值 | null value | null（关键字除外） |
| 根目录 | root directory | root（路径或 GC 根除外） |
| 提交 | commit | commit（散列值除外） |
| 日期 | date | date |
| 报告 | report | report（文件名除外） |
| 检查 | check / lint | check / lint（命令名除外） |
| 跳过 | skip | skip（状态名或参数除外） |
| 跳过原因 | skip reason | skip reason |
| 快速入门 | quickstart | quickstart |
| 工作流 | workflow | workflow |
| 线路接入 | wiring | wiring |
| 当前主版本 / 次版本 | current major / minor version | major / minor |
| 保守默认值 | conservative default | conservative default |
| 尽力而为 | best effort | best-effort |
| 一对 / 成对关系 | pair | pair |
| 每调用 / 每线程 / 每帧 | per-call / per-thread / per-frame | per-call / per-thread / per-frame |
| 跨语言 / 跨模块 | cross-language / cross-module | cross-language / cross-module |

## 13. 允许保留的原始拼写 / Allowed exact spellings

下列内容可以出现在中文段落中，但必须属于专有名称、缩写或反引号代码，而不能作为未翻译的普通英文散词：

The following spellings may appear in Chinese prose only as proper names, established acronyms, or backticked source text—not as untranslated ordinary words:

- 产品与组件：PolyglotCompiler、Polyglot、Poly、Ploy、PolyUI、CodeMirror、Typora；
- 语言与平台：C、C++、Rust、Python、Java、.NET、JavaScript、TypeScript、Go、Ruby、Lua、Qt、Windows、Linux、macOS、Apple、Unix；
- 标准缩写：API、ABI、FFI、IR、AST、SSA、CFG、CLI、IDE、LSP、UI、GC、EH、PGO、LTO、JIT、AOT、JSON、NDJSON、UTF-8、CRLF、JS、TS、VM、OS、Hz、RAII、LIFO；
- 文件、对象与图像格式：ELF、Mach-O、COFF、PE、Wasm、WAT、DWARF、PDB、CSV、PNG、JPEG、WebP、GIF、SVG、BMP；
- 协议、模块制式与样式标准：HTTP、MIME、CSS、CommonMark、GFM、ESM、CommonJS、RFC；
- 工具与生态系统名称：CMake、Ninja、LLVM、GitHub、Bash、PowerShell、clang-format、clang-tidy、clangd、pyright、jdtls、OmniSharp、Cargo、Maven、Gradle、NuGet、Bundler、gem、npm、pip、Conda、PostgreSQL、MySQL、gccgo、CPython、QProcess、QSettings、GCC、Graphviz、tree-sitter、CRuby、CoreCLR、CLR、NumPy；
- 规范化缩写与源码标记：POBJ、BSS、CIE、FDE、GOT、PLT、PRE、ThinLTO、SCCP、ASan、UBSan、LSan、DDL、DML、TODO、FIXME、cgo、UEDGE、SQL、CodeView、GiB、KiB、RGBA、ECMAScript、HTML、DLL、ICU、DOT、Kahn、RPATH、RUNNABLE；
- 源码与命令：所有反引号包围的关键字、标识符、路径、命令、参数、符号、类型和诊断码。

- Products and components: PolyglotCompiler, Polyglot, Poly, Ploy, PolyUI, CodeMirror, and Typora;
- languages and platforms: C, C++, Rust, Python, Java, .NET, JavaScript, TypeScript, Go, Ruby, Lua, Qt, Windows, Linux, macOS, Apple, and Unix;
- standard abbreviations: API, ABI, FFI, IR, AST, SSA, CFG, CLI, IDE, LSP, UI, GC, EH, PGO, LTO, JIT, AOT, JSON, NDJSON, UTF-8, CRLF, JS, TS, VM, OS, Hz, RAII, and LIFO;
- file, object, and image formats: ELF, Mach-O, COFF, PE, Wasm, WAT, DWARF, PDB, CSV, PNG, JPEG, WebP, GIF, SVG, and BMP;
- protocols, module systems, and style standards: HTTP, MIME, CSS, CommonMark, GFM, ESM, CommonJS, and RFC;
- tool and ecosystem names: CMake, Ninja, LLVM, GitHub, Bash, PowerShell, clang-format, clang-tidy, clangd, pyright, jdtls, OmniSharp, Cargo, Maven, Gradle, NuGet, Bundler, gem, npm, pip, Conda, PostgreSQL, MySQL, gccgo, CPython, QProcess, QSettings, GCC, Graphviz, tree-sitter, CRuby, CoreCLR, CLR, and NumPy;
- canonical abbreviations and source markers: POBJ, BSS, CIE, FDE, GOT, PLT, PRE, ThinLTO, SCCP, ASan, UBSan, LSan, DDL, DML, TODO, FIXME, cgo, UEDGE, SQL, CodeView, GiB, KiB, RGBA, ECMAScript, HTML, DLL, ICU, DOT, Kahn, RPATH, and RUNNABLE;
- source and command text: any keyword, identifier, path, command, option, symbol, type, or diagnostic code enclosed in backticks.

## 14. 状态词 / Implementation-status vocabulary

| 中文定稿 | English | 定义 / Definition |
|---|---|---|
| 完整 | complete | 源码、测试与可执行路径共同证明 / source, tests, and executable path agree |
| 分层 | layered | 部分层已完成，其他层仍依赖适配器或运行时 / some layers are complete while others remain adapter- or runtime-dependent |
| 实验 | experimental | 可使用，但兼容性或覆盖范围仍可能变化 / usable with evolving compatibility or coverage |
| 规划 | planned | 仅有规范或路线图，不能作为当前实现 / specified or planned but not a current implementation |
| 目标语义 | intended semantics | 语言合同要求，但当前端到端实现尚未完全证明 / required by the language contract but not yet proven end to end |
| 当前事实 | current fact | 已由当前源码、测试或实测产物证明 / proven by current source, tests, or observed artifacts |
| 历史兼容形式 | legacy compatibility form | 为旧源码保留，通常伴随弃用诊断 / retained for old source, usually with a deprecation diagnostic |

## 15. 维护规则 / Maintenance

新增术语时必须先更新本词典，再修改教材；同一概念不得在正文中临时创造第二种译法。附录 I 只保留常用速查，完整定义以本文件为准。

Add a term here before using it in the textbook. Do not invent a second translation ad hoc. Appendix I remains a compact quick reference; this external file is normative.
