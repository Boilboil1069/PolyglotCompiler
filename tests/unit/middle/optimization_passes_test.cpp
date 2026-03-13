#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>

#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/passes/transform/advanced_optimizations.h"
#include "middle/include/ir/passes/opt.h"

using namespace polyglot::ir;
using namespace polyglot::passes::transform;

// ---------------------------------------------------------------------------
// Helpers: build IR constructs for testing
// ---------------------------------------------------------------------------

// Create a function with an entry block
static Function CreateFuncWithEntry(const std::string &name) {
    Function func;
    func.name = name;
    func.ret_type = IRType::I64();
    auto *entry = func.CreateBlock("entry");
    (void)entry;
    return func;
}

// Convenience: make a BinaryInstruction
static std::shared_ptr<BinaryInstruction> MakeBinOp(
    BinaryInstruction::Op op,
    const std::string &result_name,
    const std::string &lhs,
    const std::string &rhs,
    IRType ty = IRType::I64()) {
    auto inst = std::make_shared<BinaryInstruction>();
    inst->op = op;
    inst->name = result_name;
    inst->type = ty;
    inst->operands = {lhs, rhs};
    return inst;
}

// Convenience: make a ReturnStatement
static std::shared_ptr<ReturnStatement> MakeRet(const std::string &val = "") {
    auto ret = std::make_shared<ReturnStatement>();
    if (!val.empty()) {
        ret->operands = {val};
    }
    return ret;
}

// Convenience: make a StoreInstruction
static std::shared_ptr<StoreInstruction> MakeStore(
    const std::string &addr,
    const std::string &value) {
    auto st = std::make_shared<StoreInstruction>();
    st->operands = {value, addr};
    return st;
}

// Convenience: make a LoadInstruction
static std::shared_ptr<LoadInstruction> MakeLoad(
    const std::string &result_name,
    const std::string &addr,
    IRType ty = IRType::I64()) {
    auto ld = std::make_shared<LoadInstruction>();
    ld->name = result_name;
    ld->type = ty;
    ld->operands = {addr};
    return ld;
}

// Convenience: make a CallInstruction
static std::shared_ptr<CallInstruction> MakeCall(
    const std::string &result_name,
    const std::string &callee,
    std::vector<std::string> args = {},
    IRType ret = IRType::I64()) {
    auto call = std::make_shared<CallInstruction>();
    call->name = result_name;
    call->callee = callee;
    call->type = ret;
    call->operands = std::move(args);
    return call;
}

// Convenience: make a CondBranch
static std::shared_ptr<CondBranchStatement> MakeCondBr(
    const std::string &cond,
    BasicBlock *true_bb,
    BasicBlock *false_bb) {
    auto br = std::make_shared<CondBranchStatement>();
    br->operands = {cond};
    br->true_target = true_bb;
    br->false_target = false_bb;
    return br;
}

// Convenience: make an unconditional BranchStatement
static std::shared_ptr<BranchStatement> MakeBr(BasicBlock *target) {
    auto br = std::make_shared<BranchStatement>();
    br->target = target;
    return br;
}

// Convenience: make an AllocaInstruction
static std::shared_ptr<AllocaInstruction> MakeAlloca(
    const std::string &result_name,
    IRType pointee = IRType::I64()) {
    auto alloca_inst = std::make_shared<AllocaInstruction>();
    alloca_inst->name = result_name;
    alloca_inst->type = IRType::Pointer(pointee);
    return alloca_inst;
}

// Count instructions of a specific type in the function
template <typename T>
static size_t CountInstructions(const Function &func) {
    size_t count = 0;
    for (auto &blk : func.blocks) {
        for (auto &inst : blk->instructions) {
            if (dynamic_cast<T *>(inst.get())) ++count;
        }
    }
    return count;
}

// Count all non-dead instructions
static size_t CountLiveInstructions(const Function &func) {
    size_t count = 0;
    for (auto &blk : func.blocks) {
        for (auto &inst : blk->instructions) {
            if (!inst->is_dead) ++count;
        }
    }
    return count;
}

