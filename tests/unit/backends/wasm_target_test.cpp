/**
 * @file     wasm_target_test.cpp
 * @brief    Standalone unit tests for the WebAssembly backend
 *
 * Tests construct minimal IRContext modules and exercise the WasmTarget
 * code-generation API:
 *   1. EmitAssembly (WAT text) for a single add function.
 *   2. EmitAssembly for a multi-function module.
 *   3. EmitWasmBinary produces valid WASM magic bytes.
 *   4. Parameter types map correctly (i32, i64, f64).
 *   5. Void-return functions omit the (result ...) clause.
 *   6. Control flow (if/while) produces block annotations.
 *   7. Empty module produces minimal "(module)" text.
 *   8. Multiple exports appear in WAT output.
 *
 * @ingroup  Tests / Backends / WASM
 * @author   Manning Cyrus
 * @date     2026-04-11
 */

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backends/wasm/include/wasm_target.h"
#include "middle/include/ir/cfg.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"

using namespace polyglot::backends::wasm;
using namespace polyglot::ir;

// ============================================================================
// Helpers — build minimal IR modules
// ============================================================================

namespace {

// Create an IRContext with a single function: add(i64, i64) -> i64
// Body: entry block with a binary add and return.
IRContext MakeAddModule() {
    IRContext ctx;
    auto fn = ctx.CreateFunction("add", IRType::I64(),
                                 {{"a", IRType::I64()}, {"b", IRType::I64()}});
    auto *entry = fn->CreateBlock("entry");

    auto bin = std::make_shared<BinaryInstruction>();
    bin->op   = BinaryInstruction::Op::kAdd;
    bin->name = "sum";
    bin->type = IRType::I64();
    bin->operands = {"a", "b"};
    entry->AddInstruction(bin);

    auto ret = std::make_shared<ReturnStatement>();
    ret->operands = {"sum"};
    entry->SetTerminator(ret);

    return ctx;
}

// Create an IRContext with two functions: inc(i32) -> i32, dec(i32) -> i32
IRContext MakeTwoFuncModule() {
    IRContext ctx;
    {
        auto fn = ctx.CreateFunction("inc", IRType::I32(),
                                     {{"x", IRType::I32()}});
        auto *entry = fn->CreateBlock("entry");

        auto bin = std::make_shared<BinaryInstruction>();
        bin->op   = BinaryInstruction::Op::kAdd;
        bin->name = "r";
        bin->type = IRType::I32();
        bin->operands = {"x", "one"};
        entry->AddInstruction(bin);

        auto ret = std::make_shared<ReturnStatement>();
        ret->operands = {"r"};
        entry->SetTerminator(ret);
    }
    {
        auto fn = ctx.CreateFunction("dec", IRType::I32(),
                                     {{"x", IRType::I32()}});
        auto *entry = fn->CreateBlock("entry");

        auto bin = std::make_shared<BinaryInstruction>();
        bin->op   = BinaryInstruction::Op::kSub;
        bin->name = "r";
        bin->type = IRType::I32();
        bin->operands = {"x", "one"};
        entry->AddInstruction(bin);

        auto ret = std::make_shared<ReturnStatement>();
        ret->operands = {"r"};
        entry->SetTerminator(ret);
    }
    return ctx;
}

// Create an IRContext with a void-return function: noop() -> void
IRContext MakeVoidModule() {
    IRContext ctx;
    auto fn = ctx.CreateFunction("noop", IRType::Void(), {});
    auto *entry = fn->CreateBlock("entry");
    auto ret = std::make_shared<ReturnStatement>();
    entry->SetTerminator(ret);
    return ctx;
}

// Create an IRContext with mixed param types: mixed(i32, i64, f64) -> f64
IRContext MakeMixedTypesModule() {
    IRContext ctx;
    auto fn = ctx.CreateFunction("mixed", IRType::F64(),
                                 {{"a", IRType::I32()},
                                  {"b", IRType::I64()},
                                  {"c", IRType::F64()}});
    auto *entry = fn->CreateBlock("entry");
    auto ret = std::make_shared<ReturnStatement>();
    ret->operands = {"c"};
    entry->SetTerminator(ret);
    return ctx;
}

// Create an IRContext with a function that has multiple basic blocks
// (simulating control flow with if/else pattern).
IRContext MakeControlFlowModule() {
    IRContext ctx;
    auto fn = ctx.CreateFunction("branch_fn", IRType::I64(),
                                 {{"cond", IRType::I64()}, {"x", IRType::I64()}});
    auto *entry  = fn->CreateBlock("entry");
    auto *if_blk = fn->CreateBlock("if_true");
    auto *else_blk = fn->CreateBlock("if_false");

    // entry: just a return (simplified; we only care about block annotations)
    auto ret0 = std::make_shared<ReturnStatement>();
    ret0->operands = {"cond"};
    entry->SetTerminator(ret0);

    // if_true: return x + 1
    auto bin = std::make_shared<BinaryInstruction>();
    bin->op   = BinaryInstruction::Op::kAdd;
    bin->name = "inc";
    bin->type = IRType::I64();
    bin->operands = {"x", "one"};
    if_blk->AddInstruction(bin);
    auto ret1 = std::make_shared<ReturnStatement>();
    ret1->operands = {"inc"};
    if_blk->SetTerminator(ret1);

    // if_false: return x - 1
    auto bin2 = std::make_shared<BinaryInstruction>();
    bin2->op   = BinaryInstruction::Op::kSub;
    bin2->name = "dec_val";
    bin2->type = IRType::I64();
    bin2->operands = {"x", "one"};
    else_blk->AddInstruction(bin2);
    auto ret2 = std::make_shared<ReturnStatement>();
    ret2->operands = {"dec_val"};
    else_blk->SetTerminator(ret2);

    return ctx;
}

} // namespace

