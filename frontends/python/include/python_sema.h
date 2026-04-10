/**
 * @file     python_sema.h
 * @brief    Python language frontend
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/python/include/python_ast.h"

namespace polyglot::python {

void AnalyzeModule(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::python
