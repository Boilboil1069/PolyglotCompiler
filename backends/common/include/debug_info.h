#pragma once

#include <string>
#include <vector>

namespace polyglot::backends {

struct DebugLineInfo {
  std::string file;
  int line{0};
  int column{0};
};

class DebugInfoBuilder {
 public:
  void AddLine(DebugLineInfo info) { lines_.push_back(std::move(info)); }
  const std::vector<DebugLineInfo> &Lines() const { return lines_; }

 private:
  std::vector<DebugLineInfo> lines_{};
};

}  // namespace polyglot::backends
