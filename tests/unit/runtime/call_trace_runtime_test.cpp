/**
 * @file     call_trace_runtime_test.cpp
 * @brief    Unit tests for runtime call_trace + profile_sink services
 *
 * @ingroup  Tests / Unit / Runtime
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "runtime/include/services/call_trace.h"
#include "runtime/include/services/profile_sink.h"

using polyglot::runtime::services::CallStats;
using polyglot::runtime::services::CallTraceSnapshot;
using polyglot::runtime::services::CallTracer;
using polyglot::runtime::services::ProfileSample;
using polyglot::runtime::services::ProfileSink;

namespace {

// Stable interned identifiers — the runtime hooks compare names by
// pointer (the IR pass emits the same .rodata symbol for enter / exit).
constexpr const char *kLang = "ploy";
const char *const kAlpha = "alpha_fn";
const char *const kBeta  = "beta_fn";
const char *const kOuter = "outer_fn";
const char *const kInner = "inner_fn";
const char *const kGhost = "ghost_fn";
const char *const kSample = "sample_fn";

const CallStats *FindStats(const CallTraceSnapshot &snap, const std::string &name) {
  for (const auto &entry : snap.entries) {
    if (entry.qualified_name == name) {
      return &entry;
    }
  }
  return nullptr;
}

std::string TmpPath(const char *name) {
  return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

TEST_CASE("CallTracer records enter/exit pairs", "[runtime][calltrace]") {
  CallTracer::Instance().Clear();
  __ploy_rt_call_trace_enable(1);

  __ploy_rt_call_enter(kAlpha, kLang);
  __ploy_rt_call_exit(kAlpha);
  __ploy_rt_call_enter(kAlpha, kLang);
  __ploy_rt_call_exit(kAlpha);

  auto snap = CallTracer::Instance().PeekSnapshot();
  const auto *stats = FindStats(snap, kAlpha);
  REQUIRE(stats != nullptr);
  REQUIRE(stats->call_count == 2);
  REQUIRE(stats->language == kLang);

  __ploy_rt_call_trace_enable(0);
  CallTracer::Instance().Clear();
}

TEST_CASE("CallTracer attributes nested self vs inclusive time",
          "[runtime][calltrace][nesting]") {
  CallTracer::Instance().Clear();
  __ploy_rt_call_trace_enable(1);

  __ploy_rt_call_enter(kOuter, kLang);
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  __ploy_rt_call_enter(kInner, kLang);
  std::this_thread::sleep_for(std::chrono::milliseconds(4));
  __ploy_rt_call_exit(kInner);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  __ploy_rt_call_exit(kOuter);

  auto snap = CallTracer::Instance().PeekSnapshot();
  const auto *outer = FindStats(snap, kOuter);
  const auto *inner = FindStats(snap, kInner);
  REQUIRE(outer != nullptr);
  REQUIRE(inner != nullptr);
  REQUIRE(outer->inclusive_ns >= inner->inclusive_ns);
  REQUIRE(outer->self_ns <= outer->inclusive_ns);

  __ploy_rt_call_trace_enable(0);
  CallTracer::Instance().Clear();
}

TEST_CASE("CallTracer is a no-op when disabled", "[runtime][calltrace][gating]") {
  CallTracer::Instance().Clear();
  __ploy_rt_call_trace_enable(0);
  REQUIRE(__ploy_rt_call_trace_is_enabled() == 0);

  __ploy_rt_call_enter(kGhost, kLang);
  __ploy_rt_call_exit(kGhost);

  auto snap = CallTracer::Instance().PeekSnapshot();
  REQUIRE(FindStats(snap, kGhost) == nullptr);
}

TEST_CASE("CallTracer JSON serialisation honours documented schema",
          "[runtime][calltrace][json]") {
  CallTracer::Instance().Clear();
  __ploy_rt_call_trace_enable(1);
  __ploy_rt_call_enter(kAlpha, kLang);
  __ploy_rt_call_exit(kAlpha);
  auto snap = CallTracer::Instance().PeekSnapshot();
  __ploy_rt_call_trace_enable(0);

  auto json = CallTracer::SerializeJson(snap);
  REQUIRE(json.find("polyglot.calltrace.v1") != std::string::npos);
  REQUIRE(json.find("\"entries\"") != std::string::npos);
  REQUIRE(json.find(kAlpha) != std::string::npos);

  CallTracer::Instance().Clear();
}

TEST_CASE("CallTracer DrainSnapshot empties the aggregate state",
          "[runtime][calltrace][drain]") {
  CallTracer::Instance().Clear();
  __ploy_rt_call_trace_enable(1);
  __ploy_rt_call_enter(kBeta, kLang);
  __ploy_rt_call_exit(kBeta);

  auto first = CallTracer::Instance().DrainSnapshot();
  REQUIRE(FindStats(first, kBeta) != nullptr);

  auto second = CallTracer::Instance().PeekSnapshot();
  REQUIRE(FindStats(second, kBeta) == nullptr);

  __ploy_rt_call_trace_enable(0);
}

TEST_CASE("ProfileSink writes both document and stream payloads",
          "[runtime][profilesink]") {
  CallTracer::Instance().Clear();
  __ploy_rt_call_trace_enable(1);
  __ploy_rt_call_enter(kSample, kLang);
  __ploy_rt_call_exit(kSample);
  ProfileSample sample{};
  sample.timestamp_ns = 1000;
  sample.window_ns = 200'000'000ULL;
  sample.calls = CallTracer::Instance().DrainSnapshot();
  sample.live_threads = 1;
  sample.resident_bytes = 4096;
  __ploy_rt_call_trace_enable(0);

  // Document mode.
  const auto doc_path = TmpPath("polyglot_profile_doc_test.json");
  {
    auto sink = ProfileSink::Open(doc_path, /*stream_mode=*/false);
    REQUIRE(sink != nullptr);
    sink->Push(sample);
    sink->Close();
  }
  std::ifstream doc_in(doc_path);
  std::stringstream doc_buf;
  doc_buf << doc_in.rdbuf();
  auto doc = doc_buf.str();
  REQUIRE(doc.find("polyglot.profile.v1") != std::string::npos);
  REQUIRE(doc.find(kSample) != std::string::npos);
  std::remove(doc_path.c_str());

  // Stream mode (NDJSON).
  const auto stream_path = TmpPath("polyglot_profile_stream_test.ndjson");
  {
    auto sink = ProfileSink::Open(stream_path, /*stream_mode=*/true);
    REQUIRE(sink != nullptr);
    REQUIRE(sink->IsStream());
    sink->Push(sample);
    sink->Push(sample);
    sink->Close();
  }
  std::ifstream stream_in(stream_path);
  std::string line;
  std::size_t lines = 0;
  while (std::getline(stream_in, line)) {
    if (!line.empty()) {
      REQUIRE(line.front() == '{');
      REQUIRE(line.back() == '}');
      ++lines;
    }
  }
  REQUIRE(lines == 2);
  std::remove(stream_path.c_str());
}
