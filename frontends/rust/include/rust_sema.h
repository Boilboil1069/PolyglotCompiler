/**
 * @file     rust_sema.h
 * @brief    Rust language frontend
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/rust/include/rust_ast.h"

namespace polyglot::rust {

class CrateLoader;

/// Optional inputs that augment the analyser with crate-graph resolution.
/// When `loader` is null the analyser falls back to the legacy behaviour of
/// declaring `use` paths as opaque module symbols.
struct RustSemaOptions {
    CrateLoader *loader{nullptr};
};

// Lightweight semantic pass for Rust: scopes, symbols, and basic types/captures.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);
void AnalyzeModule(const Module &module, frontends::SemaContext &context,
                   const RustSemaOptions &options);

} // namespace polyglot::rust
