#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "backends/x86_64/include/x86_target.h"
#include "common/include/core/source_loc.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_sema.h"
#include "frontends/cpp/include/cpp_sema.h"
#include "frontends/rust/include/rust_sema.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"

namespace {

struct Settings {
    std::string source{"print('hello')"};
    std::string language{"python"};
    bool pp_cpp{true};
    bool pp_rust{false};
    bool pp_python{false};
    std::vector<std::string> include_paths{"."};
    polyglot::backends::x86_64::RegAllocStrategy regalloc{
        polyglot::backends::x86_64::RegAllocStrategy::kLinearScan};
};

Settings ParseArgs(int argc, char **argv) {
    Settings s;
    bool source_set = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--lang=", 0) == 0) {
            s.language = arg.substr(7);
            continue;
        }
        if (arg.rfind("--regalloc=", 0) == 0) {
            auto mode = arg.substr(11);
            if (mode == "graph" || mode == "graph-coloring" || mode == "coloring") {
                s.regalloc = polyglot::backends::x86_64::RegAllocStrategy::kGraphColoring;
            } else {
                s.regalloc = polyglot::backends::x86_64::RegAllocStrategy::kLinearScan;
            }
            continue;
        }
        if (arg == "--pp-cpp") { s.pp_cpp = true; continue; }
        if (arg == "--no-pp-cpp") { s.pp_cpp = false; continue; }
        if (arg == "--pp-rust") { s.pp_rust = true; continue; }
        if (arg == "--no-pp-rust") { s.pp_rust = false; continue; }
        if (arg == "--pp-python") { s.pp_python = true; continue; }
        if (arg == "--no-pp-python") { s.pp_python = false; continue; }
        if (arg.rfind("--I=", 0) == 0) {
            s.include_paths.push_back(arg.substr(4));
            continue;
        }
        if (arg == "--I" && i + 1 < argc) {
            s.include_paths.push_back(argv[++i]);
            continue;
        }
        if (!source_set) {
            s.source = arg;
            source_set = true;
            continue;
        }
    }
    return s;
}

void ApplyIncludePaths(polyglot::frontends::Preprocessor &pp,
                       const std::vector<std::string> &paths) {
    for (const auto &p : paths) {
        pp.AddIncludePath(p);
    }
}

}  // namespace

int main(int argc, char **argv) {
    Settings settings = ParseArgs(argc, argv);

    polyglot::frontends::Diagnostics diagnostics;
    std::string processed = settings.source;  // default: no preprocessing

    auto run_pp = [&](bool enabled) {
        if (!enabled) return settings.source;
        polyglot::frontends::Preprocessor preprocessor(diagnostics);
        ApplyIncludePaths(preprocessor, settings.include_paths);
        return preprocessor.Process(settings.source, "<cli>");
    };

    if (settings.language == "cpp") {
        processed = run_pp(settings.pp_cpp);
    } else if (settings.language == "rust") {
        processed = run_pp(settings.pp_rust || std::getenv("POLYC_PREPROCESS_RUST"));
    } else if (settings.language == "python") {
        processed = run_pp(settings.pp_python);
    }

    if (settings.language == "python") {
        polyglot::python::PythonLexer lexer(processed, "<cli>", &diagnostics);
        polyglot::python::PythonParser parser(lexer, diagnostics);
        parser.ParseModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::python::AnalyzeModule(*parser.TakeModule(), sema);
    } else if (settings.language == "cpp") {
        polyglot::cpp::CppLexer lexer(processed, "<cli>");
        polyglot::cpp::CppParser parser(lexer, diagnostics);
        parser.ParseModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::cpp::AnalyzeModule(*parser.TakeModule(), sema);
    } else if (settings.language == "rust") {
        polyglot::rust::RustLexer lexer(processed, "<cli>");
        polyglot::rust::RustParser parser(lexer, diagnostics);
        parser.ParseModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::rust::AnalyzeModule(*parser.TakeModule(), sema);
    } else {
        diagnostics.Report(polyglot::core::SourceLoc{"<cli>", 1, 1},
                           "Unknown language: " + settings.language);
    }

    if (diagnostics.HasErrors()) {
        std::cerr << "Compilation failed with " << diagnostics.All().size() << " error(s).\n";
        return 1;
    }

    polyglot::ir::IRContext ir_module;
    auto fn = ir_module.CreateFunction("main");
    auto *entry = fn->CreateBlock("entry");
    auto add = std::make_shared<polyglot::ir::BinaryInstruction>();
    add->op = polyglot::ir::BinaryInstruction::Op::kAdd;
    add->operands = {"2", "3"};
    add->name = "sum";
    add->type = polyglot::ir::IRType::I64();
    entry->AddInstruction(add);

    auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
    ret->operands = {"sum"};
    ret->type = polyglot::ir::IRType::Void();
    entry->SetTerminator(ret);

    polyglot::backends::x86_64::X86Target target(&ir_module);
    target.SetRegAllocStrategy(settings.regalloc);
    std::cout << "Target: " << target.TargetTriple() << "\n";
    std::cout << target.EmitAssembly();
    return 0;
}
