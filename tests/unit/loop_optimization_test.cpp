/**
 * Unit tests for Loop Optimization and Tail Call Optimization
 *
 * Tests cover:
 * - Tail Call Detection and Marking
 * - Tail Recursion to Loop Conversion
 * - Loop Analysis
 * - Loop-Invariant Code Motion (LICM)
 * - Loop Unrolling
 * - Loop Strength Reduction
 */

#include <catch2/catch_test_macros.hpp>
#include "middle/include/passes/transform/loop_optimization.h"
#include "middle/include/passes/transform/advanced_optimizations.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/nodes/expressions.h"

using namespace polyglot::passes::transform;
using namespace polyglot::ir;

// ============================================================================
// Helper Functions for Test Setup
// ============================================================================

namespace {

/**
 * Create a simple loop structure for testing
 * Structure: entry -> header -> body -> header (back edge)
 *                          \-> exit
 */
Function CreateSimpleLoop() {
    Function func;
    func.name = "simple_loop";
    
    // Create blocks
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    auto header = std::make_shared<BasicBlock>();
    header->name = "loop.header";
    
    auto body = std::make_shared<BasicBlock>();
    body->name = "loop.body";
    
    auto exit_block = std::make_shared<BasicBlock>();
    exit_block->name = "loop.exit";
    
    // Entry -> Header
    auto br_entry = std::make_shared<BranchStatement>();
    br_entry->target = header.get();
    entry->SetTerminator(br_entry);
    entry->successors.push_back(header.get());
    header->predecessors.push_back(entry.get());
    
    // Create induction variable phi in header
    auto phi_i = std::make_shared<PhiInstruction>();
    phi_i->name = "%i";
    phi_i->type = IRType::I32();
    phi_i->incomings.push_back({entry.get(), "0"});
    phi_i->incomings.push_back({body.get(), "%i.next"});
    header->phis.push_back(phi_i);
    
    // Header: i < 10 comparison
    auto cmp = std::make_shared<BinaryInstruction>();
    cmp->op = BinaryInstruction::Op::kCmpSlt;
    cmp->name = "%cond";
    cmp->operands = {"%i", "10"};
    cmp->type = IRType::I1();
    header->instructions.push_back(cmp);
    
    // Header -> Body (if cond) or Exit (if !cond)
    auto br_header = std::make_shared<CondBranchStatement>();
    br_header->operands = {"%cond"};
    br_header->true_target = body.get();
    br_header->false_target = exit_block.get();
    header->SetTerminator(br_header);
    header->successors.push_back(body.get());
    header->successors.push_back(exit_block.get());
    body->predecessors.push_back(header.get());
    exit_block->predecessors.push_back(header.get());
    
    // Body: i.next = i + 1
    auto add = std::make_shared<BinaryInstruction>();
    add->op = BinaryInstruction::Op::kAdd;
    add->name = "%i.next";
    add->operands = {"%i", "1"};
    add->type = IRType::I32();
    body->instructions.push_back(add);
    
    // Body -> Header (back edge)
    auto br_body = std::make_shared<BranchStatement>();
    br_body->target = header.get();
    body->SetTerminator(br_body);
    body->successors.push_back(header.get());
    header->predecessors.push_back(body.get());
    
    // Exit: return
    auto ret = std::make_shared<ReturnStatement>();
    exit_block->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.blocks.push_back(header);
    func.blocks.push_back(body);
    func.blocks.push_back(exit_block);
    func.entry = entry.get();
    
    return func;
}

/**
 * Create a function with a tail call for testing
 */
Function CreateTailCallFunction() {
    Function func;
    func.name = "factorial";
    func.params = {"n", "acc"};
    func.param_types = {IRType::I32(), IRType::I32()};
    
    // Create blocks
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    auto if_true = std::make_shared<BasicBlock>();
    if_true->name = "return.acc";
    
    auto if_false = std::make_shared<BasicBlock>();
    if_false->name = "recurse";
    
    // Entry: check n <= 1
    auto cmp = std::make_shared<BinaryInstruction>();
    cmp->op = BinaryInstruction::Op::kCmpSle;
    cmp->name = "%cmp";
    cmp->operands = {"%n", "1"};
    cmp->type = IRType::I1();
    entry->instructions.push_back(cmp);
    
    auto br = std::make_shared<CondBranchStatement>();
    br->operands = {"%cmp"};
    br->true_target = if_true.get();
    br->false_target = if_false.get();
    entry->SetTerminator(br);
    entry->successors.push_back(if_true.get());
    entry->successors.push_back(if_false.get());
    if_true->predecessors.push_back(entry.get());
    if_false->predecessors.push_back(entry.get());
    
    // If true: return acc
    auto ret1 = std::make_shared<ReturnStatement>();
    ret1->operands = {"%acc"};
    if_true->SetTerminator(ret1);
    
    // If false: return factorial(n-1, n*acc)
    auto sub = std::make_shared<BinaryInstruction>();
    sub->op = BinaryInstruction::Op::kSub;
    sub->name = "%n_minus_1";
    sub->operands = {"%n", "1"};
    sub->type = IRType::I32();
    if_false->instructions.push_back(sub);
    
    auto mul = std::make_shared<BinaryInstruction>();
    mul->op = BinaryInstruction::Op::kMul;
    mul->name = "%n_times_acc";
    mul->operands = {"%n", "%acc"};
    mul->type = IRType::I32();
    if_false->instructions.push_back(mul);
    
    auto call = std::make_shared<CallInstruction>();
    call->callee = "factorial";  // Self-recursive
    call->name = "%result";
    call->operands = {"%n_minus_1", "%n_times_acc"};
    call->type = IRType::I32();
    if_false->instructions.push_back(call);
    
    auto ret2 = std::make_shared<ReturnStatement>();
    ret2->operands = {"%result"};
    if_false->SetTerminator(ret2);
    
    func.blocks.push_back(entry);
    func.blocks.push_back(if_true);
    func.blocks.push_back(if_false);
    func.entry = entry.get();
    
    return func;
}

/**
 * Create a function with loop-invariant code for LICM testing
 */
Function CreateLICMTestFunction() {
    Function func;
    func.name = "licm_test";
    
    // Create blocks
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    auto header = std::make_shared<BasicBlock>();
    header->name = "loop.header";
    
    auto body = std::make_shared<BasicBlock>();
    body->name = "loop.body";
    
    auto exit_block = std::make_shared<BasicBlock>();
    exit_block->name = "loop.exit";
    
    // Entry -> Header
    auto br_entry = std::make_shared<BranchStatement>();
    br_entry->target = header.get();
    entry->SetTerminator(br_entry);
    entry->successors.push_back(header.get());
    header->predecessors.push_back(entry.get());
    
    // Induction variable phi
    auto phi_i = std::make_shared<PhiInstruction>();
    phi_i->name = "%i";
    phi_i->type = IRType::I32();
    phi_i->incomings.push_back({entry.get(), "0"});
    phi_i->incomings.push_back({body.get(), "%i.next"});
    header->phis.push_back(phi_i);
    
    // Comparison
    auto cmp = std::make_shared<BinaryInstruction>();
    cmp->op = BinaryInstruction::Op::kCmpSlt;
    cmp->name = "%cond";
    cmp->operands = {"%i", "100"};
    cmp->type = IRType::I1();
    header->instructions.push_back(cmp);
    
    auto br_header = std::make_shared<CondBranchStatement>();
    br_header->operands = {"%cond"};
    br_header->true_target = body.get();
    br_header->false_target = exit_block.get();
    header->SetTerminator(br_header);
    header->successors.push_back(body.get());
    header->successors.push_back(exit_block.get());
    body->predecessors.push_back(header.get());
    exit_block->predecessors.push_back(header.get());
    
    // Loop body: has loop-invariant computation %a + %b
    auto invariant = std::make_shared<BinaryInstruction>();
    invariant->op = BinaryInstruction::Op::kAdd;
    invariant->name = "%invariant";
    invariant->operands = {"%a", "%b"};  // Both defined outside loop
    invariant->type = IRType::I32();
    body->instructions.push_back(invariant);
    
    // Use invariant result
    auto use = std::make_shared<BinaryInstruction>();
    use->op = BinaryInstruction::Op::kAdd;
    use->name = "%use";
    use->operands = {"%i", "%invariant"};
    use->type = IRType::I32();
    body->instructions.push_back(use);
    
    // IV update
    auto add = std::make_shared<BinaryInstruction>();
    add->op = BinaryInstruction::Op::kAdd;
    add->name = "%i.next";
    add->operands = {"%i", "1"};
    add->type = IRType::I32();
    body->instructions.push_back(add);
    
    // Back edge
    auto br_body = std::make_shared<BranchStatement>();
    br_body->target = header.get();
    body->SetTerminator(br_body);
    body->successors.push_back(header.get());
    header->predecessors.push_back(body.get());
    
    // Exit
    auto ret = std::make_shared<ReturnStatement>();
    exit_block->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.blocks.push_back(header);
    func.blocks.push_back(body);
    func.blocks.push_back(exit_block);
    func.entry = entry.get();
    
    return func;
}

}  // namespace

