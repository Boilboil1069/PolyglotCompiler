# PolyglotCompiler 更新日志

本文件记录 PolyglotCompiler 的所有重要变更。

英文版本见 [`CHANGELOG.md`](CHANGELOG.md)。
日常使用说明见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)；构建/API 契约见
[`api/api_reference_zh.md`](api/api_reference_zh.md) 以及 [`realization/`](realization/)
下的逐特性说明。

下述版本范围为 **v0.1.0 (2026-01-15) → v1.9.1 (2026-04-29)**，新版本在前。
每个 `### vX.Y.Z (YYYY-MM-DD)` 段落只描述发布行为本身。

---

## v1.45.0 (2026-05-06)

- 样例回归矩阵新增共享的最小样例
  `tests/samples/00_minimal/print_then_exit.ploy`，其 stdout 由
  `expected_output.txt` 字节级固定。`build_all_samples.ps1` 与
  `build_all_samples.sh` 现在识别同目录下的 `expected_output.skip`
  标记，把对应样例归入 SKIP 桶，且不再调用 polyc / polyld；SKIP
  不计入 `--require-min-ok N` 阈值。
- `samples_report.json` 新增顶层 `ok` 数组（按 ASCII 升序），列出
  当前矩阵中处于 OK 桶的样例名。集成测试
  `samples_regression_test.cpp` 现在断言共享的最小样例必须在 OK 桶，
  并断言 `ok` 数组与从 `samples` 段中走出的 OK 集合字节级一致，避免
  脚本与测试对 “OK” 的定义出现漂移。
- Windows PowerShell 端将 `Start-Process` 改为调用运算符
  （`& $Polyc ...`），让带绝对路径的 `--emit-obj=<path>` 参数原样传给
  polyc，不再因等号后的反斜杠被静默截断；同时为单样例错误加上
  `$ErrorActionPreference = 'Continue'` 的局部作用域，避免一个样例
  失败导致整个矩阵中断。
- 新增 CI 矩阵：`samples-windows-2022`、`samples-ubuntu-24.04`、
  `samples-macos-14` 三条 job 各自构建 polyc / polyld，再执行
  `ctest -R "samples_regression"`，并把 `samples_report.json` 作为
  artifact 上传；任一 job 失败即整个工作流失败。
- `runtime/include/services/error_bridge.h` 与 `async_bridge.h` 改用
  可移植的 `POLYRT_NORETURN` 宏并显式 `#include <string>`，恢复了
  Windows 下 MSVC `/TP` 路径上 polyrt 共享库的干净构建。
- 根 `CMakeLists.txt` 与 `VERSION.txt` 递进到 **1.45.0**。

---

## v1.44.0 (2026-05-06)

- polyld 在 `tools/polyld/src/linker_elf.cpp`
  （`polyglot::linker::elf::BuildELFImage`）中提供了真实的 ELF64
  `ET_EXEC` 写出器。每一份产出镜像在结构上都自洽，可由未经修改的
  Linux 内核直接通过 `execve(2)` 加载：
  - ELF 头：`EI_CLASS = ELFCLASS64`、`EI_DATA = ELFDATA2LSB`、
    `e_type = ET_EXEC`，`e_machine` 根据请求选择
    （`EM_X86_64` 或 `EM_AARCH64`），`e_entry` 指向 `.text` 头部
    polyld 注入的 `_start` 桩；
  - 固定四条程序头：`PT_PHDR`、覆盖 ELF 头 + phdr 表 + `.text` +
    `.rodata` 的 `PT_LOAD(R+X)`、覆盖 `.data` + `.bss` 的
    `PT_LOAD(R+W)`（无内容时尺寸折叠为 0）、以及携带 `PF_R | PF_W`
    的 `PT_GNU_STACK`，确保内核强制非可执行栈；
  - 节头表至少包含 `.text` 与 `.shstrtab`，按需追加 `.rodata` /
    `.data` / `.bss`，能被 `readelf -S` 与 `objdump -h` 正确解析。
- `_start` 桩由 polyld 直接拼装机器码，并通过
  `BuildStartStubX86_64` / `BuildStartStubArm64` 暴露给单元测试做
  逐字节比对。两条桩均为 16 字节，先 `call` / `bl` 进入 `main`，
  再把 `main` 的返回值送入内核 exit 系统调用
  （x86_64 上 `__NR_exit = 60`，aarch64 上为 93），尾部以架构
  对应的 NOP 对齐。
- 新增运行时垫片 `runtime/src/libs/polyrt_linux.c` 与
  头文件 `runtime/include/polyrt_linux.h`，以内联 `syscall` /
  `svc #0` 序列直接调用内核 `write(2)` 与 `exit_group(2)`，提供
  `polyrt_println` 与 `polyrt_exit`，完全不依赖 libc。在
  非 Linux 主机上保留 `#else` 分支的 libc 回退实现，使得整个
  `runtime` 目标在 macOS / Windows 开发机上也能正常构建。
- `Linker::GenerateELFExecutable` 改造为薄壳：将 `output_sections_`
  按可执行/只读/可写/BSS 划分后委托给 `BuildELFImage`；旧的手写
  写出实现被 `#if 0` 圈起作为参考保留。
- 测试：
  - `tests/unit/polyld/elf_image_layout_test.cpp`（`[elf][polyld]`）
    对 x86_64 与 aarch64 的最小镜像分别解码：校验 magic / class /
    machine、遍历程序头定位覆盖 `e_entry` 的 `PT_LOAD(R+X)`、断言
    `PT_GNU_STACK` 的 `p_flags == PF_R | PF_W`，并对 `.text` 头
    16 字节与架构桩模板做完全相等比对。另有一例验证当
    `data` / `bss_size` 非空时 R/W `PT_LOAD` 的形状与对齐约束。
  - `tests/integration/elf_exec_smoke_test.cpp`
    （`[elf][exec][integration]`）以 `00_minimal/print_then_exit.ploy`
    为输入串联 `polyc` 与 `polyld`，再对 `/tmp/polyld_elf_smoke`
    执行 `fork + execve + waitpid`，断言 `WEXITSTATUS == 0` 且
    stdout 捕获到 `"ok\n"`。仅在 `__linux__` 下编译，其他平台
    输出占位通过用例。
- 新增 CI 辅助脚本 `scripts/ci/run_linux_smoke.sh`，可在
  `ubuntu:24.04` 容器内完成配置、构建与 ELF 相关 ctest 选择器
  的执行；可在 macOS / Windows 工作机上通过
  `docker run --rm -v "$PWD":/w -w /w ubuntu:24.04` 一键触发。

---

## v1.43.0 (2026-05-06)

- polyld 的 Mach-O 写出器现在为每一个 `MH_EXECUTE` 镜像生成真实的
  `LC_DYLD_EXPORTS_TRIE` 内容。该 trie 严格携带两个常规导出符号——
  `_main`（`address = LC_MAIN.entryoff + __TEXT.fileoff`，`flags =
  EXPORT_SYMBOL_FLAGS_KIND_REGULAR`）与 `_mh_execute_header`
  （`address = 0`，标志位同上），二者共享 `"_m"` 前缀并在内部分叉节
  点下展开。每个节点按 Apple 的 ULEB128 格式编码（`terminal_size`、
  可选的 `flags + address`、`children_count`、每条边的
  `edge_string + child_offset`），整体按 8 字节对齐。`datasize` 严格
  大于 24 字节，能够通过 AMFI / CoreTrust / AppleSystemPolicy 在
  execve(2) 入口处对真实 `_main` 的强校验。
- `LC_FUNCTION_STARTS` 现在承载真正的函数起始偏移：单函数情形下其
  payload 为 `ULEB128(LC_MAIN.entryoff) + 0x00` 并填充至 8 字节；底
  层辅助函数已支持以升序序列输入多函数起始偏移并按差分 ULEB128 编码。
- `LC_DATA_IN_CODE` 仍保持 `datasize == 0`，但其 `dataoff` 槽位继续按
  顺序衔接至 LINKEDIT 字节流，确保段偏移单调递增。
- LC 顺序固定为 `... LC_DYLD_CHAINED_FIXUPS → LC_DYLD_EXPORTS_TRIE →
  LC_FUNCTION_STARTS → LC_DATA_IN_CODE → LC_SYMTAB → LC_DYSYMTAB →
  LC_LOAD_DYLINKER → LC_LOAD_DYLIB → LC_UUID → LC_BUILD_VERSION →
  LC_SOURCE_VERSION → LC_MAIN → LC_CODE_SIGNATURE`；LINKEDIT 字节流
  顺序为 `chained_fixups → exports_trie → function_starts →
  data_in_code → nlist → string → code_signature`，
  `__LINKEDIT.filesize` 恰好覆盖到内嵌代码签名 blob 的尾偏移。
- 新增单元测试 `unit/polyld/macho_exports_trie_test.cpp`
  （`[macho][exports_trie][polyld]`）：构造 `entry_offset = 0x10`
  的最小 `MH_EXECUTE`，使用手写 ULEB128 解码器从 trie root 遍历，断
  言两个导出符号的解析地址符合规范。
- 新增集成测试 `integration/macho_exec_smoke_test.cpp`
  （`[macho][exec][integration]`，仅在 `__APPLE__ && __aarch64__`
  下编译）：驱动 `polyc` + `polyld` 编译并链接新增的
  `tests/samples/00_minimal/print_then_exit.ploy`，通过
  `posix_spawn` 启动 `/tmp/polyld_macho_smoke` 并断言
  `WEXITSTATUS == 0` 且 stdout 为 `"ok\n"`。
- 新增样例 `tests/samples/00_minimal/print_then_exit.ploy` 与中英双语
  `README.md` / `README_zh.md`，以及由真实运行得到的
  `expected_output.txt`，作为跨平台冒烟用源文件。

---

## v1.42.5 (2026-05-06)

- polyld 的 Mach-O 写出器新增 `LC_FUNCTION_STARTS`(`cmd = 0x26`,16
  字节 `linkedit_data_command`,payload 为 8 字节,内容是单个 ULEB128
  终止符 `0x00` 加 7 字节零填充)与 `LC_DATA_IN_CODE`(`cmd = 0x29`,
  16 字节命令,`datasize = 0`)。两条 LC 紧跟 `LC_DYLD_EXPORTS_TRIE`
  之后、`LC_SYMTAB` 之前发射,与 Apple 链接器对无函数表 / 无内嵌
  数据字面量二进制的输出顺序完全一致。
- LINKEDIT 字节流顺序固定为 `chained_fixups → exports_trie →
  function_starts → data_in_code → nlist → string → code_signature`,
  每段 `dataoff` 严格等于前一段的 `dataoff + datasize`;
  `__LINKEDIT.filesize` 仍精确延伸到内嵌代码签名 blob 末尾。
- 新增单元测试 `unit/polyld/macho_linkedit_data_emit_test.cpp`
  (`[macho][linkedit][polyld]`):走读最小 MH_EXECUTE 镜像的 load
  command 序列,断言 LC 顺序、四段 LINKEDIT payload 偏移依次衔接、
  function-starts payload 恰为 8 字节全零、`__LINKEDIT.filesize`
  与代码签名 blob 末尾对齐。
- 回归:`[bin8],[bin7],[samples]` integration_tests 全绿 ——
  8 用例 / 151 断言;`[linker_macho]` 单元测试全绿 —— 8 用例 /
  32 断言;`codesign --verify --strict` 仍输出 `valid on disk`。

## v1.42.4 (2026-05-06)

- polyld 的 Mach-O 写出器新增 `LC_DYLD_CHAINED_FIXUPS`(16 字节命令指向
  56 字节负载:32 字节 `dyld_chained_fixups_header` 加一段空的
  `dyld_chained_starts_in_image`,按 8 字节对齐补零)与
  `LC_DYLD_EXPORTS_TRIE`(16 字节命令指向 8 字节空 trie)。两条命令插入
  在段块与 `LC_SYMTAB` 之间,与 Apple 链接器输出的顺序保持一致。
- `BuildRequest::minos`、`BuildRequest::sdk` 默认值由 11.0 提升至
  26.0 / 26.4,使 `LC_BUILD_VERSION` 与当前 Apple Silicon 上的 macOS
  Tahoe SDK 一致。
- LINKEDIT 段的 `filesize` 改为代码签名 blob 末尾的精确偏移
  (此前被向上对齐到一页);段 `vmsize` 仍然按页对齐供内核映射使用。
- 效果:AMFI 不再以 `"no CMS blob? Unrecoverable CT signature issue"`
  拒绝产物,`codesign --verify --strict` 输出 `valid on disk`。
  已知限制:macOS 26 内核仍会在 execve 阶段以
  `AppleSystemPolicy: ASP: Security policy would not allow process`
  拦截执行。下一步将补齐包含 `_main` 与 `_mh_execute_header` 的真实
  exports trie,以及 `LC_FUNCTION_STARTS` / `LC_DATA_IN_CODE` 两条
  Linkedit-data 命令。
- 回归:`[bin8],[bin7],[samples]` integration_tests 通过 — 8 个用例、
  151 条断言。

## v1.42.3 (2026-05-06)

**Mach-O 写出器现在内联生成 linker-signed 代码签名。**