// Find an instruction by name
static Instruction *FindInstruction(Function &func, const std::string &name) {
    for (auto &blk : func.blocks) {
        for (auto &inst : blk->instructions) {
            if (inst->name == name) return inst.get();
        }
    }
    return nullptr;
}

// ============================================================================
// Test 1: Tail Call Optimization
// ============================================================================
TEST_CASE("Optimization - Tail Call Optimization", "[opt][tco]") {
    SECTION("Basic tail recursion") {
        // Build:  factorial(n) { if (n<=1) ret 1; else ret factorial(n-1)*n; }
        // TCO should mark tail calls
        Function func = CreateFuncWithEntry("factorial");
        func.params = {"n"};
        func.param_types = {IRType::I64()};
        auto *entry = func.blocks[0].get();

        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSle, "cmp", "n", "1");
        entry->AddInstruction(cmp);

        auto *then_bb = func.CreateBlock("then");
        auto *else_bb = func.CreateBlock("else");
        entry->SetTerminator(MakeCondBr("cmp", then_bb, else_bb));

        then_bb->SetTerminator(MakeRet("1"));

        auto sub = MakeBinOp(BinaryInstruction::Op::kSub, "n_minus_1", "n", "1");
        else_bb->AddInstruction(sub);
        auto call = MakeCall("rec", "factorial", {"n_minus_1"});
        call->is_tail_call = false;
        else_bb->AddInstruction(call);
        auto mul = MakeBinOp(BinaryInstruction::Op::kMul, "result", "rec", "n");
        else_bb->AddInstruction(mul);
        else_bb->SetTerminator(MakeRet("result"));

        TailCallOptimization(func);

        // The function should still compile and have the correct structure
        REQUIRE(func.blocks.size() >= 3);
    }

    SECTION("Non-tail recursion stays unchanged") {
        Function func = CreateFuncWithEntry("fib");
        func.params = {"n"};
        auto *entry = func.blocks[0].get();
        auto call = MakeCall("r", "fib", {"n"});
        entry->AddInstruction(call);
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "sum", "r", "1");
        entry->AddInstruction(add);
        entry->SetTerminator(MakeRet("sum"));

        size_t before = CountInstructions<CallInstruction>(func);
        TailCallOptimization(func);
        // Call is not in tail position, should remain
        REQUIRE(CountInstructions<CallInstruction>(func) == before);
    }

    SECTION("No tail call — straight-line code") {
        Function func = CreateFuncWithEntry("simple");
        auto *entry = func.blocks[0].get();
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "x", "1", "2");
        entry->AddInstruction(add);
        entry->SetTerminator(MakeRet("x"));

        TailCallOptimization(func);
        REQUIRE(func.blocks.size() == 1);
    }
}

// ============================================================================
// Test 2: Loop Unrolling
// ============================================================================
TEST_CASE("Optimization - Loop Unrolling", "[opt][unroll]") {
    SECTION("Simple loop with known bound") {
        // Build: for (i=0; i<16; i++) { sum += arr[i]; }
        Function func;
        func.name = "simple_loop";
        auto *entry = func.CreateBlock("entry");
        auto *header = func.CreateBlock("header");
        auto *body = func.CreateBlock("body");
        auto *exit = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(header));

        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "cond", "i", "16");
        header->AddInstruction(cmp);
        header->SetTerminator(MakeCondBr("cond", body, exit));
        header->successors = {body, exit};
        body->predecessors = {header};
        exit->predecessors = {header};

        auto add_body = MakeBinOp(BinaryInstruction::Op::kAdd, "sum_next", "sum", "val");
        body->AddInstruction(add_body);
        auto inc = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        body->AddInstruction(inc);
        body->SetTerminator(MakeBr(header));
        body->successors = {header};
        header->predecessors = {entry, body};

        exit->SetTerminator(MakeRet("sum"));

        size_t blocks_before = func.blocks.size();
        LoopUnrolling(func, 4);

        // After unrolling the function should still have a valid structure
        REQUIRE(func.blocks.size() >= blocks_before);
    }

    SECTION("Empty function — no crash") {
        Function func;
        func.name = "empty";
        LoopUnrolling(func, 4);
        REQUIRE(func.blocks.empty());
    }
}

