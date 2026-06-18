# PolyglotCompiler 设计规格

## 1. 文档控制

| 字段 | 值 |
|---|---|
| 文档类型 | Kiro spec 风格的仓库级设计文档 |
| 范围 | 整个 PolyglotCompiler 工作区 |
| 读者 | 需求分析、架构审查、实现规划、QA、发布负责人 |
| 源码基线 | 当前 `/Volumes/extend/PolyglotCompiler` 工作区 |
| 构建系统 | CMake 3.20+、C++20、CTest |
| 产品版本来源 | 根目录 `CMakeLists.txt`、生成的 `common/include/version.h`、`VERSION.txt` |
| 完成状态 | 本文档基于代码、构建文件、测试、样例、脚本和文档审查后的仓库设计规格 |

## 2. 产品定义

PolyglotCompiler 是一个自举式多语言编译器、链接器、运行时、语言服务器、IDE 与样例/测试套件。它通过第一方语言前端解析源码，将受支持语言降级到共享 SSA 风格 IR，执行中端优化，通过 x86_64、ARM64 或 WebAssembly 后端生成目标产物，并由项目自带链接器打包为最终可执行文件或库。

`.ploy` 是跨语言编排层，用于描述导入、包约束、虚拟环境配置、函数级链接、跨语言调用、类实例化、方法调用、属性访问、资源管理、类型映射、控制流、异步流程、错误处理和流水线导出。

### 2.1 产品目标

- 为 C++、Python、Rust、Java、.NET、JavaScript、Ruby、Go 和 `.ploy` 提供统一编译架构。
- 将源码解析和降级保留在第一方前端内，而不是把外部编译器作为主要实现路径。
- 在最终打包前校验跨语言签名、参数数量、类型、包约束、ABI 兼容性和运行时桥需求。
- 通过 x86_64、ARM64 和 WebAssembly 后端输出原生和可移植目标。
- 输出 ELF、PE32+、Mach-O、Wasm、静态库和动态库。
- 在 CLI、JSON/progress 输出、`polyls`、`polyui`、测试和文档示例中复用诊断。
- 所有扩展面显式化：前端注册表、后端注册表、插件 C ABI、运行时 C ABI、包管理器注册表、settings schema、LSP capability registry、UI panel service。

### 2.2 非目标

- `.ploy` 不是所有受支持语言的替代品；它是互操作和编排语言。
- `polyui` 不能绕开编译器服务，直接依赖 parser 内部实现来提供用户可见行为。
- 当必需编译产物缺失时，`polyld` 和后端阶段不能虚构 fallback symbol、合成 object section 或成功输出。
- 插件 API 不能把不稳定的 C++ 实现 ABI 暴露为宿主契约。

## 3. 工作区资产清单

### 3.1 顶层目录

| 路径 | 职责 | 主要内容 |
|---|---|---|
| `common/` | 编译器共享基础 | 类型、符号、源码位置、目标三元组、二进制容器、调试工具、插件 API 与管理器 |
| `middle/` | 共享 IR 与优化 | IR context、CFG、SSA、verifier、parser/printer、data layout、模板、优化 Pass、PGO、LTO |
| `frontends/` | 第一方语言前端 | 前端公共工具，以及 C++、Python、Rust、`.ploy`、Java、.NET、JavaScript、Ruby、Go |
| `backends/` | 代码生成 | 后端注册表、ABI 工具、object builder、debug emitter、x86_64、ARM64、Wasm |
| `runtime/` | 运行时服务与互操作 | GC、分配、FFI、marshalling、对象生命周期、语言运行时桥、异步/错误/反射/线程/profile 服务 |
| `tools/` | 用户工具和内部工具 | `polyc`、`polyld`、`polyasm`、`polyopt`、`polyrt`、`polyver`、`polydoc`、`polytopo`、`polyls`、`polybench`、`polyui` |
| `tests/` | 验证资产 | 单元测试、集成测试、基准、fixture、分类样例 |
| `docs/` | 项目文档 | API、spec、realization、tutorial、changelog、user guide、demand log、design |
| `scripts/` | 自动化 | 文档检查、样例构建、CI binary matrix、打包脚本、依赖拉取 |
| `deps/` | 本地可选依赖 | 项目本地 Qt 安装 |
| `.vscode/`、`.idea/`、`.claude/` | 本地/工具元数据 | 编辑器和助手配置 |

### 3.2 构建与配置资产

| 资产 | 作用 |
|---|---|
| `CMakeLists.txt` | 项目定义、C++ 标准、版本、共享库策略、sanitizer/coverage 选项、子目录顺序 |
| `Dependencies.cmake` | fmt、nlohmann/json、Catch2、mimalloc 的 FetchContent 依赖配置 |
| `VERSION.txt` | 打包使用的生成版本文本 |
| `LICENSE` | GPLv3 许可证 |
| `tools/fixup_macos_bundle.cmake` | `polyui` macOS `.app` 动态库 install-name 修复 |
| `scripts/installer.nsi` | Windows NSIS 安装器定义 |

## 4. 构建目标

### 4.1 库目标

