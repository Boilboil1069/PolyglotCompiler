# PolyglotCompiler 更新日志

本文件记录 PolyglotCompiler 的所有重要变更。

英文版本见 [`CHANGELOG.md`](CHANGELOG.md)。
日常使用说明见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)；构建/API 契约见
[`api/api_reference_zh.md`](api/api_reference_zh.md) 以及 [`realization/`](realization/)
下的逐特性说明。

下述版本范围为 **v0.1.0 (2026-01-15) → v1.5.5 (2026-04-29)**，新版本在前。
每个 `### vX.Y.Z (YYYY-MM-DD)` 段落只描述发布行为本身。

---

## v1.5.5 (2026-04-29)

**`BuildPrintlnSequencePE` 上线——运行时标准输出管线 B4 阶段（demand `2026-04-28-49`）。**

### 新增内容

- 新增公开 PE 写入入口 `polyglot::linker::pe::BuildPrintlnSequencePE(const std::vector<std::string> &call_messages)`，
  生成可独立运行的 PE32+ 镜像：按入参顺序对 `STD_OUTPUT_HANDLE` 依次发起
  `kernel32!WriteFile`，最后通过 `kernel32!ExitProcess(0)` 终止进程。
  这是该管线中**首个真正发射 AMD64 机器码驱动 Windows 标准输出**的阶段，
  消费的正是 v1.5.4 在 IR 层产出的 `polyrt_println(i8*, i64)` 调用所携带的字节流。

### Win64 ABI shim 契约

- `.text` 布局为 `序言（0x25 字节）+ N × 单条消息块（0x1D 字节）+ 尾声（0x09 字节）`。
  所有偏移字节稳定，单元测试逐字节校验。
- 序言：`sub rsp, 0x38` 一次性预留 Win64 强制的 32 字节影子空间 +
  8 字节 `lpOverlapped` 槽（清零）+ 8 字节 `lpNumberOfBytesWritten` 槽（清零）+
  8 字节 stdout 句柄缓存。随后 `GetStdHandle(-11)` 仅调用一次，返回值缓存到
  `[rsp+0x30]`，后续 WriteFile 调用直接重新加载，无需额外押栈被调用者保存寄存器。
- 单条消息块：`mov rcx, [rsp+0x30]; lea rdx, [rip+msg_i];
  mov r8d, len_i; lea r9, [rsp+0x28]; call qword ptr [rip+WriteFile]`。
  所有 RIP 相对位移基于本块自身 RVA 计算，前面无论挂多少消息块，shim 都自洽。
- 尾声：`xor ecx, ecx; call qword ptr [rip+ExitProcess]; int3`
  （`int3` 不可达，仅用于把 shim 长度对齐到确定值）。

### 去重与布局契约

- 字节相同的消息共享同一段 `.rdata` 存储（线性扫描的唯一表），
  与 v1.5.4 中 `IRBuilder::MakeStringLiteral` 的 IR 层 interning 契约严格对齐。
  `PRINTLN "hello\n";` 调用十次，对应十个 `WriteFile` 块，但 `.rdata` 仅一份。
- `call_messages.empty()` 时直接转发 `BuildExitZeroPE({})`，产出字节级一致的镜像，
  绝不会发射退化的空 shim。
- 单条消息长度上界为 `0xFFFFFFFFu`（WriteFile 的 `nNumberOfBytesToWrite` 是 DWORD），
  超界时返回空 `BuildResult`。
- 三段式布局（`.text` + `.rdata` + `.idata`）以及 `BuildPE32PlusImage` 全套管线
  与 `BuildHelloWorldPE` 完全复用，所以 v1.5.1 起的所有节头 / IAT / 导入描述符
  不变量继续有效。

### 新增测试

- `tests/unit/linker/pe_writer_test.cpp`（+4 用例 / +30 断言）——
  空入参转发、单消息结构、三消息展开后 `.text` 长度、重复消息触发 `.rdata` 收缩。
- `tests/integration/pe_runtime_smoke_test.cpp`（+1 用例 / +5 断言）——
  生成包含一条重复消息的 3 条 PE，重定向 stdout 到临时文件，
  按字节比对 `"alpha\r\nbeta\r\nalpha\r\n"`。
- `tools/polyld/src/pe_writer_smoke.cpp`（+1 手工片段）——端到端肉眼验证：
  生成的 `pe_smoke_println.exe` 在宿主加载器上打印三行并以 0 退出。

### 回归结果

- `test_linker`：56 用例 / 284 断言 ✅（v1.5.4 为 52 用例）。
- `test_frontend_ploy`：310 用例 / 1907 断言 ✅（不变）。
- `test_e2e`：54 用例 / 171 断言 ✅（不变）。
- `integration_tests`：130 用例 / 552 断言 ✅（v1.5.4 为 129 用例）。

### 后续

- **B5**：让 polyld 从对象文件中提取 `polyrt_println` 调用点及其 interned 字符串载荷，
  作为 `call_messages` 喂给 `BuildPrintlnSequencePE`，替换今天还在发射的占位 exit-zero shim。
- **B6**：端到端 `.ploy → .obj → .exe` 管线；扩展 demand-04 的 expected_output 校验
  以按字节比对真实 stdout。

---

## v1.5.4 (2026-04-29)

**`PrintlnStmt` 现已降级为 IR——运行时标准输出管线 B3 阶段（demand `2026-04-28-49`）。**

### 降级契约

- `PRINTLN "literal";` 会降级为两件 IR 产物：
  1. 一个类型为 `[N x i8]` 的全局常量，其内容是 **解码后** 的字节
     （前端刻意保留了反斜杠转义，因此解码表——`\n`、`\r`、`\t`、`\\`、
     `\"`、`\0`、`\xHH` ——位于降级层，作为解释器、IR 文本回环和代码生成
     共同遵循的唯一权威）。
  2. 在当前基本块中直接发射一条
     `call void @polyrt_println(i8* msg, i64 len)` 指令。被调方刻意保留
     为外部符号；B5 阶段会通过 polyld 的运行时 DLL 导入表完成解析。
- 选择「指针 + 长度」而非 NUL 结尾，是为了让内嵌 `\0` 字节能干净往返，
  也让空字面量（`PRINTLN "";`）能直接降级为单条
  `polyrt_println(ptr, 0)` 调用，无需在下游做特殊处理。
- 同样内容的字面量会通过 `IRBuilder::MakeStringLiteral` 的「按内容键控
  去重」共享同一个全局，从而让 `.rdata` 在所有目标格式下都保持紧凑。
- 顶层 `PRINTLN` 会按既有 `IRContext::DefaultBlock` 约定落到自动创建的
  `entry_fn` 中；位于 `FUNC` 体内的 `PRINTLN` 则落到所在函数的基本块，
  这是 B4 代码生成阶段保证栈帧正确性的关键性质。
- 未知或畸形的转义序列（如 `\q`、`\xZZ`）会触发
  `kGenericWarning` 诊断并把原始字节按字面保留——降级永不因坏转义而中断，
  以便其余模块仍可被检查。

### 实现

- `frontends/ploy/include/ploy_lowering.h` —— 新增
  `LowerPrintlnStatement(const std::shared_ptr<PrintlnStmt> &)` 声明。
- `frontends/ploy/src/lowering/lowering.cpp`：
  - `LowerStatement` 分发器识别 `PrintlnStmt`。
  - 新增匿名命名空间内的 `DecodePrintlnLiteral(...)` 辅助函数承担
    转义解码工作；结构上让未来扩展（`\u{…}`、`\u00XX`、`\b`、`\f`）
    只需新增一个 `case`。
  - `LowerPrintlnStatement` 通过 `builder_.MakeStringLiteral(...)`
    内化解码字节，然后发射 `polyrt_println` 调用。
- 除新增的一个方法外，公共头文件没有任何接口变化；现有前端调用方
  保持二进制兼容。

### 测试

- `tests/unit/frontends/ploy/println_lowering_test.cpp`（5 用例 /
  21 断言）：覆盖 IR 级解码契约、空消息边界、全局去重、函数体基本块
  落点，以及未知转义只警告不中断的路径。
