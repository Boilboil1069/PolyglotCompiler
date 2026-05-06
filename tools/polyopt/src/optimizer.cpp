/**
 * @file     optimizer.cpp
 * @brief    Optimisation tool
 *
 * @ingroup  Tool / polyopt
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/ir_parser.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/passes/pass_manager.h"
#include "common/include/target_triple.h"

namespace polyglot::tools {

void Optimize(ir::IRContext &context, int opt_level) {
  if (opt_level <= 0)
    return;
  passes::PassManager pm(static_cast<passes::PassManager::OptLevel>(opt_level));
  pm.Build();
  pm.RunOnModule(context, /*verbose=*/false);
}

} // namespace polyglot::tools

static void PrintUsage(const char *argv0) {
  std::cerr << "Usage: " << argv0 << " [options] <input.ir> [-o <output.ir>]\n";
  std::cerr << "Options:\n";
  std::cerr << "  -o <file>    Write optimised IR to <file> (default: stdout)\n";
  std::cerr << "  -O0          No optimisation\n";
  std::cerr << "  -O1          Basic optimisations (constant fold + DCE)\n";
  std::cerr << "  -O2          Standard optimisations (+ CSE + inlining) [default]\n";
  std::cerr << "  -O3          Aggressive optimisations (loop, vectorisation, etc.)\n";
  std::cerr << "  --target=<t> Target triple (BIN-7), e.g. x86_64-unknown-linux-gnu;\n";
  std::cerr << "               defaults to the host triple.  Emitted as a\n";
  std::cerr << "               `; target-triple:` header in the optimised IR.\n";
  std::cerr << "  --help       Show this message\n";
}

int main(int argc, char *argv[]) {
  std::string input_file;
  std::string output_file;
  int opt_level = 2;
  // BIN-7: own resolved target triple.  Defaults to the host triple
  // and is overridable via `--target=<spec>`.
  ::polyglot::common::TargetTriple target_triple = ::polyglot::common::HostTriple();
  bool target_explicit = false;

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
    } else if (arg.rfind("--target=", 0) == 0) {
      std::string spec = arg.substr(9);
      auto parsed = ::polyglot::common::ParseTargetTriple(spec);
      if (!parsed.ok()) {
        std::cerr << "[error] --target=" << spec
                  << ": polyopt-err-E1100 "
                  << (parsed.error ? parsed.error->message : "invalid triple") << "\n";
        return 1;
      }
      target_triple = *parsed.triple;
      target_explicit = true;
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

  // BIN-7: detect upstream `; target-triple:` annotation and warn when
  // it disagrees with our own resolved triple.
  {
    std::istringstream iss(ir_text);
    std::string line;
    int peeked = 0;
    const std::string marker = "; target-triple:";
    while (peeked < 16 && std::getline(iss, line)) {
      ++peeked;
      auto pos = line.find(marker);
      if (pos == std::string::npos) continue;
      std::string spec = line.substr(pos + marker.size());
      auto a = spec.find_first_not_of(" \t\r");
      auto b = spec.find_last_not_of(" \t\r");
      if (a == std::string::npos) break;
      spec = spec.substr(a, b - a + 1);
      auto parsed = ::polyglot::common::ParseTargetTriple(spec);
      if (parsed.ok() && *parsed.triple != target_triple) {
        std::cerr << "[warn] polyopt-warn-W1101 input target triple '"
                  << parsed.triple->str() << "' differs from polyopt target '"
                  << target_triple.str() << "' in " << input_file << "\n";
      } else if (parsed.ok() && !target_explicit) {
        // Adopt the upstream triple when the user did not override it
        // explicitly so the emitted header round-trips faithfully.
        target_triple = *parsed.triple;
      }
      break;
    }
  }

  // Build an IRContext by parsing the textual IR representation.
  polyglot::ir::IRContext ctx;
  std::string parse_error;
  if (!polyglot::ir::ParseModule(ir_text, ctx, &parse_error)) {
    std::cerr << "[error] failed to parse IR: " << parse_error << "\n";
    return 1;
  }

  // Run optimisation passes via PassManager according to the requested level.
  polyglot::tools::Optimize(ctx, opt_level);

  // Emit the optimised IR.
  std::ostringstream ir_out;
  // BIN-7: prepend a `; target-triple:` header so polyasm and other
  // downstream tools can detect mismatches.
  ir_out << "; target-triple: " << target_triple.str() << "\n";
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

  std::cerr << "[info] optimised (" << ctx.Functions().size() << " functions, -O" << opt_level
            << ")\n";
  return 0;
}
