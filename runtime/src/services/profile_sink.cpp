/**
 * @file     profile_sink.cpp
 * @brief    Streaming sink implementation
 *
 * @ingroup  Runtime / Services
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#include "runtime/include/services/profile_sink.h"

#include <chrono>
#include <sstream>

namespace polyglot::runtime::services {

ProfileSink::ProfileSink(std::string path, bool stream_mode)
    : path_(std::move(path)), stream_mode_(stream_mode) {
  out_.open(path_, std::ios::out | std::ios::trunc | std::ios::binary);
}

ProfileSink::~ProfileSink() {
  Close();
}

std::unique_ptr<ProfileSink> ProfileSink::Open(const std::string &path, bool stream_mode) {
  std::unique_ptr<ProfileSink> sink(new ProfileSink(path, stream_mode));
  if (!sink->out_.is_open()) {
    return nullptr;
  }
  if (!stream_mode) {
    // Non-stream mode emits a JSON document with an outer array; the
    // opening bracket is written here and the closing bracket is
    // emitted by Close() so a partially-written file is still valid
    // JSON-prefix-extractable.
    sink->out_ << "{\"schema\":\"polyglot.profile.v1\",\"samples\":[";
    sink->out_.flush();
  }
  return sink;
}

void ProfileSink::Push(const ProfileSample &sample) {
  std::lock_guard<std::mutex> lk(mu_);
  if (!out_.is_open()) {
    return;
  }
  const std::string serialized = SerializeSample(sample);
  if (stream_mode_) {
    // Newline-delimited JSON: one record per line.
    out_ << serialized << '\n';
  } else {
    if (written_.load(std::memory_order_acquire) > 0) {
      out_ << ',';
    }
    out_ << serialized;
  }
  out_.flush();
  written_.fetch_add(1, std::memory_order_release);
}

void ProfileSink::Close() {
  std::lock_guard<std::mutex> lk(mu_);
  if (!out_.is_open()) {
    return;
  }
  if (!stream_mode_) {
    out_ << "]}";
  }
  out_.flush();
  out_.close();
}

std::string ProfileSink::SerializeSample(const ProfileSample &sample) {
  std::ostringstream os;
  os << "{\"timestamp_ns\":" << sample.timestamp_ns << ",\"window_ns\":" << sample.window_ns
     << ",\"live_threads\":" << sample.live_threads
     << ",\"resident_bytes\":" << sample.resident_bytes << ",\"calls\":"
     << CallTracer::SerializeJson(sample.calls) << '}';
  return os.str();
}

} // namespace polyglot::runtime::services
