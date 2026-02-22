#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <type_traits>
#if __has_include(<elf.h>)
#include <elf.h>
#else
// Minimal ELF64 definitions for platforms without <elf.h> (e.g., macOS)
using Elf64_Half = std::uint16_t;
using Elf64_Word = std::uint32_t;
using Elf64_Sword = std::int32_t;
using Elf64_Xword = std::uint64_t;
using Elf64_Sxword = std::int64_t;
using Elf64_Addr = std::uint64_t;
using Elf64_Off = std::uint64_t;

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
};

struct Elf64_Shdr {
    Elf64_Word sh_name;
    Elf64_Word sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr sh_addr;
    Elf64_Off sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word sh_link;
    Elf64_Word sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
};

struct Elf64_Sym {
    Elf64_Word st_name;
    unsigned char st_info;
    unsigned char st_other;
    Elf64_Half st_shndx;
    Elf64_Addr st_value;
    Elf64_Xword st_size;
};

struct Elf64_Rela {
    Elf64_Addr r_offset;
    Elf64_Xword r_info;
    Elf64_Sxword r_addend;
};

#define ELFMAG "\177ELF"
#define SELFMAG 4
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_REL 1
#define EM_X86_64 62
#define EM_AARCH64 183
#define SHN_UNDEF 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_FUNC 2
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_AARCH64_JUMP26 282
#define R_AARCH64_CALL26 283
#define ELF64_ST_INFO(b, t) (((b) << 4) + ((t) & 0x0f))
#define ELF64_R_INFO(sym, type) ((((Elf64_Xword)(sym)) << 32) + ((type) & 0xffffffffULL))
#endif
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#if defined(__APPLE__)
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>
#endif
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "backends/x86_64/include/x86_target.h"
#include "backends/arm64/include/arm64_target.h"
#include "backends/wasm/include/wasm_target.h"
#include "common/include/core/source_loc.h"
#include "frontends/common/include/diagnostics.h"
#include "frontends/common/include/preprocessor.h"
#include "frontends/common/include/sema_context.h"
#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"
#include "frontends/python/include/python_sema.h"
#include "frontends/python/include/python_lowering.h"
#include "frontends/cpp/include/cpp_sema.h"
#include "frontends/rust/include/rust_sema.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"
#include "frontends/rust/include/rust_lowering.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/java/include/java_lexer.h"
#include "frontends/java/include/java_parser.h"
#include "frontends/java/include/java_sema.h"
#include "frontends/java/include/java_lowering.h"
#include "frontends/dotnet/include/dotnet_lexer.h"
#include "frontends/dotnet/include/dotnet_parser.h"
#include "frontends/dotnet/include/dotnet_sema.h"
#include "frontends/dotnet/include/dotnet_lowering.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/nodes/statements.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/ssa.h"
#include "middle/include/ir/verifier.h"
#include "middle/include/ir/passes/opt.h"
#include "middle/include/passes/transform/advanced_optimizations.h"
#include "tools/polyld/include/polyglot_linker.h"
#include "runtime/include/libs/base.h"

namespace polyglot::tools {
namespace {

namespace fs = std::filesystem;

enum class RegAllocChoice { kLinearScan, kGraphColoring };

struct Settings {
    std::string source{};
    std::string source_path{};            // original file path (empty if inline)
    std::string language{"ploy"};
    bool language_explicit{false};        // true if --lang= was specified
    std::string arch{"x86_64"};
    bool pp_cpp{true};
    bool pp_rust{false};
    bool pp_python{false};
    bool force{false};
    bool verbose{true};                   // progress output (default on)
    bool emit_aux{true};                  // emit aux/ intermediate files (default on)
    std::vector<std::string> include_paths{"."};
    std::string mode{"link"};  // compile | assemble | link (stub)
#if defined(_WIN32)
    std::string obj_format{"coff"};  // auto-detect COFF on Windows
#elif defined(__APPLE__)
    std::string obj_format{"macho"};
#else
    std::string obj_format{"elf"};
#endif
    std::string output{"a.out"};
    std::string emit_obj_path{};      // optional override path for object output
    std::string emit_asm_path{};
    std::string emit_ir_path{};
    std::string polyld_path{"polyld"};
    int opt_level{0};  // 0-3
    int jobs{1};  // parallelism hint
    RegAllocChoice regalloc{RegAllocChoice::kLinearScan};
};

// Detect language from file extension
std::string DetectLanguage(const std::string &path) {
    auto ext = fs::path(path).extension().string();
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".ploy" || ext == ".poly") return "ploy";
    if (ext == ".py") return "python";
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" || ext == ".hpp" || ext == ".h") return "cpp";
    if (ext == ".rs") return "rust";
    if (ext == ".java") return "java";
    if (ext == ".cs" || ext == ".vb") return "dotnet";
    return "";
}

// Read entire file content into string
std::string ReadFileContent(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return {};
    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::string content(static_cast<size_t>(size), '\0');
    ifs.read(content.data(), size);
    return content;
}

// Timer helper for progress output
struct StageTimer {
    std::string name;
    std::chrono::high_resolution_clock::time_point start;
    bool verbose;

    StageTimer(const std::string &stage_name, bool verbose_flag)
        : name(stage_name), verbose(verbose_flag) {
        start = std::chrono::high_resolution_clock::now();
        if (verbose) {
            std::cerr << "[polyc] " << name << "... " << std::flush;
        }
    }

    double Stop() {
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        if (verbose) {
            std::cerr << "done (" << std::fixed << std::setprecision(1) << ms << "ms)\n";
        }
        return ms;
    }
};

Settings ParseArgs(int argc, char **argv) {
    Settings s;
    bool source_set = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: polyc [options] <source-file-or-code>\n"
                      << "\n"
                      << "Options:\n"
                      << "  --lang=<lang>       Language: ploy|python|cpp|rust|java|dotnet (auto-detected from extension)\n"
                      << "  -O<0-3>             Optimisation level\n"
                      << "  -o <output>         Output file name\n"
                      << "  --mode=<mode>       compile|assemble|link\n"
                      << "  --arch=<arch>       x86_64|arm64|wasm\n"
                      << "  --emit-ir=<path>    Write IR to file\n"
                      << "  --emit-asm=<path>   Write assembly to file\n"
                      << "  --emit-obj=<path>   Write object to file\n"
                      << "  --obj-format=<fmt>  pobj|coff|elf|macho (auto-detected per OS)\n"
                      << "  --quiet             Suppress progress output\n"
                      << "  --no-aux            Do not emit auxiliary files\n"
                      << "  --force             Continue despite errors\n"
                      << "  -j<N>               Parallelism hint\n"
                      << "  --regalloc=<mode>   linear-scan|graph-coloring\n"
                      << "\n"
                      << "The source argument can be a file path (.ploy, .py, .cpp, .rs, .java, .cs) or inline code.\n"
                      << "When a file is given, auxiliary files are written to <dir>/aux/.\n";
            std::exit(0);
        }
        if (arg.rfind("--lang=", 0) == 0) {
            s.language = arg.substr(7);
            s.language_explicit = true;
            continue;
        }
        if (arg == "--quiet" || arg == "-q") { s.verbose = false; continue; }
        if (arg == "--no-aux") { s.emit_aux = false; continue; }
        if (arg.rfind("--mode=", 0) == 0) {
            s.mode = arg.substr(7);
            continue;
        }
        if (arg.rfind("--opt=", 0) == 0) {
            auto lvl = arg.substr(6);
            if (lvl == "O0") s.opt_level = 0;
            else if (lvl == "O1") s.opt_level = 1;
            else if (lvl == "O2") s.opt_level = 2;
            else if (lvl == "O3") s.opt_level = 3;
            continue;
        }
        if (arg.rfind("-O", 0) == 0 && arg.size() == 3) {
            char lvl = arg[2];
            if (lvl >= '0' && lvl <= '3') s.opt_level = lvl - '0';
            continue;
        }
        if (arg.rfind("-o", 0) == 0) {
            if (arg == "-o" && i + 1 < argc) {
                s.output = argv[++i];
            } else if (arg.size() > 2) {
                s.output = arg.substr(2);
            }
            continue;
        }
        if (arg.rfind("--emit-asm=", 0) == 0) {
            s.emit_asm_path = arg.substr(11);
            continue;
        }
        if (arg.rfind("--emit-obj=", 0) == 0) {
            s.emit_obj_path = arg.substr(11);
            continue;
        }
        if (arg.rfind("--obj-format=", 0) == 0) {
            s.obj_format = arg.substr(13);
            continue;
        }
        if (arg.rfind("--polyld=", 0) == 0) {
            s.polyld_path = arg.substr(9);
            continue;
        }
        if (arg.rfind("--emit-ir=", 0) == 0) {
            s.emit_ir_path = arg.substr(10);
            continue;
        }
        if (arg.rfind("--arch=", 0) == 0) {
            s.arch = arg.substr(7);
            continue;
        }
        if (arg == "--force" || arg == "--ignore-diagnostics") { s.force = true; continue; }
        if (arg.rfind("-j", 0) == 0) {
            if (arg == "-j" && i + 1 < argc) {
                s.jobs = std::atoi(argv[++i]);
            } else if (arg.size() > 2) {
                s.jobs = std::atoi(arg.substr(2).c_str());
            }
            if (s.jobs < 1) s.jobs = 1;
            continue;
        }
        if (arg.rfind("--regalloc=", 0) == 0) {
            auto mode = arg.substr(11);
            if (mode == "graph" || mode == "graph-coloring" || mode == "coloring") {
                s.regalloc = RegAllocChoice::kGraphColoring;
            } else {
                s.regalloc = RegAllocChoice::kLinearScan;
            }
            continue;
        }
        if (arg == "--pp-cpp") { s.pp_cpp = true; continue; }
        if (arg == "--no-pp-cpp") { s.pp_cpp = false; continue; }
        if (arg == "--pp-rust") { s.pp_rust = true; continue; }
        if (arg == "--no-pp-rust") { s.pp_rust = false; continue; }
        if (arg == "--pp-python") { s.pp_python = true; continue; }
        if (arg == "--no-pp-python") { s.pp_python = false; continue; }
        if (arg.rfind("--I=", 0) == 0) {
            s.include_paths.push_back(arg.substr(4));
            continue;
        }
        if (arg == "--I" && i + 1 < argc) {
            s.include_paths.push_back(argv[++i]);
            continue;
        }
        if (!source_set) {
            s.source = arg;
            source_set = true;
            continue;
        }
    }

    // If source looks like a file path, read its contents
    if (source_set && fs::exists(s.source)) {
        s.source_path = fs::absolute(s.source).string();
        s.source = ReadFileContent(s.source_path);
        if (s.source.empty()) {
            std::cerr << "[error] could not read file: " << s.source_path << "\n";
            std::exit(1);
        }
        // Auto-detect language from extension if not explicitly set
        if (!s.language_explicit) {
            auto detected = DetectLanguage(s.source_path);
            if (!detected.empty()) {
                s.language = detected;
            }
        }
        // Add source directory to include paths
        s.include_paths.push_back(fs::path(s.source_path).parent_path().string());
    }

    return s;
}

