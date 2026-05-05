# 二进制容器与目标三元组

## 概览

链接器、代码生成器与命令行工具现在共享同一份"我要产出什么二进制"
的描述。两个值类型承担这一职责，均位于 `common/`：

| 类型                                  | 头文件                              | 用途                                                |
| ------------------------------------- | ----------------------------------- | --------------------------------------------------- |
| `polyglot::common::TargetTriple`      | `common/include/target_triple.h`    | LLVM 风格 `arch-vendor-os-env[-sub]` 描述符。       |
| `polyglot::common::BinaryContainer`   | `common/include/binary_container.h` | 容器族（`kAuto / kELF / kPE / kMachO / kWasm`）。   |

`TargetTriple` 在 CLI 解析阶段产生一次后挂到 `LinkerConfig` 上；
`BinaryContainer` 默认 `kAuto`，链接阶段调用
`ResolveContainer(triple, requested)` 求出真正的容器。

## 三元组解析

`ParseTargetTriple(spec)` 返回 `TripleParseResult` 值（不抛异常、
不退出进程）。下表列出已支持的标准写法：

| 输入                               | Arch        | Vendor   | OS       | Env      |
| ---------------------------------- | ----------- | -------- | -------- | -------- |
| `x86_64-pc-windows-msvc`           | `kX86_64`   | `kPc`    | `kWindows` | `kMsvc`  |
| `x86_64-pc-windows-gnu`            | `kX86_64`   | `kPc`    | `kWindows` | `kGnu`   |
| `aarch64-pc-windows-msvc`          | `kAArch64`  | `kPc`    | `kWindows` | `kMsvc`  |
| `i386-pc-windows-msvc`             | `kX86`      | `kPc`    | `kWindows` | `kMsvc`  |
| `x86_64-apple-darwin`              | `kX86_64`   | `kApple` | `kDarwin`  | —        |
| `aarch64-apple-darwin`             | `kAArch64`  | `kApple` | `kDarwin`  | —        |
| `arm64-apple-macos`                | `kAArch64`  | `kApple` | `kDarwin`  | —        |
| `x86_64-unknown-linux-gnu`         | `kX86_64`   | `kUnknown` | `kLinux` | `kGnu`   |
| `x86_64-unknown-linux-musl`        | `kX86_64`   | `kUnknown` | `kLinux` | `kMusl`  |
| `aarch64-unknown-linux-gnu`        | `kAArch64`  | `kUnknown` | `kLinux` | `kGnu`   |
| `aarch64-linux-android`            | `kAArch64`  | `kUnknown` | `kLinux` | `kAndroid` |
| `armv7-unknown-linux-gnueabihf`    | `kArm`      | `kUnknown` | `kLinux` | `kGnu`   |
| `riscv64-unknown-linux-gnu`        | `kRiscv64`  | `kUnknown` | `kLinux` | `kGnu`   |
| `riscv32-unknown-none-elf`         | `kRiscv32`  | `kUnknown` | `kNone`  | `kEabi`  |
| `wasm32-wasi`                      | `kWasm32`   | `kUnknown` | `kWasi`  | —        |
| `wasm32-unknown-unknown`           | `kWasm32`   | `kUnknown` | `kNone`  | —        |
| `x86_64-unknown-freebsd`           | `kX86_64`   | `kUnknown` | `kFreeBSD` | —        |

解析时折叠的别名：

* 架构：`amd64`、`x64` → `x86_64`；`arm64` → `aarch64`；
  `armv7a` → `armv7`；`i486/i586/i686/x86` → `i386`。
* 系统：`macos`、`macosx`、`ios`、`tvos`、`watchos` → `darwin`；
  `mingw32`、`cygwin`、`win32` → `windows`。
* 环境：`gnueabi`、`gnueabihf` → `gnu`；`musleabi`、`musleabihf`
  → `musl`；`elf`、`eabihf` → `eabi`。

