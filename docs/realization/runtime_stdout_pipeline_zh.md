# 运行时标准输出管线

本文档描述把 `.ploy` 中的 `PRINTLN "literal";` 语句变成宿主进程标准输出字节
的端到端管线。该管线由 demand `2026-04-28-49` 跟踪、按 B1–B8 共八个阶段增量
建设；其中 **B1 至 B4** 已发布，其余阶段在此仅作为锚点列出，用于说明已发布
层必须继续遵守的契约。

英文版本见 [`runtime_stdout_pipeline.md`](runtime_stdout_pipeline.md)。

---

## 1. 层次图

```
+------------------+   PRINTLN "hi";       (.ploy 源码)
|  B2  ploy 前端   |
+------------------+
        |  PrintlnStmt AST 节点 + sema 校验
        v
+------------------+
|  B3  ploy 降级   |
+------------------+
        |  IR：@str.<hash> = constant [N x i8] c"..."
        |       call void @polyrt_println(i8* ptr, i64 len)
        v
+------------------+
|  B4  PE 写入器   |    BuildPrintlnSequencePE(call_messages)
+------------------+
        |  AMD64 .text + .rdata 载荷 + .idata kernel32 导入
        v
+------------------+
| Windows 加载器   |   GetStdHandle  +  WriteFile  +  ExitProcess
+------------------+
```

每层独立单元测试，相邻层之间的契约按字节稳定，从而后续层的重定向不会扰动前
面已经发布的层。

---

## 2. B2 — ploy 前端（v1.5.3 发布）

词法层识别关键字 `PRINTLN`；语法层产出携带**原始**字面量字节（保留转义序列）
的 `PrintlnStmt` AST 节点；语义层校验字面量是合法的引号字符串 token，且语句
以 `;` 结尾。

“前端不解码转义”是有意为之的关键决策：IR 降级层（B3）作为解码表
（`\n`、`\r`、`\t`、`\\`、`\"`、`\0`、`\xHH`）的**唯一真源**——解释器、
IR 文本回环、代码生成全都消费已解码的字节。后续若新增转义只需改一处。

---

## 3. B3 — IR 降级（v1.5.4 发布）

`PrintlnStmt` 降级为两件 IR 制品：

1. **interned 全局**：类型 `[N x i8]`，存放*已解码*的消息字节。该全局按内容
   做键，由 `IRBuilder::MakeStringLiteral` 去重，因此同一字面量重复 PRINTLN
   只占一份全局。
2. **直接调用** `call void @polyrt_println(i8* msg, i64 len)`，写入当前基本块。
   被调用者刻意保持外部符号；后续阶段通过链接器的运行时 DLL 导入路径解析。

采用 `(指针, 长度)` 而非 NUL 终结，使得内嵌 `\0` 字节能干净往返，并让空字面量
`PRINTLN "";` 直接降为 `polyrt_println(ptr, 0)`，下游无需特例化。

顶层 `PRINTLN` 落入按既有 `IRContext::DefaultBlock` 约定自动建立的
`entry_fn`；`FUNC` 体内的 `PRINTLN` 落入该函数自身的基本块。

---

## 4. B4 — `BuildPrintlnSequencePE`（v1.5.5 发布）

`polyglot::linker::pe::BuildPrintlnSequencePE(call_messages)` 产出可独立运
行的 PE32+ 镜像：按入参顺序对标准输出句柄依次发起 `kernel32!WriteFile`，
最后通过 `kernel32!ExitProcess(0)` 终止进程。

这是该管线中**首个真正发射 AMD64 机器码驱动 Windows 标准输出**的阶段。复用
了 `BuildHelloWorldPE` 已有的三段式布局（`.text` / `.rdata` / `.idata`）、
导入描述符构造器以及 `BuildPE32PlusImage` 全套管线，因此 v1.5.1 起所有节
头与 IAT 不变量在 PE 写入器测试中继续有效。

### 4.1 Win64 ABI shim 布局

