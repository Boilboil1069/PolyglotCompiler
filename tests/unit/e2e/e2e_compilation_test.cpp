#include <catch2/catch_test_macros.hpp>
#include <string>
#include <sstream>

// TODO: These headers need to be implemented
// #include "frontends/cpp/include/cpp_lexer.h"
// #include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
// #include "middle/include/ir/verifier.h"
// #include "middle/include/ir/ssa.h"
#include "common/include/ir/ir_printer.h"
#include "backends/x86_64/include/x86_target.h"
#include "backends/arm64/include/arm64_target.h"

using namespace polyglot;

// Temporarily disabled until Lexer/Parser/Verifier/SSABuilder are implemented
#if 0
TEST_CASE("End-to-end: Simple function compilation", "[e2e]") {
    std::string code = R"(
        int add(int a, int b) {
            return a + b;
        }
    )";

    // Step 1: Parse
    frontends::Diagnostics diags;
    cpp::Lexer lexer(code, diags);
    auto tokens = lexer.Tokenize();
    
    cpp::Parser parser(tokens, diags);
    auto module = parser.Parse();
    
    REQUIRE(!diags.HasErrors());
    REQUIRE(module != nullptr);
    
    // Step 2: Lower to IR
    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);
    
    REQUIRE(!diags.HasErrors());
    auto &functions = ctx.Functions();
    REQUIRE(functions.size() >= 1);
    
    // Step 3: Verify IR
    ir::IRVerifier verifier;
    for (const auto &fn : functions) {
        REQUIRE(verifier.Verify(*fn));
    }
    
    // Step 4: SSA conversion
    for (auto &fn : functions) {
        ir::SSABuilder ssa_builder;
        ssa_builder.BuildSSA(*fn);
    }
    
    // Step 5: Print IR (for debugging)
    std::ostringstream ir_out;
    ir::IRPrinter printer(ir_out);
    for (const auto &fn : functions) {
        printer.Print(*fn);
    }
    
    std::string ir_text = ir_out.str();
    INFO("Generated IR:\n" << ir_text);
    REQUIRE(!ir_text.empty());
}

TEST_CASE("End-to-end: Control flow compilation", "[e2e]") {
    std::string code = R"(
        int factorial(int n) {
            if (n <= 1) {
                return 1;
            }
            return n * factorial(n - 1);
        }
    )";

    frontends::Diagnostics diags;
    cpp::Lexer lexer(code, diags);
    auto tokens = lexer.Tokenize();
    
    cpp::Parser parser(tokens, diags);
    auto module = parser.Parse();
    
    REQUIRE(!diags.HasErrors());
    
    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);
    
    REQUIRE(!diags.HasErrors());
    
    auto &functions = ctx.Functions();
    REQUIRE(!functions.empty());
    
    // Verify control flow was created
    auto &fn = functions[0];
    REQUIRE(fn->blocks.size() > 1);  // Should have multiple blocks for if/else
    
    // Verify IR
    ir::IRVerifier verifier;
    REQUIRE(verifier.Verify(*fn));
}

TEST_CASE("End-to-end: Assembly generation x86_64", "[e2e][backend]") {
    std::string code = R"(
        int simple(int x) {
            return x + 42;
        }
    )";

    frontends::Diagnostics diags;
    cpp::Lexer lexer(code, diags);
    auto tokens = lexer.Tokenize();
    
    cpp::Parser parser(tokens, diags);
    auto module = parser.Parse();
    
    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);
    
    REQUIRE(!diags.HasErrors());
    
    // Create backend target
    backends::x86_64::X86Target target;
    target.SetModule(&ctx);
    target.SetRegAllocStrategy(backends::x86_64::RegAllocStrategy::kLinearScan);
    
    // Generate assembly
    std::string asm_code = target.EmitAssembly();
    
    INFO("Generated Assembly:\n" << asm_code);
    REQUIRE(!asm_code.empty());
    REQUIRE(asm_code.find("simple:") != std::string::npos);
    REQUIRE(asm_code.find("ret") != std::string::npos);
}

TEST_CASE("End-to-end: Object code generation", "[e2e][backend][codegen]") {
    std::string code = R"(
        int add(int a, int b) {
            return a + b;
        }
    )";

    frontends::Diagnostics diags;
    cpp::Lexer lexer(code, diags);
    auto tokens = lexer.Tokenize();
    
    cpp::Parser parser(tokens, diags);
    auto module = parser.Parse();
    
    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);
    
    backends::x86_64::X86Target target;
    target.SetModule(&ctx);
    
    // Generate object code
    auto obj_result = target.EmitObjectCode();
    
    REQUIRE(!obj_result.sections.empty());
    
    // Verify we have a text section with code
    bool has_text = false;
    for (const auto &sec : obj_result.sections) {
        if (sec.name == ".text" && !sec.data.empty()) {
            has_text = true;
            break;
        }
    }
    REQUIRE(has_text);
    
    // Verify we have symbols
    REQUIRE(!obj_result.symbols.empty());
}

TEST_CASE("End-to-end: Loop compilation", "[e2e]") {
    std::string code = R"(
        int sum_to_n(int n) {
            int sum = 0;
            for (int i = 1; i <= n; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }
    )";

    frontends::Diagnostics diags;
    cpp::Lexer lexer(code, diags);
    auto tokens = lexer.Tokenize();
    
    cpp::Parser parser(tokens, diags);
    auto module = parser.Parse();
    
    REQUIRE(!diags.HasErrors());
    
    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);
    
    if (diags.HasErrors()) {
        INFO("Errors during lowering:");
        for (const auto &msg : diags.GetMessages()) {
            INFO("  " << msg);
        }
    }
    
    REQUIRE(!diags.HasErrors());
    
    auto &functions = ctx.Functions();
    REQUIRE(!functions.empty());
    
    // Should have multiple blocks for loop structure
    auto &fn = functions[0];
    REQUIRE(fn->blocks.size() >= 3);  // At least: cond, body, exit
}
#endif  // Temporarily disabled tests