| Target | 来源层 | 作用 |
|---|---|---|
| `polyglot_common` | `common/` | 共享类型系统、符号表、插件管理器、调试工具、目标/容器工具 |
| `middle_ir` | `middle/` | IR、SSA、优化器、PGO、LTO |
| `frontend_common` | `frontends/common/` | Token pool、string arena、identifier table、lexer base、preprocessor、registry、language versions |
| `frontend_python` | `frontends/python/` | Python lexer/parser/sema/lowering/frontend 与 `.pyi` loader |
| `frontend_cpp` | `frontends/cpp/` | C++ lexer/parser/sema/lowering/frontend 与 constexpr 支持 |
| `frontend_rust` | `frontends/rust/` | Rust lexer/parser/sema/lowering/frontend 与 crate loader |
| `frontend_ploy` | `frontends/ploy/` | `.ploy` lexer/parser/sema/lowering/frontend、package discovery、config registry |
| `frontend_java` | `frontends/java/` | Java lexer/parser/sema/lowering/frontend 与 class file reader |
| `frontend_dotnet` | `frontends/dotnet/` | .NET lexer/parser/sema/lowering/frontend 与 metadata reader |
| `frontend_javascript` | `frontends/javascript/` | JavaScript lexer/parser/sema/lowering/frontend 与 import resolver |
| `frontend_ruby` | `frontends/ruby/` | Ruby lexer/parser/sema/lowering/frontend 与 import resolver |
| `frontend_go` | `frontends/go/` | Go lexer/parser/sema/lowering/frontend 与 import resolver |
| `backend_common` | `backends/common/` | 后端注册表、target interface、ABI、object file builder、debug emission |
| `backend_x86_64` | `backends/x86_64/` | x86_64 Machine IR、指令选择、调度、调用约定、寄存器分配 |
| `backend_arm64` | `backends/arm64/` | ARM64 Machine IR、指令选择、调度、调用约定、寄存器分配 |
| `backend_wasm` | `backends/wasm/` | Wasm lowering、type mapping、section emission、LEB128、WAT printing |
| `runtime` | `runtime/` | 运行时 C ABI、GC、互操作、桥、服务、分配 |
| `polyglot_tools_settings` | `tools/common/` | CLI 工具和 `polyui` 共享的三层 settings loader |
| `polyc_lib` | `tools/polyc/` | driver 和测试共享的分阶段编译流水线 |
| `linker_lib` | `tools/polyld/` | `polyld` 和测试共享的链接器核心与容器 writer |
| `lsp_lib` | `tools/ui/common/lsp/` | 无 Qt 的 JSON-RPC 与 LSP client/session 基础设施 |
| `polyls_core` | `tools/polyls/` | 语言服务器核心功能、grammar descriptor、symbol index |
| `topo_lib` | `tools/polytopo/` | 拓扑图分析、校验、打印、代码生成 |

### 4.2 可执行目标

| Target | 角色 | 关键依赖 |
|---|---|---|
| `polyc` | 编译器驱动 | `polyc_lib`、settings |
| `polyld` | 链接器和容器写入器 | `linker_lib`、common、`.ploy` frontend、settings |
| `polyasm` | 汇编到 object 工具 | 后端和 middle IR |
| `polyopt` | 独立 IR 优化器 | Middle IR 和后端 |
| `polyrt` | 运行时启动/控制工具 | Runtime、common、settings |
| `polyver` | 工具链/包管理器探测 | Common、JSON |
| `polydoc` | `.ploy` 文档注释提取 | `.ploy` frontend、common |
| `polyls` | 自托管语言服务器 | `polyls_core` |
| `polybench` | 性能基准运行器 | Frontends、backends、runtime、settings |
| `polytopo` | 拓扑分析 CLI | `topo_lib`、common、`.ploy` frontend、settings |
| `polyui` | Qt 桌面 IDE | Frontends、backends、runtime、topology、settings、LSP、language server core |
| `pe_smoke` | PE writer harness | `linker_lib`、common |

## 5. 架构

### 5.1 依赖方向

```text
polyglot_common
  -> middle_ir
  -> frontend_common
  -> frontend_<language>
  -> backend_common -> backend_<target>
  -> runtime
  -> linker_lib
  -> polyc_lib
  -> tools/tests
```

顶层 CMake 按 `common`、`middle`、`frontends`、`backends`、`runtime`、`tools`、`tests` 顺序包含子目录。这是架构依赖规则。除测试隔离代码外，反向依赖应视为设计缺陷。

### 5.2 端到端编译流

```text
源码文件和 .ploy 编排
  -> 语言检测或显式 --lang
  -> frontend registry 分发
  -> lexer/parser/sema/lowering
  -> SemanticDatabase
  -> 跨语言边界 MarshalPlan
  -> BridgeGenerationOutput 与 descriptor metadata
  -> IR 校验与优化
  -> 目标后端发射
  -> object/container 打包
  -> runtime bridge 解析
  -> 最终可执行文件、Wasm 模块、静态库或动态库
```

### 5.3 运行时执行流

```text
编译后入口
  -> 跨语言边界处的生成 bridge stub
  -> 用于分配、GC root、字符串、容器、异常、异步和 profile 的 runtime C ABI
  -> Python/C++/Rust/Java/.NET/Go/JavaScript/Ruby 语言桥表
  -> CLI、文件或 UI 面板使用的 profile/call-trace sink
```

## 6. 编译器核心组件

### 6.1 公共核心