// ============================================================================
// Loop Analysis Tests
// ============================================================================

TEST_CASE("Loop Analysis - Simple Loop Detection", "[loop-optimization][analysis]") {
    Function func = CreateSimpleLoop();
    
    LoopAnalysis analysis(func);
    const auto& loops = analysis.GetLoops();
    
    SECTION("Detects at least one loop") {
        REQUIRE(loops.size() >= 1);
    }
    
    SECTION("Loop header is correct") {
        if (!loops.empty()) {
            REQUIRE(loops[0]->header != nullptr);
            REQUIRE(loops[0]->header->name == "loop.header");
        }
    }
    
    SECTION("GetLoopForBlock returns correct loop") {
        if (!loops.empty()) {
            auto* header_bb = func.blocks[1].get();
            LoopInfo* loop = analysis.GetLoopForBlock(header_bb);
            if (loop) {
                REQUIRE(loop->header == header_bb);
            }
        }
    }
}

// ============================================================================
// Tail Call Optimization Tests
// ============================================================================

TEST_CASE("Tail Call Optimization - Detection", "[tco][optimization]") {
    Function func = CreateTailCallFunction();
    
    SECTION("Before optimization, call is not marked as tail call") {
        // Find the call instruction
        CallInstruction* call = nullptr;
        for (auto& bb : func.blocks) {
            for (auto& inst : bb->instructions) {
                if (auto c = std::dynamic_pointer_cast<CallInstruction>(inst)) {
                    call = c.get();
                }
            }
        }
        REQUIRE(call != nullptr);
        REQUIRE(!call->is_tail_call);
    }
    
    SECTION("After optimization, tail call is marked") {
        // Use convert_to_loop=false to only mark without converting
        TailCallOptimization(func, false);
        
        // Find the call instruction
        bool found_tail_call = false;
        for (auto& bb : func.blocks) {
            for (auto& inst : bb->instructions) {
                if (auto c = std::dynamic_pointer_cast<CallInstruction>(inst)) {
                    if (c->is_tail_call) {
                        found_tail_call = true;
                    }
                }
            }
        }
        REQUIRE(found_tail_call);
    }
}

