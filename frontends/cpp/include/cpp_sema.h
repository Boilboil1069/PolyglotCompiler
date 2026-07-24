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

// Validate declarations and their types without resolving function/global
// initializer bodies.  Signature extraction uses this boundary because an
// angle-bracket system header is intentionally treated as an external
// dependency: its declarations are not imported into the local symbol table,
// so body references to those declarations cannot be resolved soundly here.
void AnalyzeModuleSignatures(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::cpp
