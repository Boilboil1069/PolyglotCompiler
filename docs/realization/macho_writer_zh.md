# Mach-O 写出器（真实布局）

本文档描述位于 `tools/polyld/src/linker_macho.cpp` 与
`tools/polyld/include/linker_macho.h` 的 Mach-O 镜像写出器，作为
[binary_containers_zh.md](binary_containers_zh.md)（覆盖 polyld 派发层）
的补充，聚焦于 `MH_EXECUTE`、`MH_DYLIB`、`MH_BUNDLE` 三种 filetype 在
`x86_64` 与 `arm64` 下落盘字节的真实形状。

写出器与 `Linker` 类的数据流解耦：它接收 `BuildRequest` 值类型，返回
`BuildResult`（包含成品镜像与若干常用偏移）。实现末尾的三个
`Linker::GenerateMachO*` 成员方法仅是薄封装，将 `Linker` 状态投影成
`BuildRequest`，调用 `BuildMachOImage`，并把结果写盘。

## Header

镜像前 32 字节是一个 `mach_header_64`：

| 偏移 | 字段          | 取值                                                            |
| ---: | ------------- | --------------------------------------------------------------- |
|    0 | `magic`       | `0xFEEDFACF`（`MH_MAGIC_64`）                                   |
|    4 | `cputype`     | x86_64 = `0x01000007`，arm64 = `0x0100000C`                     |
|    8 | `cpusubtype`  | `3`（`CPU_SUBTYPE_ALL`）                                        |
|   12 | `filetype`    | 可执行 `2`，dylib `6`，bundle `8`                               |
|   16 | `ncmds`       | 后续 load command 数量                                          |
|   20 | `sizeofcmds`  | 所有 load command 的 `cmdsize` 累加                             |
|   24 | `flags`       | 默认 `MH_NOUNDEFS|MH_DYLDLINK|MH_TWOLEVEL|MH_PIE`               |
|   28 | `reserved`    | 64 位下固定 `0`                                                 |

## Load command 排布

写出器恒定按以下顺序排列 load command，`otool -l` 与 `lldb` 看到的
形状与 Apple 工具链产物一致：

| 序 | 命令                     | 用途                                                              |
| -: | ------------------------ | ----------------------------------------------------------------- |
|  1 | `LC_SEGMENT_64`(`__PAGEZERO`) | 仅可执行；4 GiB 不可映射的保护段                            |
|  2 | `LC_SEGMENT_64` × N      | 每个用户 `SegmentDesc`（`__TEXT`、`__DATA_CONST` ...）            |
|  3 | `LC_SEGMENT_64`(`__LINKEDIT`) | 容纳符号表、字符串表、间接表、代码签名占位                  |
|  4 | `LC_SYMTAB`              | offset / nsyms / stroff / strsize                                 |
|  5 | `LC_DYSYMTAB`            | local / extdef / undef 计数，其余字段置零                         |
|  6 | `LC_LOAD_DYLINKER`       | `/usr/lib/dyld`                                                   |
|  7 | `LC_LOAD_DYLIB`          | `/usr/lib/libSystem.B.dylib`，`current=1.0.0`、`compat=1.0.0`     |
|  8 | `LC_UUID`                | 节内容 SHA-256 截断 16B 摘要                                      |
|  9 | `LC_BUILD_VERSION`       | `platform=macOS`，`minos`、`sdk` 三字节三元组                     |
| 10 | `LC_SOURCE_VERSION`      | 由 `BuildRequest::source_version` 打包得到 `A.B.C.D.E`            |
| 11 | `LC_MAIN` 或 `LC_ID_DYLIB` | 可执行写入口偏移；dylib 写 install name                       |
| 12 | `LC_CODE_SIGNATURE`      | 可选 ad-hoc 占位（4 KiB SuperBlob 窗口）                          |

bundle filetype 省略 `__PAGEZERO` 与 `LC_MAIN`，且不写
`LC_ID_DYLIB`，其余与可执行共享同一布局，写出器只维护一条主路径。

