/**
 * @file     go_sema.h
 * @brief    Go language semantic analyzer
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#pragma once

#include "frontends/common/include/sema_context.h"
#include "frontends/go/include/go_ast.h"

namespace polyglot::go {

void AnalyzeFile(const File &file, frontends::SemaContext &ctx);

}  // namespace polyglot::go