| 区域 | 文件 | 设计职责 |
|---|---|---|
| 类型与符号 | `common/include/core/types.h`、`common/include/core/symbols.h`、`common/src/core/type_system.cpp`、`common/src/core/symbol_table.cpp` | 前端、IR、诊断和链接器元数据共享的类型与符号表示 |
| 源码/配置 | `common/include/core/source_loc.h`、`common/include/core/config.h` | 稳定源码位置和共享配置结构 |
| 目标/容器 | `common/include/target_triple.h`、`common/include/binary_container.h` | Target triple 解析、host triple 检测、输出容器选择 |
| 调试 | `common/include/debug/`、`common/src/debug/dwarf5.cpp` | 调试元数据 builder/adapter 和 DWARF 5 支持 |
| 插件 | `common/include/plugins/plugin_api.h`、`common/include/plugins/plugin_manager.h`、`common/src/plugins/plugin_manager.cpp` | 稳定插件 C ABI 和宿主侧插件加载 |
| 工具 | `common/include/utils/` | Arena、hash、logging、string pool |
| 版本 | `common/include/version.h`、`common/include/version.h.in` | 生成的产品版本契约 |

### 6.2 前端公共层

| 组件 | 文件 | 契约 |
|---|---|---|
| 诊断 | `frontends/common/include/diagnostics.h` | 带源码位置和严重级别的共享 parse/sema diagnostics |
| 注册表 | `frontend_registry.h/.cpp` | 线程安全前端注册、名称/别名/扩展名查询、语言检测 |
| 前端接口 | `language_frontend.h` | 公共前端 adapter surface |
| Lexer/parser 基础 | `lexer_base.h/.cpp`、`parser_base.h` | 共享 token 和 parser 基础设施 |
| 预处理 | `preprocessor.h/.cpp` | 共享预处理层 |
| Token 存储 | `token_pool.h/.cpp`、`string_arena.h/.cpp`、`identifier_table.h/.cpp` | Interning 和高效 token/identifier 管理 |
| 语言版本 | `language_versions.h/.cpp` | 版本门控和语言特性控制 |
| Sema context | `sema_context.h` | 共享语义上下文基础 |

### 6.3 语言前端矩阵

| 语言 | 库 | 文件扩展 | 实现单元 | 特殊范围 |
|---|---|---|---|---|
| C++ | `frontend_cpp` | `.cpp`、`.cxx`、`.cc`、`.h`、`.hpp` | AST、lexer、parser、sema、lowering、frontend、constexpr | C++ 语言模型和常量表达式 |
| Python | `frontend_python` | `.py`、`.pyi` | AST、lexer、parser、sema、lowering、frontend、advanced features、`.pyi` loader | Python 3.8+ 风格特性门控和 stub 加载 |
| Rust | `frontend_rust` | `.rs` | AST、lexer、parser、sema、lowering、frontend、advanced features、crate loader | Rust edition、crate discovery、borrow/type checks |
| `.ploy` | `frontend_ploy` | `.ploy` | AST、lexer、parser、sema、lowering、frontend、package cache、command runner、package indexer、config registry | 跨语言编排、包发现、bridge descriptor |
| Java | `frontend_java` | `.java`、class metadata | AST、lexer、parser、sema、lowering、frontend、class file reader | Java 8/17/21/23 特性和 class metadata |
| .NET | `frontend_dotnet` | `.cs`、`.vb`、metadata | AST、lexer、parser、sema、lowering、frontend、metadata reader | .NET 6/7/8/9 metadata 和语言特性门控 |
| JavaScript | `frontend_javascript` | `.js` | AST、lexer、parser、sema、lowering、frontend、import resolver | ES 特性解析和模块导入解析 |
| Ruby | `frontend_ruby` | `.rb` | AST、lexer、parser、sema、lowering、frontend、import resolver | Ruby 导入和动态语言互操作模型 |
| Go | `frontend_go` | `.go` | AST、lexer、parser、sema、lowering、frontend、import resolver | Go module import 和泛型特性处理 |

### 6.4 `.ploy` 语言能力清单

| 能力 | 设计契约 |
|---|---|
| 函数链接 | `LINK` 声明定义跨语言函数绑定和签名 |
| 函数调用 | `CALL(language, symbol, args...)` 创建显式跨语言调用点 |
| 对象实例化 | `NEW(language, class, args...)` 构造外部对象，schema 已知时返回 typed handle |
| 方法调用 | `METHOD(language, object, method, args...)` 调用外部方法 |
| 属性访问 | `GET` 和 `SET` 映射到外部属性/property 访问 |
| 资源管理 | `WITH` 表示 enter/exit 资源生命周期 |
| 对象删除 | `DELETE` 映射到语言相关 release/drop/dispose hook |
| 类型映射 | `MAP_TYPE`、`MAP_FUNC`、`CONVERT` 和 typed handle 跨语言映射数据 |
| 包 | `IMPORT ... PACKAGE`、版本约束、选择性导入、包管理器配置 |
| 配置 | venv、conda、uv、pipenv、poetry、cargo、npm、maven、gradle、NuGet、bundler、Go module 风格配置 |
| 控制流 | `IF`、`ELSE`、`WHILE`、`FOR`、`MATCH`、`CASE`、`BREAK`、`CONTINUE` |
| 错误流 | `TRY`、`CATCH`、`FINALLY`、`THROW`、`ERROR`、option 风格传播 |
| 异步流 | `ASYNC`、`AWAIT`、runtime async bridge |
| 可见性/属性 | `PUB`、`PRIVATE`、annotation catalog |
| 字面量与类型 | 字符串、raw string、template string、数值宽度、容器、struct、generics |