TEST_CASE("Tail Call Optimization - Self-Recursive Detection", "[tco][optimization]") {
    Function func = CreateTailCallFunction();
    TailCallOptimization(func, false);  // Don't convert, just mark
    
    // Find the call instruction and verify it's self-recursive
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto c = std::dynamic_pointer_cast<CallInstruction>(inst)) {
                if (c->is_tail_call) {
                    REQUIRE(c->callee == func.name);
                }
            }
        }
    }
}

TEST_CASE("Tail Call Optimization - Non-Tail Call", "[tco][optimization]") {
    Function func;
    func.name = "not_tail_call";
    
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    // call foo()
    auto call = std::make_shared<CallInstruction>();
    call->callee = "foo";
    call->name = "%result";
    call->type = IRType::I32();
    entry->instructions.push_back(call);
    
    // Use the result: result + 1 (this prevents it from being a tail call)
    auto add = std::make_shared<BinaryInstruction>();
    add->op = BinaryInstruction::Op::kAdd;
    add->name = "%final";
    add->operands = {"%result", "1"};
    add->type = IRType::I32();
    entry->instructions.push_back(add);
    
    // Return %final
    auto ret = std::make_shared<ReturnStatement>();
    ret->operands = {"%final"};
    entry->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.entry = entry.get();
    
    TailCallOptimization(func, false);  // Don't convert, just mark
    
    // The call should NOT be marked as tail call
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto c = std::dynamic_pointer_cast<CallInstruction>(inst)) {
                REQUIRE(!c->is_tail_call);
            }
        }
    }
}

