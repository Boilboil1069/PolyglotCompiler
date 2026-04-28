# 通用 MachineIR 与 Verifier

## 1. 这一层为什么存在

历史上 x86-64 与 arm64 后端各自携带一份 MachineIR 数据模型与三个核心代码
生成算法（活跃区间分析、线性扫描 / 图着色寄存器分配、列表调度）。除目标
特定的 `Opcode` 枚举与 `Operand` 默认寄存器值外，两份代码字节级一致：

| 文件                                  | x86-64 | arm64 |
|---------------------------------------|-------:|------:|
| `include/machine_ir.h`                | 178 行 | 160 行 |
| `src/regalloc/linear_scan.cpp`        | 121 行 | 121 行 |
| `src/regalloc/graph_coloring.cpp`     |  84 行 |  84 行 |
| `src/asm_printer/scheduler.cpp`       |  84 行 |  84 行 |

任何 regalloc / scheduler 修复都要改两遍，且没有任何机制保证两份不漂移。
解法是把共享代码上提到 `backends/common/include/machine_ir/`，以
`TargetTraits` 策略类参数化的 C++ 模板形式发布；在每个目标头里只保留一层
薄别名 / 声明。各目标 `src/regalloc/` 与 `src/asm_printer/` 下的 `.cpp`
文件全部保留，每个文件现在只承担**唯一一处**对应目标命名空间自由函数的
定义 —— 该定义只是把调用转发到匹配的通用模板，因此算法本体在源码树中只
存在一份，且不需要从构建中移除任何源文件。

附带收益是新增的与目标无关的 **MachineIR Verifier**，无需各后端各写一份
即可捕获结构性 / def-use bug。

## 2. 这一层的文件

```
backends/common/include/machine_ir/
    machine_ir.h     # 模板化数据模型 + 4 个算法 + Print()
    verifier.h       # MachineIRVerifier<TargetTraits, OpcodeT>

backends/x86_64/include/machine_ir.h            # 别名 / 声明层
backends/x86_64/src/regalloc/linear_scan.cpp    # x86-64 自由函数定义
backends/x86_64/src/regalloc/graph_coloring.cpp
backends/x86_64/src/asm_printer/scheduler.cpp

backends/arm64/include/machine_ir.h             # 别名 / 声明层
backends/arm64/src/regalloc/linear_scan.cpp     # arm64 自由函数定义
backends/arm64/src/regalloc/graph_coloring.cpp
backends/arm64/src/asm_printer/scheduler.cpp
```

## 3. `TargetTraits` 协议

希望实例化通用 MachineIR 模板的后端暴露一个简单的策略结构：

```cpp
struct X86TargetTraits {
    using Register                              = ::polyglot::backends::x86_64::Register;
    static constexpr Register kDefaultRegister  = Register::kRax;
};
```

整个表面就这么大：一个关联的 `Register` 值类型，加上一个 `Operand` 在尚未
被分配物理寄存器时使用的默认值（该字段始终存在，使 `Operand` 保持平凡可
默认构造）。目标特定的 `Opcode` 枚举**不**在 traits 内 —— 它作为单独的
模板参数 `OpcodeT` 传递，使各后端的 opcode 枚举可独立演进，且模板无需就
一套通用 opcode 编码达成共识。

## 4. 发布到目标命名空间的别名

每个目标的 `machine_ir.h` 把模板再导出，使 `isel.cpp` / `emit.cpp` /
`optimizations.cpp` / `calling_convention.cpp` /
`<target>_target_backend.cpp` 中的旧调用点零变更继续编译：

```cpp
using Operand            = common::machine_ir::Operand<X86TargetTraits>;
using MachineInstr       = common::machine_ir::MachineInstr<X86TargetTraits, Opcode>;
using MachineBasicBlock  = common::machine_ir::MachineBasicBlock<X86TargetTraits, Opcode>;
using MachineFunction    = common::machine_ir::MachineFunction<X86TargetTraits, Opcode>;
using LiveInterval       = common::machine_ir::LiveInterval<X86TargetTraits>;
using AllocationResult   = common::machine_ir::AllocationResult<X86TargetTraits>;
using common::machine_ir::RegAllocStrategy;
```

四个自由函数入口点在头文件中**声明**，在 per-target `.cpp` 文件中**定义**。
每个定义只有一行，转发到通用模板：

```cpp
// backends/x86_64/src/regalloc/linear_scan.cpp
AllocationResult LinearScanAllocate(const MachineFunction& fn,
                                    const std::vector<Register>& available) {
    return common::machine_ir::LinearScanAllocate<X86TargetTraits, Opcode>(fn, available);
}
```

这种形态既保留了原公共 API（`auto res = x86_64::LinearScanAllocate(fn,
regs);` 调用点零变更），也保留了原逐文件源码布局，同时确保算法本体只在
`backends/common/include/machine_ir/machine_ir.h` 中写一份。

`CostModel` 与 `SelectInstructions` 有意保持 per-target —— 它们的代价表
与 isel 规则本质上就是目标特定的。

## 5. 算法

三个算法都是原源码的字节等价移植；回归测试套件（`test_backends` /
`test_core` / `test_middle` / `test_runtime` / `test_linker`）确认行为零
变更。