// ============================================================================
// 1. Single add function — WAT contains (func and (export
// ============================================================================

TEST_CASE("WASM: EmitAssembly for add function contains (func and (export",
          "[wasm][backend][assembly]") {
    IRContext ctx = MakeAddModule();
    WasmTarget target(&ctx);

    std::string wat = target.EmitAssembly();
    REQUIRE_FALSE(wat.empty());

    CHECK(wat.find("(module") != std::string::npos);
    CHECK(wat.find("(func $add") != std::string::npos);
    CHECK(wat.find("(export \"add\")") != std::string::npos);
    CHECK(wat.find("(param $a i64)") != std::string::npos);
    CHECK(wat.find("(param $b i64)") != std::string::npos);
    CHECK(wat.find("(result i64)") != std::string::npos);
}

// ============================================================================
// 2. Multi-function module — both functions appear in WAT
// ============================================================================

TEST_CASE("WASM: EmitAssembly for two-function module lists both exports",
          "[wasm][backend][assembly]") {
    IRContext ctx = MakeTwoFuncModule();
    WasmTarget target(&ctx);

    std::string wat = target.EmitAssembly();
    CHECK(wat.find("(func $inc") != std::string::npos);
    CHECK(wat.find("(func $dec") != std::string::npos);
    CHECK(wat.find("(export \"inc\")") != std::string::npos);
    CHECK(wat.find("(export \"dec\")") != std::string::npos);
}

// ============================================================================
// 3. EmitWasmBinary — output starts with WASM magic number
// ============================================================================

TEST_CASE("WASM: EmitWasmBinary produces valid WASM magic header",
          "[wasm][backend][binary]") {
    IRContext ctx = MakeAddModule();
    WasmTarget target(&ctx);

    std::vector<std::uint8_t> binary = target.EmitWasmBinary();
    REQUIRE(binary.size() >= 8);

    // WASM magic: \0asm
    CHECK(binary[0] == 0x00);
    CHECK(binary[1] == 0x61);
    CHECK(binary[2] == 0x73);
    CHECK(binary[3] == 0x6D);

    // WASM version: 1
    CHECK(binary[4] == 0x01);
    CHECK(binary[5] == 0x00);
    CHECK(binary[6] == 0x00);
    CHECK(binary[7] == 0x00);
}

// ============================================================================
// 4. Mixed parameter types map to correct WAT type names
// ============================================================================

TEST_CASE("WASM: EmitAssembly maps i32, i64, f64 param types correctly",
          "[wasm][backend][types]") {
    IRContext ctx = MakeMixedTypesModule();
    WasmTarget target(&ctx);

    std::string wat = target.EmitAssembly();
    CHECK(wat.find("(param $a i32)") != std::string::npos);
    CHECK(wat.find("(param $b i64)") != std::string::npos);
    CHECK(wat.find("(param $c f64)") != std::string::npos);
    CHECK(wat.find("(result f64)") != std::string::npos);
}

// ============================================================================
// 5. Void-return function omits (result ...) in WAT
// ============================================================================

TEST_CASE("WASM: void-return function has no (result ...) clause",
          "[wasm][backend][void]") {
    IRContext ctx = MakeVoidModule();
    WasmTarget target(&ctx);

    std::string wat = target.EmitAssembly();
    CHECK(wat.find("(func $noop") != std::string::npos);
    CHECK(wat.find("(export \"noop\")") != std::string::npos);
    // There should be no (result ...) for void functions
    // Find the func line and check it does not contain "result"
    auto pos = wat.find("(func $noop");
    REQUIRE(pos != std::string::npos);
    auto end_of_func_header = wat.find('\n', pos);
    std::string header_line = wat.substr(pos, end_of_func_header - pos);
    CHECK(header_line.find("result") == std::string::npos);
}

// ============================================================================
// 6. Control flow — multiple basic blocks produce block annotations
// ============================================================================

TEST_CASE("WASM: multi-block function emits block annotations in WAT",
          "[wasm][backend][control-flow]") {
    IRContext ctx = MakeControlFlowModule();
    WasmTarget target(&ctx);

    std::string wat = target.EmitAssembly();
    CHECK(wat.find("(func $branch_fn") != std::string::npos);
    // Each basic block should produce a ";; block" comment
    CHECK(wat.find(";; block entry") != std::string::npos);
    CHECK(wat.find(";; block if_true") != std::string::npos);
    CHECK(wat.find(";; block if_false") != std::string::npos);
}

// ============================================================================
// 7. Empty (null) module — produces minimal output
// ============================================================================

TEST_CASE("WASM: null module emits minimal (module) text",
          "[wasm][backend][empty]") {
    WasmTarget target;
    std::string wat = target.EmitAssembly();
    CHECK(wat.find("(module)") != std::string::npos);
}

// ============================================================================
// 8. Target triple reports wasm32
// ============================================================================

TEST_CASE("WASM: TargetTriple returns wasm32-unknown-unknown",
          "[wasm][backend][triple]") {
    WasmTarget target;
    CHECK(target.TargetTriple() == "wasm32-unknown-unknown");
}
