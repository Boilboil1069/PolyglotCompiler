/**
 * @file     javascript_sema.h
 * @brief    JavaScript semantic analyzer
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/javascript/include/javascript_ast.h"

namespace polyglot::javascript {

/// Run semantic analysis on a parsed JavaScript module.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);

}  // namespace polyglot::javascript