- Ploy 前端整套测试保持全绿：310 用例 / 1907 断言。
- `test_linker`（43/245）、`test_e2e`（54/171）、`integration_tests`
  （129/547）全部通过——`polyrt_println` 在目标文件中目前是未解析的
  外部符号，但尚无任何端到端测试链接含 PRINTLN 的模块（这是 B5 的工作）。

### 需求追踪

- demand `2026-04-28-49` 阶段 B3 标记为 `[done]`；下一里程碑为 B4
  （`polyrt_println` 调用点的 AMD64 代码生成 + Win64 ABI 影子空间）。

---

## v1.5.3 (2026-04-28)

**Ploy 前端新增 `PRINTLN "字面量";` 语句——运行时标准输出管线 B2 阶段（demand `2026-04-28-49`）。**

### 语言层

- 新增顶层 / 块内语句：`PRINTLN STRING ';'`，将一个字符串字面量原样写入宿主标准输出。
  这是有意为之的最小运行时-IO 原语；表达式、字符串拼接与格式化将在后续阶段陆续登场。
  末尾的 `';'` 是必填项。
- 字面量字节会按原样存放到 AST 节点上——前后双引号会被剥除，
  但反斜杠转义（`\r`、`\n`、`\t`、`\\`、`\"` 等）**不**由前端解码。
  唯一的标准解码器将在代码生成阶段（B4）实现，从而保证解释器、IR 文本回环
  以及 `.rdata` 段三方对同一份字面量的诠释完全一致。
- `PRINTLN` 关键字遵循与其它 Ploy 关键字一致的大小写不敏感规则
  （`println`、`Println`、`PRINTLN` 等价），承袭 v1.5.2 引入的词法折叠机制。

### 前端管线

- `frontends/ploy/include/ploy_ast.h` —— 新增 `PrintlnStmt : Statement`
  结构，仅含一个 `std::string message` 字段。
- `frontends/ploy/src/lexer/lexer.cpp` —— 将 `"PRINTLN"` 加入规范关键字集合。
- `frontends/ploy/src/parser/parser.cpp` —— `ParseTopLevel` 与
  `ParseStatement` 两处分发位置都把新关键字交给新增的
  `ParsePrintlnStatement()` 助手；该助手生成 `PrintlnStmt`，
  若其后不是字符串则报告诊断并触发 `Sync()` 恢复。
  `Sync()` 的恢复关键字集合也已加入 `PRINTLN`。
- `frontends/ploy/src/sema/sema.cpp` —— `AnalyzeStatement` 将
  `PrintlnStmt` 视为无操作；解析器已经强制了形态，且空字符串是合法的。

### 测试

- `tests/unit/frontends/ploy/println_stmt_test.cpp`（6 用例 / 37 断言）：
  顶层解析、转义字节保留、关键字大小写不敏感、`ParseStatement` 分发、
  非字符串操作数错误恢复、空消息边界条件下的语义接受。
- Ploy 前端整套测试保持全绿（305 用例 / 1886 断言）。
- 链接器（43/245）、端到端（54/171）与集成（129/547）回归套件继续通过，
  行为无任何变化——`PrintlnStmt` 在降级阶段仍被忽略，直到 B3 将其接入 IR。

### 兼容性

- `PRINTLN` 此前会被识别为标识符；任何把它当作变量/函数/管线名称的程序
  需要改名。捆绑的样例与测试夹具均未使用此名。
- 无任何公共 API 被移除；`PrintlnStmt` 完全是新增内容。

### 需求追踪

- demand `2026-04-28-49` 阶段 B2 标记为 `[done]`；下一里程碑为 B3
  （将 PRINTLN 降级为 IR）。

---

## v1.5.2 (2026-04-28)

**Ploy 词法层清理——关键字大小写不敏感、`RETURNS` 子句弃用、`AND` / `OR` / `NOT` 正式成为 `&&` / `||` / `!` 的别名。**

### 词法规则

- Ploy 词法器现在以任意大小写接受所有保留字（`link`、`Link`、
  `LINK`、`LiNk` 都识别为同一个 LINK 关键字）。`Token::lexeme`
  始终被规范化为标准的 UPPER 大写拼写，使语法分析器无需逐次折叠
  即可保持单一字符串比较的写法；用户在源码中真正写下的拼写则被
  保留在新增的 `Token::raw_lexeme` 字段上，并通过
  `Token::SourceText()` 暴露，让诊断信息和"忠于源码"的格式化器
  仍然可以打印出用户原本写下的字面量。
- **标识符仍然是大小写敏感的**：只有关键字集合参与折叠，这是
  为了保持语法分析逻辑统一、诊断可执行的有意权衡。
- **保留字冲突。** 过去仅在大小写上与关键字不同的标识符
  （`config`、`array`、`get`、`set`、`pipeline`、`new` ……）
  现在变成保留字。请将其迁移到非关键字命名（如 `app_config`、
  `np_array`、`getter`）。本仓库内的单元 / 集成 / 基准测试
  fixture 均已同步更新；`tests/samples/` 下面向用户的样例本就
  使用 UPPER 关键字，无需改动。

### 运算符别名

- `AND` / `OR` / `NOT` 关键字解析得到的 AST 与符号形式 `&&` /
  `||` / `!` **完全相同**（`BinaryExpression::op == "||"`，
  `UnaryExpression::op == "!"`），下游阶段无须按拼写分支。
- 文档将符号形式登记为新代码的推荐风格，但两种写法都长期保留。

### `RETURNS` 弃用

- 遗留的 `LINK(...) RETURNS Type { ... }` 写法仍然能解析并照常
  填入 `LinkDecl::return_type`，但语法分析器现在会发出
  `kDeprecatedKeyword` 警告（`ErrorCode = 3024`），警告文本中
  会回显用户源码中真实写下的拼写，方便在源码树中 grep。
- 新代码请通过 LINK 签名上的标准 `-> Type` 箭头来声明返回类型。
  我们不做自动改写，警告本身就是迁移信号。

### 兼容性

- 现有 `tests/samples/01_basic_linking..16_*` 程序仍按既有方式
  解析、语义分析与下沉——它们本就使用 UPPER 关键字。
- 共享的 `frontends/common::Token` 结构体只在 **末尾** 新增了
  一个 `raw_lexeme` 字符串字段，因此 C++/Java/Python/Rust/
  .NET/JavaScript 各前端中所有三参 / 四参聚合初始化的调用点
  都不需要改动即可继续编译。
- 新增的 `frontends::ErrorCode::kDeprecatedKeyword = 3024`
  位于 `kSignatureMissing = 3023` 与 `kGenericWarning = 3099`
  之间，属于非致命警告类别。

### 测试

- `tests/unit/frontends/ploy/lexer_case_insensitive_test.cpp`
  覆盖 56 个关键字在 lower / mixed / UPPER 各拼写下的识别，
  验证标识符仍然大小写敏感，并验证以关键字为前缀的较长标识符
  （`letter`、`iffy`、`linker` ……）不会被切分。
- `tests/unit/frontends/ploy/keyword_alias_test.cpp` 证明
  `AND/OR/NOT` 与 `&&/||/!` 产生结构上完全相同的 AST，并验证
  `RETURNS` 子句对 UPPER 与 lower 两种输入都恰好发出一条
  `kDeprecatedKeyword` 警告，警告文本中回显用户的源码拼写。

---

## v1.5.1 (2026-04-28)

**PE32+ 写入器接入运行时 IO ——产出的 `.exe` 现在能在退出前向标准输出写入数据。**

v1.5.0 的 PE 写入器只能产出"入口直接调用 `kernel32!ExitProcess(0)`"
的自终止映像。真实样例至少需要在退出前驱动一次用户可见的副作用，
本补丁因此把写入器扩展到能对宿主标准输出句柄做一次同步 `WriteFile`
所需的最小表面。

本次落地：

