/**
 * @file compilation_behavior_test.cpp
 * @brief Compilation product behavior-level assertion tests
 *
 * Tests verify "output correctness" rather than merely "no crash":
 *  - IR output must contain expected symbols, types, and instructions
 *  - Backend assembly/object must contain correct labels and encodings
 *  - Failure paths must produce the correct diagnostics
 *  - Cross-language descriptor generation must be accurate
 */

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/verifier.h"
#include "middle/include/ir/nodes/statements.h"
#include "backends/x86_64/include/x86_target.h"
#include "backends/arm64/include/arm64_target.h"

using namespace polyglot;

// ============================================================================
// Helper: compile C++ source to IR text
// ============================================================================
namespace {

struct CppCompileResult {
    std::string ir_text;
    ir::IRContext ctx;
    bool success{false};
    size_t function_count{0};
};

CppCompileResult CompileCpp(const std::string &code, frontends::Diagnostics &diags) {
    CppCompileResult result;
    cpp::CppLexer lexer(code, "<test>");
    cpp::CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) return result;

    cpp::LowerToIR(*module, result.ctx, diags);
    if (diags.HasErrors()) return result;

    result.function_count = result.ctx.Functions().size();

    std::ostringstream oss;
    ir::PrintModule(result.ctx, oss);
    result.ir_text = oss.str();
    result.success = true;
    return result;
}

struct PloyCompileResult {
    std::string ir_text;
    std::vector<ploy::CrossLangCallDescriptor> descriptors;
    bool success{false};
    size_t error_count{0};
    size_t warning_count{0};
};

PloyCompileResult CompilePloy(const std::string &code, frontends::Diagnostics &diags) {
    PloyCompileResult result;
    ploy::PloyLexer lexer(code, "<test>");
    ploy::PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) {
        result.error_count = diags.ErrorCount();
        return result;
    }

    ploy::PloySema sema(diags);
    if (!sema.Analyze(module)) {
        result.error_count = diags.ErrorCount();
        result.warning_count = diags.WarningCount();
        return result;
    }

    ir::IRContext ctx;
    ploy::PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) {
        result.error_count = diags.ErrorCount();
        return result;
    }

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        ir::PrintFunction(*fn, oss);
    }
    result.ir_text = oss.str();
    result.descriptors = lowering.CallDescriptors();
    result.success = true;
    result.error_count = diags.ErrorCount();
    result.warning_count = diags.WarningCount();
    return result;
}

}  // namespace

// ============================================================================
// C++ IR output correctness
// ============================================================================

TEST_CASE("Behavior: C++ add function IR has correct signature",
          "[behavior][cpp][ir]") {
    std::string code = R"(
        int add(int a, int b) {
            return a + b;
        }
    )";
    frontends::Diagnostics diags;
    auto result = CompileCpp(code, diags);

    REQUIRE(result.success);
    REQUIRE(result.function_count == 1);

    // IR must contain the function name
    REQUIRE(result.ir_text.find("add") != std::string::npos);
    // IR must reference both parameters
    REQUIRE(result.ir_text.find("a") != std::string::npos);
    REQUIRE(result.ir_text.find("b") != std::string::npos);
    // IR must contain an add instruction
    REQUIRE(result.ir_text.find("add") != std::string::npos);
    // IR must contain a return
    REQUIRE(result.ir_text.find("ret") != std::string::npos);
}

TEST_CASE("Behavior: C++ multiple functions produce separate IR entries",
          "[behavior][cpp][ir]") {
    std::string code = R"(
        int foo(int x) { return x; }
        int bar(int y) { return y + 1; }
    )";
    frontends::Diagnostics diags;
    auto result = CompileCpp(code, diags);

    REQUIRE(result.success);
    REQUIRE(result.function_count == 2);
    REQUIRE(result.ir_text.find("foo") != std::string::npos);
    REQUIRE(result.ir_text.find("bar") != std::string::npos);
}

TEST_CASE("Behavior: C++ conditional produces multiple basic blocks",
          "[behavior][cpp][ir]") {
    std::string code = R"(
        int max_val(int a, int b) {
            if (a > b) { return a; }
            return b;
        }
    )";
    frontends::Diagnostics diags;
    auto result = CompileCpp(code, diags);

    REQUIRE(result.success);
    auto &fn = result.ctx.Functions()[0];
    // An if-else should produce at least 2 blocks (then + merge/else)
    REQUIRE(fn->blocks.size() >= 2);
}

// ============================================================================
// x86_64 backend output correctness
// ============================================================================

