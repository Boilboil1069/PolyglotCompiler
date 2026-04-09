// ============================================================================
// Integration Tests â€?Cross-Language Interop Tests
//
// These tests verify cross-language interoperability features:
// LINK chains, NEW/METHOD/DELETE lifecycles, EXTEND dispatch,
// GET/SET attribute access, and WITH resource management patterns.
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>
#include <algorithm>
#include <iostream>

#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/common/include/diagnostics.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_printer.h"

using polyglot::frontends::Diagnostics;
using polyglot::frontends::Token;
using polyglot::frontends::TokenKind;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloyLowering;
using polyglot::ploy::CrossLangCallDescriptor;
using polyglot::ir::IRContext;
using namespace polyglot::ploy;

// ============================================================================
// Helpers
// ============================================================================

namespace {

struct InteropResult {
    std::string ir_text;
    std::vector<CrossLangCallDescriptor> descriptors;
    bool success;
};

InteropResult CompileInterop(const std::string &code, Diagnostics &diags) {
    PloyLexer lexer(code, "<interop>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module || diags.HasErrors()) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return {"", {}, false};
    }

    PloySema sema(diags, PloySemaOptions{});
    if (!sema.Analyze(module)) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return {"", {}, false};
    }

    IRContext ctx;
    PloyLowering lowering(ctx, diags, sema);
    if (!lowering.Lower(module)) {
        for (const auto &d : diags.All()) {
            std::cerr << "[DIAG] " << d.message << "\n";
        }
        return {"", {}, false};
    }

    std::ostringstream oss;
    for (const auto &fn : ctx.Functions()) {
        polyglot::ir::PrintFunction(*fn, oss);
    }
    return {oss.str(), lowering.CallDescriptors(), true};
}

// Count descriptors matching a specific source language
size_t CountDescriptorsForLang(const std::vector<CrossLangCallDescriptor> &descs,
                                const std::string &lang) {
    return std::count_if(descs.begin(), descs.end(),
        [&](const CrossLangCallDescriptor &d) { return d.source_language == lang; });
}

// Check if any descriptor has a specific function name
[[maybe_unused]] bool HasDescriptorForFunc(const std::vector<CrossLangCallDescriptor> &descs,
                           const std::string &func_name) {
    return std::any_of(descs.begin(), descs.end(),
        [&](const CrossLangCallDescriptor &d) {
            return d.source_function.find(func_name) != std::string::npos;
        });
}

} // namespace

// ============================================================================
// LINK Chain Tests
// ============================================================================

TEST_CASE("Interop: single LINK chain between cpp and python", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, math::compute, viz::display) {
    MAP_TYPE(cpp::double, python::float);
}

FUNC demo(x: f64) -> f64 {
    LET result = CALL(cpp, math::compute, x);
    RETURN result;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    REQUIRE(result.descriptors.size() >= 1);
}