- **`BuildPE32PlusImage` 支持可选 `.rdata` 节。**`BuildRequest` 新增
  `rdata_bytes` 字段，`BuildResult` 通过 `rdata_rva` 把该节的 RVA
  回送给调用方。当 `rdata_bytes` 非空时写入器布局为
  `[.text][.rdata][.idata]`（3 节）而不是 `[.text][.idata]`（2 节），
  `SizeOfInitializedData` 按 PE/COFF 规范累加 `.rdata` 与 `.idata`
  的对齐后大小。

- **新工厂 `BuildHelloWorldPE(message)`**（位于
  `tools/polyld/include/pe_writer.h`）。产出可直接运行的 PE32+，
  入口顺序执行 AMD64 序列
  `GetStdHandle(STD_OUTPUT_HANDLE) → WriteFile(handle, msg, len, &n, NULL)
  → ExitProcess(0)`。68 字节入口 shim 大小固定，
  使 `.rdata`、`.idata` RVA 在代码生成前即可完全确定。
  `sub rsp, 0x38` 同时分配 32 字节阴影空间、`[rsp+0x20]` 处的
  WriteFile ARG5 槽位与 `[rsp+0x28]` 处的字节数 DWORD 槽位，
  并在每次子 `CALL` 之前保持 Win64 ABI 要求的 16 字节栈对齐。

- **3 函数 import 通路全程实测。**`BuildImportSection` 早已支持每个
  DLL 的 N 个函数；本次以
  `kernel32.dll!{GetStdHandle, WriteFile, ExitProcess}` 走通该路径，
  并验证每个 IAT 槽 RVA 都通过 `BuildResult::iat_slot_rva` 回送。

- **`pe_smoke` 哨兵程序扩展**：除原有最小退出映像外，再构建、写盘、
  spawn 并校验一份 hello-world 映像。

- **新单元测试**位于 `tests/unit/linker/pe_writer_test.cpp`：
  3 节布局断言、3 个 kernel32 import 的 IAT 槽枚举、
  `.rdata` 负载在磁盘上的逐字节保留、shim prologue（`48 83 EC 38`）
  与尾部 `int3`，以及对 `BuildPE32PlusImage` 的空 `text_bytes` 拒绝守卫。

- **新集成测试**位于 `tests/integration/pe_runtime_smoke_test.cpp`：
  在进程内构建 hello-world PE，写入临时路径，
  通过 `cmd /c` 重定向 stdout 到临时 `.out` 文件后 spawn，
  并断言退出码与捕获字节等于注入消息。
  重定向命令需要再裹一层外引号——`std::system` 会转发给
  `cmd /c <string>`，cmd 在重新解析前会剥掉一层引号。

回归：`test_linker` 43/43（245 断言）、
`integration_tests` 129/129（547 断言）、
`test_e2e` 54/54（171 断言）。`Linker` 公共 API 未变；
新增的 pe_writer 表面纯属增量。

尚未落地：`polyc → polyld` 流水线对 `.ploy` 输入仍向
`BuildExitZeroPE` 传递空 `.text`，因此 `polyc hello.ploy -o hello.exe`
当前产出的是退出零映像而非 hello-world 映像。把
`BuildHelloWorldPE`（以及更通用的运行时-IO 发射器）接到前端是
多阶段运行时-stdout 路线图的下一个里程碑。

---

## v1.5.0 (2026-04-28)

**原生 Windows PE32+ 可执行文件生成 —— `polyc tiny.ploy -o tiny.exe` 终于能产出能跑的 `.exe`**

在本次发布之前，内置的 `polyld` 始终输出 ELF 格式的可执行文件，
即便 `-o` 后缀是 `.exe`、即便宿主就是 Windows。结果是一个 423 字节、
内部却是 ELF 的 `tiny.exe`，Windows 加载器拒绝执行（`'tiny.exe' 不是
有效的 Win32 应用程序`）。想拿到能跑的 `.exe` 必须装 MSVC `link.exe`
或 LLVM `lld-link`，依赖 v1.4.1 引入的“先探测后调用”回退；宿主上完全
没有系统链接器时则毫无办法。

本次发布在仓内自带一个 PE32+ 镜像写入器，让自带的 `polyld` 能端到端
产出真正的 Windows AMD64 控制台可执行文件：

- 新增模块 `tools/polyld/{include,src}/pe_writer.{h,cpp}` 完整布局
  PE32+ 镜像：64 字节 DOS 头加常规 stub、`PE\0\0` 签名、COFF File
  Header（Machine = `IMAGE_FILE_MACHINE_AMD64`，
  `Characteristics` = `RELOCS_STRIPPED | EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE`），
  240 字节的 PE32+ 可选头（Magic = `0x020B`、Subsystem = Windows
  控制台、`DllCharacteristics` = `NX_COMPAT | TERMINAL_SERVER_AWARE`、
  16 个数据目录）、节表、对齐填充后的头部，以及 `.text`（RX）与
  `.idata`（RW）两节。`.idata` 内含 `IMAGE_IMPORT_DESCRIPTOR` 数组、
  ILT、IAT、`IMAGE_IMPORT_BY_NAME` 记录与 DLL 名字串，并把数据目录
  `[1]`（Import）与 `[12]`（IAT）正确指向各自 RVA。
- 新增入口跳板生成器 `BuildExitProcessShim()`，输出标准 13 字节
  AMD64 序列：
  `48 83 EC 28  31 C9  FF 15 disp32  CC`
  （`sub rsp, 0x28; xor ecx, ecx; call qword ptr [rip+disp32]; int3`）。
  `sub rsp, 0x28` 同时完成 32 字节 shadow space 预留与 16 字节栈对齐，
  这是 Windows x64 ABI 调用任意导入函数前的强制要求。
- 新增便捷封装 `BuildExitZeroPE(user_text_bytes)`，把调用方传入的
  `.text` 字节缓冲与跳板拼成完整镜像。用户字节仍嵌入磁盘镜像
  （便于后续把 `AddressOfEntryPoint` 重新指向真正的 `main`，等 Win32
  ABI 翻译层就绪后），但当前入口仍是跳板，保证镜像通过
  `kernel32!ExitProcess(0)` 干净退出。
- 链接器枚举新增 `OutputFormat::kPEExecutable`，对应实现
  `Linker::GeneratePEExecutable()` 取出合并后的 `.text` 输出节字节，
  经 `BuildExitZeroPE` 包装后写入 `config_.output_file`。
- `polyld` 在 `-o` 以 `.exe` 结尾时（大小写不敏感）自动选择
  `kPEExecutable`；Windows 宿主上即使没有 `.exe` 后缀也默认 PE。
  显式开关也已加入：`--pe` / `--output-format=pe` 与
  `--elf` / `--output-format=elf`。

合入前已通过的质量门禁：

- 新增 Catch2 单元用例集
  `tests/unit/linker/pe_writer_test.cpp`（4 个用例、47 个断言），
  覆盖 MZ/PE magic、e_lfanew、COFF Characteristics、可选头 Magic、
  AddressOfEntryPoint、Subsystem、`DllCharacteristics`、16 个数据目录、
  Import + IAT 目录非空、13 字节跳板形状、`BuildExitZeroPE` 对用户
  `.text` 字节的回环嵌入，以及 `BuildExitZeroPE({})` 与
  `BuildMinimalExitZeroImage()` 字节级等价。
- 新增 Windows 专用集成用例
  `tests/integration/pe_runtime_smoke_test.cpp`（2 个用例、8 个断言）：
  进程内构建 PE，落到临时文件，用 `std::system` 启动子进程，
  断言退出码为 0；分别测试最小镜像与包装了 256 字节用户代码的镜像。
- 端到端复现：`polyc tests/samples/01_basic_linking/basic_linking.ploy
  -o tests/samples/01_basic_linking/test_bin.exe` 现在产出 1536 字节
  的 PE32+（`MZ` magic、`dumpbin /imports` 显示
  `kernel32.dll: ExitProcess` 已正确解析），退出码 0。
- 既有回归保持绿色：`test_linker` 39/39，`test_e2e` 54/54。

本轮已知限制（已纳入后续版本规划）：

