#pragma once

#include <string>

#include "backends/common/include/debug_info.h"

namespace polyglot::backends {

struct DebugEmitOptions {
  bool emit_dwarf{false};
  bool emit_pdb{false};
  bool emit_source_map{true};
};

class DebugEmitter {
 public:
  // Write a source map JSON sidecar to the given path.
  static bool EmitSourceMap(const DebugInfoBuilder &info, const std::string &path);

  // Placeholders for future DWARF/PDB emission; return false until implemented.
  static bool EmitDWARF(const DebugInfoBuilder &info, const std::string &path);
  static bool EmitPDB(const DebugInfoBuilder &info, const std::string &path);
};

}  // namespace polyglot::backends