TEST_CASE("Interop: multiple LINK declarations", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
LINK(cpp, python, encoder::encode, decoder::decode) {
    MAP_TYPE(cpp::int, python::int);
}

LINK(rust, python, transform::compress, decoder::decode) {
    MAP_TYPE(rust::i32, python::int);
}

FUNC pipeline(data: i32) -> i32 {
    LET encoded = CALL(cpp, encoder::encode, data);
    LET compressed = CALL(rust, transform::compress, data);
    RETURN encoded;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    REQUIRE(result.descriptors.size() >= 2);
}

// ============================================================================
// NEW / METHOD / DELETE Lifecycle Tests
// ============================================================================

TEST_CASE("Interop: NEW creates cross-language object", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE model;

FUNC create_only() -> INT {
    LET obj = NEW(python, model::Net, 10);
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW descriptor
    REQUIRE(result.descriptors.size() >= 1);
    REQUIRE(result.descriptors[0].source_language == "python");
}

TEST_CASE("Interop: NEW then METHOD chain", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE torch;

FUNC method_chain() -> INT {
    LET c = NEW(python, torch::Module);
    LET x = 42;
    LET y = METHOD(python, c, forward, x);
    LET z = METHOD(python, c, backward, y);
    LET sz = METHOD(python, c, parameters);
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW + forward + backward + parameters = 4 descriptors
    REQUIRE(result.descriptors.size() >= 4);
    for (const auto &d : result.descriptors) {
        REQUIRE(d.source_language == "python");
    }
}

TEST_CASE("Interop: full object lifecycle NEW â†?METHOD â†?DELETE", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
FUNC db_lifecycle() -> INT {
    LET conn = NEW(python, db::Connection, "localhost");
    METHOD(python, conn, execute, "SELECT 1");
    LET rows = METHOD(python, conn, fetch_count);
    METHOD(python, conn, close);
    DELETE(python, conn);
    RETURN rows;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW + execute + fetch_count + close + DELETE = 5
    REQUIRE(result.descriptors.size() >= 5);
}

TEST_CASE("Interop: multiple objects different languages", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE plotter;
IMPORT cpp::linalg;

FUNC multi_object() -> INT {
    LET vec = NEW(cpp, linalg::Vec3, 1.0, 2.0, 3.0);
    LET norm = METHOD(cpp, vec, normalize);

    LET plot = NEW(python, plotter::Figure, 800, 600);
    METHOD(python, plot, draw_point, 1.0, 2.0);
    METHOD(python, plot, show);

    DELETE(cpp, vec);
    DELETE(python, plot);
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW*2 + METHOD*3 + DELETE*2 = 7
    REQUIRE(result.descriptors.size() >= 7);
    REQUIRE(CountDescriptorsForLang(result.descriptors, "cpp") >= 3);
    REQUIRE(CountDescriptorsForLang(result.descriptors, "python") >= 3);
}

// ============================================================================
// GET / SET Attribute Access Tests
// ============================================================================

TEST_CASE("Interop: GET reads remote attribute", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE config;

FUNC read_attr() -> INT {
    LET cfg = NEW(python, config::AppConfig, "production");
    LET port = GET(python, cfg, port);
    LET workers = GET(python, cfg, num_workers);
    RETURN port;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW + GET*2
    REQUIRE(result.descriptors.size() >= 3);
}

TEST_CASE("Interop: SET writes remote attribute", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE config;

FUNC write_attr() -> INT {
    LET cfg = NEW(python, config::AppConfig, "staging");
    SET(python, cfg, port, 8080);
    SET(python, cfg, debug, TRUE);
    LET port = GET(python, cfg, port);
    RETURN port;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW + SET*2 + GET
    REQUIRE(result.descriptors.size() >= 4);
}

TEST_CASE("Interop: GET and SET across languages", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT python PACKAGE dashboard;
IMPORT cpp::sensor;

FUNC cross_lang_attrs() -> INT {
    LET sen = NEW(cpp, sensor::TemperatureSensor, 25.0);
    LET temp = GET(cpp, sen, temperature);

    LET dash = NEW(python, dashboard::Display, 1920, 1080);
    SET(python, dash, title, "Sensor Data");
    METHOD(python, dash, update, temp);
    LET brightness = GET(python, dash, brightness);

    DELETE(cpp, sen);
    DELETE(python, dash);
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW*2 + GET*2 + SET + METHOD + DELETE*2 = 8
    REQUIRE(result.descriptors.size() >= 8);
}

// ============================================================================
// WITH Resource Management Tests
// ============================================================================

TEST_CASE("Interop: WITH basic resource management", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
FUNC with_file() -> INT {
    WITH(python, NEW(python, io::File, "test.txt", "r")) AS f {
        LET content = METHOD(python, f, read);
    }
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // NEW + __enter__ + METHOD(read) + __exit__
    REQUIRE(result.descriptors.size() >= 3);
}

TEST_CASE("Interop: nested WITH blocks", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
FUNC nested_with() -> INT {
    WITH(python, NEW(python, io::File, "in.txt", "r")) AS reader {
        WITH(python, NEW(python, io::File, "out.txt", "w")) AS writer {
            LET data = METHOD(python, reader, read);
            METHOD(python, writer, write, data);
        }
    }
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    REQUIRE(result.descriptors.size() >= 6);
}

// ============================================================================
// EXTEND Subclass Dispatch Tests
// ============================================================================

TEST_CASE("Interop: EXTEND basic subclass creation", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
EXTEND(python, base::Shape) AS Circle {
    FUNC area(radius: FLOAT) -> FLOAT {
        RETURN 3.14159 * radius * radius;
    }
}

FUNC use_circle() -> INT {
    LET c = NEW(python, Circle, "circle");
    LET a = METHOD(python, c, area, 5.0);
    DELETE(python, c);
    RETURN 0;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    REQUIRE_FALSE(result.ir_text.empty());
    REQUIRE(result.ir_text.find("area") != std::string::npos);
}

TEST_CASE("Interop: EXTEND with multiple methods", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
EXTEND(python, base::Widget) AS MyButton {
    FUNC on_click(x: INT, y: INT) -> INT {
        RETURN x + y;
    }

    FUNC label() -> STRING {
        RETURN "OK";
    }

    FUNC enabled() -> BOOL {
        RETURN TRUE;
    }
}

FUNC use_button() -> INT {
    LET btn = NEW(python, MyButton, "submit");
    LET result = METHOD(python, btn, on_click, 10, 20);
    LET txt = METHOD(python, btn, label);
    DELETE(python, btn);
    RETURN result;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    REQUIRE(result.ir_text.find("on_click") != std::string::npos);
    REQUIRE(result.ir_text.find("label") != std::string::npos);
    REQUIRE(result.ir_text.find("enabled") != std::string::npos);
}

// ============================================================================
// Complex Interop Combination Tests
// ============================================================================

TEST_CASE("Interop: combined OOP features in single function", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
EXTEND(python, framework::App) AS MyApp {
    FUNC initialize(port: INT) -> INT {
        RETURN port;
    }
}

FUNC app_lifecycle() -> INT {
    LET app = NEW(python, MyApp, "DemoApp");
    SET(python, app, debug, TRUE);
    SET(python, app, port, 3000);

    LET port = METHOD(python, app, initialize, 8080);
    LET is_debug = GET(python, app, debug);

    WITH(python, NEW(python, framework::Session, "user1")) AS session {
        METHOD(python, session, set_data, "key", "value");
        LET val = METHOD(python, session, get_data, "key");
    }

    DELETE(python, app);
    RETURN port;
}
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    // EXTEND bridge + NEW*2 + SET*2 + METHOD*4 + GET + WITH + DELETE = many
    REQUIRE(result.descriptors.size() >= 8);
}

TEST_CASE("Interop: three-language full interop", "[integration][interop]") {
    Diagnostics diags;
    std::string code = R"(
IMPORT cpp::engine;
IMPORT python PACKAGE ai;

LINK(cpp, python, engine::render_frame, ai::decide) {
    MAP_TYPE(cpp::double, python::float);
}

FUNC game_loop(steps: i32) -> i32 {
    LET world = NEW(cpp, engine::World, 1000, 1000);
    LET brain = NEW(python, ai::NeuralBrain, 8, 4);

    VAR step = 0;
    WHILE step < steps {
        LET state = METHOD(cpp, world, get_state);
        LET action = METHOD(python, brain, decide, state);
        METHOD(cpp, world, apply_force, action);
        step = step + 1;
    }

    DELETE(python, brain);
    DELETE(cpp, world);
    RETURN step;
}

EXPORT game_loop AS "run_game";
    )";
    auto result = CompileInterop(code, diags);
    REQUIRE(result.success);
    REQUIRE(result.ir_text.find("game_loop") != std::string::npos);
    REQUIRE(CountDescriptorsForLang(result.descriptors, "cpp") >= 3);
    REQUIRE(CountDescriptorsForLang(result.descriptors, "python") >= 2);
}
