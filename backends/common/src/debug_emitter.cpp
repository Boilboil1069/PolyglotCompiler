#include "backends/common/include/debug_emitter.h"

#include <fstream>

namespace polyglot::backends {

bool DebugEmitter::EmitSourceMap(const DebugInfoBuilder &info, const std::string &path) {
  std::ofstream os(path, std::ios::out | std::ios::trunc);
  if (!os.is_open()) return false;
  os << info.EmitSourceMapJSON();
  return os.good();
}

bool DebugEmitter::EmitDWARF(const DebugInfoBuilder &, const std::string &) {
  // TODO: Implement DWARF emission using an appropriate library or custom encoder.
  return false;
}

bool DebugEmitter::EmitPDB(const DebugInfoBuilder &, const std::string &) {
  // TODO: Implement PDB emission for Windows targets.
  return false;
}

}  // namespace polyglot::backends
