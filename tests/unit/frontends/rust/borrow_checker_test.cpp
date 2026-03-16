// Rust Borrow Checker Tests
// Tests for the comprehensive borrow checker implementation including:
// - Lifetime analysis and validation
// - Mutable/immutable borrow conflicts
// - Move semantics and use-after-move detection
// - Reference lifetime checking

#include <catch2/catch_test_macros.hpp>

#include "frontends/common/include/sema_context.h"
#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_sema.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::SemaContext;
using polyglot::rust::AnalyzeModule;
using namespace polyglot::rust;

namespace {

// Helper to create a simple identifier
std::shared_ptr<Identifier> MakeId(const std::string &name) {
    auto id = std::make_shared<Identifier>();
    id->name = name;
    return id;
}

// Helper to create a type path
std::shared_ptr<TypePath> MakeTypePath(const std::string &type_name) {
    auto tp = std::make_shared<TypePath>();
    tp->segments.push_back(type_name);
    return tp;
}

// Helper to create a reference type with lifetime
std::shared_ptr<ReferenceType> MakeRefType(const std::string &inner_type, 
                                            const std::string &lifetime = "",
                                            bool is_mut = false) {
    auto ref = std::make_shared<ReferenceType>();
    ref->is_mut = is_mut;
    ref->inner = MakeTypePath(inner_type);
    if (!lifetime.empty()) {
        auto lt = std::make_shared<LifetimeType>();
        lt->name = lifetime;
        ref->lifetime = lt;
    }
    return ref;
}

// Helper to create identifier pattern
std::shared_ptr<IdentifierPattern> MakeIdPattern(const std::string &name) {
    auto pat = std::make_shared<IdentifierPattern>();
    pat->name = name;
    return pat;
}

// Helper to create let statement
std::shared_ptr<LetStatement> MakeLet(const std::string &name, 
                                       std::shared_ptr<Expression> init,
                                       std::shared_ptr<TypeNode> type = nullptr) {
    auto let = std::make_shared<LetStatement>();
    let->pattern = MakeIdPattern(name);
    let->init = init;
    let->type_annotation = type;
    return let;
}

// Helper to create a unary expression
std::shared_ptr<UnaryExpression> MakeUnary(const std::string &op, 
                                            std::shared_ptr<Expression> operand) {
    auto un = std::make_shared<UnaryExpression>();
    un->op = op;
    un->operand = operand;
    return un;
}

// Helper to create function item
std::shared_ptr<FunctionItem> MakeFunction(const std::string &name,
                                            std::vector<FunctionItem::Param> params,
                                            std::shared_ptr<TypeNode> ret_type,
                                            std::vector<std::shared_ptr<Statement>> body,
                                            std::vector<std::string> type_params = {}) {
    auto fn = std::make_shared<FunctionItem>();
    fn->name = name;
    fn->params = std::move(params);
    fn->return_type = ret_type;
    fn->body = std::move(body);
    fn->has_body = true;
    fn->type_params = std::move(type_params);
    return fn;
}

} // namespace

// ============ Test 1: Basic Borrow Conflicts ============
TEST_CASE("Rust Borrow Checker - Basic Borrow Conflicts", "[rust][borrow]") {
    SECTION("Immutable borrow prevents mutable borrow") {
        Module mod;
        
        // fn test() {
        //     let x = 5;
        //     let y = &x;     // immutable borrow
        //     let z = &mut x; // should fail - x is already borrowed
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        
        auto ref_y = MakeUnary("&", MakeId("x"));
        auto let_y = MakeLet("y", ref_y);
        
        auto ref_z = MakeUnary("&mut", MakeId("x"));
        auto let_z = MakeLet("z", ref_z);
        
        auto fn = MakeFunction("test", {}, nullptr, {let_x, let_y, let_z});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Should have an error about conflicting borrows
        bool has_borrow_error = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("borrow") != std::string::npos ||
                d.message.find("borrowed") != std::string::npos) {
                has_borrow_error = true;
                break;
            }
        }
        REQUIRE(has_borrow_error);
    }
    
    SECTION("Multiple immutable borrows allowed") {
        Module mod;
        
        // fn test() {
        //     let x = 5;
        //     let y = &x;  // immutable borrow
        //     let z = &x;  // another immutable borrow - should be OK
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        
        auto ref_y = MakeUnary("&", MakeId("x"));
        auto let_y = MakeLet("y", ref_y);
        
        auto ref_z = MakeUnary("&", MakeId("x"));
        auto let_z = MakeLet("z", ref_z);
        
        auto fn = MakeFunction("test", {}, nullptr, {let_x, let_y, let_z});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Multiple immutable borrows should not cause errors
        bool has_borrow_conflict = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("cannot take") != std::string::npos &&
                d.message.find("borrow") != std::string::npos) {
                has_borrow_conflict = true;
                break;
            }
        }
        REQUIRE_FALSE(has_borrow_conflict);
    }
    
    SECTION("Mutable borrow prevents any other borrow") {
        Module mod;
        
        // fn test() {
        //     let mut x = 5;
        //     let y = &mut x;  // mutable borrow
        //     let z = &x;      // should fail - can't borrow while mutably borrowed
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        let_x->is_mut = true;
        
        auto ref_y = MakeUnary("&mut", MakeId("x"));
        auto let_y = MakeLet("y", ref_y);
        
        auto ref_z = MakeUnary("&", MakeId("x"));
        auto let_z = MakeLet("z", ref_z);
        
        auto fn = MakeFunction("test", {}, nullptr, {let_x, let_y, let_z});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Should have an error about conflicting borrows
        bool has_borrow_error = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("borrow") != std::string::npos) {
                has_borrow_error = true;
                break;
            }
        }
        REQUIRE(has_borrow_error);
    }
}

