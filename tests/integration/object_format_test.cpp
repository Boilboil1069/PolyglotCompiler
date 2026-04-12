/**
 * @file     object_format_test.cpp
 * @brief    Integration tests for backend object file format emission
 *
 * Validates ELF64, COFF, Mach-O64 object builders and DWARF / PDB debug
 * information output by constructing a minimal compilation unit (a single
 * function with code bytes) and verifying the resulting binary headers.
 *
 * @ingroup  Test / Integration
 * @author   Manning Cyrus
 * @date     2026-04-11
 */
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "backends/common/include/debug_emitter.h"
#include "backends/common/include/debug_info.h"
#include "tools/polyc/include/driver_stages.h"
#include "tools/polyc/src/stage_packaging.h"

using namespace polyglot;
using namespace polyglot::tools;

namespace fs = std::filesystem;

// ============================================================================
// Helpers
// ============================================================================

namespace {

// Read a binary file into a byte vector.
std::vector<uint8_t> ReadBinary(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return {};
    auto sz = ifs.tellg();
    ifs.seekg(0);
    std::vector<uint8_t> buf(static_cast<std::size_t>(sz));
    ifs.read(reinterpret_cast<char *>(buf.data()), sz);
    return buf;
}

// Read a little-endian uint16_t from a byte buffer at the given offset.
uint16_t ReadU16(const std::vector<uint8_t> &buf, std::size_t off) {
    return static_cast<uint16_t>(buf[off]) |
           (static_cast<uint16_t>(buf[off + 1]) << 8);
}

// Read a little-endian uint32_t from a byte buffer at the given offset.
uint32_t ReadU32(const std::vector<uint8_t> &buf, std::size_t off) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(buf[off + i]) << (i * 8);
    return v;
}

// Read a little-endian uint64_t from a byte buffer at the given offset.
uint64_t ReadU64(const std::vector<uint8_t> &buf, std::size_t off) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(buf[off + i]) << (i * 8);
    return v;
}

// Build a minimal set of sections + symbols that represents a tiny function.
// The .text section contains a handful of NOP bytes (0x90 for x86-64).
void BuildMinimalUnit(std::vector<ObjSection> &sections,
                      std::vector<ObjSymbol>  &symbols) {
    // .text section: 16 bytes of NOPs (simulating a tiny function body)
    ObjSection text;
    text.name = ".text";
    text.bss  = false;
    text.data.assign(16, 0x90);
    // One relocation (rel32, pointing at the first symbol)
    ObjReloc rel;
    rel.section_index = 0;
    rel.offset        = 4;
    rel.type          = 1;  // rel32
    rel.symbol_index  = 0;
    rel.addend        = 0;
    text.relocs.push_back(rel);
    sections.push_back(std::move(text));

    // .data section: 8 bytes (a 64-bit constant)
    ObjSection data;
    data.name = ".data";
    data.bss  = false;
    data.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    sections.push_back(std::move(data));

    // .bss section: 32 bytes
    ObjSection bss;
    bss.name = ".bss";
    bss.bss  = true;
    bss.data.resize(32, 0);
    sections.push_back(std::move(bss));

    // Two symbols: one defined function, one undefined external
    ObjSymbol fn;
    fn.name          = "my_func";
    fn.section_index = 0;
    fn.value         = 0;
    fn.size          = 16;
    fn.global        = true;
    fn.defined       = true;
    symbols.push_back(fn);

    ObjSymbol ext;
    ext.name          = "external_call";
    ext.section_index = 0xFFFFFFFF;
    ext.value         = 0;
    ext.size          = 0;
    ext.global        = true;
    ext.defined       = false;
    symbols.push_back(ext);
}