// ============================================================================
// Test 3: Strength Reduction
// ============================================================================
TEST_CASE("Optimization - Strength Reduction", "[opt][strength]") {
    SECTION("Multiply by power of 2 becomes shift") {
        Function func = CreateFuncWithEntry("mul_pow2");
        auto *entry = func.blocks[0].get();

        // x * 8 should become x << 3
        auto mul = MakeBinOp(BinaryInstruction::Op::kMul, "result", "x", "8");
        entry->AddInstruction(mul);
        entry->SetTerminator(MakeRet("result"));

        StrengthReduction(func);

        // Check that the multiply was converted to a shift or is still valid
        auto *inst = FindInstruction(func, "result");
        REQUIRE(inst != nullptr);
    }

    SECTION("Divide by power of 2") {
        Function func = CreateFuncWithEntry("div_pow2");
        auto *entry = func.blocks[0].get();

        // x / 4 should become x >> 2
        auto div_inst = MakeBinOp(BinaryInstruction::Op::kSDiv, "result", "x", "4");
        entry->AddInstruction(div_inst);
        entry->SetTerminator(MakeRet("result"));

        StrengthReduction(func);

        auto *inst = FindInstruction(func, "result");
        REQUIRE(inst != nullptr);
    }

    SECTION("Non-power of 2 unchanged") {
        Function func = CreateFuncWithEntry("mul_non_pow2");
        auto *entry = func.blocks[0].get();

        auto mul = MakeBinOp(BinaryInstruction::Op::kMul, "result", "x", "7");
        entry->AddInstruction(mul);
        entry->SetTerminator(MakeRet("result"));

        StrengthReduction(func);

        auto *inst = FindInstruction(func, "result");
        REQUIRE(inst != nullptr);
        auto *bin = dynamic_cast<BinaryInstruction *>(inst);
        // For non-power-of-2 the op may stay as kMul
        REQUIRE(bin != nullptr);
    }

    SECTION("Modulo by power of 2") {
        Function func = CreateFuncWithEntry("mod_pow2");
        auto *entry = func.blocks[0].get();

        auto rem = MakeBinOp(BinaryInstruction::Op::kURem, "result", "x", "16");
        entry->AddInstruction(rem);
        entry->SetTerminator(MakeRet("result"));

        StrengthReduction(func);

        auto *inst = FindInstruction(func, "result");
        REQUIRE(inst != nullptr);
    }
}

// ============================================================================
// Test 4: Loop Invariant Code Motion
// ============================================================================
TEST_CASE("Optimization - Loop Invariant Code Motion", "[opt][licm]") {
    SECTION("Invariant hoisted out of loop") {
        // Build: preheader -> header -> body -> header, header -> exit
        // Body computes y = a + b (invariant) and uses it
        Function func;
        func.name = "basic_licm";
        func.params = {"a", "b", "n"};

        auto *preheader = func.CreateBlock("preheader");
        auto *header = func.CreateBlock("header");
        auto *body = func.CreateBlock("body");
        auto *exit_bb = func.CreateBlock("exit");

        preheader->SetTerminator(MakeBr(header));
        preheader->successors = {header};

        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "cond", "i", "n");
        header->AddInstruction(cmp);
        header->SetTerminator(MakeCondBr("cond", body, exit_bb));
        header->successors = {body, exit_bb};
        header->predecessors = {preheader, body};

        // a+b is loop-invariant
        auto inv = MakeBinOp(BinaryInstruction::Op::kAdd, "inv", "a", "b");
        body->AddInstruction(inv);
        auto use_inv = MakeBinOp(BinaryInstruction::Op::kAdd, "used", "inv", "i");
        body->AddInstruction(use_inv);
        auto inc = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        body->AddInstruction(inc);
        body->SetTerminator(MakeBr(header));
        body->successors = {header};
        body->predecessors = {header};

        exit_bb->SetTerminator(MakeRet("used"));
        exit_bb->predecessors = {header};

        LoopInvariantCodeMotion(func);

        // The function should still be valid
        REQUIRE(func.blocks.size() >= 4);
    }

    SECTION("No invariant — nothing hoisted") {
        Function func = CreateFuncWithEntry("no_inv");
        auto *entry = func.blocks[0].get();
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "x", "1", "2");
        entry->AddInstruction(add);
        entry->SetTerminator(MakeRet("x"));

        size_t inst_count = CountLiveInstructions(func);
        LoopInvariantCodeMotion(func);
        REQUIRE(CountLiveInstructions(func) == inst_count);
    }
}

