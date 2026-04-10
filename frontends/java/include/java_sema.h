/**
 * @file     java_sema.h
 * @brief    Java language frontend
 *
 * @ingroup  Frontend / Java
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/java/include/java_ast.h"

namespace polyglot::java {

// Perform semantic analysis on a parsed Java compilation unit.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::java
