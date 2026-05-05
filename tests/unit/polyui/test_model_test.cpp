/**
 * @file     test_model_test.cpp
 * @brief    Unit tests for `TestModel` and the five report parsers.
 *
 * @ingroup  Tool / polyui / Tests
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include <catch2/catch_test_macros.hpp>

#include "tools/ui/common/testing/test_model.h"

using namespace polyglot::tools::ui::testing;

TEST_CASE("TestModel tracks status, parents, and aggregates",
          "[polyui][testing][model]") {
  TestModel m;
  TestNode root;  root.id = "p"; root.kind = TestKind::kProject; root.label = "p";
  m.Upsert(root);
  TestNode suite;  suite.id = "p::s"; suite.parent_id = "p";
  suite.kind = TestKind::kSuite; suite.label = "s";  m.Upsert(suite);
  TestNode a;      a.id = "p::s::a"; a.parent_id = "p::s"; a.label = "a";
  a.kind = TestKind::kCase; a.status = TestStatus::kPassed;  m.Upsert(a);
  TestNode b;      b.id = "p::s::b"; b.parent_id = "p::s"; b.label = "b";
  b.kind = TestKind::kCase; b.status = TestStatus::kFailed;  m.Upsert(b);

  CHECK(m.Children("p").size() == 1);
  CHECK(m.Children("p::s").size() == 2);

  m.MarkStatus("p::s::a", TestStatus::kFailed,
               std::chrono::milliseconds{5}, "boom");
  CHECK(m.Find("p::s::a")->failure_message == "boom");
  CHECK(m.Find("p::s::a")->duration.count() == 5);

  auto agg = m.Aggregate();
  CHECK(agg.failed == 2);
}

TEST_CASE("ParseCTestReport recognises pass/fail nodes",
          "[polyui][testing][ctest]") {
  std::string xml = R"(<?xml version="1.0"?>
<Testing>
  <Test Status="passed">
    <Name>polyc.smoke</Name>
    <FullCommandLine>./polyc --check</FullCommandLine>
    <Results>
      <NamedMeasurement type="numeric/double" name="Execution Time">
        <Value>0.42</Value>
      </NamedMeasurement>
    </Results>
  </Test>
  <Test Status="failed">
    <Name>polyc.fuzz</Name>
    <Results>
      <Output>segfault</Output>
    </Results>
  </Test>
</Testing>)";
  auto nodes = ParseCTestReport(xml);
  REQUIRE(nodes.size() == 3);  // 1 root + 2 cases
  CHECK(nodes[1].status == TestStatus::kPassed);
  CHECK(nodes[1].duration.count() == 420);
  CHECK(nodes[2].status == TestStatus::kFailed);
  CHECK(nodes[2].failure_message.find("segfault") != std::string::npos);
}

TEST_CASE("ParseJUnitReport handles failures and skips",
          "[polyui][testing][junit]") {
  std::string xml = R"(<?xml version="1.0"?>
<testsuite name="t">
  <testcase classname="C" name="ok" time="0.01"/>
  <testcase classname="C" name="bad" time="0.02">
    <failure message="x">trace</failure>
  </testcase>
  <testcase classname="C" name="skip">
    <skipped/>
  </testcase>
</testsuite>)";
  auto nodes = ParseJUnitReport(xml);
  REQUIRE(nodes.size() == 4);  // 1 suite + 3 cases
  CHECK(nodes[1].status == TestStatus::kPassed);
  CHECK(nodes[2].status == TestStatus::kFailed);
  CHECK(nodes[2].failure_message == "trace");
  CHECK(nodes[3].status == TestStatus::kSkipped);
}

TEST_CASE("ParseCargoReport reads libtest JSON lines",
          "[polyui][testing][cargo]") {
  std::string lines = R"({"type":"test","event":"ok","name":"t::a","exec_time":0.001}
{"type":"test","event":"failed","name":"t::b","stdout":"oops"}
{"type":"test","event":"ignored","name":"t::c"})";
  auto nodes = ParseCargoReport(lines);
  REQUIRE(nodes.size() == 4);  // root + 3 cases
  CHECK(nodes[1].status == TestStatus::kPassed);
  CHECK(nodes[2].status == TestStatus::kFailed);
  CHECK(nodes[2].failure_message == "oops");
  CHECK(nodes[3].status == TestStatus::kSkipped);
}

TEST_CASE("ParseXUnitReport reads result attributes",
          "[polyui][testing][xunit]") {
  std::string xml = R"(<assemblies>
  <assembly>
    <collection name="C">
      <test name="A.x" result="Pass" time="0.01"/>
      <test name="A.y" result="Fail" time="0.02">
        <failure><message>boom</message></failure>
      </test>
      <test name="A.z" result="Skip"/>
    </collection>
  </assembly>
</assemblies>)";
  auto nodes = ParseXUnitReport(xml);
  REQUIRE(nodes.size() == 4);
  CHECK(nodes[1].status == TestStatus::kPassed);
  CHECK(nodes[2].status == TestStatus::kFailed);
  CHECK(nodes[2].failure_message == "boom");
}

TEST_CASE("ParseNUnitReport walks nested suites",
          "[polyui][testing][nunit]") {
  std::string xml = R"(<test-run>
  <test-suite fullname="Root">
    <test-suite fullname="Root.Sub">
      <test-case fullname="Root.Sub.a" result="Passed" duration="0.001"/>
      <test-case fullname="Root.Sub.b" result="Failed" duration="0.002">
        <failure><message>nope</message></failure>
      </test-case>
    </test-suite>
  </test-suite>
</test-run>)";
  auto nodes = ParseNUnitReport(xml);
  // 2 suites + 2 cases
  REQUIRE(nodes.size() == 4);
  CHECK(nodes[2].status == TestStatus::kPassed);
  CHECK(nodes[3].status == TestStatus::kFailed);
  CHECK(nodes[3].failure_message == "nope");
}