- `tools/polyld/src/linker_macho.cpp` 新增 `BuildLinkerSignedSignature`：在镜像其余部分布局完成后，写出一个 SHA-256 page-hash CodeDirectory（v=20400），外层封装为 embedded SuperBlob，flags 为 `adhoc | linker-signed`，`execSeg` 覆盖 `__TEXT` segment，整体写入 `LC_CODE_SIGNATURE` 区域。
- CodeDirectory identifier 默认取输出文件的 basename，使 `codesign -dvvvv` 与 `otool -l` 报告稳定且可识别的名称。
- 产出二进制的 `codesign -dvvvv` 现在显示 `flags=0x20002(adhoc,linker-signed)`，与 `clang -arch arm64` 产出的 ad-hoc 签名一致；`codesign --verify --strict` 显示 `valid on disk`。
- 已知限制：Apple Silicon 的 `AppleSystemPolicy` 在 `execve(2)` 时仍会拒绝该二进制，原因是缺少 `LC_DYLD_CHAINED_FIXUPS` / `LC_DYLD_EXPORTS_TRIE`；缺少现代 dyld 元数据的静态二进制即使 linker-signed CodeDirectory 校验通过也无法被内核接受。下一阶段里程碑是补齐 chained-fixups 发射。

## v1.42.2 (2026-05-06)

**macOS Mach-O 写出器加固 — 产出可执行文件现在能通过 `file`、`otool -h/-l/-tv`、`codesign -vv`。**

- `EmitDylinkerCommand` 的 cmdsize 改为以实际对齐后的记录长度计算；之前的公式少了 4 字节，导致加载器在后续加载命令上偏移 4 字节，把 `LC_LOAD_DYLIB` / `LC_BUILD_VERSION` 读成乱码。
- ARM64 Mach-O 头现在写 `cpusubtype = CPU_SUBTYPE_ARM64_ALL`（0）；之前是 X86 的 `CPU_SUBTYPE_ALL`（3），在 Apple Silicon 上被内核判为 `bad CPU type in executable`。
- `LC_MAIN.entryoff` 现在指向真实的 `__text` 文件偏移；之前固定为 0，跳入 Mach 头。
- Mach-O 目标文件解析器现在按 section 自身的 `segname` 划分 section flags（而不是父 segment 的，后者在 obj 中为空串），`__TEXT,__text` 能被正确打 `kExecInstr` 标记、把用户代码带进最终镜像。
- Mach-O 写出器保证 section 数据位置与 section record 的 `offset` 字段一致（删除了 section 数据发射前多余的 page pad）。
- 已知限制：产出的 ad-hoc 签名产物仍被 AMFI 以 -423 拒接；完整的 `dyld` 互联（LINKEDIT 为代码签名预留容量、DYSYMTAB 计数与实际符号表一致、到 `_write`/`_exit` 的 dyld bind opcodes、后端 Mach-O `GOT_LOAD_PAGE21/PAGEOFF12` reloc）是下一阶段里程碑。

---

## v1.42.0 (2026-05-07)

**二进制容器正式发布 — 闭合的跨目标矩阵、CI 集成、性能基线，以及“假 .exe”反向断言。**

- `polyc --target=<triple>` / `--container=<auto|elf|pe|macho|wasm>` / `--subsystem=` / `--entry=` 是工具链每个入口（`polyc`、`polyld`、`polyasm`、`polyopt`、`polybench`、`polyrt`）的一等公民；未指定三元组时统一回退到 `common::HostTriple()`。
- 新增 `tests/integration/binary_matrix/binary_matrix_test.cpp`（`[bin8][binary_matrix]`），按 7 个支持的三元组 × 6 个代表性样例分三步覆盖：静态帮助 API 一致性、本机直跑列（产物 magic 必须与 `ResolveContainer` 一致）、反向断言（在非 Windows 宿主上要求 `polyc -o foo.exe` 时，必须产出宿主原生 magic 并触发 `polyc-warn-W2101`，从此“假 .exe（实际是 ELF/Mach-O）”回归永远不可能）。
- `scripts/ci/run_binary_matrix.{sh,ps1}` 在 CI 中驱动同一矩阵，把产物落到 `artifacts/binary_matrix/<triple>/`，并探测 `dumpbin` / `otool` / `readelf` / `wasm-objdump`（缺失时优雅降级）。
- `scripts/build_all_samples.{sh,ps1}` 是 `tests/integration/samples_regression_test.cpp` 使用的跨平台样例回归脚手架；两套实现产出相同形状的 `samples_report.json`。
- `polybench link`（同时被并入 `polybench all`）新增四条基准：`pe_link_time` / `macho_link_time` / `elf_link_time` / `wasm_link_time`，结果写入 `benchmark_link_times.json` 并在顶层标注当前生效的 `target_triple`。
- 双语 `docs/realization/binary_containers_{en,zh}.md` 新增 “发布矩阵与 CI” 与 “性能基线” 收口章节；顶级 `README.md` 增加 “支持的目标平台与容器格式” 表格，覆盖全部 7 个三元组。
- 版本号：`1.42.0-pre.5` → `1.42.0`（正式发布）。`VERSION.txt` 由 `POLYGLOT_VERSION_SUFFIX`（现为空）重新生成。
- 测试基线：`integration_tests "[bin8]"` 65/3、`integration_tests "[bin7]"` 70/4、`integration_tests "[samples]"` 16/1。

---

## v1.42.0-pre.4 (2026-05-05)

**PE 链接胶水（BIN-4）：真正的 `Linker::GeneratePEDll`、`.def` 文件、导出表、重定位翻译。**

- 新增 `tools/polyld/include/linker_pe.h` 与 `tools/polyld/src/linker_pe.cpp`，与 `linker.cpp` 同属 `polyglot::linker` 家族，承载 PE 侧的链接器胶水。
- `Linker::GeneratePEDll()` 完成真实实现：通过 `BuildRequest::extra_file_characteristics` 写入 `IMAGE_FILE_DLL`（0x2000），由 `pe::BuildExportSection` 构造 `IMAGE_EXPORT_DIRECTORY`（EAT、Name Pointer Table、Ordinal Table、DLL 名、按导出名一一排列的字符串），并在镜像生成后回填可选头中的导出数据目录。DLL 默认首选基地址为 0x180000000。
- `pe::ParseDefFile` 解析 Microsoft `.def` 文件（含 `LIBRARY` + `EXPORTS` 段、`name=internal`、`@N`、`NONAME`、`DATA`、`PRIVATE` 修饰、`;`/`#` 注释）。`pe::ParseCliExportSpec` 在没有 `.def` 时支持完全相同的右侧语法。`pe::MergeExports` 按公开名去重，互不相容的重复声明上报为 `polyld-err-E3201`。
- `pe::TranslateRelocationsToPEBaseRelocs` 把中性 `Relocation` 编码翻译为 PE 基址重定位（DIR64 / HIGHLOW / ARM64 page 系列）。不受支持的编码——包括仅在 ELF 上有意义的 `R_X86_64_GOTPCREL` 等——会被记录为 `polyld-err-E3210` 并使函数返回 `false`，同时继续翻译剩余项以一次性暴露完整冲突集。
- polyld 新增 CLI：`--def <file>`、`/EXPORT:<spec>`（cl 风格）、`--export <spec>`（GNU 风格）、`--dll-name <name>`。`LinkerConfig` 增加 `def_files`、`cli_export_specs`、`dll_name` 字段。
- 新增单元测试 `tests/unit/linker/linker_pe_test.cpp`，覆盖 `.def` 解析、`/EXPORT` 解析、冲突上报、导出段字节级布局断言以及重定位翻译器的成功 / E3210 路径。
- 双语文档 `docs/realization/binary_containers_{en,zh}.md` 新增 “PE 路径细节” 章节，详细描述导出来源、`.edata` 字节布局、重定位翻译表与新增错误码。
- 测试套基线：`test_core "[common]"` 133/9、`test_linker` 503/87（原 452/82，+51/+5）、`unit_tests "[polyui]"` 1304/238。

---

## v1.42.0-pre.3 (2026-05-05)

**PE32+ 写出器加固（BIN-3）。**

- `BuildPE32PlusImage` 现按规范顺序 `.text / .data / .rdata / .bss /
  .pdata / .xdata / .idata / .reloc` 规划多节布局，仅在对应请求字段
  非空时才发射；`.bss` 仅占虚拟空间（`SizeOfRawData = 0`）。
- 新增 `BaseRelocation` 与 `BuildBaseRelocSection` /
  `DecodeBaseRelocSection`：按 4 KiB 页分组写出
  `IMAGE_BASE_RELOCATION` 块，必要时追加 `IMAGE_REL_BASED_ABSOLUTE`
  填充使 `SizeOfBlock` 4 字节对齐。非空 `.reloc` 会清除
  `IMAGE_FILE_RELOCS_STRIPPED` 并置位 `DYNAMIC_BASE`（x64 / arm64 还
  会再置 `HIGH_ENTROPY_VA`）。
- `BuildRequest::machine` 选择 `IMAGE_FILE_MACHINE_AMD64 / ARM64 /
  I386`；`BuildRequest::subsystem` 选择 `WindowsCui / WindowsGui /
  EfiApplication / NativeDriver`；`dll_characteristics` 覆盖默认值；
  `extra_file_characteristics` 与 COFF Characteristics 按位或（DLL 场
  景请传 `0x2000`，即 `IMAGE_FILE_DLL`）。
- Data Directory[3]（Exception）与 Data Directory[5]（Base
  Relocation）在对应节存在时分别接到 `.pdata` 与 `.reloc`。
- 新增 `tests/unit/linker/pe_writer_hardened_test.cpp` 覆盖编码器回环、
  多节顺序、Characteristics 切换、字段覆盖；旧 `pe_writer_test` 用例
  字节级保持不变。
- 新增实现文档 `docs/realization/pe_writer_{en,zh}.md`。

---

## v1.42.0-pre.2 (2026-05-05)

**链接器容器派发（BIN-2）。**
`Linker::GenerateOutput()` 现在按解析后的
`(OutputFormat, BinaryContainer)` 对派发，不再硬编码为
ELF。`Linker::ResolveContainerAndTriple()` 在构造函数中运行，
将遗留的 `target_os` 字段折叠到规范 `target_triple`（原
调用点免改动）并依 [`ResolveContainer`](../common/include/binary_container.h)
锁定 `effective_container_`。共享库现为真正的镜像：ELF 发
`ET_DYN`；Mach-O 发 `MH_DYLIB`，并由 `LC_ID_DYLIB` 携带
 install name。新增 `Linker::GenerateWasmModule()` 写出结构完整
的 WebAssembly 1.0 模块（前缀 + Type / Function / Export /
Code 节，导出 `_start`），并将合并后的 `.text` 作为
`polyglot.text` 自定义节嵌入。PE 共享库走新增的
`Linker::GeneratePEDll()`（在 BIN-4 接入导出表之前转发给
PE32+ 写器）。

[`SuffixesFor`](../common/include/binary_container.h) /
[`SuffixMatchesContainer`](../common/include/binary_container.h)
导出各容器的规范后缀（PE：`.exe / .dll / .lib`；Mach-O：
`.dylib`；ELF：`.so`；Wasm：`.wasm`），以供编译器驱动在
用户 `-o` 路径与解析容器不匹配时发出 `polyc-warn-W2101`。

### 已验证套件

* `test_linker` — 75 例 / 381 断言（包含 6 例派发用例）。
* `test_core "[common]"` — 9 例 / 133 断言。
* `unit_tests "[polyui]"` — 238 例 / 1304 断言（回归）。

---

## v1.42.0-pre.1 (2026-05-05)

**二进制容器与目标三元组抽象（BIN-1）。**
`polyglot::common::TargetTriple`（[`common/include/target_triple.h`](../common/include/target_triple.h)）
是强类型的 `arch-vendor-os-env[-sub]` 值，提供不抛异常
的 `ParseTargetTriple()`、`HostTriple()` 宿主推导、确定性
`str()` 循环、等号与 `std::hash`。解析器覆盖主流
三元组（`x86_64-pc-windows-msvc`、`aarch64-apple-darwin`、
`x86_64-unknown-linux-{gnu,musl}`、`aarch64-linux-android`、
`riscv64-unknown-linux-gnu`、`wasm32-wasi`…）并折叠常见
别名（`amd64 / x64`、`arm64`、`mingw32`、`gnueabihf`、`elf`）。
`polyglot::common::BinaryContainer`（[`common/include/binary_container.h`](../common/include/binary_container.h)）
提供 `kAuto / kELF / kPE / kMachO / kWasm` 以及 `ContainerForOS`、
`DefaultOSForContainer` 与 `ResolveContainer(triple, requested)`
（显式请求优先；`wasm32-*` 总是映射到 `kWasm`）。
`LinkerConfig` 新增 `target_triple`、`container` 与向后兼容的
`target_os` 字段。后端三元组重载与 `Linker::GenerateOutput()`
的真正容器派发在 BIN-2 完成。

本版本在统一版号线上开启 **1.5.0 预发布周期**；
预发布后缀存于 `POLYGLOT_VERSION_SUFFIX` /
`POLYGLOT_VERSION_STRING`，`VERSION.txt` 输出 `1.42.0-pre.1`。

### 已验证套件

* `test_core "[common]"` — 9 例 / 133 断言。
* `unit_tests "[polyui]"` — 238 例 / 1304 断言（回归）。

---

