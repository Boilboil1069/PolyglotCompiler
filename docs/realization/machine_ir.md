# Common MachineIR & Verifier

## 1. Why this layer exists

Historically the x86-64 and arm64 backends each carried their own copy of the
MachineIR data model and the three core code-generation algorithms
(live-interval analysis, linear-scan / graph-coloring register allocation,
list scheduling). The two copies were byte-identical save for the per-target
opcode enum and the default register value used in `Operand` initialisation:

| File                                    | x86-64 | arm64 |
|-----------------------------------------|-------:|------:|
| `include/machine_ir.h`                  | 178 ln | 160 ln |
| `src/regalloc/linear_scan.cpp`          | 121 ln | 121 ln |
| `src/regalloc/graph_coloring.cpp`       |  84 ln |  84 ln |
| `src/asm_printer/scheduler.cpp`         |  84 ln |  84 ln |

Any regalloc or scheduler fix had to be made twice and there was no mechanism
that prevented the two copies from drifting. The fix is to lift the shared
code into `backends/common/include/machine_ir/` as C++ templates parameterised
on a `TargetTraits` policy and leave behind a thin alias / declaration layer
in each target header. Each per-target `.cpp` file under `src/regalloc/` and
`src/asm_printer/` is preserved and now contains the **single** definition of
its target-namespace free function — that definition simply delegates to the
matching common template, so the algorithm body exists in exactly one place
in the source tree without removing any source file from the build.

A side benefit is the new target-independent **MachineIR Verifier**, which
catches structural and def-use bugs in any backend without each backend
having to author its own.

## 2. Files in this layer

```
backends/common/include/machine_ir/
    machine_ir.h     # templated data model + 4 algorithms + Print()
    verifier.h       # MachineIRVerifier<TargetTraits, OpcodeT>

backends/x86_64/include/machine_ir.h            # alias / declaration layer
backends/x86_64/src/regalloc/linear_scan.cpp    # x86-64 free-function definitions
backends/x86_64/src/regalloc/graph_coloring.cpp
backends/x86_64/src/asm_printer/scheduler.cpp

backends/arm64/include/machine_ir.h             # alias / declaration layer
backends/arm64/src/regalloc/linear_scan.cpp     # arm64 free-function definitions
backends/arm64/src/regalloc/graph_coloring.cpp
backends/arm64/src/asm_printer/scheduler.cpp
```

## 3. The `TargetTraits` protocol

A backend wishing to instantiate the common MachineIR templates exposes a
trivial policy struct:

```cpp
struct X86TargetTraits {
    using Register                              = ::polyglot::backends::x86_64::Register;
    static constexpr Register kDefaultRegister  = Register::kRax;
};
```

That is the entire surface area: an associated `Register` value type plus a
default value used when an `Operand` is constructed without a phys-reg
assignment (the field is always present so `Operand` stays trivially default
constructible). The per-target `Opcode` enum is **not** part of the traits;
it travels as a separate template parameter (`OpcodeT`) so each backend's
opcode enum can evolve independently and so the templates do not have to
agree on a common opcode encoding.

## 4. Aliases shipped to the per-target namespace

Each target's `machine_ir.h` re-exports the templates so legacy call sites in
`isel.cpp`, `emit.cpp`, `optimizations.cpp`, `calling_convention.cpp`, and
`<target>_target_backend.cpp` keep compiling unchanged:

```cpp
using Operand            = common::machine_ir::Operand<X86TargetTraits>;
using MachineInstr       = common::machine_ir::MachineInstr<X86TargetTraits, Opcode>;
using MachineBasicBlock  = common::machine_ir::MachineBasicBlock<X86TargetTraits, Opcode>;
using MachineFunction    = common::machine_ir::MachineFunction<X86TargetTraits, Opcode>;
using LiveInterval       = common::machine_ir::LiveInterval<X86TargetTraits>;
using AllocationResult   = common::machine_ir::AllocationResult<X86TargetTraits>;
using common::machine_ir::RegAllocStrategy;
```

The four free-function entry points are **declared** in the header and
**defined** in the per-target `.cpp` files. Each definition is a one-liner
delegating to the common template:

```cpp
// backends/x86_64/src/regalloc/linear_scan.cpp
AllocationResult LinearScanAllocate(const MachineFunction& fn,
                                    const std::vector<Register>& available) {
    return common::machine_ir::LinearScanAllocate<X86TargetTraits, Opcode>(fn, available);
}
```

This shape preserves both the original public API (`auto res = x86_64::
LinearScanAllocate(fn, regs);` keeps working unchanged) and the original
file-by-file source layout, while ensuring the algorithm body itself is
written exactly once in `backends/common/include/machine_ir/machine_ir.h`.

`CostModel` and `SelectInstructions` deliberately stay per-target — their
cost tables and isel rules are intrinsically target-specific.

## 5. Algorithms

