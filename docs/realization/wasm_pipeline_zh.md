# Wasm 链接管线（BIN-6）

本文说明 `polyld` 如何把 `.wasm` 模块作为头等输入接入分发链，以及
多模块合并器如何在它们之间满足 import。实现位于
[tools/polyld/include/linker_wasm.h](../../tools/polyld/include/linker_wasm.h)
与
[tools/polyld/src/linker_wasm.cpp](../../tools/polyld/src/linker_wasm.cpp)。

## 概览

[tools/polyld/src/linker.cpp](../../tools/polyld/src/linker.cpp) 中的
分发链根据 `TargetTriple` 解析目标，当 OS 字段为 `wasi` 或二进制
容器为 `kWasm` 时，控制权转交至 `linker_wasm.cpp` 中的
`Linker::GenerateWasmModule()`。该成员遍历每一份输入文件，解析所有
以 `\0asm\1\0\0\0` 开头的字节流，并按以下三种路径之一处理：

| 输入数量 | 路径 |
|---|---|
| 0 个 wasm 文件 | 合成最小可用模块，把合并后的 `.text` 数据放入 `polyglot.text` 自定义段。 |
| 1 个 wasm 文件 | 走解析→重写一遍，使 LEB128 宽度规范化。 |
| N 个 wasm 文件 | 调用 `LinkWasmModules` 合并。 |

当解析到的目标 OS 为 `wasi` 时，管线会重新解析输出镜像并执行
`ValidateWasiEntry`；若没有导出 `_start`，则报告 `polyld-err-E3320`。

## 段表布局

解析与发射覆盖完整的 WebAssembly 1.0 段表（`type` 0x01 至 `data`
0x0B），并支持 `datacount`（0x0C）与 `custom`（0x00）。发射器按规范
顺序写入并跳过空段，避免在文件中留下零长度段头。

## 多模块索引空间

每个 WebAssembly 模块拥有六个独立的索引空间（function、table、
memory、global、data、element）。每个空间内，import 项位于较低索引，
本地定义紧随其后。合并器为每个源模块维护一组按种类分桶的映射表，把
*源模块本地索引* 翻译为合并模块中的索引。布局由两条结构性事实驱动：

1. **保留下来的 import 排在前面**。合并器遍历所有输入，依次尝试用全局
   导出表满足每个 import；未能解析的 import 被追加到合并模块统一的
   import 向量中。索引按"输入 → 声明"顺序分配，保证输出确定性。
2. **本地定义紧随其后**。每个源模块的本地 function/table/memory/global
   按输入顺序拼接，合并索引从该种类保留 import 数量起始。

data 与 element 段没有 import，因此只需统计本地数量。

## Import 解析

`LinkWasmModules` 在所有输入上构建单一的 `name → (源模块, 类别,
本地索引)` 映射，然后按声明顺序遍历每个 import：

* 若有同名同类的 export，import 视为已解析。对函数 import，进一步比对
  声明签名与导出方的 `FuncType`（先做类型 intern）。签名不一致触发
  `polyld-err-E3330`。
* 若 kind 一致但导出方实际并非函数（跨类影子），报告 `polyld-err-E3310`。
* 否则该 import 原样保留进合并模块。

每个解析成功的 import，其在 code body、export 与 element 段中的所有
引用都会通过合并索引表重写，调用者永远观察不到原始的 import 槽位。

## Code body 重写

函数体保留原始字节表示（单模块路径不解码，因此 SIMD 模块可无损
往返）。一旦需要重新索引，合并器就启用一个完整的 WebAssembly 1.0 +
bulk-memory + reference-types 指令游走器：

| 操作码 | 重写的立即数 |
|---|---|
| `0x10` `call`                        | funcidx |
| `0x11` `call_indirect`               | typeidx, tableidx |
| `0x23` / `0x24` `global.get/set`     | globalidx |
| `0x25` / `0x26` `table.get/set`      | tableidx |
| `0xD2` `ref.func`                    | funcidx |
| `0xFC 8` `memory.init`               | dataidx, memidx |
| `0xFC 9` `data.drop`                 | dataidx |
| `0xFC 10` `memory.copy`              | memidx, memidx |
| `0xFC 11` `memory.fill`              | memidx |
| `0xFC 12` `table.init`               | elemidx, tableidx |
| `0xFC 13` `elem.drop`                | elemidx |
| `0xFC 14` `table.copy`               | tableidx, tableidx |
| `0xFC 15..17` `table.grow/size/fill` | tableidx |
| `block`/`loop`/`if` 中的 block-type  | typeidx（若存在） |

游走器对 local 索引、label 索引、memarg、常量字面量等保持原样。多模块
合并阶段如遇 `0xFD`（SIMD）或 `0xFE`（atomic）前缀，立即以
`polyld-err-E3340` 失败。

## 错误码

| 错误码              | 含义 |
|---------------------|------|
| `polyld-err-E3300` | 解析失败（preamble 错误、段截断、未知段号）。 |
| `polyld-err-E3310` | import 未解析或跨类引用。 |
| `polyld-err-E3320` | `wasi` 目标缺少 `_start`。 |
| `polyld-err-E3330` | 函数 import 签名不匹配。 |
| `polyld-err-E3340` | code body 重写失败（索引越界、不支持的操作码）。 |

## 调试速查

* `wasm-objdump -h <file>` —— 查看段表。
* `wasm-validate <file>` —— 跑规范校验器。
* `wasmtime run --invoke _start <file>` —— 调用 WASI 入口。
