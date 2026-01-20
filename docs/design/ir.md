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