- 暂未生成基址重定位表。设置了 `IMAGE_FILE_RELOCS_STRIPPED`，因此
  加载器会按首选 `ImageBase`（`0x140000000`）加载。
- 暂无导出表、资源表、TLS 表与调试目录。
- `AddressOfEntryPoint` 仍指向尾部跳板；用户的 ELF 风格 `.text`
  字节虽已嵌入磁盘但暂未被调用。把入口重新指向真正的 `main` 会与
  COFF 对象加载器以及 Win32 ABI 翻译层一并落地。

## v1.4.1 (2026-04-28)

**链接器“先探测后调用”——消除原始 shell 噪声**

此前，`polyc` 的分阶段编译流水线与遗留单遍驱动都通过 `std::system()`
无条件调用 `link.exe`，失败后再调用 `lld-link`。当两者都不在
`PATH` 上时（用户未在 MSVC“开发者命令提示符”中运行 `polyc` 时的常见情况），
Windows CMD 自身会向 `stderr` 输出
`'link' 不是内部或外部命令，也不是可运行的程序或批处理文件。`，
即便流水线整体报告成功且 `.obj` 已正确产出，原始 shell 噪声仍会泄露到
`polyc` 的输出中。

- ✅ 新增共享模块 `tools/polyc/include/linker_probe.h` +
  `tools/polyc/src/linker_probe.cpp`，导出
  `polyglot::tools::linker_probe::{IsExecutableOnPath, ShellQuote,
  LinkerChoice, SelectAvailableLinker, ExpandLinkCommand}`。
  探测使用 `where`（Windows）/ `command -v`（POSIX），并将
  `stdout`、`stderr` 同时重定向到平台空设备，因此探测过程本身完全静默。
- ✅ 按目标格式排序的候选优先级：
  - `pobj`  → 内置 `polyld`（标准消费者；无原生回退）；
  - `coff`  → MSVC `link` → LLVM `lld-link` → 内置 `polyld`；
  - `macho` → `clang` → `ld` → 内置 `polyld`；
  - `elf`   → `clang` → `gcc` → `ld` → 内置 `polyld`。
  内置 `polyld` 之所以能作为通用回退，是因为其加载器
  （`tools/polyld/src/linker.cpp`）会按魔数识别 COFF / ELF / Mach-O
  输入并产出对应原生可执行文件，从而保证只装了 Polyglot 工具链的主机
  上 `polyc` 始终能成功链接。
- ✅ 两个调用点统一改用新模块：
  `tools/polyc/src/compilation_pipeline.cpp`（分阶段流水线
  `mode == "link"` 分支）以及 `tools/polyc/src/stage_packaging.cpp`
  （遗留单遍驱动 `emit_and_link` lambda）。
- ✅ Verbose 模式现在记录的是真正被选中的链接器：
  `[polyc] Invoking polyld (fallback) -> a.out`（之前即便从未真正
  调用 `link.exe`，也会硬编码输出 `Invoking link.exe`）。
- ✅ `ShellQuote` 用平台对应的引用规则处理含空格路径
  （Windows 用 `"…"`；POSIX 用 `'…'`，对内嵌单引号采用 `'\''` 转义）；
  不含空格的路径原样透传。
- ✅ 新增单元测试 `tests/unit/tools/linker_probe_test.cpp`
  （7 个用例、23 个断言）：覆盖 `ShellQuote` 引用规则；空名/伪造名
  的拒绝；现存绝对路径走 stat 快路径的接受；`pobj` 在 `polyld` 伪造
  时返回空 `LinkerChoice`；`pobj` 在 `polyld` 可达时返回非空 `LinkerChoice`；
  以及 `ExpandLinkCommand` 占位符替换与 polyld 专属标志门控。
- ✅ 复现命令 `polyc.exe tests/samples/03_pipeline/pipeline.ploy`
  在主机 `PATH` 上没有 `link.exe` / `lld-link.exe` 的情况下，
  现在以退出码 0 结束、输出零条 shell 噪声行；链接阶段自动透明回退到
  内置 `polyld`。

> 不破坏源/ABI：根 `CMakeLists.txt` 的工程版本仍为 **1.4.0**；本条
> 之所以以 v1.4.1 段落形式出现在更新日志，是因为用户可见行为已发生
> 变化（噪声行已消失）。

---

## v1.4.0 (2026-04-28)

**调试信息发射器规范化与系列收尾**

- ✅ `backends/common/src/debug_emitter.cpp` 与 `backends/common/src/dwarf_builder.cpp`
  中全部 12 处 `// Placeholder` / `// (placeholder)` / `// Simplified`
  注释改写为对 DWARF / System V ABI / ELF gABI 契约的正式陈述 —— 每个
  "预留长度，section 关闭时回填"位置均显式引用对应的 `Patch32` 调用点。
- ✅ `DebugLineInfo` 新增第四个字段 `std::uint64_t address{0}`（默认初始化；
  现有的聚合初始化代码无需修改即可继续构建并保持原行为）。
- ✅ `DwarfSectionBuilder::EncodeLineStatements` 现在按行发射显式
  `DW_LNS_advance_pc ULEB128(delta)` 操作码，遵循 `DebugLineInfo::address`；
  当调用方未填写 address 时退化为每行步进 1 —— 行号程序因此严格单调且
  按行可寻址。
- ✅ `kDwLnsAdvancePc` 标准操作码从 `[[maybe_unused]]` 提升为活跃发射。
- ✅ 新增单元测试文件
  `tests/unit/backends/debug_emitter_normalization_test.cpp`（4 个用例）：
  `.debug_info` `unit_length` 回填往返；`.debug_line` `unit_length` +
  `header_length` 回填往返；单调 PC 输入下 `DW_LNS_advance_pc` 出现验证；
  PDB GUID 的 RFC 4122 v4 / variant 1 位合规与熵特性。
- ✅ 中英双语文档 `docs/realization/debug_emitter_normalization.md` /
  `_zh.md`；`api_reference` 新增 §7.14（英文）/ §7.13（中文）。
- ✅ `test_backends`：433 断言 / 62 用例 → **461 / 66**；其余四个测试集不变
  （`test_core` 357/41、`test_middle` 292/80、`test_runtime` 35199/101、
  `test_linker` 171/35）。
- ✅ 收束自 v1.3.3 起开启的为期四周的后端重构系列（RISC-V 后端推迟到
  未来某个次版本）。

## v1.3.7 (2026-04-28)

**`ITargetBackend::EmitBitcode` 默认实现启用**

- ✅ `ITargetBackend::EmitBitcode` 默认实现不再返回 "unsupported" 诊断，
  而是将输入的 `IRContext` 序列化为本项目原生的 polyglot bitcode（与
  `LTOModule` 已在使用的同一份 UTF-8 流），并写入
  `TargetArtifacts::bitcode_bytes`。
- ✅ `LTOModule` 新增 API：`SerializeBitcode()`、
  `DeserializeBitcode(string_view)`、静态 `FromIRContext(ctx, name)`。
  原有 `SaveBitcode` / `LoadBitcode` 重载改为薄包装，输出字节级一致，
  公开签名保持兼容。
- ✅ 三个目标适配器（`x86_target_backend.cpp`、`arm64_target_backend.cpp`、
  `wasm_target_backend.cpp`）将 `Capabilities().emits_bitcode` 由 `false`
  翻为 `true`，仍然使用默认实现，未添加任何 override。
- ✅ 新增单元测试 `tests/unit/backends/emit_bitcode_roundtrip_test.cpp`
  （3 个用例）：空 `IRContext` 往返；三后端 payload 字节相等；基本块 +
  指令 + 操作数 + 入口拓扑保持。
- ✅ `target_backend_registry_test.cpp` 翻牌：原先断言 "unsupported 诊断"
  的用例改为通过 `EmitBitcode` + `DeserializeBitcode` 进行双函数
  `IRContext` 往返。
- ✅ 中英双语文档 `docs/realization/bitcode_emission.md` / `_zh.md`；
  `api_reference` §7.13（英文）/ §7.12（中文）。