### 6.5 中端

| 区域 | 文件 | 设计职责 |
|---|---|---|
| IR 所有权 | `middle/include/ir/ir_context.h`、`middle/src/ir/ir_context.cpp` | 函数、全局值、dialect、data layout |
| IR 构建 | `ir_builder.h`、`builder.cpp` | 程序化 IR 创建 |
| CFG 和 SSA | `cfg.h/.cpp`、`ssa.h/.cpp` | Basic block、控制流、SSA 转换 |
| IR parser/printer | `ir_parser.h`、`ir_printer.h`、`parser.cpp`、`printer.cpp` | 文本 IR roundtrip 和工具化 |
| 分析 | `analysis.h/.cpp`、`passes/analysis/alias.h`、`passes/analysis/dominance.h` | 数据流/控制流分析 |
| 校验 | `verifier.h/.cpp` | 后端发射前的 IR 结构校验 |
| 模板/类 | `template_instantiator.h/.cpp`、`class_metadata.h` | 泛型和类元数据支持 |
| 函数级 Pass | `middle/include/ir/passes/opt.h` | 常量折叠、DCE、拷贝传播、CFG 规范化、phi 简化、CSE、Mem2Reg |
| Transform Pass | `middle/include/passes/transform/` | 常量折叠、DCE、CSE、内联、去虚化、循环、GVN、高级优化、call trace instrumentation |
| PGO | `middle/include/pgo/profile_data.h`、`middle/src/pgo/profile_data.cpp` | Profile 数据模型 |
| LTO | `middle/include/lto/link_time_optimizer.h`、`middle/src/lto/link_time_optimizer.cpp` | 跨模块优化 |

### 6.6 后端系统

| 后端区域 | 文件 | 契约 |
|---|---|---|
| Target interface | `backends/common/include/target_backend.h` | 可重入 `ITargetBackend`、options、capabilities、artifacts、diagnostics |
| 注册表 | `backend_registry.h/.cpp` | 通过 triple/alias 查询 target 和能力发现 |
| ABI | `abi.h`、`abi/`、`relocation.h`、`target_machine.h` | 调用约定、重定位、target machine metadata |
| Object builder | `object_file.h/.cpp` | ELF、COFF、Mach-O object 构造支持 |
| Debug emission | `debug_info.h/.cpp`、`debug_emitter.h/.cpp`、`dwarf_builder.h/.cpp` | DWARF/debug info 发射 |
| x86_64 | `backends/x86_64/` | x86 target backend、register、Machine IR、instruction selection、optimization、scheduling、regalloc |
| ARM64 | `backends/arm64/` | ARM64 target backend、register、Machine IR、instruction selection、scheduling、regalloc |
| Wasm | `backends/wasm/` | Wasm target、backend、type mapping、function/instruction lowering、sections、LEB128、WAT |

后端输出必须包含 sections、relocations、symbols、unresolved symbols、适用时的 object bytes、请求且支持时的 assembly/bitcode、diagnostics 和分阶段耗时统计。

### 6.7 运行时系统

| 运行时区域 | 文件 | 契约 |
|---|---|---|
| GC | `runtime/include/gc/`、`runtime/src/gc/` | Mark-sweep、generational、copying、incremental、root guard、heap/runtime API |
| 分配 | `runtime/include/memory/polyglot_alloc.h`、`runtime/src/memory/polyglot_alloc.cpp` | 运行时分配接口 |
| 互操作 | `runtime/include/interop/`、`runtime/src/interop/` | FFI、memory、marshalling、type mapping、calling convention、container marshalling、object lifecycle |
| Base C ABI | `runtime/include/libs/base.h`、`runtime/src/libs/base.c`、`base_gc_bridge.cpp` | 生成代码调用的稳定运行时入口 |
| 语言桥 | `cpp_rt`、`python_rt`、`rust_rt`、`java_rt`、`dotnet_rt`、`go_rt`、`javascript_rt`、`ruby_rt` | 语言相关 bridge entry table 和生命周期函数 |
| 服务 | `runtime/include/services/`、`runtime/src/services/` | Exception、error bridge、async bridge、event loop、reflection、threading、call trace、profile sink |
| 平台 shim | `runtime/include/polyrt_linux.h`、`runtime/src/libs/polyrt_linux.c` | Linux runtime entry 支持 |

运行时代码生成契约：生成代码调用 C linkage 符号，并把外部对象视为 opaque handle，除非 marshalling 规则定义了具体布局。

## 7. 编译流水线数据契约

`tools/polyc/include/compilation_pipeline.h` 是主要流水线契约。

