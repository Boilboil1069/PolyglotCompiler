#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Symbol {
  std::string name;
  std::uint64_t address{0};
  std::uint64_t size{0};
  bool defined{false};
  std::string section;
};

struct Section {
  std::string name;
  std::vector<std::uint8_t> data;
  std::uint64_t address{0};
  std::uint64_t size{0};  // declared size (for bss)
  bool bss{false};
};

enum class RelocType { kAbs64, kRel32 };

struct Relocation {
  std::string section;   // section this relocation applies to
  std::size_t offset{0}; // offset within section data
  RelocType type{RelocType::kAbs64};
  std::string symbol;    // target symbol name
  std::int64_t addend{0};
};

// Simple custom object container format ("POBJ")
#pragma pack(push, 1)
struct FileHeader {
  char magic[4];
  std::uint16_t version{1};
  std::uint16_t section_count{0};
  std::uint16_t symbol_count{0};
  std::uint16_t reloc_count{0};
  std::uint64_t strtab_offset{0};
};

struct SectionRecord {
  std::uint32_t name_offset{0};
  std::uint32_t flags{0};  // bit0: alloc/data, bit1: bss
  std::uint64_t offset{0};
  std::uint64_t size{0};
};

struct SymbolRecord {
  std::uint32_t name_offset{0};
  std::uint32_t section_index{0xFFFFFFFF};  // 0xFFFFFFFF => undefined
  std::uint64_t value{0};
  std::uint64_t size{0};
  std::uint8_t binding{0};  // 0: local, 1: global
  std::uint8_t reserved[3]{};
};

struct RelocRecord {
  std::uint32_t section_index{0};
  std::uint64_t offset{0};
  std::uint32_t type{0};
  std::uint32_t symbol_index{0};
  std::int64_t addend{0};
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 20, "Unexpected header size");
static_assert(sizeof(SectionRecord) == 24, "Unexpected section record size");
static_assert(sizeof(SymbolRecord) == 28, "Unexpected symbol record size");
static_assert(sizeof(RelocRecord) == 28, "Unexpected reloc record size");

constexpr std::uint32_t kSectionFlagBss = 1u << 1;

std::string LoadString(const std::vector<std::uint8_t> &strtab, std::uint32_t offset) {
  if (offset >= strtab.size()) return {};
  const char *base = reinterpret_cast<const char *>(strtab.data());
  return std::string(base + offset);
}

}  // namespace

