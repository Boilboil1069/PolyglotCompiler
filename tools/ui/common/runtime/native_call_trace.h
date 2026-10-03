#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::runtime {

struct NativeTraceParameter {
  std::string name, type;
};
struct NativeTraceSite {
  std::string id, file, callee, language, result_type;
  int line{0}, column{0};
  std::vector<NativeTraceParameter> arguments;
};
struct NativeTraceValue {
  std::string name, type, display, raw_bits, unavailable_reason;
  bool available{false};
};
struct NativeTraceSample {
  std::string site_id, instance_id, parent_instance_id;
  std::uint64_t started_ticks{0}, ended_ticks{0}, ticks_per_second{0};
  long double duration_ns{0};
  std::vector<NativeTraceValue> arguments;
  NativeTraceValue result;
};

// A session contains samples from one invocation of an instrumented executable.
// The compiler's sidecar is authoritative for identities and ABI types. Runtime
// integers stay strings on the wire; JSON floating numbers are never accepted.
class NativeCallTrace {
public:
  bool LoadSites(const std::string &json, std::string &error);
  bool AppendEvent(const std::string &json_line, std::string &error);
  void Clear();
  const std::unordered_map<std::string, NativeTraceSite> &Sites() const { return sites_; }
  const std::vector<NativeTraceSample> &Samples() const { return samples_; }
  const NativeTraceSite *Site(const std::string &id) const;
  static NativeTraceValue Decode(const NativeTraceParameter &parameter, const std::string &bits);
  static constexpr std::size_t kMaxSamples = 50000;

private:
  std::unordered_map<std::string, NativeTraceSite> sites_;
  std::unordered_map<std::string, std::size_t> instance_ids_;
  std::vector<NativeTraceSample> samples_;
};

} // namespace polyglot::tools::ui::runtime
