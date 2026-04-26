/**
 * @file     ruby_sema.h
 * @brief    Ruby semantic analyzer
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/ruby/include/ruby_ast.h"

namespace polyglot::ruby {
void AnalyzeModule(const Module &mod, frontends::SemaContext &ctx);
}  // namespace polyglot::ruby
