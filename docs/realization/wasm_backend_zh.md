# WASM 后端翻译单元布局

WebAssembly 后端最初由单一的 1047 行 `wasm_target.cpp` 实现，覆盖 8 个互不相关
的关注点（二进制格式常量、LEB128 编码、IR 类型映射、7 个段发射器、庞大的
`LowerInstruction` switch、`LowerFunction`、公共入口 `EmitWasmBinary`、WAT 文本
打印器）。该布局让任何下游修改都要碰这个千行文件，所有改动的影响半径都被无谓
放大。

后端现已拆分为：1 个私有常量头、1 个轻量公共入口 TU、以及 6 个聚焦实现 TU。
所有公共方法签名、字节级二进制输出、诊断顺序保持不变 —— 由既有 8 个
`wasm_target_test` 用例 + 4 个新增 `wasm_split_smoke_test` 用例守护。

## 1. 文件布局

```
backends/wasm/
    include/
        wasm_target.h              公共类声明 (未变)
        internal/
            wasm_constants.h       后端私有 opcode + magic 常量
    src/
        wasm_target.cpp            EmitWasmBinary 公共入口
        wasm_target_backend.cpp    ITargetBackend 适配器 (未触动)
        encoding/
            leb128.cpp             EmitU32Leb128 / EmitI32Leb128 /
                                   EmitI64Leb128 / EmitString / EmitSection
        lowering/
            type_mapping.cpp       IRTypeToWasm
            function_lowerer.cpp   LowerFunction (locals + 块深度)
            instruction_lowerer.cpp LowerInstruction (11 个 IR 形态)
        sections/
            section_emitters.cpp   7 个 EmitXxxSection
        wat_printer.cpp            EmitAssembly + EmitInstructionWAT
```

## 2. 常量头 —— `internal::` 可见性

`backends/wasm/include/internal/wasm_constants.h` 是后端树下唯一位于 `internal/`
目录的头。仅由 `backends/wasm/src/**` 下的翻译单元消费；`internal/` 边界以上的
任何 `include/` 都不引用它。所有常量声明为 `inline constexpr`（C++17），多个
TU 可同时包含而不产生 ODR 冲突。

该头收纳：

| 类别 | 符号 |
|------|------|
| Magic / 版本 | `kWasmMagic`、`kWasmVersion` |
| 控制流       | `kOpUnreachable`、`kOpNop`、`kOpBlock`、`kOpLoop`、`kOpIf`、`kOpElse`、`kOpEnd`、`kOpBr`、`kOpBrIf`、`kOpReturn`、`kOpCall`、`kOpDrop`、`kOpSelect` |
| Locals/globals | `kOpLocalGet`、`kOpLocalSet`、`kOpLocalTee`、`kOpGlobalGet`、`kOpGlobalSet` |
| 内存         | `kOpI32Load…F64Store`（8 项） |
| 数值         | `kOpI32Const…F64Const` 及完整 `i32/i64/f64` 算术、比较、转换 opcode |
| 块类型       | `kBlockTypeVoid` |

## 3. 编码 TU —— `encoding/leb128.cpp`

收纳 5 个 LEB128 / 段框 helper。5 个均保持为 `WasmTarget` 的私有静态方法；只是
把定义迁出。LEB128 是无 IR 依赖的叶节点关注点，该 TU 只 include `<cstdint>` /
`<string>` / `<vector>` 加 `wasm_target.h` —— 全后端最小 TU。

## 4. 类型映射 TU —— `lowering/type_mapping.cpp`

`IRTypeToWasm` 是唯一一个 IR 感知的叶函数：把 `ir::IRTypeKind` 映射到
`WasmValType`。指针类一律折叠为 `kI32`（wasm32 指针宽度）。

## 5. 段发射 TU —— `sections/section_emitters.cpp`

收纳 7 个段发射器：

| 段 | 方法 | 说明 |
|----|------|------|
| Type     | `EmitTypeSection`     | `types_` 空时跳过。 |
| Import   | `EmitImportSection`   | `imports_` 空时跳过。 |
| Function | `EmitFunctionSection` | `func_type_indices_` 空时跳过。 |
| Memory   | `EmitMemorySection`   | 总是发射单页 (1 page) 内存。 |
| Global   | `EmitGlobalSection`   | 发射 shadow-stack `__stack_pointer`，初值 65536（首页顶端）。 |
| Export   | `EmitExportSection`   | `exports_` 空时跳过。 |
| Code     | `EmitCodeSection`     | `func_bodies_` 空时跳过。 |

仅本 TU 与 `wasm_target.cpp` 需要 `internal::` 常量；两者均通过显式 `using`
声明导入。

## 6. 指令降级 TU —— `lowering/instruction_lowerer.cpp`

`LowerInstruction` 是单方法最大者（约 325 行），现独占一个 TU。覆盖 11 种 IR
指令形态：

1. `BinaryInstruction` —— 26 个二元 / 比较 opcode，含 `kFRem` 的
   `x − trunc(x / y) · y` 降级。
2. `ReturnStatement`。
3. `CallInstruction` —— 通过 `func_name_to_index_` 解析；解析失败记录硬错误
   并发射 `unreachable`。
4. `LoadInstruction` —— 宽度 / 对齐由 `load->type` 推导。
5. `StoreInstruction` —— 宽度 / 对齐由 `store->type` 推导。
6. `CastInstruction` —— `kZExt/kIntToPtr → i64.extend_i32_u`、
   `kSExt → i64.extend_i32_s`、`kTrunc/kPtrToInt → i32.wrap_i64`、
   `kBitcast/kFpExt/kFpTrunc → nop`。
