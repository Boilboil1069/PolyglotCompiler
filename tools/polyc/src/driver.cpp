#include <iostream>
#include <string>

#include "backends/x86_64/include/x86_target.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"

int main(int argc, char **argv) {
  std::string source = "print('hello')";
  if (argc > 1) {
    source = argv[1];
  }

  polyglot::frontends::Diagnostics diagnostics;
  polyglot::python::PythonLexer lexer(source, "<cli>");
  polyglot::python::PythonParser parser(lexer, diagnostics);
  parser.ParseModule();

  if (diagnostics.HasErrors()) {
    std::cerr << "Compilation failed with " << diagnostics.All().size() << " error(s).\n";
    return 1;
  }

  polyglot::backends::x86_64::X86Target target;
  std::cout << "Target: " << target.TargetTriple() << "\n";
  std::cout << target.EmitAssembly();
  return 0;
}
