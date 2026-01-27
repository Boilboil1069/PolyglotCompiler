#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::backends {

struct DebugLineInfo {
  std::string file;
  int line{0};
  int column{0};
};

struct DebugVariable {
  std::string name;
  std::string type;
  std::string file;
  int line{0};
  int scope_depth{0};
};

struct DebugType {
  std::string name;
  std::string kind;
  std::size_t size{0};
  std::size_t alignment{0};
};

struct DebugSymbol {
  std::string name;
  std::string section;
  std::uint64_t address{0};
  std::uint64_t size{0};
  bool is_function{false};
};

class DebugInfoBuilder {
 public:
  void AddVariable(DebugVariable var) { variables_.push_back(std::move(var)); }
  void AddType(DebugType ty) { types_.push_back(std::move(ty)); }
  void AddSymbol(DebugSymbol sym) { symbols_.push_back(std::move(sym)); }
  void AddLine(DebugLineInfo info) { lines_.push_back(std::move(info)); }
  const std::vector<DebugLineInfo> &Lines() const { return lines_; }
  const std::vector<DebugVariable> &Variables() const { return variables_; }
  const std::vector<DebugType> &Types() const { return types_; }
  const std::vector<DebugSymbol> &Symbols() const { return symbols_; }

  // Emit a simple source map (JSON) capturing lines, variables, types, and symbols.
  std::string EmitSourceMapJSON() const;

 private:
  std::vector<DebugLineInfo> lines_{};
  std::vector<DebugVariable> variables_{};
  std::vector<DebugType> types_{};
  std::vector<DebugSymbol> symbols_{};
};

}  // namespace polyglot::backends
