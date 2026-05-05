/**
 * @file     binary_container.cpp
 * @brief    Container resolution and OS<->container mapping.
 *
 * @ingroup  Common / Core
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "common/include/binary_container.h"

namespace polyglot::common {

const char *BinaryContainerName(BinaryContainer c) {
  switch (c) {
    case BinaryContainer::kAuto:  return "auto";
    case BinaryContainer::kELF:   return "elf";
    case BinaryContainer::kPE:    return "pe";
    case BinaryContainer::kMachO: return "macho";
    case BinaryContainer::kWasm:  return "wasm";
  }
  return "auto";
}

BinaryContainer ContainerForOS(OS o) {
  switch (o) {
    case OS::kWindows: return BinaryContainer::kPE;
    case OS::kDarwin:  return BinaryContainer::kMachO;
    case OS::kWasi:    return BinaryContainer::kWasm;
    case OS::kLinux:
    case OS::kFreeBSD:
    case OS::kNone:
    case OS::kUnknown:
      return BinaryContainer::kELF;
  }
  return BinaryContainer::kELF;
}

OS DefaultOSForContainer(BinaryContainer c) {
  switch (c) {
    case BinaryContainer::kPE:    return OS::kWindows;
    case BinaryContainer::kMachO: return OS::kDarwin;
    case BinaryContainer::kWasm:  return OS::kWasi;
    case BinaryContainer::kELF:   return OS::kLinux;
    case BinaryContainer::kAuto:  return OS::kUnknown;
  }
  return OS::kUnknown;
}

BinaryContainer ResolveContainer(const TargetTriple &triple,
                                  BinaryContainer requested) {
  if (requested != BinaryContainer::kAuto) return requested;
  if (triple.arch == Arch::kWasm32 || triple.arch == Arch::kWasm64)
    return BinaryContainer::kWasm;
  return ContainerForOS(triple.os);
}

ContainerSuffixes SuffixesFor(BinaryContainer c) {
  switch (c) {
    case BinaryContainer::kPE:    return {".exe",  ".dll",   ".lib", ".obj"};
    case BinaryContainer::kMachO: return {"",      ".dylib", ".a",   ".o"};
    case BinaryContainer::kELF:   return {"",      ".so",    ".a",   ".o"};
    case BinaryContainer::kWasm:  return {".wasm", ".wasm",  ".a",   ".o"};
    case BinaryContainer::kAuto:  return {"",      "",       "",     ""};
  }
  return {"", "", "", ""};
}

namespace {
bool ends_with_ci(const std::string &s, const char *suffix) {
  if (!suffix || !*suffix) return true;
  size_t n = 0;
  while (suffix[n]) ++n;
  if (s.size() < n) return false;
  for (size_t i = 0; i < n; ++i) {
    char a = s[s.size() - n + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}
}  // namespace

bool SuffixMatchesContainer(const std::string &path,
                            BinaryContainer container,
                            const std::string &kind) {
  if (container == BinaryContainer::kAuto) return true;
  ContainerSuffixes s = SuffixesFor(container);
  const char *expected = nullptr;
  if      (kind == "executable") expected = s.executable;
  else if (kind == "shared")     expected = s.shared_library;
  else if (kind == "static")     expected = s.static_library;
  else if (kind == "object")     expected = s.object;
  else                            return true;
  if (!expected || !*expected) {
    // For Unix executables: any suffix is acceptable, but reject
    // the obviously-wrong PE/Wasm extensions on non-PE/Wasm targets.
    if (kind == "executable") {
      if (ends_with_ci(path, ".exe")  && container != BinaryContainer::kPE)   return false;
      if (ends_with_ci(path, ".wasm") && container != BinaryContainer::kWasm) return false;
    }
    return true;
  }
  return ends_with_ci(path, expected);
}

}  // namespace polyglot::common