## v1.41.0 (2026-05-05)
[`ImageViewer`](../tools/ui/common/viewer/image_viewer.h) 从魔数
识别 PNG / JPEG / WebP / GIF / SVG / BMP（文本型回退到扩展
名），缩放夹紧 5 – 6400 %，支持平移、行主序 RGBA 缓冲上
的像素拾取与单通道（红 / 绿 / 蓝 / Alpha）分离。
[`HexViewer`](../tools/ui/common/viewer/hex_viewer.h) 面向 ≥ 1 GiB
大文件：通过 `HexReader` 回调分块读取，查找时仅保留
`needle.size() - 1` 字节作为重叠，跨块匹配依然命中；`JumpTo`
按行对齐向下贴齐；跟踪命名高亮供链接器段映射等工具使用。
[`IdentifyBinary`](../tools/ui/common/viewer/binary_inspector.h)
识别 ELF / PE / Mach-O / WASM 容器，报告架构（`x86_64`、
`aarch64`、`wasm32` …）、位宽、字节序与 subsystem；
`DisassemblerFacade` 接入 `polyasm`，不支持架构下以 `.byte`
占位渲染，面板始终有内容。
[`SqlConsole`](../tools/ui/common/dbclient/sql_console.h)
构建于抽象 `SqlDriver` 接口（Qt 构建绑 SQLite，测试用伪驱动），
记录有界历史、完整透传驱动错误，与 `ResultPager`（固定页大小、
页数计算）及 `ExportCsv`（遵 RFC-4180 对逗号 / 引号 / 换行转义）
配合使用。面向用户教程：
[`tutorial/viewers_en.md`](tutorial/viewers_en.md) /
[`tutorial/viewers_zh.md`](tutorial/viewers_zh.md)；USER_GUIDE 中英
双语章节见 [`USER_GUIDE.md`](USER_GUIDE.md) 与
[`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试：
[`tests/unit/polyui/viewers_test.cpp`](../tests/unit/polyui/viewers_test.cpp)
（238 例 1304 条 polyui 断言全绿）。

---

## v1.40.0 (2026-05-05)

**i18n、无障碍与选入式遥测 / 崩溃报告。**
[`StringCatalog`](../tools/ui/common/i18n/i18n.h) 存储按 id
× locale 的 UI 字符串，内建五种语言（`zh-CN`、`zh-TW`、
`en`、`ja`、`ko`）；查找依次回退到配置的回退语言与 id 本
身，因此 UI 不会因缺失翻译变空。`Translator` 绑定目录与当
前语言；Qt 层接入 `QTranslator`。`MissingStringScanner`
是 CI 闸门，扫描 `tr("...")` 调用并拒绝硬编码字面量与未
知 id。
[`FocusOrder`](../tools/ui/common/a11y/accessibility.h) 维护
确定性键盘 Tab 链，循环回绕并跳过禁用控件；
`ScreenReaderQueue` Drain 时先取 assertive 后取 polite，供
平台无障碍桥（UIA / AT-SPI / NSAccessibility）使用；
`AccessibilityProfile` 汇总高对比度、大字体（夹紧 80–300%）
与减少动效开关，以 JSON 往返。
[`ConsentManager`](../tools/ui/common/telemetry/telemetry.h)
默认关闭且随时可撤回；`FieldAllowList` 在事件进入本地预
览之前剀除任何不在允许名单内的字段；`TelemetryBuffer`
是有界、用户可审的日志；`CrashReportStore` 始终先落盘，
上传是独立闸门。双语实现文档：
[`realization/i18n_en.md`](realization/i18n_en.md) /
[`realization/i18n_zh.md`](realization/i18n_zh.md)、
[`realization/accessibility_en.md`](realization/accessibility_en.md) /
[`realization/accessibility_zh.md`](realization/accessibility_zh.md)、
[`realization/telemetry_en.md`](realization/telemetry_en.md) /
[`realization/telemetry_zh.md`](realization/telemetry_zh.md)。
面向用户章节见 [`USER_GUIDE.md`](USER_GUIDE.md) 与
[`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试：
[`tests/unit/polyui/localization_test.cpp`](../tests/unit/polyui/localization_test.cpp)
（232 例 1244 条 polyui 断言全绿）。

---

## v1.39.0 (2026-05-05)

**IDE 外壳：欢迎页、通知中心、可定制状态栏、最近列表、会话恢复、
书签与 TODO 索引。**
[`WelcomePage`](../tools/ui/common/shell/welcome.h) 维护最近工作
区（按路径去重、最新在前）、教程 / 样例入口与按版本归档的新特
性提示；页面可关闭、可固定，并以 JSON 序列化。
[`NotificationCenter`](../tools/ui/common/shell/notifications.h)
推送持久化通知，支持分级（`info` / `warning` / `error` /
`progress`）、action 按钮、未读计数与不打扰模式——不打扰只屏蔽
非关键级别，警告与错误始终透传。
[`StatusBar`](../tools/ui/common/shell/status_bar.h) 内建九个槽
位（分支、问题、语言、语言服务器、编码、行尾、缩进、包管理器、
Profiler），接受第三方注册；用户可显隐每个槽位、在左右两侧之
间按优先级拖动，布局可往返 JSON。
[`RecentList`](../tools/ui/common/shell/recent.h) 是 `Ctrl+R`（工
作区）与 `Ctrl+E`（文件）背后的有上限、可固定的存储：访问即提
顶，固定项永远高于未固定项，裁剪时保留全部固定项。
[`SessionStore`](../tools/ui/common/shell/session.h) 序列化完整
编辑器会话——分屏方向、面板列表、标签（含光标 / 滚动 / 折叠）、
面板大小与显隐、调试视图状态（配置、watch、打开的子视图）以及
任意字符串 extras——并可无损反序列化。
[`BookmarkStore`](../tools/ui/common/shell/bookmarks.h) 按
`(path, line)` 切换可加标签、可着色的书签，提供按文件与全局两
种视图。
[`TodoIndex`](../tools/ui/common/shell/todo_index.h) 按可配置关
键字集合（默认 `TODO`、`FIXME`；可扩展 `XXX`、`HACK` 等）重新
扫描文件，强制单词边界以避免 `TODOMARKER` 误中，按关键字汇总
计数供面板使用。
面向用户的章节见 [`USER_GUIDE.md`](USER_GUIDE.md) 与
[`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)；教程走查见
[`tutorial/shell_en.md`](tutorial/shell_en.md) /
[`tutorial/shell_zh.md`](tutorial/shell_zh.md)。测试：
[`tests/unit/polyui/shell_test.cpp`](../tests/unit/polyui/shell_test.cpp)
（223 例 1180 条 polyui 断言全绿）。

---

## v1.38.0 (2026-05-05)

**插件系统 + 本地 Marketplace + 多根工作区。**
[`ExtensionHost`](../tools/ui/common/ext/extension_api.h) 加载以
`extension.json` 描述的插件（id、name、version、publisher、
entry point、loader——原生动态库或 JavaScript / TypeScript bundle、
activation events、所需 capabilities、contributions）。贡献点覆盖
commands、keybindings、menus、panels、views、status-bar items、
themes、语言客户端、调试适配器、文件图标主题、格式化器、
snippets、任务与重构 provider；注册表以 `(kind, id)` 去重，重
新激活不会遗留陈旧项。
[`CapabilityGate`](../tools/ui/common/ext/extension_api.h) 强制
用户明示授予 `filesystem`、`network`、`process`、`clipboard`、
`secrets` 能力；任一所需能力未授权时激活直接失败。
[`Marketplace`](../tools/ui/common/ext/marketplace.h) 解析文件系
统 / HTTP 索引，按 semver 选取最高版本，提供安装 / 卸载 /
更新 / 回滚，并按 id 维护安装历史；`SignaturePolicy` 可选地
强制所有安装携带受信任的签名。
[`Workspace`](../tools/ui/common/workspace/workspace.h) 解析 / 序
列化 `polyui.code-workspace`，维护多根及每根独立设置（文件夹值
优先，其次工作区值），提供跨根搜索，并透过
`LanguageServerPool` 以 `(folder, language, version)` 为粒度隔离
polyls / DAP / 任务实例。设计详情：
[`api/extension_api_zh.md`](api/extension_api_zh.md)、
[`realization/marketplace_zh.md`](realization/marketplace_zh.md)；
面向用户的章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试覆
盖于
[`tests/unit/polyui/extension_api_test.cpp`](../tests/unit/polyui/extension_api_test.cpp)、
[`marketplace_test.cpp`](../tests/unit/polyui/marketplace_test.cpp)
与 [`workspace_test.cpp`](../tests/unit/polyui/workspace_test.cpp)
（216 例 1096 条 polyui 断言全绿）。

---

## v1.37.0 (2026-05-05)

**AI 助手集成 + 协作 / PR。**
[`AiProvider`](../tools/ui/common/ai/ai_provider.h) 是 IDE 内所有
AI 能力的统一接口：聊天、FIM 补全、行内灰字建议与重构提议。
内置适配器覆盖本地 Ollama、OpenAI 兼容 HTTP、Azure OpenAI 与
Anthropic；API key 不会嵌入二进制。隐私控制交由
`AiPrivacyPolicy`（总开关 + 工作区允许 / 拒绝名单 + diagnostics /
已打开文件开关）负责；在用户明确同意之前，任何远程调用都会以
`finish_reason = "consent_denied"` 返回。提示词模板支持
`{{name}}` 替换；`FilterContextPaths` / `PathPassesPolicy` 拦截
项目上下文采集器。
[`InlineSuggestionSession`](../tools/ui/common/ai/inline_suggestion.h)
实现 Tab 接受 / Esc 拒绝 / Alt+] 切换的状态机。
[`RefactorReviewSession`](../tools/ui/common/ai/refactor_diff.h)
跟踪逐 hunk 接受 / 拒绝决策，并为已接受的 hunk 输出 unified
diff。
[`CollabProvider`](../tools/ui/common/collab/collab_provider.h)
统一 GitHub / GitLab / Gitea 三家的 PR 列表、diff 获取、Review 提
交、分支 Push、Issue 创建、关联 commit 与文件行引用；并提供确
定性的 `kInMemory` 适配器供测试与离线模式使用。设计详情见
[`realization/ai_integration_zh.md`](realization/ai_integration_zh.md)
与
[`realization/collab_zh.md`](realization/collab_zh.md)；面向用户的
章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试覆盖于
[`tests/unit/polyui/ai_provider_test.cpp`](../tests/unit/polyui/ai_provider_test.cpp)、
[`inline_suggestion_test.cpp`](../tests/unit/polyui/inline_suggestion_test.cpp)、
[`refactor_diff_test.cpp`](../tests/unit/polyui/refactor_diff_test.cpp)
与
[`collab_provider_test.cpp`](../tests/unit/polyui/collab_provider_test.cpp)
（199 例 986 条 polyui 断言全绿）。

---

## v1.36.0 (2026-05-05)

**远程开发——SSH / WSL / Container / Dev Container。**
PolyUI 现以统一的
[`RemoteSession`](../tools/ui/common/remote/remote_session.h)
抽象托管远程工作区。`LocalRemote`、`SshRemote`、`WslRemote`
与 `ContainerRemote`（docker / podman）提供同一套文件系统、
进程、端口转发与终端 API；polyls、DAP、任务系统与集成终
端全部在同一套接口上运行。`ParseConnectionString`
识别 `local:`、`ssh://[user@]host[:port]/path`、
`wsl://distro/path` 与
`container://[runtime/]image-or-id/path`；进程启动会以匹配的
传输包装命令（`ssh -p <端口> 用户@主机 -- <命令>`、
`wsl -d <发行版> -- <命令>`、`docker exec -u <用户> <容器>
<命令>`）。
[`DevContainer`](../tools/ui/common/remote/dev_container.h) 解析
`.devcontainer/devcontainer.json`（image、dockerFile、
workspaceFolder、remoteUser、forwardPorts、postCreateCommand、
features、remoteEnv），产出对应的 `RemoteDescriptor` 与
`ProvisionPlan`（总是包含 polyls 以及识别出的容器 feature 所
需的 LSP：Python、Node、Java、Go、Rust、.NET、Ruby、C++）。
[`PlanSync`](../tools/ui/common/remote/file_sync.h) 对本地与远
端文件索引进行差异运算，产出 upload / download / delete 计
划，以及面向单向场景的 push-only / pull-only 模式。设计详
情见 [`realization/remote_dev_zh.md`](realization/remote_dev_zh.md)，
面向用户的章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。
测试覆盖于
[`tests/unit/polyui/remote_session_test.cpp`](../tests/unit/polyui/remote_session_test.cpp)、
[`dev_container_test.cpp`](../tests/unit/polyui/dev_container_test.cpp)
与 [`file_sync_test.cpp`](../tests/unit/polyui/file_sync_test.cpp)
（178 例 875 条 polyui 断言全绿）。

---

## v1.35.0 (2026-05-05)

**Sample / Tutorial Browser、Topology Live 与 Inlay Hints。**
PolyUI 新增一个项目内置的示例 / 教程目录
([`tools/ui/common/samples/sample_browser.h`](../tools/ui/common/samples/sample_browser.h))，
索引 `tests/samples/` 与 `docs/tutorial/`，按语言 / 主题 / 难
度 / 全文筛选，并产出 *以工作区副本打开* 的 `CopyPlan`，
由 IDE 将条目复制到目标目录而不侵入源树。
[`LiveTopologyTracker`](../tools/ui/common/topology_live/topology_live.h)
在静态拓扑图之上叠加跟随当前焦点的视图：它以当前
符号为中心抽取子拓扑（无符号时以当前文件为锚点），
对编辑进行 debounce，并能将拓扑节点点击解析回源位置，
实现编辑器↔拓扑双向即时跳转。
[`InlayHintProvider`](../tools/ui/common/inlay_hints/inlay_hints.h)
为 polyls 的 `textDocument/inlayHint` 提供后端：类型 hint
以 `: HANDLE<python::torch::nn::Linear>` 形式追加于
`LET ... = NEW(...)` 声明之后，参数名 hint 以 `x:` 形式出
现于实参之前，两类 hint 可通过设置独立启用 / 关闭。设
计详情见
[`realization/sample_topology_inlay_zh.md`](realization/sample_topology_inlay_zh.md)，
面向用户的章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试覆
盖于
[`tests/unit/polyui/sample_browser_test.cpp`](../tests/unit/polyui/sample_browser_test.cpp)、
[`topology_live_test.cpp`](../tests/unit/polyui/topology_live_test.cpp)
与 [`inlay_hints_test.cpp`](../tests/unit/polyui/inlay_hints_test.cpp)
（162 例 737 条 polyui 断言全绿）。

---

## v1.34.0 (2026-05-05)

**编译流水线 Inspector、IR Viewer / Diff 与 Asm Viewer + 源码↔汇
编联动。**  PolyUI 在 [`tools/ui/common/pipeline/`](../tools/ui/common/pipeline)
下提供一套类 Compiler Explorer 的档案浏览能力。
[`PipelineRun`](../tools/ui/common/pipeline/pipeline_inspector.h)
摄入 `aux/pipeline.json`，呈现六个标准阶段（frontend、sema、
IR pre-opt、IR post-opt、backend asm、link）及其产出路径，并
提供以最长阶段为基准归一化的直方图，便于一眼看出瓶颈。
[`IrModule`](../tools/ui/common/pipeline/ir_viewer.h) 将 LLVM / MLIR
风格转储解析为可折叠的函数与基本块树，`DiffFunctions`
在 pre-opt / post-opt 两版本之间产出基于 LCS 的行级别 diff，
`LineBindingTable` 维护源码↔IR↔产物三者间的双向绑定。
[`AsmModule`](../tools/ui/common/pipeline/asm_viewer.h) 面向
x86_64、arm64、wasm 的反汇编文本进行解析，识别 DWARF 的
`.file`/`.loc` 指令及 polyasm 输出的内联 `; src=文件:行` 注
释，并提供 `AsmForSource` / `SourceForAsm` 以实现双向跳转。
设计详情见
[`realization/compile_pipeline_inspector_zh.md`](realization/compile_pipeline_inspector_zh.md)，
面向用户的章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试覆盖
于 [`tests/unit/polyui/pipeline_inspector_test.cpp`](../tests/unit/polyui/pipeline_inspector_test.cpp)、
[`ir_viewer_test.cpp`](../tests/unit/polyui/ir_viewer_test.cpp) 与
[`asm_viewer_test.cpp`](../tests/unit/polyui/asm_viewer_test.cpp)
（149 例 678 条 polyui 断言全绿）。

---

## v1.33.0 (2026-05-05)

**跨语言导航、Bridge 面板与 Marshalling 可视化。**  PolyUI 在
[`tools/ui/common/cross_language/`](../tools/ui/common/cross_language)
下统一提供跨语言 IDE 能力。
[`LinkRegistry`](../tools/ui/common/cross_language/cross_language_navigator.h)
集中登记所有 `.ploy` `LINK` 点位以及 polyls 解析出的宿主语言
定义：在 `LINK cpp::math::add` 上按 F12 可直跳 C++ 源；宿主语言
定义处会渲染「X `.ploy` LINK references」CodeLens。
[`RenamePlanner`](../tools/ui/common/cross_language/cross_language_navigator.h)
生成贯穿 `.ploy` 点位、宿主语言定义与 LSP 发现的引用的协调
`WorkspaceEdit` 计划，交由 polyls 原子提交。
[`BridgePanelModel`](../tools/ui/common/cross_language/bridge_panel.h)
摄入 polyc 产出的 `aux/bridges.json`，列出每个生成 stub 的
marshalling 策略、源码位置，以及来自 polyrt calltrace 的实时调
用次数——重新导入时运行期计数会被保留。
[`MarshallingViewBuilder`](../tools/ui/common/cross_language/marshalling_view.h)
为每个参数与返回值渲染 IR 下降 → helper → ABI 适配器的转
换链路：如果 `aux/marshalling.json` 已存在则直接解析，否则从
bridge 元数据合成五种宿主语言（C++、Rust、Python、Java、.NET）
的标准三阶段流水线。设计细节见
[`realization/cross_language_ide_zh.md`](realization/cross_language_ide_zh.md)，
面向用户的章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试覆盖于
[`tests/unit/polyui/cross_language_navigator_test.cpp`](../tests/unit/polyui/cross_language_navigator_test.cpp)、
[`bridge_panel_test.cpp`](../tests/unit/polyui/bridge_panel_test.cpp)
与 [`marshalling_view_test.cpp`](../tests/unit/polyui/marshalling_view_test.cpp)
（139 例 626 条 polyui 断言全绿）。

---

## v1.32.0 (2026-05-05)

**包管理、依赖图、漏洞扫描、REPL 与 Notebook。**  PolyUI 在
[`tools/ui/common/packages/`](../tools/ui/common/packages) 下提供
统一的包管理面：[`PackageManagerService`](../tools/ui/common/packages/package_manager.h)
通过十二个后端覆盖 venv、conda、uv、pipenv、poetry、cargo、npm、
maven、gradle、nuget、gem 与 go-mod。每个后端声明清单/锁文件名、
install / upgrade / remove 的 argv，以及把锁文件解析为 `Package`
列表的解析器；服务通过注入的 `CommandExecutor` 调度子进程，因此
整套值模型可以独立做单元测试。`SyncWithConfig` 在解析锁文件与
`.ploy CONFIG` 之间双向比对漂移。
[`DependencyGraph`](../tools/ui/common/packages/dependency_graph.h)
支撑新的「树 + 力导向图」双视图，标记版本冲突，并导出自洽的
SVG 文件。[`VulnerabilityScanner`](../tools/ui/common/packages/vulnerability_scanner.h)
同时解析 osv.dev 与 GitHub Advisory 文档，按 `>=`、`<=`、`>`、`<`、
`==` 及逗号合取匹配版本，并支持按 id 抑制单条告警。Notebook 栈
位于 [`tools/ui/common/notebook/`](../tools/ui/common/notebook)：
[`ReplSession`](../tools/ui/common/notebook/repl_session.h) 通过
可插拔的 `ReplTransport` 包装常驻引擎，默认提供 `polyc --repl`、
Python、IRust、IRB、dotnet-script 五种规格；
[`Notebook`](../tools/ui/common/notebook/notebook.h) 承载代码、
Markdown 与跨语言 `LINK` 单元，并通过 `.polynb` JSON 信封序列化。
设计细节见
[`realization/polyui_package_management_zh.md`](realization/polyui_package_management_zh.md)，
面向用户的章节见 [`USER_GUIDE_zh.md`](USER_GUIDE_zh.md)。测试覆盖于
[`tests/unit/polyui/package_manager_test.cpp`](../tests/unit/polyui/package_manager_test.cpp)、
[`dependency_graph_test.cpp`](../tests/unit/polyui/dependency_graph_test.cpp)、
[`vulnerability_scanner_test.cpp`](../tests/unit/polyui/vulnerability_scanner_test.cpp)
与 [`notebook_test.cpp`](../tests/unit/polyui/notebook_test.cpp)
（545 条 polyui 断言全绿）。

---

## v1.31.0 (2026-05-05)

**测试浏览器、Inline Run-Test 与覆盖率视图。**  PolyUI 在
[`tools/ui/common/testing/`](../tools/ui/common/testing) 下新增统一的
测试视图层：[`TestModel`](../tools/ui/common/testing/test_model.h) 提供
项目 → 套件 → 用例三级树，新「测试浏览器」面板基于它工作；五个
`Parse*Report` 适配器分别解析 CTest CDash XML、JUnit/pytest XML、
cargo libtest JSON 行流、xUnit v2 XML、NUnit 3 XML，状态着色、失败
优先排序与套件汇总均为一等公民。
[`InlineTestLens`](../tools/ui/common/testing/inline_test_lens.h) 在
Catch2、pytest、Rust `#[test]`、JUnit `@Test`、xUnit
`[Fact]`/`[Theory]`、NUnit `[Test]` 声明上方渲染 ▶ 运行 / 🐞 调试
CodeLens，并支持把运行器的失败信息回填为行内诊断。
[`CoverageModel`](../tools/ui/common/testing/coverage_model.h) 把
lcov、Cobertura、coverage.py、cargo-tarpaulin、dotnet coverlet 报告
加载为每文件「行号 → 命中」映射，提供工作区总体百分比与
`BelowThreshold` 阈值过滤，供行号槽位视图使用。

---

## v1.30.0 (2026-05-05)

**任务、Run/Debug 选择器与 Hot Reload。**  PolyUI 在
[`tools/ui/common/tasks/`](../tools/ui/common/tasks) 下新增 VS Code 级
别的任务编排器：[`TaskDefinition`](../tools/ui/common/tasks/task_config.h)
解析 `.polyc/tasks.json`（信封或裸数组），
[`TaskRunner`](../tools/ui/common/tasks/task_runner.h) 按 `dependsOrder`
构建拓扑分批执行计划，
[`ProblemMatcher`](../tools/ui/common/tasks/problem_matcher.h) 识别 `$gcc` /
`$clang` / `$msbuild` / `$tsc` / `$rustc` / `$pylint` / `$polyc` 及
watch 模式的起止标记。新增的
[`RunDebugPicker`](../tools/ui/common/runtime/run_debug_picker.h) 将任务与
`launch.json` 配置合并为单一状态栏快捷菜单；
[`HotReloadEngine`](../tools/ui/common/runtime/hot_reload.h) 按语言路由
保存事件（`.ploy`、Python、走 polyrt 的 C++/Rust、通过 JDI/EnC 的
Java/.NET）并合并重叠请求。详见
[`realization/tasks_runtime_zh.md`](realization/tasks_runtime_zh.md)。

---

## v1.29.0 (2026-05-05)

**Debug Adapter Protocol 集成。**  PolyUI 新增位于
[`tools/ui/common/dap/`](../tools/ui/common/dap) 的与 Qt 无关 DAP
客户端：[`MessageFramer`](../tools/ui/common/dap/dap_client.h) 处理
`Content-Length` 帧；[`DapClient`](../tools/ui/common/dap/dap_client.h)
以序列号待决表路由请求 / 响应，并分发 `stopped` / `continued` /
`output` / `thread` / `breakpoint` / `exited` / `terminated` /
`initialized` 事件。[`DebugSession`](../tools/ui/common/dap/debug_session.h)
在上面叠加断点表、线程/栈/Scope 缓存以及行内变量值映射，适配器
一旦报告 stop 就自动发起 `threads` + `stackTrace` + `scopes`。
[`launch_config`](../tools/ui/common/dap/launch_config.h) 解析
`.polyc/launch.json`（VS Code 规范），展开 `${workspaceFolder}` /
`${file}` / `${env:…}` / `${command:…}` 变量，并内置 `.ploy`、
Python、C/C++、Rust、Java、.NET 等默认模板。详见
[`realization/dap_integration_zh.md`](realization/dap_integration_zh.md)。

---

## v1.28.0 (2026-05-05)

**SCM 进阶：diff / blame / 合并解决器 / 提供者抽象。**  新增与后端
无关的 [`ScmProvider`](../tools/ui/common/scm/scm_provider.h) 接口，
将 IDE 与具体 VCS 解耦；
[`GitProvider`](../tools/ui/common/scm/git_provider.h) 是首个实现，其
 porcelain / blame / log 解析器均以独立函数形式暴露，便于单元测试。
新 [`diff_engine`](../tools/ui/common/scm/diff_engine.h) 以 LCS DP 计算
行级 diff，然后按上下文合并为 hunk，并提供 `ApplyHunk` /
`RevertHunk` 以支持 stage / unstage。
[`merge_resolver`](../tools/ui/common/scm/merge_resolver.h) 解析三向冲突
标记（含可选的 `|||||||` 基准段），支持按 current / incoming /
both 或逐冲突自定义替换进行解决。详见
[`realization/scm_advanced_zh.md`](realization/scm_advanced_zh.md)。

---

## v1.27.0 (2026-05-05)

**多光标 / 折叠 / 格式化 / Snippets / EditorConfig。**  PolyUI 新增 Qt
无关的 [`MultiCursor`](../tools/ui/common/editing/multi_cursor.h)
模型，覆盖 `Alt+Click`、`Ctrl+Alt+↑/↓`、`Ctrl+D`、`Ctrl+Shift+L` 与
矩形 `Shift+Alt+拖拽`。[`folding_model`](../tools/ui/common/editing/folding_model.h)
在一次扫描中计算括号 / 注释 / `// region` 三类折叠。`polyls` 现在由
[`format_engine`](../tools/ui/common/editing/format_engine.h) 驱动实现
`textDocument/formatting`、`rangeFormatting` 与 `onTypeFormatting`，按
括号嵌套重排缩进并尊重 LSP 的格式化选项。Snippet 展开位于
[`snippet_engine`](../tools/ui/common/editing/snippet_engine.h)，支持 VS
Code 风格的 tabstop、选项与变量替换；用户库从 JSON 加载。项目级
格式化由
[`editor_config`](../tools/ui/common/editing/editor_config.h) 指导——这是
一份精简但符合规范的 `.editorconfig` 解析器，含完整 glob
支持。详见
[`realization/power_editing_zh.md`](realization/power_editing_zh.md)。

---

## v1.26.0 (2026-05-05)

**多标签 / 分屏 / Quick Open / 全局搜索 / Outline。**  PolyUI 新增
`EditorGroup` / `EditorGrid` 模型，最多 4×4 分屏，pinned 标签对批量
关闭免疫，分组之间支持标签拖拽
（[`tools/ui/common/editor/`](../tools/ui/common/editor/)）。`Ctrl+P`
打开 Quick Open 面板，由
[`quick_open_ranker`](../tools/ui/common/quickopen/quick_open_ranker.h)
驱动，使用 VS Code 风格的子序列打分，并对最近文件加权。`polyls`
实现 `textDocument/documentSymbol` 与 `workspace/symbol`，并在
`initialize` 中通告 `documentSymbolProvider` /
`workspaceSymbolProvider`；`Ctrl+T` 与 `Ctrl+Shift+O` 走 LSP。
`Ctrl+Shift+F` 由
[`global_search_engine`](../tools/ui/common/search/global_search_engine.h)
支撑，支持正则 / 大小写 / 全词、glob include + exclude、捕获组替换、
流式 `GlobalSearchSink`。共享的
[`outline_model`](../tools/ui/common/outline/outline_model.h) 同时驱动
Outline 面板、Breadcrumbs 与 Minimap。详见
[`realization/editor_panels_zh.md`](realization/editor_panels_zh.md)。

---

## v1.25.0 (2026-05-05)

**语义级语法高亮 + tree-sitter 形态运行时。** 新增解析运行时位于
[`tools/ui/common/syntax/tree_sitter_runtime.{h,cpp}`](../tools/ui/common/syntax/tree_sitter_runtime.h)，对外暴露与 tree-sitter 兼容的接口
（`Parse` / `Edit` / `Tokens` / `Folds` / `Outline` / `SmartSelect`）。
Ploy 与五种宿主语言（C++ / Python / Rust / Java / C#）的 grammar
描述符位于 [`tools/polyls/grammar/`](../tools/polyls/grammar/)，
并固化共享的语义 token legend。`polyls` 实现了
`textDocument/semanticTokens/full` 与
`textDocument/semanticTokens/range`，按 LSP 3.16 规范返回 delta
编码的 `uint32` 流。编辑器通过新增的
[`semantic_tokens_client`](../tools/ui/common/syntax/semantic_tokens_client.h)
消费该流，颜色来自 `theme_manager`；旧的正则版 `SyntaxHighlighter`
作为离线场景下的 fallback 保留。新设置项 “Use LSP semantic
tokens”（默认开启）控制两者切换。详见
[`realization/semantic_highlight_zh.md`](realization/semantic_highlight_zh.md)。

---

## v1.24.0 (2026-05-05)

**IDE 重构：重命名 / 提取函数 / 内联 / 修改签名 / 移动文件。**
`polyls` 现在应答 `textDocument/prepareRename`、
`textDocument/rename` 与 `textDocument/codeAction`。重命名基于工作区
`SymbolIndex` 在单个原子 `WorkspaceEdit` 中重写全部引用——跨文件、跨
语言都会同步。在宿主语言文件（C++ / Python / Rust / Java / .NET）
中发起重命名会自动同步更新引入该符号的 `.ploy` `LINK` / `EXPORT`
位点；反向跳转同样覆盖。`codeAction` 返回重构菜单：提取为 `FUNC`
与内联 `LET` 为具体编辑；内联函数 / 修改签名 / 移动文件 作为灯泡
项与编辑器向导协同。详见
[docs/realization/refactoring_zh.md](realization/refactoring_zh.md)。

同时升级 lifecycle 单测，验证 `renameProvider` / `codeActionProvider` 能力
标志已全部为 true。

---

## v1.23.0 (2026-05-05)

**IDE 跳转：定义 / 声明 / 实现 / 类型定义 / 引用。** `polyls` 现已内建
工作区级 `SymbolIndex`，会扫描 `.ploy` 源码以及它们 `IMPORT` 的所有
宿主语言模块（`cpp` / `python` / `rust` / `java` / `dotnet`）。新增的五个
`textDocument/*` 请求返回 LSP 标准的 `Location[]`，编辑器可直接驱动跳转。
索引随 `didOpen` / `didChange` / `didSave` 增量更新，并持久化到
`<workspace>/.polyc-cache/symbol_index.json`，使下次冷启动无需重新解析
即可应答查询。跨语言跳转双向支持：点击 `.ploy` 中的 `LINK` 限定名可跳转
到宿主语言定义；在宿主文件上发起 `references` 会列出每一处引用它的
`.ploy` `LINK` 位置。设计说明见
[`realization/symbol_index_zh.md`](realization/symbol_index_zh.md)。

## v1.18.0 (2026-05-05)

**P3 收尾：小语法瑕疵集合。** 一组源码层面的细节打磨，便于从
Rust / Python / TypeScript 迁移过来的用户：控制流头部可省略外层
括号、引入 `IF LET` 解构 `OPTION<T>`、`///` 文档注释专用通道，
以及新的 `polydoc` 抽取工具。

* **解析器**：`IF` / `WHILE` / `FOR` 外层括号可选。`IF (cond) { … }`
  与 `IF cond { … }` 等价（表达式语法已把 `(cond)` 视为分组表达式）；
  `FOR (i IN xs) { … }` 显式吸收外层括号。
* **AST + 解析器**：新增 `IfLetStatement` 节点与
  `ParseIfLetStatement` 入口：`IF LET Some(x) = opt { … } ELSE { … }`
  与 `IF LET None = opt { … }` 用于解构 `OPTION<T>`，并将绑定引入
  THEN 体作用域。
* **Sema**：`AnalyzeIfLetStatement` 校验被检查值类型为 `OPTION<T>`
  （或跨语言场景下退化为 `Any` / `Unknown`），核对构造子名称
  （`Some` 需要恰好一个绑定，`None` 不带绑定），并将绑定注册为
  新的 `kVariable` 符号。
* **Sema**：`LET x: OPTION<T> = NULL;` 现在会产生定向
  `kTypeMismatch` 诊断并建议改用 `None`。`NULL` 保留给裸指针互操作。
* **词法 + AST**：以 `///` 开头的行注释会进入 `pending_doc_` 缓冲，
  并作为 `doc_comment: vector<string>` 挂到紧邻的 `FUNC` / `STRUCT`
  / `LET` / `VAR` 声明上。普通 `//` 与 `////` 仍为普通行注释。
* **工具链**：新增 `polydoc` 可执行文件（`tools/polydoc/`），读取
  `.ploy` 源码、遍历已解析模块，将文档块渲染为 Markdown（默认）或
  JSON（`--json`）。可通过 `-o OUT` 写入文件。
* **规范**：明确 `LIST<T>` 为连续序列容器，等价 Rust `Vec<T>` /
  C++ `std::vector<T>`，**不是**链表。
* **测试**：`tests/unit/frontends/ploy/polish_grammar_test.cpp` 新增
  11 条用例，覆盖可选括号、`IF LET`、NULL-with-OPTION 诊断与 `///`
  捕获。示例 41（`tests/samples/41_grammar_polish/`）端到端演练全部
  新构造。

---

## v1.17.0 (2026-05-04)

**扩展字符串字面量形式：原始 `r"..."` / `r#"..."#`、多行
`"""..."""`、以及模板 `f"..."` 插值。** 四种形式共用现有的
`kString` token 类型，并通过既有的 `polyrt_println`
风格驻留路径下沉。

* **词法器。** 新增 `LexRawString`、`LexMultilineString`、
  `LexTemplateString` 辅助方法与原有的 `LexString` 并列；raw 与
  multiline 字面量会被重新编码为规范的 `"..."` 形式，以保证
  既有转义解码管线无须修改。仅在 `r` / `f` 之后紧跟 `"`
  （raw 还允许 `#`）时才将其识别为字符串字面量前缀，
  从而避免 `result`、`foo` 等普通标识符被误识别。
* **AST。** 新节点 `polyglot::ploy::TemplateString` 携带
  `parts: vector<Part>`，每个 `Part` 要么是字面文本片段，要么
  是一个被插值的 `Expression`。
* **解析器。** `BuildStringExpression(lexeme, loc)` 集中处理字符串
  字面量，以 `{` / `}` （支持 `{{` / `}}` 转义）切分 `f"..."`
  主体，并使用一对新的 `PloyLexer` + `PloyParser` 子解析每个
  插值。
* **Sema。** `AnalyzeExpression` 增加 `TemplateString` 分支。每个
  插值表达式需具备**可格式化**类型 — `Int`、`Float`、
  `String` 或 `Bool`（同时接受 `Any` / `Unknown`，以避免未解
  析的跨语言引用阻塞编译）。整体表达式类型恒为 `String`。
* **下沉（MVP）。** 当所有插值都是编译期 `Literal` 时，
  模板字符串在编译期折叠为单个驻留全局常量。一旦模板引用
  运行时值，下沉层仅拼接字面文本片段并发出 `kGenericWarning`
  警告；运行时格式化辅助函数作为后续工作跟踪。
* **测试。** `tests/unit/frontends/ploy/string_literals_test.cpp`
  （13 个用例 / 18 条断言）全部通过；完整回归
  `test_frontend_ploy` 437 / 2603。
* **示例。** `tests/samples/40_string_literals/` 附双语 README。
* **文档。** 双语 realization 文档
  `docs/realization/string_literals{,_zh}.md`、语言规范
  「字符串字面量」子章节、USER_GUIDE 4.2.5f、tutorial 16.9
  （英） / 16.11（中）。
* **未来工作。** 引入运行时格式化辅助函数支持变量插值；
  缩进感知多行去缩进；模板大括号内的格式说明后缀
  （`{value:.3f}`）；插值段内嵌套模板字符串。

## v1.16.0 (2026-05-04)

**模块边界可见性（`PUB` / `PRIVATE`）与顶层 `FUNC` / `STRUCT`
声明的 `@name(args)` 属性前缀。** 可见性与属性以元数据形式穿过
解析与 sema；MVP 不改动 IR 构建器、优化器与运行时。

* **表层语法。** 词法器新增两个保留字：`PUB` 与 `PRIVATE`。解析器
  在 `FUNC`、`ASYNC FUNC`、`STRUCT` 之前接受
  `@name @name(arg, ...) PUB|PRIVATE` 形式的可选前缀。其他顶层
  形式由解析器报告诊断。
* **AST。** `Visibility { kPrivate, kPub }`、
  `Attribute { name, args }`，以及 `FuncDecl` / `StructDecl` 上的
  `visibility` / `visibility_explicit` / `attributes` 字段。
* **Sema。** `PloySymbol` 同步持有可见性，供 `EXPORT` 分析查询。
  `PloySema::ValidateAttributes` 对内建注册表（`inline`、
  `noinline`、`always_inline`、`hot`、`cold`、`profile`、
  `no_profile`、`deprecated`、`link_name`、`target`）外的名称发出
  警告。`AnalyzeExportDecl` 要求 `kPub`：显式 `PRIVATE` 为硬错误，
  仍带默认 `kPrivate` 的符号会被自动升级并发出弃用警告，以保持
  v1.16.0 之前源码可编译。
* **下沉。** 未变；本版本中属性与可见性是 AST 上的惰性元数据。把
  `@inline` / `@hot` / `@profile` 接入优化器、把 `@link_name` /
  `@target` 接入链接器记入后续工作。
* **测试。** `tests/unit/frontends/ploy/visibility_attrs_test.cpp`
  （13 个用例 / 41 条断言）全部通过；完整回归
  `test_frontend_ploy` 424 / 2585。
* **示例。** `tests/samples/39_visibility_attrs/` 为标准端到端示例，
  附双语 README。
* **文档。** 双语 realization 文档
  `docs/realization/visibility_attrs{,_zh}.md`、属性目录
  `docs/specs/attribute_catalog{,_zh}.md`、语言规范关键字花名册与
  「可见性与属性」子章节、USER_GUIDE 4.2.5e、tutorial 16.8（英）/
  16.10（中）。
* **未来工作。** 把每个内建属性接入优化器 / 链接器；把可见性 /
  属性前缀扩展到 `CLASS`、`CONST`、`TYPE`、`MAP_FUNC` 与模块作用域
  `LET`；引入嵌套 `MODULE name { ... }` 以提供更细粒度的作用域；
  在未来主版本中将 `EXPORT` 缺少 `PUB` 的弃用警告升级为硬错误。

## v1.15.0 (2026-05-04)

**Ploy 泛型 `FUNC` / `STRUCT` 与带 bound 的类型参数，类型擦除的 MVP
下沉路径。** Ploy 新增 `<T: Bound, U>` 参数列表、`WHERE` 子句与
内建 trait 注册表；sema 校验每个 bound 名并在解析签名/函数体类型时
把每个参数引入作用域。

* **表层语法。** 词法器新增一个保留字：`WHERE`。解析器在 `FUNC`
  与 `STRUCT` 声明名后接受 `<T: Bound1 + Bound2, U>`，并在 `FUNC`
  返回类型与函数体之间接受可选的
  `WHERE T: Bound1 + Bound2, U: Bound3`。类型位置的 `Pair<i32,
  String>` 等泛型实例化被解析为 `ParameterizedType`。
* **AST。** `FuncDecl::TypeParam { name, bounds }` 与
  `FuncDecl::type_params` / `StructDecl::type_params` 持有声明的
  参数。WHERE 子句在解析阶段并入对应参数的 `bounds`。
* **Sema。** `PloySema::active_type_params_` 由
  `ResolveType(SimpleType)` 优先查询，使得声明内的参数名解析为
  `Any`。`PloySema::ValidateTypeParamBounds` 对照内建注册表
  （`Comparable`、`Hashable`、`Numeric`、`Iterable`、`Display`）
  校验每个声明的 bound。
* **下沉。** 泛型声明仅下沉一次，每个参数解析为 `Any`（类型擦除）；
  单一体服务所有调用点。按实例化的单态化记录为后续工作。
* **测试。** `tests/unit/frontends/ploy/generics_test.cpp`
  （8 个用例 / 35 条断言）全部通过；完整回归
  `test_frontend_ploy` 411 / 2544。
* **示例。** `tests/samples/38_generics/` 为标准端到端示例，附双语
  README。
* **文档。** 双语 realization 文档
  `docs/realization/generics{,_zh}.md`、语言规范关键字花名册与
  「泛型」子章节、USER_GUIDE 4.2.5d、tutorial 16.7（英）/ 16.9
  （中）。
* **未来工作。** 按实例化单态化；调用点上对具体类型的 bound 强校验；
  `LINK` 声明中的参数化泛型；`CLASS` 块的泛型方法；用户自定义 trait
  与高阶 bound。

## v1.14.0 (2026-05-04)

**Ploy 协作式异步与跨语言 `Future<T>` 运行时桥。** Ploy 新增
`ASYNC` / `AWAIT` 构造；运行时公开协作式事件循环与稳定 C ABI，
任意宿主语言适配器均可调用，把 Python `asyncio` 协程、Rust
`Future`、C++20 `std::coroutine`、Java `CompletableFuture`、.NET
`Task<T>` 转发到统一的 `Future<T>` 句柄。

* **表层语法。** 词法器新增两个保留字：`ASYNC` 与 `AWAIT`。
  解析器在顶层、语句、类方法位置接受 `ASYNC FUNC name(...) -> T
  { ... }`，在一元优先级层接受 `AWAIT <expr>`。`ASYNC FUNC` 在
  `FuncDecl` AST 节点上携带 `is_async = true`，`AwaitExpression`
  是独立 AST 节点。
* **Sema。** `ASYNC FUNC` 体外的 AWAIT 以 `kTypeMismatch` 拒绝；
  隐式回填把 `T` 在 ABI 边界包装为 `Future<T>`。
* **下沉。** 每个 `ASYNC FUNC` 体被 `__ploy_rt_async_enter` 与
  `__ploy_rt_async_complete` 包夹；每个 `AWAIT <expr>` 处下沉为
  `__ploy_rt_await(<handle>)`。
* **运行时。** `runtime/services/async_bridge.{h,cpp}` 与
  `runtime/services/event_loop.{h,cpp}` 实现协作式调度器。
  C ABI：`__ploy_rt_async_enter`、`__ploy_rt_async_complete`、
  `__ploy_rt_async_spawn`、`__ploy_rt_await`、
  `__ploy_rt_future_resolve`、`__ploy_rt_async_run`、
  `__ploy_rt_async_pending`、`__ploy_rt_async_suspended`、
  `__ploy_rt_async_completed`、`__ploy_rt_async_active_frames`。
  C++ 接口位于 `polyglot::runtime::services`：`SpawnPloyTask`、
  `ResolveFuture`、`RunUntilIdle`、`SnapshotScheduler`、
  `ResetScheduler`。
* **工具链。** `polyrt async` 输出协作式事件循环的快照；
  `--json` 以 JSON 输出同一负载，`--run[=N]` 在汇报前最多驱动
  `N` 个 tick。
* **测试。** `tests/unit/frontends/ploy/async_await_test.cpp`
  （6 个用例 / 18 条断言）与
  `tests/unit/runtime/async_bridge_test.cpp`
  （5 个用例 / 14 条断言）全部通过；完整回归
  `test_frontend_ploy` 403 / 2509、`test_runtime` 117 / 35257。
* **示例。** `tests/samples/37_async_await/` 为标准端到端示例；
  `tests/samples/14_async_pipeline/` 升级为真实的 `ASYNC FUNC` /
  `AWAIT` 实现，替换原占位形态。
* **文档。** 双语 realization 文档
  `docs/realization/async_model{,_zh}.md`、语言规范关键字花名册与
  「异步 / Await」子章节、USER_GUIDE 4.2.5c、tutorial 16.6
  （英）/ 16.8（中）。
* **未来工作。** 通过 IR 层 `invoke` / `landingpad` 机制实现真正
  的协程挂起；基于 `runtime/threading.{h,cpp}` 的多线程
  work-stealing 线程池；取消传播；跨语言反向适配器
  （`pyloy_async_resolve`、`cppploy_async_resolve`、
  `jloy_async_resolve`、`clrloy_async_resolve`、
  `rsloy_async_resolve`）；待泛型（demand 2026-04-28-15）落地后赋予
  `Future<T>` 参数化类型。

## v1.13.0 (2026-05-04)

**Ploy 结构化异常处理与跨语言运行时错误桥。**Ploy 新增 `TRY` /
`CATCH` / `FINALLY` / `THROW` 构造与内建 `Error` 句柄类型；运行时
公开稳定的 C ABI，任意宿主语言适配器均可调用，把 Python
`Exception`、C++ `std::exception`、Java `Throwable`、.NET `Exception`
或 Rust `Result::Err` 转发为统一的 `Error`。

* **表层语法。**词法新识别五个关键字：`TRY`、`CATCH`、`FINALLY`、
  `THROW`、`ERROR`。Sema 校验每个 `TRY` 至少携带一个 `CATCH` 子句
  或一个 `FINALLY` 子句，每个 `CATCH` 都声明绑定名，`THROW` 携带
  值。
* **内建 `Error` 句柄。**捕获绑定的类型为内建句柄 `Error`，公开
  `message: String`、`source_lang: String`、`stacktrace: List<String>`
  三个字段。
* **运行时桥。**新增 `runtime/include/services/error_bridge.h` C
  ABI：`__ploy_rt_try_begin`、`__ploy_rt_try_end`、`__ploy_rt_throw`、
  `__ploy_rt_throw_from`、`__ploy_rt_current_error`、
  `__ploy_rt_current_error_message`、
  `__ploy_rt_current_error_source_lang`、
  `__ploy_rt_current_error_stacktrace_count`、
  `__ploy_rt_current_error_stacktrace_at`、`__ploy_rt_clear_error`。
  数据平面线程局部；抛出 Error 通过抛出 C++ `RuntimeError` 实现，
  外层任意原生帧均可捕获。
* **下沉。**TRY 下沉为带显式 body / catch / finally / merge 基本块
  的运行时调用形态，依据 `__ploy_rt_try_begin` 返回值进行派发。
  THROW 下沉为单次 `__ploy_rt_throw` 调用后接 `unreachable`。
* **测试。**`tests/unit/frontends/ploy/try_catch_throw_test.cpp`
 （8 个用例 / 30 个断言）覆盖解析与 sema；`tests/unit/runtime/
  error_bridge_test.cpp`（5 个用例 / 19 个断言）通过 C++ `try` /
  `catch` 覆盖 C ABI 数据平面。
* **样例。**`tests/samples/36_try_catch/` 演示该语法，并提供中英双
  语 README。
* **文档。**新增实现说明 `docs/realization/error_handling.md`
 （及 `_zh.md`）；语言规范、`USER_GUIDE.md` 与教程的中英双语版均
  新增"错误处理"子节。
* **未来工作。**类型化 catch 派发、IR 级 `invoke` / `landingpad`
  集成、后缀 `?` 短路传播以及外语异常的完整反向拦截作为后续 demand
  跟踪。

---

## v1.12.0 (2026-05-04)

**字符串化的 `CONFIG` 形式与基于注册表的包管理器分发。**
`.ploy` 中的 `CONFIG` 声明改为统一的
`CONFIG <语言> "<包管理器>" "<路径或环境名>";`，使用同一种语法覆盖
所有受支持的包管理器，无需为每个新管理器引入新关键字。

* **规范字符串形式。** 包管理器名与路径都改为普通字符串字面量，例如
  `CONFIG python "venv" "/opt/envs/ml";`、
  `CONFIG rust "cargo" ".";`、
  `CONFIG javascript "npm" "./node_modules";`、
  `CONFIG java "maven" "./pom.xml";`、
  `CONFIG dotnet "nuget" "./packages";`、
  `CONFIG ruby "bundler" "./Gemfile";`、
  `CONFIG go "gomod" "./go.mod";`。

* **注册表。** 新增的中心化注册表
  `frontends/ploy/src/sema/config_registry.cpp` 维护所有受支持的
  `(语言, 包管理器)` 组合。新增包管理器只需修改一行表项，词法和语法
  分析器无需变动。详见
  [实现 → 包管理 → 注册自定义包管理器](realization/package_management_zh.md#注册自定义包管理器)。

* **旧关键字兼容。** 旧的关键字形式
  （`CONFIG VENV / CONDA / UV / PIPENV / POETRY`）仍可解析，sema 阶段
  会发出 `kDeprecatedKeyword` 警告并给出指向新形式的改写建议，同时自动
  按新形式重写为内部 IR。

* **诊断。** 未注册的 `(语言, 包管理器)` 组合（如
  `CONFIG python "npm" …`）会在 sema 阶段被精确拒绝，错误信息会指出
  违规的组合。

* **样例。** `tests/samples/04_package_import/` 新增三个镜像入口文件，
  分别演示 npm / cargo / maven 三个包管理器在新形式下的写法；现有样例
  （`04`、`09`、`16`）已迁移到字符串形式。

* **测试。** 新增 `tests/unit/frontends/ploy/config_stringification_test.cpp`
  单元测试套件，覆盖注册表、所有受支持的 `(语言, 包管理器)` 组合、旧形式
  弃用警告、未知管理器拒绝以及格式错误的解析器路径。

* **文档。** `docs/specs/language_spec*.md`、`docs/USER_GUIDE*.md` 与
  `docs/realization/package_management*.md` 均已更新为以规范形式为先，
  并补充了自定义包管理器的注册指南。

## v1.11.0 (2026-05-04)

**命名参数默认值、EXTEND 使用限制，以及集中化的 `AS` 语义专章。**
本版本收紧 `.ploy` 中两处长期存在的歧义，并补齐对应的文档与样例。

* **默认参数值。** `FUNC` 声明现在可以为末尾形参提供 `=` 引出的
  默认表达式。默认值必须是常量可折叠的字面量 / 一元 / 二元表达式，
  或一次纯的 ploy 内 `FUNC` 调用；跨语言 `CALL`、闭包捕获、读取
  其他形参都会被拒绝并给出精确诊断。lowering 在每一个省略对应实参
  的调用点物化默认表达式的副本，后端永远看不到"短调用"。

* **调用点的命名实参。** 任何实参都可以按 `name: value` 传递。
  位置实参不可出现在命名实参之后；同一个形参至多被提供一次；每个
  必需（无默认值）的形参必须由位置或名字提供。否则 sema 报告
  `"required parameter 'X' of 'F' is not supplied"`。

* **EXTEND 仅限动态宿主。** `EXTEND(<lang>, ...)` 现在只接受
  `python`、`ruby`、`javascript`（以及标签别名 `rb`、`js`、
  `typescript`、`ts`）。在静态类型语言（`cpp`、`c`、`rust`、
  `java`、`dotnet` / `csharp`、`go` / `golang`）上使用会被拒绝，
  并给出"改为本地 ploy `FUNC` 包装并使用 `CALL` / `METHOD`"的
  修正提示。

* **集中化的 `AS` 语义专章。** `docs/realization/ploy_language_spec_zh.md`
  §4.17（及英文镜像）列出五个绑定位置（`IMPORT … AS`、
  `EXPORT … AS`、`LINK … AS`、语言级 `IMPORT … AS`、以及
  `EXTEND … AS`），并给出一组反例，明确哪些写法属于歧义 / 被禁。

* **样例。** 新增 `tests/samples/34_default_args/`（默认值 + 命名
  调用 + 纯调用默认值）与 `tests/samples/35_extend_dynamic/`
  （Python 上被接受的 `EXTEND` + 静态语言的拒绝与迁移说明）。
  `tests/samples/08_delete_extend/` README 增补"使用限制"小节，
  指向新的规则。

* **测试。** 新增 `tests/unit/frontends/ploy/default_args_and_extend_test.cpp`
  （15 个用例，覆盖签名记录、位置 / 命名 / 混合调用、parser 顺序
  规则、默认值的常量 / 纯调用规则、必需参数覆盖检查，以及 EXTEND
  拒绝集合）。原 `devirtualization_test` 中"EXTEND on Rust base"
  用例改写为断言新拒绝诊断。完整 ploy 前端套件：371 用例 / 2348
  断言。

* **文档。** `docs/USER_GUIDE.md` / `docs/USER_GUIDE_zh.md`、
  `docs/tutorial/ploy_language_tutorial.md` /
  `docs/tutorial/ploy_language_tutorial_zh.md` 与
  `docs/realization/ploy_language_spec.md` /
  `docs/realization/ploy_language_spec_zh.md` 同步更新默认值 /
  命名实参语法、EXTEND 使用限制以及统一 `AS` 表格。

## v1.10.0 (2026-05-04)

**`MATCH` 模式匹配。** `.ploy` 的 `MATCH` 分支现已支持完整的模式
语法 —— 通配、范围、元组 / 结构体解构、OR 模式、绑定（`n @ ...`）、
类型守卫（`x: i32 IF ...`）以及 `OPTION` 构造子（`Some(x)` / `None`），
并配套了详尽性检查、可达性警告与一套两策略 lowering 通路。

* AST：现有 `Pattern` 体系（`WildcardPattern`、`LiteralPattern`、
  `IdentifierPattern`、`RangePattern`、`TuplePattern`、
  `StructPattern`、`OrPattern`、`BindingPattern`、
  `ConstructorPattern`、`TypePattern`）现在被 parser、sema 与
  lowering 端到端地驱动。
* 语法：模式（或守卫）与分支体之间的箭头可选，`->` 与 `=>` 均
  接受。裸标识符 `None` 解析为零参数的 `ConstructorPattern`，
  `CASE None` 才能参与 OPTION 详尽性。`CASE` 后 `Name { ... }`
  通过一字符前瞻消歧，因此 `CASE None { ... }` 不再吞掉分支体。
* Sema：类型相容性、绑定作用域、对 `bool` / `OPTION(T)` 与任意
  类型的详尽性检查、紧跟在不可拒绝分支或重复字面量之后的可达
  性诊断。元组 / 结构体 / 类型守卫模式在其子部分均不可拒绝时
  也被正确识别为不可拒绝。
* Lowering：`LowerMatchStatement` 在两条路径间二选一 —— 全字面量
  且无守卫的快路径生成一条 `ir::SwitchStatement`（允许通配），
  其他形态走 `match.try.N` → `match.body.N` → `match.merge` 的
  结构化级联。
* 测试：[`tests/unit/frontends/ploy/pattern_matching_test.cpp`](../tests/unit/frontends/ploy/pattern_matching_test.cpp)
  端到端覆盖每种模式形态；
  [`tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp`](../tests/unit/frontends/ploy/pattern_matching_lowering_test.cpp)
  锁定派发决策与基本块结构。
* 样例：[`tests/samples/33_pattern_matching/`](../tests/samples/33_pattern_matching/)
  是一个端到端可运行的演示，附带确定性的 stdout 标记。
* 文档：新增 [`docs/realization/pattern_matching.md`](realization/pattern_matching.md)
  / [`pattern_matching_zh.md`](realization/pattern_matching_zh.md)；
  language spec、USER_GUIDE 与 tutorial 中的 MATCH 章节已在中
  英两种文档中扩展同步。需求 2026-04-28-10 标记为 `--end -done`。

---

## v1.9.1 (2026-04-29)

**真实后端 PRINTLN 流水线收尾：`polyc → polyld → exe` 现在能产出
一个 Win32 可执行文件，其 stdout 与源代码中 `PRINTLN` 字面量的字节
完全一致；任何含 PRINTLN 的程序都不再走 `BuildExitZeroPE` 兜底路径。**

### Fixed

- **COFF 重定位记录的盘上步长。** COFF 加载器之前按照
  `sizeof(struct{u32,u32,u16})`（自然对齐后为 12 字节）读取每条重定位
  记录，而盘上的实际步长正好是 10 字节，导致除第一条以外的所有
  重定位都偏移 2 字节，符号下标、类型与偏移全部错乱。所有 COFF
  重定位记录现在都通过 `std::memcpy` 从固定的 10 字节缓冲区中
  逐字段读取。
- **COFF 符号表 aux 记录的下标对齐。** 解析器之前会跳过 aux 辅助
  记录，但同时把生成的 `obj.symbols` 向量也*压缩*掉（不为 aux
  保留槽位），而盘上的重定位记录仍然引用*未压缩*的盘上下标。
  解析器现在为每条被吞掉的 aux 记录都向 `obj.symbols` 追加一个
  占位 `Symbol`（空名、未定义），从而让盘上下标与 `obj.symbols`
  位置一一对齐，恢复正确的重定位回填。
- **`CollectPolyrtPrintlnSequence` 适配新的 IR 实习池命名。**
  恢复 pass 之前只识别历史的 `println.msg<N>` 前缀；当前
  `IRBuilder::MakeStringLiteral` 路径会同时发出 `str<N>` 符号，导致
  恢复 pass 返回空序列，链接器退化到 `BuildExitZeroPE`。该 pass
  现在两种前缀都识别，并要求前缀后必须紧跟数字以严格收口；当
  COFF 符号尺寸为 0 时还能根据同段内下一个已定义符号的偏移
  （或段尾 NUL）反推消息长度。

### Added

- `tests/integration/printf_pipeline_e2e_test.cpp` —
  `[printf][pe7][integration]` 用例。它驱动一段顶层包含两条 `PRINTLN`
  的 `.ploy` 源码穿过 `polyc → polyld`，运行产出的可执行文件，
  在 Win32 上通过 `cmd /c >` 捕获 stdout、在 POSIX 上通过
  `fork/pipe/dup2` 捕获 stdout，然后断言捕获到的字节与
  `"alpha\r\nbeta\r\n"` 完全一致。
- `scripts/build_all_samples.{ps1,sh}` 新增 `--require-min-ok N` /
  `-RequireMinOk N` 参数。逐样本循环结束后，OK 桶中的样本数若少于
  `N`，脚手架进程退出码非零，方便 CI 在不解析 JSON 报告的前提下
  把"可工作样本数下限"作为发布门闸。

### Changed

- 根 `CMakeLists.txt` 的 `project(... VERSION 1.9.1 ...)` 与 `VERSION.txt`
  从 `1.8.0` 升级到 `1.9.1`。

---

## v1.8.0 (2026-04-29)

**Ploy 语法清理：跨语言链接采用全新的标准 / 带签名 `LINK` 形式作为推荐写法，
`STAGE` 升级为保留关键字且只能出现在 `PIPELINE` 体内。**

### 新增

- 标准带签名 `LINK` 形式：

  ```ploy
  LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type>;
  LINK <lang>::<module>::<func> AS FUNC(<param_types>) -> <return_type> {
      MAP_TYPE(<target>, <source>);
  }
  ```

  由新增的 `PloyParser::ParseSignedLinkDecl()` 解析；现有的
  `ParseLinkDecl()` 现在作为分发器，依据 `LINK` 之后是 `(`（旧形式）
  还是标识符（带签名形式）路由到对应实现。

- 新增 `STAGE` 关键字（规范关键字总数 71 → 72）。`ParseStatement`
  仅在解析器当前处于 `PIPELINE` 体内（`PloyParser::in_pipeline_context_`）
  时才接受 `STAGE [name] CALL <lang>::<module>::<func>;`，否则报告
  解析诊断。

- 在 AST 中新增 `LinkDecl::is_legacy_form` 布尔标志和
  `StageDecl{name, call_target, language}` 节点。

- 镜像示例 [`tests/samples/01_basic_linking_v2/`](../tests/samples/01_basic_linking_v2/)
  使用带签名形式重写 v1 跨语言链接示例；v1 的 README 现被标注为
  *legacy form*。

- `tests/unit/frontends/ploy/` 新增两个单元测试：
  `link_deprecation_test.cpp`（验证 `kDeprecatedKeyword` 警告会被发出）
  与 `stage_misuse_test.cpp`（验证 `STAGE` 出现在 `PIPELINE` 之外
  会被解析器拒绝）。

### 变更

- `PloySema::AnalyzeLinkDecl` 在遇到 `is_legacy_form` 为 true 的
  `LinkDecl` 时会发出 `kDeprecatedKeyword` 警告。
- `docs/realization/ploy_language_spec.md`（含 `_zh.md`）§4.2 同时收录
  两种形式，且把带签名形式列在前面作为推荐写法。
- `docs/USER_GUIDE.md`（含 `_zh.md`）：在文档顶部新增弃用通告；
  关键字速查表的 `LINK` 示例更新为带签名形式，并新增 `STAGE` 行。
- `tests/samples/README.md`（含 `_zh.md`）：示例索引收录 v2 镜像示例，
  v1 示例标注为 *legacy form*。

### 兼容性

- Windows / MSVC 上 19 个分模块测试二进制全部通过（1215 用例，
  约 89,000 个断言），现有 Ploy 程序无源码级破坏：旧形式
  `LINK(...)` 仍然能编译，只是会多一条警告诊断。

---

## v1.7.0 (2026-04-29)

**Ploy 新增显式宽度原始类型、`TYPE` 别名以及带常量折叠和宽度警告的编译期
`CONST` 声明。**

### 新增

- 14 个 Ploy 关键字：`i8` / `i16` / `i32` / `i64`、`u8` / `u16` / `u32` /
  `u64`、`f32` / `f64`、`usize` / `isize`，以及 `TYPE` 与 `CONST`。
  全部关键字遵循原有的大小写不敏感词法折叠；规范关键字总数由 56 增至 71。
- `PloySema::ResolveType` 中加入宽度感知的原始类型解析，将新关键字映射
  到类型系统已有的 `core::Type::Int(N, sign)` / `core::Type::Float(N)`
  工厂。
- `TYPE <name> = <type_expr>;` 声明注册命名别名，与 struct 共享查找路径；
  反向映射 `formatted_type → alias_name` 让诊断文本可输出
  `Pixel (alias of i32)` 之类的提示。
- `CONST <name>: <type> = <expr>;` 声明由新的递归折叠器处理，覆盖字面量、
  对此前 `CONST` 的引用、一元 `-` / `!` / `NOT` 以及二元算术、比较、逻辑
  运算（同时支持 `+` 作为字符串拼接）。声明类型与折叠结果出现宽度不匹配
  时按 `kTypeMismatch` 等级发出警告。
- `INT` 与 `FLOAT` 现作为 `i64` / `f64` 的官方别名；旧程序无需改动即可
  继续编译。
- 新增样例 `tests/samples/31_explicit_widths/`，演示 `i32` / `u32` / `i64`
  的跨语言链接（带 C++ 内核），并提供双语 `README.md` / `README_zh.md`
  与标准 `expected_output.txt` 回归产物。
- 新增单元测试集 `type_alias_test.cpp`、`const_decl_test.cpp`、
  `width_mismatch_diag_test.cpp`（18 用例 / 73 断言），以及驱动完整
  lexer + parser + sema 流水线的集成测试 `explicit_widths_e2e_test.cpp`。

### 变更

- `PloySema` 暴露 `TypeAliases()`、`ConstantCount()`、
  `LookupConstantText(name)` 访问器，IDE / 工具层无需反射即可查询别名和
  常量表。
- `PloyLowering` 将 `ConstDecl` 转交不可变 `VarDecl` 路径处理；并在合成
  `main` 分类器中把 `TypeAliasDecl` 视为非执行声明。
- `docs/specs/language_spec.md{,_zh}` 与
  `docs/realization/ploy_language_spec.md{,_zh}` 同步记录新关键字集、
  别名规则、常量折叠契约与原始类型表的更新。
- `tests/samples/README.md{,_zh}` 索引在“显式宽度数值类型”主题下列出
  新样例。

### 兼容性

- 完全向后兼容。所有使用 `INT` / `FLOAT` 的旧程序保持编译通过且 IR 输出
  一致；新表层关键字仅通过额外的查找路径解析。

---

## v1.6.0 (2026-04-29)

**IDE 新增性能分析器与调用分析器面板，由共享的 `ProfileSession`、运行时
call-trace + profile sink 服务以及 `polyc`/`polyrt` 发射器共同驱动。**

### 新增内容

- 运行时服务
  - `runtime/include/services/call_trace.h` — `CallTracer` 单例，提供
    `Enter` / `Exit` / `Drain` / `Peek` / `SerializeJson`，及 C-ABI
    钩子 `__ploy_rt_call_enter` / `__ploy_rt_call_exit` / `_enable` /
    `_is_enabled`。默认关闭；relaxed-load 短路使 LTO 可裁剪。
  - `runtime/include/services/profile_sink.h` — `ProfileSink::Open(path,
    stream_mode)`，支持文档模式（`polyglot.profile.v1`）与 NDJSON 流式模式。
- 中端
  - 中端新增 `InstrumentCallTrace` Pass，按需在非桥接函数前后插入上述钩子。
- polyc 命令行
  - `--emit=call-graph:<path>` 写出 `polyglot.callgraph.v1` 文档。
  - `--emit=profile-symbols:<path>` 写出 id ↔ 全限定名映射。
  - `--profile-instrument` 启用上述 IR Pass。
- polyrt 命令行
  - `polyrt profile [--json|--stream] --duration-ms ... --interval-ms ...`
    采样运行中的进程。
  - `polyrt calltrace --json <path>` 排空当前快照。
- IDE（`polyui`）
  - 新增 **Profiler** 停靠面板 — Flame / Hotspots / Timeline / Languages /
    Log 标签页，由 `ProfileSession` 驱动。`Ctrl+Alt+P` 切换。
  - 新增 **Call Analyzer** 停靠面板 — caller/callee 树 + 分层 DAG 图视图 +
    节点表 + 语言对过滤 + 有界 DFS 路径搜索。`Ctrl+Alt+G` 切换。
  - 两个面板共享同一个 `ProfileSession`，加载 profile 时运行时调用计数会
    自动叠加到 Call Analyzer。
  - 三个新的共享数据模型：`FlameTreeModel`、`CallGraphModel`、
    `TimelineModel`（位于 `tools/ui/common/include/data_models/`）。
- 测试
  - `tests/unit/runtime/call_trace_runtime_test.cpp`：6 用例覆盖 enter/exit、
    嵌套计时、开关、JSON schema、drain 语义、profile sink 文档/流模式。
  - `tests/unit/tools/call_graph_model_test.cpp`：6 用例覆盖邻接表、DFS、
    运行时叠加语义。
  - `tests/unit/tools/profile_session_test.cpp`：3 用例覆盖 JSON 加载器。
  - `tests/unit/tools/profiler_panel_smoke_test.cpp`：3 用例实例化两个面板
    并验证会话共享。
  - `tests/integration/profiler_e2e_test.cpp`：2 用例对 `09_mixed_pipeline`
    和 `15_full_stack` 真实驱动 `polyc` 二进制。
- 文档
  - `docs/realization/{profiler,call_analyzer}{,_zh}.md`
  - `docs/specs/{call_graph_schema,profile_stream_schema}{,_zh}.md`
  - `docs/api/profile_api{,_zh}.md`
  - `docs/tutorial/{profiling,call_analyzer}_quickstart{,_zh}.md`

### 兼容性

- Schema：`polyglot.calltrace.v1`、`polyglot.profile.v1`、
  `polyglot.callgraph.v1`、`polyglot.profilesymbols.v1`。新增字段非破坏性；
  升版后缀保留给布局变更。
- 快捷键有意避开 `Ctrl+Shift+P/G`（IDE 已绑定为命令面板与 Git 面板）。

---

## v1.5.8 (2026-04-29)

**样例矩阵扩展至 30 个主题示例，并交付回归脚本与 Catch2 集成测试。**

### 新增内容

- `tests/samples/` 下原有的 16 个样例（`01_basic_linking` 到
  `16_config_and_venv`）一律补全：
  - 每个 `.ploy` 文件末尾追加 `PRINTLN "<目录名>: ok\r\n";` 标记语句，
    使得每个样例都能向 stdout 输出确定的、可逐字节比对的一行。
  - 每个目录新增 `expected_output.txt`，内容正是上述那一行。
  - 双语 `README.md` / `README_zh.md`，描述涉及的语言、关键字、构建命令
    与预期运行输出。
- 新增 14 个主题样例：`17_string_processing`、`18_numeric_kernels`、
  `19_file_io`、`20_json_pipeline`、`21_image_processing`、
  `22_database_access`、`23_http_client`、`24_concurrency`、
  `25_event_loop`、`26_state_machine`、`27_plugin_system`、
  `28_ml_inference`、`29_data_analytics`、`30_game_loop_demo`。每个目录
  均含 `.ploy` 入口文件、两个可编译的宿主语言源文件（真实代码，无占位）、
  双语 README 与 `expected_output.txt`。
- `scripts/build_all_samples.ps1` 与 `scripts/build_all_samples.sh` 会遍
  历所有样例目录，依次执行 `polyc --emit-obj=…` 与 `polyld … -o …`，
  运行产物并捕获 stdout，按字节与目录下的 `expected_output.txt` 对比，
  最终写出 `build/samples_report.json`。每个样例的状态取自
  `OK / OUTPUT_MISMATCH / EMPTY_STDOUT / RUN_FAIL / LINK_FAIL / COMPILE_FAIL / SKIP`
  其中之一。脚本默认以 0 退出；如需严格门禁，可使用 PowerShell 的
  `-FailOnMismatch` 或 bash 的 `--fail-on-mismatch` 开关。
- 集成测试 `tests/integration/samples_regression_test.cpp`（Catch2 标签
  `[samples][b6][integration]`，注册于 `integration_tests` 二进制下）会
  驱动该脚本并断言生成的 JSON 报告结构合法：覆盖全部样例、所有状态字符
  串都属于上述 7 种枚举之一，且记录的总数与目录中实际计数一致。
- 重写了 `tests/samples/README.md`（及其中文版）以一张目录矩阵列出全部
  30 个样例，并附「按主题分组」「按语言组合分组」两份索引。

### 备注

- 后端流水线尚未在每个样例上做到端到端可执行，因此脚本默认采取宽松
  策略：JSON 报告本身才是该版本的契约。随着越来越多样例的状态收敛到
  `OK`，将自然演变为正式发版门禁。
- 现有测试套件（`test_linker`、`test_frontend_ploy`、`test_e2e`、
  `integration_tests`）不受本版本影响。

---

## v1.5.7 (2026-04-29)

**`polyld` 现在能从输入目标文件中识别 `polyrt_println` 调用点，并产出
带多条消息的 Windows PE 可执行文件。**

### 新增内容

- 新增公开的自由函数
  `polyglot::linker::CollectPolyrtPrintlnSequence(const std::vector<ObjectFile> &)`，
  对链接器已加载的目标文件状态执行一次纯分析过程，返回 IR 层
  `polyrt_println` 下沉所留下的、按源代码顺序排列的解码后消息字节
  序列——这些调用点在每个可执行输入节中表现为
  `(lea message-global, call polyrt_println)` 的成对重定位。该过程：
  - 遍历每个 `ObjectFile` 以及每个带有 `SectionFlags::kExecInstr`
    标志的节。
  - 按 `offset` 字段对每个节的重定位排序，保证那些以文件序而非
    指令序暴露重定位的加载器同样得到正确的调用顺序。
  - 跟踪「最近一次的 `println.msg<N>(.ptr)?` 重定位」作为待配对的
    消息游标，在下一次 `polyrt_println` 重定位出现时与之配对；
    IR 字符串内联器引入的尾缀 `.ptr` GEP 别名会在符号查找前剥离。
  - 通过线性扫描所有已加载 `ObjectFile` 解析消息全局符号（跨翻译单元
    共享同样可用），从其所属节中读取
    `data[symbol.offset .. +symbol.size)` 字节区间，并把解码后的
    内容追加到结果中。
  - 静默跳过孤立调用（之前没有消息重定位）以及无法解析的符号，
    其余有效序列保持完整。
  - 严格保留调用顺序语义——内联器复用同一全局所产生的重复消息会
    在每个调用点都输出一次；底层 `.rdata` 的去重由
    `BuildPrintlnSequencePE` 那一层统一负责。

- `polyglot::linker::Linker::GeneratePEExecutable` 现在调用上述新分析
  过程；当结果向量非空时，转入 `pe::BuildPrintlnSequencePE` 产出真实
  的多行标准输出二进制；否则原样保留既有的
  `pe::BuildExitZeroPE(user_text)` 路径，确保非 PRINTLN 程序行为不变。

### 可观测行为

如下这样最小的 `.ploy` 源：

```ploy
PRINTLN "alpha\r\n";
PRINTLN "beta\r\n";
PRINTLN "alpha\r\n";
```

经 `polyc … && polyld … -o demo.exe` 之后产出的 `demo.exe` 实际写入
标准输出的字节序列正好是

```
alpha
beta
alpha
```

——通过三次真实的 `WriteFile` 写入 `STD_OUTPUT_HANDLE`，随后调用
`ExitProcess(0)`。集成测试
`tests/integration/pe_runtime_smoke_test.cpp`（`[b5]` 标签）会构造
`polyc` 应当产出的 `ObjectFile` 形态、把恢复出的镜像在主机加载器上运行，
并断言退出码与捕获到的字节序列均与预期一致。

### 新增公开 API

- `polyglot::linker::CollectPolyrtPrintlnSequence`
  （位于 `tools/polyld/include/linker.h`）。

### 测试

- `tests/unit/linker/polyrt_println_collect_test.cpp` —— 10 个 Catch2
  用例覆盖：空输入、无 PRINTLN 目标、单次调用、多次调用顺序保持、
  内联器复用全局产生的重复输出、`.ptr` GEP 别名解析、跨目标文件
  数据全局查找、未排序重定位的内部排序、对错误标志节的防御性过滤、
  以及孤立调用的静默跳过。
- `tests/integration/pe_runtime_smoke_test.cpp` —— 新增 `[b5]` 端到端
  用例，对一段含三次调用、其中两次共享同一消息的程序，断言退出码
  为 0 且捕获到的标准输出与源代码顺序的拼接完全相符。

### 已验证的测试套件

`test_linker`（337 断言 / 68 用例）、`test_frontend_ploy`（1907 断言
/ 310 用例）、`test_e2e`（171 断言 / 54 用例）、`integration_tests`
（`[b5]` 过滤，6 断言 / 1 用例）——全部通过。

---

## v1.5.6 (2026-04-29)

**`.ploy` -> `.obj` -> `.exe` -> 真实进程退出码这条链路全部打通。**

### 新增内容

- `polyc` 在 `--obj-format=coff` 下现在产出货真价实的 AMD64 / ARM64
  COFF 目标文件。此前该分支会静默回落到 ELF 写入器，导致 Microsoft
  链接器以 `LNK1107: invalid or corrupt file` 拒绝读取。新增的
  `polyglot::backends::COFFBuilder` 完整铺设 `IMAGE_FILE_HEADER`、
  各节头（`.text`、`.rdata`、`.bss` 等）、节原始数据、按节重定位表、
  COFF 符号表（同时支持 8 字符短名与字符串表长名两种形式）以及字符串
  表，可被 MS `link.exe` 字节级接受。
- `polyld` 的 `DetectObjectFormat` 现在通过 `IMAGE_FILE_HEADER.Machine`
  字段（`0x8664`、`0xAA64`、`0x014C`、`0x01C0`、`0x01C4`、`0x0200`）
  识别 raw COFF 目标。先前的 MZ DOS Stub 启发式只能识别完整 PE 镜像，
  会将 raw `.obj` 误判后回落到 POBJ 分支并丢弃节内容。
- `polyld` 的 Win32 PE 写入器会按 `_start` -> `__ploy_main` -> `main`
  的优先级查找用户入口符号，通过节贡献映射换算出其在合并后 `.text`
  中的偏移，并产出一个 `.exe`：其 `AddressOfEntryPoint` 指向一段
  18 字节的 Win64 ABI shim，由该 shim 调用用户 `main` 并将其 `int`
  返回值（RAX 低 32 位）转交给 `kernel32!ExitProcess`。先前无论源
  程序返回什么，入口都恒指向旧的 `ExitProcess(0)` shim。

### 可观察行为

简单到 `FUNC main() -> i32 { RETURN 42; }` 的 `.ploy` 源代码，经过
`polyc audit_hello.ploy --emit-obj=audit_hello.obj --obj-format=coff
&& polyld audit_hello.obj -o audit_hello.exe` 编译链接后，所产出的
可执行文件在 Windows 下 `GetExitCodeProcess` 返回 **42**；同样的
契约也适用于 `RETURN 0` 与 `RETURN 7`，已在
`tests/integration/ploy_e2e_real_exit_code_test.cpp` 三个用例中
锁定。

### 新增公开 API

- `polyglot::backends::COFFBuilder` —— `ObjectFileBuilder` 的子类，
  发射 AMD64（`is_arm64=false`）或 ARM64（`is_arm64=true`）raw COFF
  目标。
- `polyglot::linker::pe::BuildExeWithUserEntry(user_text_bytes,
  user_main_offset_in_text)` —— 用「先调用用户 main、再
  `ExitProcess(eax)`」的 shim 包裹调用方提供的 `.text`。
- `polyglot::linker::pe::BuildUserMainExitShim(shim_rva, user_main_rva,
  exit_process_iat_rva)` —— 将那 18 字节的 AMD64 shim 编码逻辑暴露
  给单元测试。

### 测试覆盖

- `tests/unit/linker/object_format_detect_test.cpp`（9 个用例）：
  锁定 `DetectObjectFormat` 对 raw COFF（六个识别 Machine 值）、
  带 MZ Stub 的 PE 镜像、ELF、Mach-O 与 POBJ 的处理。
- `tests/unit/backends/coff_builder_test.cpp`（4 个用例）：在字节
  层校验 AMD64/ARM64 头、外部符号在 COFF 符号表中的布局以及通过
  字符串表实现的长节名编码。
- `tests/unit/linker/section_merge_test.cpp`（2 个用例）：回归锁定
  来自真实 COFF 目标的用户 `.text` 字节经过 load -> resolve -> layout
  管线后字节级保留，且多目标 `.text` 输入会被拼接、两段都完整存在。
- `tests/integration/ploy_e2e_real_exit_code_test.cpp`（3 个用例）：
  对字面值 42、0、7 完整跑 `.ploy` -> `.obj` -> `.exe` -> 进程
  生成 -> `GetExitCodeProcess` 烟雾测试。

### 兼容性

- `BuildExitZeroPE` 仍旧导出，并作为「未定义用户入口符号」时的回落
  路径；依赖此前「永远退出码 0」行为的调用方不受影响。
- COFF 检测变化是先前 MZ 规则的真子集，原本被识别为 PE 镜像的
  输入仍然被识别。

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