| 阶段 | 输入 | 输出 | 关键字段 |
|---|---|---|---|
| Frontend | `CompilationContext::Config` | `FrontendOutput` | AST、source file、language、token stream、parse diagnostics、source mtime、success |
| Semantic | `FrontendOutput` | `SemanticDatabase` | Validated AST、symbols、signatures、class schemas、link entries、type mappings、packages、venv configs、sema diagnostics |
| Marshal Plan | `SemanticDatabase` | `MarshalPlan` | Per-call marshal plans、helper dependencies、plan diagnostics |
| Bridge Generation | `MarshalPlan` 和 `SemanticDatabase` | `BridgeGenerationOutput` | Generated stubs、exported symbols、runtime dependencies、descriptor metadata JSON、diagnostics |
| Backend | Semantics、bridges、target config | `BackendOutput` | Compiled objects、target arch/OS/triple、diagnostics、assembly text |
| Packaging | `BackendOutput`、config | `PackagingOutput` | Binary data、format、path、entry point、needed libraries、exported symbols、diagnostics、checksum |

### 7.1 配置面

`CompilationContext::Config` 包含 source file/text/language、output file、target arch/OS/triple、container、subsystem、entry symbol、mode、object format、linker path、optimization level、strict/force flags、aux dir、package indexing、IR/ASM/object emission paths、additional libraries、include/system include paths、defines、Python stub paths、Java classpath、.NET references、Rust crate settings 和 `.ploy` descriptor path。

### 7.2 Marshal 策略

| 策略 | 使用场景 |
|---|---|
| `kDirectCopy` | ABI 兼容 primitive 或相同表示 |
| `kIntToFloat` | 整数到浮点转换 |
| `kFloatToInt` | 浮点到整数转换 |
| `kStringEncode` | UTF/string 表示转换 |
| `kContainerCopy` | list/tuple/dict/container 转换 |
| `kPointerToHandle` | 原生指针包装为 runtime handle |
| `kStructFieldByField` | 结构化数据转换 |
| `kOpaquePtr` | Opaque pointer 透传 |

## 8. 链接器与打包设计

### 8.1 链接器组件

| 组件 | 文件 | 职责 |
|---|---|---|
| Core linker | `tools/polyld/include/linker.h`、`tools/polyld/src/linker.cpp` | 符号、section、relocation、通用链接流 |
| ELF writer | `linker_elf.h/.cpp` | Linux ELF 容器发射 |
| PE writer | `linker_pe.h/.cpp`、`pe_writer.h/.cpp` | Windows PE32+ 发射和 writer hardening |
| Mach-O writer | `linker_macho.h/.cpp` | macOS Mach-O 发射 |
| Wasm linker | `linker_wasm.h/.cpp` | WebAssembly 链接 |
| Polyglot linker | `polyglot_linker.h/.cpp` | 跨语言符号解析、ABI 校验、glue stub 生成、descriptor 加载 |
| CLI | `tools/polyld/src/main.cpp` | 用户可见链接器驱动 |

### 8.2 跨语言链接器契约

`PolyglotLinker` 接收 `.ploy` call descriptor、已校验 link entry、跨语言 symbol、descriptor 文件和 aux 目录发现结果。它解析语言/符号对，校验 ABI 兼容性，生成 glue stub，发射 relocation，跟踪 errors/warnings，并把生成 stub 暴露给主链接器。

硬失败条件：

- 找不到必需符号。
- ABI mismatch 无法适配。
- 需要 descriptor 文件但文件缺失或非法。
- 必需 marshalling helper relocation 没有发射。
- 重复 strong symbol 无法解析。
- 目标/container 不匹配且未显式允许。

## 9. 工具套件设计

| 工具 | 主要输入 | 主要输出 | 设计职责 |
|---|---|---|---|
| `polyc` | 源文件、`.ploy`、CLI flags、settings | Objects、aux artifacts、binaries/libraries、diagnostics | 完整编译流水线驱动 |
| `polyld` | Objects、descriptor files、libraries | ELF/PE/Mach-O/Wasm outputs | 链接和容器发射 |
| `polyasm` | Assembly | Object files | 后端产物的汇编路径 |
| `polyopt` | Textual IR | Optimized IR | 独立中端测试与优化 |
| `polyrt` | Runtime commands/config | Runtime actions/statistics | 运行时工具 |
| `polyver` | Host environment | Toolchain database | 工具链/包管理器发现 |
| `polydoc` | `.ploy` source | Markdown/JSON docs | 文档注释提取 |
| `polytopo` | `.ploy` and descriptors | Text/DOT/JSON topology | 跨语言拓扑分析 |
| `polyls` | LSP JSON-RPC | LSP responses/notifications | 编辑器集成和诊断 |
| `polybench` | Bench suites/settings | Benchmark results | 性能验证 |
| `polyui` | Workspace/project files | IDE UX、panels、diagnostics | 桌面 IDE |

## 10. IDE、LSP 与工具 UI 设计

### 10.1 语言服务器

| 区域 | 文件 | 职责 |
|---|---|---|
| Server lifecycle | `tools/polyls/polyls_core/polyls_server.h/.cpp` | initialize、shutdown、exit、document sync |
| Feature dispatch | `polyls_features.cpp`、`polyls_navigation.cpp`、`polyls_refactor.cpp`、`polyls_semantic.cpp` | Completion、hover、signature help、navigation、refactor、semantic tokens |
| Symbol index | `symbol_index.h/.cpp` | Workspace/document symbol model |
| Grammar descriptor | `tools/polyls/grammar/` | 工具使用的 grammar metadata |
| Transport CLI | `tools/polyls/polyls.cpp` | stdio JSON-RPC driver |
| Shared LSP client | `tools/ui/common/lsp/` | 无 Qt 的 LSP messages、sessions、capability registry、client |

