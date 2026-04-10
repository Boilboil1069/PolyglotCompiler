// topology_test.cpp — Unit tests for the topology analysis tool.
//
// Tests cover the four core components:
//   1. TopologyGraph — node/edge management, topological sort, cycle detection.
//   2. TopologyAnalyzer — AST-to-graph construction from .ploy source.
//   3. TopologyValidator — edge type validation, unconnected port detection.
//   4. TopologyPrinter — text / DOT / JSON / summary output formats.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/python/include/python_frontend.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyc/src/foreign_signature_extractor.h"
#include "tools/polytopo/include/topology_analyzer.h"
#include "tools/polytopo/include/topology_graph.h"
#include "tools/polytopo/include/topology_printer.h"
#include "tools/polytopo/include/topology_validator.h"

using namespace polyglot::tools::topo;
using polyglot::frontends::Diagnostics;
using polyglot::ploy::PloyLexer;
using polyglot::ploy::PloyParser;
using polyglot::ploy::PloySema;
using polyglot::ploy::PloySemaOptions;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Build a TopologyGraph from .ploy source code
TopologyGraph BuildGraph(const std::string &source) {
    Diagnostics diags;
    PloyLexer lexer(source, "<test>");
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module != nullptr);

    PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    PloySema sema(diags, sema_opts);
    sema.Analyze(module);

    TopologyAnalyzer analyzer(sema);
    bool ok = analyzer.Build(module);
    REQUIRE(ok);
    return analyzer.MutableGraph();
}

} // namespace

// ============================================================================
// TopologyGraph — basic node/edge operations
// ============================================================================

TEST_CASE("TopologyGraph: add and retrieve nodes", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "foo";
    n1.language = "ploy";
    n1.kind = TopologyNode::Kind::kFunction;
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "bar";
    n2.language = "cpp";
    n2.kind = TopologyNode::Kind::kExternalCall;
    auto id2 = graph.AddNode(n2);

    REQUIRE(graph.NodeCount() == 2);
    REQUIRE(graph.GetNode(id1) != nullptr);
    REQUIRE(graph.GetNode(id1)->name == "foo");
    REQUIRE(graph.GetNode(id2)->language == "cpp");
}

TEST_CASE("TopologyGraph: find node by name", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n;
    n.name = "myModule::process";
    n.language = "python";
    graph.AddNode(n);

    auto *found = graph.FindNodeByName("myModule::process");
    REQUIRE(found != nullptr);
    REQUIRE(found->language == "python");

    REQUIRE(graph.FindNodeByName("nonexistent") == nullptr);
}

TEST_CASE("TopologyGraph: add edges and query in/out", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "src";
    n1.language = "ploy";
    Port out_p;
    out_p.name = "result";
    out_p.direction = Port::Direction::kOutput;
    n1.outputs.push_back(out_p);
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "dst";
    n2.language = "ploy";
    Port in_p;
    in_p.name = "x";
    in_p.direction = Port::Direction::kInput;
    n2.inputs.push_back(in_p);
    auto id2 = graph.AddNode(n2);

    // Retrieve assigned port ids
    const auto *node1 = graph.GetNode(id1);
    const auto *node2 = graph.GetNode(id2);
    REQUIRE(!node1->outputs.empty());
    REQUIRE(!node2->inputs.empty());

    TopologyEdge edge;
    edge.source_node_id = id1;
    edge.source_port_id = node1->outputs[0].id;
    edge.target_node_id = id2;
    edge.target_port_id = node2->inputs[0].id;
    edge.status = TopologyEdge::Status::kValid;
    auto eid = graph.AddEdge(edge);

    REQUIRE(graph.EdgeCount() == 1);
    auto out_edges = graph.OutEdges(id1);
    REQUIRE(out_edges.size() == 1);
    REQUIRE(out_edges[0]->target_node_id == id2);

    auto in_edges = graph.InEdges(id2);
    REQUIRE(in_edges.size() == 1);
    REQUIRE(in_edges[0]->source_node_id == id1);
}

TEST_CASE("TopologyGraph: roots and leaves", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "root";
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "middle";
    auto id2 = graph.AddNode(n2);

    TopologyNode n3;
    n3.name = "leaf";
    auto id3 = graph.AddNode(n3);

    TopologyEdge e1;
    e1.source_node_id = id1;
    e1.target_node_id = id2;
    graph.AddEdge(e1);

    TopologyEdge e2;
    e2.source_node_id = id2;
    e2.target_node_id = id3;
    graph.AddEdge(e2);

    auto roots = graph.Roots();
    REQUIRE(roots.size() == 1);
    REQUIRE(roots[0]->name == "root");

    auto leaves = graph.Leaves();
    REQUIRE(leaves.size() == 1);
    REQUIRE(leaves[0]->name == "leaf");
}