namespace polyglot::tools {

struct ObjectFile {
  std::string name;
  std::unordered_map<std::string, Symbol> symbols;
  std::vector<Section> sections;
  std::vector<Relocation> relocs;
};

bool LoadObjectFile(const std::string &path, ObjectFile &obj) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open()) return false;

  FileHeader hdr{};
  ifs.read(reinterpret_cast<char *>(&hdr), sizeof(hdr));
  if (ifs.gcount() != static_cast<std::streamsize>(sizeof(hdr))) return false;
  if (std::strncmp(hdr.magic, "POBJ", 4) != 0 || hdr.version != 1) return false;

  std::vector<SectionRecord> section_recs(hdr.section_count);
  if (!section_recs.empty()) {
    ifs.read(reinterpret_cast<char *>(section_recs.data()), static_cast<std::streamsize>(section_recs.size() * sizeof(SectionRecord)));
    if (ifs.gcount() != static_cast<std::streamsize>(section_recs.size() * sizeof(SectionRecord))) return false;
  }

  std::vector<SymbolRecord> symbol_recs(hdr.symbol_count);
  if (!symbol_recs.empty()) {
    ifs.read(reinterpret_cast<char *>(symbol_recs.data()), static_cast<std::streamsize>(symbol_recs.size() * sizeof(SymbolRecord)));
    if (ifs.gcount() != static_cast<std::streamsize>(symbol_recs.size() * sizeof(SymbolRecord))) return false;
  }

  std::vector<RelocRecord> reloc_recs(hdr.reloc_count);
  if (!reloc_recs.empty()) {
    ifs.read(reinterpret_cast<char *>(reloc_recs.data()), static_cast<std::streamsize>(reloc_recs.size() * sizeof(RelocRecord)));
    if (ifs.gcount() != static_cast<std::streamsize>(reloc_recs.size() * sizeof(RelocRecord))) return false;
  }

  // Load string table
  ifs.seekg(0, std::ios::end);
  auto file_size = static_cast<std::size_t>(ifs.tellg());
  if (hdr.strtab_offset > file_size) return false;
  std::size_t strtab_size = file_size - static_cast<std::size_t>(hdr.strtab_offset);
  std::vector<std::uint8_t> strtab(strtab_size);
  ifs.seekg(static_cast<std::streamoff>(hdr.strtab_offset), std::ios::beg);
  if (strtab_size > 0) {
    ifs.read(reinterpret_cast<char *>(strtab.data()), static_cast<std::streamsize>(strtab_size));
    if (ifs.gcount() != static_cast<std::streamsize>(strtab_size)) return false;
  }

  obj.name = path;
  obj.sections.reserve(section_recs.size());
  for (std::size_t i = 0; i < section_recs.size(); ++i) {
    const auto &rec = section_recs[i];
    Section sec;
    sec.name = LoadString(strtab, rec.name_offset);
    sec.size = rec.size;
    sec.bss = (rec.flags & kSectionFlagBss) != 0;
    if (!sec.bss && rec.size > 0) {
      sec.data.resize(static_cast<std::size_t>(rec.size));
      ifs.seekg(static_cast<std::streamoff>(rec.offset), std::ios::beg);
      ifs.read(reinterpret_cast<char *>(sec.data.data()), static_cast<std::streamsize>(rec.size));
      if (ifs.gcount() != static_cast<std::streamsize>(rec.size)) return false;
    }
    obj.sections.push_back(std::move(sec));
  }

  std::vector<std::string> symbol_names;
  symbol_names.reserve(symbol_recs.size());
  for (const auto &rec : symbol_recs) {
    Symbol sym;
    sym.name = LoadString(strtab, rec.name_offset);
    sym.size = rec.size;
    if (rec.section_index != 0xFFFFFFFF && rec.section_index < section_recs.size()) {
      sym.defined = true;
      sym.section = obj.sections[rec.section_index].name;
      sym.address = rec.value;
    }
    obj.symbols[sym.name] = sym;
    symbol_names.push_back(sym.name);
  }

  for (const auto &rec : reloc_recs) {
    if (rec.section_index >= obj.sections.size() || rec.symbol_index >= symbol_names.size()) return false;
    Relocation r;
    r.section = obj.sections[rec.section_index].name;
    r.offset = rec.offset;
    r.type = (rec.type == 0) ? RelocType::kAbs64 : RelocType::kRel32;
    r.symbol = symbol_names[rec.symbol_index];
    r.addend = rec.addend;
    obj.relocs.push_back(std::move(r));
  }

  return true;
}

class Linker {
 public:
  void AddObject(const ObjectFile &obj) { objects_.push_back(obj); }
  bool AddObjectFromFile(const std::string &path) {
    ObjectFile obj;
    if (!LoadObjectFile(path, obj)) return false;
    objects_.push_back(std::move(obj));
    return true;
  }