`HostTriple()` 通过 `_WIN32`、`__APPLE__`、`__linux__`、
`__aarch64__`、`_M_X64`、`__wasm__` 等宏推导出宿主机三元组。

## 容器派发

| 三元组 OS           | `ContainerForOS`     |
| ------------------- | -------------------- |
| `kWindows`          | `kPE`                |
| `kDarwin`           | `kMachO`             |
| `kLinux / kFreeBSD` | `kELF`               |
| `kWasi`             | `kWasm`              |
| `kNone / kUnknown`  | `kELF`（默认）       |

`ResolveContainer(triple, requested)` 规则：

1. `requested != kAuto` 则用户优先；
2. 否则若 `triple.arch ∈ {kWasm32, kWasm64}` 返回 `kWasm`；
3. 否则返回 `ContainerForOS(triple.os)`。

## 命令行接入

`polyc` 与 `polyld` 共享两个开关：

* `--target=<triple>`：覆盖宿主机默认三元组。
* `--container=auto|elf|pe|macho|wasm`：覆盖派发结果。

`--target-os=linux|macos|windows` 仍可使用；未显式指定 `--target`
时由编译流水线折回到对应的标准三元组。

## 派发表（链接时生效）

`Linker::ResolveContainerAndTriple()` 在构造函数中运行并锁定
`effective_container_`；`Linker::GenerateOutput()` 随后按
`(OutputFormat, BinaryContainer)` 对映射到具体写出函数：

| OutputFormat        | kELF                        | kPE                  | kMachO                   | kWasm                |
| ------------------- | --------------------------- | -------------------- | ------------------------ | -------------------- |
| `kExecutable`       | `GenerateELFExecutable`     | `GeneratePEExecutable` | `GenerateMachOExecutable` | `GenerateWasmModule` |
| `kSharedLibrary`    | `GenerateELFSharedLibrary`（`ET_DYN`） | `GeneratePEDll`      | `GenerateMachODylib`（`MH_DYLIB`） | `GenerateWasmModule` |
| `kStaticLibrary`    | `GenerateStaticLibrary`（与容器无关） | ↑ | ↑ | ↑ |
| `kRelocatable`      | `GenerateRelocatable`（与容器无关） | ↑ | ↑ | ↑ |
| `kPEExecutable`     | 总是 `GeneratePEExecutable`（遗留别名） | | | |

`OutputFormat::kExecutable` 与 `BinaryContainer::kAuto` 同时到达
会报结构化错误：调用者必须让
`ResolveContainerAndTriple()` 先将容器赋值，不允许在 `kAuto`
状态下调用 `GenerateOutput()`。

### 后缀策略

`SuffixesFor(container)` 返回某容器下可执行 / 共享 / 静态 /
目标文件的规范后缀。`polyc`、`polyld` 调用
`SuffixMatchesContainer(path, container, kind)`，在用户传入的
`-o` 路径与解析出的容器不一致时报 `polyc-warn-W2101`。
Unix 可执行文件容许任意后缀（或无后缀），但 `.exe` 与
`.wasm` 仅保留给各自的容器。

## 迁移指南

| 旧接口                                       | 新接口                                            |
| -------------------------------------------- | ------------------------------------------------- |
| `LinkerConfig::target_arch`                  | `LinkerConfig::target_triple`（旧字段保留兼容）   |
| `GenerateOutput()` 中硬编码 ELF              | BIN-2 阶段按 `effective_container_` 派发          |
| 后端 `EmitObjectCode()`                      | BIN-2 阶段加入接收 `TargetTriple` 的重载          |

## 已知限制

* 本次仅落实抽象层；`GenerateOutput()` 的真正派发与
  后端三元组重载将在 BIN-2 完成。
* `Vendor` 暂只覆盖 `pc / apple / unknown`，未建模
  `nintendo / sony` 等小众厂商。
* `SubArch` 在 ARM 上仅区分 `v7 / v8 / v9`，更细粒度
  （如 `armv8.2-a+sve2`）由后端单独维护。

## PE 路径细节（BIN-4）