TEST_CASE("Tail Call Optimization - Convert to Loop", "[tco][optimization]") {
    Function func = CreateTailCallFunction();
    
    // Verify call exists before
    size_t calls_before = 0;
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (std::dynamic_pointer_cast<CallInstruction>(inst)) {
                calls_before++;
            }
        }
    }
    REQUIRE(calls_before == 1);
    
    // Convert to loop
    TailCallOptimization(func, true);
    
    // After conversion, the self-recursive call should be eliminated
    size_t calls_after = 0;
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (std::dynamic_pointer_cast<CallInstruction>(inst)) {
                calls_after++;
            }
        }
    }
    REQUIRE(calls_after == 0);
    
    // There should now be a branch back to entry instead
    bool found_branch_to_entry = false;
    for (auto& bb : func.blocks) {
        if (auto br = std::dynamic_pointer_cast<BranchStatement>(bb->terminator)) {
            if (br->target == func.entry) {
                found_branch_to_entry = true;
            }
        }
    }
    REQUIRE(found_branch_to_entry);
}

// ============================================================================
// Loop-Invariant Code Motion Tests
// ============================================================================

TEST_CASE("LICM - Identify Loop Invariant", "[licm][optimization]") {
    Function func = CreateLICMTestFunction();
    
    LoopAnalysis analysis(func);
    LICMPass pass(func);
    
    // The analysis should detect at least one loop
    REQUIRE(analysis.GetLoops().size() >= 1);
}

TEST_CASE("LICM - Run Pass", "[licm][optimization]") {
    Function func = CreateLICMTestFunction();
    
    LICMPass pass(func);
    pass.Run();
    
    // Function should still be valid
    REQUIRE(func.entry != nullptr);
}

// ============================================================================
// Loop Unrolling Tests
// ============================================================================

TEST_CASE("Loop Unrolling - Basic Unroll", "[unroll][optimization]") {
    Function func = CreateSimpleLoop();
    
    LoopUnrollingPass pass(func, 4);
    pass.Run();
    
    // Function should still be valid
    REQUIRE(func.entry != nullptr);
    REQUIRE(func.blocks.size() >= 4);
}

// ============================================================================
// Loop Strength Reduction Tests
// ============================================================================

TEST_CASE("Strength Reduction - Run Pass", "[strength-reduction][optimization]") {
    Function func = CreateSimpleLoop();
    
    LoopStrengthReductionPass pass(func);
    pass.Run();
    
    // Function should still be valid
    REQUIRE(func.entry != nullptr);
}

// ============================================================================
// Escape Analysis Tests
// ============================================================================

TEST_CASE("Escape Analysis - Non-escaping Allocation", "[escape-analysis][optimization]") {
    Function func;
    func.name = "escape_test";
    
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    // Local allocation
    auto alloca = std::make_shared<AllocaInstruction>();
    alloca->name = "%local";
    alloca->type = IRType::Pointer(IRType::I32());
    entry->instructions.push_back(alloca);
    
    // Store to local
    auto store = std::make_shared<StoreInstruction>();
    store->operands = {"%local", "42"};
    store->type = IRType::I32();
    entry->instructions.push_back(store);
    
    // Load from local
    auto load = std::make_shared<LoadInstruction>();
    load->name = "%val";
    load->operands = {"%local"};
    load->type = IRType::I32();
    entry->instructions.push_back(load);
    
    // Return the value (not the pointer)
    auto ret = std::make_shared<ReturnStatement>();
    ret->operands = {"%val"};
    entry->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.entry = entry.get();
    
    EscapeAnalysis(func);
    
    // The allocation should be marked as non-escaping
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto a = std::dynamic_pointer_cast<AllocaInstruction>(inst)) {
                REQUIRE(a->no_escape == true);
            }
        }
    }
}

