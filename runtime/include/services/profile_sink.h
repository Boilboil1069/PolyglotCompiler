/**
 * @file     profile_sink.h
 * @brief    Streaming sink for runtime profiling samples
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "runtime/include/services/call_trace.h"

namespace polyglot::runtime::services {

// One profiling sample produced by ProfileSink.
//
// A sample is an aggregated view over a fixed wall-clock window
// (default 200 ms) and includes both the call tracer snapshot for that
// window and a coarse memory / thread footprint that the IDE renders on
// the timeline.
/** @brief ProfileSample data structure. */
struct ProfileSample {
  std::uint64_t timestamp_ns{0};
  std::uint64_t window_ns{0};
  CallTraceSnapshot calls;
  std::size_t live_threads{0};
  std::size_t resident_bytes{0};
};

/** @brief ProfileSink class. */
class ProfileSink {
public:
  // Open a file-backed sink.  When stream_mode is true the file is
  // truncated on open and every sample is appended as one JSON line so
  // the IDE side can tail-read in real time without having to lock the
  // whole file; this is the fan-out path used by `polyrt --stream`.
  static std::unique_ptr<ProfileSink> Open(const std::string &path, bool stream_mode);

  ~ProfileSink();

  // Push a new sample to the sink.  Thread-safe.
  void Push(const ProfileSample &sample);

  // Stop the writer thread (if any) and flush remaining samples.
  void Close();

  // JSON serialisation of a single sample.  The schema is documented
  // in docs/specs/profile_stream_schema_en.md.
  static std::string SerializeSample(const ProfileSample &sample);

  bool IsStream() const { return stream_mode_; }
  std::size_t WrittenSamples() const { return written_.load(std::memory_order_acquire); }

private:
  ProfileSink(std::string path, bool stream_mode);

  std::string path_;
  bool stream_mode_;
  std::ofstream out_;
  std::mutex mu_;
  std::atomic<std::size_t> written_{0};
};

} // namespace polyglot::runtime::services