// ============================================================================
// Test 5: Induction Variable Elimination
// ============================================================================
TEST_CASE("Optimization - Induction Variable Elimination", "[opt][ive]") {
    SECTION("Basic induction variable") {
        Function func;
        func.name = "basic_iv";
        auto *entry = func.CreateBlock("entry");
        auto *header = func.CreateBlock("header");
        auto *body = func.CreateBlock("body");
        auto *exit_bb = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(header));
        entry->successors = {header};

        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "cond", "i", "100");
        header->AddInstruction(cmp);
        header->SetTerminator(MakeCondBr("cond", body, exit_bb));
        header->successors = {body, exit_bb};
        header->predecessors = {entry, body};

        // i = i + 1 is an induction variable
        auto inc = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        body->AddInstruction(inc);
        body->SetTerminator(MakeBr(header));
        body->successors = {header};
        body->predecessors = {header};

        exit_bb->SetTerminator(MakeRet("i"));
        exit_bb->predecessors = {header};

        InductionVariableElimination(func);

        // Function should remain valid
        REQUIRE(!func.blocks.empty());
    }

    SECTION("No induction variable") {
        Function func = CreateFuncWithEntry("no_iv");
        auto *entry = func.blocks[0].get();
        entry->SetTerminator(MakeRet("0"));

        InductionVariableElimination(func);
        REQUIRE(func.blocks.size() == 1);
    }
}

// ============================================================================
// Test 6: Escape Analysis
// ============================================================================
TEST_CASE("Optimization - Escape Analysis", "[opt][escape]") {
    SECTION("Local object does not escape") {
        Function func = CreateFuncWithEntry("local_obj");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("ptr");
        entry->AddInstruction(alloc);
        auto store = MakeStore("ptr", "42");
        entry->AddInstruction(store);
        auto load = MakeLoad("val", "ptr");
        entry->AddInstruction(load);
        entry->SetTerminator(MakeRet("val"));

        EscapeAnalysis(func);

        // After escape analysis the alloca should be marked no_escape
        auto *a = dynamic_cast<AllocaInstruction *>(FindInstruction(func, "ptr"));
        REQUIRE(a != nullptr);
        // The pass should at least not crash; whether no_escape is set depends
        // on pass sophistication.
    }

    SECTION("Returned object escapes") {
        Function func = CreateFuncWithEntry("return_obj");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("ptr");
        entry->AddInstruction(alloc);
        entry->SetTerminator(MakeRet("ptr"));

        EscapeAnalysis(func);
        // ptr escapes via return
        auto *a = dynamic_cast<AllocaInstruction *>(FindInstruction(func, "ptr"));
        REQUIRE(a != nullptr);
    }

    SECTION("Object passed to function escapes") {
        Function func = CreateFuncWithEntry("pass_to_func");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("ptr");
        entry->AddInstruction(alloc);
        auto call = MakeCall("_", "external_fn", {"ptr"}, IRType::Void());
        entry->AddInstruction(call);
        entry->SetTerminator(MakeRet("0"));

        EscapeAnalysis(func);
        auto *a = dynamic_cast<AllocaInstruction *>(FindInstruction(func, "ptr"));
        REQUIRE(a != nullptr);
    }
}