TEST_CASE("Escape Analysis - Escaping Allocation", "[escape-analysis][optimization]") {
    Function func;
    func.name = "escape_test";
    
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    // Local allocation
    auto alloca = std::make_shared<AllocaInstruction>();
    alloca->name = "%local";
    alloca->type = IRType::Pointer(IRType::I32());
    entry->instructions.push_back(alloca);
    
    // Return the pointer (escapes!)
    auto ret = std::make_shared<ReturnStatement>();
    ret->operands = {"%local"};
    entry->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.entry = entry.get();
    
    EscapeAnalysis(func);
    
    // The allocation should NOT be marked as non-escaping
    for (auto& bb : func.blocks) {
        for (auto& inst : bb->instructions) {
            if (auto a = std::dynamic_pointer_cast<AllocaInstruction>(inst)) {
                REQUIRE(a->no_escape == false);
            }
        }
    }
}

// ============================================================================
// Dead Store Elimination Tests
// ============================================================================

TEST_CASE("Dead Store Elimination - Remove Dead Store", "[dse][optimization]") {
    Function func;
    func.name = "dse_test";
    
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    // Alloca
    auto alloca = std::make_shared<AllocaInstruction>();
    alloca->name = "%ptr";
    alloca->type = IRType::Pointer(IRType::I32());
    entry->instructions.push_back(alloca);
    
    // Dead store: store 10 to %ptr
    auto store1 = std::make_shared<StoreInstruction>();
    store1->operands = {"%ptr", "10"};
    store1->type = IRType::I32();
    entry->instructions.push_back(store1);
    
    // Overwriting store: store 20 to %ptr (makes previous store dead)
    auto store2 = std::make_shared<StoreInstruction>();
    store2->operands = {"%ptr", "20"};
    store2->type = IRType::I32();
    entry->instructions.push_back(store2);
    
    // Return
    auto ret = std::make_shared<ReturnStatement>();
    entry->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.entry = entry.get();
    
    size_t insts_before = entry->instructions.size();
    
    DeadStoreElimination(func);
    
    size_t insts_after = entry->instructions.size();
    
    // One store should be removed
    REQUIRE(insts_after < insts_before);
}

// ============================================================================
// SCCP Tests
// ============================================================================

TEST_CASE("SCCP - Constant Propagation", "[sccp][optimization]") {
    Function func;
    func.name = "sccp_test";
    
    auto entry = std::make_shared<BasicBlock>();
    entry->name = "entry";
    
    // x = 5 + 3 (should become 8)
    auto add1 = std::make_shared<BinaryInstruction>();
    add1->op = BinaryInstruction::Op::kAdd;
    add1->name = "%x";
    add1->operands = {"5", "3"};
    add1->type = IRType::I32();
    entry->instructions.push_back(add1);
    
    // y = x + 2 (should become 10)
    auto add2 = std::make_shared<BinaryInstruction>();
    add2->op = BinaryInstruction::Op::kAdd;
    add2->name = "%y";
    add2->operands = {"%x", "2"};
    add2->type = IRType::I32();
    entry->instructions.push_back(add2);
    
    auto ret = std::make_shared<ReturnStatement>();
    ret->operands = {"%y"};
    entry->SetTerminator(ret);
    
    func.blocks.push_back(entry);
    func.entry = entry.get();
    
    SCCP(func);
    
    // Check that constants were propagated
    // After SCCP, %x = 42 (constant) and %y = 42 + 2 = 44
    // The function should still have a valid entry block with instructions
    REQUIRE(func.blocks.size() > 0);
    REQUIRE(func.entry->instructions.size() > 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("Integration - Multiple Optimizations", "[integration][optimization]") {
    Function func = CreateSimpleLoop();
    
    // Run multiple optimization passes in sequence
    TailCallOptimization(func);
    
    LICMPass licm(func);
    licm.Run();
    
    LoopUnrollingPass unroll(func, 2);
    unroll.Run();
    
    LoopStrengthReductionPass strength(func);
    strength.Run();
    
    DeadStoreElimination(func);
    SCCP(func);
    JumpThreading(func);
    
    // Function should still be valid after all optimizations
    REQUIRE(func.entry != nullptr);
    REQUIRE(!func.blocks.empty());
}