Windows PE 路径根据 `LinkerConfig::output_format` 分发到两个写出器：

- `kPEExecutable`：`tools/polyld/src/linker.cpp` 中的 `Linker::GeneratePEExecutable()` 按 `_start` → `__ploy_main` → `main` 的优先级解析用户入口（可被 `--entry` 覆盖），并分发到 `pe::BuildExeWithUserEntry` / `pe::BuildPrintlnSequencePE` / `pe::BuildExitZeroPE`。
- 在 Windows triple 下的 `kSharedLibrary`：`tools/polyld/src/linker_pe.cpp` 中的 `Linker::GeneratePEDll()` 通过 `BuildRequest::extra_file_characteristics` 写入 `IMAGE_FILE_DLL`（0x2000），由 `pe::BuildExportSection` 构造 `IMAGE_EXPORT_DIRECTORY` 字节并在镜像生成后回填可选头中的导出数据目录项。

### 导出描述符

导出来自三个独立源并自动合并：

| 来源 | polyld 入口 | 说明 |
| --- | --- | --- |
| `--def <file>` | `pe::ParseDefFile` | 解析 `LIBRARY`、`EXPORTS` 段；容忍 `IMPORTS`、`STUB` 等。 |
| `/EXPORT:<spec>`（cl 风格）或 `--export <spec>`（GNU 风格） | `pe::ParseCliExportSpec` | 与 `EXPORTS` 单行语法一致。 |
| `__declspec(dllexport)` | 保留 `pe::ExportSource::kDeclSpec` | 后续补丁由前端接入。 |

`pe::MergeExports` 按公开名去重，并将互不相容的重复声明上报为 `polyld-err-E3201`。完全相同的声明被静默合并；当某个源仅缺少序号等可补全字段时，会就地合入更具体的条目。

### 导出段布局

`pe::BuildExportSection(exports, dll_name, edata_rva)` 产出顺序为：

```
IMAGE_EXPORT_DIRECTORY  (40 B)
Export Address Table    (按 ordinal − Base 索引，每槽 4 B)
Name Pointer Table      (按字典序排序，每名 4 B)
Ordinal Table           (与 NPT 一一对应，每名 2 B)
DLL 名称                (ASCIZ)
导出名称字符串            (按 NPT 顺序，逐条 ASCIZ)
```

自动分配序号从 1 起步，跳过被 `@N` 显式占用的值。`NONAME` 导出占据 EAT 槽位，但不出现在 NPT/EOT 中。`DATA` 标记会被透传作为提示；当前写出器不在 EAT 槽位上区分数据与代码。

### 重定位翻译

`pe::TranslateRelocationsToPEBaseRelocs(input, machine, out, errors)` 把链接器中性的 `Relocation` 记录转换为写出器需要的 `BaseRelocation` 数组：

| 内部类型 | x86_64 PE | arm64 PE |
| --- | --- | --- |
| `R_X86_64_64`、`R_AARCH64_ABS64` | DIR64 (10) | DIR64 (10) |
| `R_X86_64_32`、`R_X86_64_32S`、`R_AARCH64_ABS32` | HIGHLOW (3) | HIGHLOW (3) |
| `R_AARCH64_CALL26` / `JUMP26` | — | ARM64 BRANCH26 (3) |
| `R_AARCH64_ADR_PREL_PG_HI21` | — | ARM64 PAGEBASE_REL21 (4) |
| `R_AARCH64_ADD_ABS_LO12_NC` | — | ARM64 PAGEOFFSET_12A (7) |
| `R_AARCH64_LDST64_ABS_LO12_NC` | — | ARM64 PAGEOFFSET_12L (9) |

PC 相对条目直接跳过（PE 基址重定位只修正绝对地址）。其他类型——包括仅在 ELF 上有意义的 `R_X86_64_GOTPCREL` 等 GOT/PLT 编码——会被记录为 `polyld-err-E3210` 并使函数返回 `false`；为了一次性暴露完整的不兼容集合，剩余条目仍会继续翻译。