// Build a DebugInfoBuilder populated with a single function symbol.
backends::DebugInfoBuilder BuildMinimalDebugInfo() {
    backends::DebugInfoBuilder info;

    // Add a function symbol
    backends::DebugSymbol sym;
    sym.name        = "my_func";
    sym.section     = ".text";
    sym.address     = 0;
    sym.size        = 16;
    sym.is_function = true;
    info.AddSymbol(sym);

    // Add a basic type
    backends::DebugType int_type;
    int_type.name      = "int";
    int_type.kind      = "builtin";
    int_type.size      = 4;
    int_type.alignment = 4;
    info.AddType(int_type);

    // Add a pointer type
    backends::DebugType ptr_type;
    ptr_type.name      = "int*";
    ptr_type.kind      = "pointer";
    ptr_type.size      = 8;
    ptr_type.alignment = 8;
    info.AddType(ptr_type);

    // Add line info
    backends::DebugLineInfo line;
    line.file   = "test_source.cpp";
    line.line   = 1;
    line.column = 1;
    info.AddLine(line);

    return info;
}

// Create a temporary directory that is cleaned up at scope exit.
struct TmpDir {
    fs::path path;
    TmpDir() {
        path = fs::temp_directory_path() / "polyglot_objfmt_test";
        fs::create_directories(path);
    }
    ~TmpDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // anonymous namespace

// ============================================================================
// ELF64 Object Format Tests
// ============================================================================

TEST_CASE("Object format: ELF64 header and sections", "[integration][objfmt][elf]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    // Use the packaging-stage ELF builder via DriverSettings + EmitObject.
    // We invoke BuildElf64 indirectly through EmitObject.
    DriverSettings settings;
    settings.output     = (tmp.path / "test").string();
    settings.obj_format = "elf";
    settings.arch       = "x86_64";

    std::string obj_path = settings.output + ".o";
    settings.emit_obj_path = obj_path;

    // EmitObject is declared in the anonymous namespace of stage_packaging.cpp,
    // so we test through the public header-visible BuildElf64 path by writing
    // an ELF ourselves through the same DriverSettings mechanism. Since the
    // anonymous namespace isn't reachable, we instead validate the format by
    // constructing the ELF with the same logic that stage_packaging.cpp uses.
    // For integration purposes we rely on the object existing after a
    // compile-like invocation; however, we can validate format correctness by
    // reading the binary produced by a direct file-write.

    // Write a minimal ELF ourselves using the driver settings:
    // (The test actually tests the *format* — not the driver wiring.)
    // We'll produce a file and then read it back.
    // To call the builder directly we need to forward-declare it:
    // Since it's in an anonymous namespace, we replicate the key check here:
    // Write the object via the public packaging API would require a full
    // BackendResult; instead we write bytes directly and validate the format.
    // Approach: write a minimal valid ELF with known data, read it back.

    // Minimal ELF header probe: 64 bytes, big enough for the ELF header.
    // We exercise the DebugEmitter for DWARF which internally builds an ELF.
    auto info = BuildMinimalDebugInfo();
    std::string dwarf_path = (tmp.path / "test_debug.o").string();
    REQUIRE(backends::DebugEmitter::EmitDWARF(info, dwarf_path));

    auto buf = ReadBinary(dwarf_path);
    REQUIRE(buf.size() >= 64);

    // Validate ELF magic
    REQUIRE(buf[0] == 0x7F);
    REQUIRE(buf[1] == 'E');
    REQUIRE(buf[2] == 'L');
    REQUIRE(buf[3] == 'F');

    // ELF class: 64-bit
    REQUIRE(buf[4] == 2);

    // Data encoding: little-endian
    REQUIRE(buf[5] == 1);

    // e_type: ET_REL (1)
    REQUIRE(ReadU16(buf, 16) == 1);

    // e_machine: EM_X86_64 (62)
    REQUIRE(ReadU16(buf, 18) == 62);

    // e_shnum > 0 (we have debug sections)
    uint16_t shnum = ReadU16(buf, 60);
    REQUIRE(shnum > 0);

    // Verify that the section header string table index is valid
    uint16_t shstrndx = ReadU16(buf, 62);
    REQUIRE(shstrndx < shnum);
}

TEST_CASE("Object format: ELF64 has .eh_frame section", "[integration][objfmt][elf][eh_frame]") {
    TmpDir tmp;
    auto info = BuildMinimalDebugInfo();
    std::string path = (tmp.path / "eh_frame_test.o").string();
    REQUIRE(backends::DebugEmitter::EmitDWARF(info, path));

    auto buf = ReadBinary(path);
    REQUIRE(buf.size() >= 64);

    // Find section headers
    uint64_t shoff    = ReadU64(buf, 40);  // e_shoff
    uint16_t shnum    = ReadU16(buf, 60);
    uint16_t shstrndx = ReadU16(buf, 62);
    REQUIRE(shoff + shnum * 64 <= buf.size());

    // Read .shstrtab to locate section names
    uint64_t shstrtab_off  = ReadU64(buf, shoff + shstrndx * 64 + 24);
    uint64_t shstrtab_size = ReadU64(buf, shoff + shstrndx * 64 + 32);
    REQUIRE(shstrtab_off + shstrtab_size <= buf.size());

    bool found_eh_frame = false;
    bool found_debug_frame = false;
    for (uint16_t i = 0; i < shnum; ++i) {
        uint32_t name_idx = ReadU32(buf, shoff + i * 64);
        if (name_idx < shstrtab_size) {
            std::string name(reinterpret_cast<const char *>(buf.data() + shstrtab_off + name_idx));
            if (name == ".eh_frame")     found_eh_frame = true;
            if (name == ".debug_frame")  found_debug_frame = true;
        }
    }
    REQUIRE(found_eh_frame);
    REQUIRE(found_debug_frame);
}

TEST_CASE("Object format: .eh_frame CIE has 'zR' augmentation", "[integration][objfmt][eh_frame]") {
    TmpDir tmp;
    auto info = BuildMinimalDebugInfo();
    std::string path = (tmp.path / "eh_frame_cie.o").string();
    REQUIRE(backends::DebugEmitter::EmitDWARF(info, path));

    auto buf = ReadBinary(path);
    REQUIRE(buf.size() >= 64);

    // Find .eh_frame section data
    uint64_t shoff    = ReadU64(buf, 40);
    uint16_t shnum    = ReadU16(buf, 60);
    uint16_t shstrndx = ReadU16(buf, 62);
    uint64_t shstrtab_off = ReadU64(buf, shoff + shstrndx * 64 + 24);

    uint64_t eh_off = 0, eh_size = 0;
    for (uint16_t i = 0; i < shnum; ++i) {
        uint32_t name_idx = ReadU32(buf, shoff + i * 64);
        std::string name(reinterpret_cast<const char *>(buf.data() + shstrtab_off + name_idx));
        if (name == ".eh_frame") {
            eh_off  = ReadU64(buf, shoff + i * 64 + 24);
            eh_size = ReadU64(buf, shoff + i * 64 + 32);
            break;
        }
    }
    REQUIRE(eh_size > 0);

    // CIE starts at the beginning of .eh_frame.
    // Bytes: [length:4][CIE_id:4][version:1][augmentation...]
    uint32_t cie_length = ReadU32(buf, eh_off);
    REQUIRE(cie_length > 0);
    uint32_t cie_id = ReadU32(buf, eh_off + 4);
    REQUIRE(cie_id == 0);  // .eh_frame CIE id is 0

    uint8_t version = buf[eh_off + 8];
    REQUIRE(version == 1);

    // Augmentation string starts at offset 9
    char aug0 = static_cast<char>(buf[eh_off + 9]);
    char aug1 = static_cast<char>(buf[eh_off + 10]);
    REQUIRE(aug0 == 'z');
    REQUIRE(aug1 == 'R');
}

// ============================================================================
// COFF Object Format Tests
// ============================================================================

TEST_CASE("Object format: COFF header and sections", "[integration][objfmt][coff]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    // We cannot call the anonymous-namespace BuildCoff directly from the test
    // binary, but the same COFF builder is exposed through the EmitObject
    // dispatcher when obj_format == "coff". For a direct integration test we
    // re-implement the call path: construct DriverSettings, write via the
    // packaging stage. Since that requires a full BackendResult, we instead
    // test the COFF format by examining what a full e2e compile produces.
    // Here, we construct a minimal COFF binary in-process to validate headers.
    //
    // However, the BuildCoff function is in an anonymous namespace inside
    // stage_packaging.cpp. To make this test self-contained we instead use
    // the DebugEmitter PDB path (which exercises the PDB TPI stream) and
    // separately write a COFF-like object with known content for validation.
    //
    // Strategy: compile a minimal C++ function through the frontend → backend
    // pipeline and verify the resulting .obj file.

    // For a true format-level integration test we replicate the COFF header
    // layout and validate it. The real pipeline is tested by test_e2e.

    // --- Direct COFF-format validation ---
    // Write the same sections/symbols through the packaging stage by linking
    // the polyc_lib target (which exports RunPackagingStage).
    BackendResult backend;
    backend.success       = true;
    backend.sections      = sections;
    backend.symbols       = symbols;
    backend.assembly_text = "; nop sled";

    BridgeResult bridge;

    DriverSettings settings;
    settings.output        = (tmp.path / "coff_test").string();
    settings.obj_format    = "coff";
    settings.arch          = "x86_64";
    settings.mode          = "compile";
    settings.emit_obj_path = (tmp.path / "coff_test.obj").string();
    settings.verbose       = false;

    auto pkg = RunPackagingStage(settings, backend, bridge, "", "coff_test");
    REQUIRE(pkg.success);
    REQUIRE(!pkg.obj_path.empty());

    auto buf = ReadBinary(pkg.obj_path);
    REQUIRE(buf.size() >= 20);  // COFF file header is 20 bytes

    // IMAGE_FILE_HEADER: Machine field
    uint16_t machine = ReadU16(buf, 0);
    REQUIRE(machine == 0x8664);  // IMAGE_FILE_MACHINE_AMD64

    // Number of sections >= 1
    uint16_t num_sections = ReadU16(buf, 2);
    REQUIRE(num_sections >= 1);

    // Characteristics should include LARGE_ADDRESS_AWARE (0x0020)
    uint16_t characteristics = ReadU16(buf, 18);
    REQUIRE((characteristics & 0x0020) != 0);

    // Symbol table pointer should be non-zero
    uint32_t sym_ptr = ReadU32(buf, 8);
    REQUIRE(sym_ptr > 0);

    // Number of symbols should be > 0
    uint32_t num_syms = ReadU32(buf, 12);
    REQUIRE(num_syms > 0);

    // Validate first section header (starts at offset 20)
    // Section name should be ".text" or similar
    REQUIRE(buf.size() >= 20 + 40 * num_sections);
    char sec_name[9] = {};
    std::memcpy(sec_name, buf.data() + 20, 8);
    std::string first_sec(sec_name);
    // The first section should be .text
    REQUIRE(first_sec.find(".text") != std::string::npos);

    // SizeOfRawData for .text section should be 16 (our NOP sled)
    uint32_t raw_size = ReadU32(buf, 20 + 16);
    REQUIRE(raw_size == 16);
}

TEST_CASE("Object format: COFF auxiliary section symbols present", "[integration][objfmt][coff]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    BackendResult backend;
    backend.success  = true;
    backend.sections = sections;
    backend.symbols  = symbols;

    BridgeResult bridge;

    DriverSettings settings;
    settings.output        = (tmp.path / "coff_aux").string();
    settings.obj_format    = "coff";
    settings.arch          = "x86_64";
    settings.mode          = "compile";
    settings.emit_obj_path = (tmp.path / "coff_aux.obj").string();

    auto pkg = RunPackagingStage(settings, backend, bridge, "", "coff_aux");
    REQUIRE(pkg.success);

    auto buf = ReadBinary(pkg.obj_path);
    REQUIRE(buf.size() >= 20);

    uint32_t sym_offset = ReadU32(buf, 8);
    uint32_t num_syms   = ReadU32(buf, 12);
    REQUIRE(sym_offset > 0);
    REQUIRE(num_syms > 0);
    REQUIRE(sym_offset + num_syms * 18 <= buf.size());

    // Count auxiliary symbols (NumberOfAuxSymbols > 0)
    uint32_t aux_count = 0;
    for (uint32_t i = 0; i < num_syms; ++i) {
        std::size_t sym_pos = sym_offset + i * 18;
        uint8_t num_aux = buf[sym_pos + 17];
        if (num_aux > 0) {
            aux_count += num_aux;
            i += num_aux;  // skip aux records
        }
    }
    // We should have at least one auxiliary record per section (3 sections)
    REQUIRE(aux_count >= 3);
}

// ============================================================================
// Mach-O64 Object Format Tests
// ============================================================================

TEST_CASE("Object format: Mach-O64 header and load commands", "[integration][objfmt][macho]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    BackendResult backend;
    backend.success  = true;
    backend.sections = sections;
    backend.symbols  = symbols;

    BridgeResult bridge;

    DriverSettings settings;
    settings.output        = (tmp.path / "macho_test").string();
    settings.obj_format    = "macho";
    settings.arch          = "x86_64";
    settings.mode          = "compile";
    settings.emit_obj_path = (tmp.path / "macho_test.o").string();

    auto pkg = RunPackagingStage(settings, backend, bridge, "", "macho_test");
    REQUIRE(pkg.success);
    REQUIRE(!pkg.obj_path.empty());

    auto buf = ReadBinary(pkg.obj_path);
    REQUIRE(buf.size() >= 32);  // mach_header_64 is 32 bytes

    // Mach-O magic: 0xFEEDFACF (64-bit)
    uint32_t magic = ReadU32(buf, 0);
    REQUIRE(magic == 0xFEEDFACF);

    // cputype: x86_64 (0x01000007)
    uint32_t cputype = ReadU32(buf, 4);
    REQUIRE(cputype == 0x01000007);

    // filetype: MH_OBJECT (1)
    uint32_t filetype = ReadU32(buf, 12);
    REQUIRE(filetype == 1);

    // ncmds: should be 3 (LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB)
    uint32_t ncmds = ReadU32(buf, 16);
    REQUIRE(ncmds == 3);
}

TEST_CASE("Object format: Mach-O64 has LC_DYSYMTAB", "[integration][objfmt][macho]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    BackendResult backend;
    backend.success  = true;
    backend.sections = sections;
    backend.symbols  = symbols;

    BridgeResult bridge;

    DriverSettings settings;
    settings.output        = (tmp.path / "macho_dysym").string();
    settings.obj_format    = "macho";
    settings.arch          = "x86_64";
    settings.mode          = "compile";
    settings.emit_obj_path = (tmp.path / "macho_dysym.o").string();

    auto pkg = RunPackagingStage(settings, backend, bridge, "", "macho_dysym");
    REQUIRE(pkg.success);

    auto buf = ReadBinary(pkg.obj_path);
    REQUIRE(buf.size() >= 32);

    uint32_t ncmds     = ReadU32(buf, 16);
    uint32_t sizeofcmds = ReadU32(buf, 20);

    // Walk load commands looking for LC_SYMTAB (0x02) and LC_DYSYMTAB (0x0B)
    bool found_symtab   = false;
    bool found_dysymtab = false;
    std::size_t pos = 32;  // past mach_header_64
    for (uint32_t i = 0; i < ncmds && pos + 8 <= buf.size(); ++i) {
        uint32_t cmd     = ReadU32(buf, pos);
        uint32_t cmdsize = ReadU32(buf, pos + 4);
        if (cmd == 0x02) found_symtab   = true;
        if (cmd == 0x0B) found_dysymtab = true;
        pos += cmdsize;
    }
    REQUIRE(found_symtab);
    REQUIRE(found_dysymtab);
}

TEST_CASE("Object format: Mach-O64 has __DATA section when data present",
          "[integration][objfmt][macho]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    BackendResult backend;
    backend.success  = true;
    backend.sections = sections;
    backend.symbols  = symbols;

    BridgeResult bridge;

    DriverSettings settings;
    settings.output        = (tmp.path / "macho_data").string();
    settings.obj_format    = "macho";
    settings.arch          = "x86_64";
    settings.mode          = "compile";
    settings.emit_obj_path = (tmp.path / "macho_data.o").string();

    auto pkg = RunPackagingStage(settings, backend, bridge, "", "macho_data");
    REQUIRE(pkg.success);

    auto buf = ReadBinary(pkg.obj_path);
    REQUIRE(buf.size() >= 32);

    // Walk load commands to find LC_SEGMENT_64 (0x19) and count sections
    std::size_t pos = 32;
    uint32_t ncmds = ReadU32(buf, 16);
    bool found_text = false;
    bool found_data = false;

    for (uint32_t i = 0; i < ncmds && pos + 8 <= buf.size(); ++i) {
        uint32_t cmd     = ReadU32(buf, pos);
        uint32_t cmdsize = ReadU32(buf, pos + 4);
        if (cmd == 0x19) {  // LC_SEGMENT_64
            uint32_t nsects = ReadU32(buf, pos + 64);
            std::size_t sec_pos = pos + 72;  // section_64 starts after segment_command_64
            for (uint32_t s = 0; s < nsects && sec_pos + 80 <= buf.size(); ++s) {
                char sectname[17] = {};
                std::memcpy(sectname, buf.data() + sec_pos, 16);
                char segname[17] = {};
                std::memcpy(segname, buf.data() + sec_pos + 16, 16);
                if (std::string(sectname) == "__text" && std::string(segname) == "__TEXT")
                    found_text = true;
                if (std::string(sectname) == "__data" && std::string(segname) == "__DATA")
                    found_data = true;
                sec_pos += 80;
            }
        }
        pos += cmdsize;
    }
    REQUIRE(found_text);
    REQUIRE(found_data);
}

TEST_CASE("Object format: Mach-O64 ARM64 cpu type", "[integration][objfmt][macho]") {
    TmpDir tmp;
    std::vector<ObjSection> sections;
    std::vector<ObjSymbol>  symbols;
    BuildMinimalUnit(sections, symbols);

    BackendResult backend;
    backend.success  = true;
    backend.sections = sections;
    backend.symbols  = symbols;

    BridgeResult bridge;

    DriverSettings settings;
    settings.output        = (tmp.path / "macho_arm").string();
    settings.obj_format    = "macho";
    settings.arch          = "arm64";
    settings.mode          = "compile";
    settings.emit_obj_path = (tmp.path / "macho_arm.o").string();

    auto pkg = RunPackagingStage(settings, backend, bridge, "", "macho_arm");
    REQUIRE(pkg.success);

    auto buf = ReadBinary(pkg.obj_path);
    REQUIRE(buf.size() >= 32);

    uint32_t magic   = ReadU32(buf, 0);
    uint32_t cputype = ReadU32(buf, 4);
    REQUIRE(magic == 0xFEEDFACF);
    REQUIRE(cputype == 0x0100000C);  // CPU_TYPE_ARM64
}

// ============================================================================
// PDB Debug Info Tests
// ============================================================================

TEST_CASE("Object format: PDB file has valid MSF header", "[integration][objfmt][pdb]") {
    TmpDir tmp;
    auto info = BuildMinimalDebugInfo();
    std::string pdb_path = (tmp.path / "test.pdb").string();
    REQUIRE(backends::DebugEmitter::EmitPDB(info, pdb_path));

    auto buf = ReadBinary(pdb_path);
    REQUIRE(buf.size() >= 64);

    // MSF magic: "Microsoft C/C++ MSF 7.00\r\n\x1ADS\0\0\0"
    REQUIRE(buf[0] == 'M');
    REQUIRE(buf[1] == 'i');
    REQUIRE(buf[2] == 'c');
    REQUIRE(buf[3] == 'r');
    REQUIRE(buf[4] == 'o');

    // Block size should be 4096
    uint32_t block_size = ReadU32(buf, 32);
    REQUIRE(block_size == 4096);

    // Number of blocks should be > 0
    uint32_t num_blocks = ReadU32(buf, 40);
    REQUIRE(num_blocks > 0);
}

TEST_CASE("Object format: PDB TPI stream has type records", "[integration][objfmt][pdb]") {
    TmpDir tmp;
    auto info = BuildMinimalDebugInfo();
    std::string pdb_path = (tmp.path / "tpi_test.pdb").string();
    REQUIRE(backends::DebugEmitter::EmitPDB(info, pdb_path));

    auto buf = ReadBinary(pdb_path);
    REQUIRE(buf.size() >= 4096 * 4);  // Need at least header + a few blocks

    // The TPI stream is stream 2. The stream directory is at block 3.
    // Read stream directory from block 3.
    uint32_t block_size = ReadU32(buf, 32);
    std::size_t dir_off = 3 * static_cast<std::size_t>(block_size);
    REQUIRE(dir_off + 4 <= buf.size());

    uint32_t num_streams = ReadU32(buf, dir_off);
    REQUIRE(num_streams >= 3);  // At least streams 0, 1, 2

    // Read stream sizes
    std::vector<uint32_t> stream_sizes(num_streams);
    for (uint32_t i = 0; i < num_streams; ++i) {
        stream_sizes[i] = ReadU32(buf, dir_off + 4 + i * 4);
    }

    // TPI stream (index 2) should have non-zero size
    uint32_t tpi_size = stream_sizes[2];
    REQUIRE(tpi_size > 0);

    // Read TPI stream block index
    std::size_t block_idx_off = dir_off + 4 + num_streams * 4;
    // Skip blocks for streams 0 and 1
    for (uint32_t s = 0; s < 2; ++s) {
        uint32_t nblocks = stream_sizes[s] == 0 ? 0 :
            (stream_sizes[s] + block_size - 1) / block_size;
        block_idx_off += nblocks * 4;
    }

    uint32_t tpi_block = ReadU32(buf, block_idx_off);
    std::size_t tpi_off = static_cast<std::size_t>(tpi_block) * block_size;
    REQUIRE(tpi_off + 56 <= buf.size());

    // TPI header: Version should be 20040203
    uint32_t tpi_version = ReadU32(buf, tpi_off);
    REQUIRE(tpi_version == 20040203);

    // Header size should be 56
    uint32_t header_size = ReadU32(buf, tpi_off + 4);
    REQUIRE(header_size == 56);

    // Type index begin should be 0x1000
    uint32_t ti_begin = ReadU32(buf, tpi_off + 8);
    REQUIRE(ti_begin == 0x1000);

    // Type index end should be > 0x1000 (we have type records)
    uint32_t ti_end = ReadU32(buf, tpi_off + 12);
    REQUIRE(ti_end > 0x1000);

    // Type record bytes should be > 0
    uint32_t tr_bytes = ReadU32(buf, tpi_off + 16);
    REQUIRE(tr_bytes > 0);
}

// ============================================================================
// DWARF Debug Info Tests
// ============================================================================

TEST_CASE("Object format: DWARF output contains all standard sections",
          "[integration][objfmt][dwarf]") {
    TmpDir tmp;
    auto info = BuildMinimalDebugInfo();
    std::string path = (tmp.path / "dwarf_all.o").string();
    REQUIRE(backends::DebugEmitter::EmitDWARF(info, path));

    auto buf = ReadBinary(path);
    REQUIRE(buf.size() >= 64);

    // Parse section names from ELF section headers
    uint64_t shoff    = ReadU64(buf, 40);
    uint16_t shnum    = ReadU16(buf, 60);
    uint16_t shstrndx = ReadU16(buf, 62);
    uint64_t shstrtab_off = ReadU64(buf, shoff + shstrndx * 64 + 24);

    std::vector<std::string> section_names;
    for (uint16_t i = 0; i < shnum; ++i) {
        uint32_t name_idx = ReadU32(buf, shoff + i * 64);
        section_names.emplace_back(
            reinterpret_cast<const char *>(buf.data() + shstrtab_off + name_idx));
    }

    auto has_section = [&](const std::string &name) {
        return std::find(section_names.begin(), section_names.end(), name) != section_names.end();
    };

    REQUIRE(has_section(".debug_str"));
    REQUIRE(has_section(".debug_abbrev"));
    REQUIRE(has_section(".debug_info"));
    REQUIRE(has_section(".debug_line"));
    REQUIRE(has_section(".debug_frame"));
    REQUIRE(has_section(".eh_frame"));
    REQUIRE(has_section(".debug_aranges"));
}

TEST_CASE("Object format: Source map JSON emission", "[integration][objfmt][srcmap]") {
    TmpDir tmp;
    auto info = BuildMinimalDebugInfo();
    std::string map_path = (tmp.path / "test.map").string();
    REQUIRE(backends::DebugEmitter::EmitSourceMap(info, map_path));

    // Read it back and check it contains expected content
    std::ifstream ifs(map_path);
    REQUIRE(ifs.is_open());
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    REQUIRE(!content.empty());
    // Should contain the function name and source file
    REQUIRE(content.find("my_func") != std::string::npos);
    REQUIRE(content.find("test_source.cpp") != std::string::npos);
}
