# Polyglot IR core (CFG + SSA)

This document summarizes the unified IR layer introduced for control-flow graph and SSA construction.

## Building blocks
- **Type system**: `IRTypeKind` covers scalars (`i1/i8/i32/i64/f32/f64/void`), pointers/references, arrays, vectors, structs, and function signatures (ret + params). Convenience ctors: `Pointer`, `Reference`, `Array`, `Vector`, `Struct`, `Function`; helpers for structural equality (`SameShape`), widening/bitcast checks (`CanLosslesslyConvertTo`, `CanBitcastTo`).
- **Value/Constants**: base SSA value with a type and an optional name. `LiteralExpression` covers int/float literals; `ConstantString`/`ConstantArray`/`ConstantStruct` model richer constants; `ConstantGEP` encodes address constants (pointer-producing getelementptr-style); `UndefValue` is an uninitialized placeholder; `GlobalValue` models globals/consts (textual init allowed).
- **Instruction**: extends `Value` and carries operands (by variable name). Subclasses include `BinaryInstruction` (add/sub/mul/div/and/or/cmp), `AssignInstruction` (pre-SSA named assignment), `PhiInstruction` (SSA φ node), memory/call/cast/addressing (`AllocaInstruction`, `LoadInstruction`, `StoreInstruction`, `CallInstruction`, `CastInstruction` zext/sext/trunc/bitcast, `GetElementPtrInstruction`), terminators (`ReturnStatement`, `BranchStatement`, `CondBranchStatement`, `SwitchStatement`).
- **BasicBlock**: owns phi nodes, regular instructions and a terminator; tracks predecessors/successors.
- **Function**: groups blocks with an entry pointer.
- **IRContext**: holds functions and provides a default function/block for quick construction; `IRBuilder` wraps common builders.

## CFG and dominance
`BuildCFG(Function&)` populates predecessors/successors from terminators. `ComputeDominators` returns an immediate-dominator map and tree; `ComputeDominanceFrontier` derives dominance frontiers for SSA placement.

## SSA conversion
`ConvertToSSA(Function&)` performs standard φ-insertion via dominance frontiers and variable renaming with versioned names (e.g., `x_0`). Helpers `InsertPhiNodes` and `RenameToSSA` are exposed for targeted use.
Best-effort `Mem2Reg` in the optimizer can promote trivially stack-allocated scalars when only accessed locally.

## Notes
- Variables are tracked by their `Instruction::name`; `AssignInstruction` is treated as a defining instruction (non-void type). Memory SSA is not yet implemented; loads/stores are first-class instructions.
- Φ nodes live in `BasicBlock::phis` and are updated automatically during renaming.
- Unreachable blocks are ignored by dominance/SSA calculations for robustness.

## Minimal example
See `tests/samples/ir/ssa_example.cpp` for a runnable snippet that builds a loop, inserts φ nodes, and prints SSA names. Core steps:
1. Create a `Function` and a few `BasicBlock`s (`entry`, `loop`, `exit`).
2. Add pre-SSA defs (e.g., `AssignInstruction` / `BinaryInstruction`) and terminators (`Branch` / `CondBranch`).
3. Call `ConvertToSSA` to place φ nodes and rename variables to SSA form.

## Frontend hookup (sketch)
Frontends can lower their AST into the IR as follows:
- Create/obtain an `IRContext`, then a `Function` per source function and initial `BasicBlock`.
- Use the richer instruction set: `Alloca/Load/Store` for locals/globals, `Call` for calls, `Cast` for conversions, `Switch/Branch/CondBranch` for control flow, `Binary` for arithmetic/logic.
- Once lowering is done for a function, run `BuildCFG` + `ConvertToSSA` then optional optimizations (`passes::RunDefaultOptimizations`) to normalize variable names, insert φ-nodes, fold constants, remove dead code, and simplify CFG/CSE. This provides a clean SSA IR for downstream passes.