`.text` 载荷布局为：**序言（0x25 字节）** + N × **单条消息块（0x1D 字节）** +
**尾声（0x09 字节）**。所有偏移按字节稳定，单元测试逐字节校验。

| 区域 | 大小 | 作用 |
| --- | --- | --- |
| 序言 | 0x25 字节 | `sub rsp, 0x38` 一次性预留 Win64 强制的 32 字节影子空间 + 8 字节 `lpOverlapped` 槽（清零）+ 8 字节 `lpNumberOfBytesWritten` 槽（清零）+ 8 字节 stdout 句柄缓存。随后 `GetStdHandle(STD_OUTPUT_HANDLE)` 仅调用一次，返回值缓存到 `[rsp+0x30]`。 |
| 单条消息块 | 0x1D 字节 | `mov rcx, [rsp+0x30]; lea rdx, [rip+msg_i]; mov r8d, len_i; lea r9, [rsp+0x28]; call qword ptr [rip+WriteFile]`。所有 RIP 相对位移基于本块自身 RVA 计算，前面无论挂多少消息块都自洽。 |
| 尾声 | 0x09 字节 | `xor ecx, ecx; call qword ptr [rip+ExitProcess]; int3`。`int3` 不可达，仅用于把 shim 长度对齐到确定值。 |

句柄缓存到栈槽而非被调用者保存寄存器，使得序言/尾声无需押栈与弹栈
RBX / R12 / R13——便于维护以上按字节计数的不变量并写单元测试。

### 4.2 去重与 .rdata 布局

字节相同的消息共享同一段 `.rdata` 存储（线性扫描的唯一表），与 B3 的 IR 层
`MakeStringLiteral` interning 契约严格对齐。`PRINTLN "hello\n";` 调用十次
对应十个 WriteFile 块，但 `.rdata` 仅一份。

### 4.3 边界情形

| 入参 | 行为 |
| --- | --- |
| `call_messages.empty()` | 转发 `BuildExitZeroPE({})`，产出字节级一致镜像，绝不会发射退化的空 shim。 |
| 单条消息 ≤ 4 GiB | 行为等价于 `BuildHelloWorldPE`（序言/单块切分不同，可观测的 stdout 与退出码相同）。 |
| 单条消息 > 4 GiB | `BuildResult` 返回空（WriteFile 的 `nNumberOfBytesToWrite` 是 DWORD）。 |
| 重复载荷 | `.rdata` 收缩到唯一字节之和；`.text` 仍按*每次调用*出一个块。 |

### 4.4 测试

* `tests/unit/linker/pe_writer_test.cpp` — 四个 `[pe_writer][println][b4]`
  用例：空入参转发、单消息结构、三消息展开后 `.text` 长度、重复消息触发
  `.rdata` 收缩。
* `tests/integration/pe_runtime_smoke_test.cpp` — 一个
  `[pe_writer][integration][windows][println][b4]` 用例：生成包含一条重复
  消息的 3 条 PE，重定向 stdout 到临时文件，按字节比对预期拼接。
* `tools/polyld/src/pe_writer_smoke.cpp` — 手工脚手架，产出
  `pe_smoke_println.exe`，bring-up 阶段用于肉眼确认 Windows 加载器接受该镜像
  且三行如期到达控制台。

---

## 5. B5 — `polyld` 调用点回收（v1.5.7 发布）

`polyld` 不再在输入目标已经携带 `polyrt_println` 调用时仍然产出占位的
`ExitProcess(0)` shim。新增的公开自由函数

```cpp
namespace polyglot::linker {
std::vector<std::string>
CollectPolyrtPrintlnSequence(const std::vector<ObjectFile> &objects);
}
```

会在 `Linker::GeneratePEExecutable` 进入 PE 写入器分发之前被调用。
当回收向量非空时走 `pe::BuildPrintlnSequencePE(messages)`（B4 入口），
否则原样保留既有的 `pe::BuildExitZeroPE(user_text)` 路径，确保非 PRINTLN
程序不受影响。

