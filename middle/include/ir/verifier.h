/**
 * @file     verifier.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <string>

#include "middle/include/ir/data_layout.h"
#include "middle/include/ir/ir_context.h"

namespace polyglot::ir {

// Verification options: strict mode rejects placeholder/I64 fallbacks
// that are silently tolerated in the default (lenient) mode.
/** @brief VerifyOptions data structure. */
struct VerifyOptions {
    bool strict{false};  // When true, reject placeholder IR patterns
};

// Returns true if IR is well-formed. Optional message will contain first error.
bool Verify(const Function &func, std::string *msg = nullptr);
bool Verify(const Function &func, const DataLayout *layout, std::string *msg = nullptr);
bool Verify(const IRContext &ctx, std::string *msg = nullptr);
bool Verify(const IRContext &ctx, const VerifyOptions &opts, std::string *msg = nullptr);

}  // namespace polyglot::ir