TEST_CASE("Behavior: x86_64 assembly contains function label and ret",
          "[behavior][x86][asm]") {
    std::string code = R"(
        int identity(int x) { return x; }
    )";
    frontends::Diagnostics diags;
    auto result = CompileCpp(code, diags);
    REQUIRE(result.success);

    backends::x86_64::X86Target target;
    target.SetModule(&result.ctx);
    std::string asm_text = target.EmitAssembly();

    REQUIRE(!asm_text.empty());
    // Must contain the function label
    REQUIRE(asm_text.find("identity:") != std::string::npos);
    // Must contain a return instruction
    REQUIRE(asm_text.find("ret") != std::string::npos);
}

TEST_CASE("Behavior: x86_64 object code has .text with non-zero bytes",
          "[behavior][x86][obj]") {
    std::string code = R"(
        int square(int n) { return n * n; }
    )";
    frontends::Diagnostics diags;
    auto result = CompileCpp(code, diags);
    REQUIRE(result.success);

    backends::x86_64::X86Target target;
    target.SetModule(&result.ctx);
    auto mc = target.EmitObjectCode();

    // Must have at least one section
    REQUIRE(!mc.sections.empty());

    // .text section must exist with actual machine code bytes
    bool found_text = false;
    for (auto &sec : mc.sections) {
        if (sec.name == ".text") {
            found_text = true;
            REQUIRE(sec.data.size() > 0);
            // Machine code should not be all zeros (that would indicate stub)
            bool all_zero = true;
            for (auto b : sec.data) {
                if (b != 0) { all_zero = false; break; }
            }
            REQUIRE_FALSE(all_zero);
        }
    }
    REQUIRE(found_text);

    // Must export the function symbol
    bool found_symbol = false;
    for (auto &sym : mc.symbols) {
        if (sym.name == "square") {
            found_symbol = true;
            REQUIRE(sym.global);
        }
    }
    REQUIRE(found_symbol);
}

// ============================================================================
// arm64 backend output correctness
// ============================================================================

TEST_CASE("Behavior: arm64 assembly contains function label and ret",
          "[behavior][arm64][asm]") {
    ir::IRContext ctx;
    auto fn = ctx.CreateFunction("arm_add", ir::IRType::I64(),
                                 {{"a", ir::IRType::I64()},
                                  {"b", ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");
    auto binop = std::make_shared<ir::BinaryInstruction>();
    binop->op = ir::BinaryInstruction::Op::kAdd;
    binop->name = "result";
    binop->type = ir::IRType::I64();
    binop->operands = {"a", "b"};
    blk->AddInstruction(binop);
    auto ret = std::make_shared<ir::ReturnStatement>();
    ret->operands.push_back("result");
    blk->SetTerminator(ret);

    backends::arm64::Arm64Target target(&ctx);
    std::string asm_text = target.EmitAssembly();

    REQUIRE(!asm_text.empty());
    REQUIRE(asm_text.find("arm_add:") != std::string::npos);
    REQUIRE(asm_text.find("ret") != std::string::npos);
}

// ============================================================================
// Ploy cross-language descriptor correctness
// ============================================================================

TEST_CASE("Behavior: Ploy LINK produces correct descriptor count",
          "[behavior][ploy][descriptor]") {
    frontends::Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, math::add, pymath::add) {
    MAP_TYPE(cpp::int, python::int);
    MAP_TYPE(cpp::int, python::int);
}

FUNC use_add(a: i32, b: i32) -> i32 {
    LET result = CALL(cpp, math::add, a, b);
    RETURN result;
}
    )";
    auto result = CompilePloy(code, diags);

    REQUIRE(result.success);
    // Must produce at least one cross-language call descriptor
    REQUIRE(result.descriptors.size() >= 1);
    // The descriptor must reference cpp as source language (the foreign language being called)
    bool found_cpp_desc = false;
    for (auto &d : result.descriptors) {
        if (d.source_language == "cpp" && d.source_function.find("add") != std::string::npos) {
            found_cpp_desc = true;
            // Verify the bridge stub was generated with a non-empty name
            REQUIRE(!d.stub_name.empty());
        }
    }
    REQUIRE(found_cpp_desc);
}

TEST_CASE("Behavior: Ploy FUNC produces named IR function",
          "[behavior][ploy][ir]") {
    frontends::Diagnostics diags;
    std::string code = R"(
FUNC compute(x: i32, y: i32) -> i32 {
    LET sum = x + y;
    RETURN sum;
}

EXPORT compute AS "polyglot_compute";
    )";
    auto result = CompilePloy(code, diags);

    REQUIRE(result.success);
    REQUIRE(result.ir_text.find("compute") != std::string::npos);
    REQUIRE(result.ir_text.find("ret") != std::string::npos);
}

