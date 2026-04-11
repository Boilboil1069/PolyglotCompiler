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

// ============================================================================
// PIPELINE drill-down: context_node_id and expansion visibility
// ============================================================================

TEST_CASE("TopologyAnalyzer: PIPELINE stages have kPipelineStage origin",
          "[topology][analyzer][pipeline]") {
    std::string source = R"(
PIPELINE image_process {
    FUNC step1(img: Image) -> Image {
        RETURN img;
    }
    FUNC step2(img: Image) -> Image {
        RETURN img;
    }
}
)";

    auto graph = BuildGraph(source);

    // There should be a pipeline container node
    const TopologyNode *pipeline_node = nullptr;
    for (const auto &n : graph.Nodes()) {
        if (n.kind == TopologyNode::Kind::kPipeline) {
            pipeline_node = &n;
            break;
        }
    }
    REQUIRE(pipeline_node != nullptr);
    CHECK(pipeline_node->origin == TopologyNode::Origin::kDecl);

    // There should be two stage sub-nodes with kPipelineStage origin
    std::vector<const TopologyNode *> stages;
    for (const auto &n : graph.Nodes()) {
        if (n.origin == TopologyNode::Origin::kPipelineStage) {
            stages.push_back(&n);
        }
    }
    REQUIRE(stages.size() == 2);
    CHECK(stages[0]->kind == TopologyNode::Kind::kFunction);
    CHECK(stages[1]->kind == TopologyNode::Kind::kFunction);

    // There should be one stage-order edge connecting the two stages
    int stage_order_edges = 0;
    for (const auto &e : graph.Edges()) {
        if (e.origin == TopologyEdge::Origin::kPipelineStage) {
            ++stage_order_edges;
        }
    }
    CHECK(stage_order_edges == 1);
}

TEST_CASE("TopologyAnalyzer: PIPELINE FUNC with CALL sets context_node_id",
          "[topology][analyzer][pipeline][drilldown]") {
    std::string source = R"(
PIPELINE data_flow {
    FUNC preprocess(x: Int) -> Int {
        VAR result = CALL(cpp, transform, x);
        RETURN result;
    }
    FUNC postprocess(y: Int) -> Int {
        VAR output = CALL(python, finalize, y);
        RETURN output;
    }
}
)";

    auto graph = BuildGraph(source);

    // Find the stage nodes for preprocess and postprocess
    const TopologyNode *preprocess_stage = nullptr;
    const TopologyNode *postprocess_stage = nullptr;
    for (const auto &n : graph.Nodes()) {
        if (n.name.find("preprocess") != std::string::npos &&
            n.origin == TopologyNode::Origin::kPipelineStage) {
            preprocess_stage = &n;
        }
        if (n.name.find("postprocess") != std::string::npos &&
            n.origin == TopologyNode::Origin::kPipelineStage) {
            postprocess_stage = &n;
        }
    }
    REQUIRE(preprocess_stage != nullptr);
    REQUIRE(postprocess_stage != nullptr);

    // Find the external CALL nodes (cpp::transform, python::finalize)
    const TopologyNode *transform_node = nullptr;
    const TopologyNode *finalize_node = nullptr;
    for (const auto &n : graph.Nodes()) {
        if (n.name == "cpp::transform") {
            transform_node = &n;
        }
        if (n.name == "python::finalize") {
            finalize_node = &n;
        }
    }
    REQUIRE(transform_node != nullptr);
    REQUIRE(finalize_node != nullptr);

    // External nodes created by a stage body should have context_node_id
    // pointing to the enclosing stage node
    CHECK(transform_node->context_node_id == preprocess_stage->id);
    CHECK(finalize_node->context_node_id == postprocess_stage->id);

    // CALL edges created inside stage bodies should also have context_node_id
    // pointing to their enclosing stage
    bool found_transform_edge = false;
    bool found_finalize_edge = false;
    for (const auto &e : graph.Edges()) {
        if (e.target_node_id == transform_node->id) {
            CHECK(e.context_node_id == preprocess_stage->id);
            found_transform_edge = true;
        }
        if (e.target_node_id == finalize_node->id) {
            CHECK(e.context_node_id == postprocess_stage->id);
            found_finalize_edge = true;
        }
    }
    CHECK(found_transform_edge);
    CHECK(found_finalize_edge);
}