### 10.2 PolyUI 子系统

| 子系统 | 路径 | 职责 |
|---|---|---|
| Core shell | `tools/ui/common/src/mainwindow.cpp`、`panel_manager`、`action_manager`、shell 目录 | 主窗口、面板、动作路由、通知、session/recent/bookmarks/todo |
| Editor | `code_editor`、`syntax_highlighter`、`editor_group`、`editor_grid`、`semantic_tokens_client` | 多文档编辑、语法和语义高亮 |
| Editing features | `editing/`、`outline/`、`search/`、`quickopen/`、`inlay_hints/` | Format、snippet、folding、multi-cursor、config、outline、search、quick open、inlay hints |
| Compiler integration | `compiler_service`、`build_panel`、`output_panel`、`terminal_widget` | 编译/构建命令、输出展示、终端集成 |
| Problems and code assist | `problems_aggregator`、`problems_panel`、`code_assist`、`completion_ranker`、`lsp_bridge` | 诊断、补全、代码辅助、LSP bridge |
| Debug/task/runtime | `dap/`、`tasks/`、`runtime/`、`debug_panel` | DAP client/session、launch config、task config/runner、hot reload、run/debug picker |
| SCM | `scm/`、`git_panel` | SCM provider、Git provider、diff、merge resolver |
| Testing | `testing/` | Test model、inline test lens、coverage model |
| Packages | `packages/` | Package manager、dependency graph、vulnerability scanner |
| Cross-language panels | `cross_language/`、`pipeline/`、`topology_live/`、`samples/` | Navigator、bridge panel、marshalling view、pipeline inspector、IR/ASM viewers、topology live、sample browser |
| Profiling/calls | `profiler_panel`、`profile_session`、call graph/timeline/flame models、`call_analyzer_panel` | Runtime profile 和 call graph analysis |
| Workspace/remote | `workspace/`、`remote/`、`file_browser`、`workspace_scanner` | Workspace model、remote sessions、dev containers、file sync |
| AI/collab/ext | `ai/`、`collab/`、`ext/` | AI provider、inline suggestions、refactor diff、collaboration provider、extension API、marketplace |
| Settings/theme/i18n/a11y | `settings_*`、`theme_*`、resources、`i18n/`、`a11y/` | Settings schema/service/page、theme service/manager、localization、accessibility |
| Viewers/data tools | `viewer/`、`dbclient/`、`markdown_viewer` | Image viewer、hex viewer、binary inspector、SQL console、Markdown viewer |
| Platform entry | `tools/ui/linux`、`tools/ui/macos`、`tools/ui/windows` | 平台相关 `polyui` 启动和资源 |

## 11. 设置、主题、插件与扩展

| 系统 | 资产 | 契约 |
|---|---|---|
| Settings | `tools/common/include/effective_settings_loader.h`、`tools/ui/common/resources/default_settings.json`、`settings_schema.json` | CLI 和 UI 共享的 default/user/workspace 分层 JSON settings |
| Themes | `theme_schema.json`、内置 `.polytheme.json` 文件 | VS Code 风格主题发现、校验、预览和 UI 应用 |
| Plugin API | `common/include/plugins/plugin_api.h`、`docs/specs/plugin_specification.md` | 稳定 C ABI、capability flags、host services、lifecycle |
| Extension UI | `tools/ui/common/ext/extension_api.*`、`marketplace.*` | UI 侧 extension 和 marketplace 管理 |
| Docs generation | `scripts/docs_generate.py`、`polydoc` | 生成文档和 `.ploy` 文档注释提取 |

## 12. 样例与 Fixture

### 12.1 样例分类

| 范围 | 样例 | 特性覆盖 |
|---|---|---|
| 00 | `00_minimal` | 最小 print/exit 路径 |
| 01-04 | `01_basic_linking`、`01_basic_linking_v2`、`02_type_mapping`、`03_pipeline`、`04_package_import` | 链接、类型映射、流水线、包 |
| 05-10 | `05_class_instantiation`、`06_attribute_access`、`07_resource_management`、`08_delete_extend`、`09_mixed_pipeline`、`10_error_handling` | 对象互操作、属性、资源、delete/extend、混合流水线、错误 |
| 11-15 | `11_java_interop`、`12_dotnet_interop`、`13_generic_containers`、`14_async_pipeline`、`15_full_stack` | Java/.NET、容器、异步、全栈流程 |
| 16-20 | `16_config_and_venv`、`17_string_processing`、`18_numeric_kernels`、`19_file_io`、`20_json_pipeline` | 环境、字符串、数值 kernel、文件 IO、JSON |
| 21-30 | `21_image_processing` 到 `30_game_loop_demo` | 图像、数据库、HTTP、并发、事件循环、状态机、插件系统、ML、分析、game loop |
| 31-41 | `31_explicit_widths` 到 `41_grammar_polish` | 数值宽度、typed handles、pattern matching、default args、dynamic extend、try/catch、async/await、generics、visibility、strings、grammar polish |

每个样例目录预期包含 README 中英双文档、`.ploy` 文件、必要的支持语言文件，以及 expected output 文件；环境相关场景可按项目约定例外处理。

### 12.2 Fixtures

