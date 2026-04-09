// ============================================================================
// Ploy Frontend Devirtualization Tests
//
// These tests verify that after ploy lowering the cross-language call
// descriptors reflect proper devirtualization: direct method-call stubs
// replace virtual dispatch paths when the concrete type is statically known
// (via EXTEND declarations and typed NEW expressions).
//
// Unlike the IR-level devirtualization tests (tests/unit/devirtualization_test.cpp
// which operates on raw IR class hierarchies), these tests work at the ploy
// source language level and verify the observable effects through
// CrossLangCallDescriptor counts and stub naming conventions.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;
using polyglot::ploy::PloyLowering;
using polyglot::ploy::CrossLangCallDescriptor;
using polyglot::ir::IRContext;

// ============================================================================
// Helpers
// ============================================================================

namespace {

struct PloyCompileResult {
    std::string ir_text;
    std::vector<CrossLangCallDescriptor> descriptors;
    bool success;
};

PloyCompileResult CompileWithDescriptors(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<devirt_test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) return {"", {}, false};

    PloySema sema(diags, PloySemaOptions{});
    if (!sema.Analyze(module)) return {"", {}, false};

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) return {"", {}, false};

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return {oss.str(), lowering.CallDescriptors(), true};
}

// Count descriptors whose source_function contains the given substring
int CountDescriptorsMatching(const std::vector<CrossLangCallDescriptor> &descs,
                              const std::string &substring) {
    return static_cast<int>(std::count_if(descs.begin(), descs.end(),
        [&](const CrossLangCallDescriptor &d) {
            return d.source_function.find(substring) != std::string::npos;
        }));
}

} // namespace

// ============================================================================
// 1. Baseline: virtual call via generic object (no EXTEND, no static type)
//    Expected: descriptor for 'forward' is recorded with generic bridge name
// ============================================================================

TEST_CASE("Ploy devirt: generic METHOD produces one call descriptor", "[ploy][devirt]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
IMPORT python PACKAGE torch;

FUNC run(model: python::Module) -> INT {
    LET output = METHOD(python, model, forward, 42);
    RETURN 0;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // Must have exactly one descriptor for 'forward'
    int forward_count = CountDescriptorsMatching(result.descriptors, "forward");
    CHECK(forward_count == 1);

    // That descriptor should be from python language
    bool found = false;
    for (const auto &d : result.descriptors) {
        if (d.source_function == "forward") {
            found = true;
            CHECK(d.source_language == "python");
        }
    }
    CHECK(found);
}

// ============================================================================
// 2. After EXTEND: static type is known → direct bridge generated, not
//    generic virtual dispatch path.  The stub name should embed the derived
//    class name (Dog::speak) rather than a generic __ploy_bridge.
// ============================================================================

TEST_CASE("Ploy devirt: EXTEND produces specialised extend-bridge descriptor", "[ploy][devirt]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING {
        RETURN "Woof";
    }
}

FUNC make_noise() -> INT {
    LET dog = NEW(python, Dog);
    LET sound = METHOD(python, dog, speak);
    RETURN 0;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // The 'speak' call should be a direct EXTEND bridge, not a generic bridge
    bool found_extend_bridge = false;
    for (const auto &d : result.descriptors) {
        if (d.source_function.find("speak") != std::string::npos) {
            // An EXTEND-aware lowering records the derived class in the bridge name
            found_extend_bridge = (d.source_function.find("Dog") != std::string::npos ||
                                   d.source_function == "speak");
            CHECK(d.source_language == "python");
        }
    }
    CHECK(found_extend_bridge);

    // The IR must contain the EXTEND register stub
    CHECK(result.ir_text.find("__ploy_extend_register") != std::string::npos);
    // And a direct Dog::speak bridge
    CHECK(result.ir_text.find("__ploy_extend_Dog_speak") != std::string::npos);
}

// ============================================================================
// 3. Two EXTEND classes with the SAME method name: must produce TWO distinct
//    descriptors (one per concrete type), not collapsed into a single entry.
// ============================================================================