- ✅ `tools/polyld` 继续无歧义地消费 LLVM bitcode（`BC\xC0\xDE` 魔数）
  与 polyglot bitcode（`m` 前缀）。

## v1.3.6 (2026-04-28)

**WASM 后端翻译单元拆分**

- ✅ 1500+ 行单文件 `backends/wasm/src/wasm_target.cpp` 沿发射器使用的
  module / types / instructions / runtime 边界拆分为多个用途明确的 TU
  （每个新 TU 控制在团队的单文件大小指南内，仍链接进既有的
  `backend_wasm` 目标，CMake 目标图无需调整）。
- ✅ 公开入口 `WasmTargetBackend::Compile` 与二进制 / WAT 输出字节在拆分
  前后完全相同（由现有的 WASM e2e 用例验证）。
- ✅ 新增 `tests/unit/backends/wasm_split_smoke_test.cpp` 冒烟测试，确认
  各 TU 内的辅助函数仍可从发射器门面访问，且公开 `Capabilities()` 矩阵
  保持不变。

## v1.3.5 (2026-04-28)

**ABI / 重定位模型重写**

- ✅ x86_64、ARM64、WASM 发射器共享同一个 `RelocationKind` 枚举与
  `ABIDescriptor` 结构；移除各后端原有的 ad hoc 重定位枚举。
- ✅ `tests/unit/backends/abi_relocation_test.cpp` 与
  `abi_calling_convention_test.cpp` 以同一套参数化矩阵覆盖三平台上的
  GOT / PLT / PC-relative / 绝对 / 线程本地重定位。
- ✅ ELF / Mach-O / COFF 发射器消费统一描述符；跨格式差异限制在各格式
  写入器内部。

## v1.3.4 (2026-04-28)

**MachineIR 验证器**

- ✅ 新增 `MachineIRVerifier` 库，校验：寄存器使用前缀、栈帧闭合（入口/
  出口平衡）、跨基本块活跃区间一致性、操作数种类与各后端
  `MachineInstrTemplate` 的兼容性。
- ✅ `tests/unit/backends/machine_ir_verifier_test.cpp` 与
  `machine_ir_template_test.cpp` 在手工构造的畸形用例与生产 isel pass
  输出上演练验证器。
- ✅ 验证器通过逐后端开关以可选 pass 的形式参与流水线，现有 CI 配置不受
  影响；未来后端可翻开开关以在发射 bug 上 fail-fast。

## v1.3.3 (2026-04-28)

**后端注册表 —— `ITargetBackend` + `BackendRegistry`**

- ✅ 新增抽象基类 `ITargetBackend`，正式化
  `Capabilities()` / `Compile()` / `EmitAssembly()` / `EmitObject()` /
  `EmitBitcode()` 契约；替换驱动中隐式的鸭子分派。
- ✅ `BackendRegistry` 按 triple、别名、优先级发现并排序后端；CLI
  `--target=` 解析改走注册表，`--target=x86-64` 等别名继续可用，且驱动
  中无需散落 strncmp。
- ✅ 三个适配器（`x86_target_backend.cpp`、`arm64_target_backend.cpp`、
  `wasm_target_backend.cpp`）封装现有发射器；驱动只依赖
  `ITargetBackend`。
- ✅ `tests/unit/backends/target_backend_registry_test.cpp` 覆盖注册、
  别名解析、优先级排序、能力检视，以及默认 `EmitBitcode` 诊断（在 v1.3.7
  默认实现到位后被翻面）。

---

## v1.0.6 (2026-03-19)

**CI 质量闸门**
- ✅ 新增 `.clang-tidy` 配置：bugprone、cppcoreguidelines、modernize、performance、readability 检查，含项目特定排除项
- ✅ 新增 CMake 选项：`POLYGLOT_ENABLE_ASAN`、`POLYGLOT_ENABLE_UBSAN`、`POLYGLOT_ENABLE_COVERAGE`，用于消毒器和覆盖率构建
- ✅ CI `format-check` 任务：通过 `clang-format-17` 强制所有 C/C++ 源文件遵循 `.clang-format` 风格
- ✅ CI `clang-tidy` 任务：使用 `clang-tidy-17` 扫描全部项目 `.cpp` 源文件（不再限制前 50 个）
- ✅ CI `sanitizers` 任务：在 ASan + UBSan 下运行非 benchmark 测试集（`ctest -LE benchmark`），降低噪声
- ✅ CI `coverage` 任务：通过 lcov/gcov 从非 benchmark 测试集收集覆盖率，上传过滤后的报告作为产物
- ✅ CI `benchmark-smoke` 任务：以 fast 模式运行基准测试，捕获性能回退
- ✅ CI 并发控制：取消同一分支/PR 的进行中运行
- ✅ 平台构建现在依赖 `format-check` 闸门先通过
- ✅ 文档更新：README、USER_GUIDE（中英文）添加质量闸门表格和本地使用说明

**跨语言链接闭环**
- ✅ `polyc` 现在在调用 `ResolveLinks()` 之前自动从 LINK 声明和 CALL 描述符合成 `CrossLangSymbol` 条目，解决了链接器有描述符但无符号表可解析的问题
- ✅ 当 sema 已知函数签名可用时，自动填充参数描述符
- ✅ 编译模型文档已更新，反映自动化符号注册流程

**收紧降级路径**
- ✅ `lowering.cpp`：无签名信息的 LINK 桩函数在严格模式（默认）下现在报错，而非静默回退到单个 opaque i64 参数
- ✅ `driver.cpp`：最小 main 合成在严格模式下被阻止；仅在宽松模式 + `--force` 下允许，并输出明确的 DEGRADED BUILD 警告
- ✅ `driver.cpp`：优化后 IR 验证失败在严格模式下现在是硬错误；之前仅在 verbose 模式下静默记录
- ✅ 后端空 section 桩注入已限制在 `--force` + 非严格模式下；添加了一致的 DEGRADED BUILD 消息

**管线重构**
- ✅ 将 `PassManager` 从内部 `.cpp` 类提升为公共头文件 `middle/include/passes/pass_manager.h`
- ✅ `PassManager` 支持 O0/O1/O2/O3 级别，具有命名 `PassEntry` 阶段、自定义 pass 注入和逐函数 verbose 日志
- ✅ `driver.cpp` 优化管线（约 40 行内联 pass 调用）替换为 `PassManager::Build()` + `RunOnModule()` — 仅 5 行调用
- ✅ Pass 管线现在可检查、可扩展，CLI、UI、测试和插件均可复用

**测试解耦**
- ✅ 将单体 `unit_tests` 拆分为 14 个按模块的测试二进制（`test_core`、`test_plugins`、`test_frontend_python`、`test_frontend_cpp`、`test_frontend_rust`、`test_frontend_ploy`、`test_frontend_java`、`test_frontend_dotnet`、`test_frontend_common`、`test_middle`、`test_backends`、`test_runtime`、`test_linker`、`test_e2e`）
- ✅ 每个模块二进制仅链接实际需要的库，减少链接开销并隔离 dylib 故障
- ✅ CTest 现有 19 个测试入口（14 个按模块 + 合并 `unit_tests` + `integration_tests` + 3 个基准目标），带标签（`unit`、`frontend`、`middle`、`backend`、`runtime`、`linker`、`e2e`、`benchmark`）
- ✅ 保留合并 `unit_tests` 目标以保持向后兼容
- ✅ 顶层 `CMakeLists.txt` 添加 `enable_testing()` 使按模块测试可从构建根目录发现

**模块边界修正**
- ✅ 将 `backends/common/` 源码（debug_info、debug_emitter、dwarf_builder、object_file）从 `polyglot_common` 移出，创建独立的 `backend_common` 库
- ✅ `backend_x86_64`、`backend_arm64`、`backend_wasm` 现在依赖 `backend_common` 而非直接依赖 `polyglot_common` 编入的后端源码
- ✅ 消除了 common → backends 的层级反转（正确方向：backends → common）
- ✅ `polyld` CLI 逻辑合并：将 `linker.cpp` 中 170 行重复的 `main` 函数（含 `--ploy-desc`、`--aux-dir`、`--allow-adhoc-link` 等选项）合并到 `main.cpp`，删除 `linker.cpp` 中的重复入口

