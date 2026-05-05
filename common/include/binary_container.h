/**
 * @file     binary_container.h
 * @brief    Binary container abstraction (ELF / PE / Mach-O / WASM)
 *           and triple-driven dispatch.
 *
 * @ingroup  Common / Core
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include "common/include/target_triple.h"

namespace polyglot::common {

enum class BinaryContainer {
  kAuto,    ///< Resolved from the target triple at link time.
  kELF,
  kPE,
  kMachO,
  kWasm,
};

const char *BinaryContainerName(BinaryContainer c);

/// Map an OS to its native container.  `OS::kNone` and
/// `OS::kUnknown` map to `kELF` (the historical default).
BinaryContainer ContainerForOS(OS o);

/// Inverse of `ContainerForOS`: pick a representative OS for a
/// container.  Used by legacy callers that still hold an `OS`.
OS DefaultOSForContainer(BinaryContainer c);

/// Resolve the effective container for a given triple and an
/// explicit user request:
///   * `requested != kAuto`  → returned verbatim;
///   * triple is wasm32/wasm64 → `kWasm`;
///   * otherwise               → `ContainerForOS(triple.os)`.
BinaryContainer ResolveContainer(const TargetTriple &triple,
                                  BinaryContainer requested);

/// File suffixes a given container expects for the three artefact
/// kinds.  Empty strings mean "no suffix" (Unix executables, static
/// archives may differ per container).
struct ContainerSuffixes {
  const char *executable;
  const char *shared_library;
  const char *static_library;
  const char *object;
};
ContainerSuffixes SuffixesFor(BinaryContainer c);

/// Returns true when `path`'s suffix is consistent with `container`
/// for the given artefact kind.  Used by `polyc`/`polyld` to emit
/// the `polyc-warn-W2101` warning when a user-provided `-o` path
/// contradicts the resolved container.  Recognised kinds:
///   * "executable" / "shared" / "static" / "object".
bool SuffixMatchesContainer(const std::string &path,
                            BinaryContainer container,
                            const std::string &kind);

}  // namespace polyglot::common
