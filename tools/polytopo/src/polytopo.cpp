// polytopo — Polyglot Topology Analysis Tool
//
// Parses .ploy files, builds a function-level I/O topology graph,
// validates type compatibility across language boundaries, and outputs
// the graph in various formats (text, DOT, JSON).
//
// Usage:
//   polytopo <file.ploy> [options]
//
// Options:
//   --format <text|dot|json|summary>  Output format (default: text)
//   --validate                        Run validation checks
//   --strict                          Treat Any-typed edges as errors
//   --no-color                        Disable ANSI color output
//   --show-locations                  Show source locations
//   --output <file>                   Write output to file
//   --help                            Show help
//   --version                         Show version

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polytopo/include/topology_analyzer.h"
#include "tools/polytopo/include/topology_graph.h"
#include "tools/polytopo/include/topology_printer.h"
#include "tools/polytopo/include/topology_validator.h"

namespace polyglot::tools {

// ============================================================================
// Constants
// ============================================================================

constexpr const char *kTopoVersion = "1.0.0";
constexpr const char *kTopoName = "polytopo";

// ============================================================================
// CLI Options
// ============================================================================

struct TopoOptions {
    std::string input_file;
    std::string output_file;       // empty = stdout
    std::string format{"text"};    // text, dot, json, summary
    bool validate{false};
    bool strict{false};
    bool use_color{true};
    bool show_locations{false};
    bool show_help{false};
    bool show_version{false};
    bool compact{false};
    bool allow_cycles{false};
    bool dot_horizontal{false};
};

// ============================================================================
// Help / Version
// ============================================================================

static void PrintHelp() {
    std::cout
        << "polytopo — Polyglot Topology Analysis Tool v" << kTopoVersion << "\n\n"
        << "Usage:\n"
        << "  polytopo <file.ploy> [options]\n\n"
        << "Options:\n"
        << "  --format <text|dot|json|summary>  Output format (default: text)\n"
        << "  --validate                        Run validation checks and report\n"
        << "  --strict                          Treat Any-typed edges as errors\n"
        << "  --no-color                        Disable ANSI color output\n"
        << "  --show-locations                  Show source file locations\n"
        << "  --compact                         Compact output\n"
        << "  --allow-cycles                    Do not error on cycles\n"
        << "  --dot-horizontal                  DOT: use left-to-right layout\n"
        << "  --output <file>                   Write output to file\n"
        << "  --help                            Show this help message\n"
        << "  --version                         Show version\n\n"
        << "Examples:\n"
        << "  polytopo my_project.ploy\n"
        << "  polytopo my_project.ploy --format dot --output graph.dot\n"
        << "  polytopo my_project.ploy --validate --strict\n"
        << "  polytopo my_project.ploy --format json --output graph.json\n";
}

static void PrintVersion() {
    std::cout << kTopoName << " version " << kTopoVersion << "\n";
}

// ============================================================================
// Argument parsing
// ============================================================================

static TopoOptions ParseArgs(int argc, char *argv[]) {
    TopoOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opts.show_help = true;
        } else if (arg == "--version" || arg == "-v") {
            opts.show_version = true;
        } else if (arg == "--format" && i + 1 < argc) {
            opts.format = argv[++i];
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc) opts.output_file = argv[++i];
        } else if (arg == "--validate") {
            opts.validate = true;
        } else if (arg == "--strict") {
            opts.strict = true;
            opts.validate = true;  // strict implies validate
        } else if (arg == "--no-color") {
            opts.use_color = false;
        } else if (arg == "--show-locations") {
            opts.show_locations = true;
        } else if (arg == "--compact") {
            opts.compact = true;
        } else if (arg == "--allow-cycles") {
            opts.allow_cycles = true;
        } else if (arg == "--dot-horizontal") {
            opts.dot_horizontal = true;
        } else if (arg[0] != '-' && opts.input_file.empty()) {
            opts.input_file = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
        }
    }
    return opts;
}

// ============================================================================
// Main pipeline
// ============================================================================