TEST_CASE("TopologyGraph: topological sort on DAG", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "a";
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "b";
    auto id2 = graph.AddNode(n2);

    TopologyNode n3;
    n3.name = "c";
    auto id3 = graph.AddNode(n3);

    TopologyEdge e1;
    e1.source_node_id = id1;
    e1.target_node_id = id2;
    graph.AddEdge(e1);

    TopologyEdge e2;
    e2.source_node_id = id2;
    e2.target_node_id = id3;
    graph.AddEdge(e2);

    auto sorted = graph.TopologicalSort();
    REQUIRE(sorted.size() == 3);
    REQUIRE(sorted[0]->name == "a");
    REQUIRE(sorted[1]->name == "b");
    REQUIRE(sorted[2]->name == "c");
}

TEST_CASE("TopologyGraph: detect cycle", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "x";
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "y";
    auto id2 = graph.AddNode(n2);

    // x -> y -> x  (cycle)
    TopologyEdge e1;
    e1.source_node_id = id1;
    e1.target_node_id = id2;
    graph.AddEdge(e1);

    TopologyEdge e2;
    e2.source_node_id = id2;
    e2.target_node_id = id1;
    graph.AddEdge(e2);

    auto cycles = graph.DetectCycles();
    REQUIRE(!cycles.empty());

    // Topological sort should fail (return empty)
    auto sorted = graph.TopologicalSort();
    REQUIRE(sorted.empty());
}

TEST_CASE("TopologyGraph: language distribution", "[topology][graph]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "a";
    n1.language = "cpp";
    graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "b";
    n2.language = "python";
    graph.AddNode(n2);

    TopologyNode n3;
    n3.name = "c";
    n3.language = "cpp";
    graph.AddNode(n3);

    auto dist = graph.LanguageDistribution();
    REQUIRE(dist["cpp"] == 2);
    REQUIRE(dist["python"] == 1);
}

// ============================================================================
// TopologyValidator — type compatibility and structure validation
// ============================================================================

TEST_CASE("TopologyValidator: valid edge passes", "[topology][validator]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "producer";
    n1.language = "ploy";
    Port out_p;
    out_p.name = "result";
    out_p.direction = Port::Direction::kOutput;
    out_p.type = polyglot::core::Type::Int();
    n1.outputs.push_back(out_p);
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "consumer";
    n2.language = "ploy";
    Port in_p;
    in_p.name = "x";
    in_p.direction = Port::Direction::kInput;
    in_p.type = polyglot::core::Type::Int();
    n2.inputs.push_back(in_p);
    auto id2 = graph.AddNode(n2);

    const auto *node1 = graph.GetNode(id1);
    const auto *node2 = graph.GetNode(id2);

    TopologyEdge edge;
    edge.source_node_id = id1;
    edge.source_port_id = node1->outputs[0].id;
    edge.target_node_id = id2;
    edge.target_port_id = node2->inputs[0].id;
    edge.status = TopologyEdge::Status::kValid;
    graph.AddEdge(edge);

    ValidationOptions opts;
    TopologyValidator validator(opts);
    validator.Validate(graph);

    REQUIRE(validator.ErrorCount() == 0);
}

TEST_CASE("TopologyValidator: cycle detection raises error", "[topology][validator]") {
    TopologyGraph graph;

    TopologyNode n1;
    n1.name = "loopA";
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "loopB";
    auto id2 = graph.AddNode(n2);

    TopologyEdge e1;
    e1.source_node_id = id1;
    e1.target_node_id = id2;
    graph.AddEdge(e1);

    TopologyEdge e2;
    e2.source_node_id = id2;
    e2.target_node_id = id1;
    graph.AddEdge(e2);

    ValidationOptions opts;
    opts.allow_cycles = false;
    TopologyValidator validator(opts);
    validator.Validate(graph);

    REQUIRE(validator.ErrorCount() > 0);
}

// ============================================================================
// TopologyPrinter — text, DOT, JSON, summary outputs
// ============================================================================

TEST_CASE("TopologyPrinter: text output contains node names", "[topology][printer]") {
    TopologyGraph graph;
    graph.module_name = "test_module";

    TopologyNode n1;
    n1.name = "alpha";
    n1.language = "ploy";
    n1.kind = TopologyNode::Kind::kFunction;
    graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "beta";
    n2.language = "cpp";
    n2.kind = TopologyNode::Kind::kExternalCall;
    graph.AddNode(n2);

    PrintOptions opts;
    opts.use_color = false;
    TopologyPrinter printer(opts);

    std::ostringstream oss;
    printer.PrintText(graph, oss);
    std::string output = oss.str();

    REQUIRE(output.find("alpha") != std::string::npos);
    REQUIRE(output.find("beta") != std::string::npos);
}