## v1.0.5 (2026-03-17)

**文档单源化与自动校验**
- ✅ 修复路径引用：README.md、USER_GUIDE.md、USER_GUIDE_zh.md、`setup_qt.sh`、`setup_qt.ps1` 现在正确引用 `tools/ui/setup_qt.*`（原为 `scripts/setup_qt.*`）
- ✅ 修复死链接：USER_GUIDE 中 `plugin_specification.md` / `plugin_specification_zh.md` 链接现在正确解析
- ✅ 修复 `language_spec.md` / `language_spec_zh.md`：运行时桥接头文件路径更新为 `runtime/include/libs/*.h`
- ✅ 修复 `project_tutorial.md` / `project_tutorial_zh.md`：驱动程序路径更新为 `tools/polyc/src/driver.cpp`
- ✅ 新增 `scripts/docs_lint.py`：全面的文档校验工具，包含 9 类检查：
  - DL001：路径引用验证（反引号引用的路径必须存在于仓库中）
  - DL002/DL003：双语配对 — 每个 `*.md` 必须有 `*_zh.md` 对应文件
  - DL004：标题结构同步 — 中英文标题各级数量必须一致
  - DL005：死链接检测 — 相对 Markdown 链接必须可解析
  - DL006：版本一致性 — 各文档中的版本字符串必须一致
  - DL007：孤立文档检测 — 未被 README 或 USER_GUIDE 引用的文档
  - DL008：已发布文档中的 TODO/FIXME 标记
  - DL009：占位文本检测
- ✅ 新增 `scripts/docs_generate.py`：单源文档生成器，使用模板标记（`<!-- BEGIN:section_name -->` / `<!-- END:section_name -->`）；支持 `--apply` / `--check` 模式；从 `docs/_variables.json` 更新测试徽章、Qt 路径、依赖表、版本页脚
- ✅ 新增 `scripts/docs_sync_check.py`：双语同步检查器，比较中英文文档的标题结构和内容长度差异
- ✅ 新增 `scripts/check_include_deps.py`：头文件依赖层级约束检查器（middle→common/ir、backends→frontends、frontends→backends）
- ✅ 新增 `docs/_variables.json`：文档变量单一数据源，包含版本号、测试计数、路径、依赖信息、工具名称 — 消除手动多文件更新
- ✅ 在 README.md 中插入模板标记（`test_badge`、`qt_setup_en`、`dependencies_table`、`version_footer_en`）和 USER_GUIDE 页脚（`version_footer_en`、`version_footer_zh`）
- ✅ CI 集成：在 `.github/workflows/ci.yml` 中添加 `docs-lint` 作业，在每次 push/PR 时运行 `docs_lint.py --ci`、`docs_sync_check.py --ci` 和 `docs_generate.py --check`
- ✅ 测试徽章从 813 → 808 更新（准确计数）；版本页脚更新为 v1.0.4 / 2026-03-17
- ✅ 808 单元测试全部通过（34085 断言）；51/52 集成测试通过

## v1.0.4 (2026-03-17)

**插件系统与 UI 可扩展性**
- ✅ 插件宿主回调完整实现：`emit_diagnostic` 将诊断转发到 IDE 输出面板并触发 `DIAGNOSTIC` 事件；`open_file` 委托给已注册的 `OpenFileCallback`（连接到 IDE 标签管理器）；`register_file_type` 填充中央文件类型注册表
- ✅ 事件订阅系统：插件可通过 `subscribe_event` / `unsubscribe_event` 宿主服务订阅 8 种事件类型（`FILE_OPENED`、`FILE_SAVED`、`FILE_CLOSED`、`BUILD_STARTED`、`BUILD_FINISHED`、`DIAGNOSTIC`、`WORKSPACE_CHANGED`、`THEME_CHANGED`）
- ✅ `PluginManager::FireEvent()`：线程安全分发 — 在锁内快照订阅者，在锁外投递事件以避免重入死锁
- ✅ 文件类型注册表：`RegisterFileType()` / `GetLanguageForExtension()` / `GetRegisteredFileTypes()`
- ✅ 菜单贡献系统：`RegisterMenuItem()` / `UnregisterMenuItem()` / `GetMenuContributions()` / `ExecuteMenuAction()`
- ✅ 新增 `ActionManager` 类：从 MainWindow 提取的集中化动作/快捷键管理
- ✅ 新增 `PanelManager` 类：管理底部面板标签页，支持插件贡献面板
- ✅ MainWindow 重构：面板切换/显示逻辑委托给 `PanelManager`；快捷键管理委托给 `ActionManager`
- ✅ 12 个新单元测试：事件订阅/取消/分发、文件类型注册、菜单贡献等
- ✅ 测试计数：796 → 808 测试用例；断言：34056 → 34085 — 全部通过

## v1.0.3 (2026-03-17)

**测试质量提升**
- ✅ 将 `lto_test.cpp` 中的 `REQUIRE(true)` 替换为优化器统计行为断言（空模块所有统计为 0，模块经历所有优化阶段后仍存在）
- ✅ 将 `gc_algorithms_test.cpp` 中的 `REQUIRE(true)` 替换为 GC 统计验证：多周期测试 `collections >= 10`、`total_allocations >= 500`；增量回收 `total_allocations >= 200`
- ✅ 将注释掉的性能基准测试替换为真实吞吐量断言（`total_allocations >= 10000`、`collections >= 1`）
- ✅ 将 `java_test.cpp` 和 `dotnet_test.cpp` 中的 `SUCCEED()` 替换为语义分析行为检查（验证诊断信息为类型映射相关，而非崩溃）
- ✅ 将 `threading_services_test.cpp` 中的 `REQUIRE(true)` 替换为基于原子变量的屏障阶段跟踪和大工作负载精确求和验证
- ✅ 增强 Java/DotNet 枚举和结构体降级测试，添加正向诊断状态断言
- ✅ 新增 `compilation_behavior_test.cpp`：15 个行为级和失败路径测试用例：
  - C++ 单函数/多函数 IR 输出正确性
  - C++ 条件语句产生多个基本块
  - x86_64 汇编包含函数标签和 `ret` 指令
  - x86_64 目标代码包含 `.text` 节且有非零字节和函数符号
  - ARM64 汇编输出包含函数标签
  - IR 验证通过格式正确的 IR；检测到格式错误（空函数）的 IR
  - Ploy `LINK` 产生正确的跨语言调用描述符
  - Ploy `FUNC` 产生具名 IR 函数
  - 失败路径：解析错误诊断、未定义变量语义错误、降级无效代码返回失败
- ✅ 测试用例数：781 → 796；断言数：3985 → 34056 — 全部通过

## v1.0.2 (2026-03-17)

**包发现重构**
- ✅ 新增 `CommandResult` 结构体，提供结构化的命令执行结果（标准输出、退出码、超时标志）
- ✅ `ICommandRunner` 新增 `RunWithResult()` 接口，`Run()` 保留并委托至 `RunWithResult()`
- ✅ `DefaultCommandRunner` 构造函数支持可配置默认超时（基于 `std::async` + `std::future::wait_for`）
- ✅ 新增 `PackageIndexer` 类——在语义分析之前运行的显式包索引阶段
- ✅ `PackageIndexer` 支持超时、重试、进度回调和统计信息收集
- ✅ `polyc` 驱动程序新增 Phase 2.5：根据 AST 导入声明扫描所需语言并运行包索引
- ✅ 新增 CLI 标志：`--no-package-index`（跳过索引）、`--pkg-timeout=<ms>`（设置超时）
- ✅ `PloySemaOptions::enable_package_discovery` 默认值改为 `false`，由预阶段缓存替代
- ✅ 新增 10+ 个测试用例覆盖 `CommandResult`、`PackageIndexer`、超时模拟及预阶段→语义分析流程

