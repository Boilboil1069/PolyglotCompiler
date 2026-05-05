# PE32+ 写出器（加固版）

本文档记录 `tools/polyld/src/pe_writer.cpp` 的多节、基址重定位与目标可配
置特性，是 `binary_containers_zh.md`（容器分发层）的下钻补充，重点描述
PE32+ 写出器实际写出的字节布局。

## 节区清单

`BuildPE32PlusImage` 按以下规范顺序规划节区，仅当对应请求字段非空时才
真正发射：

| 名称     | 来源字段                    | Characteristics                                 | 备注                                       |
| -------- | --------------------------- | ----------------------------------------------- | ------------------------------------------- |
| `.text`  | `text_bytes`                | `CODE | EXEC | READ`                            | 始终存在。                                 |
| `.data`  | `data_bytes`                | `INITIALIZED_DATA | READ | WRITE`               | 已初始化的可写全局。                       |
| `.rdata` | `rdata_bytes`               | `INITIALIZED_DATA | READ`                       | 只读常量 / 字符串。                        |
| `.bss`   | `bss_size`                  | `UNINITIALIZED_DATA | READ | WRITE`             | 零填充；`SizeOfRawData = 0`。              |
| `.pdata` | `pdata_bytes`               | `INITIALIZED_DATA | READ`                       | x64 `RUNTIME_FUNCTION[]`，写入 Data Directory[3]（Exception）。 |
| `.xdata` | `xdata_bytes`               | `INITIALIZED_DATA | READ`                       | 与 `.pdata` 配对的 `UNWIND_INFO`。         |
| `.idata` | `imports`                   | `INITIALIZED_DATA | READ | WRITE`               | 导入描述符 + IAT + Hint/Name 表。          |
| `.reloc` | `base_relocations`          | `INITIALIZED_DATA | DISCARDABLE | READ`         | 按页分组的 IMAGE_BASE_RELOCATION 块。      |

所有节虚拟对齐到 `0x1000`，文件对齐到 `0x200`，节 RVA 严格单调递增，顺
序与上表一致。

## 基址重定位编码

`BuildBaseRelocSection(relocs)` 按 4 KiB 虚拟页对条目分组，每页发射一个
块：

```
+0  PageRVA       (4 字节，page_rva = rva & ~0xFFF)
+4  SizeOfBlock   (4 字节，头 + 条目，向上取整为 4 的倍数)
+8  WORD 条目     (高 4 位 = 类型，低 12 位 = 页内偏移)
```

当某页条目数为奇数时，追加一个 `IMAGE_REL_BASED_ABSOLUTE`（type=0）填
充条目，使 `SizeOfBlock` 满足规范要求的 4 字节对齐。`DecodeBaseRelocSection`
是反向函数，单元测试用它做编码器的回环验证。

当前覆盖的类型值：

| 值    | 名称                                    | 体系结构    |
| ----- | --------------------------------------- | ----------- |
| 0     | `IMAGE_REL_BASED_ABSOLUTE`              | （填充）    |
| 3     | `IMAGE_REL_BASED_HIGHLOW`               | x86         |
| 3     | `IMAGE_REL_BASED_ARM64_BRANCH26`        | arm64       |
| 4     | `IMAGE_REL_BASED_ARM64_PAGEBASE_REL21`  | arm64       |
| 7     | `IMAGE_REL_BASED_ARM64_PAGEOFFSET_12A`  | arm64       |
| 9     | `IMAGE_REL_BASED_ARM64_PAGEOFFSET_12L`  | arm64       |
| 10    | `IMAGE_REL_BASED_DIR64`                 | x64         |

非空 `.reloc` 节会切换两个头部位：

* `IMAGE_FILE_RELOCS_STRIPPED`（COFF Characteristics bit 0）会被**清除**。
* 当 `dll_characteristics == 0` 时，写出器会**置位**
  `IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE`（x64 / arm64 还会同时置位
  `HIGH_ENTROPY_VA`）。

## Machine / Subsystem / DllCharacteristics

| 请求字段                     | 头字段                       | 默认值                                                          |
| ---------------------------- | ---------------------------- | --------------------------------------------------------------- |
| `machine`                    | `Machine`                    | `IMAGE_FILE_MACHINE_AMD64` (0x8664)                             |
| `subsystem`                  | `Subsystem`                  | `IMAGE_SUBSYSTEM_WINDOWS_CUI` (3, 控制台)                       |
| `dll_characteristics`        | `DllCharacteristics`         | `NX_COMPAT | TERMINAL_SERVER_AWARE`（存在 `.reloc` 时再加 `DYNAMIC_BASE | HIGH_ENTROPY_VA`） |
| `extra_file_characteristics` | `Characteristics`（按位或）  | `0`，DLL 场景请传 `0x2000`（`IMAGE_FILE_DLL`）。                |

`PEMachine::kI386` 仍然产出 PE32+ 镜像（Optional Magic 保持 `0x20B`），
仅 `Machine = 0x014C`，主要用于跨格式检测管线中的元数据标记。

## Data Directory 接线

| 索引  | 目录              | 来源                                    |
| ----- | ----------------- | --------------------------------------- |
| 1     | Import            | `BuildImportSection(imports)`           |
| 3     | Exception         | `.pdata` 节 RVA / 大小（无则为 0）       |
| 5     | Base Relocation   | `.reloc` 节 RVA / 大小（无则为 0）       |
| 12    | IAT               | `.idata` 子区间                         |

其余目录项均为 0。

## 调试 cheat sheet

* **快速核对头部** ─ `dumpbin /headers polyc.exe`，确认 `machine
  (x64)`（或 `(ARM64)`）、`subsystem (Windows CUI/GUI)`，并在
  `DLL characteristics` 行看到 `Dynamic base` 与 `High Entropy Virtual
  Addresses`（存在重定位时）。
* **检查节表** ─ `dumpbin /headers` 列出每节的 `Characteristics`，`.bss`
  应显示 `BSS / Read Write` 且 `Raw Data` 为 0。
* **校验基址重定位** ─ `dumpbin /relocations polyc.exe`，每页块应汇报
  `RVA` 与编码器写出的条目数。
* **反汇编异常数据** ─ `dumpbin /exports /imports /unwindinfo polyc.exe`
  交叉验证 `.pdata` / `.xdata`。
* **测试中回环验证** ─ 使用 `DecodeBaseRelocSection` 把编码器输出还原为
  原 `BaseRelocation` 集合做对比。