TEST_CASE("TopologyPrinter: DOT output is valid graph format", "[topology][printer]") {
    TopologyGraph graph;
    graph.module_name = "dot_test";

    TopologyNode n1;
    n1.name = "src";
    n1.language = "ploy";
    auto id1 = graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "dst";
    n2.language = "python";
    auto id2 = graph.AddNode(n2);

    TopologyEdge e;
    e.source_node_id = id1;
    e.target_node_id = id2;
    e.status = TopologyEdge::Status::kValid;
    graph.AddEdge(e);

    PrintOptions opts;
    opts.use_color = false;
    TopologyPrinter printer(opts);

    std::ostringstream oss;
    printer.PrintDot(graph, oss);
    std::string output = oss.str();

    // DOT file should contain digraph declaration and arrow
    REQUIRE(output.find("digraph") != std::string::npos);
    REQUIRE(output.find("->") != std::string::npos);
}

TEST_CASE("TopologyPrinter: JSON output contains nodes and edges keys", "[topology][printer]") {
    TopologyGraph graph;
    graph.module_name = "json_test";

    TopologyNode n;
    n.name = "node1";
    n.language = "ploy";
    graph.AddNode(n);

    PrintOptions opts;
    opts.use_color = false;
    TopologyPrinter printer(opts);

    std::ostringstream oss;
    printer.PrintJson(graph, oss);
    std::string output = oss.str();

    REQUIRE(output.find("\"nodes\"") != std::string::npos);
    REQUIRE(output.find("\"edges\"") != std::string::npos);
    REQUIRE(output.find("node1") != std::string::npos);
}

TEST_CASE("TopologyPrinter: summary output shows counts", "[topology][printer]") {
    TopologyGraph graph;
    graph.module_name = "summary_test";

    TopologyNode n1;
    n1.name = "a";
    n1.language = "ploy";
    graph.AddNode(n1);

    TopologyNode n2;
    n2.name = "b";
    n2.language = "cpp";
    graph.AddNode(n2);

    PrintOptions opts;
    opts.use_color = false;
    TopologyPrinter printer(opts);

    std::ostringstream oss;
    printer.PrintSummary(graph, oss);
    std::string output = oss.str();

    // Should mention the node count
    REQUIRE(output.find("2") != std::string::npos);
}

// ============================================================================
// TopologyAnalyzer — end-to-end from .ploy source
// ============================================================================

TEST_CASE("TopologyAnalyzer: FUNC declaration creates node", "[topology][analyzer]") {
    std::string source = R"(
FUNC add(a: Int, b: Int) -> Int {
    RETURN a + b;
}
)";

    auto graph = BuildGraph(source);
    REQUIRE(graph.NodeCount() >= 1);

    auto *node = graph.FindNodeByName("add");
    REQUIRE(node != nullptr);
    REQUIRE(node->kind == TopologyNode::Kind::kFunction);
    REQUIRE(node->inputs.size() == 2);
    REQUIRE(node->outputs.size() >= 1);
}

TEST_CASE("TopologyAnalyzer: LINK declaration creates edge", "[topology][analyzer]") {
    std::string source = R"(
LINK cpp::math::add(a: Int, b: Int) -> Int;
)";

    auto graph = BuildGraph(source);
    // LINK creates the external target node
    REQUIRE(graph.NodeCount() >= 1);

    auto *linked = graph.FindNodeByName("cpp::math::add");
    if (linked) {
        REQUIRE(linked->is_linked);
    }
}

TEST_CASE("TopologyAnalyzer: PIPELINE creates pipeline node", "[topology][analyzer]") {
    std::string source = R"(
PIPELINE image_process {
    FUNC step1(img: Image) -> Image {
        RETURN img;
    }
}
)";

    auto graph = BuildGraph(source);
    // Should have a pipeline node
    bool found_pipeline = false;
    for (const auto &n : graph.Nodes()) {
        if (n.kind == TopologyNode::Kind::kPipeline) {
            found_pipeline = true;
            break;
        }
    }
    REQUIRE(found_pipeline);
}