**构建系统模块化**
- ✅ 顶层 `CMakeLists.txt` 从约 690 行精简为约 80 行
- ✅ 新增 7 个子目录 `CMakeLists.txt`：`common/`、`frontends/`、`middle/`、`backends/`、`runtime/`、`tools/`、`tests/`
- ✅ 单元测试采用显式逐模块源文件列表（不再使用 `GLOB_RECURSE`）
- ✅ 集成测试和基准测试保留 `GLOB_RECURSE` 以便快速添加
- ✅ Qt6 检测与部署逻辑迁移至 `tools/CMakeLists.txt`
- ✅ 全部 781 个测试用例 / 3985 条断言通过

## v1.0.1 (2026-03-17)
- ✅ 两种显式编译模式：**严格**和**宽松**（`--strict` / `--permissive` CLI 标志）
- ✅ Release 构建默认启用严格模式（通过 `POLYC_DEFAULT_STRICT` 编译定义）
- ✅ 严格模式：语义分析将占位类型回退报告为错误而非警告
- ✅ 严格模式：IR 验证器拒绝包含未解析占位 I64 返回类型的函数
- ✅ 严格模式：降级阶段拒绝返回类型未知的跨语言调用
- ✅ 严格模式：禁止通过 `--force` 生成降级桩；`--strict` 与 `--force` 互斥
- ✅ 宽松模式：所有占位类型回退以警告形式可见（此前为静默处理）
- ✅ `ReportStrictDiag()` 辅助方法：根据模式将诊断路由为错误（严格）或警告（宽松）
- ✅ `IRType::is_placeholder` 标志区分真实 I64 与未解析回退 I64

## v1.0.1 (2026-04-09)

**Staged 编译流水线 与 polyui 图标内嵌**
- ✅ 在 `tools/polyc/src/compilation_pipeline.cpp` 落地 `.ploy` 六阶段编译主链实现
- ✅ 阶段拆分明确并以结构化数据传递：`frontend -> semantic db -> marshal plan -> bridge generation -> backend -> packaging`
- ✅ 新增管道编排执行器（`CompilationPipeline::RunAll()` 及逐阶段运行函数），并记录阶段耗时
- ✅ `polyc` 驱动已将 `.ploy` 主路径切换为 staged pipeline 执行
- ✅ staged 语义阶段接入 `PackageIndexer` + 共享 `PackageDiscoveryCache` 的包索引流程
- ✅ bridge generation 阶段接入 `PolyglotLinker`，并在后端发射前注入已解析桥接桩
- ✅ packaging 阶段支持确定性 `.pobj` 输出，并在 `link` 模式可选调用 `polyld` 完成链接
- ✅ `polyui` 的 Windows 可执行文件现通过 CMake 资源脚本（`tools/ui/windows/polyui.rc`）嵌入 `tools/ui/common/resources/icon.ico`，生成的 `polyui.exe` 默认带项目图标
- ✅ `polyui` 现已显式设置运行时标题栏/窗口图标：Windows 使用内嵌 `:/icons/icon.ico`，Linux/macOS 使用内嵌 `:/icons/icon.png`

> 说明：上方存在两条互相独立的 v1.0.1 条目，因为项目在 2026 年 3 月 / 4 月期间
> 并行运行了两条维护分支，二者均以同一补丁号发布，随后才开启 v1.1.x 序列。

## v1.0.0 (2026-03-15)
- ✅ 项目版本统一为 **1.0.0**（所有原 v5.x/v4.x 版本号重命名为 v0.5.x/v0.4.x）
- ✅ 三平台发布打包脚本（`scripts/package_windows.ps1`、`scripts/package_linux.sh`、`scripts/package_macos.sh`）
- ✅ Windows：免安装 ZIP 压缩包 + NSIS 安装程序（`scripts/installer.nsi`），支持 PATH 注册和开始菜单快捷方式
- ✅ Linux：免安装 `.tar.gz`，自动打包 Qt 库和启动包装脚本
- ✅ macOS：免安装 `.tar.gz`，集成 `macdeployqt` 生成 `.app` 包
- ✅ 打包文档（`docs/specs/release_packaging.md` / `_zh.md`）
- ✅ 版本号已更新至 CMakeLists.txt、所有工具源文件、README.md 和 USER_GUIDE（中英文）

## v0.5.5 (2026-03-15)
- ✅ 统一主题系统：通过 `ThemeManager` 单例管理 — 4 套内置配色方案（暗色、亮色、Monokai、Solarized Dark）；所有面板（主窗口、构建、调试、Git、设置）从统一来源应用样式
- ✅ 编译与运行（`Ctrl+R`）：基于 QProcess 的工作流，编译当前文件、定位输出二进制文件、启动并将 stdout/stderr 实时输出至日志面板
- ✅ 停止（`Ctrl+Shift+R`）：终止运行中的进程并取消活跃构建
- ✅ 调试面板：完整的调用栈帧解析，同时支持 lldb 和 GDB/MI 输出（模块、函数、文件、行号）；变量类型/值提取；监视表达式求值并实时更新结果
- ✅ 调试面板：监视右键菜单（删除 / 全部删除 / 求值）连接至 `OnRemoveWatch` 和 `OnEvaluateWatch`
- ✅ 调试面板：支持从设置中配置调试器路径和入口断点选项
- ✅ 自定义快捷键：在设置 → 键绑定页面中通过 `QKeySequenceEdit` 编辑；28 个默认操作；自定义快捷键通过 `QSettings` 持久化并在启动时加载
- ✅ 设置联动：CMake 路径、构建目录、调试器路径自动同步至 `BuildPanel` 和 `DebugPanel`
- ✅ 新增源文件 `theme_manager.cpp` / `theme_manager.h` 至 `tools/ui/common/`