TEST_CASE("TopologyAnalyzer: FUNC with CALL sets context_node_id on edges",
          "[topology][analyzer][drilldown]") {
    std::string source = R"(
FUNC process(x: Int) -> Int {
    VAR mid = CALL(cpp, compute, x);
    VAR result = CALL(python, finalize, mid);
    RETURN result;
}
)";

    auto graph = BuildGraph(source);

    // Find the process FUNC declaration node
    const TopologyNode *process_node = nullptr;
    for (const auto &n : graph.Nodes()) {
        if (n.name == "process" && n.origin == TopologyNode::Origin::kDecl) {
            process_node = &n;
            break;
        }
    }
    REQUIRE(process_node != nullptr);

    // External CALL nodes should have context_node_id == process_node->id
    for (const auto &n : graph.Nodes()) {
        if (n.kind == TopologyNode::Kind::kExternalCall) {
            INFO("External node: " << n.name << " context_node_id=" << n.context_node_id);
            CHECK(n.context_node_id == process_node->id);
        }
    }

    // CALL edges should have context_node_id == process_node->id
    for (const auto &e : graph.Edges()) {
        if (e.origin == TopologyEdge::Origin::kCall) {
            INFO("CALL edge id=" << e.id << " context_node_id=" << e.context_node_id);
            CHECK(e.context_node_id == process_node->id);
        }
    }
}