void ApplyIncludePaths(polyglot::frontends::Preprocessor &pp,
                       const std::vector<std::string> &paths) {
    for (const auto &p : paths) {
        pp.AddIncludePath(p);
    }
}

// Minimal POBJ container structures (kept in sync with polyld)
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
    std::uint32_t flags{0};  // bit1: bss
    std::uint64_t offset{0};
    std::uint64_t size{0};
};

struct SymbolRecord {
    std::uint32_t name_offset{0};
    std::uint32_t section_index{0xFFFFFFFF};
    std::uint64_t value{0};
    std::uint64_t size{0};
    std::uint8_t binding{0};  // 0 local, 1 global
    std::uint8_t reserved[3]{};
};

struct RelocRecord {
    std::uint32_t section_index{0};
    std::uint64_t offset{0};
    std::uint32_t type{0};  // 0 abs64, 1 rel32
    std::uint32_t symbol_index{0};
    std::int64_t addend{0};
};
#pragma pack(pop)

constexpr std::uint32_t kSectionFlagBss = 1u << 1;

struct ObjReloc {
    std::uint32_t section_index{0};
    std::uint64_t offset{0};
    std::uint32_t type{1};
    std::uint32_t symbol_index{0};
    std::int64_t addend{-4};
};

struct ObjSection {
    std::string name;
    std::vector<std::uint8_t> data;
    bool bss{false};
    std::vector<ObjReloc> relocs;
};

struct ObjSymbol {
    std::string name;
    std::uint32_t section_index{0xFFFFFFFF};
    std::uint64_t value{0};
    std::uint64_t size{0};
    bool global{true};
    bool defined{false};
};

std::string BuildPobj(const std::string &path, const std::vector<ObjSection> &obj_sections,
                      const std::vector<ObjSymbol> &obj_symbols) {
    std::vector<std::uint8_t> strtab{0};
    auto add_str = [&](const std::string &s) {
        std::uint32_t off = static_cast<std::uint32_t>(strtab.size());
        strtab.insert(strtab.end(), s.begin(), s.end());
        strtab.push_back(0);
        return off;
    };

    std::vector<SectionRecord> sections;
    sections.reserve(obj_sections.size());
    std::size_t cursor = sizeof(FileHeader);  // will add tables later

    for (const auto &sec : obj_sections) {
        SectionRecord rec{};
        rec.name_offset = add_str(sec.name);
        rec.flags = sec.bss ? kSectionFlagBss : 0;
        rec.size = static_cast<std::uint64_t>(sec.data.size());
        sections.push_back(rec);
    }

    std::vector<SymbolRecord> symbols;
    symbols.reserve(obj_symbols.size());
    for (const auto &sym : obj_symbols) {
        SymbolRecord rec{};
        rec.name_offset = add_str(sym.name);
        rec.section_index = sym.defined ? sym.section_index : 0xFFFFFFFF;
        rec.value = sym.value;
        rec.size = sym.size;
        rec.binding = sym.global ? 1 : 0;
        symbols.push_back(rec);
    }

    std::vector<RelocRecord> reloc_records;
    for (std::size_t si = 0; si < obj_sections.size(); ++si) {
        for (const auto &r : obj_sections[si].relocs) {
            RelocRecord rr{};
            rr.section_index = static_cast<std::uint32_t>(si);
            rr.offset = r.offset;
            rr.type = r.type;
            rr.symbol_index = r.symbol_index;
            rr.addend = r.addend;
            reloc_records.push_back(rr);
        }
    }

    std::size_t table_bytes = sections.size() * sizeof(SectionRecord) + symbols.size() * sizeof(SymbolRecord) + reloc_records.size() * sizeof(RelocRecord);
    cursor += table_bytes;

    // Set section offsets (after header + tables, contiguous data)
    std::size_t data_cursor = cursor;
    for (std::size_t i = 0; i < obj_sections.size(); ++i) {
        sections[i].offset = obj_sections[i].bss ? 0 : data_cursor;
        if (!obj_sections[i].bss) data_cursor += obj_sections[i].data.size();
    }

    FileHeader hdr{};
    std::memcpy(hdr.magic, "POBJ", 4);
    hdr.version = 1;
    hdr.section_count = static_cast<std::uint16_t>(sections.size());
    hdr.symbol_count = static_cast<std::uint16_t>(symbols.size());
    hdr.reloc_count = static_cast<std::uint16_t>(reloc_records.size());
    hdr.strtab_offset = data_cursor;

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};
    ofs.write(reinterpret_cast<const char *>(&hdr), static_cast<std::streamsize>(sizeof(hdr)));
    ofs.write(reinterpret_cast<const char *>(sections.data()), static_cast<std::streamsize>(sections.size() * sizeof(SectionRecord)));
    ofs.write(reinterpret_cast<const char *>(symbols.data()), static_cast<std::streamsize>(symbols.size() * sizeof(SymbolRecord)));
    ofs.write(reinterpret_cast<const char *>(reloc_records.data()), static_cast<std::streamsize>(reloc_records.size() * sizeof(RelocRecord)));
    for (const auto &sec : obj_sections) {
        if (sec.bss) continue;
        ofs.write(reinterpret_cast<const char *>(sec.data.data()), static_cast<std::streamsize>(sec.data.size()));
    }
    ofs.write(reinterpret_cast<const char *>(strtab.data()), static_cast<std::streamsize>(strtab.size()));
    if (!ofs.good()) return {};
    return path;
}

