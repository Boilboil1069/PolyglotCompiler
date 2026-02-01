#include <catch2/catch_test_macros.hpp>
#include "middle/include/passes/transform/gvn.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/nodes/statements.h"

using namespace polyglot::passes::transform;
using namespace polyglot::ir;

TEST_CASE("GVN - Basic Redundancy Elimination", "[gvn]") {
    Function func;
    func.name = "test_gvn";
    
    auto bb = std::make_shared<BasicBlock>();
    bb->name = "entry";
    
    // Create redundant computations
    // %1 = add %a, %b
    auto add1 = std::make_shared<BinaryInstruction>();
    add1->op = BinaryInstruction::Op::kAdd;
    add1->name = "%1";
    add1->operands = {"%a", "%b"};
    bb->instructions.push_back(add1);
    
    // %2 = mul %c, %d
    auto mul = std::make_shared<BinaryInstruction>();
    mul->op = BinaryInstruction::Op::kMul;
    mul->name = "%2";
    mul->operands = {"%c", "%d"};
    bb->instructions.push_back(mul);
    
    // %3 = add %a, %b  (redundant!)
    auto add2 = std::make_shared<BinaryInstruction>();
    add2->op = BinaryInstruction::Op::kAdd;
    add2->name = "%3";
    add2->operands = {"%a", "%b"};
    bb->instructions.push_back(add2);
    
    func.blocks.push_back(bb);
    func.entry = bb.get();
    
    GVNPass pass(func);
    bool changed = pass.Run();
    
    // Should eliminate the redundant add
    REQUIRE(changed == true);
    
    // Verify that %3 is replaced with %1
    // (in real test, check that %3 no longer exists)
}

TEST_CASE("GVN - Commutative Operations", "[gvn]") {
    Function func;
    func.name = "test_gvn_comm";
    
    auto bb = std::make_shared<BasicBlock>();
    bb->name = "entry";
    
    // %1 = add %a, %b
    auto add1 = std::make_shared<BinaryInstruction>();
    add1->op = BinaryInstruction::Op::kAdd;
    add1->name = "%1";
    add1->operands = {"%a", "%b"};
    bb->instructions.push_back(add1);
    
    // %2 = add %b, %a  (should be recognized as same)
    auto add2 = std::make_shared<BinaryInstruction>();
    add2->op = BinaryInstruction::Op::kAdd;
    add2->name = "%2";
    add2->operands = {"%b", "%a"};
    bb->instructions.push_back(add2);
    
    func.blocks.push_back(bb);
    func.entry = bb.get();
    
    GVNPass pass(func);
    bool changed = pass.Run();
    
    // Should recognize commutative equivalence
    REQUIRE(changed == true);
}

TEST_CASE("GVN - Non-Pure Instructions", "[gvn]") {
    Function func;
    func.name = "test_gvn_nonpure";
    
    auto bb = std::make_shared<BasicBlock>();
    bb->name = "entry";
    
    // Store should not be eliminated
    auto store1 = std::make_shared<StoreInstruction>();
    store1->operands = {"%val", "%ptr"};
    bb->instructions.push_back(store1);
    
    auto store2 = std::make_shared<StoreInstruction>();
    store2->operands = {"%val", "%ptr"};
    bb->instructions.push_back(store2);
    
    func.blocks.push_back(bb);
    func.entry = bb.get();
    
    GVNPass pass(func);
    bool changed = pass.Run();
    
    // Should not eliminate stores (they have side effects)
    REQUIRE(changed == false);
}

TEST_CASE("Alias Analysis - No Alias", "[alias-analysis]") {
    Function func;
    func.name = "test_alias";
    
    auto bb = std::make_shared<BasicBlock>();
    bb->name = "entry";
    
    // Create two allocas
    auto alloca1 = std::make_shared<AllocaInstruction>();
    alloca1->name = "%ptr1";
    bb->instructions.push_back(alloca1);
    
    auto alloca2 = std::make_shared<AllocaInstruction>();
    alloca2->name = "%ptr2";
    bb->instructions.push_back(alloca2);
    
    func.blocks.push_back(bb);
    func.entry = bb.get();
    
    AliasAnalysisPass pass(func);
    auto result = pass.Query("%ptr1", "%ptr2");
    
    // Different allocas should not alias
    REQUIRE(result == AliasAnalysisPass::AliasResult::kNoAlias);
}