### 5.1 `ComputeLiveIntervals`

单趟线性扫描，为每个虚拟寄存器赋一个 `[start, end]` 区间，位置计数器每
条指令推进 `2`（间隔留给后续更细粒度调度使用）。def 扩展 `start`；use
扩展 `end`。输出按 `start` 排序以喂给线性扫描分配器。

### 5.2 `LinearScanAllocate`

经典 Poletto/Sarkar 线性扫描。空闲寄存器池为空时：若活跃集中存活时间最
长的区间结束晚于新区间，则将其溢出并复用其物理寄存器；否则将新区间溢出
到新栈槽。

### 5.3 `GraphColoringAllocate`

由 `LiveInterval` 重叠关系构建干扰图，按度数降序（同度按 `start` 排序）
排列顶点，贪心地为每个顶点分配第一个可用寄存器；耗尽调色板的顶点溢出到
新栈槽。

### 5.4 `ScheduleFunction`

逐块列表调度：先把 terminator 暂存出去，对块体构建 def-use 依赖 DAG，按
延迟降序（同延迟按 cost 降序）发射就绪指令。暂存的 terminator 最后追加
到末尾，使 verifier 规则 (a) 始终成立。

## 6. Verifier

`MachineIRVerifier<TargetTraits, OpcodeT>::Verify(fn)` 强制三条与 ABI 无
关的不变量，每个后端都应在指令选择后（以及寄存器分配后再次）维持：

| 规则 | 诊断信息 |
|------|----------|
| (a) 每个基本块必须以 `terminator==true` 的指令结束（空 BB 也被报）。 | `"basic block does not end with a terminator instruction"` / `"basic block is empty (no terminator)"` |
| (b) 每个 `use` 引用的虚拟寄存器必须在函数中某处被定义。 | `"use of virtual register that has no definition anywhere in the function"` |
| (c) 同一基本块内不重复定义同一 vreg（块内单赋值）。 | `"duplicate definition of virtual register inside the same basic block"` |

诊断携带 `function_name`、`block_name`、`block_index`、`instruction_index`，
以及对出错函数的 `MachineFunction::Print()` 快照（首个错误处惰性构建一
次，后续诊断共享，使 payload 有界）。

ABI / 寄存器类兼容性 / 栈槽尺寸校验依赖显式 ABI 模型，有意不在结构性
verifier 中实现。

### 6.1 快照格式

```
function f {
  entry:
    [0] op=0 def=1 uses=[] term=no
    [1] op=1 def=2 uses=[1] term=no
    [2] op=22 def=-1 uses=[2] term=yes
}
```

`op=N` 是目标特定 opcode 枚举的底层整型值；verifier 有意不依赖目标
opcode 名称表，因为该表是后端实现细节。

## 7. 接入新后端

1. 添加 `Register` 枚举与 `<Target>TargetTraits` 结构，暴露 `Register`
   与 `kDefaultRegister`。
2. 在 `backends/<target>/include/machine_ir.h` 定义目标 `Opcode` 枚举。
3. 添加 7 条 `using` 别名（`Operand` / `MachineInstr` /
   `MachineBasicBlock` / `MachineFunction` / `LiveInterval` /
   `AllocationResult` / `RegAllocStrategy`）。
4. 在头文件中声明 4 个自由函数（`ComputeLiveIntervals` /
   `LinearScanAllocate` / `GraphColoringAllocate` / `ScheduleFunction`），
   并在 `backends/<target>/src/regalloc/*.cpp` 与
   `backends/<target>/src/asm_printer/scheduler.cpp` 中提供转发定义。
5. 在目标特定源文件中实现 `SelectInstructions` 与 `CostModel`。
6. 在 `tests/unit/backends/` 下镜像 `machine_ir_template_test.cpp` 添加
   一个单元测试实例化，确保模板能用新 traits 干净实例化。

## 8. 故障排查

| 现象 | 原因 | 解法 |
|------|------|------|
| `error: 在 '<X>TargetTraits' 中找不到 'kDefaultRegister'` | Traits 结构缺少 constexpr 默认值 | 加上 `static constexpr Register kDefaultRegister = Register::k…;` |
| 后端 `.cpp` 中消费 `MachineFunction` 报 `'X' 不是类模板` | 后端忘了写 `using MachineFunction = common::machine_ir::MachineFunction<…>;` 别名 | 补齐 §4 别名块 |
| 链接器报 `<target>::LinearScanAllocate` 未定义引用 | 目标的 `regalloc/linear_scan.cpp` 未加入 `backends/CMakeLists.txt` | 把源文件加到对应的 `add_library(...)` 项 |
| Verifier 在新 lower 完的函数上报 `"use … no definition anywhere"` | 指令选择漏发了生产者指令，或在生产者上写了 `def = -1` | 看诊断里的快照；修正出错的 isel 规则 |
| 调度后 Verifier 报 `"basic block does not end with a terminator"` | 自定义 pass 丢掉了 terminator —— `ScheduleFunction` 自身不会 | 用快照定位丢失的 `kRet/kJmp/kJcc`，重跑出错 pass |