7. `AllocaInstruction` —— 针对 `__stack_pointer`（global 0）的 shadow-stack
   降级；新指针保留在操作数栈上。
8. `UnreachableStatement`。
9. `BranchStatement` —— `br`，深度由 `block_depth_map_` 解析（建表见
   `LowerFunction`）。
10. `CondBranchStatement` —— `br_if`，同样的深度解析。
11. Fallback —— 发射 `unreachable` 并在 `lowering_errors_` 中记录
    `unsupported IR instruction` 诊断。

## 7. 函数降级 TU —— `lowering/function_lowerer.cpp`

`LowerFunction` 流程：

1. 收集 WASM 局部类型（每条带结果的 IR 指令 / phi 各一）。
2. 用 RLE 把连续同类型局部压缩到 locals 头。
3. 由 IR basic-block 名 → 整数索引建立 `block_depth_map_`。
4. 为每个 IR basic block 发射 `block (block_type_void)` 开头，然后是降级后的
   指令，再为每个 block 发射匹配的 `end` 加上函数体 `end`。

## 8. WAT 打印 TU —— `wat_printer.cpp`

`EmitAssembly` 产出 `(module ... (func $name (export …) (param …) (result …) … ))`
文本表示。指令打印 helper `EmitInstructionWATImpl` 位于本 TU 的匿名命名空间；
类成员 `EmitInstructionWAT` 仅作 trampoline，使 helper 不外泄。

## 9. 公共入口 TU —— `wasm_target.cpp`

保留的 `wasm_target.cpp` 现在是一个 143 行文件，仅含 `EmitWasmBinary` 这个公共
二进制发射驱动。三阶段：

1. **类型收集** —— 遍历 `module_->Functions()`，把签名去重进 `types_`，填充
   `func_type_indices_` 与 per-function 导出条目，并构建 `func_name_to_index_`。
2. **函数体降级** —— 对每个 IR 函数调用 `LowerFunction`，把 bytecode 累积进
   `func_bodies_`。
3. **段汇编** —— 拼接 magic + version 头与 7 个段（按 canonical 顺序）；当任何
   函数使用 `alloca` 时条件性包含 global 段。

`lowering_errors_` 中的诊断在阶段 2、3 之间冲刷至 `stderr` —— 顺序与拆分前
完全一致。

## 10. CMake 接线

`backends/CMakeLists.txt` 在 `backend_wasm` 中列出全部 8 个 WASM TU：

```cmake
add_library(backend_wasm
    wasm/src/wasm_target.cpp
    wasm/src/wasm_target_backend.cpp
    wasm/src/encoding/leb128.cpp
    wasm/src/lowering/type_mapping.cpp
    wasm/src/lowering/function_lowerer.cpp
    wasm/src/lowering/instruction_lowerer.cpp
    wasm/src/sections/section_emitters.cpp
    wasm/src/wat_printer.cpp
)
```

`target_include_directories(backend_wasm PUBLIC ${CMAKE_SOURCE_DIR})` 让
`internal/` 头通过源相对 include 路径可达。

WASM adapter（`wasm_target_backend.cpp`）继承默认
`ITargetBackend::EmitBitcode`，走 polyglot bitcode 序列化路径，无需
WASM 专属重写。

## 11. 字节等价保证

拆分是纯结构性的。两组测试守护契约：

- `tests/unit/backends/wasm_target_test.cpp` —— 既有 8 个端到端用例，覆盖
  `EmitAssembly` 文本形态、`EmitWasmBinary` magic 字节、参数类型映射、void
  返回、控制流块注释、空模块、多导出排序。
- `tests/unit/backends/wasm_split_smoke_test.cpp` —— 4 个新增用例，断言
  空模块 8 字节头、单 `add(i32,i32) → i32` 的 canonical 段 id 序列
  `(1, 3, 5, 7, 10)`、`(module ... (func $add ...))` WAT 形态、以及通过
  类型表去重间接验证 LEB128（两个同签名函数必须产出 function 段字节
  `0x02 0x00 0x00`）。

## 12. 添加新降级时

按职责选择 TU：

| 变更 | TU |
|------|-----|
| 新 IR opcode → wasm bytecode | `lowering/instruction_lowerer.cpp` |
| 二进制中新增段 | `sections/section_emitters.cpp`（并接到 `wasm_target.cpp` 阶段 3） |
| 新 IR 类型 → wasm 类型 | `lowering/type_mapping.cpp` |
| 新 WAT 行 | `wat_printer.cpp` |
| 新 opcode 常量 | `internal/wasm_constants.h` |

公共方法签名必须与 `wasm_target.h` 同步；若需变更签名，先改头。

## 13. 故障排查

- **`WasmTarget::Foo` 未定义符号** —— 方法在头中声明但其 TU 未列入
  `add_library(backend_wasm …)`。把新 TU 加到 `backends/CMakeLists.txt`。
- **`kOpXxx` 多重定义** —— 某 TU 重复定义了 `internal/wasm_constants.h` 已
  导出的常量。该头独占常量；删除本地副本。
- **同 IR 输入下二进制与历史版本不同** —— 在字节流中 diff 可疑段并与历史构建
  对比，再按 TU 粒度二分回滚。拆分是按构造保证字节等价的；任何漂移都指向拆分
  之后引入的无关回归。