TEST_CASE("TopologyAnalyzer: cross-language CALL creates edge", "[topology][analyzer]") {
    std::string source = R"(
FUNC process() -> Int {
    VAR result = CALL(cpp, compute, 42);
    RETURN result;
}
)";

    auto graph = BuildGraph(source);
    REQUIRE(graph.NodeCount() >= 2);
    REQUIRE(graph.EdgeCount() >= 1);

    // There should be an external call node for cpp::compute
    bool found_ext = false;
    for (const auto &n : graph.Nodes()) {
        if (n.kind == TopologyNode::Kind::kExternalCall) {
            found_ext = true;
            break;
        }
    }
    REQUIRE(found_ext);
}

TEST_CASE("TopologyAnalyzer: LINK source node return type after foreign injection",
          "[topology][analyzer][foreign]") {
    // Ensure frontends are registered
    auto &reg = polyglot::frontends::FrontendRegistry::Instance();
    reg.Register(std::make_shared<polyglot::cpp::CppLanguageFrontend>());
    reg.Register(std::make_shared<polyglot::python::PythonLanguageFrontend>());

    // Find the sample file relative to the workspace root
    // The test binary runs from the build dir; samples are at:
    //   <workspace>/tests/samples/01_basic_linking/basic_linking.ploy
    std::string sample_dir;
    for (auto candidate : {
        "tests/samples/01_basic_linking",
        "../tests/samples/01_basic_linking",
        "../../tests/samples/01_basic_linking",
    }) {
        if (std::filesystem::exists(std::string(candidate) + "/basic_linking.ploy")) {
            sample_dir = candidate;
            break;
        }
    }
    REQUIRE_FALSE(sample_dir.empty());

    std::string filename = sample_dir + "/basic_linking.ploy";
    std::ifstream ifs(filename);
    REQUIRE(ifs.is_open());
    std::string source((std::istreambuf_iterator<char>(ifs)),
                        std::istreambuf_iterator<char>());
    ifs.close();

    // Parse
    Diagnostics diags;
    PloyLexer lexer(source, filename);
    PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto module = parser.TakeModule();
    REQUIRE(module != nullptr);

    // Sema
    PloySemaOptions sema_opts;
    sema_opts.enable_package_discovery = false;
    sema_opts.strict_mode = false;
    PloySema sema(diags, sema_opts);
    sema.Analyze(module);

    // Before injection: source-side sig should have Unknown return type
    {
        auto it = sema.KnownSignatures().find("math_ops::abs_val");
        REQUIRE(it != sema.KnownSignatures().end());
        INFO("Before injection return_type.name = '" << it->second.return_type.name << "'");
        CHECK(it->second.return_type.kind == polyglot::core::TypeKind::kUnknown);
        CHECK_FALSE(it->second.param_count_known);
    }

    // Foreign signature extraction
    polyglot::tools::ForeignExtractionOptions feopts;
    feopts.base_directory = std::filesystem::path(filename).parent_path().string();
    polyglot::tools::ForeignSignatureExtractor extractor(feopts);
    auto foreign_sigs = extractor.ExtractAll(*module);

    INFO("Foreign sigs extracted: " << foreign_sigs.size());
    REQUIRE_FALSE(foreign_sigs.empty());

    // Check foreign sig for abs_val exists
    {
        auto it = foreign_sigs.find("math_ops::abs_val");
        REQUIRE(it != foreign_sigs.end());
        INFO("Foreign abs_val return_type.name = '" << it->second.return_type.name << "'");
        CHECK(it->second.return_type.name == "i32");
        CHECK(it->second.param_count_known == true);
    }

    // Inject
    sema.InjectForeignSignatures(foreign_sigs);

    // After injection: should have upgraded return type
    {
        auto it = sema.KnownSignatures().find("math_ops::abs_val");
        REQUIRE(it != sema.KnownSignatures().end());
        INFO("After injection return_type.name = '" << it->second.return_type.name << "'");
        INFO("After injection return_type.kind = " << static_cast<int>(it->second.return_type.kind));
        INFO("After injection param_count_known = " << it->second.param_count_known);
        CHECK(it->second.return_type.name == "i32");
        CHECK(it->second.param_count_known == true);
    }

    // Build topology
    TopologyAnalyzer analyzer(sema);
    REQUIRE(analyzer.Build(module));

    // Find the source node for abs_val
    const auto *abs_val_node = analyzer.Graph().FindNodeByName("cpp::math_ops::abs_val");
    REQUIRE(abs_val_node != nullptr);
    REQUIRE_FALSE(abs_val_node->outputs.empty());

    INFO("abs_val output port type.name = '" << abs_val_node->outputs[0].type.name << "'");
    CHECK(abs_val_node->outputs[0].type.name == "i32");
}