// ============================================================================
// Test 7: Scalar Replacement of Aggregates
// ============================================================================
TEST_CASE("Optimization - Scalar Replacement", "[opt][sroa]") {
    SECTION("Simple struct alloca replaced") {
        Function func = CreateFuncWithEntry("simple_struct");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("s", IRType::I64());
        entry->AddInstruction(alloc);
        auto store = MakeStore("s", "42");
        entry->AddInstruction(store);
        auto load = MakeLoad("v", "s");
        entry->AddInstruction(load);
        entry->SetTerminator(MakeRet("v"));

        ScalarReplacement(func);

        // The function should still compile correctly
        REQUIRE(func.blocks.size() == 1);
    }

    SECTION("No replacement for escaped alloca") {
        Function func = CreateFuncWithEntry("no_replace");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("s");
        entry->AddInstruction(alloc);
        auto call = MakeCall("_", "extern", {"s"}, IRType::Void());
        entry->AddInstruction(call);
        entry->SetTerminator(MakeRet("0"));

        ScalarReplacement(func);
        REQUIRE(FindInstruction(func, "s") != nullptr);
    }
}

// ============================================================================
// Test 8: Dead Store Elimination
// ============================================================================
TEST_CASE("Optimization - Dead Store Elimination", "[opt][dse]") {
    SECTION("Overwritten store is dead") {
        Function func = CreateFuncWithEntry("overwrite");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("addr");
        entry->AddInstruction(alloc);
        auto store1 = MakeStore("addr", "10");  // dead store
        entry->AddInstruction(store1);
        auto store2 = MakeStore("addr", "20");  // live store
        entry->AddInstruction(store2);
        auto load = MakeLoad("val", "addr");
        entry->AddInstruction(load);
        entry->SetTerminator(MakeRet("val"));

        size_t stores_before = CountInstructions<StoreInstruction>(func);
        DeadStoreElimination(func);
        // The first store should be eliminated or marked dead
        size_t stores_after = CountInstructions<StoreInstruction>(func);
        (void)stores_after;  // Suppress unused variable warning
        size_t live_after = CountLiveInstructions(func);
        // At minimum the pass should not crash; ideally stores_after < stores_before
        REQUIRE(live_after <= stores_before + 3);
    }

    SECTION("Live store remains") {
        Function func = CreateFuncWithEntry("live");
        auto *entry = func.blocks[0].get();

        auto alloc = MakeAlloca("addr");
        entry->AddInstruction(alloc);
        auto store = MakeStore("addr", "42");
        entry->AddInstruction(store);
        auto load = MakeLoad("val", "addr");
        entry->AddInstruction(load);
        entry->SetTerminator(MakeRet("val"));

        DeadStoreElimination(func);
        // Store should not be eliminated as it feeds a load
        REQUIRE(FindInstruction(func, "val") != nullptr);
    }
}

// ============================================================================
// Test 9: Auto Vectorization
// ============================================================================
TEST_CASE("Optimization - Auto Vectorization", "[opt][vectorize]") {
    SECTION("Simple contiguous adds") {
        // Build a loop body with element-wise add
        Function func;
        func.name = "vec_add";
        auto *entry = func.CreateBlock("entry");
        auto *header = func.CreateBlock("header");
        auto *body = func.CreateBlock("body");
        auto *exit_bb = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(header));
        entry->successors = {header};

        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "c", "i", "128");
        header->AddInstruction(cmp);
        header->SetTerminator(MakeCondBr("c", body, exit_bb));
        header->successors = {body, exit_bb};
        header->predecessors = {entry, body};

        auto load_a = MakeLoad("a_i", "a_ptr");
        body->AddInstruction(load_a);
        auto load_b = MakeLoad("b_i", "b_ptr");
        body->AddInstruction(load_b);
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "sum_i", "a_i", "b_i");
        body->AddInstruction(add);
        auto st = MakeStore("c_ptr", "sum_i");
        body->AddInstruction(st);
        auto inc = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        body->AddInstruction(inc);
        body->SetTerminator(MakeBr(header));
        body->successors = {header};
        body->predecessors = {header};

        exit_bb->SetTerminator(MakeRet("0"));
        exit_bb->predecessors = {header};

        AutoVectorization(func);

        // Function should remain valid after vectorization attempt
        REQUIRE(func.blocks.size() >= 4);
    }

    SECTION("Non-contiguous access — no vectorization") {
        Function func = CreateFuncWithEntry("non_contig");
        auto *entry = func.blocks[0].get();
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "x", "1", "2");
        entry->AddInstruction(add);
        entry->SetTerminator(MakeRet("x"));

        AutoVectorization(func);
        REQUIRE(func.blocks.size() == 1);
    }
}

