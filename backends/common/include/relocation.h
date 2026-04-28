/**
 * @file     relocation.h
 * @brief    Forwarding header that exposes the target-independent relocation
 *           kinds defined in `backends/common/include/abi/relocation.h` under
 *           the historic `polyglot::backends::RelocType` name.
 *
 *           Note: the type name `polyglot::backends::Relocation` is
 *           intentionally *not* re-aliased here — the rich
 *           `polyglot::backends::Relocation` struct declared in
 *           `object_file.h` (used by `ELFBuilder` / `MachOBuilder`) keeps
 *           its meaning. Code that needs a target-independent relocation
 *           record should refer to `common::abi::RelocationEntry` directly.
 *
 * @ingroup  Backend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include "backends/common/include/abi/relocation.h"

namespace polyglot::backends {

/// @brief Backwards-compatible alias for the legacy `RelocType` enum.  The
///        new `RelocationKind` is a strict superset; the original three
///        enumerators (`kAbs32`, `kAbs64`, `kPcRel32`) keep their names.
using RelocType = ::polyglot::backends::common::abi::RelocationKind;

}  // namespace polyglot::backends