static int Run(const TopoOptions &opts) {
    // Read input file
    if (opts.input_file.empty()) {
        std::cerr << "Error: No input file specified. Use --help for usage.\n";
        return 1;
    }

    if (!std::filesystem::exists(opts.input_file)) {
        std::cerr << "Error: File not found: " << opts.input_file << "\n";
        return 1;
    }

    std::ifstream ifs(opts.input_file);
    if (!ifs.is_open()) {
        std::cerr << "Error: Cannot open file: " << opts.input_file << "\n";
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    std::string filename = std::filesystem::path(opts.input_file).filename().string();

    // Progress output
    std::cerr << "[1/5] Lexing " << filename << "...\n";

    // Step 1: Lex
    frontends::Diagnostics diagnostics;
    ploy::PloyLexer lexer(source, filename);

    // Collect all tokens
    std::vector<frontends::Token> tokens;
    while (true) {
        auto tok = lexer.NextToken();
        tokens.push_back(tok);
        if (tok.kind == frontends::TokenKind::kEndOfFile) break;
    }

    if (tokens.size() <= 1) {
        std::cerr << "Error: Lexer produced no meaningful tokens.\n";
        return 1;
    }

    // Step 2: Parse
    std::cerr << "[2/5] Parsing...\n";
    // Re-create lexer for the parser (parser expects to drive the lexer)
    ploy::PloyLexer parse_lexer(source, filename);
    ploy::PloyParser parser(parse_lexer, diagnostics);
    parser.ParseModule();
    auto module = parser.TakeModule();

    if (!module) {
        std::cerr << "Error: Parser failed to produce AST.\n";
        for (const auto &diag : diagnostics.All()) {
            std::cerr << "  " << diag.message << "\n";
        }
        return 1;
    }

    // Step 3: Semantic analysis
    std::cerr << "[3/5] Semantic analysis...\n";
    ploy::PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    sema_opts.strict_mode = opts.strict;
    ploy::PloySema sema(diagnostics, sema_opts);
    sema.Analyze(module);

    // Step 4: Build topology graph
    std::cerr << "[4/5] Building topology graph...\n";
    topo::TopologyAnalyzer analyzer(sema);
    if (!analyzer.Build(module)) {
        std::cerr << "Error: Failed to build topology graph.\n";
        return 1;
    }

    auto &graph = analyzer.MutableGraph();
    graph.source_file = opts.input_file;
    graph.module_name = filename;

    // Step 5: Validate (if requested)
    std::vector<topo::ValidationDiagnostic> validation_diags;
    if (opts.validate) {
        std::cerr << "[5/5] Validating topology...\n";
        topo::ValidationOptions val_opts;
        val_opts.strict_any = opts.strict;
        val_opts.allow_cycles = opts.allow_cycles;
        topo::TopologyValidator validator(val_opts);
        validator.Validate(graph);
        validation_diags = validator.Diagnostics();
    } else {
        std::cerr << "[5/5] Skipping validation (use --validate to enable).\n";
    }

    // Output
    topo::PrintOptions print_opts;
    print_opts.use_color = opts.use_color;
    print_opts.show_types = true;
    print_opts.show_edge_status = true;
    print_opts.show_locations = opts.show_locations;
    print_opts.compact = opts.compact;
    print_opts.dot_horizontal = opts.dot_horizontal;
    topo::TopologyPrinter printer(print_opts);

    // Select output stream
    std::ofstream ofs_out;
    std::ostream *out_stream = &std::cout;
    if (!opts.output_file.empty()) {
        ofs_out.open(opts.output_file);
        if (!ofs_out.is_open()) {
            std::cerr << "Error: Cannot open output file: " << opts.output_file << "\n";
            return 1;
        }
        out_stream = &ofs_out;
    }

    if (opts.format == "text") {
        printer.PrintText(graph, *out_stream);
        if (!validation_diags.empty()) {
            printer.PrintDiagnostics(validation_diags, *out_stream);
        }
    } else if (opts.format == "dot") {
        printer.PrintDot(graph, *out_stream);
    } else if (opts.format == "json") {
        printer.PrintJson(graph, *out_stream);
    } else if (opts.format == "summary") {
        printer.PrintSummary(graph, *out_stream);
        if (!validation_diags.empty()) {
            *out_stream << "\n";
            printer.PrintDiagnostics(validation_diags, *out_stream);
        }
    } else {
        std::cerr << "Error: Unknown format: " << opts.format << "\n";
        return 1;
    }

    // Print summary to stderr
    std::cerr << "\nTopology: " << graph.NodeCount() << " nodes, "
              << graph.EdgeCount() << " edges";
    if (opts.validate) {
        size_t errors = 0, warnings = 0;
        for (const auto &d : validation_diags) {
            if (d.severity == topo::ValidationDiagnostic::Severity::kError) errors++;
            if (d.severity == topo::ValidationDiagnostic::Severity::kWarning) warnings++;
        }
        std::cerr << ", " << errors << " error(s), " << warnings << " warning(s)";
    }
    std::cerr << "\n";

    // Return non-zero if validation errors found
    if (opts.validate) {
        for (const auto &d : validation_diags) {
            if (d.severity == topo::ValidationDiagnostic::Severity::kError) {
                return 1;
            }
        }
    }

    return 0;
}

} // namespace polyglot::tools

// ============================================================================
// main
// ============================================================================

int main(int argc, char *argv[]) {
    auto opts = polyglot::tools::ParseArgs(argc, argv);

    if (opts.show_help) {
        polyglot::tools::PrintHelp();
        return 0;
    }
    if (opts.show_version) {
        polyglot::tools::PrintVersion();
        return 0;
    }

    return polyglot::tools::Run(opts);
}