`tests/fixtures/external_packages/` 包含 JavaScript、Python stubs、Rust Cargo packages、Go modules、Ruby gems 以及其他 package/import 集成路径的 fixture。这些 fixture 由外部包发现测试和样例回归流使用。

## 13. 文档系统

| 文档区域 | 文件 | 目的 |
|---|---|---|
| 用户指南 | `docs/USER_GUIDE.md`、`docs/USER_GUIDE_zh.md` | 用户完整指南 |
| API 文档 | `docs/api/` | API reference、extension API、`polyls`、`polydoc`、profile API |
| Specs | `docs/specs/` | Language/IR、namespace、optimization、runtime ABI、plugin spec、packaging、LSP、schemas、attributes |
| Realization notes | `docs/realization/` | 编译器、运行时、UI、互操作、语言特性、打包、settings、themes、tools 的实现说明 |
| Tutorials | `docs/tutorial/` | `.ploy`、project、LSP、profiling、problems panel、shell、viewers、call analyzer 教程 |
| Demand log | `docs/demand/demand.md` | 历史需求流和完成标记 |
| Changelog | `docs/CHANGELOG.md`、`docs/CHANGELOG_zh.md` | 发布历史 |
| Design | `docs/design.md`、`docs/design_zh.md` | 本仓库级设计规格 |

文档规则：

- 面向用户的文档应保持双语，除非既有命名约定明确为 `_en.md`。
- 交叉链接必须引用存在的文件。
- 影响设计的实现变更必须在同一变更集中更新文档。
- Demand-log 条目保留完成标记；设计文档不复制原始需求文本。

## 14. 脚本与自动化

| 脚本 | 职责 |
|---|---|
| `scripts/build_all_samples.sh`、`scripts/build_all_samples.ps1` | 构建样例矩阵 |
| `scripts/check_include_deps.py` | Include dependency 检查 |
| `scripts/docs_generate.py` | 文档生成 |
| `scripts/docs_lint.py` | Markdown path/link/bilingual/version lint |
| `scripts/docs_sync_check.py` | 核心双语文档同步 |
| `scripts/fetch_deps.sh`、`scripts/fetch_deps.ps1` | 依赖拉取 |
| `scripts/ci/run_binary_matrix.sh`、`scripts/ci/run_binary_matrix.ps1` | Target/container binary matrix 检查 |
| `scripts/package_linux.sh`、`scripts/package_macos.sh`、`scripts/package_windows.ps1` | 发布包组装 |
| `scripts/installer.nsi` | Windows 安装器 |

## 15. 错误处理与诊断

### 15.1 错误类别

| 类别 | 示例 | 必需行为 |
|---|---|---|
| CLI/config | 未知 flag、非法 target triple、非法 settings JSON | 编译前失败并给出可执行消息 |
| Lex/parse | 未知 token、语法错误、字符串未闭合 | 停止该源码 sema 并发布 parse diagnostics |
| Semantic | 缺失符号、参数数量错误、类型不兼容、非法包管理器 | 停止 marshal planning 和后端发射 |
| Package discovery | 缺失包、版本不匹配、index 命令超时 | 带 manager/environment 上下文报告 package diagnostic |
| Marshal plan | 不支持转换、未知 handle 类型、缺失 type map | 停止 bridge generation |
| Bridge generation | 缺失 helper、relocation 遗漏、不支持调用过渡 | 停止 backend/link path |
| IR verification | 非法 CFG、畸形 SSA、非法类型操作 | 停止后端，除非显式 force mode 激活 |
| Backend | 不支持指令、寄存器分配失败、缺失必需 section | 停止 packaging |
| Linker | 缺失符号、未解析 relocation、重复 strong symbol、非法 descriptor | `polyld` 失败 |
| Runtime | 外部异常、null handle、GC root 误用、async bridge 失败 | 通过 runtime status/error bridge/call trace 暴露 |
| UI/LSP | 非法 document sync、不支持 request、陈旧 workspace cache | 返回 JSON-RPC error 或 UI diagnostic，不崩溃服务 |

### 15.2 诊断契约

诊断应携带 severity、适用时的 stable code、source file/URI、line、column、span、message、可选 note/help，以及跨语言 traceback 的 related locations。

同一个诊断对象模型应支持：

- 人类可读 CLI 输出。
- JSON progress 输出。
- LSP `publishDiagnostics`。
- `polyui` Problems 面板。
- 单元和集成测试断言。
- 生成文档示例。

## 16. 安全、可靠性与健壮性

| 区域 | 设计要求 |
|---|---|
| Plugins | 激活前校验 API version、required exports、capability flags、lifecycle return codes |
| Package discovery | 使用有界超时和显式 manager 配置；默认不把任意项目脚本作为 discovery 执行 |
| Path writes | 生成文件必须位于 output 或 aux 位置，除非用户提供绝对路径 |
| Runtime handles | 对象生命周期或方法操作前校验 null/invalid handle |
| Marshalling | 分配/拷贝前校验 container length、element size、alignment、ownership |
| LSP/UI | Workspace text 和 metadata 在编译器服务校验前视为不可信 |
| Linker | Unresolved symbol/relocation/descriptor 为硬错误 |
| Tests | 影响 parser、sema、lowering、backend、linker、runtime 或 UI model 契约的修复必须伴随回归测试 |

## 17. 性能设计

