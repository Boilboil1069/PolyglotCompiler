/**
 * @file     abi.h
 * @brief    Forwarding header that exposes the `polyglot::backends::common::
 *           abi` calling-convention model under the historic
 *           `polyglot::backends::ABI` name.  New code should include the
 *           sub-namespace umbrella `backends/common/include/abi/abi.h`
 *           directly and refer to `common::abi::AbiDescriptor` /
 *           `common::abi::CallingConvention` etc.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "backends/common/include/abi/abi.h"

namespace polyglot::backends {

/// @brief Backwards-compatible alias for the legacy `ABI` struct.  The new
///        `AbiDescriptor` keeps the original `name` and `pointer_size`
///        fields and adds `stack_alignment` / `red_zone_size`.
using ABI = ::polyglot::backends::common::abi::AbiDescriptor;

}  // namespace polyglot::backends
