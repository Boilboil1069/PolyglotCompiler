#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QGraphicsTextItem>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "frontends/common/include/frontend_registry.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/go/include/go_frontend.h"
#include "frontends/python/include/python_frontend.h"
#include "frontends/rust/include/rust_frontend.h"
#include "tools/ui/common/cross_language/poly_workspace_analysis.h"
#include "tools/ui/common/include/native_trace_panel.h"
#include "tools/ui/common/include/topology_panel.h"

using namespace polyglot;
using namespace polyglot::tools::ui;
using Json = nlohmann::json;
namespace {
void App() {
  if (QApplication::instance())
    return;
  static int argc = 1;
  static char name[] = "native_trace_ui_test";
  static char *argv[] = {name, nullptr};
  static QApplication app(argc, argv);
}
void Register() {
  auto &registry = frontends::FrontendRegistry::Instance();
  registry.Register(std::make_shared<cpp::CppLanguageFrontend>());
  registry.Register(std::make_shared<python::PythonLanguageFrontend>());
  registry.Register(std::make_shared<rust::RustLanguageFrontend>());
  registry.Register(std::make_shared<go::GoLanguageFrontend>());
}
void Write(const QString &path, const std::string &text, bool append = false) {
  QFile file(path);
  REQUIRE(file.open(append ? QIODevice::Append : QIODevice::WriteOnly));
  REQUIRE(file.write(text.data(), static_cast<qint64>(text.size())) ==
          static_cast<qint64>(text.size()));
}
std::string Read(const std::filesystem::path &path) {
  std::ifstream file(path);
  REQUIRE(file.good());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
Json Metadata(const QString &source, int column = 31) {
  return {
      {"schema", "polyglot.native-call-sites.v1"},
      {"sites",
       Json::array({{{"id", "1"},
                     {"source", {{"file", source.toStdString()}, {"line", 2}, {"column", column}}},
                     {"callee", "math::twice"},
                     {"language", "cpp"},
                     {"arguments", Json::array({{{"name", "x"}, {"type", "i32"}}})},
                     {"result_type", "i32"}}})}};
}
std::string Record(const char *instance = "1", const char *argument = "7",
                   const char *result = "14") {
  return Json{{"schema", "polyglot.native-calltrace.v1"},
              {"site_id", "1"},
              {"instance_id", instance},
              {"parent_instance_id", "0"},
              {"started_ticks", "1000"},
              {"ended_ticks", "1240"},
              {"ticks_per_second", "24000000"},
              {"argument_bits", Json::array({argument})},
              {"result_bits", result}}
      .dump();
}
bool HasText(const TopoNodeItem *node, const QString &text) {
  for (auto *child : node->childItems())
    if (auto *label = dynamic_cast<QGraphicsTextItem *>(child);
        label && label->toPlainText().contains(text))
      return true;
  return false;
}
} // namespace

TEST_CASE("Shared workspace analysis resolves foreign returns and preserves real errors",
          "[polyui][native-trace][analysis]") {
  App();
  Register();
  QTemporaryDir directory;
  const auto path = directory.filePath("flow.poly");
  Write(directory.filePath("math.cpp"), "int twice(int x) { return x * 2; }\n");
  const std::string source = "IMPORT cpp::math;\nFUNC main() -> INT { LET value = CALL(cpp, "
                             "math::twice, 7); RETURN value; }\n";
  Write(path, source);
  const auto good = cross_language::AnalyzePolyWorkspace(source, path.toStdString());
  for (const auto &diagnostic : good->diagnostics.All())
    INFO(frontends::Diagnostics::Format(diagnostic));
  REQUIRE_FALSE(good->diagnostics.HasErrors());
  REQUIRE(good->foreign_signatures.contains("math::twice"));
  CHECK(cross_language::CompilerTypeName(good->foreign_signatures.at("math::twice").return_type) ==
        "i32");
  const auto bad = cross_language::AnalyzePolyWorkspace(
      "IMPORT cpp::math;\nFUNC main() -> INT { RETURN CALL(cpp, math::twice, 7, 8); }\n",
      path.toStdString());
  CHECK(bad->diagnostics.HasErrors());
  const auto missing = cross_language::AnalyzePolyWorkspace(
      "IMPORT cpp::math;\nFUNC main() -> INT { RETURN CALL(cpp, math::absent, 7); }\n",
      path.toStdString());
  CHECK(missing->diagnostics.HasErrors());
}

TEST_CASE("UI resolves four vendored package signatures in the order risk project",
          "[polyui][native-trace][analysis]") {
  App();
  Register();
  const auto root =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  const auto path = root / "examples/order_risk_analyzer/order_risk.poly";
  const auto analysis = cross_language::AnalyzePolyWorkspace(Read(path), path.string());
  for (const auto &diagnostic : analysis->diagnostics.All())
    INFO(frontends::Diagnostics::Format(diagnostic));
  REQUIRE_FALSE(analysis->diagnostics.HasErrors());
  for (const auto *name :
       {"pricing_engine::pricing_session_payable", "fraud_engine::fraud_session_band",
        "fulfillment_engine::fulfillment_session_gate",
        "logistics_engine::logistics_session_decision"}) {
    CAPTURE(name);
    REQUIRE(analysis->foreign_signatures.contains(name));
    CHECK(cross_language::CompilerTypeName(analysis->foreign_signatures.at(name).return_type)
              .find("unknown") == std::string::npos);
  }
}

TEST_CASE("Trace panel consumes partial records and reports absent runtime output",
          "[polyui][native-trace][panel]") {
  App();
  QTemporaryDir directory;
  const auto path = directory.filePath("calls.jsonl");
  Write(path + ".sites.json", Metadata(directory.filePath("flow.poly")).dump());
  NativeTracePanel panel;
  REQUIRE(panel.BeginSession(path));
  const auto record = Record();
  Write(path, record.substr(0, record.size() / 2));
  panel.Poll();
  CHECK(panel.Trace().Samples().empty());
  Write(path, record.substr(record.size() / 2) + "\n" + Record("2", "9", "18"), true);
  panel.FinishSession();
  REQUIRE(panel.Error().isEmpty());
  REQUIRE(panel.Trace().Samples().size() == 2);
  REQUIRE(panel.SelectSample(1));
  CHECK(panel.SelectedSample()->instance_id == "2");
  CHECK(panel.SelectedSample()->arguments[0].display == "9");
  CHECK(panel.SelectedSample()->result.display == "18");
  const auto *values = panel.findChild<QTreeWidget *>("runtimeTraceValues");
  REQUIRE(values);
  CHECK(values->topLevelItem(0)->text(2) == "9");
  panel.ClearSession();
  CHECK(panel.SelectedSample() == nullptr);
  QFile::remove(path);
  REQUIRE(panel.BeginSession(path));
  panel.FinishSession();
  CHECK_FALSE(panel.Error().isEmpty());
}

TEST_CASE("Runtime source mapping requires exact call column and overlay restores static ports",
          "[polyui][native-trace][graph]") {
  App();
  Register();
  QTemporaryDir directory;
  const auto source = directory.filePath("flow.poly");
  Write(directory.filePath("math.cpp"), "int twice(int x) { return x * 2; }\n");
  const std::string program =
      "IMPORT cpp::math;\nFUNC main() -> INT { LET a = CALL(cpp, math::twice, 7); LET b = "
      "CALL(cpp, math::twice, 9); RETURN a + b; }\n";
  Write(source, program);
  TopologyPanel panel;
  panel.LoadFromFile(source);
  std::vector<TopoNodeItem *> calls;
  for (const auto &[id, node] : panel.NodeItems())
    if (node->NodeName() == "cpp::math::twice")
      calls.push_back(node);
  REQUIRE(calls.size() == 2);
  std::sort(calls.begin(), calls.end(),
            [](auto *a, auto *b) { return a->CallColumn() < b->CallColumn(); });
  REQUIRE(calls[0]->CallColumn() != calls[1]->CallColumn());
  const auto trace = directory.filePath("calls.jsonl");
  Write(trace + ".sites.json", Metadata(source, calls[1]->CallColumn()).dump());
  Write(trace, Record("1", "9", "18") + "\n");
  int navigated_column = 0;
  QObject::connect(
      &panel, &TopologyPanel::RuntimeSampleSelected,
      [&](const QString &, int, int column, const QString &) { navigated_column = column; });
  REQUIRE(panel.BeginTraceSession(trace, false));
  CHECK(navigated_column == calls[1]->CallColumn());
  CHECK_FALSE(HasText(calls[0], "= 9"));
  CHECK(HasText(calls[1], "= 9"));
  CHECK(HasText(calls[1], "= 18"));
  auto *toggle = panel.TracePanel()->findChild<QCheckBox *>("runtimeOverlayToggle");
  REQUIRE(toggle);
  toggle->setChecked(false);
  CHECK_FALSE(HasText(calls[1], "= 9"));
  CHECK_FALSE(HasText(calls[1], "= 18"));
  toggle->setChecked(true);
  CHECK(HasText(calls[1], "= 18"));
  panel.TracePanel()->ClearSession();
  CHECK_FALSE(HasText(calls[1], "= 18"));
}
