// Unit tests for the Go frontend.
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <unordered_set>

#include "frontends/go/include/go_lexer.h"
#include "frontends/go/include/go_frontend.h"
#include "frontends/go/include/go_parser.h"
#include "frontends/go/include/go_sema.h"
#include "frontends/go/include/go_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/verifier.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::frontends::SemaContext;
using polyglot::ir::IRContext;
using namespace polyglot::go;

static std::vector<Token> Tokenize(const char *src) {
    GoLexer lex(src, "<test>");
    std::vector<Token> ts;
    for (;;) {
        auto t = lex.NextToken();
        ts.push_back(t);
        if (t.kind == TokenKind::kEndOfFile) break;
    }
    return ts;
}

static std::unique_ptr<File> Parse(const char *src, Diagnostics &d) {
    GoLexer lex(src, "<test>");
    GoParser p(lex, d);
    p.ParseFile();
    return p.TakeFile();
}

static std::unique_ptr<File> ParseAt(const char *src, Diagnostics &d,
                                     polyglot::frontends::GoVersion version) {
    GoLexer lex(src, "<test>");
    GoParser p(lex, d);
    p.SetGoVersion(version);
    p.ParseFile();
    return p.TakeFile();
}

static IRContext LowerContext(const char *src, Diagnostics &d) {
    auto f = Parse(src, d);
    IRContext ir;
    if (!f) return ir;
    SemaContext ctx(d);
    AnalyzeFile(*f, ctx);
    LowerToIR(*f, ir, d);
    return ir;
}

static std::string LowerIR(const char *src, Diagnostics &d) {
    auto ir = LowerContext(src, d);
    std::ostringstream os;
    for (const auto &fn : ir.Functions()) polyglot::ir::PrintFunction(*fn, os);
    return os.str();
}

TEST_CASE("Go lexer keywords and idents", "[go][lexer]") {
    auto ts = Tokenize("package main\nfunc Add(a int, b int) int { return a + b }\n");
    REQUIRE(ts.size() >= 10);
    CHECK(ts[0].kind == TokenKind::kKeyword);
    CHECK(ts[0].lexeme == "package");
    CHECK(ts[1].kind == TokenKind::kIdentifier);
    CHECK(ts[1].lexeme == "main");
}

TEST_CASE("Go lexer raw string literal", "[go][lexer]") {
    auto ts = Tokenize("package m\nvar s = `raw\nstring`\n");
    bool saw = false;
    for (auto &t : ts) if (t.kind == TokenKind::kString) saw = true;
    CHECK(saw);
}

TEST_CASE("Go lexer auto-semicolon insertion", "[go][lexer]") {
    auto ts = Tokenize("package m\nvar x = 1\nvar y = 2\n");
    int semis = 0;
    for (auto &t : ts) if (t.kind == TokenKind::kSymbol && t.lexeme == ";") ++semis;
    CHECK(semis >= 2);
}