  // Very simplified linker: assign addresses sequentially, merge .text/.data/.bss, and report undefined symbols.
  std::string Link(const std::string &output) {
    section_offsets_.clear();
    Section text{.name = ".text"};
    Section data{.name = ".data"};
    Section bss{.name = ".bss"};

    std::unordered_map<std::string, Symbol> symtab;
    auto mangle = [](const std::string &name) {
      // Simple Itanium-like: leave names as-is, prefix global with '_' for demonstration.
      if (!name.empty() && name[0] == '_') return name;
      return std::string("_") + name;
    };
    for (const auto &obj : objects_) {
      for (const auto &[name, sym] : obj.symbols) {
        std::string key = mangle(name);
        Symbol entry = sym;
        entry.name = key;
        if (sym.defined) {
          symtab[key] = entry;
        } else if (symtab.find(key) == symtab.end()) {
          symtab[key] = entry;  // keep placeholder for undefined
        }
      }
    }

    std::uint64_t text_base = 0x1000;
    std::uint64_t data_base = 0x8000;
    std::uint64_t bss_base = 0xA000;
    std::uint64_t text_cursor = text_base;
    std::uint64_t data_cursor = data_base;
    std::uint64_t bss_cursor = bss_base;

    // Track section base addresses for relocation application
    std::unordered_map<std::string, std::uint64_t> section_base{{".text", text_base}, {".data", data_base}, {".bss", bss_base}};

    for (const auto &obj : objects_) {
      for (const auto &sec : obj.sections) {
        std::size_t sec_size = sec.size ? static_cast<std::size_t>(sec.size) : sec.data.size();
        if (sec.name == ".text") {
          auto offset = text_cursor - text_base;
          text.data.insert(text.data.end(), sec.data.begin(), sec.data.end());
          if (sec.data.size() < sec_size) text.data.resize(text.data.size() + (sec_size - sec.data.size()), 0);
          text_cursor += sec_size;
          section_offsets_[obj.name + sec.name] = offset;
        } else if (sec.name == ".data") {
          auto offset = data_cursor - data_base;
          data.data.insert(data.data.end(), sec.data.begin(), sec.data.end());
          if (sec.data.size() < sec_size) data.data.resize(data.data.size() + (sec_size - sec.data.size()), 0);
          data_cursor += sec_size;
          section_offsets_[obj.name + sec.name] = offset;
        } else if (sec.name == ".bss" || sec.bss) {
          auto offset = bss_cursor - bss_base;
          bss_cursor += sec_size;
          section_offsets_[obj.name + sec.name] = offset;
        }
      }
    }

    // Finalize merged section base addresses for relocation math
    text.address = text_base;
    data.address = data_base;
    bss.address = bss_base;

    // Fix up symbol absolute addresses now that layout is known
    for (const auto &obj : objects_) {
      for (const auto &[name, sym] : obj.symbols) {
        if (!sym.defined) continue;
        auto base_it = section_base.find(sym.section);
        auto off_it = section_offsets_.find(obj.name + sym.section);
        if (base_it == section_base.end() || off_it == section_offsets_.end()) continue;
        auto it = symtab.find(mangle(name));
        if (it == symtab.end()) continue;
        it->second.address = base_it->second + off_it->second + sym.address;
      }
    }

    // Apply relocations (only handles relocations into merged .text/.data)
    auto apply_reloc = [&](Section &merged, const Relocation &r, std::uint64_t offset_in_merged) {
      auto sym_it = symtab.find(mangle(r.symbol));
      if (sym_it == symtab.end() || !sym_it->second.defined) return false;
      std::uint64_t S = sym_it->second.address + r.addend;
      std::uint64_t P = merged.address + offset_in_merged;
      if (offset_in_merged + (r.type == RelocType::kAbs64 ? 8 : 4) > merged.data.size()) return false;
      if (r.type == RelocType::kAbs64) {
        for (int i = 0; i < 8; ++i) merged.data[offset_in_merged + i] = static_cast<std::uint8_t>((S >> (i * 8)) & 0xFF);
      } else if (r.type == RelocType::kRel32) {
        std::int64_t rel = static_cast<std::int64_t>(S) - static_cast<std::int64_t>(P);
        for (int i = 0; i < 4; ++i) merged.data[offset_in_merged + i] = static_cast<std::uint8_t>((rel >> (i * 8)) & 0xFF);
      }
      return true;
    };

    for (const auto &obj : objects_) {
      for (const auto &rel : obj.relocs) {
        auto off_it = section_offsets_.find(obj.name + rel.section);
        std::uint64_t base_off = (off_it != section_offsets_.end()) ? off_it->second : 0;
        if (rel.section == ".text") {
          apply_reloc(text, rel, base_off + rel.offset);
        } else if (rel.section == ".data") {
          apply_reloc(data, rel, base_off + rel.offset);
        }
      }
    }

    std::vector<std::string> undefined;
    for (const auto &[name, sym] : symtab) {
      if (!sym.defined) undefined.push_back(name);
    }

    if (!undefined.empty()) {
      std::cerr << "Undefined symbols:\n";
      for (auto &u : undefined) std::cerr << "  " << u << "\n";
    }

    std::ofstream ofs(output, std::ios::binary | std::ios::trunc);
    if (ofs.is_open()) {
      ofs.write(reinterpret_cast<const char *>(text.data.data()), static_cast<std::streamsize>(text.data.size()));
      ofs.write(reinterpret_cast<const char *>(data.data.data()), static_cast<std::streamsize>(data.data.size()));
    }
    return output;
  }

