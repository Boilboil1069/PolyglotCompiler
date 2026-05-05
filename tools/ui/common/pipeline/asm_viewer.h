/**
 * @file     asm_viewer.h
 * @brief    Compiler-Explorer-style asm viewer + source linkage.
 *
 * The asm viewer parses a textual disassembly produced by polyasm
 * or a backend disassembler, normalises it into per-function
 * blocks, and binds every asm line to the source line that
 * generated it (when DWARF / sourcemap data is available).  The
 * model supports x86_64, arm64 and wasm targets.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace polyglot::tools::ui::pipeline {

enum class AsmTarget {
  kX86_64,
  kArm64,
  kWasm,
};

std::string AsmTargetName(AsmTarget t);
std::optional<AsmTarget> AsmTargetFromName(const std::string &name);

struct AsmLine {
  int line_no{0};            ///< 1-based line number in the asm view.
  std::string text;          ///< Raw asm text.
  std::string source_file;   ///< Origin source file (may be empty).
  int source_line{0};        ///< Origin source line (0 when unknown).
};

struct AsmFunction {
  std::string name;
  int start_line{0};
  int end_line{0};
  std::vector<AsmLine> lines;
};

class AsmModule {
 public:
  /// Parse a textual disassembly.  Function boundaries are detected
  /// via labels of the form `<name>:` at column 0.  Source binding
  /// hints come from `.loc <file_id> <line>` directives (DWARF
  /// style) and from inline `; src=<file>:<line>` comments emitted
  /// by polyasm.
  static AsmModule Parse(AsmTarget target, const std::string &text);

  AsmTarget target() const { return target_; }
  const std::vector<AsmFunction> &functions() const { return functions_; }
  const AsmFunction *FindFunction(const std::string &name) const;

  /// Asm lines that map to `(source_file, source_line)`.
  std::vector<const AsmLine *> AsmForSource(const std::string &file,
                                            int line) const;

  /// Source location for `(function, line)` in the asm view; empty
  /// when no binding is known.
  std::optional<std::pair<std::string, int>> SourceForAsm(
      const std::string &function, int line_no) const;

 private:
  AsmTarget target_{AsmTarget::kX86_64};
  std::vector<AsmFunction> functions_;
};

}  // namespace polyglot::tools::ui::pipeline
