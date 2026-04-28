# 公共 ABI 层

公共 ABI 层（`polyglot::backends::common::abi`）以模板方式提供调用约定机制和重定位
分类法的统一实现，被所有原生后端（当前 x86_64 与 arm64）共享。它取代了原先两个
极其轻量的头文件 `backends/common/include/abi.h` 和
`backends/common/include/relocation.h`——这两个头文件作为转发壳保留，旧的 include
路径仍可编译。

## 1. 模块布局

```
backends/common/include/
    abi.h                       (转发壳 → abi/abi.h)
    relocation.h                (转发壳 → abi/relocation.h)
    abi/
        abi.h                   (聚合头)
        calling_convention.h    (CallingConvention<Traits>、StackFrame<Traits>、
                                 ComputeStackFrame 模板)
        relocation.h            (RelocationKind、RelocationEntry、AbiDescriptor、
                                 mapper、ToString/Parse)
backends/common/src/
    abi.cpp                     (ELF / Mach-O 编码点 mapper、
                                 ToString / ParseRelocationKind)
```

## 2. CallingConvention&lt;Traits&gt;

`CallingConvention` 是一个对各 `TargetTraits` 暴露的静态寄存器表的薄封装。要将新
目标接入公共层，其 `Traits` 必须公开：

```cpp
struct MyTargetTraits {
    // ... 现有 MachineIR 契约 (Register、RegisterClass 等) ...

    inline static const std::vector<Register> kIntegerArgRegs   = { /* ABI 顺序 */ };
    inline static const std::vector<Register> kFloatArgRegs     = { /* ABI 顺序 */ };
    inline static const std::vector<Register> kCalleeSavedRegs  = { /* ABI 顺序 */ };
    inline static const std::vector<Register> kVolatileRegs     = { /* caller-saved */ };

    static constexpr int kStackAlignment = 16;   // 字节
    static constexpr int kPointerSize    =  8;
    static constexpr int kRedZoneSize    =  0;   // SysV x86_64 = 128
};
```

八个只读访问器暴露这些表与常量：`IntegerArgRegs`、`FloatArgRegs`、
`CalleeSavedRegs`、`VolatileRegs`、`StackAlignment`、`PointerSize`、`RedZoneSize`，以及
便利方法 `AvailableRegisters()`，按分配器偏好顺序拼接 volatile + callee-saved 集合。

## 3. StackFrame&lt;Traits&gt; 与 ComputeStackFrame

```cpp
template <typename Traits>
struct StackFrame {
    int                                    total_size{0};
    int                                    spill_area_size{0};
    int                                    local_area_size{0};
    int                                    arg_area_size{0};
    std::vector<typename Traits::Register> saved_regs;
};
```

`ComputeStackFrame(cc, fn, alloc, call_opcode)` 以目标无关方式物化栈帧布局：

1. **溢出区**：`alloc.stack_slots * cc.PointerSize()` 字节。
2. **被调用者保存集**：按 ABI 顺序遍历 `cc.CalleeSavedRegs()`，保留 `alloc` 实际
   使用的寄存器，保持稳定的 prologue/epilogue 顺序。
3. **出参区**：扫描 `fn` 中所有 opcode 等于 `call_opcode` 的 `MachineInstr`；若
   任何调用的操作数多于 `cc.IntegerArgRegs().size()`，则为溢出部分预留空间。
4. **总大小**：将（溢出 + 局部 + 出参 + 保存寄存器槽位）之和向上对齐到
   `cc.StackAlignment()`。

该算法与 `backends/x86_64/src/calling_convention.cpp` 中原有的栈帧计算逻辑逐字
节等价，因此 `polyc` 的输出保持不变。

## 4. RelocationKind 与按格式 mapper

`RelocationKind` 是一个不透明的、与格式无关的枚举，包含 14 个值，覆盖 x86_64 和
AArch64 的并集（绝对、PC 相对、GOT/PLT、ADRP/ADD 页+偏移、B/Bcond 跳转、
MOVZ/MOVK 组）。每种对象文件格式的标准编码由四个纯函数提供：

