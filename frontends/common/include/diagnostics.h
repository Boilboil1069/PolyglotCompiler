#pragma once

#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::frontends {

struct Diagnostic {
  core::SourceLoc loc{};
  std::string message;
};

class Diagnostics {
 public:
  void Report(const core::SourceLoc &loc, const std::string &message) {
    diagnostics_.push_back(Diagnostic{loc, message});
  }

  const std::vector<Diagnostic> &All() const { return diagnostics_; }

  bool HasErrors() const { return !diagnostics_.empty(); }

 private:
  std::vector<Diagnostic> diagnostics_{};
};

}  // namespace polyglot::frontends
