/**
 * @file     bridge_panel_test.cpp
 * @brief    Unit tests for `BridgePanelModel`.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/cross_language/bridge_panel.h"

using namespace polyglot::tools::ui::cross_language;

TEST_CASE("MarshallingStrategy name round-trips",
          "[polyui][crosslang][bridge]") {
  for (auto s : {MarshallingStrategy::kZeroCopy,
                 MarshallingStrategy::kCopyByValue,
                 MarshallingStrategy::kPodSerialisation,
                 MarshallingStrategy::kJsonRoundtrip,
                 MarshallingStrategy::kProtobufRoundtrip,
                 MarshallingStrategy::kHandleTable}) {
    CHECK(*MarshallingStrategyFromName(MarshallingStrategyName(s)) == s);
  }
}

TEST_CASE("ImportFromAux loads bridges across all five languages",
          "[polyui][crosslang][bridge]") {
  std::string aux = R"({
    "bridges": [
      {"id":"br-1","name":"add","host":"ploy","target":"cpp",
       "stub":"polyglot_add","strategy":"copy-by-value",
       "source":{"file":"a.ploy","line":10,"column":2},
       "target_location":{"file":"a.cpp","line":3,"column":1}},
      {"id":"br-2","name":"hash","host":"ploy","target":"rust",
       "stub":"polyglot_hash","strategy":"zero-copy"},
      {"id":"br-3","name":"sum","host":"ploy","target":"python",
       "stub":"polyglot_sum","strategy":"json-roundtrip"},
      {"id":"br-4","name":"size","host":"ploy","target":"java",
       "stub":"polyglot_size","strategy":"handle-table"},
      {"id":"br-5","name":"min","host":"ploy","target":"dotnet",
       "stub":"polyglot_min","strategy":"protobuf-roundtrip"}
    ]
  })";
  BridgePanelModel m;
  REQUIRE(m.ImportFromAux(aux));
  CHECK(m.bridges().size() == 5);
  CHECK(m.FindByStub("polyglot_hash")->target_language == HostLanguage::kRust);
  CHECK(m.FindById("br-4")->strategy == MarshallingStrategy::kHandleTable);
  CHECK(m.FindById("br-1")->source.line == 10);
}

TEST_CASE("Re-importing preserves runtime call counts",
          "[polyui][crosslang][bridge]") {
  BridgePanelModel m;
  Bridge b;
  b.id = "br-1";
  b.name = "add";
  b.stub_name = "polyglot_add";
  b.target_language = HostLanguage::kCpp;
  b.strategy = MarshallingStrategy::kCopyByValue;
  m.AddBridge(b);
  CHECK(m.IncrementCallCount("br-1", 7));
  CHECK(m.FindById("br-1")->call_count == 7);

  std::string reimport = R"({"bridges":[{"id":"br-1","name":"add",
    "host":"ploy","target":"cpp","stub":"polyglot_add",
    "strategy":"copy-by-value"}]})";
  REQUIRE(m.ImportFromAux(reimport));
  CHECK(m.bridges().size() == 1);
  CHECK(m.FindById("br-1")->call_count == 7);
}

TEST_CASE("UpdateCallCount and filter helpers work",
          "[polyui][crosslang][bridge]") {
  BridgePanelModel m;
  Bridge a; a.id = "a"; a.host_language = HostLanguage::kCpp;
  a.target_language = HostLanguage::kRust;
  Bridge b; b.id = "b"; b.host_language = HostLanguage::kCpp;
  b.target_language = HostLanguage::kRust;
  Bridge c; c.id = "c"; c.host_language = HostLanguage::kCpp;
  c.target_language = HostLanguage::kPython;
  m.AddBridge(a); m.AddBridge(b); m.AddBridge(c);

  CHECK(m.UpdateCallCount("b", 99));
  CHECK_FALSE(m.UpdateCallCount("nope", 1));
  CHECK(m.FilterByLanguagePair(HostLanguage::kCpp,
                               HostLanguage::kRust).size() == 2);
  CHECK(m.FilterByLanguagePair(HostLanguage::kCpp,
                               HostLanguage::kPython).size() == 1);
}