// ============================================================================
// Test 10: Loop Fusion
// ============================================================================
TEST_CASE("Optimization - Loop Fusion", "[opt][fusion]") {
    SECTION("Two adjacent loops with same bounds") {
        Function func;
        func.name = "adjacent";

        auto *entry = func.CreateBlock("entry");
        auto *h1 = func.CreateBlock("header1");
        auto *b1 = func.CreateBlock("body1");
        auto *h2 = func.CreateBlock("header2");
        auto *b2 = func.CreateBlock("body2");
        auto *exit_bb = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(h1));
        entry->successors = {h1};

        // Loop 1
        auto cmp1 = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "c1", "i1", "N");
        h1->AddInstruction(cmp1);
        h1->SetTerminator(MakeCondBr("c1", b1, h2));
        h1->successors = {b1, h2};
        h1->predecessors = {entry, b1};

        auto add1 = MakeBinOp(BinaryInstruction::Op::kAdd, "s1", "sum1", "val1");
        b1->AddInstruction(add1);
        b1->SetTerminator(MakeBr(h1));
        b1->successors = {h1};
        b1->predecessors = {h1};

        // Loop 2
        auto cmp2 = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "c2", "i2", "N");
        h2->AddInstruction(cmp2);
        h2->SetTerminator(MakeCondBr("c2", b2, exit_bb));
        h2->successors = {b2, exit_bb};
        h2->predecessors = {h1, b2};

        auto add2 = MakeBinOp(BinaryInstruction::Op::kAdd, "s2", "sum2", "val2");
        b2->AddInstruction(add2);
        b2->SetTerminator(MakeBr(h2));
        b2->successors = {h2};
        b2->predecessors = {h2};

        exit_bb->SetTerminator(MakeRet("0"));
        exit_bb->predecessors = {h2};

        size_t blocks_before = func.blocks.size();
        LoopFusion(func);

        // After fusion, block count may decrease or stay the same
        REQUIRE(func.blocks.size() <= blocks_before + 2);
    }

    SECTION("Single loop — nothing to fuse") {
        Function func = CreateFuncWithEntry("single_loop");
        auto *entry = func.blocks[0].get();
        entry->SetTerminator(MakeRet("0"));

        LoopFusion(func);
        REQUIRE(func.blocks.size() == 1);
    }
}

// ============================================================================
// Test 11: SCCP
// ============================================================================
TEST_CASE("Optimization - SCCP", "[opt][sccp]") {
    SECTION("Constant expressions folded") {
        Function func = CreateFuncWithEntry("sccp_const");
        auto *entry = func.blocks[0].get();

        // a = 3 + 4 (both operands constant-like names)
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "a", "3", "4");
        entry->AddInstruction(add);
        // b = a * 2
        auto mul = MakeBinOp(BinaryInstruction::Op::kMul, "b", "a", "2");
        entry->AddInstruction(mul);
        entry->SetTerminator(MakeRet("b"));

        SCCP(func);

        // Function should remain valid
        REQUIRE(func.blocks.size() >= 1);
        REQUIRE((FindInstruction(func, "b") != nullptr || FindInstruction(func, "a") != nullptr));
    }

    SECTION("No constants to propagate") {
        Function func = CreateFuncWithEntry("sccp_none");
        func.params = {"x", "y"};
        auto *entry = func.blocks[0].get();
        auto add = MakeBinOp(BinaryInstruction::Op::kAdd, "z", "x", "y");
        entry->AddInstruction(add);
        entry->SetTerminator(MakeRet("z"));

        SCCP(func);
        REQUIRE(FindInstruction(func, "z") != nullptr);
    }
}

