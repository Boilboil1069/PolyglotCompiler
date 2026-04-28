# Polyglot Bitcode 发射

后端接口在 `EmitObject` / `EmitAssembly` 之外定义了 `EmitBitcode`
入口。在此版本之前，默认实现对每个后端都返回 unsupported 诊断，三个
生产目标的 `BackendCapabilities::emits_bitcode` 也全部为 false。本次
里程碑把该入口接到项目既有的 polyglot bitcode 序列化器（LTO /
ThinLTO 流水线已经在用的字节流），并把 x86_64、arm64、wasm32 三个
后端的能力位翻为 true。

变更纯增量：需要别的格式（比如 LLVM-to-LLVM 链接器要求的真 LLVM
bitcode）的后端可以重写虚函数；没有特殊需求的后端继承默认路径，立即
获得真实可往返的制品。

## 1. 格式

Polyglot bitcode 是 `polyglot::lto::LTOModule` 产出与消费的 UTF-8
文本流。它不是 LLVM bitcode，首字节为 `m`（与 LLVM 的 `BC` 魔数不同），
所以已经按魔数判别格式的检测点（`tools/polyld/src/linker.cpp` 把
`BC\xC0\xDE` 识别为 `kLLVMBitcode`）不会被干扰。

文法（空白分隔 token，`\n` 分隔记录）：

```
module <module_name>
<fn_count> <gv_count>
( 每个函数重复 fn_count 次：
    <fn_name>
    <block_count>
    ( 每个块重复 block_count 次：
        <block_name> <inst_count>
        ( 每条指令重复 inst_count 次：
            <inst_name|_> <type_kind:int> <op_count> <ops...>
        )
    )
)
( 每个 global 重复 gv_count 次：
    <gv_name>
)
<entry_point_count>
( 每个 entry_point 重复 entry_point_count 次：
    <entry_point_name>
)
```

空名编码为单字符 `_`，反序列化时还原为空串；以此规避空白分隔
tokenization 的歧义。

## 2. API

### 2.1 `polyglot::lto::LTOModule`

```cpp
// 内存版本 —— 产出 / 消费字节，不落盘。
std::string SerializeBitcode() const;
bool        DeserializeBitcode(std::string_view bytes);

// 磁盘版本 —— 内存版本之上的薄壳。
bool SaveBitcode(const std::string& filename) const;
bool LoadBitcode(const std::string& filename);

// 由内存 IRContext 构建 LTOModule。
static LTOModule FromIRContext(const polyglot::ir::IRContext& ctx,
                               std::string module_name);
```

`SaveBitcode` 现在是 `SerializeBitcode` 加一次二进制写；`LoadBitcode`
是把文件读入 `std::string` 后调 `DeserializeBitcode`。磁盘版本字节级
完全等于改造前的产物 —— 由既有 `tests/unit/middle/lto_test.cpp` 的
round-trip 用例保护。

`FromIRContext` 深拷贝 `IRContext` 中每个 `Function` 与 `GlobalValue`
（产出 `LTOModule` 不与源共享指针），并把每个函数名记入 entry-point
候选集。需要更小导出面的调用方可以在序列化前后处理 `entry_points`。

### 2.2 `polyglot::backends::ITargetBackend::EmitBitcode`

```cpp
virtual CompileResult EmitBitcode(const polyglot::ir::IRContext& module,
                                  const TargetOptions& options);
```

默认行为：

1. 用 `TargetTriple()` 作为 bitcode 模块名，从 `module` 构建 `LTOModule`；
2. 调 `LTOModule::SerializeBitcode` 序列化；
3. 把字节挪入 `result.artifacts.bitcode_bytes`；
4. `result.ok = true`、无诊断返回。

若 `SerializeBitcode` 失败导致 payload 为空，调用返回 `ok = false`，
带一条 `EmitBitcode` 组件的错误诊断。

需要其他 bitcode 格式的后端重写虚函数即可，根本不会走默认路径。

## 3. 能力矩阵

| 后端 triple                | `emits_bitcode`（之前） | `emits_bitcode`（现在） |
|----------------------------|:----------------------:|:----------------------:|
| `x86_64-unknown-elf`       | false                  | true                   |
| `aarch64-unknown-elf`      | false                  | true                   |
| `wasm32-unknown-unknown`   | false                  | true                   |

三个 adapter 都不重写虚函数，全部共享默认 polyglot bitcode 路径。
`target_backend_registry_test` 的矩阵断言与新的
`emit_bitcode_roundtrip_test` byte-equal 用例都依赖这一点。

## 4. 与 `polyld` 的交互

`tools/polyld/src/linker.cpp` 按首字节识别 4 种对象格式：ELF
（`\x7FELF`）、Mach-O（`\xFE\xED\xFA…`）、COFF、LLVM bitcode
（`BC\xC0\xDE`）。Polyglot bitcode 首字节为 ASCII `m`（来自 `module `
头），落在所有已识别魔数之外，不会被错认为 LLVM bitcode。

想把 polyglot bitcode 制品送入 `polyld` 的调用方，应把它当作项目
原生 LTO 格式，喂给 LTO 流水线（`CompileToBitcode` / `MergeBitcode`）
而不是对象文件加载器。两种格式可以同处一次链接调用，因为首字节空间
互斥。

## 5. 测试覆盖

| 测试 | 断言 |
|------|------|
| `unit/middle/lto_test.cpp`（既有） | 磁盘格式下 `SaveBitcode` / `LoadBitcode` 字节等价。 |
| `unit/backends/target_backend_registry_test.cpp`（修改） | 三个后端的 `emits_bitcode` 现为 true；原 "unsupported diagnostic" 用例改写为 "emits polyglot bitcode bytes"，断言 `result.ok`、首字节 `m`、`DeserializeBitcode` 回放后函数数为 2。 |
| `unit/backends/emit_bitcode_roundtrip_test.cpp`（新增 3 用例） | 空 IRContext 的头部合法；三个后端 triple 的 payload 字节等价（剥离每 triple 不同的模块名头行）；函数 / 块 / 指令拓扑保持，含 operand 字符串与 `entry_points` 成员关系。 |

## 6. 迁移说明

- 既有调用方若在 x86_64 / arm64 / wasm 上判 `result.ok == false` 后
  绕过某一阶段，现在会看到 `ok == true` 与已填充的 `bitcode_bytes`。
  依赖 "unsupported" 诊断跳过阶段的代码须改为显式查
  `Capabilities().emits_bitcode`（这本来就是文档约定的路径）。
- `LTOModule::SaveBitcode` 落盘的字节流字节级不变；该 helper 现在通过
  `SerializeBitcode` 中转，但发射结果完全一致。
- 计划发射真 LLVM bitcode（例如借 libLLVM）的后端应自行重写
  `EmitBitcode` 并设置 `Capabilities().emits_bitcode = true` ——
  此情况下默认路径根本不运行。
