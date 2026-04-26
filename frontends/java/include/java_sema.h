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

// Forward declaration — full definition in class_file_reader.h.
class ClasspathLoader;

// Optional configuration passed by the language frontend.
struct JavaSemaOptions {
    // Loader used to resolve `import` statements against real .class / .jar
    // metadata.  When null, imports are recorded as opaque module symbols
    // (legacy behaviour, kept for unit tests that exercise sema in isolation).
    ClasspathLoader *classpath_loader{nullptr};
};

// Perform semantic analysis on a parsed Java compilation unit.
// 2-arg overload retained for backwards compatibility with existing tests.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);
void AnalyzeModule(const Module &module, frontends::SemaContext &context,
                   const JavaSemaOptions &options);

} // namespace polyglot::java