// ============ Test 2: Lifetime Validation ============
TEST_CASE("Rust Borrow Checker - Lifetime Validation", "[rust][lifetime]") {
    SECTION("Return reference requires lifetime parameter") {
        Module mod;
        
        // fn get_ref(x: &i32) -> &i32 {
        //     x
        // }
        // This should work with lifetime elision
        FunctionItem::Param param;
        param.name = "x";
        param.type = MakeRefType("i32", "'a");
        
        auto ret = std::make_shared<ReturnStatement>();
        ret->value = MakeId("x");
        
        auto fn = MakeFunction("get_ref", {param}, MakeRefType("i32", "'a"), {ret}, {"'a"});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // With explicit lifetime annotation, should compile without errors
        REQUIRE(diags.ErrorCount() == 0);
    }
    
    SECTION("Return reference without lifetime annotation warns") {
        Module mod;
        
        // fn bad_ref() -> &i32 {
        //     let x = 5;
        //     &x  // returning reference to local - should warn
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        
        auto ret = std::make_shared<ReturnStatement>();
        ret->value = MakeUnary("&", MakeId("x"));
        
        auto fn = MakeFunction("bad_ref", {}, MakeRefType("i32"), {let_x, ret});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Should have a warning about lifetime
        bool has_lifetime_warning = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("lifetime") != std::string::npos ||
                d.message.find("reference") != std::string::npos) {
                has_lifetime_warning = true;
                break;
            }
        }
        REQUIRE(has_lifetime_warning);
    }
}

// ============ Test 3: Assignment While Borrowed ============
TEST_CASE("Rust Borrow Checker - Assignment While Borrowed", "[rust][borrow]") {
    SECTION("Cannot assign while borrowed") {
        Module mod;
        
        // fn test() {
        //     let mut x = 5;
        //     let y = &x;
        //     x = 10;  // should fail - x is borrowed
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        let_x->is_mut = true;
        
        auto ref_y = MakeUnary("&", MakeId("x"));
        auto let_y = MakeLet("y", ref_y);
        
        auto lit10 = std::make_shared<Literal>();
        lit10->value = "10";
        auto assign = std::make_shared<AssignmentExpression>();
        assign->op = "=";
        assign->left = MakeId("x");
        assign->right = lit10;
        
        auto expr_stmt = std::make_shared<ExprStatement>();
        expr_stmt->expr = assign;
        
        auto fn = MakeFunction("test", {}, nullptr, {let_x, let_y, expr_stmt});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Should have an error about assignment while borrowed
        bool has_assign_error = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("assign") != std::string::npos &&
                d.message.find("borrowed") != std::string::npos) {
                has_assign_error = true;
                break;
            }
        }
        REQUIRE(has_assign_error);
    }
}

// ============ Test 4: Use While Mutably Borrowed ============
TEST_CASE("Rust Borrow Checker - Use While Mutably Borrowed", "[rust][borrow]") {
    SECTION("Cannot use value while mutably borrowed") {
        Module mod;
        
        // fn test() {
        //     let mut x = 5;
        //     let y = &mut x;
        //     let z = x;  // should fail - x is mutably borrowed
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        let_x->is_mut = true;
        
        auto ref_y = MakeUnary("&mut", MakeId("x"));
        auto let_y = MakeLet("y", ref_y);
        
        auto let_z = MakeLet("z", MakeId("x"));
        
        auto fn = MakeFunction("test", {}, nullptr, {let_x, let_y, let_z});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Should have an error about use while borrowed
        bool has_use_error = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("mutably borrowed") != std::string::npos) {
                has_use_error = true;
                break;
            }
        }
        REQUIRE(has_use_error);
    }
}

// ============ Test 5: Function Return Type Checking ============
TEST_CASE("Rust Borrow Checker - Function Return Type", "[rust][return]") {
    SECTION("Non-void function must return") {
        Module mod;
        
        // fn missing_return() -> i32 {
        //     let x = 5;
        //     // missing return
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        
        auto fn = MakeFunction("missing_return", {}, MakeTypePath("i32"), {let_x});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Should have a warning about missing return
        bool has_return_warning = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("return") != std::string::npos &&
                d.message.find("exit") != std::string::npos) {
                has_return_warning = true;
                break;
            }
        }
        REQUIRE(has_return_warning);
    }
}

// ============ Test 6: Copy Types ============
TEST_CASE("Rust Borrow Checker - Copy Types", "[rust][copy]") {
    SECTION("Copy types can be used after move") {
        Module mod;
        
        // fn test() {
        //     let x: i32 = 5;
        //     let y = x;  // copy, not move
        //     let z = x;  // should work - i32 is Copy
        // }
        auto lit5 = std::make_shared<Literal>();
        lit5->value = "5";
        auto let_x = MakeLet("x", lit5, MakeTypePath("i32"));
        
        auto let_y = MakeLet("y", MakeId("x"));
        auto let_z = MakeLet("z", MakeId("x"));
        
        auto fn = MakeFunction("test", {}, nullptr, {let_x, let_y, let_z});
        mod.items.push_back(fn);
        
        Diagnostics diags;
        SemaContext ctx(diags);
        AnalyzeModule(mod, ctx);
        
        // Copy types should not generate move errors
        bool has_move_error = false;
        for (const auto &d : diags.All()) {
            if (d.message.find("moved") != std::string::npos) {
                has_move_error = true;
                break;
            }
        }
        REQUIRE_FALSE(has_move_error);
    }
}
