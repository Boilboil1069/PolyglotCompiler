#include "backends/common/include/debug_info.h"

#include <nlohmann/json.hpp>

namespace polyglot::backends {

std::string DebugInfoBuilder::EmitSourceMapJSON() const {
  nlohmann::json j;
  j["lines"] = nlohmann::json::array();
  for (const auto &l : lines_) {
    j["lines"].push_back({{"file", l.file}, {"line", l.line}, {"column", l.column}});
  }

  j["variables"] = nlohmann::json::array();
  for (const auto &v : variables_) {
    j["variables"].push_back({{"name", v.name},
                               {"type", v.type},
                               {"file", v.file},
                               {"line", v.line},
                               {"scopeDepth", v.scope_depth}});
  }

  j["types"] = nlohmann::json::array();
  for (const auto &t : types_) {
    j["types"].push_back({{"name", t.name},
                          {"kind", t.kind},
                          {"size", t.size},
                          {"alignment", t.alignment}});
  }

  j["symbols"] = nlohmann::json::array();
  for (const auto &s : symbols_) {
    j["symbols"].push_back({{"name", s.name},
                             {"section", s.section},
                             {"address", s.address},
                             {"size", s.size},
                             {"isFunction", s.is_function}});
  }

  return j.dump();
}

}  // namespace polyglot::backends
