/**
 * @file     polytopo.cpp
 * @brief    Topology graph implementation
 *
 * @ingroup  Tool / polytopo
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
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
#include "tools/polytopo/include/topology_codegen.h"
#include "tools/polytopo/include/topology_graph.h"
#include "tools/polytopo/include/topology_printer.h"
#include "common/include/version.h"
#include "tools/polytopo/include/topology_validator.h"

namespace polyglot::tools {

// ============================================================================
// Constants
// ============================================================================

constexpr const char *kTopoVersion = POLYGLOT_VERSION_STRING;
constexpr const char *kTopoName = POLYGLOT_POLYTOPO_NAME;

// ============================================================================
// CLI Options
// ============================================================================

struct TopoOptions {
    std::string input_file;
    std::string output_file;       // empty = stdout
    std::string format{"text"};    // text, dot, json, summary
    std::string subcommand;        // empty = default analysis, "generate" = code gen
    std::string view_mode;         // empty = show all; "link" or "call"
    std::string filter_language;   // empty = show all; language name to filter by
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
        << "  polytopo <file.ploy> [options]\n"
        << "  polytopo generate <topo.json> -o <output.ploy>\n\n"
        << "Subcommands:\n"
        << "  generate <topo.json>              Generate .ploy source from a JSON\n"
        << "                                    topology graph (produced by --format json)\n\n"
        << "Options:\n"
        << "  --format <text|dot|json|summary>  Output format (default: text)\n"
        << "  --validate                        Run validation checks and report\n"
        << "  --strict                          Treat Any-typed edges as errors\n"
        << "  --no-color                        Disable ANSI color output\n"
        << "  --show-locations                  Show source file locations\n"
        << "  --compact                         Compact output\n"
        << "  --allow-cycles                    Do not error on cycles\n"
        << "  --dot-horizontal                  DOT: use left-to-right layout\n"
        << "  --view-mode <link|call>           Filter edges by origin (link or call)\n"
        << "  --filter-language <lang>          Show only nodes of a specific language\n"
        << "  --output, -o <file>               Write output to file\n"
        << "  --help                            Show this help message\n"
        << "  --version                         Show version\n\n"
        << "Examples:\n"
        << "  polytopo my_project.ploy\n"
        << "  polytopo my_project.ploy --format dot --output graph.dot\n"
        << "  polytopo my_project.ploy --validate --strict\n"
        << "  polytopo my_project.ploy --format json --output graph.json\n"
        << "  polytopo generate graph.json -o generated.ploy\n";
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
        } else if (arg == "generate" && opts.subcommand.empty() &&
                   opts.input_file.empty()) {
            opts.subcommand = "generate";
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
        } else if (arg == "--view-mode" && i + 1 < argc) {
            opts.view_mode = argv[++i];
        } else if (arg == "--filter-language" && i + 1 < argc) {
            opts.filter_language = argv[++i];
        } else if (arg[0] != '-' && opts.input_file.empty()) {
            opts.input_file = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
        }
    }
    return opts;
}

// ============================================================================
// Generate subcommand — converts JSON topology graph to .ploy source
// ============================================================================

static int RunGenerate(const TopoOptions &opts) {
    if (opts.input_file.empty()) {
        std::cerr << "Error: No input JSON file specified.\n"
                  << "Usage: polytopo generate <topo.json> -o <output.ploy>\n";
        return 1;
    }

    if (!std::filesystem::exists(opts.input_file)) {
        std::cerr << "Error: File not found: " << opts.input_file << "\n";
        return 1;
    }

    // Read input JSON
    std::ifstream ifs(opts.input_file);
    if (!ifs.is_open()) {
        std::cerr << "Error: Cannot open file: " << opts.input_file << "\n";
        return 1;
    }
    std::string json_str((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());
    ifs.close();

    std::cerr << "[1/3] Parsing JSON topology graph...\n";

    // Parse JSON into TopologyGraph
    topo::TopologyGraph graph;
    if (!topo::ParseJsonToGraph(json_str, graph)) {
        std::cerr << "Error: Failed to parse JSON topology graph.\n";
        return 1;
    }

    std::cerr << "[2/3] Generating .ploy source ("
              << graph.NodeCount() << " nodes, "
              << graph.EdgeCount() << " edges)...\n";

    // Generate .ploy source
    std::string ploy_src = topo::GeneratePloySrc(graph);

    // Verify generated source is parseable
    std::cerr << "[3/3] Verifying generated source...\n";
    {
        frontends::Diagnostics verify_diags;
        ploy::PloyLexer vlex(ploy_src, "<generated>");
        ploy::PloyParser vparser(vlex, verify_diags);
        vparser.ParseModule();
        auto vmod = vparser.TakeModule();
        if (!vmod) {
            std::cerr << "Warning: generated code has parse errors:\n";
            for (const auto &d : verify_diags.All()) {
                std::cerr << "  " << d.message << "\n";
            }
        } else {
            ploy::PloySemaOptions sema_opts;
            sema_opts.enable_package_discovery = false;
            sema_opts.strict_mode = false;
            ploy::PloySema sema(verify_diags, sema_opts);
            sema.Analyze(vmod);
        }
    }

    // Write output
    if (opts.output_file.empty()) {
        std::cout << ploy_src;
    } else {
        std::ofstream ofs(opts.output_file);
        if (!ofs.is_open()) {
            std::cerr << "Error: Cannot open output file: "
                      << opts.output_file << "\n";
            return 1;
        }
        ofs << ploy_src;
        ofs.close();
        std::cerr << "Generated: " << opts.output_file << "\n";
    }

    return 0;
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

    // Apply --view-mode filter: remove edges that do not match the mode
    if (!opts.view_mode.empty()) {
        topo::TopologyEdge::Origin target_origin;
        bool valid_mode = true;
        if (opts.view_mode == "link") {
            target_origin = topo::TopologyEdge::Origin::kLink;
        } else if (opts.view_mode == "call") {
            target_origin = topo::TopologyEdge::Origin::kCall;
        } else {
            std::cerr << "Error: Unknown view-mode: " << opts.view_mode
                      << " (expected 'link' or 'call')\n";
            valid_mode = false;
        }
        if (valid_mode) {
            graph.RemoveEdgesIf([&](const topo::TopologyEdge &e) {
                return e.origin != target_origin;
            });
            // Remove orphan nodes (no remaining edges)
            std::unordered_set<uint64_t> connected;
            for (const auto &e : graph.Edges()) {
                connected.insert(e.source_node_id);
                connected.insert(e.target_node_id);
            }
            graph.RemoveNodesIf([&](const topo::TopologyNode &n) {
                return connected.find(n.id) == connected.end();
            });
        }
    }

    // Apply --filter-language: remove nodes whose language does not match
    if (!opts.filter_language.empty()) {
        graph.RemoveNodesIf([&](const topo::TopologyNode &n) {
            return !n.language.empty() && n.language != opts.filter_language;
        });
        // Remove edges whose endpoints were removed
        graph.RemoveEdgesIf([&](const topo::TopologyEdge &e) {
            return !graph.GetNode(e.source_node_id) ||
                   !graph.GetNode(e.target_node_id);
        });
    }

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

    if (opts.subcommand == "generate") {
        return polyglot::tools::RunGenerate(opts);
    }

    return polyglot::tools::Run(opts);
}