std::string BuildElf64(const std::string &path, const std::vector<ObjSection> &obj_sections,
                       const std::vector<ObjSymbol> &obj_symbols, const std::string &arch) {
    // Minimal relocatable ELF64 with .text/.data/.bss plus symtab/strtab and .rela.text
    bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
    auto align_to = [](std::size_t value, std::size_t align) {
        if (align == 0) return value;
        auto mask = align - 1;
        return (value + mask) & ~mask;
    };

    // Locate sections of interest
    auto text_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.name == ".text"; });
    auto data_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.name == ".data"; });
    auto bss_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.bss; });

    std::string shstr;
    shstr.push_back('\0');
    auto add_name = [&](const std::string &n) {
        std::uint32_t off = static_cast<std::uint32_t>(shstr.size());
        shstr.append(n);
        shstr.push_back('\0');
        return off;
    };

    std::string strtab(1, '\0');
    std::vector<std::uint16_t> section_map(obj_sections.size(), SHN_UNDEF);
    std::vector<Elf64_Sym> symtab;
    symtab.push_back(Elf64_Sym{});  // null symbol

    for (const auto &sym : obj_symbols) {
        std::uint32_t name_off = static_cast<std::uint32_t>(strtab.size());
        strtab.append(sym.name);
        strtab.push_back('\0');
        Elf64_Sym s{};
        s.st_name = name_off;
        bool defined = sym.defined && sym.section_index != 0xFFFFFFFF;
        s.st_info = ELF64_ST_INFO(sym.global ? STB_GLOBAL : STB_LOCAL, defined ? STT_FUNC : STT_NOTYPE);
        s.st_other = 0;
        s.st_shndx = SHN_UNDEF;  // filled after section_map is populated
        s.st_value = sym.value;
        s.st_size = sym.size;
        symtab.push_back(s);
    }

    std::vector<Elf64_Shdr> shdrs;
    auto add_sh = [&](const Elf64_Shdr &sh) {
        shdrs.push_back(sh);
        return static_cast<std::uint16_t>(shdrs.size() - 1);
    };

    Elf64_Shdr sh_null{};
    sh_null.sh_name = 0;
    add_sh(sh_null);

    std::size_t off = sizeof(Elf64_Ehdr);
    std::uint16_t idx_text = SHN_UNDEF;
    if (text_it != obj_sections.end()) {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".text");
        sh.sh_type = SHT_PROGBITS;
        sh.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        sh.sh_offset = off;
        sh.sh_size = text_it->data.size();
        sh.sh_addralign = 16;
        idx_text = add_sh(sh);
        section_map[static_cast<std::size_t>(text_it - obj_sections.begin())] = idx_text;
        off += text_it->data.size();
    }

    std::uint16_t idx_data = SHN_UNDEF;
    if (data_it != obj_sections.end() && !data_it->data.empty()) {
        off = align_to(off, 8);
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".data");
        sh.sh_type = SHT_PROGBITS;
        sh.sh_flags = SHF_ALLOC | SHF_WRITE;
        sh.sh_offset = off;
        sh.sh_size = data_it->data.size();
        sh.sh_addralign = 8;
        idx_data = add_sh(sh);
        section_map[static_cast<std::size_t>(data_it - obj_sections.begin())] = idx_data;
        off += data_it->data.size();
    }

    std::uint16_t idx_bss = SHN_UNDEF;
    if (bss_it != obj_sections.end()) {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".bss");
        sh.sh_type = SHT_NOBITS;
        sh.sh_flags = SHF_ALLOC | SHF_WRITE;
        sh.sh_offset = 0;
        sh.sh_size = bss_it->data.size();
        sh.sh_addralign = 8;
        idx_bss = add_sh(sh);
        section_map[static_cast<std::size_t>(bss_it - obj_sections.begin())] = idx_bss;
    }

    off = align_to(off, 8);
    std::uint16_t idx_symtab = SHN_UNDEF;
    {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".symtab");
        sh.sh_type = SHT_SYMTAB;
        sh.sh_offset = off;
        sh.sh_size = symtab.size() * sizeof(Elf64_Sym);
        sh.sh_info = 1;  // one local (null)
        sh.sh_addralign = 8;
        sh.sh_entsize = sizeof(Elf64_Sym);
        idx_symtab = add_sh(sh);
        off += sh.sh_size;
    }

    std::uint16_t idx_strtab = SHN_UNDEF;
    {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".strtab");
        sh.sh_type = SHT_STRTAB;
        sh.sh_offset = off;
        sh.sh_size = strtab.size();
        sh.sh_addralign = 1;
        idx_strtab = add_sh(sh);
        off += sh.sh_size;
    }

    bool has_relocs = (text_it != obj_sections.end() && !text_it->relocs.empty());
    std::uint16_t idx_rela = SHN_UNDEF;
    if (has_relocs) {
        off = align_to(off, 8);
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".rela.text");
        sh.sh_type = SHT_RELA;
        sh.sh_offset = off;
        sh.sh_size = text_it->relocs.size() * sizeof(Elf64_Rela);
        sh.sh_addralign = 8;
        sh.sh_entsize = sizeof(Elf64_Rela);
        idx_rela = add_sh(sh);
        off += sh.sh_size;
    }

    std::uint16_t idx_shstr = SHN_UNDEF;
    {
        Elf64_Shdr sh{};
        sh.sh_name = add_name(".shstrtab");
        sh.sh_type = SHT_STRTAB;
        sh.sh_offset = off;
        sh.sh_size = shstr.size();
        sh.sh_addralign = 1;
        idx_shstr = add_sh(sh);
        off += sh.sh_size;
    }

    // Now that section indices are known, fix up symbol section indexes and links.
    for (std::size_t i = 1; i < symtab.size(); ++i) {
        const auto &sym = obj_symbols[i - 1];
        if (sym.defined && sym.section_index != 0xFFFFFFFF && sym.section_index < section_map.size()) {
            symtab[i].st_shndx = section_map[sym.section_index];
        } else {
            symtab[i].st_shndx = SHN_UNDEF;
        }
    }

    if (idx_symtab != SHN_UNDEF && idx_strtab != SHN_UNDEF) {
        shdrs[idx_symtab].sh_link = idx_strtab;
    }
    if (idx_rela != SHN_UNDEF && idx_symtab != SHN_UNDEF && idx_text != SHN_UNDEF) {
        shdrs[idx_rela].sh_link = idx_symtab;
        shdrs[idx_rela].sh_info = idx_text;
    }

    Elf64_Ehdr ehdr{};
    std::memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS] = ELFCLASS64;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = ET_REL;
    ehdr.e_machine = is_arm64 ? EM_AARCH64 : EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = 0;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    off = align_to(off, 8);
    ehdr.e_shoff = off;
    ehdr.e_shnum = static_cast<std::uint16_t>(shdrs.size());
    ehdr.e_shstrndx = idx_shstr;

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};

    ofs.write(reinterpret_cast<const char *>(&ehdr), sizeof(ehdr));
    std::size_t cursor = sizeof(Elf64_Ehdr);
    auto pad_to = [&](std::size_t target) {
        if (target <= cursor) return;
        std::vector<char> zero(target - cursor, 0);
        ofs.write(zero.data(), static_cast<std::streamsize>(zero.size()));
        cursor = target;
    };

    if (idx_text != SHN_UNDEF && text_it != obj_sections.end()) {
        pad_to(shdrs[idx_text].sh_offset);
        ofs.write(reinterpret_cast<const char *>(text_it->data.data()), static_cast<std::streamsize>(text_it->data.size()));
        cursor += text_it->data.size();
    }

    if (idx_data != SHN_UNDEF && data_it != obj_sections.end()) {
        pad_to(shdrs[idx_data].sh_offset);
        ofs.write(reinterpret_cast<const char *>(data_it->data.data()), static_cast<std::streamsize>(data_it->data.size()));
        cursor += data_it->data.size();
    }

    if (idx_symtab != SHN_UNDEF) {
        pad_to(shdrs[idx_symtab].sh_offset);
        ofs.write(reinterpret_cast<const char *>(symtab.data()), static_cast<std::streamsize>(symtab.size() * sizeof(Elf64_Sym)));
        cursor += shdrs[idx_symtab].sh_size;
    }

    if (idx_strtab != SHN_UNDEF) {
        pad_to(shdrs[idx_strtab].sh_offset);
        ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
        cursor += shdrs[idx_strtab].sh_size;
    }

    if (idx_rela != SHN_UNDEF && text_it != obj_sections.end()) {
        pad_to(shdrs[idx_rela].sh_offset);
        for (const auto &r : text_it->relocs) {
            Elf64_Rela rela{};
            rela.r_offset = r.offset;
            std::uint32_t reloc_type = 0;
            if (is_arm64) {
                reloc_type = (r.type == 1) ? R_AARCH64_CALL26 : R_AARCH64_JUMP26;
            } else {
                reloc_type = (r.type == 1) ? R_X86_64_PC32 : R_X86_64_64;
            }
            rela.r_info = ELF64_R_INFO(static_cast<std::uint32_t>(r.symbol_index + 1), reloc_type);
            rela.r_addend = r.addend;
            ofs.write(reinterpret_cast<const char *>(&rela), sizeof(rela));
            cursor += sizeof(rela);
        }
    }

    if (idx_shstr != SHN_UNDEF) {
        pad_to(shdrs[idx_shstr].sh_offset);
        ofs.write(shstr.data(), static_cast<std::streamsize>(shstr.size()));
        cursor += shdrs[idx_shstr].sh_size;
    }

    pad_to(ehdr.e_shoff);
    ofs.write(reinterpret_cast<const char *>(shdrs.data()), static_cast<std::streamsize>(shdrs.size() * sizeof(Elf64_Shdr)));
    return ofs.good() ? path : std::string{};
}

