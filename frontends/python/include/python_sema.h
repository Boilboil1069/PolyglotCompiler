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

class PyiLoader;

/// Optional inputs that augment the semantic analyser with real `.pyi`
/// resolution.  When `loader` is null the analyser falls back to its
/// hard-coded stdlib stub registry (preserved for tests that don't supply
/// stub paths).
struct PythonSemaOptions {
    PyiLoader *loader{nullptr};
};

void AnalyzeModule(const Module &module, frontends::SemaContext &context);
void AnalyzeModule(const Module &module, frontends::SemaContext &context,
                   const PythonSemaOptions &options);

} // namespace polyglot::python
