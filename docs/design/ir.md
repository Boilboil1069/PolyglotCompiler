# Polyglot IR core (CFG + SSA)

This document summarizes the unified IR layer introduced for control-flow graph and SSA construction.

## Building blocks
- **Value**: base SSA value with a type and an optional name. `LiteralExpression` represents integer constants; `UndefValue` is an uninitialized placeholder.
- **Instruction**: extends `Value` and carries operands (by variable name). Subclasses include:
  - `BinaryInstruction` (add/sub/mul/div/and/or/cmp)
  - `AssignInstruction` (pre-SSA named assignment)
  - `PhiInstruction` (SSA φ node)
  - Terminators: `ReturnStatement`, `BranchStatement`, `CondBranchStatement`
- **BasicBlock**: owns phi nodes, regular instructions and a terminator; tracks predecessors/successors.
- **Function**: groups blocks with an entry pointer.
- **IRContext**: holds functions and provides a default function/block for quick construction; `IRBuilder` wraps common builders.

## CFG and dominance
`BuildCFG(Function&)` populates predecessors/successors from terminators. `ComputeDominators` returns an immediate-dominator map and tree; `ComputeDominanceFrontier` derives dominance frontiers for SSA placement.

## SSA conversion
`ConvertToSSA(Function&)` performs standard φ-insertion via dominance frontiers and variable renaming with versioned names (e.g., `x_0`). Helpers `InsertPhiNodes` and `RenameToSSA` are exposed for targeted use.

## Notes
- Variables are tracked by their `Instruction::name`; `AssignInstruction` is treated as a defining instruction (non-void type).
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
- Emit `AssignInstruction`/`BinaryInstruction` for expressions; lower control-flow constructs to `Branch`/`CondBranch` and arrange `BasicBlock` edges accordingly.
- Once lowering is done for a function, run `BuildCFG` + `ConvertToSSA` to normalize variable names and insert φ-nodes. This provides a clean SSA IR for optimization passes (e.g., constant folding, CSE, DCE).