| Mapper | 失败返回 | 出处 |
|--------|---------|------|
| `MapToElfX86_64`   | 对仅 AArch64 的 kind 返回 `kUnsupportedRelocation` | psABI x86_64 §4.4 |
| `MapToElfAArch64`  | 对仅 x86_64 的 kind 返回 `kUnsupportedRelocation`  | ELF for the Arm 64-bit Architecture |
| `MapToMachOX86_64` | 对仅 AArch64 的 kind 返回 `kUnsupportedRelocation` | `<mach-o/x86_64/reloc.h>` |
| `MapToMachOArm64`  | 对仅 x86_64 的 kind 返回 `kUnsupportedRelocation`  | `<mach-o/arm64/reloc.h>` |

`kUnsupportedRelocation == 0xFFFFFFFFu` 是单一哨兵值，后端通过其诊断管道呈现该
错误。`ToString(RelocationKind)` 与 `ParseRelocationKind(const std::string&,
RelocationKind*)` 提供供工具与测试使用的稳定文本序列化。

`AbiDescriptor` 是值类型指纹（`name`、`pointer_size`、`stack_alignment`、
`red_zone_size`），供链接器与诊断在不打开模板化 `CallingConvention` 的情况下
比较两个 ABI。

## 5. 旧 include 的转发壳

按项目策略保留两个既有头文件：

- `backends/common/include/abi.h` 现仅含
  `using ABI = polyglot::backends::common::abi::AbiDescriptor;` 与对应的 include。
- `backends/common/include/relocation.h` 暴露
  `using RelocType = polyglot::backends::common::abi::RelocationKind;`。

需要注意：两个壳均未为 `Relocation` 这一名字定义别名。
`object_file.h` 中由 `tools/polyc/src/compilation_pipeline.cpp` 使用的
`polyglot::backends::Relocation` 含义保持不变。

## 6. AbiContract 与验证器规则

`MachineIRVerifier` 接受可选的 `AbiContract<Traits, OpcodeT>`，编码目标的调用
opcode、操作数上限以及 volatile 寄存器集合：

```cpp
template <typename Traits, typename OpcodeT>
struct AbiContract {
    OpcodeT                                       call_opcode;
    std::size_t                                   max_call_operands;
    std::vector<typename Traits::Register>        volatile_regs;
};
```

调用 `Verify(fn, &contract)` 时，除了三条结构规则外，会额外执行两条 ABI 规则：

| 诊断码 | 触发条件 |
|--------|---------|
| `kAbiCallArityExceeded` | 某个 `call_opcode` 指令的操作数数量超过 `max_call_operands`。 |
| `kAbiVolatileRegLeak`   | 调用之后紧接的指令读取了某个 `volatile_regs` 寄存器，且其间无定义。 |

单参数重载 `Verify(fn)` 行为与之前完全一致（以 `nullptr` 委托），现有调用方
不受影响。

## 7. 各目标的接入方式

`backends/x86_64/src/calling_convention.cpp` 与
`backends/arm64/src/calling_convention.cpp` 现在实例化
`abi::CallingConvention<…TargetTraits>` 并调用 `abi::ComputeStackFrame`，仅保留真正
目标特定的部分——prologue/epilogue 发射、call setup 序列以及 `RenderOperand`
辅助函数。SysV-x86_64 与 AAPCS64 寄存器表作为 `Traits` 常量存在，因此添加新的
ABI 变体（如 Windows x64、ILP32 AArch64）只需提供新的 `Traits` 类型。

## 8. 故障排查

- **链接器输出报告未解析的 `RelocationKind`**——几乎一定是某个 mapper 返回了
  `kUnsupportedRelocation`。对照目标 psABI 表核对该 kind；若该 kind 在该格式
  下确实不受支持，调用方应在到达 mapper 之前将其下沉为受支持的指令序列。
- **同样输入下栈帧大小与历史版本不同**——核对 `kPointerSize` 与
  `kStackAlignment` 与历史值一致，并且 `kCalleeSavedRegs` 仍按相同 ABI 顺序
  列出。算法本身保持顺序稳定。
- **下沉后验证器报告 `kAbiVolatileRegLeak`**——在调用点插入显式 clobber/重载，
  或延长相关虚拟寄存器的活跃范围，使调用之后的读取看到已定义值。
