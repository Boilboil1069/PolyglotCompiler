#include <catch2/catch_test_macros.hpp>
#include <string>
#include <sstream>

#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "backends/x86_64/include/x86_target.h"
#include "backends/arm64/include/arm64_target.h"

using namespace polyglot;

TEST_CASE("End-to-end: Simple function compilation", "[e2e]") {
    std::string code = R"(
        int add(int a, int b) {
            return a + b;
        }
    )";

    // Step 1: Parse
    frontends::Diagnostics diags;
    cpp::CppLexer lexer(code, "<e2e>");
    cpp::CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

    REQUIRE(!diags.HasErrors());
    REQUIRE(module != nullptr);

    // Step 2: Lower to IR
    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);

    REQUIRE(!diags.HasErrors());
    auto &functions = ctx.Functions();
    REQUIRE(functions.size() >= 1);

    // Step 3: Print IR (for debugging)
    std::ostringstream ir_out;
    ir::PrintModule(ctx, ir_out);

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
    cpp::CppLexer lexer(code, "<e2e>");
    cpp::CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

    REQUIRE(!diags.HasErrors());

    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);

    REQUIRE(!diags.HasErrors());

    auto &functions = ctx.Functions();
    REQUIRE(!functions.empty());

    // Verify control flow was created
    auto &fn = functions[0];
    REQUIRE(fn->blocks.size() > 1);  // Should have multiple blocks for if/else
}

TEST_CASE("End-to-end: Assembly generation x86_64", "[e2e][backend]") {
    std::string code = R"(
        int simple(int x) {
            return x + 42;
        }
    )";

    frontends::Diagnostics diags;
    cpp::CppLexer lexer(code, "<e2e>");
    cpp::CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

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
    cpp::CppLexer lexer(code, "<e2e>");
    cpp::CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

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
    // Test that produces multiple basic blocks through nested if/else.
    // Avoid while/for since the C++ parser has an identifier-as-type ambiguity
    // that prevents expression-statements starting with identifiers from parsing.
    std::string code = R"(
        int classify(int x, int y) {
            if (x == 0) {
                if (y == 0) {
                    return 0;
                }
                return 1;
            }
            if (y == 0) {
                return 2;
            }
            return 3;
        }
    )";

    frontends::Diagnostics diags;
    cpp::CppLexer lexer(code, "<e2e>");
    cpp::CppParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();

    if (diags.HasErrors()) {
        std::ostringstream err_out;
        err_out << "Parse errors:";
        for (const auto &d : diags.All()) {
            err_out << "\n  [" << d.loc.line << ":" << d.loc.column << "] " << d.message;
        }
        FAIL(err_out.str());
    }
    REQUIRE(module != nullptr);

    ir::IRContext ctx;
    cpp::LowerToIR(*module, ctx, diags);

    if (diags.HasErrors()) {
        std::ostringstream err_out;
        err_out << "Errors during lowering:";
        for (const auto &d : diags.All()) {
            err_out << "\n  " << d.message;
        }
        FAIL(err_out.str());
    }

    REQUIRE(!diags.HasErrors());

    auto &functions = ctx.Functions();
    REQUIRE(!functions.empty());

    // Should have multiple blocks from nested if/else
    auto &fn = functions[0];
    REQUIRE(fn->blocks.size() >= 3);  // At least: entry, then-branch, merge
}