#if defined(__APPLE__)
std::string BuildMachO64(const std::string &path, const std::vector<ObjSection> &obj_sections,
                         const std::vector<ObjSymbol> &obj_symbols, const std::string &arch) {
    // Minimal 64-bit relocatable Mach-O emitting __TEXT,__text plus relocations and symtab.
    auto text_it = std::find_if(obj_sections.begin(), obj_sections.end(), [](const ObjSection &s) { return s.name == ".text"; });
    bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
    std::size_t text_size = text_it != obj_sections.end() ? text_it->data.size() : 0;

    mach_header_64 mh{};
    mh.magic = MH_MAGIC_64;
    mh.cputype = is_arm64 ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    mh.cpusubtype = is_arm64 ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;
    mh.filetype = MH_OBJECT;
    mh.ncmds = 2;
    mh.sizeofcmds = sizeof(segment_command_64) + sizeof(section_64) + sizeof(symtab_command);
    mh.flags = MH_SUBSECTIONS_VIA_SYMBOLS;

    segment_command_64 seg{};
    seg.cmd = LC_SEGMENT_64;
    seg.cmdsize = sizeof(segment_command_64) + sizeof(section_64);
    std::memcpy(seg.segname, "__TEXT", 6);
    seg.vmsize = text_size;
    seg.filesize = text_size;
    seg.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
    seg.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    seg.nsects = 1;

    section_64 sec{};
    std::memcpy(sec.sectname, "__text", 6);
    std::memcpy(sec.segname, "__TEXT", 6);
    sec.size = text_size;
    sec.align = 4;  // 16-byte alignment expressed as 2^4
    sec.flags = S_REGULAR | S_ATTR_SOME_INSTRUCTIONS;
    sec.offset = sizeof(mach_header_64) + seg.cmdsize + sizeof(symtab_command);

    std::string strtab(1, '\0');
    std::vector<nlist_64> nlists;
    std::unordered_map<std::string, std::uint32_t> sym_index;

    auto add_sym = [&](const ObjSymbol &sym) {
        if (sym_index.count(sym.name)) return sym_index[sym.name];
        std::uint32_t name_off = static_cast<std::uint32_t>(strtab.size());
        strtab.append(sym.name);
        strtab.push_back('\0');
        nlist_64 nl{};
        nl.n_un.n_strx = name_off;
        bool defined = sym.defined && sym.section_index != 0xFFFFFFFF;
        nl.n_type = (defined ? (N_EXT | N_SECT) : (N_EXT | N_UNDF));
        nl.n_sect = defined ? 1 : 0;
        nl.n_desc = 0;
        nl.n_value = defined ? sym.value : 0;
        sym_index[sym.name] = static_cast<std::uint32_t>(nlists.size());
        nlists.push_back(nl);
        return sym_index[sym.name];
    };

    for (const auto &sym : obj_symbols) {
        add_sym(sym);
    }

    std::size_t reloc_bytes = text_it != obj_sections.end() ? text_it->relocs.size() * sizeof(relocation_info) : 0;
    sec.reloff = static_cast<std::uint32_t>(sec.offset + text_size);
    sec.nreloc = static_cast<std::uint32_t>(text_it != obj_sections.end() ? text_it->relocs.size() : 0);

    symtab_command st{};
    st.cmd = LC_SYMTAB;
    st.cmdsize = sizeof(symtab_command);
    st.symoff = static_cast<std::uint32_t>(sec.reloff + reloc_bytes);
    st.nsyms = static_cast<std::uint32_t>(nlists.size());
    st.stroff = st.symoff + static_cast<std::uint32_t>(nlists.size() * sizeof(nlist_64));
    st.strsize = static_cast<std::uint32_t>(strtab.size());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};

    ofs.write(reinterpret_cast<const char *>(&mh), sizeof(mh));
    ofs.write(reinterpret_cast<const char *>(&seg), sizeof(seg));
    ofs.write(reinterpret_cast<const char *>(&sec), sizeof(sec));
    ofs.write(reinterpret_cast<const char *>(&st), sizeof(st));
    if (text_it != obj_sections.end()) {
        ofs.write(reinterpret_cast<const char *>(text_it->data.data()), static_cast<std::streamsize>(text_it->data.size()));
        for (const auto &r : text_it->relocs) {
            relocation_info ri{};
            ri.r_address = static_cast<std::int32_t>(r.offset);
            ri.r_symbolnum = static_cast<std::int32_t>(r.symbol_index);
            ri.r_pcrel = (r.type == 1);
            ri.r_length = 2;  // 2 => 4 bytes
            ri.r_extern = 1;
            ri.r_type = is_arm64 ? 2 : 0;  // ARM64_RELOC_BRANCH26 or X86_64_RELOC_BRANCH
            ofs.write(reinterpret_cast<const char *>(&ri), sizeof(ri));
        }
    }

    for (const auto &n : nlists) {
        ofs.write(reinterpret_cast<const char *>(&n), sizeof(n));
    }
    ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));
    return ofs.good() ? path : std::string{};
}
#else
// Non-Apple platforms: stub out Mach-O emission.
std::string BuildMachO64(const std::string &path, const std::vector<ObjSection> &, const std::vector<ObjSymbol> &, const std::string &) {
    (void)path;
    return {};
}
#endif

// ---- PE/COFF Object File Emitter (for Windows) ----
// Minimal COFF relocatable object so Windows toolchain (MSVC link.exe) can consume it.
#pragma pack(push, 1)
struct CoffFileHeader {
    std::uint16_t Machine;
    std::uint16_t NumberOfSections;
    std::uint32_t TimeDateStamp;
    std::uint32_t PointerToSymbolTable;
    std::uint32_t NumberOfSymbols;
    std::uint16_t SizeOfOptionalHeader;
    std::uint16_t Characteristics;
};

struct CoffSectionHeader {
    char Name[8];
    std::uint32_t VirtualSize;
    std::uint32_t VirtualAddress;
    std::uint32_t SizeOfRawData;
    std::uint32_t PointerToRawData;
    std::uint32_t PointerToRelocations;
    std::uint32_t PointerToLinenumbers;
    std::uint16_t NumberOfRelocations;
    std::uint16_t NumberOfLinenumbers;
    std::uint32_t Characteristics;
};

struct CoffSymbol {
    union {
        char ShortName[8];
        struct {
            std::uint32_t Zeroes;
            std::uint32_t Offset;
        } LongName;
    } Name;
    std::uint32_t Value;
    std::int16_t  SectionNumber;
    std::uint16_t Type;
    std::uint8_t  StorageClass;
    std::uint8_t  NumberOfAuxSymbols;
};

struct CoffRelocation {
    std::uint32_t VirtualAddress;
    std::uint32_t SymbolTableIndex;
    std::uint16_t Type;
};
#pragma pack(pop)

// COFF machine types
constexpr std::uint16_t kCoffMachineAMD64  = 0x8664;
constexpr std::uint16_t kCoffMachineARM64  = 0xAA64;
// COFF section characteristics
constexpr std::uint32_t kCoffSecText  = 0x60000020;  // CNT_CODE | MEM_EXECUTE | MEM_READ
constexpr std::uint32_t kCoffSecData  = 0xC0000040;  // CNT_INITIALIZED_DATA | MEM_READ | MEM_WRITE
constexpr std::uint32_t kCoffSecBss   = 0xC0000080;  // CNT_UNINITIALIZED_DATA | MEM_READ | MEM_WRITE
// COFF symbol storage classes
constexpr std::uint8_t kCoffSymExternal  = 2;
constexpr std::uint8_t kCoffSymStatic    = 3;
// COFF relocation types (AMD64)
constexpr std::uint16_t kCoffRelocAMD64Addr64 = 0x0001;
constexpr std::uint16_t kCoffRelocAMD64Rel32  = 0x0004;
// COFF relocation types (ARM64)
constexpr std::uint16_t kCoffRelocARM64Branch26 = 0x0003;