 private:
  std::vector<ObjectFile> objects_{};
  std::unordered_map<std::string, std::uint64_t> section_offsets_{};  // key: obj+section
};

}  // namespace polyglot::tools

bool WriteDemoObject(const std::string &path) {
  // Build a tiny object: .text with two NOPs and a rel32 call to puts, plus _start symbol.
  std::vector<std::uint8_t> strtab{0};
  auto add_str = [&](const std::string &s) {
    std::uint32_t off = static_cast<std::uint32_t>(strtab.size());
    strtab.insert(strtab.end(), s.begin(), s.end());
    strtab.push_back(0);
    return off;
  };

  std::vector<std::uint8_t> text_data{0x90, 0x90, 0x00, 0x00, 0x00, 0x00};

  SectionRecord text_sec{};
  text_sec.name_offset = add_str(".text");
  text_sec.flags = 0;
  text_sec.size = text_data.size();

  std::vector<SectionRecord> sections{std::move(text_sec)};

  SymbolRecord start_sym{};
  start_sym.name_offset = add_str("_start");
  start_sym.section_index = 0;  // .text
  start_sym.value = 0;
  start_sym.size = text_data.size();
  start_sym.binding = 1;  // global

  SymbolRecord puts_sym{};
  puts_sym.name_offset = add_str("puts");
  puts_sym.section_index = 0xFFFFFFFF;  // undefined
  puts_sym.binding = 1;

  std::vector<SymbolRecord> symbols{start_sym, puts_sym};

  RelocRecord rel{};
  rel.section_index = 0;  // .text
  rel.offset = 2;         // patch bytes 2..5
  rel.type = 1;           // Rel32
  rel.symbol_index = 1;   // puts
  rel.addend = -4;

  std::vector<RelocRecord> relocs{rel};

  FileHeader hdr{};
  std::memcpy(hdr.magic, "POBJ", 4);
  hdr.version = 1;
  hdr.section_count = static_cast<std::uint16_t>(sections.size());
  hdr.symbol_count = static_cast<std::uint16_t>(symbols.size());
  hdr.reloc_count = static_cast<std::uint16_t>(relocs.size());

  std::size_t header_bytes = sizeof(FileHeader);
  std::size_t table_bytes = sections.size() * sizeof(SectionRecord) + symbols.size() * sizeof(SymbolRecord) + relocs.size() * sizeof(RelocRecord);
  std::size_t cursor = header_bytes + table_bytes;

  sections[0].offset = cursor;
  cursor += text_data.size();
  hdr.strtab_offset = cursor;

  std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) return false;
  ofs.write(reinterpret_cast<const char *>(&hdr), static_cast<std::streamsize>(sizeof(hdr)));
  ofs.write(reinterpret_cast<const char *>(sections.data()), static_cast<std::streamsize>(sections.size() * sizeof(SectionRecord)));
  ofs.write(reinterpret_cast<const char *>(symbols.data()), static_cast<std::streamsize>(symbols.size() * sizeof(SymbolRecord)));
  ofs.write(reinterpret_cast<const char *>(relocs.data()), static_cast<std::streamsize>(relocs.size() * sizeof(RelocRecord)));
  ofs.write(reinterpret_cast<const char *>(text_data.data()), static_cast<std::streamsize>(text_data.size()));
  ofs.write(reinterpret_cast<const char *>(strtab.data()), static_cast<std::streamsize>(strtab.size()));
  return ofs.good();
}

int main() {
  const std::string demo_obj = "demo.pobj";
  if (!WriteDemoObject(demo_obj)) {
    std::cerr << "failed to write demo object\n";
    return 1;
  }

  polyglot::tools::Linker ld;
  if (!ld.AddObjectFromFile(demo_obj)) {
    std::cerr << "failed to load demo object\n";
    return 1;
  }

  std::cout << ld.Link("a.out") << "\n";
  return 0;
}
