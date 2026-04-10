/**
 * @file     parser_base.h
 * @brief    Shared frontend infrastructure
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>

#include "frontends/common/include/diagnostics.h"

namespace polyglot::frontends {

/** @brief ParserBase class. */
class ParserBase {
 public:
  explicit ParserBase(Diagnostics &diagnostics) : diagnostics_(diagnostics) {}
  virtual ~ParserBase() = default;

  virtual void ParseModule() = 0;

 protected:
  Diagnostics &diagnostics_;
};

}  // namespace polyglot::frontends
