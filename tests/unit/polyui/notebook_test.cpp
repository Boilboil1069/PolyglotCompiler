/**
 * @file     notebook_test.cpp
 * @brief    Unit tests for `Notebook` + `ReplSession`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/notebook/notebook.h"

using namespace polyglot::tools::ui::notebook;

namespace {

// Echo transport: returns the input as stdout, optionally capitalised
// to model an "engine".
class EchoTransport : public ReplTransport {
 public:
  explicit EchoTransport(std::string tag) : tag_(std::move(tag)) {}
  bool Start(const ReplEngineSpec &) override { running_ = true; return true; }
  ReplTurn Eval(const std::string &input) override {
    ReplTurn t;
    t.stdout_text = "[" + tag_ + "] " + input;
    return t;
  }
  void Stop() override { running_ = false; }
  bool running() const override { return running_; }
 private:
  std::string tag_;
  bool running_{false};
};

}  // namespace

TEST_CASE("DefaultSpec carries argv and prompt for every engine",
          "[polyui][notebook][repl]") {
  for (auto e : {ReplEngine::kPloy, ReplEngine::kPython, ReplEngine::kIRust,
                 ReplEngine::kIRB, ReplEngine::kDotnetScript}) {
    auto s = DefaultSpec(e);
    CHECK_FALSE(s.argv.empty());
    CHECK_FALSE(s.prompt_regex.empty());
    CHECK_FALSE(s.exit_command.empty());
  }
  CHECK(DefaultSpec(ReplEngine::kPloy).argv[0] == "polyc");
}

TEST_CASE("ReplSession transcribes evaluations",
          "[polyui][notebook][repl]") {
  ReplSession s(DefaultSpec(ReplEngine::kPython),
                std::make_unique<EchoTransport>("py"));
  REQUIRE(s.Start());
  s.Eval("1 + 1");
  s.Eval("print('hi')");
  REQUIRE(s.transcript().size() == 2);
  CHECK(s.transcript()[0].stdout_text == "[py] 1 + 1");
  CHECK(s.transcript()[1].input == "print('hi')");
}

TEST_CASE("Notebook executes code cells via injected sessions",
          "[polyui][notebook]") {
  Notebook nb;
  Cell c;
  c.kind = CellKind::kCode;
  c.engine = ReplEngine::kPloy;
  c.source = "let x = 1";
  auto id = nb.AddCell(c);

  ReplSession ploy(DefaultSpec(ReplEngine::kPloy),
                   std::make_unique<EchoTransport>("ploy"));
  REQUIRE(ploy.Start());
  std::unordered_map<ReplEngine, ReplSession *> sessions = {
      {ReplEngine::kPloy, &ploy}};
  auto out = nb.Execute(id, sessions);
  CHECK_FALSE(out.error);
  CHECK(out.stdout_text == "[ploy] let x = 1");
  CHECK(nb.Find(id)->output.stdout_text == out.stdout_text);
}

TEST_CASE("Notebook runs cross-language LINK cells",
          "[polyui][notebook]") {
  Notebook nb;
  Cell c;
  c.kind = CellKind::kCrossLanguageLink;
  c.link.target_engine = ReplEngine::kPloy;
  c.link.source_engine = ReplEngine::kPython;
  c.link.target_symbol = "consume";
  c.link.source_symbol = "produce";
  auto id = nb.AddCell(c);

  ReplSession ploy(DefaultSpec(ReplEngine::kPloy),
                   std::make_unique<EchoTransport>("ploy"));
  ReplSession py(DefaultSpec(ReplEngine::kPython),
                 std::make_unique<EchoTransport>("py"));
  REQUIRE(ploy.Start());
  REQUIRE(py.Start());
  std::unordered_map<ReplEngine, ReplSession *> sessions = {
      {ReplEngine::kPloy, &ploy}, {ReplEngine::kPython, &py}};
  auto out = nb.Execute(id, sessions);
  CHECK(out.stdout_text.find("[py] produce") != std::string::npos);
  CHECK(out.stdout_text.find("[ploy] consume") != std::string::npos);
}

TEST_CASE("Notebook serialises and round-trips through .polynb JSON",
          "[polyui][notebook]") {
  Notebook nb;
  Cell md;
  md.kind = CellKind::kMarkdown;
  md.source = "# title";
  nb.AddCell(md);
  Cell code;
  code.kind = CellKind::kCode;
  code.engine = ReplEngine::kIRust;
  code.source = "let v = vec![1,2,3];";
  nb.AddCell(code);

  auto json = nb.ToJson();
  Notebook nb2;
  REQUIRE(nb2.LoadJson(json));
  REQUIRE(nb2.cells().size() == 2);
  CHECK(nb2.cells()[0].kind == CellKind::kMarkdown);
  CHECK(nb2.cells()[1].engine == ReplEngine::kIRust);
  CHECK(nb2.cells()[1].source == "let v = vec![1,2,3];");
}

TEST_CASE("Notebook supports remove and move cell operations",
          "[polyui][notebook]") {
  Notebook nb;
  auto a = nb.AddCell({});
  auto b = nb.AddCell({});
  auto c = nb.AddCell({});
  CHECK(nb.MoveCell(c, 0));
  CHECK(nb.cells()[0].id == c);
  CHECK(nb.RemoveCell(a));
  CHECK(nb.cells().size() == 2);
  CHECK_FALSE(nb.RemoveCell("nope"));
}