TEST_CASE("Go parser parses package + func", "[go][parser]") {
    Diagnostics d;
    auto f = Parse("package main\nfunc Inc(x int) int { return x + 1 }\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
    CHECK(f->package_name == "main");
}

TEST_CASE("Go parser accepts struct + method", "[go][parser]") {
    Diagnostics d;
    auto f = Parse(
        "package geom\n"
        "type Point struct { X int; Y int }\n"
        "func (p Point) Sum() int { return p.X + p.Y }\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("Go parser accepts if/for/range", "[go][parser]") {
    Diagnostics d;
    auto f = Parse(
        "package m\n"
        "func F(xs []int) int {\n"
        "  s := 0\n"
        "  for i, v := range xs { if v > 0 { s = s + v } else { s = s + i } }\n"
        "  return s\n"
        "}\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
}

TEST_CASE("Go parser preserves generic declarations and instantiations", "[go][parser][modern]") {
    Diagnostics d;
    auto f = ParseAt(
        "package generic\n"
        "type Pair[T any, U comparable] struct { First T; Second U }\n"
        "func Id[T any](value T) T { return value }\n"
        "func Use() int { return Id[int](1) }\n",
        d, polyglot::frontends::GoVersion::kGo1_24);
    REQUIRE(f);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(f->decls.size() == 1);
    REQUIRE(f->decls[0].types.size() == 1);
    CHECK(f->decls[0].types[0].type_params.size() == 2);
    REQUIRE(f->funcs.size() == 2);
    CHECK(f->funcs[0]->type_params.size() == 1);
    auto ret = std::dynamic_pointer_cast<ReturnStmt>(f->funcs[1]->body->stmts.at(0));
    auto call = std::dynamic_pointer_cast<CallExpr>(ret->results.at(0));
    REQUIRE(call);
    auto inst = std::dynamic_pointer_cast<TypeInstantiationExpr>(call->fun);
    REQUIRE(inst);
    CHECK(inst->type_args.size() == 1);
}

TEST_CASE("Go parser represents interface type-set constraints", "[go][parser][modern]") {
    Diagnostics d;
    auto f = ParseAt(
        "package generic\n"
        "type Number interface { ~int | ~int64 | ~float64 }\n"
        "func Identity[T Number](value T) T { return value }\n",
        d, polyglot::frontends::GoVersion::kGo1_24);
    REQUIRE(f);
    REQUIRE_FALSE(d.HasErrors());
    auto constraint = f->decls.at(0).types.at(0).type;
    REQUIRE(constraint);
    REQUIRE(constraint->kind == TypeKind::kInterface);
    REQUIRE(constraint->methods.size() == 1);
    CHECK(constraint->methods.at(0).type->kind == TypeKind::kUnion);
    CHECK(constraint->methods.at(0).type->terms.size() == 3);
}

TEST_CASE("Go parser gates generic aliases and integer range", "[go][parser][versions]") {
    auto has_version_error = [](const Diagnostics &d) {
        for (const auto &diag : d.All())
            if (diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch)
                return true;
        return false;
    };

    Diagnostics alias_old;
    ParseAt("package p\ntype Vec[T any] = []T\n", alias_old,
            polyglot::frontends::GoVersion::kGo1_23);
    CHECK(has_version_error(alias_old));
    Diagnostics alias_new;
    ParseAt("package p\ntype Vec[T any] = []T\n", alias_new,
            polyglot::frontends::GoVersion::kGo1_24);
    CHECK_FALSE(alias_new.HasErrors());

    Diagnostics range_old;
    ParseAt("package p\nfunc F() { for range 3 {} }\n", range_old,
            polyglot::frontends::GoVersion::kGo1_21);
    CHECK(has_version_error(range_old));
    Diagnostics range_new;
    auto modern = ParseAt("package p\nfunc F() { for range 3 {} }\n", range_new,
                          polyglot::frontends::GoVersion::kGo1_22);
    REQUIRE(modern);
    CHECK_FALSE(range_new.HasErrors());
    auto loop = std::dynamic_pointer_cast<ForStmt>(modern->funcs[0]->body->stmts.at(0));
    REQUIRE(loop);
    CHECK(loop->is_range);
}

TEST_CASE("Go parser fails closed for uncertain legacy range operands",
          "[go][parser][versions]") {
    Diagnostics d;
    ParseAt("package p\nfunc F(n int) { for range n {} }\n", d,
            polyglot::frontends::GoVersion::kGo1_21);
    bool unsupported = false;
    for (const auto &diag : d.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedSyntax;
    CHECK(unsupported);
}

TEST_CASE("Go parser gates range over a known function value at Go 1.23",
          "[go][parser][versions]") {
    constexpr const char *source =
        "package p\n"
        "func F(seq func()) { for v := range seq {} }\n";
    Diagnostics old_d;
    ParseAt(source, old_d, polyglot::frontends::GoVersion::kGo1_22);
    bool mismatch = false;
    for (const auto &diag : old_d.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics new_d;
    auto file = ParseAt(source, new_d, polyglot::frontends::GoVersion::kGo1_23);
    REQUIRE(file);
    CHECK_FALSE(new_d.HasErrors());
    auto loop = std::dynamic_pointer_cast<ForStmt>(file->funcs[0]->body->stmts.at(0));
    REQUIRE(loop);
    CHECK(loop->is_range);
}

TEST_CASE("Go parser preserves and gates Go 1.26 new expression", "[go][parser][versions]") {
    constexpr const char *source =
        "package p\nfunc F() *int { return new(1 + 2) }\n";
    Diagnostics old_d;
    ParseAt(source, old_d, polyglot::frontends::GoVersion::kGo1_25);
    bool mismatch = false;
    for (const auto &diag : old_d.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics new_d;
    auto file = ParseAt(source, new_d, polyglot::frontends::GoVersion::kGo1_26);
    REQUIRE(file);
    REQUIRE_FALSE(new_d.HasErrors());
    auto ret = std::dynamic_pointer_cast<ReturnStmt>(file->funcs[0]->body->stmts.at(0));
    REQUIRE(ret);
    auto call = std::dynamic_pointer_cast<CallExpr>(ret->results.at(0));
    REQUIRE(call);
    CHECK(call->is_new_expression);
}

TEST_CASE("Go parser gates self-referential generic constraints at Go 1.26",
          "[go][parser][versions]") {
    constexpr const char *source =
        "package p\n"
        "type Adder[A Adder[A]] interface { Add(A) A }\n";

    Diagnostics old_d;
    auto old_file = ParseAt(source, old_d, polyglot::frontends::GoVersion::kGo1_25);
    REQUIRE(old_file);
    bool mismatch = false;
    for (const auto &diag : old_d.All())
        mismatch |= diag.code == polyglot::frontends::ErrorCode::kLangVersionMismatch;
    CHECK(mismatch);

    Diagnostics current_d;
    auto current_file = ParseAt(source, current_d, polyglot::frontends::GoVersion::kGo1_26);
    REQUIRE(current_file);
    REQUIRE_FALSE(current_d.HasErrors());
    REQUIRE(current_file->decls.size() == 1);
    REQUIRE(current_file->decls[0].types.size() == 1);
    CHECK(current_file->decls[0].types[0].type_params.size() == 1);
}

TEST_CASE("Go ordinary index expressions are not treated as generic instantiations",
          "[go][parser][modern]") {
    Diagnostics d;
    auto f = Parse("package p\nfunc At(a []int, i int) int { return a[i+1] }\n", d);
    REQUIRE(f);
    CHECK_FALSE(d.HasErrors());
    auto ret = std::dynamic_pointer_cast<ReturnStmt>(f->funcs[0]->body->stmts.at(0));
    CHECK(std::dynamic_pointer_cast<IndexExpr>(ret->results.at(0)) != nullptr);
}

TEST_CASE("Go array constant expressions remain in the AST and signatures fail closed",
          "[go][parser][signatures][arrays]") {
    constexpr const char *symbolic =
        "package p\nconst N = 4\nfunc Sum(values [N]int) int { return 0 }\n";
    Diagnostics parse_diags;
    auto file = Parse(symbolic, parse_diags);
    REQUIRE(file);
    REQUIRE_FALSE(parse_diags.HasErrors());
    REQUIRE(file->funcs.size() == 1);
    REQUIRE(file->funcs[0]->params.size() == 1);
    auto array_type = file->funcs[0]->params[0].second;
    REQUIRE(array_type);
    CHECK(array_type->kind == TypeKind::kArray);
    REQUIRE(array_type->array_len_expr);
    CHECK(std::dynamic_pointer_cast<Identifier>(array_type->array_len_expr)->name == "N");

    GoLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    Diagnostics symbolic_diags;
    CHECK(frontend.ExtractSignatures(symbolic, "p.go", "p", symbolic_diags, options).empty());
    bool missing = false;
    for (const auto &diag : symbolic_diags.All())
        missing |= diag.code == polyglot::frontends::ErrorCode::kSignatureMissing;
    CHECK(missing);

    Diagnostics literal_diags;
    auto literal_signatures = frontend.ExtractSignatures(
        "package p\nfunc Sum(values [4]int) int { return 0 }\n",
        "p.go", "p", literal_diags, options);
    REQUIRE_FALSE(literal_diags.HasErrors());
    REQUIRE(literal_signatures.size() == 1);
    REQUIRE(literal_signatures[0].param_types.size() == 1);
    CHECK(literal_signatures[0].param_types[0].kind == polyglot::core::TypeKind::kArray);
    CHECK(literal_signatures[0].param_types[0].array_size == 4);
}

TEST_CASE("Go version-aware signature extraction preserves multiple results",
          "[go][signatures][versions]") {
    GoLanguageFrontend frontend;
    polyglot::frontends::FrontendOptions options;
    options.go_version = polyglot::frontends::GoVersion::kGo1_22;
    Diagnostics d;
    auto signatures = frontend.ExtractSignatures(
        "package values\nfunc Pair() (int, string) { return 1, \"value\" }\n",
        "values.go", "values", d, options);
    REQUIRE_FALSE(d.HasErrors());
    REQUIRE(signatures.size() == 1);
    CHECK(signatures[0].return_type.kind == polyglot::core::TypeKind::kTuple);
    CHECK(signatures[0].return_type.type_args.size() == 2);
}

TEST_CASE("Go sema flags break outside loop", "[go][sema]") {
    Diagnostics d;
    auto f = Parse("package m\nfunc F() { break }\n", d);
    REQUIRE(f);
    SemaContext ctx(d);
    AnalyzeFile(*f, ctx);
    CHECK(d.HasErrors());
}

TEST_CASE("Go lowering emits IR for function", "[go][lowering]") {
    Diagnostics d;
    auto ir = LowerIR("package main\nfunc Inc(x int) int { return x + 1 }\n", d);
    CHECK_FALSE(ir.empty());
    CHECK(ir.find("Inc") != std::string::npos);
}

TEST_CASE("Go lowering uses native immediates and boolean comparisons", "[go][lowering][scalar]") {
    SECTION("integer literals remain textual immediates") {
        Diagnostics d;
        auto ctx = LowerContext("package main\nfunc Inc(x int) int { return x + 1 }\n", d);
        REQUIRE_FALSE(d.HasErrors());
        REQUIRE(ctx.Functions().size() == 1);

        const polyglot::ir::BinaryInstruction *add = nullptr;
        for (const auto &block : ctx.Functions()[0]->blocks) {
            for (const auto &inst : block->instructions) {
                auto candidate = dynamic_cast<const polyglot::ir::BinaryInstruction *>(inst.get());
                if (candidate && candidate->op == polyglot::ir::BinaryInstruction::Op::kAdd)
                    add = candidate;
            }
        }
        REQUIRE(add != nullptr);
        REQUIRE(add->operands.size() == 2);
        CHECK(add->operands[1] == "1");
    }

    SECTION("comparison results are typed as i1") {
        Diagnostics d;
        auto ctx = LowerContext(
            "package main\nfunc IsPositive(x int) bool { return x > 0 }\n", d);
        REQUIRE_FALSE(d.HasErrors());
        REQUIRE(ctx.Functions().size() == 1);

        const polyglot::ir::BinaryInstruction *comparison = nullptr;
        for (const auto &block : ctx.Functions()[0]->blocks) {
            for (const auto &inst : block->instructions) {
                auto candidate = dynamic_cast<const polyglot::ir::BinaryInstruction *>(inst.get());
                if (candidate && candidate->op == polyglot::ir::BinaryInstruction::Op::kCmpSgt)
                    comparison = candidate;
            }
        }
        REQUIRE(comparison != nullptr);
        CHECK(comparison->type.kind == polyglot::ir::IRTypeKind::kI1);
        REQUIRE(comparison->operands.size() == 2);
        CHECK(comparison->operands[1] == "0");
    }
}

TEST_CASE("Go lowering assigns unique SSA names within a function",
          "[go][lowering][ssa]") {
    Diagnostics d;
    auto ctx = LowerContext(
        "package main\n"
        "func helper(x int) int { return x }\n"
        "func process(a int, b int) int {\n"
        "  first := helper(a)\n"
        "  second := helper(b)\n"
        "  greater := first > second\n"
        "  smaller := first < second\n"
        "  sum := first + second\n"
        "  adjusted := sum - a\n"
        "  return adjusted * b\n"
        "}\n",
        d);
    REQUIRE_FALSE(d.HasErrors());

    const polyglot::ir::Function *process = nullptr;
    for (const auto &fn : ctx.Functions())
        if (fn->name == "process")
            process = fn.get();
    REQUIRE(process != nullptr);

    std::unordered_set<std::string> all_names;
    std::unordered_set<std::string> load_names;
    std::unordered_set<std::string> binary_names;
    std::unordered_set<std::string> call_names;
    for (const auto &block : process->blocks) {
        for (const auto &inst : block->instructions) {
            if (!inst->name.empty()) {
                INFO("duplicate SSA name: " << inst->name);
                CHECK(all_names.insert(inst->name).second);
            }
            if (dynamic_cast<const polyglot::ir::LoadInstruction *>(inst.get()))
                load_names.insert(inst->name);
            if (dynamic_cast<const polyglot::ir::BinaryInstruction *>(inst.get()))
                binary_names.insert(inst->name);
            if (dynamic_cast<const polyglot::ir::CallInstruction *>(inst.get()))
                call_names.insert(inst->name);
        }
    }

    CHECK(load_names.size() >= 10);
    CHECK(binary_names.size() == 5);
    CHECK(call_names.size() == 2);
}

TEST_CASE("Go lowering executes stack structs constructors fields and pointer receiver methods",
          "[go][lowering][oop]") {
    Diagnostics d;
    auto ctx = LowerContext(
        "package main\n"
        "type LogisticsSession struct { gate int; score int; eta int }\n"
        "func NewLogisticsSession(s *LogisticsSession, gate int, score int, eta int) {\n"
        "  s.gate = gate\n"
        "  s.score = score\n"
        "  s.eta = eta\n"
        "}\n"
        "func (s *LogisticsSession) Adjust(delta int) { s.eta += delta }\n"
        "func (s *LogisticsSession) Decision() int {\n"
        "  return s.gate + s.score*10 + s.eta\n"
        "}\n"
        "func FromConstructor(gate int, score int, eta int) int {\n"
        "  var session LogisticsSession\n"
        "  NewLogisticsSession(&session, gate, score, eta)\n"
        "  session.Adjust(2)\n"
        "  return session.Decision()\n"
        "}\n"
        "func FromComposite(gate int, score int, eta int) int {\n"
        "  session := LogisticsSession{gate: gate, score: score, eta: eta}\n"
        "  return session.Decision()\n"
        "}\n",
        d);
    INFO(d.FormatAll());
    REQUIRE_FALSE(d.HasErrors());

    std::string verify_message;
    INFO(verify_message);
    REQUIRE(polyglot::ir::Verify(ctx, &verify_message));

    bool saw_struct_alloca = false;
    std::size_t gep_count = 0;
    std::unordered_set<std::string> callees;
    for (const auto &fn : ctx.Functions()) {
        for (const auto &block : fn->blocks) {
            for (const auto &inst : block->instructions) {
                if (auto alloca = dynamic_cast<const polyglot::ir::AllocaInstruction *>(inst.get())) {
                    saw_struct_alloca |= alloca->type.kind == polyglot::ir::IRTypeKind::kPointer &&
                                         !alloca->type.subtypes.empty() &&
                                         alloca->type.subtypes.front().kind ==
                                             polyglot::ir::IRTypeKind::kStruct &&
                                         alloca->type.subtypes.front().name ==
                                             "go.LogisticsSession";
                }
                if (dynamic_cast<const polyglot::ir::GetElementPtrInstruction *>(inst.get()))
                    ++gep_count;
                if (auto call = dynamic_cast<const polyglot::ir::CallInstruction *>(inst.get()))
                    callees.insert(call->callee);
            }
        }
    }
    CHECK(saw_struct_alloca);
    CHECK(gep_count >= 12);
    CHECK(callees.count("NewLogisticsSession") == 1);
    CHECK(callees.count("LogisticsSession.Adjust") == 1);
    CHECK(callees.count("LogisticsSession.Decision") == 1);
}

TEST_CASE("Go lowering rejects selectors without a static struct layout",
          "[go][lowering][oop][boundary]") {
    Diagnostics d;
    LowerIR("package main\nfunc Dynamic(value int) int { return value.missing }\n", d);
    bool explicit_selector_failure = false;
    for (const auto &diag : d.All()) {
        explicit_selector_failure |=
            diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering &&
            diag.message.find("selector receiver has no named struct layout") != std::string::npos;
    }
    CHECK(explicit_selector_failure);
}

TEST_CASE("Go lowering rejects range loops instead of emitting an infinite loop",
          "[go][lowering][modern]") {
    Diagnostics d;
    auto ir = LowerIR(
        "package main\nfunc Sum(xs []int) int { s := 0; for _, v := range xs { s += v }; return s }\n",
        d);
    (void)ir;
    bool unsupported = false;
    for (const auto &diag : d.All())
        unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
    CHECK(unsupported);
}

TEST_CASE("Go lowering rejects generics and new expression instead of fabricating IR",
          "[go][lowering][modern]") {
    SECTION("generic declaration and call") {
        Diagnostics d;
        LowerIR(
            "package main\n"
            "func Id[T any](v T) T { return v }\n"
            "func Use() int { return Id[int](1) }\n",
            d);
        bool unsupported = false;
        for (const auto &diag : d.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        CHECK(unsupported);
    }

    SECTION("Go 1.26 new expression") {
        Diagnostics d;
        LowerIR("package main\nfunc F() *int { return new(1 + 2) }\n", d);
        bool unsupported = false;
        for (const auto &diag : d.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        CHECK(unsupported);
    }

    SECTION("generic function value ambiguity") {
        Diagnostics d;
        LowerIR(
            "package main\n"
            "func Id[T any](v T) T { return v }\n"
            "func Value() any { return Id[int] }\n",
            d);
        bool unsupported = false;
        for (const auto &diag : d.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        CHECK(unsupported);
    }


    SECTION("multiple results") {
        Diagnostics d;
        LowerIR("package main\nfunc Pair() (int, int) { return 1, 2 }\n", d);
        bool unsupported = false;
        for (const auto &diag : d.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        CHECK(unsupported);
    }
}

TEST_CASE("Go lowering fails closed for unresolved scalar boundaries",
          "[go][lowering][boundary]") {
    for (const char *source : {
             "package main\nfunc Missing() int {}\n",
             "package main\nfunc Named() (value int) { return }\n",
             "package main\nfunc Unknown() int { return missing }\n",
             "package main\nfunc InferredVar() int { var value = 1; return value }\n"}) {
        Diagnostics d;
        LowerIR(source, d);
        bool unsupported = false;
        for (const auto &diag : d.All())
            unsupported |= diag.code == polyglot::frontends::ErrorCode::kUnsupportedLowering;
        INFO(source);
        CHECK(unsupported);
    }
}