## `__LINKEDIT` 内部布局

`__LINKEDIT` 段内字节按下列顺序排布：

```
+0                 nlist_64[] 符号表           （每条 16 字节）
+nsyms*16          char[]     字符串表         （NUL 结尾，ASCII）
+strtab+strsize    向 16 对齐填充
+...               LC_CODE_SIGNATURE 区域       （仅在请求时存在）
```

`LC_CODE_SIGNATURE` 占位窗口为 4 KiB，起始为一个空的
`CS_SuperBlob` 头（`magic=0xFADE0CC0`，`length=8`，`count=0`），可让
`codesign --display` 识别该镜像；正式签名可后续通过
`codesign --force --sign -` 注入。

## 重定位翻译

来自上游 IR 的重定位通过 `TranslateRelocations(arch, in, out, errors)`
映射至落盘 `relocation_info` 字节形（每条 8 字节）。无法识别的
`r_type` 上报 `polyld-err-E3220`。

| `MachOArch` | 识别的类型 |
| ----------- | ---------- |
| `kX86_64`   | `UNSIGNED`、`SIGNED`、`BRANCH`、`GOT_LOAD`、`GOT`、`SUBTRACTOR`、`SIGNED_1/2/4`、`TLV` |
| `kArm64`    | `UNSIGNED`、`SUBTRACTOR`、`BRANCH26`、`PAGE21`、`PAGEOFF12`、`GOT_LOAD_PAGE21`、`GOT_LOAD_PAGEOFF12`、`POINTER_TO_GOT`、`TLVP_PAGE21`、`TLVP_PAGEOFF12`、`ADDEND` |

即使同批次中存在错误记录，能够正确翻译的记录也会保留，从而让调用
方一次拿到完整的坏记录列表。

## UUID 派生

`Uuid16FromContent(bytes)` 对参数做 SHA-256 散列后截断 16 字节，并将
版本位强制为 `4`、变体位强制为 `10`，与 RFC 4122 要求及 Apple 的
`uuidgen` 行为一致。两次构建的用户段字节内容若完全相同，则会得到
相同 UUID，满足可复现构建验证器的预期。

## 架构差异

| 维度                | x86_64                                  | arm64                                    |
| ------------------- | --------------------------------------- | ---------------------------------------- |
| `cputype`           | `0x01000007`                            | `0x0100000C`                             |
| 分支重定位          | `X86_64_RELOC_BRANCH`                   | `ARM64_RELOC_BRANCH26`                   |
| GOT 加载对          | `GOT_LOAD`（单条）                      | `GOT_LOAD_PAGE21` + `GOT_LOAD_PAGEOFF12` |
| 页内偏移族          | 无                                      | `PAGE21` / `PAGEOFF12`                   |
| 默认 `__PAGEZERO`   | 4 GiB                                   | 4 GiB                                    |
| 小型 `exit(0)` 字节 | `B8 01 00 00 02 31 FF 0F 05`            | `00 00 80 52 30 00 80 52 01 10 00 D4`    |

## 调试速查

* `otool -hlLR <镜像>`：查看 header、load command、依赖库、重定位。
* `otool -tv <镜像>`：反汇编 `__TEXT,__text`，便于与输入字节做视觉比对。
* `dyldinfo -bind <镜像>`：虽已废弃，但仍可用于核查无残留 bind。
* `lldb -b -o "image dump symtab" <镜像>`：打印解析后的符号表，是定位
  `n_sect` 偏一错误最快的手段。
* macOS 端到端 smoke 测试会通过 `dlopen` 加载 dylib 变体，并断言加载
  器返回 Mach-O 形态的诊断而非「非 Mach-O」错误。

## 错误码

| 代码               | 触发条件                                                            |
| ------------------ | ------------------------------------------------------------------- |
| `polyld-err-E3220` | 重定位 `r_type` 在请求的 `MachOArch` 下未知                         |
| `polyld-err-E3230` | `BuildMachOImage` 返回空镜像（`BuildRequest` 内部不一致）           |