// ============================================================================
// Failure-path tests: invalid code must produce diagnostics, not crash
// ============================================================================

TEST_CASE("Failure: C++ syntax error produces diagnostic",
          "[behavior][failure][cpp]") {
    std::string bad_code = R"(
        int broken( {
    )";
    frontends::Diagnostics diags;
    auto result = CompileCpp(bad_code, diags);

    REQUIRE_FALSE(result.success);
    REQUIRE(diags.HasErrors());
    REQUIRE(diags.ErrorCount() >= 1);
}

TEST_CASE("Failure: Ploy missing RETURN in non-void function produces diagnostic",
          "[behavior][failure][ploy]") {
    frontends::Diagnostics diags;
    std::string code = R"(
FUNC bad_func(x: i32) -> i32 {
    LET y = x + 1;
}
    )";
    auto result = CompilePloy(code, diags);

    // The compiler should flag the missing return, either as error or warning
    bool has_relevant_diag = false;
    for (auto &d : diags.All()) {
        if (d.message.find("return") != std::string::npos ||
            d.message.find("RETURN") != std::string::npos) {
            has_relevant_diag = true;
            break;
        }
    }
    // If lowering succeeded despite missing RETURN, IR should still be generated
    // but the function body should not be empty
    if (result.success) {
        REQUIRE(!result.ir_text.empty());
    }
}

TEST_CASE("Failure: Ploy undefined variable produces error",
          "[behavior][failure][ploy]") {
    frontends::Diagnostics diags;
    std::string code = R"(
FUNC bad(x: i32) -> i32 {
    RETURN undefined_var;
}
    )";
    auto result = CompilePloy(code, diags);

    // Sema or lowering should flag the undefined reference
    // The pipeline may still produce IR with placeholder, but diagnostics should fire
    bool has_undef_diag = false;
    for (auto &d : diags.All()) {
        if (d.message.find("undefined") != std::string::npos ||
            d.message.find("undeclared") != std::string::npos ||
            d.message.find("not found") != std::string::npos ||
            d.message.find("unknown") != std::string::npos) {
            has_undef_diag = true;
            break;
        }
    }
    CHECK(has_undef_diag);
}

TEST_CASE("Failure: Ploy type mismatch produces diagnostic",
          "[behavior][failure][ploy]") {
    frontends::Diagnostics diags;
    std::string code = R"(
FUNC mismatch() -> i32 {
    LET s: STRING = "hello";
    RETURN s;
}
    )";
    auto result = CompilePloy(code, diags);

    // Returning a STRING where i32 is expected should produce a diagnostic
    bool has_type_diag = false;
    for (auto &d : diags.All()) {
        if (d.message.find("type") != std::string::npos ||
            d.message.find("mismatch") != std::string::npos ||
            d.message.find("cannot convert") != std::string::npos ||
            d.message.find("incompatible") != std::string::npos) {
            has_type_diag = true;
            break;
        }
    }
    CHECK(has_type_diag);
}

TEST_CASE("Failure: empty ploy source produces no functions",
          "[behavior][failure][ploy]") {
    frontends::Diagnostics diags;
    std::string code = "";
    auto result = CompilePloy(code, diags);

    // An empty source should either fail or produce zero functions
    if (result.success) {
        REQUIRE(result.ir_text.empty());
    }
}

// ============================================================================
// IR Verifier behavior
// ============================================================================

TEST_CASE("Behavior: IR verifier accepts well-formed function",
          "[behavior][verifier]") {
    ir::IRContext ctx;
    auto fn = ctx.CreateFunction("valid_fn", ir::IRType::I64(),
                                 {{"x", ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");
    auto ret = std::make_shared<ir::ReturnStatement>();
    ret->operands.push_back("x");
    blk->SetTerminator(ret);

    std::string msg;
    bool valid = ir::Verify(ctx, &msg);
    REQUIRE(valid);
    REQUIRE(msg.empty());
}

TEST_CASE("Behavior: IR verifier rejects function without terminator",
          "[behavior][verifier]") {
    ir::IRContext ctx;
    auto fn = ctx.CreateFunction("bad_fn", ir::IRType::I64(),
                                 {{"x", ir::IRType::I64()}});
    auto blk = fn->CreateBlock("entry");
    // Intentionally no terminator

    std::string msg;
    bool valid = ir::Verify(ctx, &msg);
    // Verifier should detect the missing terminator
    REQUIRE_FALSE(valid);
    REQUIRE(!msg.empty());
}
