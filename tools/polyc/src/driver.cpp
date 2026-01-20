#include <iostream>
#include <string>

#include "backends/x86_64/include/x86_target.h"
#include "common/include/core/source_loc.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_sema.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

int main(int argc, char **argv) {
  std::string source = "print('hello')";
  std::string language = "python";
  if (argc > 1) {
    source = argv[1];
  }
  if (argc > 2) {
    language = argv[2];
  }

  polyglot::frontends::Diagnostics diagnostics;
  polyglot::frontends::Preprocessor preprocessor(diagnostics);
  preprocessor.AddIncludePath(".");
  std::string processed = preprocessor.Process(source, "<cli>");

  if (language == "python") {
    polyglot::python::PythonLexer lexer(processed, "<cli>", &diagnostics);
    polyglot::python::PythonParser parser(lexer, diagnostics);
    parser.ParseModule();
    polyglot::frontends::SemaContext sema(diagnostics);
    polyglot::python::AnalyzeModule(*parser.TakeModule(), sema);
  } else if (language == "cpp") {
    polyglot::cpp::CppLexer lexer(processed, "<cli>");
    polyglot::cpp::CppParser parser(lexer, diagnostics);
    parser.ParseModule();
  } else if (language == "rust") {
    polyglot::rust::RustLexer lexer(processed, "<cli>");
    polyglot::rust::RustParser parser(lexer, diagnostics);
    parser.ParseModule();
  } else {
    diagnostics.Report(polyglot::core::SourceLoc{"<cli>", 1, 1},
                       "Unknown language: " + language);
  }

  if (diagnostics.HasErrors()) {
    std::cerr << "Compilation failed with " << diagnostics.All().size() << " error(s).\n";
    return 1;
  }

  polyglot::backends::x86_64::X86Target target;
  std::cout << "Target: " << target.TargetTriple() << "\n";
  std::cout << target.EmitAssembly();
  return 0;
}
