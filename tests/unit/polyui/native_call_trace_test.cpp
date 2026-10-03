#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "tools/ui/common/runtime/native_call_trace.h"

using namespace polyglot::tools::ui::runtime;
using Json = nlohmann::json;

static Json Sites() {
  return {
      {"schema", "polyglot.native-call-sites.v1"},
      {"sites",
       Json::array({{{"id", "1"},
                     {"source", {{"file", "/project/order.poly"}, {"line", 28}, {"column", 20}}},
                     {"callee", "pricing::subtotal"},
                     {"language", "cpp"},
                     {"arguments", Json::array({{{"name", "quantity"}, {"type", "i64"}}})},
                     {"result_type", "f64"}}})}};
}
static Json Event(const char *instance = "1", const char *parent = "0") {
  return {{"schema", "polyglot.native-calltrace.v1"},
          {"site_id", "1"},
          {"instance_id", instance},
          {"parent_instance_id", parent},
          {"started_ticks", "120"},
          {"ended_ticks", "144"},
          {"ticks_per_second", "24000000"},
          {"argument_bits", Json::array({"18446744073709551615"})},
          {"result_bits", "4609434218613702656"}}; // f64 1.5
}

TEST_CASE("Native trace preserves exact ABI integers and decodes actual float bits",
          "[polyui][native-trace]") {
  NativeCallTrace trace;
  std::string error;
  REQUIRE(trace.LoadSites(Sites().dump(), error));
  REQUIRE(trace.AppendEvent(Event().dump(), error));
  const auto &sample = trace.Samples().front();
  CHECK(sample.arguments[0].display == "-1");
  CHECK(sample.arguments[0].raw_bits == "18446744073709551615");
  CHECK(sample.result.display == "1.5");
  CHECK(sample.duration_ns == 1000.0L);
  CHECK(trace.Site("1")->line == 28);
  CHECK(trace.Site("1")->column == 20);
  CHECK(NativeCallTrace::Decode({"id", "u64"}, "18446744073709551615").display ==
        "18446744073709551615");
  CHECK(NativeCallTrace::Decode({"small", "i32"}, "4294967295").display == "-1");
  CHECK(NativeCallTrace::Decode({"zero", "f64"}, "9223372036854775808").display == "-0");
  CHECK(NativeCallTrace::Decode({"fraction", "f32"}, "1069547520").display == "1.5");
}

TEST_CASE("Repeated and nested recorded calls keep separate instance identities",
          "[polyui][native-trace]") {
  NativeCallTrace trace;
  std::string error;
  REQUIRE(trace.LoadSites(Sites().dump(), error));
  REQUIRE(trace.AppendEvent(Event("2", "1").dump(), error)); // completed child can arrive first
  REQUIRE(trace.AppendEvent(Event("1").dump(), error));
  REQUIRE(trace.AppendEvent(Event("3").dump(), error));
  CHECK(trace.Samples().size() == 3);
  CHECK(trace.Samples()[0].parent_instance_id == "1");
  CHECK_FALSE(trace.AppendEvent(Event("2").dump(), error));
  CHECK(trace.Samples().size() == 3);
  CHECK_FALSE(trace.AppendEvent(Event("4", "4").dump(), error));
}

TEST_CASE("Native trace never invents pointer payloads or absent values",
          "[polyui][native-trace]") {
  const auto pointer = NativeCallTrace::Decode({"text", "ptr"}, "4294967296");
  CHECK_FALSE(pointer.available);
  CHECK(pointer.display.empty());
  CHECK(pointer.unavailable_reason.find("not captured") != std::string::npos);
  CHECK_FALSE(NativeCallTrace::Decode({"nothing", "void"}, "0").available);
  CHECK_FALSE(NativeCallTrace::Decode({"invalid", "i1"}, "2").available);
}

TEST_CASE("Malformed runtime events fail without adding fabricated samples",
          "[polyui][native-trace]") {
  NativeCallTrace trace;
  std::string error;
  REQUIRE(trace.LoadSites(Sites().dump(), error));
  for (const auto *field :
       {"site_id", "instance_id", "argument_bits", "result_bits", "ticks_per_second"}) {
    auto event = Event();
    event.erase(field);
    CHECK_FALSE(trace.AppendEvent(event.dump(), error));
    CHECK_FALSE(error.empty());
  }
  auto event = Event();
  event["site_id"] = "99";
  CHECK_FALSE(trace.AppendEvent(event.dump(), error));
  event = Event();
  event["result_bits"] = 1.5;
  CHECK_FALSE(trace.AppendEvent(event.dump(), error));
  event = Event();
  event["ticks_per_second"] = "0";
  CHECK_FALSE(trace.AppendEvent(event.dump(), error));
  event = Event();
  event["ended_ticks"] = "100";
  CHECK_FALSE(trace.AppendEvent(event.dump(), error));
  event = Event();
  event["argument_bits"] = Json::array();
  CHECK_FALSE(trace.AppendEvent(event.dump(), error));
  CHECK(trace.Samples().empty());
}

TEST_CASE("Signed decimal ABI bits decode negative integers and float signs",
          "[polyui][native-trace]") {
  CHECK(NativeCallTrace::Decode({"value", "i64"}, "-21").display == "-21");
  CHECK(NativeCallTrace::Decode({"value", "u64"}, "-1").display == "18446744073709551615");
  CHECK(NativeCallTrace::Decode({"value", "f64"}, "-9223372036854775808").display == "-0");
  CHECK(NativeCallTrace::Decode({"value", "f64"}, "-4616189618054758400").display == "-1");
  NativeCallTrace trace;
  std::string error;
  REQUIRE(trace.LoadSites(Sites().dump(), error));
  auto event = Event();
  event["argument_bits"] = Json::array({"-21"});
  event["result_bits"] = "-4616189618054758400";
  REQUIRE(trace.AppendEvent(event.dump(), error));
  CHECK(trace.Samples()[0].arguments[0].display == "-21");
  CHECK(trace.Samples()[0].result.display == "-1");
  event["instance_id"] = "2";
  event["result_bits"] = "-9223372036854775809";
  CHECK_FALSE(trace.AppendEvent(event.dump(), error));
}
