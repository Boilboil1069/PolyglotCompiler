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

// Lightweight semantic pass for Rust: scopes, symbols, and basic types/captures.
void AnalyzeModule(const Module &module, frontends::SemaContext &context);

} // namespace polyglot::rust