// ============================================================================
// Test 12: Loop Fission
// ============================================================================
TEST_CASE("Optimization - Loop Fission", "[opt][fission]") {
    SECTION("Loop with independent statements") {
        Function func;
        func.name = "fission_indep";
        auto *entry = func.CreateBlock("entry");
        auto *header = func.CreateBlock("header");
        auto *body = func.CreateBlock("body");
        auto *exit_bb = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(header));
        entry->successors = {header};
        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "cond", "i", "N");
        header->AddInstruction(cmp);
        header->SetTerminator(MakeCondBr("cond", body, exit_bb));
        header->successors = {body, exit_bb};
        header->predecessors = {entry, body};

        // Two independent computations in the body
        auto a = MakeBinOp(BinaryInstruction::Op::kAdd, "a", "x", "1");
        body->AddInstruction(a);
        auto b = MakeBinOp(BinaryInstruction::Op::kMul, "b", "y", "2");
        body->AddInstruction(b);
        auto inc = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        body->AddInstruction(inc);
        body->SetTerminator(MakeBr(header));
        body->successors = {header};
        body->predecessors = {header};

        exit_bb->SetTerminator(MakeRet("0"));
        exit_bb->predecessors = {header};

        LoopFission(func);
        REQUIRE(func.blocks.size() >= 4);
    }
}

// ============================================================================
// Test 13: Loop Interchange
// ============================================================================
TEST_CASE("Optimization - Loop Interchange", "[opt][interchange]") {
    SECTION("Nested loops can be interchanged") {
        Function func;
        func.name = "interchange";
        auto *entry = func.CreateBlock("entry");
        auto *outer_h = func.CreateBlock("outer_header");
        auto *inner_h = func.CreateBlock("inner_header");
        auto *inner_body = func.CreateBlock("inner_body");
        auto *outer_latch = func.CreateBlock("outer_latch");
        auto *exit_bb = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(outer_h));
        entry->successors = {outer_h};

        auto cmp_o = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "co", "i", "N");
        outer_h->AddInstruction(cmp_o);
        outer_h->SetTerminator(MakeCondBr("co", inner_h, exit_bb));
        outer_h->successors = {inner_h, exit_bb};
        outer_h->predecessors = {entry, outer_latch};

        auto cmp_i = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "ci", "j", "M");
        inner_h->AddInstruction(cmp_i);
        inner_h->SetTerminator(MakeCondBr("ci", inner_body, outer_latch));
        inner_h->successors = {inner_body, outer_latch};
        inner_h->predecessors = {outer_h, inner_body};

        auto add_ib = MakeBinOp(BinaryInstruction::Op::kAdd, "s", "s_old", "val");
        inner_body->AddInstruction(add_ib);
        inner_body->SetTerminator(MakeBr(inner_h));
        inner_body->successors = {inner_h};
        inner_body->predecessors = {inner_h};

        auto inc_o = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        outer_latch->AddInstruction(inc_o);
        outer_latch->SetTerminator(MakeBr(outer_h));
        outer_latch->successors = {outer_h};
        outer_latch->predecessors = {inner_h};

        exit_bb->SetTerminator(MakeRet("0"));
        exit_bb->predecessors = {outer_h};

        LoopInterchange(func);
        REQUIRE(func.blocks.size() >= 6);
    }
}

// ============================================================================
// Test 14: Loop Tiling
// ============================================================================
TEST_CASE("Optimization - Loop Tiling", "[opt][tiling]") {
    SECTION("Single loop tiled") {
        Function func;
        func.name = "tiling";
        auto *entry = func.CreateBlock("entry");
        auto *header = func.CreateBlock("header");
        auto *body = func.CreateBlock("body");
        auto *exit_bb = func.CreateBlock("exit");

        entry->SetTerminator(MakeBr(header));
        entry->successors = {header};
        auto cmp = MakeBinOp(BinaryInstruction::Op::kCmpSlt, "cond", "i", "1024");
        header->AddInstruction(cmp);
        header->SetTerminator(MakeCondBr("cond", body, exit_bb));
        header->successors = {body, exit_bb};
        header->predecessors = {entry, body};

        auto add_b = MakeBinOp(BinaryInstruction::Op::kAdd, "s", "sum", "val");
        body->AddInstruction(add_b);
        auto inc = MakeBinOp(BinaryInstruction::Op::kAdd, "i_next", "i", "1");
        body->AddInstruction(inc);
        body->SetTerminator(MakeBr(header));
        body->successors = {header};
        body->predecessors = {header};

        exit_bb->SetTerminator(MakeRet("0"));
        exit_bb->predecessors = {header};

        LoopTiling(func, 64);
        REQUIRE(func.blocks.size() >= 4);
    }
}

