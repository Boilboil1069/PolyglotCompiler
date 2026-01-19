#pragma once

#include <memory>

#include "frontends/common/include/diagnostics.h"

namespace polyglot::frontends {

class ParserBase {
 public:
  explicit ParserBase(Diagnostics &diagnostics) : diagnostics_(diagnostics) {}
  virtual ~ParserBase() = default;

  virtual void ParseModule() = 0;

 protected:
  Diagnostics &diagnostics_;
};

}  // namespace polyglot::frontends