## v0.5.4 (2026-03-11)
- ✅ 基于 Qt 的桌面 IDE（`polyui`）：语法高亮、实时诊断、文件浏览器、多标签编辑器、输出面板、括号匹配、暗色主题
- ✅ IDE 使用编译器前端分词器实现精确的、语言感知的高亮，支持全部 6 种语言
- ✅ IDE 快捷键：Ctrl+B 编译、Ctrl+Shift+B 分析、Ctrl+N/O/S/W 文件管理
- ✅ CMake Qt6/Qt5 自动发现：优先使用 `D:\Qt` 下的独立 Qt 安装，不再引用 Anaconda 自带的 Qt；可通过 `-DQT_ROOT=<path>` 覆盖
- ✅ 默认 `ninja` 构建现在生成所有可执行文件（polyc、polyld、polyasm、polyopt、polyrt、polybench、polyui）
- ✅ `polyui` 源码按平台分离：共享代码位于 `tools/ui/common/`，平台专用入口点位于 `tools/ui/windows/`、`tools/ui/linux/`、`tools/ui/macos/`；CMake 根据操作系统自动选择正确的 `main.cpp`
- ✅ macOS 支持：`MACOSX_BUNDLE` + `macdeployqt` 后编译部署；Linux 支持：`xcb` 平台默认值、`.desktop` 集成
- ✅ IDE 内置终端：嵌入式 Shell（PowerShell/bash/zsh），ANSI 颜色解析，命令历史，多实例，`` Ctrl+` `` 切换
- ✅ 清理旧目录 `tools/ui/ployui_windows/`、`tools/ui/src/`、`tools/ui/include/`
- ✅ 全项目文档审查与统计数据刷新 — 293 个源文件，91,457 行代码
- ✅ 测试增长：743 单元（原 734）+ 52 集成（原 50）+ 18 基准 = **813 总计**（原 802）
- ✅ Ploy 前端扩展至 6 个编译单元（新增 `command_runner.cpp`、`package_discovery_cache.cpp`）
- ✅ 更新所有文档（README、USER_GUIDE 中英文、教程）至最新统计数据

## v0.5.3 (2026-02-22)
- ✅ 解除 `common` ↔ `middle` 循环依赖 — 规范 IR 头文件移至 `middle/include/ir/`；`common/include/ir/` 保留转发垫片保持向后兼容
- ✅ 新增 CI include-lint 脚本（`scripts/check_include_deps.py`）强制层级约束：`middle/` 不可包含 `common/include/ir/`，后端不可包含前端
- ✅ 统一调试信息建模 — 新增 `common/include/debug/debug_info_adapter.h`，通过 `ConvertToBackendDebugInfo()` 桥接 `polyglot::debug`（丰富 DWARF 模型）与 `polyglot::backends`（扁平发射模型）
- ✅ 新增优化管线与开关矩阵文档（`docs/specs/optimization_pipeline.md` / `_zh.md`）
- ✅ 新增运行时 C ABI 参考文档（`docs/specs/runtime_abi.md` / `_zh.md`）
- ✅ 统一工具层命名空间：`polyc` 和 `polybench` 现使用 `namespace polyglot::tools`，与 `polyasm`/`polyopt`/`polyrt` 一致

## v0.5.2 (2026-02-22)
- ✅ 新增 `PloySemaOptions` 配置结构，支持 `enable_package_discovery` 开关
- ✅ `PloySema` 构造函数接收 options，保持默认行为向后兼容
- ✅ 会话级 `PackageDiscoveryCache` — 线程安全，键为 `language|manager|env_path`
- ✅ `ICommandRunner` 抽象 — 外部命令执行与 `_popen` 解耦；支持测试 Mock
- ✅ `DiscoverPackages` 缓存优先流程 — 命中→合并缓存结果；未命中→执行+存储
- ✅ 基准测试套件禁用包发现，隔离编译器本体性能
- ✅ Lowering 微基准计时修正 — 解析/语义分析排除在计时窗口外
- ✅ 基准测试 fast/full 档位通过 `POLYBENCH_MODE` 环境变量控制（`fast`/`full`/默认）
- ✅ CTest: `benchmark_fast` 和 `benchmark_full` 目标及标签
- ✅ 发现子系统单元测试：禁用无命令、重复分析仅一次、键隔离、缓存往返

## v0.5.1 (2026-02-22)
- ✅ 链接器强失败模式 — 未解析符号为硬错误；不再为未解析对生成占位存根
- ✅ 链接器主流程：存在跨语言条目但解析失败时直接报错终止
- ✅ `polyc` 和 `polyasm` 现支持 `--arch=wasm` WebAssembly 目标
- ✅ WASM 后端：通过名称→索引映射实现真实函数调用索引解析（移除硬编码索引）
- ✅ WASM 后端：alloca 降级为影子栈模型（可变 i32 全局变量，从 65536 向下增长）
- ✅ WASM 后端：不支持的 IR 指令发射 `unreachable` + 诊断错误（不再静默 NOP）
- ✅ `.ploy` 语义分析严格模式（`SetStrictMode(true)`）— 对无类型参数、`Any` 回退、缺少注释发出警告
- ✅ `.ploy` lowering 在 I64 回退前先查询语义符号表；使用 `KnownSignatures` 获取返回/参数类型
- ✅ 调试发射器：FDE 现包含正确的 CFA 指令（push rbp / mov rbp,rsp 帧建立）
- ✅ 调试发射器：PDB TPI 流发射指针类型的 LF_POINTER 类型记录
- ✅ Mach-O 目标文件：计算并写入每节 `reloff`/`nreloc`；发射 relocation_info 条目
- ✅ Rust 和 Python 前端 lowering：未知类型 → I64 回退时发出诊断警告
- ✅ Rust advanced_features_test.cpp：所有 `REQUIRE(true)` 替换为行为性解析 + AST 断言
- ✅ E2E 测试：新增 ARM64 目标代码发射测试和 WASM 多函数二进制 smoke 测试

## v0.5.0 (2026-02-22)
- ✅ 全面项目文档更新 — 所有文档刷新以反映当前状态
- ✅ 新增 WebAssembly (WASM) 后端 (`backends/wasm/`)
- ✅ 统一 DWARF 构建器编入 `polyglot_common` 库
- ✅ PDB 发射：MSF 块布局、TPI 类型记录 (LF_ARGLIST/LF_PROCEDURE)、RFC 4122 v4 GUID
- ✅ CFA 初始化含寄存器保存规则和 NOP 对齐
- ✅ ELF：e_machine 架构切换 (x86_64/ARM64)，完整 SHT_RELA 重定位节
- ✅ Mach-O：完整 LC_SEGMENT_64、LC_SYMTAB、nlist_64 符号表、段映射
- ✅ IR 解析器/打印器对称：`fadd/fsub/fmul/fdiv/frem` 解析 + global/const 声明
- ✅ 预处理器测试覆盖：18 个宏、指令、Token池 专用测试
- ✅ 调试测试 Windows 兼容：`TmpPath()` 使用 `std::filesystem::temp_directory_path()`
- ✅ 跨语言链接主链路完全连通：polyc → PolyglotLinker → 粘合代码 → 二进制
- ✅ 所有 6 个前端真实 IR lowering（无回退桩）
- ✅ E2E 测试启用：29 个端到端测试
- ✅ 链接器完整：COFF/PE、ELF、Mach-O 全部实现
- ✅ polyopt：读取 IR 文件并运行优化管道
- ✅ polybench：完整基准套件（编译 + E2E）
- ✅ polyrt：FFI 子命令、真实 GC/线程统计
- ✅ 教程文档新增 (`docs/tutorial/`)
- ✅ 16 个示例程序（原 12 个）
- ✅ 总计：813 个测试用例，3 个测试套件（743 单元 + 52 集成 + 18 基准）

## v0.4.3 (2026-02-20)
- ✅ 新增跨语言对象销毁 `DELETE` 关键字
- ✅ 新增跨语言类继承扩展 `EXTEND` 关键字
- ✅ 增强诊断基础设施：严重性级别、错误代码（1xxx-5xxx）、溯源链、建议
- ✅ 新增语义分析中的参数数量不匹配检查
- ✅ 新增语义分析中的类型不匹配检查
- ✅ 所有错误报告升级为结构化错误代码
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现（DELETE/EXTEND）
- ✅ 36 个新测试用例，总计 207 测试用例、598 断言
- ✅ 关键字数量 52 → 54

## v0.4.2 (2026-02-20)
- ✅ 新增跨语言属性访问 `GET` 和属性赋值 `SET` 关键字
- ✅ 新增自动资源管理 `WITH` 关键字
- ✅ 新增带限定类型的类型注解支持
- ✅ 新增通过 `MAP_TYPE` 进行接口映射
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现（GET/SET/WITH）
- ✅ 31 个新测试用例，总计 171 测试用例、523 断言
- ✅ 中英双语用户指南（USER_GUIDE.md / USER_GUIDE_zh.md）
- ✅ 关键字数量 49 → 52

## v0.4.1 (2026-02-20)
- ✅ 新增跨语言类实例化 `NEW` 和方法调用 `METHOD` 关键字
- ✅ AST / 词法 / 语法 / 语义 / IR Lowering 全链路实现
- ✅ 22 个新测试用例，总计 140 测试用例、443 断言
- ✅ 中英双语功能文档 (`class_instantiation.md` / `class_instantiation_zh.md`)
- ✅ 关键字数量 47 → 49

## v0.4.0 (2026-02-19)
- ✅ 新增 .ploy 跨语言链接前端完整章节
- ✅ 多包管理器支持: CONFIG CONDA / UV / PIPENV / POETRY
- ✅ 版本约束验证（6 种运算符）
- ✅ 选择性导入
- ✅ 117+ 测试用例，361+ 断言
- ✅ 全面审查并重写完整指南（v0.3.0 → v0.4.0）
- ✅ 精确说明编译流程（PolyglotCompiler 自身前端编译，不依赖外部编译器）

## v0.3.0 (2026-02-01)
- ✅ 第 11-15 章（实现分析/测试/高级优化/成就总结）
- ✅ 33+ 优化 passes、4 种 GC 算法
- ✅ PGO/LTO/DWARF5 支持

## v0.2.0 (2026-01-29)
- ✅ 循环优化、GVN、constexpr、DWARF 5

## v0.1.0 (2026-01-15)
- ✅ 基础编译链：3 种前端、2 种后端、基础优化

---

*由 PolyglotCompiler 团队维护*  
*最后更新: 2026-04-28*