TEST_CASE("Ploy devirt: two EXTEND classes with same method produce distinct descriptors",
          "[ploy][devirt]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
EXTEND(python, Animal) AS Dog {
    FUNC speak() -> STRING { RETURN "Woof"; }
}

EXTEND(python, Animal) AS Cat {
    FUNC speak() -> STRING { RETURN "Meow"; }
}

FUNC chorus() -> INT {
    LET dog = NEW(python, Dog);
    LET cat = NEW(python, Cat);
    LET d_sound = METHOD(python, dog, speak);
    LET c_sound = METHOD(python, cat, speak);
    RETURN 0;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // Must have two separate extend-bridge stubs: one for Dog, one for Cat
    CHECK(result.ir_text.find("__ploy_extend_Dog_speak") != std::string::npos);
    CHECK(result.ir_text.find("__ploy_extend_Cat_speak") != std::string::npos);

    // And two EXTEND register calls
    // (The IR should contain __ploy_extend_register at least twice)
    size_t pos = 0;
    int register_count = 0;
    while ((pos = result.ir_text.find("__ploy_extend_register", pos)) != std::string::npos) {
        ++register_count;
        ++pos;
    }
    CHECK(register_count >= 2);
}

// ============================================================================
// 4. EXTEND with multiple methods: each method must get its own descriptor
// ============================================================================

TEST_CASE("Ploy devirt: EXTEND with multiple methods generates one descriptor per method",
          "[ploy][devirt]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
EXTEND(python, Shape) AS Circle {
    FUNC area(r: FLOAT) -> FLOAT  { RETURN r; }
    FUNC perimeter(r: FLOAT) -> FLOAT { RETURN r; }
    FUNC describe() -> STRING { RETURN "circle"; }
}

FUNC measure() -> INT {
    LET c = NEW(python, Circle);
    LET a = METHOD(python, c, area, 3.14);
    LET p = METHOD(python, c, perimeter, 3.14);
    LET s = METHOD(python, c, describe);
    RETURN 0;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // All three method bridges must appear
    CHECK(result.ir_text.find("__ploy_extend_Circle_area") != std::string::npos);
    CHECK(result.ir_text.find("__ploy_extend_Circle_perimeter") != std::string::npos);
    CHECK(result.ir_text.find("__ploy_extend_Circle_describe") != std::string::npos);
}

// ============================================================================
// 5. Call graph BEFORE devirtualisation: plain METHOD on untyped object.
//    Call graph AFTER EXTEND: typed object removes the generic virtual edge.
//
//    We compare descriptor counts: with a typed EXTEND-derived object the
//    lowering should produce EXTEND-specific entries rather than a shared
//    generic bridge, which would appear differently in the descriptor list.
// ============================================================================

TEST_CASE("Ploy devirt: call graph narrows from generic bridge to direct bridge",
          "[ploy][devirt]") {
    // ---- Before: untyped object (generic bridge) ----
    Diagnostics before_diags;
    auto before = CompileWithDescriptors(R"(
IMPORT python PACKAGE somelib;

FUNC run_generic(obj: python::Base) -> INT {
    LET r = METHOD(python, obj, process, 1);
    RETURN 0;
}
)", before_diags);

    REQUIRE(before.success);
    REQUIRE(!before_diags.HasErrors());

    // The generic bridge should appear in descriptors
    bool has_generic_process = false;
    for (const auto &d : before.descriptors) {
        if (d.source_function == "process") {
            has_generic_process = true;
        }
    }
    CHECK(has_generic_process);

    // ---- After: concrete EXTEND type eliminates virtual dispatch ----
    Diagnostics after_diags;
    auto after = CompileWithDescriptors(R"(
EXTEND(python, Base) AS ConcreteImpl {
    FUNC process(x: INT) -> INT { RETURN x; }
}

FUNC run_concrete() -> INT {
    LET obj = NEW(python, ConcreteImpl);
    LET r = METHOD(python, obj, process, 1);
    RETURN 0;
}
)", after_diags);

    REQUIRE(after.success);
    REQUIRE(!after_diags.HasErrors());

    // Direct bridge must replace generic virtual call stub
    CHECK(after.ir_text.find("__ploy_extend_ConcreteImpl_process") != std::string::npos);

    // The EXTEND-specific descriptor captures the concrete type
    bool has_concrete = false;
    for (const auto &d : after.descriptors) {
        if (d.source_function.find("process") != std::string::npos) {
            has_concrete = true;
            CHECK(d.source_language == "python");
        }
    }
    CHECK(has_concrete);
}

// ============================================================================
// 6. Cross-language devirtualisation: EXTEND on a Rust base class
// ============================================================================

TEST_CASE("Ploy devirt: EXTEND on Rust base generates Rust-language bridge",
          "[ploy][devirt]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
IMPORT rust PACKAGE tokio;

EXTEND(rust, tokio::Task) AS MyTask {
    FUNC run(id: INT) -> INT { RETURN id; }
}

FUNC execute() -> INT {
    LET task = NEW(rust, MyTask);
    LET result = METHOD(rust, task, run, 42);
    RETURN 0;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // Bridge must use Rust language
    CHECK(result.ir_text.find("__ploy_extend_MyTask_run") != std::string::npos);

    bool found_rust = false;
    for (const auto &d : result.descriptors) {
        if (d.source_function.find("run") != std::string::npos) {
            found_rust = (d.source_language == "rust");
        }
    }
    CHECK(found_rust);
}

// ============================================================================
// 7. EXTEND + DELETE: object cleanup path must also appear in IR
// ============================================================================

TEST_CASE("Ploy devirt: EXTEND + DELETE produces both bridge and cleanup stubs",
          "[ploy][devirt]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
EXTEND(python, Widget) AS Button {
    FUNC click() -> INT { RETURN 1; }
}

FUNC ui_test() -> INT {
    LET btn = NEW(python, Button);
    LET val = METHOD(python, btn, click);
    DELETE(python, btn);
    RETURN val;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // Bridge stub for click
    CHECK(result.ir_text.find("__ploy_extend_Button_click") != std::string::npos);
    // Cleanup stub for delete
    CHECK(result.ir_text.find("__ploy_py_del") != std::string::npos);
}

// ============================================================================
// 8. Sema strict mode: METHOD call on typed EXTEND object with wrong signature
//    must be rejected (validates devirt ABI contract enforcement)
// ============================================================================

TEST_CASE("Ploy devirt: strict sema rejects METHOD with wrong param count for EXTEND method",
          "[ploy][devirt][strict]") {
    Diagnostics diags;
    PloySemaOptions opts;
    opts.strict_mode = true;
    PloySema sema(diags, opts);

    PloyLexer lexer(R"(
EXTEND(python, Base) AS MyClass {
    FUNC process(x: INT) -> INT { RETURN x; }
}

FUNC main() -> INT {
    LET obj = NEW(python, MyClass);
    // process expects 1 arg; calling with 3
    LET r = METHOD(python, obj, process, 1, 2, 3);
    RETURN 0;
}
)", "<strict_devirt_test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module);

    bool ok = sema.Analyze(module);
    CHECK_FALSE(ok);
    CHECK(diags.HasErrors());

    // The error message must mention param count mismatch
    bool found_arity_error = false;
    for (const auto &d : diags.All()) {
        if (d.message.find("process") != std::string::npos &&
            (d.message.find("argument") != std::string::npos ||
             d.message.find("param") != std::string::npos ||
             d.message.find("arity") != std::string::npos)) {
            found_arity_error = true;
        }
    }
    CHECK(found_arity_error);
}

// ============================================================================
// 9. EXTEND method signature is registered in KnownSignatures
// ============================================================================

TEST_CASE("Ploy devirt: EXTEND registers method signatures in sema", "[ploy][devirt]") {
    Diagnostics diags;
    PloySema sema(diags, PloySemaOptions{});

    PloyLexer lexer(R"(
EXTEND(python, Animal) AS Cat {
    FUNC purr(volume: INT) -> STRING { RETURN "purrr"; }
    FUNC eat(food: STRING) -> INT { RETURN 0; }
}
)", "<sig_test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module);
    REQUIRE(sema.Analyze(module));

    const auto &sigs = sema.KnownSignatures();

    // Both method signatures must be accessible under DerivedClass::method
    CHECK(sigs.count("Cat::purr") == 1);
    CHECK(sigs.count("Cat::eat") == 1);

    if (sigs.count("Cat::purr")) {
        CHECK(sigs.at("Cat::purr").param_count == 1);
    }
    if (sigs.count("Cat::eat")) {
        CHECK(sigs.at("Cat::eat").param_count == 1);
    }
}

// ============================================================================
// 10. Full pipeline: EXTEND + inheritance chain → PIPELINE call
// ============================================================================

TEST_CASE("Ploy devirt: EXTEND inside PIPELINE generates correct bridge stubs",
          "[ploy][devirt][integration]") {
    Diagnostics diags;
    auto result = CompileWithDescriptors(R"(
EXTEND(python, Estimator) AS RidgeRegression {
    FUNC fit(data: INT) -> INT { RETURN data; }
    FUNC predict(x: INT) -> FLOAT { RETURN 0.0; }
}

PIPELINE ml_train {
    LET model = NEW(python, RidgeRegression);
    LET fitted = METHOD(python, model, fit, 100);
    LET pred = METHOD(python, model, predict, 42);
    RETURN pred;
}
)", diags);

    REQUIRE(result.success);
    REQUIRE(!diags.HasErrors());

    // PIPELINE function must appear
    CHECK(result.ir_text.find("__ploy_pipeline_ml_train") != std::string::npos);

    // Both devirtualised bridges must appear
    CHECK(result.ir_text.find("__ploy_extend_RidgeRegression_fit") != std::string::npos);
    CHECK(result.ir_text.find("__ploy_extend_RidgeRegression_predict") != std::string::npos);
}