### 5.1 分析过程契约

对每一个加载的 `ObjectFile`、对每一个带有 `SectionFlags::kExecInstr`
标志的节：

1. 按 `offset` 字段对该节的 `relocations` 排序，保证那些以文件序而非
   指令序暴露重定位的加载器（部分 POBJ / COFF 写入器即为此例）也能
   得到正确的调用顺序。
2. 用一个二状态机从左到右遍历排序后的重定位：
   - 目标符号以 `println.msg` 开头（且可带 IR 字符串内联器引入的可选
     尾缀 `.ptr` GEP 别名）的重定位会设置「待配对消息游标」。
   - 目标符号正好为 `polyrt_println` 的重定位会消费该游标：通过
     线性扫描所有已加载 `ObjectFile` 解析消息全局符号，从其所属节
     读取 `data[symbol.offset .. +symbol.size)` 字节区间，并把解码
     后的内容追加到结果中。
3. 之前没有待配对游标的 `polyrt_println` 调用会被静默跳过——它表示
   一个运行时计算出的消息指针，B5 暂不处理（例如
   `PRINTLN string_var;`）。
4. 走到节末仍未配对成功的游标同样被丢弃。

### 5.2 与 IR 字符串内联器的交互

按 B3 契约，IR 构造器为每个字面量产出 *两个* 全局：
`println.msg<N>`（类型 `[N x i8]`，承载字节）以及
`println.msg<N>.ptr`（一个 `ConstantGEP` 别名，作为 `i8*` 使用）。
下沉时把 `.ptr` 别名作为 `MakeCall("polyrt_println", …)` 的实参，
故分析过程在符号表查找前会剥离尾缀 `.ptr`。两种拼写都被接受，
后端若把别名折叠为对数据全局的直接引用同样能正常工作。

### 5.3 跨翻译单元的消息全局

消息解析扫描会遍历 *每一个* 已加载的 `ObjectFile`，而不仅是承载
该调用的那一个。这使得 B5 在未来的多目标管线中（例如 LTO 或共享
运行时归档让 IR 内联器跨翻译单元复用同一载荷）依然正确。

### 5.4 严格保留调用顺序语义

回收向量按源代码顺序保留语义——内联器复用同一全局所产生的重复消息
会在每个调用点都输出一次。底层存储去重由 `BuildPrintlnSequencePE`
（B4）负责，它会把回收到的消息内联折叠进 `.rdata`，但仍然每次调用
发射一次 `WriteFile`。

### 5.5 测试

- `tests/unit/linker/polyrt_println_collect_test.cpp` —— 10 个 Catch2
  用例（标签 `[b5]`），覆盖：空输入、无 PRINTLN 目标、单次调用、
  多次调用顺序保持、内联器复用全局产生的重复输出、`.ptr` GEP 别名
  解析、跨目标文件的数据全局查找、未排序重定位的内部排序、对错误
  标志的伪 `.text` 节的防御性过滤、孤立调用的静默跳过。
- `tests/integration/pe_runtime_smoke_test.cpp` —— 新增 `[b5]` 端到端
  用例：构造 `polyc` 应当产出的 `ObjectFile` 形态，把回收得到的
  镜像在主机 Windows 加载器上运行，并断言退出码（0）以及捕获到的
  stdout（`alpha\r\nbeta\r\nalpha\r\n`，含重复）完全相符。

---

## 6. B6 — 样例矩阵补全（v1.5.8 发布）

`tests/samples/<NN>_<name>/` 下原有 16 个样例目录均补齐：

- 在 `.ploy` 入口文件末尾追加 `PRINTLN "<NN>_<name>: ok\r\n";` 标记语句。
  这是每个样例必须遵守的契约，使脚本能够将 stdout 与同目录下的
  `expected_output.txt` 做逐字节比对。
- 新增 `expected_output.txt`，内容正是上述标记行：纯 ASCII，无 BOM，以
  CRLF 结尾。