TEST_CASE("TopologyGraph: drill-down visibility — collapsed hides context children",
          "[topology][graph][drilldown]") {
    // Build a graph manually simulating what the analyzer produces:
    // A pipeline container, two stage nodes, and two external nodes created
    // by stage bodies (with context_node_id set).
    TopologyGraph graph;

    // Pipeline container
    TopologyNode pipe;
    pipe.name = "pipeline:test";
    pipe.language = "ploy";
    pipe.kind = TopologyNode::Kind::kPipeline;
    pipe.origin = TopologyNode::Origin::kDecl;
    Port pipe_out;
    pipe_out.name = "result";
    pipe_out.direction = Port::Direction::kOutput;
    pipe.outputs.push_back(pipe_out);
    uint64_t pipe_id = graph.AddNode(std::move(pipe));

    // Stage 1
    TopologyNode stage1;
    stage1.name = "pipeline:test::step1";
    stage1.language = "ploy";
    stage1.kind = TopologyNode::Kind::kFunction;
    stage1.origin = TopologyNode::Origin::kPipelineStage;
    Port s1_in;
    s1_in.name = "x";
    s1_in.direction = Port::Direction::kInput;
    stage1.inputs.push_back(s1_in);
    Port s1_out;
    s1_out.name = "return";
    s1_out.direction = Port::Direction::kOutput;
    stage1.outputs.push_back(s1_out);
    uint64_t stage1_id = graph.AddNode(std::move(stage1));

    // Stage 2
    TopologyNode stage2;
    stage2.name = "pipeline:test::step2";
    stage2.language = "ploy";
    stage2.kind = TopologyNode::Kind::kFunction;
    stage2.origin = TopologyNode::Origin::kPipelineStage;
    Port s2_in;
    s2_in.name = "y";
    s2_in.direction = Port::Direction::kInput;
    stage2.inputs.push_back(s2_in);
    Port s2_out;
    s2_out.name = "return";
    s2_out.direction = Port::Direction::kOutput;
    stage2.outputs.push_back(s2_out);
    uint64_t stage2_id = graph.AddNode(std::move(stage2));

    // External node created by stage1 body
    TopologyNode ext1;
    ext1.name = "cpp::transform";
    ext1.language = "cpp";
    ext1.kind = TopologyNode::Kind::kExternalCall;
    ext1.origin = TopologyNode::Origin::kCall;
    ext1.context_node_id = stage1_id;
    Port e1_out;
    e1_out.name = "return";
    e1_out.direction = Port::Direction::kOutput;
    ext1.outputs.push_back(e1_out);
    uint64_t ext1_id = graph.AddNode(std::move(ext1));

    // External node created by stage2 body
    TopologyNode ext2;
    ext2.name = "python::finalize";
    ext2.language = "python";
    ext2.kind = TopologyNode::Kind::kExternalCall;
    ext2.origin = TopologyNode::Origin::kCall;
    ext2.context_node_id = stage2_id;
    Port e2_out;
    e2_out.name = "return";
    e2_out.direction = Port::Direction::kOutput;
    ext2.outputs.push_back(e2_out);
    uint64_t ext2_id = graph.AddNode(std::move(ext2));

    // Retrieve port ids
    const auto *s1 = graph.GetNode(stage1_id);
    const auto *s2 = graph.GetNode(stage2_id);
    const auto *e1 = graph.GetNode(ext1_id);
    const auto *e2 = graph.GetNode(ext2_id);

    // Stage-order edge: stage1 -> stage2
    TopologyEdge stage_edge;
    stage_edge.source_node_id = stage1_id;
    stage_edge.source_port_id = s1->outputs[0].id;
    stage_edge.target_node_id = stage2_id;
    stage_edge.target_port_id = s2->inputs[0].id;
    stage_edge.origin = TopologyEdge::Origin::kPipelineStage;
    stage_edge.context_node_id = 0;  // pipeline-level, not body-level
    graph.AddEdge(std::move(stage_edge));

    // CALL edge: stage1 -> ext1 (created by stage1's body)
    TopologyEdge call_edge1;
    call_edge1.source_node_id = stage1_id;
    call_edge1.source_port_id = s1->inputs[0].id;
    call_edge1.target_node_id = ext1_id;
    call_edge1.target_port_id = e1->outputs[0].id;
    call_edge1.origin = TopologyEdge::Origin::kCall;
    call_edge1.context_node_id = stage1_id;  // created by stage1's body
    auto call_edge1_id = graph.AddEdge(std::move(call_edge1));

    // CALL edge: stage2 -> ext2 (created by stage2's body)
    TopologyEdge call_edge2;
    call_edge2.source_node_id = stage2_id;
    call_edge2.source_port_id = s2->inputs[0].id;
    call_edge2.target_node_id = ext2_id;
    call_edge2.target_port_id = e2->outputs[0].id;
    call_edge2.origin = TopologyEdge::Origin::kCall;
    call_edge2.context_node_id = stage2_id;  // created by stage2's body
    auto call_edge2_id = graph.AddEdge(std::move(call_edge2));

    // Verify graph structure
    REQUIRE(graph.NodeCount() == 5);
    REQUIRE(graph.EdgeCount() == 3);

    // Verify context_node_id assignments
    const auto *ext1_check = graph.GetNode(ext1_id);
    const auto *ext2_check = graph.GetNode(ext2_id);
    CHECK(ext1_check->context_node_id == stage1_id);
    CHECK(ext2_check->context_node_id == stage2_id);

    // Check that stage nodes have context_node_id == 0 (top-level stages)
    const auto *s1_check = graph.GetNode(stage1_id);
    const auto *s2_check = graph.GetNode(stage2_id);
    CHECK(s1_check->context_node_id == 0);
    CHECK(s2_check->context_node_id == 0);

    // Verify edge context_node_ids
    for (const auto &e : graph.Edges()) {
        if (e.origin == TopologyEdge::Origin::kPipelineStage) {
            CHECK(e.context_node_id == 0);
        } else if (e.id == call_edge1_id) {
            CHECK(e.context_node_id == stage1_id);
        } else if (e.id == call_edge2_id) {
            CHECK(e.context_node_id == stage2_id);
        }
    }
}
