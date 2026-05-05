/**
 * @file     outline_model_test.cpp
 * @brief    Unit tests for the outline / breadcrumb tree model
 *           (demand 2026-04-28-25 §5).
 *
 * @ingroup  Tests / unit / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/outline/outline_model.h"

using polyglot::tools::ui::OutlineModel;
using polyglot::tools::ui::OutlineNode;

namespace {

std::unique_ptr<OutlineNode> Make(std::string name, std::string kind,
                                   std::uint32_t line) {
  auto n = std::make_unique<OutlineNode>();
  n->name = std::move(name);
  n->kind = std::move(kind);
  n->line = line;
  return n;
}

}  // namespace

TEST_CASE("OutlineModel filter hides non-matching subtrees",
          "[polyui][outline]") {
  std::vector<std::unique_ptr<OutlineNode>> roots;
  roots.push_back(Make("Alpha", "class", 0));
  roots.push_back(Make("Beta", "class", 10));
  roots[0]->children.push_back(Make("alphaMethod", "function", 1));
  OutlineModel m;
  m.SetRoots(std::move(roots));

  REQUIRE(m.Visible().size() == 2);
  m.SetFilter("beta");
  auto v = m.Visible();
  REQUIRE(v.size() == 1);
  REQUIRE(v.front()->name == "Beta");
  m.SetFilter("alphaMet");  // matches via descendant
  REQUIRE(m.Visible().size() == 1);
  m.SetFilter("");
  REQUIRE(m.Visible().size() == 2);
}

TEST_CASE("OutlineModel breadcrumbs descend into nested nodes",
          "[polyui][outline][breadcrumb]") {
  std::vector<std::unique_ptr<OutlineNode>> roots;
  roots.push_back(Make("Outer", "class", 0));
  roots[0]->children.push_back(Make("Inner", "function", 5));
  roots[0]->children.back()->children.push_back(Make("local", "variable", 7));
  OutlineModel m;
  m.SetRoots(std::move(roots));

  auto trail = m.Breadcrumbs(8, 0);
  REQUIRE(trail.size() == 3);
  REQUIRE(trail[0]->name == "Outer");
  REQUIRE(trail[1]->name == "Inner");
  REQUIRE(trail[2]->name == "local");
}