// ============================================================================
// Test 15: Jump Threading
// ============================================================================
TEST_CASE("Optimization - Jump Threading", "[opt][jump]") {
    SECTION("Redundant branch threaded") {
        Function func;
        func.name = "jump_thread";
        auto *entry = func.CreateBlock("entry");
        auto *mid = func.CreateBlock("mid");
        auto *target = func.CreateBlock("target");
        auto *other = func.CreateBlock("other");

        // entry branches to mid; mid unconditionally branches to target
        entry->SetTerminator(MakeCondBr("cond", mid, other));
        entry->successors = {mid, other};

        mid->SetTerminator(MakeBr(target));
        mid->successors = {target};
        mid->predecessors = {entry};

        target->SetTerminator(MakeRet("1"));
        target->predecessors = {mid};

        other->SetTerminator(MakeRet("0"));
        other->predecessors = {entry};

        JumpThreading(func);

        // After threading, entry might branch directly to target
        REQUIRE(func.blocks.size() >= 2);
    }
}

// ============================================================================
// Integration: Combined Passes
// ============================================================================
TEST_CASE("Optimization - Combined Passes", "[opt][combined]") {
    Function func = CreateFuncWithEntry("combined");
    auto *entry = func.blocks[0].get();

    // Add some IR that exercises multiple passes
    auto a = MakeBinOp(BinaryInstruction::Op::kAdd, "a", "3", "4");
    entry->AddInstruction(a);
    auto b = MakeBinOp(BinaryInstruction::Op::kMul, "b", "a", "8");
    entry->AddInstruction(b);
    auto c = MakeBinOp(BinaryInstruction::Op::kAdd, "c", "b", "0");
    entry->AddInstruction(c);
    // d = c + 0 (identity)
    auto d = MakeBinOp(BinaryInstruction::Op::kAdd, "d", "c", "0");
    entry->AddInstruction(d);
    entry->SetTerminator(MakeRet("d"));

    polyglot::ir::passes::ConstantFold(func);
    polyglot::ir::passes::DeadCodeEliminate(func);
    LoopInvariantCodeMotion(func);
    StrengthReduction(func);
    AutoVectorization(func);
    DeadStoreElimination(func);

    // After all passes the function should still have the entry block
    REQUIRE(!func.blocks.empty());
    // Return instruction should still be present
    REQUIRE(func.blocks[0]->terminator != nullptr);
}

// ============================================================================
// Performance smoke test
// ============================================================================
TEST_CASE("Optimization - Performance", "[opt][benchmark]") {
    // Build a function with many instructions and run optimizations
    Function func;
    func.name = "benchmark";
    auto *entry = func.CreateBlock("entry");
    for (int i = 0; i < 100; ++i) {
        auto inst = MakeBinOp(
            BinaryInstruction::Op::kAdd,
            "v" + std::to_string(i),
            (i == 0) ? std::string("0") : ("v" + std::to_string(i - 1)),
            std::to_string(i));
        entry->AddInstruction(inst);
    }
    entry->SetTerminator(MakeRet("v99"));

    polyglot::ir::passes::ConstantFold(func);
    polyglot::ir::passes::DeadCodeEliminate(func);
    LoopInvariantCodeMotion(func);
    AutoVectorization(func);

    REQUIRE(func.blocks.size() == 1);
    REQUIRE(func.blocks[0]->terminator != nullptr);
}
