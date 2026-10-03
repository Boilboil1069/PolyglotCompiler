#include <bit>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "tools/ui/common/runtime/native_call_trace.h"

namespace polyglot::tools::ui::runtime {
namespace {
using Json = nlohmann::json;
std::uint64_t Unsigned(const std::string &text) {
  std::uint64_t value{};
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    throw std::runtime_error("expected an unsigned decimal string");
  return value;
}
std::uint64_t RawBits(const std::string &text) {
  if (!text.empty() && text.front() == '-') {
    std::int64_t signed_value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), signed_value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
      throw std::runtime_error("expected a 64-bit decimal bit pattern");
    return std::bit_cast<std::uint64_t>(signed_value);
  }
  return Unsigned(text);
}
std::string Decimal(const Json &json, const char *key) {
  const auto value = json.at(key).get<std::string>();
  Unsigned(value);
  return value;
}
} // namespace

void NativeCallTrace::Clear() {
  sites_.clear();
  instance_ids_.clear();
  samples_.clear();
}

const NativeTraceSite *NativeCallTrace::Site(const std::string &id) const {
  const auto found = sites_.find(id);
  return found == sites_.end() ? nullptr : &found->second;
}

bool NativeCallTrace::LoadSites(const std::string &text, std::string &error) {
  try {
    const auto json = Json::parse(text);
    if (json.at("schema") != "polyglot.native-call-sites.v1")
      throw std::runtime_error("unsupported native call-site schema");
    std::unordered_map<std::string, NativeTraceSite> sites;
    for (const auto &value : json.at("sites")) {
      NativeTraceSite site;
      site.id = Decimal(value, "id");
      site.file = value.at("source").at("file").get<std::string>();
      site.line = value.at("source").at("line").get<int>();
      site.column = value.at("source").at("column").get<int>();
      site.callee = value.at("callee").get<std::string>();
      site.language = value.at("language").get<std::string>();
      site.result_type = value.at("result_type").get<std::string>();
      if (site.file.empty() || site.line < 1 || site.column < 1 || site.callee.empty())
        throw std::runtime_error("call-site source location or callee is missing");
      for (const auto &argument : value.at("arguments"))
        site.arguments.push_back(
            {argument.at("name").get<std::string>(), argument.at("type").get<std::string>()});
      if (!sites.emplace(site.id, std::move(site)).second)
        throw std::runtime_error("duplicate call-site id");
    }
    Clear();
    sites_ = std::move(sites);
    error.clear();
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

NativeTraceValue NativeCallTrace::Decode(const NativeTraceParameter &parameter,
                                         const std::string &bits) {
  NativeTraceValue value{parameter.name, parameter.type, {}, bits, {}, false};
  const std::uint64_t raw = RawBits(bits);
  std::ostringstream output;
  if (parameter.type == "f64") {
    output << std::setprecision(std::numeric_limits<double>::max_digits10)
           << std::bit_cast<double>(raw);
  } else if (parameter.type == "f32") {
    output << std::setprecision(std::numeric_limits<float>::max_digits10)
           << std::bit_cast<float>(static_cast<std::uint32_t>(raw));
  } else if (parameter.type == "i1" || parameter.type == "bool") {
    if (raw > 1) {
      value.unavailable_reason = "invalid recorded boolean bits";
      return value;
    }
    output << (raw == 1 ? "true" : "false");
  } else if (parameter.type == "void") {
    value.unavailable_reason = "function has no return value";
    return value;
  } else if (parameter.type.size() >= 2 &&
             (parameter.type.front() == 'i' || parameter.type.front() == 'u')) {
    unsigned width{};
    const auto converted = std::from_chars(parameter.type.data() + 1,
                                           parameter.type.data() + parameter.type.size(), width);
    if (converted.ec != std::errc{} ||
        converted.ptr != parameter.type.data() + parameter.type.size() ||
        (width != 8 && width != 16 && width != 32 && width != 64)) {
      value.unavailable_reason = "ABI type is not supported for value decoding";
      return value;
    }
    std::uint64_t normalized = width == 64 ? raw : raw & ((std::uint64_t{1} << width) - 1);
    if (parameter.type.front() == 'u')
      output << normalized;
    else {
      if (width < 64 && (normalized & (std::uint64_t{1} << (width - 1))))
        normalized |= ~((std::uint64_t{1} << width) - 1);
      output << std::bit_cast<std::int64_t>(normalized);
    }
  } else {
    value.unavailable_reason = "pointer or non-scalar payload was not captured";
    return value;
  }
  value.available = true;
  value.display = output.str();
  return value;
}

bool NativeCallTrace::AppendEvent(const std::string &text, std::string &error) {
  try {
    if (samples_.size() >= kMaxSamples)
      throw std::runtime_error("trace sample limit reached (50000)");
    const auto json = Json::parse(text);
    if (json.at("schema") != "polyglot.native-calltrace.v1")
      throw std::runtime_error("unsupported native trace schema");
    NativeTraceSample sample;
    sample.site_id = Decimal(json, "site_id");
    sample.instance_id = Decimal(json, "instance_id");
    sample.parent_instance_id = Decimal(json, "parent_instance_id");
    const auto *site = Site(sample.site_id);
    if (!site)
      throw std::runtime_error("trace references an unknown call-site id");
    if (sample.instance_id == "0" || sample.instance_id == sample.parent_instance_id ||
        instance_ids_.contains(sample.instance_id))
      throw std::runtime_error("invalid or repeated trace instance id");
    sample.started_ticks = Unsigned(Decimal(json, "started_ticks"));
    sample.ended_ticks = Unsigned(Decimal(json, "ended_ticks"));
    sample.ticks_per_second = Unsigned(Decimal(json, "ticks_per_second"));
    if (!sample.ticks_per_second || sample.ended_ticks < sample.started_ticks)
      throw std::runtime_error("invalid trace clock interval");
    sample.duration_ns = static_cast<long double>(sample.ended_ticks - sample.started_ticks) *
                         1000000000.0L / sample.ticks_per_second;
    const auto &arguments = json.at("argument_bits");
    if (!arguments.is_array() || arguments.size() != site->arguments.size())
      throw std::runtime_error("runtime argument count differs from compiler metadata");
    for (std::size_t index = 0; index < arguments.size(); ++index)
      sample.arguments.push_back(
          Decode(site->arguments[index], arguments[index].get<std::string>()));
    sample.result =
        Decode({"return", site->result_type}, json.at("result_bits").get<std::string>());
    instance_ids_[sample.instance_id] = samples_.size();
    samples_.push_back(std::move(sample));
    error.clear();
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}
} // namespace polyglot::tools::ui::runtime
