// Assembly and object output share one emitter, including frame layout and fixups.
#include "backends/arm64/include/arm64_target.h"
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace polyglot::backends::arm64 {
std::string Arm64Target::EmitAssembly() {
  const auto mc = EmitObjectCode();
  const bool darwin = target_os_ == "darwin" || target_os_ == "macos";
  std::ostringstream out;
  auto symbol = [](const std::string &name) {
    std::string quoted = "\"";
    for (char c : name) { if (c == '\\' || c == '\"') quoted += '\\'; quoted += c; }
    return quoted + "\"";
  };
  out << "// Exact native encoding; symbolic relocations remain assemblable.\n";
  for (const auto &section : mc.sections) {
    if (darwin) out << (section.name == ".text" ? ".section __TEXT,__text,regular,pure_instructions\n" : section.name == ".data" ? ".section __DATA,__data\n" : ".section __TEXT,__const\n");
    else out << ".section " << (section.name == ".rdata" ? ".rodata" : section.name) << "\n";
    if (section.name == ".text") out << ".p2align 2\n";
    std::multimap<std::size_t, const MCSymbol *> labels;
    std::map<std::size_t, const MCReloc *> relocs;
    for (const auto &s : mc.symbols) if (s.defined && s.section == section.name) labels.emplace(s.value, &s);
    for (const auto &r : mc.relocs) if (r.section == section.name) relocs.emplace(r.offset, &r);
    for (std::size_t i = 0; i <= section.data.size();) {
      const auto [first, last] = labels.equal_range(i);
      for (auto it = first; it != last; ++it) {
        const auto &s = *it->second;
        if (s.global) out << ".globl " << symbol(s.name) << "\n";
        out << symbol(s.name) << ":\n";
      }
      if (i == section.data.size()) break;
      const auto found = relocs.find(i);
      if (found != relocs.end()) {
        const auto &r = *found->second;
        const std::string name = symbol(r.symbol) + (r.addend ? "+" + std::to_string(r.addend) : "");
        if (r.type == 0) { out << "  .quad " << name << "\n"; i += 8; continue; }
        const auto reg = section.data.at(i) & 31;
        if (r.type == 1) out << "  bl " << name;
        else if (r.type == 2) out << "  adrp x" << unsigned(reg) << ", " << name << (darwin ? "@PAGE" : "");
        else if (r.type == 3) out << "  add x" << unsigned(reg) << ", x" << unsigned(reg) << ", " << (darwin ? "" : ":lo12:") << name << (darwin ? "@PAGEOFF" : "");
        else throw std::runtime_error("unsupported ARM64 assembly relocation");
        out << "\n"; i += 4; continue;
      }
      out << "  .byte " << unsigned(section.data[i++]);
      for (unsigned n = 1; n < 16 && i < section.data.size() && !labels.count(i) && !relocs.count(i); ++n)
        out << ", " << unsigned(section.data[i++]);
      out << "\n";
    }
  }
  return out.str();
}
} // namespace polyglot::backends::arm64
