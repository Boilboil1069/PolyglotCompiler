#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "middle/include/ir/ir_context.h"
#include "middle/include/passes/transform/common_subexpr.h"
#include "middle/include/passes/transform/constant_fold.h"
#include "middle/include/passes/transform/dead_code_elim.h"
#include "middle/include/passes/transform/inlining.h"
#include "common/include/ir/ir_parser.h"
#include "common/include/ir/ir_printer.h"

namespace polyglot::tools {

void Optimize(ir::IRContext &context) {
  passes::transform::RunConstantFold(context);
  passes::transform::RunDeadCodeElimination(context);
  passes::transform::RunCommonSubexpressionElimination(context);
  passes::transform::RunInlining(context);
}

}  // namespace polyglot::tools

static void PrintUsage(const char *argv0) {
  std::cerr << "Usage: " << argv0 << " [options] <input.ir> [-o <output.ir>]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -o <file>    Write optimised IR to <file> (default: stdout)\n";
  std::cerr << "  -O0          No optimisation\n";
  std::cerr << "  -O1          Basic optimisations (constant fold + DCE)\n";
  std::cerr << "  -O2          Standard optimisations (+ CSE + inlining) [default]\n";
  std::cerr << "  -O3          Aggressive optimisations (same as -O2 for now)\n";
  std::cerr << "  --help       Show this message\n";
}

int main(int argc, char *argv[]) {
  std::string input_file;
  std::string output_file;
  int opt_level = 2;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    } else if (arg == "-o" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-O0") {
      opt_level = 0;
    } else if (arg == "-O1") {
      opt_level = 1;
    } else if (arg == "-O2") {
      opt_level = 2;
    } else if (arg == "-O3") {
      opt_level = 3;
    } else if (arg[0] != '-') {
      input_file = arg;
    } else {
      std::cerr << "[error] unknown option: " << arg << "\n";
      return 1;
    }
  }

  if (input_file.empty()) {
    std::cerr << "[error] no input file specified\n";
    PrintUsage(argv[0]);
    return 1;
  }

  // Read input IR text.
  std::ifstream in(input_file);
  if (!in) {
    std::cerr << "[error] cannot open input file: " << input_file << "\n";
    return 1;
  }
  std::stringstream buf;
  buf << in.rdbuf();
  std::string ir_text = buf.str();
  in.close();

  // Build an IRContext by parsing the textual IR representation.
  polyglot::ir::IRContext ctx;
  std::string parse_error;
  if (!polyglot::ir::ParseModule(ir_text, ctx, &parse_error)) {
    std::cerr << "[error] failed to parse IR: " << parse_error << "\n";
    return 1;
  }

  // Run optimisation passes according to the requested level.
  if (opt_level >= 1) {
    polyglot::passes::transform::RunConstantFold(ctx);
    polyglot::passes::transform::RunDeadCodeElimination(ctx);
  }
  if (opt_level >= 2) {
    polyglot::passes::transform::RunCommonSubexpressionElimination(ctx);
    polyglot::passes::transform::RunInlining(ctx);
  }

  // Emit the optimised IR.
  std::ostringstream ir_out;
  polyglot::ir::PrintModule(ctx, ir_out);
  std::string output_text = ir_out.str();

  if (output_file.empty()) {
    std::cout << output_text;
  } else {
    std::ofstream out(output_file);
    if (!out) {
      std::cerr << "[error] cannot open output file: " << output_file << "\n";
      return 1;
    }
    out << output_text;
  }

  std::cerr << "[info] optimised (" << ctx.Functions().size() << " functions, -O"
            << opt_level << ")\n";
  return 0;
}
