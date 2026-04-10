/**
 * @file     cpp_sema.h
 * @brief    C++ language frontend
 *
 * @ingroup  Frontend / C++
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_ast.h"

namespace polyglot::cpp {

// Perform a lightweight semantic pass: build symbol scopes, basic type mapping, and mark captures.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::cpp
