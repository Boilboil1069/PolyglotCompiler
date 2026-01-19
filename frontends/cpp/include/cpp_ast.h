#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common/include/core/source_loc.h"

namespace polyglot::cpp {

struct AstNode {
  virtual ~AstNode() = default;
  core::SourceLoc loc{};
};

struct Identifier : AstNode {
  std::string name;
};

struct Module : AstNode {
  std::vector<std::shared_ptr<AstNode>> declarations;
};

}  // namespace polyglot::cpp