- 双语 `README.md`（英文）与 `README_zh.md`（中文），描述涉及的语言、
  关键字面、构建命令以及预期运行输出。

随后由脚本 `scripts/build_all_samples.ps1`（POSIX 版本：
`scripts/build_all_samples.sh`）遍历每个目录，依次执行
`polyc --emit-obj=…` 与 `polyld … -o …`，运行产物并捕获 stdout，按以下
七种状态之一进行归类：

| 状态 | 含义 |
| --- | --- |
| `OK` | stdout 与 `expected_output.txt` 字节一致。 |
| `OUTPUT_MISMATCH` | 二进制运行成功但 stdout 与预期不符。 |
| `EMPTY_STDOUT` | 二进制返回 0 但未写出任何字节。 |
| `RUN_FAIL` | 产物在运行期失败。 |
| `LINK_FAIL` | `polyld` 失败。 |
| `COMPILE_FAIL` | `polyc` 失败。 |
| `SKIP` | 目录缺少 `.ploy` 入口。 |

汇总结果写入 `build/samples_report.json`，每个样例对应一条记录，含字段
`{name, status, polyc_rc, polyld_rc, exe_rc, stdout_bytes,
expected_bytes, diff_first_off}`。脚本默认以 0 退出，从而既能如实记录
工具链成熟度又不会阻塞构建；如需严格门禁，可使用 PowerShell 的
`-FailOnMismatch` 或 bash 的 `--fail-on-mismatch` 开关。

## 7. B7 — 主题样例扩展（v1.5.8 发布）

新增 14 个主题样例，位于 `tests/samples/17_string_processing/` 至
`tests/samples/30_game_loop_demo/`。每个目录都遵循与 B6 一致的契约：
`.ploy` 入口、两个可编译的宿主语言源文件（真实代码，无占位）、双语
README、以及与结尾 PRINTLN 标记字节一致的 `expected_output.txt`。

| 样例 | 语言 | 主题 |
| --- | --- | --- |
| `17_string_processing` | Python、Rust | 字符串处理流水线 |
| `18_numeric_kernels` | C++、Rust | 数值内核（BLAS 风格） |
| `19_file_io` | Python、C++ | 流式文件 I/O |
| `20_json_pipeline` | Python、Java | JSON 摄取流水线 |
| `21_image_processing` | C++、Rust | 图像处理内核 |
| `22_database_access` | Python、Java | 数据库访问层 |
| `23_http_client` | Python、Go | HTTP 客户端示例 |
| `24_concurrency` | C++、Rust | 并发原语 |
| `25_event_loop` | Python、JavaScript | 事件循环模拟 |
| `26_state_machine` | C++、Java | 有限状态机 |
| `27_plugin_system` | C++、Python | 插件系统 |
| `28_ml_inference` | Python、Rust | 机器学习推理流水线 |
| `29_data_analytics` | Python、Java | 数据分析 |
| `30_game_loop_demo` | C++、Rust | 游戏循环骨架 |

整个矩阵覆盖 7 种宿主语言（C++、Python、Rust、Java、C#、Go、
JavaScript），并在主题维度上有意拉开距离，以确保后端任何回归在某一主题
上失败时，不会悄无声息地波及其它主题。

集成测试 `tests/integration/samples_regression_test.cpp`（Catch2 标签
`[samples][b6][integration]`，注册于 `integration_tests` 之下）会从 C++
驱动该脚本，并断言生成的 JSON 报告结构合法：记录的 `total` 与磁盘上的
目录数一致、所有状态字符串都属于上述 7 类枚举，且没有越界的状态值。

---

## 8. 后续阶段（尚未发布）

| 阶段 | 契约 |
| --- | --- |
| **B8** | 当后端能够端到端往返每条 PRINTLN 字面量时，把脚本收紧为正式发版门禁；并在 demand-04 与 demand-49 末尾同时追加 `--end -done`。 |

B2 – B7 的契约已稳定，不再调整。