std::string BuildCoff(const std::string &path, const std::vector<ObjSection> &obj_sections,
                      const std::vector<ObjSymbol> &obj_symbols, const std::string &arch) {
    bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");

    // Build COFF section headers
    std::vector<CoffSectionHeader> sec_headers;
    std::vector<const ObjSection *> sec_ptrs;
    std::unordered_map<std::string, std::uint16_t> sec_name_to_idx;

    for (const auto &s : obj_sections) {
        CoffSectionHeader sh{};
        std::memset(&sh, 0, sizeof(sh));
        std::string name = s.name;
        if (name.size() <= 8) {
            std::memcpy(sh.Name, name.data(), name.size());
        } else {
            std::memcpy(sh.Name, name.data(), 8);
        }
        sh.SizeOfRawData = static_cast<std::uint32_t>(s.data.size());
        if (name == ".text" || name == ".code") {
            sh.Characteristics = kCoffSecText;
        } else if (s.bss) {
            sh.Characteristics = kCoffSecBss;
            sh.SizeOfRawData = 0;
            sh.VirtualSize = static_cast<std::uint32_t>(s.data.size());
        } else {
            sh.Characteristics = kCoffSecData;
        }
        sec_name_to_idx[name] = static_cast<std::uint16_t>(sec_headers.size());
        sec_headers.push_back(sh);
        sec_ptrs.push_back(&s);
    }

    // Build COFF symbol table
    std::string strtab;
    strtab.resize(4, '\0');  // 4-byte size placeholder

    std::vector<CoffSymbol> coff_syms;
    std::unordered_map<std::string, std::uint32_t> sym_name_to_idx;

    for (const auto &sym : obj_symbols) {
        CoffSymbol cs{};
        std::memset(&cs, 0, sizeof(cs));
        if (sym.name.size() <= 8) {
            std::memcpy(cs.Name.ShortName, sym.name.data(), sym.name.size());
        } else {
            cs.Name.LongName.Zeroes = 0;
            cs.Name.LongName.Offset = static_cast<std::uint32_t>(strtab.size());
            strtab.append(sym.name);
            strtab.push_back('\0');
        }
        cs.Value = static_cast<std::uint32_t>(sym.value);
        if (sym.defined && sym.section_index != 0xFFFFFFFF) {
            cs.SectionNumber = static_cast<std::int16_t>(sym.section_index + 1);
        } else {
            cs.SectionNumber = 0;
        }
        cs.Type = 0x20;
        cs.StorageClass = sym.global ? kCoffSymExternal : kCoffSymStatic;
        cs.NumberOfAuxSymbols = 0;
        sym_name_to_idx[sym.name] = static_cast<std::uint32_t>(coff_syms.size());
        coff_syms.push_back(cs);
    }

    std::uint32_t strtab_size = static_cast<std::uint32_t>(strtab.size());
    std::memcpy(strtab.data(), &strtab_size, 4);

    // Build relocations per section
    std::vector<std::vector<CoffRelocation>> sec_relocs(sec_headers.size());
    for (std::size_t si = 0; si < obj_sections.size(); ++si) {
        for (const auto &r : obj_sections[si].relocs) {
            CoffRelocation cr{};
            cr.VirtualAddress = static_cast<std::uint32_t>(r.offset);
            if (r.symbol_index < obj_symbols.size()) {
                auto it = sym_name_to_idx.find(obj_symbols[r.symbol_index].name);
                cr.SymbolTableIndex = it != sym_name_to_idx.end() ? it->second : r.symbol_index;
            } else {
                cr.SymbolTableIndex = r.symbol_index;
            }
            if (is_arm64) {
                cr.Type = kCoffRelocARM64Branch26;
            } else {
                cr.Type = (r.type == 0) ? kCoffRelocAMD64Addr64 : kCoffRelocAMD64Rel32;
            }
            sec_relocs[si].push_back(cr);
        }
    }

    // Calculate file layout offsets
    std::size_t offset = sizeof(CoffFileHeader) + sec_headers.size() * sizeof(CoffSectionHeader);
    for (std::size_t i = 0; i < sec_headers.size(); ++i) {
        if (sec_ptrs[i]->bss) {
            sec_headers[i].PointerToRawData = 0;
        } else {
            sec_headers[i].PointerToRawData = static_cast<std::uint32_t>(offset);
            offset += sec_ptrs[i]->data.size();
        }
    }
    for (std::size_t i = 0; i < sec_headers.size(); ++i) {
        if (!sec_relocs[i].empty()) {
            sec_headers[i].PointerToRelocations = static_cast<std::uint32_t>(offset);
            sec_headers[i].NumberOfRelocations = static_cast<std::uint16_t>(sec_relocs[i].size());
            offset += sec_relocs[i].size() * sizeof(CoffRelocation);
        }
    }
    std::uint32_t sym_table_offset = static_cast<std::uint32_t>(offset);

    // Write COFF file
    CoffFileHeader fh{};
    fh.Machine = is_arm64 ? kCoffMachineARM64 : kCoffMachineAMD64;
    fh.NumberOfSections = static_cast<std::uint16_t>(sec_headers.size());
    fh.TimeDateStamp = 0;
    fh.PointerToSymbolTable = sym_table_offset;
    fh.NumberOfSymbols = static_cast<std::uint32_t>(coff_syms.size());
    fh.SizeOfOptionalHeader = 0;
    fh.Characteristics = 0;

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return {};

    ofs.write(reinterpret_cast<const char *>(&fh), sizeof(fh));
    ofs.write(reinterpret_cast<const char *>(sec_headers.data()),
              static_cast<std::streamsize>(sec_headers.size() * sizeof(CoffSectionHeader)));
    for (std::size_t i = 0; i < sec_ptrs.size(); ++i) {
        if (!sec_ptrs[i]->bss && !sec_ptrs[i]->data.empty()) {
            ofs.write(reinterpret_cast<const char *>(sec_ptrs[i]->data.data()),
                      static_cast<std::streamsize>(sec_ptrs[i]->data.size()));
        }
    }
    for (std::size_t i = 0; i < sec_relocs.size(); ++i) {
        if (!sec_relocs[i].empty()) {
            ofs.write(reinterpret_cast<const char *>(sec_relocs[i].data()),
                      static_cast<std::streamsize>(sec_relocs[i].size() * sizeof(CoffRelocation)));
        }
    }
    ofs.write(reinterpret_cast<const char *>(coff_syms.data()),
              static_cast<std::streamsize>(coff_syms.size() * sizeof(CoffSymbol)));
    ofs.write(strtab.data(), static_cast<std::streamsize>(strtab.size()));

    return ofs.good() ? path : std::string{};
}

std::string EmitObject(const Settings &settings, const std::vector<ObjSection> &sections,
                      const std::vector<ObjSymbol> &symbols) {
    // Determine output extension based on format
    std::string ext;
    std::string fmt = settings.obj_format;
    if (fmt == "pobj") ext = ".pobj";
    else if (fmt == "coff") ext = ".obj";
    else ext = ".o";

    std::string out = settings.emit_obj_path.empty() ? settings.output + ext
                                                     : settings.emit_obj_path;

    if (fmt == "pobj") {
        auto written = BuildPobj(out, sections, symbols);
        if (written.empty()) return {};
        return written;
    }

    if (fmt == "coff") {
        auto written = BuildCoff(out, sections, symbols, settings.arch);
        if (written.empty()) std::cerr << "[error] COFF emit failed\n";
        return written;
    }

    if (fmt == "elf") {
        auto written = BuildElf64(out, sections, symbols, settings.arch);
        if (written.empty()) std::cerr << "[error] ELF emit failed\n";
        return written;
    }

    if (fmt == "macho") {
#if defined(__APPLE__)
        auto written = BuildMachO64(out, sections, symbols, settings.arch);
        if (written.empty()) std::cerr << "[error] Mach-O emit failed\n";
        return written;
#else
        std::cerr << "[error] Mach-O emit requested on non-Apple platform; skipping.\n";
        return {};
#endif
    }

    std::cerr << "[warn] unknown obj format: " << fmt << ", falling back to POBJ\n";
    return BuildPobj(out, sections, symbols);
}

}  // namespace (anonymous)

// Helper: create aux directory and return its path
static std::string SetupAuxDir(const Settings &settings) {
    if (!settings.emit_aux || settings.source_path.empty()) return {};
    fs::path source_dir = fs::path(settings.source_path).parent_path();
    fs::path aux_dir = source_dir / "aux";
    std::error_code ec;
    fs::create_directories(aux_dir, ec);
    if (ec) {
        std::cerr << "[warn] could not create aux/ directory: " << ec.message() << "\n";
        return {};
    }
    return aux_dir.string();
}

// Helper: get stem name from source path for naming aux files
static std::string SourceStem(const Settings &settings) {
    if (settings.source_path.empty()) return "output";
    return fs::path(settings.source_path).stem().string();
}

// Helper: write string content to an aux file (plaintext or binary)
static void WriteAuxFile(const std::string &aux_dir, const std::string &filename,
                         const std::string &content, bool verbose) {
    if (aux_dir.empty()) return;
    fs::path path = fs::path(aux_dir) / filename;
    std::ofstream ofs(path);
    if (ofs.is_open()) {
        ofs << content;
        if (verbose) {
            std::cerr << "[polyc]   -> " << path.string() << " ("
                      << content.size() << " bytes)\n";
        }
    }
}

// ---- Binary Auxiliary File Format (PAUX) ----
// Header: magic(4) "PAUX" + version(2) + section_count(2) + reserved(8) = 16 bytes
// Each section descriptor: name_len(2) + name(N) + data_len(4) + data(M)
// All multi-byte integers are little-endian.

#pragma pack(push, 1)
struct PAuxHeader {
    char magic[4];          // "PAUX"
    std::uint16_t version;  // 1
    std::uint16_t section_count;
    std::uint8_t  reserved[8];
};
#pragma pack(pop)

// Write a binary auxiliary file containing multiple named data sections.
static void WriteAuxBinary(const std::string &aux_dir, const std::string &filename,
                           const std::vector<std::pair<std::string, std::string>> &sections,
                           bool verbose) {
    if (aux_dir.empty()) return;
    fs::path path = fs::path(aux_dir) / filename;
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return;

    PAuxHeader hdr{};
    std::memcpy(hdr.magic, "PAUX", 4);
    hdr.version = 1;
    hdr.section_count = static_cast<std::uint16_t>(sections.size());
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));
    ofs.write(reinterpret_cast<const char *>(&hdr), sizeof(hdr));

    for (const auto &[name, data] : sections) {
        // Section name: uint16 length + name bytes (no null terminator)
        std::uint16_t name_len = static_cast<std::uint16_t>(name.size());
        ofs.write(reinterpret_cast<const char *>(&name_len), 2);
        ofs.write(name.data(), name_len);
        // Section data: uint32 length + data bytes
        std::uint32_t data_len = static_cast<std::uint32_t>(data.size());
        ofs.write(reinterpret_cast<const char *>(&data_len), 4);
        ofs.write(data.data(), data_len);
    }

    if (verbose) {
        std::size_t total = sizeof(PAuxHeader);
        for (const auto &[n, d] : sections) total += 2 + n.size() + 4 + d.size();
        std::cerr << "[polyc]   -> " << path.string() << " ("
                  << total << " bytes, binary)\n";
    }
}

// Write a single-section binary auxiliary file.
static void WriteAuxBinarySingle(const std::string &aux_dir, const std::string &filename,
                                 const std::string &section_name, const std::string &content,
                                 bool verbose) {
    WriteAuxBinary(aux_dir, filename, {{section_name, content}}, verbose);
}

}  // namespace polyglot::tools

