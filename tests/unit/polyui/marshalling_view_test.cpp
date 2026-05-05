/**
 * @file     marshalling_view_test.cpp
 * @brief    Unit tests for `MarshallingViewBuilder`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/cross_language/marshalling_view.h"

using namespace polyglot::tools::ui::cross_language;

TEST_CASE("LoadAux parses parameters and return chains",
          "[polyui][crosslang][marshal]") {
  std::string aux = R"json({
    "chains": [{
      "bridge_id": "br-1",
      "symbol": "polyglot_add",
      "parameters": [{
        "name": "x",
        "source_type": "i32",
        "target_type": "int",
        "steps": [
          {"stage":"ir","description":"lower x","snippet":"; lower x"},
          {"stage":"helper","description":"copy","snippet":"copy_value(x)"},
          {"stage":"abi","description":"abi","snippet":"extern \"C\" call"}
        ]
      }],
      "return": {
        "name": "ret", "source_type": "int", "target_type": "i32",
        "steps": [{"stage":"abi","description":"adapt","snippet":"// ret"}]
      }
    }]
  })json";
  MarshallingViewBuilder b;
  REQUIRE(b.LoadAux(aux));
  REQUIRE(b.chains().size() == 1);
  const auto *c = b.FindByBridge("br-1");
  REQUIRE(c);
  REQUIRE(c->parameters.size() == 1);
  CHECK(c->parameters[0].steps.size() == 3);
  CHECK(c->parameters[0].steps[0].stage == MarshallingStage::kIRLowering);
  CHECK(c->parameters[0].steps[2].stage == MarshallingStage::kAbiAdapter);
  CHECK(c->return_value.steps.size() == 1);
}

TEST_CASE("Synthesize produces a 3-stage chain for every host language",
          "[polyui][crosslang][marshal]") {
  for (auto target : {HostLanguage::kCpp, HostLanguage::kRust,
                      HostLanguage::kPython, HostLanguage::kJava,
                      HostLanguage::kDotnet}) {
    Bridge b;
    b.id = "br-x";
    b.name = "fn";
    b.stub_name = "polyglot_fn";
    b.host_language = HostLanguage::kCpp;
    b.target_language = target;
    b.strategy = MarshallingStrategy::kCopyByValue;
    auto chain = MarshallingViewBuilder::Synthesize(b);
    REQUIRE(chain.parameters.size() == 1);
    REQUIRE(chain.parameters[0].steps.size() == 3);
    CHECK(chain.parameters[0].steps[0].stage ==
          MarshallingStage::kIRLowering);
    CHECK(chain.parameters[0].steps[2].description.find(
              HostLanguageName(target)) != std::string::npos);
    CHECK(chain.return_value.steps.size() == 3);
    CHECK(chain.symbol == "polyglot_fn");
  }
}

TEST_CASE("AddChain replaces the chain for an existing bridge id",
          "[polyui][crosslang][marshal]") {
  MarshallingViewBuilder b;
  MarshallingChain c1;
  c1.bridge_id = "br-1";
  c1.symbol = "old";
  b.AddChain(c1);
  MarshallingChain c2;
  c2.bridge_id = "br-1";
  c2.symbol = "new";
  b.AddChain(c2);
  REQUIRE(b.chains().size() == 1);
  CHECK(b.FindByBridge("br-1")->symbol == "new");
}