All three algorithms are byte-equivalent ports of the original sources; the
regression test suites (`test_backends`, `test_core`, `test_middle`,
`test_runtime`, `test_linker`) confirm no behavioural change.

### 5.1 `ComputeLiveIntervals`

A single linear sweep that assigns each virtual register a `[start, end]`
interval over a synthetic position counter advancing by `2` per instruction
(the gap leaves room for finer-grained scheduling later). Definitions extend
`start`; uses extend `end`. Output is sorted by `start` to feed the
linear-scan allocator.

### 5.2 `LinearScanAllocate`

Classic Poletto/Sarkar linear scan over the sorted intervals. When the free
register pool is empty the allocator either spills the longest-lived active
interval (if it outlives the new one) and reuses its physical register, or
spills the new interval to a fresh stack slot.

### 5.3 `GraphColoringAllocate`

Builds an interference graph from `LiveInterval` overlap, orders vertices by
descending degree (ties broken by `start`), and greedily colours each vertex
with the first available register; vertices that exhaust the palette are
spilled to fresh stack slots.

### 5.4 `ScheduleFunction`

Per-block list scheduler: the terminator is held aside, a def-use dependency
DAG is built across the body, and ready instructions are emitted in
descending-`latency` (then descending-`cost`) order. The held-aside
terminators are appended at the end so verifier rule (a) always holds.

## 6. The Verifier

`MachineIRVerifier<TargetTraits, OpcodeT>::Verify(fn)` enforces three
ABI-independent invariants every backend should preserve immediately after
instruction selection (and again after register allocation):

| Rule | Diagnostic message |
|------|--------------------|
| (a) Every basic block must end with a `terminator==true` instruction (an empty BB is also flagged). | `"basic block does not end with a terminator instruction"` / `"basic block is empty (no terminator)"` |
| (b) Every `use` references a virtual register that is defined somewhere in the function. | `"use of virtual register that has no definition anywhere in the function"` |
| (c) The same vreg is not redefined inside the same basic block (single-assignment within a block). | `"duplicate definition of virtual register inside the same basic block"` |

Diagnostics carry `function_name`, `block_name`, `block_index`,
`instruction_index`, and a lazy `MachineFunction::Print()` snapshot of the
offending function (built once on the first error and shared with subsequent
diagnostics so the payload stays bounded).

ABI / register-class compatibility / stack-slot-size validation requires an
explicit ABI model and is intentionally out of scope for the structural
verifier.

### 6.1 Snapshot format

```
function f {
  entry:
    [0] op=0 def=1 uses=[] term=no
    [1] op=1 def=2 uses=[1] term=no
    [2] op=22 def=-1 uses=[2] term=yes
}
```

`op=N` is the underlying integer value of the per-target opcode enum; the
verifier deliberately does not depend on a per-target opcode-name table since
that table is a backend implementation detail.

## 7. Adding a new backend

1. Add a `Register` enum and a `<Target>TargetTraits` struct exposing
   `Register` and a `kDefaultRegister`.
2. Define the per-target `Opcode` enum in
   `backends/<target>/include/machine_ir.h`.
3. Add the seven `using` aliases (`Operand`, `MachineInstr`,
   `MachineBasicBlock`, `MachineFunction`, `LiveInterval`,
   `AllocationResult`, `RegAllocStrategy`).
4. Declare the four free functions (`ComputeLiveIntervals`,
   `LinearScanAllocate`, `GraphColoringAllocate`, `ScheduleFunction`) in the
   header and provide their delegating definitions in
   `backends/<target>/src/regalloc/*.cpp` and
   `backends/<target>/src/asm_printer/scheduler.cpp`.
5. Implement `SelectInstructions` and `CostModel` in target-specific source
   files.
6. Add a unit-test instantiation under `tests/unit/backends/` mirroring
   `machine_ir_template_test.cpp` to guarantee the templates instantiate
   cleanly with the new traits.

## 8. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `error: no member named 'kDefaultRegister' in '<X>TargetTraits'` | Traits struct missing the constexpr default | Add `static constexpr Register kDefaultRegister = Register::k…;` |
| `error: 'X' is not a class template` when consuming `MachineFunction` from a backend `.cpp` | The backend forgot the `using MachineFunction = common::machine_ir::MachineFunction<…>;` alias | Add the alias block from §4 |
| Linker reports undefined reference to `<target>::LinearScanAllocate` | The target's `regalloc/linear_scan.cpp` is missing from `backends/CMakeLists.txt` | Add the source file to the corresponding `add_library(...)` entry |
| Verifier reports `"use … no definition anywhere"` for a freshly lowered function | Instruction selection forgot to emit the producing instruction, or wrote `def = -1` on a producer | Inspect the snapshot in the diagnostic; correct the producing isel rule |
| Verifier reports `"basic block does not end with a terminator"` after scheduling | A custom pass dropped the terminator — `ScheduleFunction` itself never does | Re-run the failing pass with the snapshot to locate the missing `kRet/kJmp/kJcc` |
