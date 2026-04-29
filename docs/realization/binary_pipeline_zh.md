# 二进制管线：`.ploy` -> `.obj` -> `.exe` -> 进程退出码

本文档描述将 `.ploy` 源文件转换为 Windows AMD64 进程、且其退出码等于
源程序 `main` 返回值的端到端原生二进制管线。它是
[`compilation_model_zh.md`](compilation_model_zh.md)（前端 / IR 侧）与
[`runtime_stdout_pipeline_zh.md`](runtime_stdout_pipeline_zh.md)
（正交的 `polyrt_println` 标准输出路径）的补充。

英文版本见 [`binary_pipeline.md`](binary_pipeline.md)。

---

## 阶段总览

```
   .ploy 源代码
       |
       |  polyc（前端 + IR + 后端）
       v
   COFF 目标文件   （IMAGE_FILE_HEADER + 节 + 符号 + 重定位）
       |
       |  polyld（load -> resolve -> layout -> emit）
       v
   PE32+ 镜像      （MZ + PE\0\0 + Optional Header + .text + .idata）
       |
       |  Windows 加载器创建进程；入口 shim 运行
       v
   进程退出码      （== 用户 main 返回值）
```

每条箭头都有专门的单元或集成测试锁定，逐阶段列出如下。

---

## 阶段 1：`polyc --emit-obj=<path> --obj-format=coff`

`polyc` 后端阶段最终调用
`tools/polyc/src/compilation_pipeline.cpp::BuildNativeObjectBinary`，
按目标格式分派到三个 `polyglot::backends::ObjectFileBuilder` 子类之一：

| `--obj-format` | builder         | 输出       |
|----------------|-----------------|------------|
| `elf`          | `ELFBuilder`    | ELF64      |
| `macho`        | `MachOBuilder`  | Mach-O 64  |
| `coff`         | `COFFBuilder`   | raw COFF   |

`COFFBuilder` 位于 `backends/common/{include,src}/object_file.{h,cpp}`，
按以下顺序产出字节：

1. `IMAGE_FILE_HEADER`（20 字节），`Machine = 0x8664`（AMD64）或
   `0xAA64`（ARM64），并在符号表位置确定后回填
   `NumberOfSections`、`PointerToSymbolTable`、`NumberOfSymbols`。
2. 每个 `Section` 一份 `IMAGE_SECTION_HEADER`（40 字节），节
   特征位由节名推断：`.text`/`.code`/`__text` ->
   `IMAGE_SCN_CNT_CODE | MEM_EXECUTE | MEM_READ`；
   `.rdata`/`.rodata`/`__const` ->
   `IMAGE_SCN_CNT_INITIALIZED_DATA | MEM_READ`；
   `.bss`/`__bss` ->
   `IMAGE_SCN_CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE`；
   其余按已初始化的可读写数据处理。
3. 各节原始字节，紧随各节的 `IMAGE_RELOCATION` 记录。
4. COFF 符号表：每节贡献一个 `static` 节符号及其 18 字节
   aux 记录，其后是全部用户符号。≤ 8 字节的符号名直接装入
   `Name.ShortName`；更长的名字写入字符串表，并通过
   `Name.LongName.{Zeroes=0, Offset}` 引用。
5. 字符串表：4 字节总长头部，紧跟以 NUL 结尾的长名字符串。

`tests/unit/backends/coff_builder_test.cpp` 锁定的契约是：所产
字节被 Microsoft `link.exe` 字节级接受。原本的
`LNK1107: invalid or corrupt file` 错误消失；之后再出现的链接器
报错（例如 `polyc` 内部调用 `link.exe` 打包 `a.out` 时报
`LNK2001` 缺失 CRT 符号）属于合法的符号解析诊断，与文件格式
无关。

---

## 阶段 2：`polyld <object> -o <exe>` —— 格式检测

`tools/polyld/src/linker.cpp::DetectObjectFormat` 检查输入文件的
首部字节并分派到 `LoadELF` / `LoadMachO` / `LoadCOFF` / `LoadPOBJ`
之一。COFF 规则分两支：

* 若文件以 MZ DOS Stub（`'M', 'Z'`）开头，按完整 PE 镜像处理，
  加载器会沿 `e_lfanew` 找到内嵌的 `IMAGE_FILE_HEADER`。
* 否则将首两字节解释为 `IMAGE_FILE_HEADER.Machine` 字段。共有
  六种值会触发 COFF 加载器：`0x8664`（AMD64）、`0xAA64`（ARM64）、
  `0x014C`（i386）、`0x01C0`（ARM）、`0x01C4`（ARMNT）、
  `0x0200`（IA64）。其它值穿透到下一个检测器（Mach-O 或 POBJ）。

之前的实现只有 MZ 这一支，会静默误判 raw COFF 目标文件。
回归被 `tests/unit/linker/object_format_detect_test.cpp`（9 个用例）
锁定，其中包含一个 ELF-magic 的用例，明确「内容为 ELF 的 `.obj`
**不是** COFF」。

---

