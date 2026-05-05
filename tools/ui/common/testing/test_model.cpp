/**
 * @file     test_model.cpp
 * @brief    Implementation of `test_model.h` plus report parsers.
 *
 * The XML parsers are deliberately narrow: they consume only the
 * attributes the Test Explorer renders, so we avoid pulling in a
 * full XML library.  The tag scanner is reusable across CTest /
 * JUnit / xUnit / NUnit because all four use simple
 * attribute-bearing elements.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/testing/test_model.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::testing {
namespace {

using Json = nlohmann::json;

std::string Trim(std::string s) {
  auto not_ws = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
  return s;
}

// Decode a small, ASCII-only subset of XML entities.
std::string DecodeEntities(std::string s) {
  static const std::pair<std::string, std::string> kSubst[] = {
      {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
      {"&quot;", "\""}, {"&apos;", "'"}};
  for (const auto &p : kSubst) {
    std::string::size_type pos = 0;
    while ((pos = s.find(p.first, pos)) != std::string::npos) {
      s.replace(pos, p.first.size(), p.second);
      pos += p.second.size();
    }
  }
  return s;
}

// Extract attribute `name` from a tag substring like
//     foo bar="baz" qux='quux'
std::string Attr(const std::string &tag_body, const std::string &name) {
  auto find_after_space = [&](std::size_t pos) {
    while (pos > 0 && !std::isspace(static_cast<unsigned char>(tag_body[pos - 1])))
      --pos;
    return pos;
  };
  std::size_t search = 0;
  while (true) {
    auto p = tag_body.find(name, search);
    if (p == std::string::npos) return {};
    if (p > 0 && std::isalnum(static_cast<unsigned char>(tag_body[p - 1]))) {
      search = p + name.size();
      continue;
    }
    auto eq = tag_body.find('=', p + name.size());
    if (eq == std::string::npos) return {};
    auto skip = eq + 1;
    while (skip < tag_body.size() &&
           std::isspace(static_cast<unsigned char>(tag_body[skip])))
      ++skip;
    if (skip >= tag_body.size()) return {};
    char quote = tag_body[skip];
    if (quote != '"' && quote != '\'') {
      search = p + name.size();
      continue;
    }
    auto end = tag_body.find(quote, skip + 1);
    if (end == std::string::npos) return {};
    (void)find_after_space;  // keep helper for symmetry; unused now
    return DecodeEntities(tag_body.substr(skip + 1, end - skip - 1));
  }
}

struct Tag {
  std::string name;
  std::string body;          ///< Everything between `<` and `>`.
  bool self_closing{false};
  bool closing{false};       ///< `</foo>`.
  std::size_t end_pos{0};    ///< Index just past the closing `>`.
};

std::optional<Tag> NextTag(const std::string &xml, std::size_t from) {
  auto open = xml.find('<', from);
  if (open == std::string::npos) return std::nullopt;
  auto close = xml.find('>', open);
  if (close == std::string::npos) return std::nullopt;
  Tag t;
  t.body = xml.substr(open + 1, close - open - 1);
  t.end_pos = close + 1;
  if (!t.body.empty() && t.body.front() == '/') {
    t.closing = true;
    t.body.erase(0, 1);
  }
  if (!t.body.empty() && t.body.back() == '/') {
    t.self_closing = true;
    t.body.pop_back();
  }
  // Trim the leading name token.
  std::size_t i = 0;
  while (i < t.body.size() &&
         !std::isspace(static_cast<unsigned char>(t.body[i])))
    ++i;
  t.name = t.body.substr(0, i);
  return t;
}

// Slice the inner text between an open and matching close tag.
std::string InnerText(const std::string &xml, std::size_t after_open,
                      const std::string &name) {
  std::string close = "</" + name;
  auto pos = xml.find(close, after_open);
  if (pos == std::string::npos) return {};
  return DecodeEntities(xml.substr(after_open, pos - after_open));
}

std::chrono::milliseconds Seconds(const std::string &s) {
  if (s.empty()) return std::chrono::milliseconds{0};
  try {
    return std::chrono::milliseconds{
        static_cast<std::int64_t>(std::stod(s) * 1000.0)};
  } catch (...) {
    return std::chrono::milliseconds{0};
  }
}

}  // namespace

// --- TestModel --------------------------------------------------------------

void TestModel::Upsert(TestNode node) {
  if (nodes_.find(node.id) == nodes_.end())
    insertion_order_.push_back(node.id);
  nodes_[node.id] = std::move(node);
}

const TestNode *TestModel::Find(const std::string &id) const {
  auto it = nodes_.find(id);
  return it == nodes_.end() ? nullptr : &it->second;
}

std::vector<const TestNode *> TestModel::Children(
    const std::string &parent_id) const {
  std::vector<const TestNode *> out;
  for (const auto &id : insertion_order_) {
    auto it = nodes_.find(id);
    if (it != nodes_.end() && it->second.parent_id == parent_id)
      out.push_back(&it->second);
  }
  return out;
}

void TestModel::MarkStatus(const std::string &id, TestStatus status,
                           std::chrono::milliseconds duration,
                           std::string failure_message) {
  auto it = nodes_.find(id);
  if (it == nodes_.end()) return;
  it->second.status = status;
  if (duration.count() > 0) it->second.duration = duration;
  if (!failure_message.empty())
    it->second.failure_message = std::move(failure_message);
}

std::vector<const TestNode *> TestModel::FailedFirst() const {
  std::vector<const TestNode *> out;
  out.reserve(nodes_.size());
  for (const auto &id : insertion_order_) {
    out.push_back(&nodes_.at(id));
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const TestNode *a, const TestNode *b) {
                     auto rank = [](TestStatus s) {
                       switch (s) {
                         case TestStatus::kFailed:  return 0;
                         case TestStatus::kErrored: return 1;
                         case TestStatus::kSkipped: return 2;
                         case TestStatus::kPending: return 3;
                         case TestStatus::kRunning: return 4;
                         case TestStatus::kPassed:  return 5;
                       }
                       return 6;
                     };
                     return rank(a->status) < rank(b->status);
                   });
  return out;
}

TestModel::Summary TestModel::Aggregate(const std::string &root_id) const {
  Summary s;
  for (const auto &kv : nodes_) {
    if (kv.second.kind != TestKind::kCase) continue;
    if (!root_id.empty()) {
      // Walk up to confirm `root_id` is an ancestor.
      const TestNode *cur = &kv.second;
      bool under = false;
      while (cur && !cur->parent_id.empty()) {
        if (cur->parent_id == root_id) { under = true; break; }
        cur = Find(cur->parent_id);
      }
      if (!under) continue;
    }
    switch (kv.second.status) {
      case TestStatus::kPassed:  ++s.passed;  break;
      case TestStatus::kFailed:  ++s.failed;  break;
      case TestStatus::kSkipped: ++s.skipped; break;
      case TestStatus::kErrored: ++s.errored; break;
      default:                   ++s.pending; break;
    }
  }
  return s;
}

// --- CTest ------------------------------------------------------------------

std::vector<TestNode> ParseCTestReport(const std::string &xml) {
  std::vector<TestNode> out;
  TestNode root;
  root.id = "ctest";
  root.label = "CTest";
  root.kind = TestKind::kProject;
  root.framework = "ctest";
  out.push_back(root);

  std::size_t cursor = 0;
  while (auto tag = NextTag(xml, cursor)) {
    cursor = tag->end_pos;
    if (tag->closing || tag->name != "Test") continue;
    std::string status = Attr(tag->body, "Status");
    // Inner block lasts until </Test>.
    std::string name;
    std::string command;
    std::string output;
    std::chrono::milliseconds duration{0};
    std::string pending_measure;
    while (auto sub = NextTag(xml, cursor)) {
      cursor = sub->end_pos;
      if (sub->closing && sub->name == "Test") break;
      if (sub->closing) continue;
      if (sub->name == "Name") {
        name = InnerText(xml, sub->end_pos, "Name");
      } else if (sub->name == "FullCommandLine") {
        command = InnerText(xml, sub->end_pos, "FullCommandLine");
      } else if (sub->name == "NamedMeasurement") {
        pending_measure = Attr(sub->body, "name");
      } else if (sub->name == "Value") {
        std::string value = InnerText(xml, sub->end_pos, "Value");
        if (pending_measure == "Execution Time") duration = Seconds(value);
        pending_measure.clear();
      } else if (sub->name == "Output") {
        output = InnerText(xml, sub->end_pos, "Output");
      }
    }
    TestNode n;
    n.id = "ctest::" + name;
    n.parent_id = root.id;
    n.label = name;
    n.kind = TestKind::kCase;
    n.framework = "ctest";
    n.duration = duration;
    if (status == "passed") n.status = TestStatus::kPassed;
    else if (status == "failed") n.status = TestStatus::kFailed;
    else if (status == "notrun") n.status = TestStatus::kSkipped;
    else                         n.status = TestStatus::kErrored;
    if (n.status == TestStatus::kFailed || n.status == TestStatus::kErrored)
      n.failure_message = Trim(output);
    out.push_back(std::move(n));
  }
  return out;
}

// --- JUnit / pytest ---------------------------------------------------------

std::vector<TestNode> ParseJUnitReport(const std::string &xml) {
  std::vector<TestNode> out;
  std::string current_suite;
  std::size_t cursor = 0;
  while (auto tag = NextTag(xml, cursor)) {
    cursor = tag->end_pos;
    if (tag->closing) continue;
    if (tag->name == "testsuite") {
      std::string name = Attr(tag->body, "name");
      if (name.empty()) continue;
      TestNode suite;
      suite.id = "junit::" + name;
      suite.label = name;
      suite.kind = TestKind::kSuite;
      suite.framework = "junit";
      out.push_back(suite);
      current_suite = suite.id;
    } else if (tag->name == "testcase") {
      std::string classname = Attr(tag->body, "classname");
      std::string name = Attr(tag->body, "name");
      std::string time = Attr(tag->body, "time");
      std::string file = Attr(tag->body, "file");
      std::string line = Attr(tag->body, "line");

      TestNode n;
      n.id = "junit::" + (classname.empty() ? "" : classname + "::") + name;
      n.parent_id = current_suite;
      n.label = name;
      n.kind = TestKind::kCase;
      n.framework = "junit";
      n.duration = Seconds(time);
      n.location.file = file;
      try { n.location.line = line.empty() ? 0 : std::stoi(line); }
      catch (...) {}
      n.status = TestStatus::kPassed;

      if (!tag->self_closing) {
        // Look for child failure / error / skipped tags up to </testcase>.
        std::size_t inner = cursor;
        while (auto sub = NextTag(xml, inner)) {
          inner = sub->end_pos;
          if (sub->closing && sub->name == "testcase") break;
          if (sub->closing) continue;
          if (sub->name == "failure") {
            n.status = TestStatus::kFailed;
            n.failure_message = Trim(InnerText(xml, sub->end_pos, "failure"));
            if (n.failure_message.empty())
              n.failure_message = Attr(sub->body, "message");
          } else if (sub->name == "error") {
            n.status = TestStatus::kErrored;
            n.failure_message = Trim(InnerText(xml, sub->end_pos, "error"));
            if (n.failure_message.empty())
              n.failure_message = Attr(sub->body, "message");
          } else if (sub->name == "skipped") {
            n.status = TestStatus::kSkipped;
          }
        }
        cursor = inner;
      }
      out.push_back(std::move(n));
    }
  }
  return out;
}

// --- cargo test -------------------------------------------------------------

std::vector<TestNode> ParseCargoReport(const std::string &json_lines) {
  std::vector<TestNode> out;
  TestNode root;
  root.id = "cargo";
  root.label = "cargo test";
  root.kind = TestKind::kProject;
  root.framework = "cargo";
  out.push_back(root);

  std::stringstream ss(json_lines);
  std::string line;
  while (std::getline(ss, line)) {
    line = Trim(std::move(line));
    if (line.empty()) continue;
    auto j = Json::parse(line, nullptr, false);
    if (j.is_discarded() || !j.is_object()) continue;
    if (j.value("type", std::string{}) != "test") continue;
    std::string event = j.value("event", std::string{});
    if (event != "ok" && event != "failed" && event != "ignored") continue;
    std::string name = j.value("name", std::string{});
    if (name.empty()) continue;
    TestNode n;
    n.id = "cargo::" + name;
    n.parent_id = root.id;
    n.label = name;
    n.kind = TestKind::kCase;
    n.framework = "cargo";
    if (j.contains("exec_time")) {
      double t = 0.0;
      if (j["exec_time"].is_number()) t = j["exec_time"].get<double>();
      n.duration = std::chrono::milliseconds{static_cast<std::int64_t>(t * 1000.0)};
    }
    if (event == "ok")            n.status = TestStatus::kPassed;
    else if (event == "ignored")  n.status = TestStatus::kSkipped;
    else                          n.status = TestStatus::kFailed;
    if (n.status == TestStatus::kFailed)
      n.failure_message = j.value("stdout", std::string{});
    out.push_back(std::move(n));
  }
  return out;
}

// --- xUnit v2 ---------------------------------------------------------------

std::vector<TestNode> ParseXUnitReport(const std::string &xml) {
  std::vector<TestNode> out;
  std::string current_collection;
  std::size_t cursor = 0;
  while (auto tag = NextTag(xml, cursor)) {
    cursor = tag->end_pos;
    if (tag->closing) continue;
    if (tag->name == "collection") {
      std::string name = Attr(tag->body, "name");
      if (name.empty()) continue;
      TestNode suite;
      suite.id = "xunit::" + name;
      suite.label = name;
      suite.kind = TestKind::kSuite;
      suite.framework = "xunit";
      out.push_back(suite);
      current_collection = suite.id;
    } else if (tag->name == "test") {
      std::string name = Attr(tag->body, "name");
      std::string result = Attr(tag->body, "result");
      std::string time = Attr(tag->body, "time");
      TestNode n;
      n.id = "xunit::" + name;
      n.parent_id = current_collection;
      n.label = name;
      n.kind = TestKind::kCase;
      n.framework = "xunit";
      n.duration = Seconds(time);
      if (result == "Pass")      n.status = TestStatus::kPassed;
      else if (result == "Skip") n.status = TestStatus::kSkipped;
      else                       n.status = TestStatus::kFailed;
      if (!tag->self_closing) {
        std::size_t inner = cursor;
        while (auto sub = NextTag(xml, inner)) {
          inner = sub->end_pos;
          if (sub->closing && sub->name == "test") break;
          if (sub->closing) continue;
          if (sub->name == "message")
            n.failure_message = Trim(InnerText(xml, sub->end_pos, "message"));
        }
        cursor = inner;
      }
      out.push_back(std::move(n));
    }
  }
  return out;
}

// --- NUnit 3 ----------------------------------------------------------------

std::vector<TestNode> ParseNUnitReport(const std::string &xml) {
  std::vector<TestNode> out;
  std::vector<std::string> suite_stack;
  std::size_t cursor = 0;
  while (auto tag = NextTag(xml, cursor)) {
    cursor = tag->end_pos;
    if (tag->name == "test-suite") {
      if (tag->closing) {
        if (!suite_stack.empty()) suite_stack.pop_back();
        continue;
      }
      std::string name = Attr(tag->body, "fullname");
      if (name.empty()) name = Attr(tag->body, "name");
      TestNode s;
      s.id = "nunit::" + name;
      s.parent_id = suite_stack.empty() ? "" : suite_stack.back();
      s.label = name;
      s.kind = TestKind::kSuite;
      s.framework = "nunit";
      out.push_back(s);
      if (!tag->self_closing) suite_stack.push_back(s.id);
    } else if (tag->name == "test-case") {
      if (tag->closing) continue;
      std::string name = Attr(tag->body, "fullname");
      if (name.empty()) name = Attr(tag->body, "name");
      std::string result = Attr(tag->body, "result");
      std::string duration = Attr(tag->body, "duration");

      TestNode n;
      n.id = "nunit::" + name;
      n.parent_id = suite_stack.empty() ? "" : suite_stack.back();
      n.label = name;
      n.kind = TestKind::kCase;
      n.framework = "nunit";
      n.duration = Seconds(duration);
      if (result == "Passed")       n.status = TestStatus::kPassed;
      else if (result == "Skipped") n.status = TestStatus::kSkipped;
      else if (result == "Failed")  n.status = TestStatus::kFailed;
      else                          n.status = TestStatus::kErrored;
      if (!tag->self_closing) {
        std::size_t inner = cursor;
        while (auto sub = NextTag(xml, inner)) {
          inner = sub->end_pos;
          if (sub->closing && sub->name == "test-case") break;
          if (sub->closing) continue;
          if (sub->name == "message")
            n.failure_message = Trim(InnerText(xml, sub->end_pos, "message"));
        }
        cursor = inner;
      }
      out.push_back(std::move(n));
    }
  }
  return out;
}

}  // namespace polyglot::tools::ui::testing
