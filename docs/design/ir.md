# Polyglot IR core

This document defines the IR semantics we will implement across frontends and backends. Scope: full SSA, explicit control flow, explicit memory model, and target-aware layout for arm64 and x86_64.

## Goals
- Target-independent SSA IR that can be lowered to arm64 and x86_64 without relying on external toolchains.
- Textual form that round-trips (printer + parser) for testing and debugging.
- Deterministic verifier and analyses (CFG, dominators, DF, liveness, alias, loops) used by all passes.
- Well-specified ABI surface: calling convention, data layout, alignment rules.

## Types
- Scalars: `i1, i8, i16, i32, i64, f32, f64` (add `i16` vs code today), `void`.
- Aggregates: pointers/references (single pointee), arrays `[N x T]`, vectors `<N x T>`, structs `{T0, T1, ...}` with optional named tag, functions `(ret, params...)`.
- Metadata: bitwidth, signedness (integers carry sign info for division/extend), size/align per target, layout helpers:
	- Size/align rules per target (arm64: AAPCS64/ILP64; x86_64: SysV LP64). Struct layout uses natural alignment with padding; vectors align to elem size * lanes (min 16 for 128-bit vectors).
	- Pointers: size = pointer width (64), alignment = pointer width.
	- Functions: not first-class sized; only callable pointers have size of pointer.

## Values and instructions
- Every SSA value has a type, unique name, and def site. Operands use SSA references, not free strings, in the internal representation (code will be updated accordingly).
- Constants: integer, float, `undef`, `null`, `poison` (for undefined but non-trapping), `ConstantArray`, `ConstantStruct`, `ConstantString` (null-terminated option), `ConstantGEP` (typed address calc).
- Memory and effects: instructions carry effect kinds (`pure`, `reads`, `writes`, `terminator`). Side-effectful instructions block reordering/CSE unless proven safe.
- Core instruction set (stable):
	- Arithmetic/logic: add, sub, mul, sdiv/udiv, srem/urem, and/or/xor, shifts (shl, lshr, ashr), comparisons (eq, ne, slt, sle, sgt, sge, ult, ule, ugt, uge), fadd/fsub/fmul/fdiv/fo cmp variants.
	- Casts: zext, sext, trunc, fpext, fptrunc, bitcast, inttoptr, ptrtoint.
	- Memory: alloca (stack slot), load, store, gep (inbounds), memcpy/memset intrinsic, addrspace (reserved for future).
	- Control: ret, br, cbr, switch, unreachable.
	- Calls: direct/indirect call with function type, varargs flag, calling convention (default target CC), tail marker (tail/call).
	- Phi: SSA merge node.

## Control flow and dominance
- Each block has zero or more `phi`, zero or more non-terminators, exactly one terminator.
- CFG is explicit via terminators; unreachable blocks are allowed but ignored by SSA/analysis until pruned.
- Dominators computed on reachable blocks from entry; DF used for SSA placement. We will add a verifier check for dominance (use must be dominated by def for reachable paths).

## Memory model
- Flat address space, byte-addressable. No trap on out-of-bounds at IR level (undefined behavior), leaving room for sanitizer passes.
- Aliasing classes: stack (alloca), globals, args, unknown. `addr_taken` marks escaping stack slots. Future: `noalias`/`readonly`/`writeonly` attributes on function params.
- Alignment: every load/store may carry an optional alignment; default natural alignment from type.

## Calling convention (MVP)
- x86_64 SysV: integer/pointer args in RDI, RSI, RDX, RCX, R8, R9; float in XMM0-7; return in RAX/XMM0; stack 16-byte aligned at call. Callee-saved: RBX, RBP, R12–R15.
- arm64 AAPCS64: integer/pointer args in x0–x7, float in v0–v7; return in x0/v0; stack 16-byte aligned. Callee-saved: x19–x28, fp/lr.
- Varargs: classify fixed args as above; spill excess to stack; maintain shadow space/red zones per target rules (SysV red zone allowed; Windows ABI not supported in MVP).
- No exceptions in MVP (calls assumed `nounwind`); will add landingpad later.

## Textual form (to implement)
- Module: globals then functions. Example syntax:
	- `global @g : i64 = 42`
	- `const @msg : [6 x i8] = "hello\00"`
	- `func @add(i64 %a, i64 %b) -> i64 { ... }`
- Blocks: `block ^name:` followed by `phi` lines then instructions; terminator last.
- Instructions: `name = op operands : type` style, stable and parseable (LL(1) grammar planned).

## Verification rules (to implement/extend)
- Structural: entry present, each block has one terminator, preds/succs consistent, no instructions after terminator.
- Typing: operands must type-check; gep indices in-bounds for static aggregates; casts obey bitwidth/compatibility; call argument/return types match callee type; phi incoming types match result.
- Dominance: for reachable blocks, def must dominate uses (including phi incoming per edge order).
- Memory: load/store pointer types match pointee; alignment must be >= type align when specified.

## Analyses and passes (target state)
- CFG, dominators, DF, loop info, liveness (per variable), alias (stack/global/arg/unknown + escape), postorder/RPO utilities.
- SSA: insert phi via DF, rename with stacks seeded by params/alloca promotions, support mem2reg with dominance and alias data.
- Optimizations (safe subset first): const fold, SCCP, copy-prop, GVN/CSE with hashing and effect fences, dead-code elim/ADCE, CFG simplification (branch folding, block merging), mem2reg.

## Testing
- Golden round-trip tests for printer/parser.
- Unit tests for verifier (positive/negative), CFG/dom/DF, SSA placement/rename, each pass, and analysis outputs.
- Integration: build small programs through frontend->IR->codegen->execute on arm64/x86_64.