## 阶段 3：`polyld` —— 加载、解析、布局

`Linker::LoadCOFF` 解析各 `IMAGE_SECTION_HEADER`，为每个节设置
`SectionFlags::kAlloc`，并对 `IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE`
节追加 `kExecInstr`。这些标志位为
`Linker::MergeInputSections` 所必需，否则 `--strip-debug` /
`--strip-all` 路径会丢弃节内容。

`Linker::MergeInputSections` 按名称把输入节分组，将其字节拼接到
同名的 `OutputSection` 中，并为每段输入记录一份
`OutputSection::Contribution {object_index, section_index,
input_offset, output_offset, size}`。该映射是把输入侧
`(obj, sec, offset)` 三元组翻译为输出偏移的唯一权威来源。

`tests/unit/linker/section_merge_test.cpp`（2 个用例）锁定：

* 单个 COFF 目标，其 `.text` 携带已知 sentinel 字节序列，最终
  `output_sections_['.text']` 的前 `Sentinel().size()` 字节字节级
  等于该 sentinel。
* 两个 COFF 目标各自携带不同 sentinel，最终 `.text` 同时包含两段
  连续字节（顺序不限），链接器既不会覆盖也不会静默丢弃任何一段。

---

## 阶段 4：`polyld` —— PE 写入器选择入口

`Linker::GeneratePEExecutable` 收集合并后的 `.text` 字节，并按
优先级查找用户入口符号：

1. `_start`
2. `__ploy_main`
3. `main`

第一个 `is_defined == true` 的命中会通过输出 `.text` 节的
contribution 映射换算到合并后的偏移：对每条 `(object_index,
section_index)` 与符号源端坐标匹配的 contribution `c`，合并偏移
为 `c.output_offset + symbol.offset`。

找到用户入口时，PE 写入器走 `pe::BuildExeWithUserEntry(user_text,
user_entry_offset)`；否则走历史路径
`pe::BuildExitZeroPE(user_text)`，确保镜像仍能干净退出。
verbose 日志中的 `path_tag`（`"user entry"` / `"exit-zero shim"`）
可观察到该分派结果。

该顺序也兼容 v1.5.5 上线的 `polyrt_println` 标准输出路径：当任意
输入目标携带 `polyrt_println` 重定位踪迹时，
`BuildPrintlnSequencePE` 优先于「用户入口」与「零退出」两条路径。

---

## 阶段 5：PE 写入器 —— 18 字节用户入口 shim

`pe::BuildExeWithUserEntry` 将 `.text` 布局为
`[user_text][user-entry shim]`，并把 `AddressOfEntryPoint` 指向
shim 而非用户 main。shim 本身共 18 字节，由
`pe::BuildUserMainExitShim` 编码：

```
off    bytes  asm
0x00   4      sub  rsp, 0x28                       ; 影子空间 + 16 字节对齐
0x04   5      call user_main                       ; E8 disp32
0x09   2      mov  ecx, eax                        ; arg0 = 用户返回值
0x0B   6      call qword ptr [rip + d_ExitProcess] ; FF 15 disp32
0x11   1      int3                                 ; 不可达
```

两个 `disp32` 字段都按 `shim_rva` 编码：

* `disp_user = user_main_rva - (shim_rva + 4 + 5)`（E8 之后的 RIP
  位于 `shim_rva + 9`）。
* `disp_exit = exit_iat_rva - (shim_rva + 0x0B + 6)`（FF 15 之后的
  RIP 位于 `shim_rva + 0x11`）。

Win64 ABI 的 `sub rsp, 0x28` 同时预留 32 字节影子空间和 OS 调入
入口时压入的 8 字节返回地址带来的对齐补偿（保证两条内层 CALL
之前 RSP 都是 16 字节对齐）。

镜像内嵌的 `.idata` 导入目录恰好携带一个导入项：
`kernel32.dll!ExitProcess`。其 IAT 槽的 RVA 在构建时即被解析，
shim 中的 `disp32` 在字节发射前就能定下来——和写入器其它部分
「先布局、后回填 disp32」的模式完全一致。

---

## 阶段 6：端到端退出码

`tests/integration/ploy_e2e_real_exit_code_test.cpp` 锁定如下契约：

| `.ploy` 函数体                              | 观察到的 `GetExitCodeProcess` |
|---------------------------------------------|--------------------------------|
| `FUNC main() -> i32 { RETURN 42; }`         | `42`                           |
| `FUNC main() -> i32 { RETURN 0;  }`         | `0`                            |
| `FUNC main() -> i32 { RETURN 7;  }`         | `7`                            |

测试用例用真正的 `polyc.exe` 和 `polyld.exe`（通过
`GetModuleFileNameA` 在测试进程同目录定位）编译每段源码，再用
`std::system` 启动产出的 `.exe`，断言其退出码。

这是后续阶段所依赖的保证强度：任何打断这条链路的改动（前端、
IR 下沉、COFF 写入、格式检测、节合并、入口选择、PE 写入、
重定位编码、IAT 解析）都会在合入发布分支前被捕获。
