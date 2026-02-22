# Optimization Pipeline & Switch Matrix

This document describes the PolyglotCompiler optimization infrastructure,
covering the default pass pipeline, the `polyopt` standalone tool, LTO
(Link-Time Optimization), PGO (Profile-Guided Optimization), and the
command-line switches that control each feature.

---

## 1 Overview

The compiler's optimization subsystem lives entirely in `middle/`:

| Layer | Header / Directory | Purpose |
|-------|-------------------|---------|
| Per-function passes | `middle/include/ir/passes/opt.h` | Constant fold, DCE, copy prop, CSE, CFG canonicalisation, Mem2Reg |
| Transform passes | `middle/include/passes/transform/` | Inlining, GVN, loop opts, advanced opts (TCO, LICM, auto-vec, etc.) |
| Analysis passes | `middle/include/passes/analysis/` | Alias analysis, dominance |
| Devirtualisation | `middle/include/passes/devirtualization.h` | Cross-language virtual call resolution |
| LTO | `middle/include/lto/link_time_optimizer.h` | Cross-module inlining, IPCP, global DCE, Thin LTO |

Two entry points invoke this infrastructure:

1. **`polyc` driver** (`tools/polyc/src/driver.cpp`) — runs per-function
   default passes after SSA construction, controlled by `-O0`..`-O3`.
2. **`polyopt` tool** (`tools/polyopt/src/optimizer.cpp`) — standalone IR
   optimizer that reads textual IR, runs context-level passes, and writes
   the result.

---

## 2 Default Per-Function Pipeline (`opt.h`)

`polyglot::ir::passes::RunDefaultOptimizations(Function&)` executes:

1. `ConstantFold` — evaluate constant expressions at compile time.
2. `DeadCodeEliminate` — remove unreachable / unused instructions.
3. `CopyProp` — forward copies and eliminate redundant moves.
4. `CanonicalizeCFG` — merge / simplify basic blocks.
5. `EliminateRedundantPhis` — collapse trivial φ-nodes.
6. `CSE` — common sub-expression elimination.
7. `Mem2Reg` — promote memory-based locals to SSA registers.

This pipeline is invoked automatically by `polyc` when `opt_level >= 1`.

---

## 3 Context-Level Passes (polyopt / polyc -O2+)

`polyglot::tools::Optimize(IRContext&)` runs:

| Pass | Header | Description |
|------|--------|-------------|
| `RunConstantFold` | `constant_fold.h` | Context-wide constant folding |
| `RunDeadCodeElimination` | `dead_code_elim.h` | Context-wide dead code removal |
| `RunCommonSubexpressionElimination` | `common_subexpr.h` | CSE across functions |
| `RunInlining` | `inlining.h` | Inline small / single-call-site functions |

---

## 4 Advanced Passes (`advanced_optimizations.h`)

These passes are available for `-O2`/`-O3` and LTO:

| Pass | Function | Category |
|------|----------|----------|
| Tail-call optimisation | `TailCallOptimization` | Control flow |
| Loop unrolling | `LoopUnrolling` | Loop |
| Software pipelining | `SoftwarePipelining` | Loop |
| Strength reduction | `StrengthReduction` | Arithmetic |
| LICM | `LoopInvariantCodeMotion` | Loop |
| Induction variable elim. | `InductionVariableElimination` | Loop |
| Partial evaluation | `PartialEvaluation` | Constant |
| Escape analysis | `EscapeAnalysis` | Memory |
| SRA | `ScalarReplacement` | Memory |
| Dead store elimination | `DeadStoreElimination` | Memory |
| Auto-vectorisation | `AutoVectorization` | SIMD |
| Loop fusion | `LoopFusion` | Loop |
| Loop fission | `LoopFission` | Loop |
| Loop interchange | `LoopInterchange` | Loop |
| Loop tiling | `LoopTiling` | Loop / cache |
| Alias analysis | `AliasAnalysis` | Analysis |
| SCCP | `SCCP` | Constant |
| Code sinking | `CodeSinking` | Scheduling |
| Code hoisting | `CodeHoisting` | Scheduling |
| Branch prediction hints | `BranchPrediction` | Control flow |

---

## 5 GVN / PRE (`gvn.h`)

`GVNPass` performs Global Value Numbering with optional Partial Redundancy
Elimination (PRE).  It operates per-function and is invoked at `-O2`.

---

## 6 Loop Optimisation Framework (`loop_optimization.h`)

`LoopAnalysis` detects natural loops, computes nesting depth, and provides
`LoopInfo` structures consumed by loop passes (unrolling, LICM, tiling,
interchange, fusion, fission).

---

## 7 Link-Time Optimization (`link_time_optimizer.h`)

LTO operates across translation units at link time.  Key features:

| Feature | Description |
|---------|-------------|
| Cross-module inlining | Inline functions across IR modules using a cost model |
| IPCP | Interprocedural constant propagation with lattice meet |
| Global DCE | Remove functions / globals unreachable from entry points |
| Devirtualisation | Resolve virtual/interface calls when concrete type is known |
| GVN (global) | Cross-module redundancy elimination |
| Thin LTO | Scalable LTO via summary-based selection |

### 7.1 LTO Cost Model

The inlining cost model balances several factors:

| Factor | Default Value |
|--------|---------------|
| Base instruction cost | 5 per instruction |
| Small function bonus | 50 (threshold ≤ 10 instructions) |
| Single call-site bonus | 75 |
| Hot call-site bonus (PGO) | 100 |
| Recursive penalty | 200 |
| Complexity penalty | 2 per basic block |

### 7.2 PGO Integration

When profile data is available the LTO pass adjusts inline thresholds:

- Hot call sites receive `kHotCallSiteBonus` (100), lowering the effective
  cost threshold and making inlining more likely.
- Profile counts drive loop unrolling factor selection.
- Branch prediction hints are emitted for backends that support them.

---

## 8 Switch Matrix

| Flag | `polyc` | `polyopt` | Effect |
|------|---------|-----------|--------|
| `-O0` | ✓ | ✓ | No optimisation |
| `-O1` | ✓ | ✓ | Basic: constant fold + DCE |
| `-O2` | ✓ | ✓ | Standard: + CSE + inlining |
| `-O3` | ✓ | ✓ | Aggressive: same as -O2 (reserved for future) |
| `--lto` | ✓ | — | Enable link-time optimisation |
| `--emit-ir <path>` | ✓ | — | Dump optimised IR to file |
| `-o <file>` | ✓ | ✓ | Output path |

### 8.1 polyopt Usage

```bash
polyopt [options] <input.ir> [-o <output.ir>]
```

| Option | Default | Description |
|--------|---------|-------------|
| `-O0` | — | Disable all passes |
| `-O1` | — | Constant fold + DCE only |
| `-O2` | ✓ | Full pipeline |
| `-O3` | — | Same as -O2 |
| `-o <file>` | stdout | Output file |

---

## 9 Adding a New Pass

1. Declare the pass function in `middle/include/passes/transform/`.
2. Implement in `middle/src/passes/transform/`.
3. Register in `polyglot::tools::Optimize()` or the per-function pipeline.
4. Add a unit test in `tests/unit/middle/`.
5. If the pass has a CLI switch, wire it through `polyopt` and `polyc`.

---

## 10 Roadmap

- Integrate PGO profile reader into `polyc` CLI.
- Expose per-pass timing diagnostics (`-ftime-report`).
- Implement `-O3` differentiation (auto-vectorisation, software pipelining).
- Add LTO partitioning for parallel optimisation.