| 区域 | 性能方法 |
|---|---|
| Build | 默认 shared library，减少重复编译并缩小可执行文件 |
| Frontends | String arena、identifier interning、shared token pool、可复用 lexer/parser infrastructure |
| Middle-end | 优化等级控制 Pass 成本；函数级和上下文级 Pass 分离 |
| LTO/PGO | Metadata 按需付费并支持跨模块优化 |
| Backends | 对 instruction selection、regalloc、scheduling、emission 记录分阶段耗时 |
| Aux artifacts | 配置化 aux 目录提供增量和检查友好的输出 |
| UI | 长耗时编译工作放在 service/model 层，不阻塞编辑交互 |
| Samples/benchmarks | 专用 benchmark tiers 和 sample regression 捕获功能和性能漂移 |

## 18. 测试策略

### 18.1 CTest 目标

| Target | 范围 |
|---|---|
| `test_core` | Common utilities、type system、target triples、DWARF |
| `test_plugins` | Plugin manager |
| `test_frontend_common` | Token pool、preprocessor、registry |
| `test_frontend_python` | Python frontend |
| `test_frontend_cpp` | C++ frontend |
| `test_frontend_rust` | Rust frontend |
| `test_frontend_ploy` | `.ploy` frontend |
| `test_frontend_java` | Java frontend |
| `test_frontend_dotnet` | .NET frontend |
| `test_frontend_javascript` | JavaScript frontend |
| `test_frontend_ruby` | Ruby frontend |
| `test_frontend_go` | Go frontend |
| `test_middle` | IR、SSA、LTO、optimization |
| `test_backends` | ABI、backend registry、machine IR、object emission |
| `test_runtime` | GC、FFI、runtime services |
| `test_linker` | Linker、object formats、PE/Mach-O/ELF/Wasm |
| `test_topology` | Topology graph 和 linker probe |
| `test_settings` | Settings loader |
| `test_lsp` | LSP client core |
| `test_polyls` | Language server features |
| `test_problems` | Problem aggregation |
| `test_completion_ranker` | Completion ranking |
| `test_topology_ui` | Qt topology UI，可用 Qt 时构建 |
| `test_e2e` | End-to-end compilation behavior |
| `unit_tests` | Aggregate compatibility target |
| `integration_tests` | Full integration suite |
| `samples_regression` | 通过 Catch2 `[samples]` 跑样例矩阵 |
| `benchmark_tests`、`benchmark_fast`、`benchmark_full` | Benchmark tiers |

### 18.2 集成覆盖

集成测试覆盖 compile pipeline、interop、performance、E2E `polyc`、object formats、external packages、language versions、theme system、PE runtime smoke、real exit codes、sample regression、cross-platform sample consistency、profiler、explicit widths、printf pipeline、LSP diagnostics/navigation/refactor/semantic tokens、Mach-O/Wasm/triple propagation、binary matrix、Mach-O exec smoke 和 ELF exec smoke。

### 18.3 验证命令

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

针对性示例：

```bash
cmake --build build --target test_frontend_ploy
./build/test_frontend_ploy

cmake --build build --target integration_tests
./build/integration_tests

python3 scripts/docs_lint.py
python3 scripts/docs_sync_check.py --ci --scope core
```

## 19. 可追踪性矩阵

| 需求区域 | 实现面 | 测试/文档面 |
|---|---|---|
| 多语言前端 | `frontends/`、`FrontendRegistry` | `test_frontend_*`、`docs/specs/language_spec.md` |
| `.ploy` 编排 | `frontends/ploy/`、`tools/polyc`、`tools/polyld` | `test_frontend_ploy`、samples、`.ploy` tutorials |
| 共享 IR/优化 | `middle/` | `test_middle`、optimization spec |
| 原生/Wasm 后端 | `backends/` | `test_backends`、binary matrix、object format tests |
| 运行时互操作 | `runtime/` | `test_runtime`、interop integration tests、runtime ABI docs |
| 链接/打包 | `tools/polyld/`、packaging stage | `test_linker`、E2E smoke tests、release packaging docs |
| CLI 工具 | `tools/polyc`、`polyopt`、`polyasm`、`polyrt`、`polyver`、`polydoc`、`polytopo`、`polybench` | Tool tests、API docs、tutorials |
| LSP | `tools/polyls`、`tools/ui/common/lsp` | `test_lsp`、`test_polyls`、LSP integration docs |
| IDE | `tools/ui/` | `test_problems`、`test_topology_ui`、polyui realization docs |
| Settings/themes | `tools/common`、`tools/ui/common/resources` | `test_settings`、theme integration test、settings/theme docs |
| Samples | `tests/samples/` | `samples_regression`、sample README pairs |
| 发布自动化 | `scripts/package_*`、`installer.nsi` | release packaging docs |

## 20. 变更控制

以下变化影响设计时必须更新本文档：

- 构建目标或依赖方向。
- 前端/后端/运行时/链接器契约。
- `.ploy` 语法、语义、诊断或包行为。
- 编译流水线阶段输入/输出。
- 影响产物的 CLI flags。
- UI/LSP 用户可见行为。
- 测试目标名称或验证策略。
- 文档分类体系。

只有实现或发布语义变化时才修改版本。纯文档规范化不需要修改根目录 `CMakeLists.txt` 版本。

--end -done