int main(int argc, char **argv) {
    using namespace polyglot::tools;

    auto total_start = std::chrono::high_resolution_clock::now();

    Settings settings = ParseArgs(argc, argv);
    bool V = settings.verbose;

    // Print header
    if (V) {
        std::cerr << "========================================\n";
        std::cerr << " PolyglotCompiler v4.3  (polyc)\n";
        std::cerr << "========================================\n";
        if (!settings.source_path.empty()) {
            std::cerr << "[polyc] Source: " << settings.source_path << "\n";
        }
        std::cerr << "[polyc] Language: " << settings.language
                  << (settings.language_explicit ? " (explicit)" : " (auto-detected)") << "\n";
        std::cerr << "[polyc] Arch: " << settings.arch << "\n";
        std::cerr << "[polyc] Opt level: O" << settings.opt_level << "\n";
        std::cerr << "[polyc] Output: " << settings.output << "\n";
        std::cerr << "----------------------------------------\n";
    }

    if (settings.jobs > 1 && V) {
        std::cerr << "[polyc] parallel build hint -j" << settings.jobs
                  << " noted (single-threaded for now)\n";
    }

    if (settings.mode != "compile" && settings.mode != "assemble" && settings.mode != "link") {
        std::cerr << "[error] Unknown mode: " << settings.mode << " (use compile|assemble|link)\n";
        return 1;
    }

    // Set up aux output directory
    std::string aux_dir = SetupAuxDir(settings);
    std::string stem = SourceStem(settings);
    if (!aux_dir.empty() && V) {
        std::cerr << "[polyc] Aux dir: " << aux_dir << "\n";
    }

    polyglot::frontends::Diagnostics diagnostics;
    std::string processed = settings.source;

    // ---- Phase 0: Preprocessing ----
    auto run_pp = [&](bool enabled) -> std::string {
        if (!enabled) return settings.source;
        StageTimer t("Preprocessing", V);
        polyglot::frontends::Preprocessor preprocessor(diagnostics);
        ApplyIncludePaths(preprocessor, settings.include_paths);
        auto result = preprocessor.Process(settings.source,
            settings.source_path.empty() ? "<cli>" : settings.source_path);
        t.Stop();
        return result;
    };

    if (settings.language == "cpp") {
        processed = run_pp(settings.pp_cpp);
    } else if (settings.language == "rust") {
        processed = run_pp(settings.pp_rust || std::getenv("POLYC_PREPROCESS_RUST"));
    } else if (settings.language == "python") {
        processed = run_pp(settings.pp_python);
    }
    // .ploy does not use preprocessing

    polyglot::ir::IRContext ir_module;
    bool lowered = false;
    std::string source_label = settings.source_path.empty() ? "<cli>" : settings.source_path;

    // ---- Phase 1-3: Frontend (Lex + Parse + Sema + Lower) ----
    if (settings.language == "ploy") {
        // ---- .ploy frontend: full pipeline ----

        // Phase 1: Lexing
        std::string token_dump;
        {
            StageTimer t("Lexing (.ploy)", V);
            // Run lexer to dump tokens for aux output
            if (!aux_dir.empty()) {
                polyglot::ploy::PloyLexer aux_lexer(processed, source_label);
                std::ostringstream toss;
                polyglot::frontends::Token tok;
                while ((tok = aux_lexer.NextToken()).kind != polyglot::frontends::TokenKind::kEndOfFile) {
                    toss << "L" << tok.loc.line << ":" << tok.loc.column
                         << "  [" << static_cast<int>(tok.kind) << "] "
                         << tok.lexeme << "\n";
                }
                token_dump = toss.str();
            }
            t.Stop();
        }
        WriteAuxBinarySingle(aux_dir, stem + ".tokens.paux", "tokens", token_dump, V);

        // Phase 2: Parsing
        polyglot::ploy::PloyLexer lexer(processed, source_label);
        polyglot::ploy::PloyParser parser(lexer, diagnostics);
        std::shared_ptr<polyglot::ploy::Module> ploy_module;
        {
            StageTimer t("Parsing (.ploy)", V);
            parser.ParseModule();
            ploy_module = parser.TakeModule();
            t.Stop();
        }

        if (!ploy_module || diagnostics.HasErrors()) {
            std::cerr << "[error] Parse failed.\n";
            for (const auto &d : diagnostics.All()) {
                std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
            }
            return 1;
        }

        // Write AST summary to aux
        if (!aux_dir.empty()) {
            std::ostringstream ast_oss;
            ast_oss << "Module: " << ploy_module->declarations.size() << " declarations\n";
            for (size_t i = 0; i < ploy_module->declarations.size(); ++i) {
                auto &decl = ploy_module->declarations[i];
                ast_oss << "  [" << i << "] loc=" << decl->loc.line
                        << ":" << decl->loc.column << "\n";
            }
            WriteAuxBinarySingle(aux_dir, stem + ".ast.paux", "ast", ast_oss.str(), V);
        }

        // Phase 3: Semantic analysis
        polyglot::ploy::PloySema sema(diagnostics);
        bool sema_ok = false;
        {
            StageTimer t("Semantic analysis (.ploy)", V);
            sema_ok = sema.Analyze(ploy_module);
            t.Stop();
        }

        if (!sema_ok && !settings.force) {
            std::cerr << "[error] Semantic analysis failed.\n";
            for (const auto &d : diagnostics.All()) {
                std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
            }
            return 1;
        }

        // Write symbol table to aux
        if (!aux_dir.empty()) {
            std::ostringstream sym_oss;
            sym_oss << "Symbol Table (" << sema.Symbols().size() << " entries)\n";
            for (const auto &[name, sym] : sema.Symbols()) {
                sym_oss << "  " << name << " : kind="
                        << static_cast<int>(sym.kind) << "\n";
            }
            WriteAuxBinarySingle(aux_dir, stem + ".symbols.paux", "symbols", sym_oss.str(), V);
        }

        // Phase 4: IR Lowering
        polyglot::ploy::PloyLowering lowering_engine(ir_module, diagnostics, sema);
        {
            StageTimer t("IR lowering (.ploy)", V);
            lowered = lowering_engine.Lower(ploy_module);
            t.Stop();
        }

        if (!lowered && !settings.force) {
            std::cerr << "[error] IR lowering failed.\n";
            for (const auto &d : diagnostics.All()) {
                std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
            }
            return 1;
        }

        // Write IR to aux
        if (!aux_dir.empty()) {
            std::ostringstream ir_oss;
            for (const auto &fn : ir_module.Functions()) {
                polyglot::ir::PrintFunction(*fn, ir_oss);
            }
            WriteAuxBinarySingle(aux_dir, stem + ".ir.paux", "ir", ir_oss.str(), V);
        }

        // Write cross-language call descriptors to aux
        if (!aux_dir.empty()) {
            std::ostringstream desc_oss;
            const auto &descs = lowering_engine.CallDescriptors();
            desc_oss << "Cross-Language Call Descriptors (" << descs.size() << ")\n";
            for (size_t i = 0; i < descs.size(); ++i) {
                desc_oss << "  [" << i << "] "
                         << descs[i].source_language << "::" << descs[i].source_function
                         << " -> "
                         << descs[i].target_language << "::" << descs[i].target_function
                         << " (params: " << descs[i].param_marshal.size() << ")\n";
            }
            WriteAuxBinarySingle(aux_dir, stem + ".descriptors.paux", "descriptors", desc_oss.str(), V);
        }

        // ---- Phase 4b: Cross-language link resolution ----
        // Feed the descriptors and link entries to the PolyglotLinker so that
        // glue stubs are resolved before we proceed to code generation.
        {
            StageTimer t("Cross-language link resolution", V);
            polyglot::linker::LinkerConfig lnk_cfg;
            lnk_cfg.verbose = V;
            polyglot::linker::PolyglotLinker poly_linker(lnk_cfg);

            for (const auto &desc : lowering_engine.CallDescriptors()) {
                poly_linker.AddCallDescriptor(desc);
            }
            for (const auto &entry : sema.Links()) {
                poly_linker.AddLinkEntry(entry);
            }

            bool link_ok = poly_linker.ResolveLinks();
            if (!link_ok && V) {
                std::cerr << "[polyc] Cross-language link warnings:\n";
                for (const auto &e : poly_linker.GetErrors()) {
                    std::cerr << "  " << e << "\n";
                }
            }
            // Glue stubs are available via poly_linker.GetStubs() for
            // downstream object emission.
            t.Stop();
        }

        lowered = true;

    } else if (settings.language == "python") {
        StageTimer t("Frontend (python)", V);
        polyglot::python::PythonLexer lexer(processed, source_label, &diagnostics);
        polyglot::python::PythonParser parser(lexer, diagnostics);
        parser.ParseModule();
        auto python_mod = parser.TakeModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::python::AnalyzeModule(*python_mod, sema);
        // Lower Python AST to IR
        polyglot::python::LowerToIR(*python_mod, ir_module, diagnostics);
        lowered = true;
        t.Stop();
    } else if (settings.language == "cpp") {
        StageTimer t("Frontend (cpp)", V);
        polyglot::cpp::CppLexer lexer(processed, source_label);
        polyglot::cpp::CppParser parser(lexer, diagnostics);
        parser.ParseModule();
        auto cpp_mod = parser.TakeModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::cpp::AnalyzeModule(*cpp_mod, sema);
        polyglot::cpp::LowerToIR(*cpp_mod, ir_module, diagnostics);
        lowered = true;
        t.Stop();
    } else if (settings.language == "rust") {
        StageTimer t("Frontend (rust)", V);
        polyglot::rust::RustLexer lexer(processed, source_label);
        polyglot::rust::RustParser parser(lexer, diagnostics);
        parser.ParseModule();
        auto rust_mod = parser.TakeModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::rust::AnalyzeModule(*rust_mod, sema);
        // Lower Rust AST to IR
        polyglot::rust::LowerToIR(*rust_mod, ir_module, diagnostics);
        lowered = true;
        t.Stop();
    } else if (settings.language == "java") {
        StageTimer t("Frontend (java)", V);
        polyglot::java::JavaLexer lexer(processed, source_label);
        polyglot::java::JavaParser parser(lexer, diagnostics);
        parser.ParseModule();
        auto java_mod = parser.TakeModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::java::AnalyzeModule(*java_mod, sema);
        polyglot::java::LowerToIR(*java_mod, ir_module, diagnostics);
        lowered = true;
        t.Stop();
    } else if (settings.language == "dotnet" || settings.language == "csharp") {
        StageTimer t("Frontend (dotnet)", V);
        polyglot::dotnet::DotnetLexer lexer(processed, source_label);
        polyglot::dotnet::DotnetParser parser(lexer, diagnostics);
        parser.ParseModule();
        auto dotnet_mod = parser.TakeModule();
        polyglot::frontends::SemaContext sema(diagnostics);
        polyglot::dotnet::AnalyzeModule(*dotnet_mod, sema);
        polyglot::dotnet::LowerToIR(*dotnet_mod, ir_module, diagnostics);
        lowered = true;
        t.Stop();
    } else {
        diagnostics.Report(polyglot::core::SourceLoc{source_label, 1, 1},
                           "Unknown language: " + settings.language);
    }

    if (!settings.force && diagnostics.HasErrors()) {
        std::cerr << "[error] Compilation failed with " << diagnostics.ErrorCount() << " error(s).\n";
        for (const auto &d : diagnostics.All()) {
            std::cerr << polyglot::frontends::Diagnostics::Format(d) << "\n";
        }
        return 1;
    }

    if (diagnostics.HasWarnings() && V) {
        std::cerr << "[polyc] " << diagnostics.WarningCount() << " warning(s)\n";
    }

    if (!lowered) {
        // No frontend produced IR — this is an error, not a recoverable state.
        std::cerr << "[error] IR lowering not available for language '"
                  << settings.language << "'\n";
        if (!settings.force) return 1;
        // In force mode, synthesize a minimal main so the pipeline can continue.
        auto fn = ir_module.CreateFunction("main");
        auto *entry = fn->CreateBlock("entry");
        auto ret = std::make_shared<polyglot::ir::ReturnStatement>();
        ret->operands = {"0"};
        ret->type = polyglot::ir::IRType::Void();
        entry->SetTerminator(ret);
    }

    // ---- Phase 5: SSA + Verify ----
    {
        StageTimer t("SSA conversion + verification", V);
        for (auto &fn : ir_module.Functions()) {
            polyglot::ir::ConvertToSSA(*fn);
        }
        std::string verify_msg;
        if (!polyglot::ir::Verify(ir_module, &verify_msg)) {
            std::cerr << "[error] IR verification failed: " << verify_msg << "\n";
            if (!settings.force) return 1;
        }
        t.Stop();
    }

    // ---- Phase 5b: Middle-end optimization pipeline ----
    if (settings.opt_level > 0) {
        StageTimer t("Middle-end optimizations (O" + std::to_string(settings.opt_level) + ")", V);
        for (auto &fn : ir_module.Functions()) {
            // -O1: basic scalar optimizations
            polyglot::ir::passes::ConstantFold(*fn);
            polyglot::ir::passes::CopyProp(*fn);
            polyglot::ir::passes::DeadCodeEliminate(*fn);
            polyglot::ir::passes::CanonicalizeCFG(*fn);
            polyglot::ir::passes::EliminateRedundantPhis(*fn);
            polyglot::ir::passes::CSE(*fn);

            if (settings.opt_level >= 2) {
                // -O2: loop & advanced optimizations
                polyglot::passes::transform::StrengthReduction(*fn);
                polyglot::passes::transform::LoopInvariantCodeMotion(*fn);
                polyglot::passes::transform::LoopUnrolling(*fn, 4);
                polyglot::passes::transform::DeadStoreElimination(*fn);
                polyglot::passes::transform::InductionVariableElimination(*fn);
                polyglot::passes::transform::SCCP(*fn);
                polyglot::passes::transform::GVN(*fn);
                polyglot::passes::transform::JumpThreading(*fn);

                // Second round of cleanup after loop opts
                polyglot::ir::passes::DeadCodeEliminate(*fn);
                polyglot::ir::passes::CanonicalizeCFG(*fn);
            }

            if (settings.opt_level >= 3) {
                // -O3: aggressive optimizations
                polyglot::passes::transform::TailCallOptimization(*fn);
                polyglot::passes::transform::EscapeAnalysis(*fn);
                polyglot::passes::transform::ScalarReplacement(*fn);
                polyglot::passes::transform::AutoVectorization(*fn);
                polyglot::passes::transform::LoopFusion(*fn);
                polyglot::passes::transform::CodeSinking(*fn);
                polyglot::passes::transform::CodeHoisting(*fn);
                polyglot::passes::transform::LoopTiling(*fn, 64);

                // Final cleanup
                polyglot::ir::passes::DeadCodeEliminate(*fn);
            }
        }
        t.Stop();

        // Re-verify after optimization
        {
            std::string verify_msg;
            if (!polyglot::ir::Verify(ir_module, &verify_msg)) {
                if (V) {
                    std::cerr << "[warn] IR verification failed after optimization: "
                              << verify_msg << "\n";
                }
            }
        }
    }

    // Allocate a GC-managed scratch buffer and keep it rooted while emitting.
    void *scratch = polyglot_alloc(32);
    polyglot_gc_register_root(&scratch);

    std::string target_triple;
    std::string asm_text;
    bool backend_failed = false;

    // Choose backend based on arch
    bool use_arm64 = (settings.arch == "arm64" || settings.arch == "aarch64" || settings.arch == "armv8");
    bool use_wasm  = (settings.arch == "wasm" || settings.arch == "wasm32" || settings.arch == "wasm64");

    // ---- Phase 6: Backend code generation ----
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol> symbols;

    auto apply_regalloc = [&](auto &backend) {
        using BackendT = std::decay_t<decltype(backend)>;
        if constexpr (std::is_same_v<BackendT, polyglot::backends::arm64::Arm64Target>) {
            backend.SetRegAllocStrategy(settings.regalloc == RegAllocChoice::kGraphColoring
                                            ? polyglot::backends::arm64::RegAllocStrategy::kGraphColoring
                                            : polyglot::backends::arm64::RegAllocStrategy::kLinearScan);
        } else {
            backend.SetRegAllocStrategy(settings.regalloc == RegAllocChoice::kGraphColoring
                                            ? polyglot::backends::x86_64::RegAllocStrategy::kGraphColoring
                                            : polyglot::backends::x86_64::RegAllocStrategy::kLinearScan);
        }
    };

    auto run_backend = [&](auto &backend) {
        apply_regalloc(backend);

        {
            StageTimer t("Assembly generation", V);
            target_triple = backend.TargetTriple();
            asm_text = backend.EmitAssembly();
            t.Stop();
        }

        StageTimer t2("Object code emission", V);
        auto mc = backend.EmitObjectCode();

        using SectionT = typename std::decay_t<decltype(mc.sections)>::value_type;
        if (mc.sections.empty()) {
            std::cerr << "[error] backend produced no code sections for target '"
                      << backend.TargetTriple() << "'\n";
            if (!settings.force) {
                backend_failed = true;
                t2.Stop();
                return;
            }
            // In force mode, emit a minimal stub so the pipeline can continue
            // with a clear warning about the stub origin.
            SectionT text;
            text.name = ".text";
            if constexpr (std::is_same_v<std::decay_t<decltype(backend)>, polyglot::backends::x86_64::X86Target>) {
                // xor eax, eax; ret
                text.data = {0x31, 0xC0, 0xC3};
            } else {
                // mov x0, #0; ret
                text.data = {0x00, 0x00, 0x80, 0xD2, 0xC0, 0x03, 0x5F, 0xD6};
            }
            mc.sections.push_back(text);
            mc.symbols.push_back({"_start", ".text", 0, static_cast<std::uint64_t>(text.data.size()), true, true});
            std::cerr << "[warn] generated minimal stub in --force mode\n";
        }

        // Map sections
        sections.clear();
        symbols.clear();
        std::unordered_map<std::string, std::uint32_t> sec_index;
        sections.reserve(mc.sections.size());
        for (const auto &sec : mc.sections) {
            ObjSection osec;
            osec.name = sec.name;
            osec.data = sec.data;
            osec.bss = sec.bss;
            sec_index[sec.name] = static_cast<std::uint32_t>(sections.size());
            sections.push_back(std::move(osec));
        }

        // Build symbols
        symbols.reserve(mc.symbols.size() + 1);
        symbols.push_back({"_start", sec_index.count(".text") ? sec_index[".text"] : 0, 0,
                          mc.sections.front().data.size(), true, true});
        for (const auto &sym : mc.symbols) {
            ObjSymbol osym;
            osym.name = sym.name;
            if (sym.defined && sec_index.count(sym.section)) {
                osym.section_index = sec_index[sym.section];
                osym.defined = true;
            } else {
                osym.section_index = 0xFFFFFFFF;
                osym.defined = false;
            }
            osym.value = sym.value;
            osym.size = sym.size;
            osym.global = sym.global;
            symbols.push_back(osym);
        }

        // Build symbol name -> index map
        std::unordered_map<std::string, std::uint32_t> sym_index;
        for (std::uint32_t i = 0; i < symbols.size(); ++i) sym_index[symbols[i].name] = i;

        // Attach relocations to sections
        for (const auto &r : mc.relocs) {
            auto s_it = sec_index.find(r.section.empty() ? ".text" : r.section);
            if (s_it == sec_index.end()) continue;
            ObjReloc orr{};
            orr.section_index = s_it->second;
            orr.offset = r.offset;
            orr.type = r.type;
            auto sym_it = sym_index.find(r.symbol);
            if (sym_it == sym_index.end()) {
                ObjSymbol ext{};
                ext.name = r.symbol;
                ext.section_index = 0xFFFFFFFF;
                ext.defined = false;
                ext.global = true;
                symbols.push_back(ext);
                sym_index[ext.name] = static_cast<std::uint32_t>(symbols.size() - 1);
                sym_it = sym_index.find(r.symbol);
            }
            orr.symbol_index = sym_it->second;
            orr.addend = r.addend;
            sections[orr.section_index].relocs.push_back(orr);
        }
        t2.Stop();
    };

    if (use_wasm) {
        // WebAssembly backend — emits .wasm binary directly instead of
        // going through the native object code pipeline.  The WASM target
        // does not perform register allocation (stack machine).
        polyglot::backends::wasm::WasmTarget wasm_target(&ir_module);

        {
            StageTimer t("Assembly generation (WAT)", V);
            target_triple = wasm_target.TargetTriple();
            asm_text = wasm_target.EmitAssembly();
            t.Stop();
        }

        StageTimer t2("WASM binary emission", V);
        auto wasm_binary = wasm_target.EmitWasmBinary();
        if (wasm_binary.empty()) {
            std::cerr << "[error] WASM backend produced empty binary\n";
            if (!settings.force) {
                backend_failed = true;
                t2.Stop();
            }
        }

        if (!backend_failed) {
            // Wrap the WASM binary into a single .text section so that the
            // rest of the pipeline (object file emission / aux file writing)
            // can process it uniformly.
            ObjSection text;
            text.name = ".text";
            text.data = std::move(wasm_binary);
            sections.push_back(std::move(text));
            symbols.push_back({"_start", 0, 0,
                               static_cast<std::uint64_t>(sections.front().data.size()),
                               true, true});
        }
        t2.Stop();
    } else if (use_arm64) {
        polyglot::backends::arm64::Arm64Target target(&ir_module);
        run_backend(target);
    } else {
        polyglot::backends::x86_64::X86Target target(&ir_module);
        run_backend(target);
    }

    if (backend_failed) {
        polyglot_gc_unregister_root(&scratch);
        return 1;
    }

    // Write assembly to aux (binary format)
    WriteAuxBinarySingle(aux_dir, stem + ".asm.paux", "assembly", asm_text, V);

    // Also write per-language library object files into aux/
    // Each cross-language bridge is emitted as a separate object file.
    if (!aux_dir.empty() && settings.language == "ploy") {
        StageTimer t("Per-language library emission", V);
        // Collect languages referenced in symbols
        std::unordered_map<std::string, std::vector<std::size_t>> lang_symbol_groups;
        for (std::size_t i = 0; i < symbols.size(); ++i) {
            const auto &sym = symbols[i];
            // Symbols named __ploy_bridge_ploy_<lang>_... belong to that language
            if (sym.name.rfind("__ploy_bridge_ploy_", 0) == 0) {
                auto rest = sym.name.substr(19);  // after "__ploy_bridge_ploy_"
                auto sep = rest.find('_');
                if (sep != std::string::npos) {
                    std::string lang = rest.substr(0, sep);
                    lang_symbol_groups[lang].push_back(i);
                }
            }
        }

        for (const auto &[lang, sym_indices] : lang_symbol_groups) {
            // Build a per-language library object file
            std::vector<ObjSection> lib_sections = sections;  // share sections
            std::vector<ObjSymbol> lib_symbols;

            for (auto idx : sym_indices) {
                if (idx < symbols.size()) {
                    lib_symbols.push_back(symbols[idx]);
                }
            }

            if (!lib_symbols.empty()) {
                fs::path lib_path = fs::path(aux_dir) / (stem + "_" + lang + ".lib.pobj");
                auto written = BuildPobj(lib_path.string(), lib_sections, lib_symbols);
                if (!written.empty() && V) {
                    std::cerr << "[polyc]   -> " << lib_path.string()
                              << " (" << lang << " bridge library)\n";
                }
            }
        }
        t.Stop();
    }

    // ---- Phase 7: Emit outputs ----
    if (!settings.emit_ir_path.empty()) {
        std::ofstream ofs(settings.emit_ir_path);
        if (ofs.is_open()) {
            polyglot::ir::PrintModule(ir_module, ofs);
        }
    }

    if (!settings.emit_asm_path.empty()) {
        std::ofstream ofs(settings.emit_asm_path);
        if (ofs.is_open()) ofs << asm_text;
    }

    // In compile mode: emit binary object file (and per-language libs)
    // In assemble mode: emit object file only
    // In link mode: emit object + link to executable
    auto emit_object_and_maybe_link = [&](bool link_stage) -> bool {
        StageTimer t(link_stage ? "Emit object + link" : "Emit object", V);
        std::string obj_path = EmitObject(settings, sections, symbols);
        if (obj_path.empty()) {
            std::cerr << "[error] failed to emit object\n";
            return false;
        }
        if (V) std::cerr << "[polyc] Produced: " << obj_path << "\n";

        // Also emit object into aux/
        if (!aux_dir.empty()) {
            std::string aux_ext = (settings.obj_format == "coff") ? ".obj"
                                : (settings.obj_format == "pobj") ? ".pobj"
                                : ".o";
            fs::path aux_obj = fs::path(aux_dir) / (stem + aux_ext);
            if (aux_obj.string() != obj_path) {
                std::error_code ec;
                fs::copy_file(obj_path, aux_obj,
                              fs::copy_options::overwrite_existing, ec);
                if (!ec && V) {
                    std::cerr << "[polyc]   -> " << aux_obj.string()
                              << " (object copy in aux)\n";
                }
            }
        }

        if (link_stage) {
            std::string out_exe = settings.output;
            if (settings.obj_format == "pobj") {
                std::string cmd = settings.polyld_path + " " + obj_path + " -o " + out_exe;
                if (V) std::cerr << "[polyc] Invoking polyld -> " << out_exe << "\n";
                int rc = std::system(cmd.c_str());
                if (rc != 0) {
                    std::cerr << "[error] polyld failed (rc=" << rc << ")\n";
                    return false;
                }
            } else if (settings.obj_format == "coff") {
                // On Windows, try MSVC link.exe first, then clang
                std::string cmd = "link /NOLOGO /OUT:" + out_exe + " " + obj_path;
                if (V) std::cerr << "[polyc] Invoking link.exe -> " << out_exe << "\n";
                int rc = std::system(cmd.c_str());
                if (rc != 0) {
                    // Fallback to clang-cl / lld-link
                    cmd = "lld-link /OUT:" + out_exe + " " + obj_path;
                    rc = std::system(cmd.c_str());
                }
                if (rc != 0) {
                    std::cerr << "[warn] system linker not available; object file produced at: "
                              << obj_path << "\n";
                }
            } else {
                std::string cmd = "clang -o " + out_exe + " " + obj_path;
                if (V) std::cerr << "[polyc] Invoking system linker -> " << out_exe << "\n";
                int rc = std::system(cmd.c_str());
                if (rc != 0) {
                    std::cerr << "[warn] system linker not available; object file produced at: "
                              << obj_path << "\n";
                }
            }
            if (V) std::cerr << "[polyc] Link stage completed\n";
        }
        t.Stop();
        return true;
    };

    // compile mode: always emit binary object file
    if (settings.mode == "compile") {
        if (!emit_object_and_maybe_link(false)) return 1;
    }

    if (settings.mode == "link") {
        if (!emit_object_and_maybe_link(true)) return 1;
    }

    if (settings.mode == "assemble") {
        if (!emit_object_and_maybe_link(false)) return 1;
    }

    // ---- Summary ----
    auto total_end = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(total_end - total_start).count();

    if (V) {
        std::cerr << "----------------------------------------\n";
        std::cerr << "[polyc] Target: " << target_triple << "\n";
        std::cerr << "[polyc] Object format: " << settings.obj_format << "\n";
        std::cerr << "[polyc] Total time: " << std::fixed << std::setprecision(1)
                  << total_ms << "ms\n";
        if (!aux_dir.empty()) {
            std::cerr << "[polyc] Aux files (binary): " << aux_dir << "\n";
        }
        std::cerr << "[polyc] Compilation successful.\n";
        std::cerr << "========================================\n";
    }

    polyglot_gc_unregister_root(&scratch);
    return 0;
}
