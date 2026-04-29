/**
 * @file     instrument_call_trace.h
 * @brief    Optional pass that injects __ploy_rt_call_enter/exit hooks
 *
 * @ingroup  Middle / Transform
 * @author   Manning Cyrus
 * @date     2026-04-29
 */
#pragma once

#include <string>
#include <vector>

#include "middle/include/ir/ir_context.h"

namespace polyglot::passes::transform {

// Records what the pass actually did so the driver can log it and tests
// can assert on instrumentation coverage.
/** @brief CallTraceInstrumentationStats data structure. */
struct CallTraceInstrumentationStats {
  std::size_t functions_visited{0};
  std::size_t functions_instrumented{0};
  std::size_t enter_calls_inserted{0};
  std::size_t exit_calls_inserted{0};
  std::vector<std::string> instrumented_names;
};

// Inject __ploy_rt_call_enter at the top of every non-bridge, non-
// external function and __ploy_rt_call_exit immediately before every
// return-style terminator.  The pass is idempotent: a function whose
// entry block already contains a __ploy_rt_call_enter call site is
// skipped so a binary that has been instrumented twice still only pays
// one pair of hook calls per invocation.
//
// The opt argument controls which language tag is attached to the call.
// When the function carries a language attribute we honour it, otherwise
// we fall back to the supplied default (typically "ploy" for the
// driver's main IR context).
CallTraceInstrumentationStats RunInstrumentCallTrace(ir::IRContext &context,
                                                     const std::string &default_language = "ploy");

} // namespace polyglot::passes::transform
