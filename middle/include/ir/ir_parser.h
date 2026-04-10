/**
 * @file     ir_parser.h
 * @brief    Intermediate Representation infrastructure
 *
 * @ingroup  Middle / IR
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <memory>
#include <string>

#include "middle/include/ir/ir_context.h"

namespace polyglot::ir {

// Parse a textual IR module (printer-compatible) into the provided context.
// On success returns true and populates functions/globals; on failure returns false and fills msg.
bool ParseModule(const std::string &text, IRContext &ctx, std::string *msg = nullptr);

// Parse a single function from text (no surrounding module/globals) and append to ctx.
bool ParseFunction(const std::string &text, IRContext &ctx, std::shared_ptr<Function> *out_fn, std::string *msg = nullptr);

}  // namespace polyglot::ir
