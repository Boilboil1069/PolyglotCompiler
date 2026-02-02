  #pragma once

#include "middle/include/ir/cfg.h"

namespace polyglot::passes::transform {

// Tail Call Optimization
// Convert tail recursion to loops to avoid stack overflow
// Set convert_to_loop=false to only mark tail calls without conversion
void TailCallOptimization(ir::Function &func, bool convert_to_loop = true);

// Loop Unrolling
// Unroll loops to reduce loop overhead and improve ILP
void LoopUnrolling(ir::Function &func, size_t factor = 4);

// Software Pipelining
// Overlap loop iterations to improve performance
void SoftwarePipelining(ir::Function &func);

// Strength Reduction
// Replace expensive operations with cheaper equivalents (e.g., multiply → add)
void StrengthReduction(ir::Function &func);

// Loop-Invariant Code Motion
// Hoist invariant computations out of loops
void LoopInvariantCodeMotion(ir::Function &func);

// Induction Variable Elimination
// Simplify or eliminate loop induction variables
void InductionVariableElimination(ir::Function &func);

// Partial Evaluation
// Execute part of the computation at compile time
void PartialEvaluation(ir::Function &func);

// Escape Analysis
// Determine if objects escape a function to enable stack allocation
void EscapeAnalysis(ir::Function &func);

// Scalar Replacement of Aggregates
// Split structs/arrays into independent scalar variables
void ScalarReplacement(ir::Function &func);

// Dead Store Elimination
// Remove stores whose values are never read
void DeadStoreElimination(ir::Function &func);

// Auto-Vectorization
// Automatically convert scalar operations to SIMD vector operations
void AutoVectorization(ir::Function &func);

// Loop Fusion
// Merge adjacent loops to improve cache locality
void LoopFusion(ir::Function &func);

// Loop Fission
// Split a loop into multiple loops to improve parallelism
void LoopFission(ir::Function &func);

// Loop Interchange
// Swap nested loop orders to improve cache performance
void LoopInterchange(ir::Function &func);

// Loop Tiling
// Tile loops to improve cache utilization
void LoopTiling(ir::Function &func, size_t tile_size = 64);

// Alias Analysis
// Analyze pointer alias relationships
void AliasAnalysis(ir::Function &func);

// Sparse Conditional Constant Propagation
// SSA-based advanced constant propagation
void SCCP(ir::Function &func);

// Code Sinking
// Move computations closer to their use sites
void CodeSinking(ir::Function &func);

// Code Hoisting
// Hoist common code to dominating nodes
void CodeHoisting(ir::Function &func);

// Jump Threading
// Optimize jump chains and branch prediction
void JumpThreading(ir::Function &func);

// Global Value Numbering
// Identify equivalent expressions and reuse results
void GVN(ir::Function &func);

// Prefetch Insertion
// Insert prefetch instructions to hide memory latency
void PrefetchInsertion(ir::Function &func);

// Branch Prediction Optimization
// Optimize branch layout using profile data
void BranchPredictionOptimization(ir::Function &func);

// Loop Predication
// Use predicated execution to eliminate branches in loops
void LoopPredication(ir::Function &func);

// Memory Layout Optimization
// Optimize data structure layout for cache performance
void MemoryLayoutOptimization(ir::Function &func);

}  // namespace polyglot::passes::transform
