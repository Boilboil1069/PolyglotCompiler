#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <fstream>
#include <vector>
#include <cstring>

#include "tools/polyld/include/linker.h"

using namespace polyglot::linker;

// ============================================================================
// Test Helpers
// ============================================================================

namespace {

// Create a minimal ELF header for testing
std::vector<std::uint8_t> CreateMinimalELF64Header() {
    std::vector<std::uint8_t> data(64, 0);
    
    // ELF magic
    data[0] = 0x7f;
    data[1] = 'E';
    data[2] = 'L';
    data[3] = 'F';
    
    // Class: 64-bit
    data[4] = 2;  // ELFCLASS64
    
    // Data: Little-endian
    data[5] = 1;  // ELFDATA2LSB
    
    // Version
    data[6] = 1;
    
    // Type: REL (relocatable)
    data[16] = 1;
    data[17] = 0;
    
    // Machine: x86_64
    data[18] = 62;
    data[19] = 0;
    
    // Version
    data[20] = 1;
    
    // ELF header size (e_ehsize at offset 52)
    data[52] = 64;
    
    return data;
}

// Create a minimal Mach-O header for testing
std::vector<std::uint8_t> CreateMinimalMachO64Header() {
    std::vector<std::uint8_t> data(32, 0);
    
    // Magic (MH_MAGIC_64, little-endian)
    data[0] = 0xcf;
    data[1] = 0xfa;
    data[2] = 0xed;
    data[3] = 0xfe;
    
    // CPU type: x86_64
    data[4] = 0x07;
    data[5] = 0x00;
    data[6] = 0x00;
    data[7] = 0x01;
    
    // CPU subtype
    data[8] = 0x03;
    
    // File type: MH_OBJECT
    data[12] = 0x01;
    
    return data;
}

// Create a minimal archive header for testing
std::vector<std::uint8_t> CreateMinimalArchive() {
    std::vector<std::uint8_t> data;
    
    // Archive magic
    const char* magic = "!<arch>\n";
    for (int i = 0; i < 8; ++i) {
        data.push_back(static_cast<std::uint8_t>(magic[i]));
    }
    
    return data;
}

// Create a test object file in temp directory
std::string CreateTestObjectFile(const std::string& prefix) {
    std::string path = "/tmp/" + prefix + "_test.o";
    auto elf = CreateMinimalELF64Header();
    
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(elf.data()), elf.size());
    file.close();
    
    return path;
}

// Clean up test file
void RemoveTestFile(const std::string& path) {
    std::remove(path.c_str());
}

} // namespace

// ============================================================================
// Object Format Detection Tests
// ============================================================================

TEST_CASE("DetectObjectFormat identifies ELF files", "[linker][format]") {
    auto elf_data = CreateMinimalELF64Header();
    
    SECTION("Valid ELF header") {
        ObjectFormat format = DetectObjectFormat(elf_data);
        REQUIRE(format == ObjectFormat::kELF);
    }
    
    SECTION("Too small to be ELF") {
        std::vector<std::uint8_t> small_data = {0x7f, 'E', 'L'};
        ObjectFormat format = DetectObjectFormat(small_data);
        REQUIRE(format != ObjectFormat::kELF);
    }
    
    SECTION("Wrong magic") {
        elf_data[0] = 0x00;
        ObjectFormat format = DetectObjectFormat(elf_data);
        REQUIRE(format != ObjectFormat::kELF);
    }
}

TEST_CASE("DetectObjectFormat identifies Mach-O files", "[linker][format]") {
    auto macho_data = CreateMinimalMachO64Header();
    
    SECTION("Valid Mach-O 64-bit header") {
        ObjectFormat format = DetectObjectFormat(macho_data);
        REQUIRE(format == ObjectFormat::kMachO);
    }
    
    SECTION("Mach-O 32-bit magic") {
        macho_data[0] = 0xce;
        macho_data[1] = 0xfa;
        macho_data[2] = 0xed;
        macho_data[3] = 0xfe;
        ObjectFormat format = DetectObjectFormat(macho_data);
        REQUIRE(format == ObjectFormat::kMachO);
    }
}

TEST_CASE("DetectObjectFormat identifies archive files", "[linker][format]") {
    auto archive_data = CreateMinimalArchive();
    
    SECTION("Valid archive magic") {
        ObjectFormat format = DetectObjectFormat(archive_data);
        REQUIRE(format == ObjectFormat::kArchive);
    }
}

TEST_CASE("DetectObjectFormat returns Unknown for invalid data", "[linker][format]") {
    SECTION("Empty data") {
        std::vector<std::uint8_t> empty;
        ObjectFormat format = DetectObjectFormat(empty);
        REQUIRE(format == ObjectFormat::kUnknown);
    }
    
    SECTION("Random data") {
        std::vector<std::uint8_t> random = {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
        ObjectFormat format = DetectObjectFormat(random);
        REQUIRE(format == ObjectFormat::kUnknown);
    }
}

// ============================================================================
// Symbol Utility Tests
// ============================================================================

TEST_CASE("DemangleSymbol handles basic cases", "[linker][symbol]") {
    SECTION("Empty symbol") {
        std::string result = DemangleSymbol("");
        REQUIRE(result.empty());
    }
    
    SECTION("Simple symbol with leading underscore") {
        std::string result = DemangleSymbol("_main");
        REQUIRE(result == "main");
    }
    
    SECTION("Symbol without underscore") {
        std::string result = DemangleSymbol("main");
        REQUIRE(result == "main");
    }
    
    SECTION("Single underscore") {
        std::string result = DemangleSymbol("_");
        REQUIRE(result == "_");
    }
}

TEST_CASE("SymbolMatchesPattern handles wildcards", "[linker][symbol]") {
    SECTION("Exact match") {
        REQUIRE(SymbolMatchesPattern("foo", "foo"));
        REQUIRE_FALSE(SymbolMatchesPattern("foo", "bar"));
    }
    
    SECTION("Star wildcard matches anything") {
        REQUIRE(SymbolMatchesPattern("anything", "*"));
        REQUIRE(SymbolMatchesPattern("", "*"));
    }
    
    SECTION("Star wildcard at end") {
        REQUIRE(SymbolMatchesPattern("foobar", "foo*"));
        REQUIRE_FALSE(SymbolMatchesPattern("barfoo", "foo*"));
    }
    
    SECTION("Star wildcard at beginning") {
        REQUIRE(SymbolMatchesPattern("barfoo", "*foo"));
        REQUIRE_FALSE(SymbolMatchesPattern("foobar", "*foo"));
    }
    
    SECTION("Question mark wildcard") {
        REQUIRE(SymbolMatchesPattern("foo", "f?o"));
        REQUIRE_FALSE(SymbolMatchesPattern("fooo", "f?o"));
    }
}

// ============================================================================
// Section Type Tests
// ============================================================================

TEST_CASE("GetSectionTypeFromName returns correct types", "[linker][section]") {
    SECTION("BSS section") {
        REQUIRE(GetSectionTypeFromName(".bss") == SectionType::kNobits);
        REQUIRE(GetSectionTypeFromName("__bss") == SectionType::kNobits);
    }
    
    SECTION("Symbol table") {
        REQUIRE(GetSectionTypeFromName(".symtab") == SectionType::kSymtab);
    }
    
    SECTION("String table") {
        REQUIRE(GetSectionTypeFromName(".strtab") == SectionType::kStrtab);
        REQUIRE(GetSectionTypeFromName(".shstrtab") == SectionType::kStrtab);
    }
    
    SECTION("Dynamic section") {
        REQUIRE(GetSectionTypeFromName(".dynamic") == SectionType::kDynamic);
    }
    
    SECTION("Relocation sections") {
        REQUIRE(GetSectionTypeFromName(".rela") == SectionType::kRela);
        REQUIRE(GetSectionTypeFromName(".rela.text") == SectionType::kRela);
        REQUIRE(GetSectionTypeFromName(".rel") == SectionType::kRel);
    }
    
    SECTION("Default to PROGBITS") {
        REQUIRE(GetSectionTypeFromName(".text") == SectionType::kProgbits);
        REQUIRE(GetSectionTypeFromName(".data") == SectionType::kProgbits);
        REQUIRE(GetSectionTypeFromName("custom") == SectionType::kProgbits);
    }
}

TEST_CASE("GetDefaultSectionFlags returns appropriate flags", "[linker][section]") {
    SECTION("Text section is executable") {
        SectionFlags flags = GetDefaultSectionFlags(".text");
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kAlloc)) != 0);
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kExecInstr)) != 0);
    }
    
    SECTION("Data section is writable") {
        SectionFlags flags = GetDefaultSectionFlags(".data");
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kAlloc)) != 0);
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kWrite)) != 0);
    }
    
    SECTION("Rodata section is read-only") {
        SectionFlags flags = GetDefaultSectionFlags(".rodata");
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kAlloc)) != 0);
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kWrite)) == 0);
    }
    
    SECTION("BSS section is writable") {
        SectionFlags flags = GetDefaultSectionFlags(".bss");
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kAlloc)) != 0);
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kWrite)) != 0);
    }
}

// ============================================================================
// Symbol Tests
// ============================================================================

TEST_CASE("Symbol binding classification", "[linker][symbol]") {
    Symbol sym;
    
    SECTION("Global symbol") {
        sym.binding = SymbolBinding::kGlobal;
        REQUIRE(sym.is_global());
        REQUIRE_FALSE(sym.is_weak());
        REQUIRE(sym.binding != SymbolBinding::kLocal);
    }
    
    SECTION("Local symbol") {
        sym.binding = SymbolBinding::kLocal;
        REQUIRE_FALSE(sym.is_global());
        REQUIRE_FALSE(sym.is_weak());
        REQUIRE(sym.binding == SymbolBinding::kLocal);
    }
    
    SECTION("Weak symbol") {
        sym.binding = SymbolBinding::kWeak;
        REQUIRE(sym.is_global());  // Weak symbols are also considered global
        REQUIRE(sym.is_weak());
    }
}

// ============================================================================
// ObjectFile Tests
// ============================================================================

TEST_CASE("ObjectFile section lookup", "[linker][object]") {
    ObjectFile obj;
    
    // Add some sections
    InputSection text_sec;
    text_sec.name = ".text";
    obj.sections.push_back(text_sec);
    
    InputSection data_sec;
    data_sec.name = ".data";
    obj.sections.push_back(data_sec);
    
    SECTION("Find existing section") {
        InputSection* found = obj.FindSection(".text");
        REQUIRE(found != nullptr);
        REQUIRE(found->name == ".text");
    }
    
    SECTION("Find non-existent section") {
        InputSection* found = obj.FindSection(".bss");
        REQUIRE(found == nullptr);
    }
    
    SECTION("Const version") {
        const ObjectFile& const_obj = obj;
        const InputSection* found = const_obj.FindSection(".data");
        REQUIRE(found != nullptr);
    }
}

TEST_CASE("ObjectFile symbol lookup", "[linker][object]") {
    ObjectFile obj;
    
    // Add some symbols
    Symbol main_sym;
    main_sym.name = "main";
    main_sym.is_defined = true;
    obj.symbols.push_back(main_sym);
    
    Symbol printf_sym;
    printf_sym.name = "printf";
    printf_sym.is_defined = false;
    obj.symbols.push_back(printf_sym);
    
    SECTION("Find defined symbol") {
        Symbol* found = obj.FindSymbol("main");
        REQUIRE(found != nullptr);
        REQUIRE(found->is_defined);
    }
    
    SECTION("Find undefined symbol") {
        Symbol* found = obj.FindSymbol("printf");
        REQUIRE(found != nullptr);
        REQUIRE_FALSE(found->is_defined);
    }
    
    SECTION("Find non-existent symbol") {
        Symbol* found = obj.FindSymbol("exit");
        REQUIRE(found == nullptr);
    }
}

// ============================================================================
// Linker Configuration Tests
// ============================================================================

TEST_CASE("LinkerConfig default values", "[linker][config]") {
    LinkerConfig config;
    
    REQUIRE(config.entry_point == "_start");
    REQUIRE(config.output_format == OutputFormat::kExecutable);
    REQUIRE(config.target_arch == TargetArch::kX86_64);
    REQUIRE(config.base_address == 0x400000);
    REQUIRE_FALSE(config.verbose);
    REQUIRE(config.static_link);  // Default is static linking
    REQUIRE_FALSE(config.gc_sections);
}

// ============================================================================
// Linker Basic Tests
// ============================================================================

TEST_CASE("Linker initialization", "[linker]") {
    LinkerConfig config;
    config.verbose = false;
    config.output_file = "/tmp/test_output";
    
    Linker linker(config);
    
    SECTION("Initial state") {
        REQUIRE(linker.GetErrors().empty());
        REQUIRE(linker.GetWarnings().empty());
    }
}

TEST_CASE("Linker symbol table operations", "[linker][symbol]") {
    LinkerConfig config;
    Linker linker(config);
    
    SECTION("Add and lookup symbol") {
        Symbol sym;
        sym.name = "test_func";
        sym.is_defined = true;
        sym.binding = SymbolBinding::kGlobal;
        
        bool added = linker.AddSymbol(sym);
        REQUIRE(added);
        
        Symbol* found = linker.LookupSymbol("test_func");
        REQUIRE(found != nullptr);
        REQUIRE(found->name == "test_func");
    }
    
    SECTION("Multiple definitions with weak override") {
        Symbol weak_sym;
        weak_sym.name = "shared";
        weak_sym.is_defined = true;
        weak_sym.binding = SymbolBinding::kWeak;
        weak_sym.value = 100;
        
        Symbol strong_sym;
        strong_sym.name = "shared";
        strong_sym.is_defined = true;
        strong_sym.binding = SymbolBinding::kGlobal;
        strong_sym.value = 200;
        
        linker.AddSymbol(weak_sym);
        linker.AddSymbol(strong_sym);
        
        Symbol* found = linker.LookupSymbol("shared");
        REQUIRE(found != nullptr);
        REQUIRE(found->value == 200);  // Strong overrides weak
    }
    
    SECTION("Undefined symbol reference") {
        Symbol undef_sym;
        undef_sym.name = "external";
        undef_sym.is_defined = false;
        undef_sym.binding = SymbolBinding::kGlobal;
        
        linker.AddSymbol(undef_sym);
        
        auto undefined = linker.GetUndefinedSymbols();
        REQUIRE(undefined.size() == 1);
        REQUIRE(undefined[0]->name == "external");
    }
}

// ============================================================================
// Linker Section Layout Tests
// ============================================================================

TEST_CASE("Linker section creation", "[linker][section]") {
    LinkerConfig config;
    Linker linker(config);
    
    SECTION("Create output section") {
        linker.CreateOutputSection(".custom", SectionFlags::kAlloc | SectionFlags::kWrite);
        
        OutputSection* sec = linker.GetOutputSection(".custom");
        REQUIRE(sec != nullptr);
        REQUIRE(sec->name == ".custom");
    }
    
    SECTION("Get non-existent section") {
        OutputSection* sec = linker.GetOutputSection(".nonexistent");
        REQUIRE(sec == nullptr);
    }
}

// ============================================================================
// SectionFlags Operator Tests
// ============================================================================

TEST_CASE("SectionFlags bitwise operations", "[linker][section]") {
    SECTION("OR operation") {
        SectionFlags flags = SectionFlags::kAlloc | SectionFlags::kWrite;
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kAlloc)) != 0);
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kWrite)) != 0);
    }
    
    SECTION("AND operation") {
        SectionFlags flags = SectionFlags::kAlloc | SectionFlags::kWrite;
        SectionFlags result = flags & SectionFlags::kAlloc;
        REQUIRE(result == SectionFlags::kAlloc);
    }
    
    SECTION("OR assignment") {
        SectionFlags flags = SectionFlags::kNone;
        flags = flags | SectionFlags::kExecInstr;
        REQUIRE((static_cast<std::uint64_t>(flags) & 
                 static_cast<std::uint64_t>(SectionFlags::kExecInstr)) != 0);
    }
    
    SECTION("Multiple flags") {
        SectionFlags flags = SectionFlags::kAlloc | SectionFlags::kWrite | SectionFlags::kExecInstr;
        REQUIRE(static_cast<std::uint64_t>(flags) != 0);
    }
}

// ============================================================================
// Linker Script Parser Tests
// ============================================================================

TEST_CASE("LinkerScriptParser basic parsing", "[linker][script]") {
    SECTION("Parse ENTRY command") {
        std::string script = "ENTRY(_start)";
        LinkerScriptParser parser(script);
        
        bool result = parser.Parse();
        REQUIRE(result);
        REQUIRE(parser.GetEntryPoint() == "_start");
    }
    
    SECTION("Parse with comments") {
        std::string script = "/* comment */ ENTRY(main)";
        LinkerScriptParser parser(script);
        
        bool result = parser.Parse();
        REQUIRE(result);
        REQUIRE(parser.GetEntryPoint() == "main");
    }
}

TEST_CASE("LinkerScriptParser error handling", "[linker][script]") {
    SECTION("Empty script") {
        LinkerScriptParser parser("");
        bool result = parser.Parse();
        REQUIRE(result);  // Empty is valid
    }
    
    SECTION("Whitespace only") {
        LinkerScriptParser parser("   \n\t  ");
        bool result = parser.Parse();
        REQUIRE(result);
    }
}

// ============================================================================
// Relocation Tests
// ============================================================================

TEST_CASE("Relocation structure", "[linker][relocation]") {
    Relocation reloc;
    
    SECTION("Default values") {
        REQUIRE(reloc.offset == 0);
        REQUIRE(reloc.addend == 0);
        REQUIRE_FALSE(reloc.is_pc_relative);
    }
    
    SECTION("PC-relative relocation") {
        reloc.is_pc_relative = true;
        reloc.type = 2;  // R_X86_64_PC32
        reloc.size = 4;
        
        REQUIRE(reloc.is_pc_relative);
        REQUIRE(reloc.size == 4);
    }
}

// ============================================================================
// GOT/PLT Tests - These test internal data structures
// ============================================================================

TEST_CASE("GOT entry structure", "[linker][got]") {
    GOTEntry entry;
    
    SECTION("Default values") {
        REQUIRE(entry.symbol.empty());
        REQUIRE(entry.index == -1);
        REQUIRE(entry.address == 0);
        REQUIRE_FALSE(entry.is_resolved);
    }
    
    SECTION("Set values") {
        entry.symbol = "printf";
        entry.index = 5;
        entry.address = 0x1000;
        entry.is_resolved = true;
        
        REQUIRE(entry.symbol == "printf");
        REQUIRE(entry.index == 5);
    }
}

TEST_CASE("PLT entry structure", "[linker][plt]") {
    PLTEntry entry;
    
    SECTION("Default values") {
        REQUIRE(entry.symbol.empty());
        REQUIRE(entry.index == -1);
        REQUIRE(entry.address == 0);
        REQUIRE(entry.got_offset == 0);
    }
    
    SECTION("Set values") {
        entry.symbol = "puts";
        entry.index = 3;
        entry.address = 0x2000;
        entry.got_offset = 0x100;
        
        REQUIRE(entry.symbol == "puts");
        REQUIRE(entry.got_offset == 0x100);
    }
}

// ============================================================================
// LinkerStats Tests
// ============================================================================

TEST_CASE("LinkerStats tracking", "[linker][stats]") {
    LinkerStats stats;
    
    SECTION("Initial values are zero") {
        REQUIRE(stats.objects_loaded == 0);
        REQUIRE(stats.archives_loaded == 0);
        REQUIRE(stats.symbols_defined == 0);
        REQUIRE(stats.relocations_processed == 0);
        REQUIRE(stats.code_size == 0);
        REQUIRE(stats.data_size == 0);
        REQUIRE(stats.bss_size == 0);
        REQUIRE(stats.total_output_size == 0);
    }
}

// ============================================================================
// Archive Tests
// ============================================================================

TEST_CASE("Archive structures", "[linker][archive]") {
    Archive archive;
    
    SECTION("Empty archive") {
        REQUIRE(archive.path.empty());
        REQUIRE(archive.members.empty());
        REQUIRE(archive.symbols.empty());
    }
    
    SECTION("Archive member") {
        ArchiveMember member;
        member.name = "test.o";
        member.offset = 8;
        member.size = 1024;
        member.data.resize(1024, 0);
        
        archive.members.push_back(member);
        
        REQUIRE(archive.members.size() == 1);
        REQUIRE(archive.members[0].name == "test.o");
    }
    
    SECTION("Archive symbol") {
        ArchiveSymbol sym;
        sym.name = "test_func";
        sym.member_offset = 8;
        
        archive.symbols.push_back(sym);
        
        REQUIRE(archive.symbols.size() == 1);
        REQUIRE(archive.symbols[0].name == "test_func");
    }
}

// ============================================================================
// Output Section Tests
// ============================================================================

TEST_CASE("OutputSection contributions", "[linker][section]") {
    OutputSection sec;
    sec.name = ".text";
    
    SECTION("Add contribution") {
        OutputSection::Contribution contrib;
        contrib.object_index = 0;
        contrib.section_index = 1;
        contrib.input_offset = 0;
        contrib.output_offset = 0;
        contrib.size = 100;
        
        sec.contributions.push_back(contrib);
        
        REQUIRE(sec.contributions.size() == 1);
        REQUIRE(sec.contributions[0].size == 100);
    }
    
    SECTION("Multiple contributions") {
        OutputSection::Contribution contrib1{0, 1, 0, 0, 100};
        OutputSection::Contribution contrib2{1, 1, 0, 100, 200};
        
        sec.contributions.push_back(contrib1);
        sec.contributions.push_back(contrib2);
        
        REQUIRE(sec.contributions.size() == 2);
    }
}

// ============================================================================
// Output Segment Tests
// ============================================================================

TEST_CASE("OutputSegment structure", "[linker][segment]") {
    OutputSegment seg;
    
    SECTION("Text segment") {
        seg.name = "__TEXT";
        seg.protection = 5;  // R + X
        seg.section_names.push_back(".text");
        seg.section_names.push_back(".rodata");
        
        REQUIRE(seg.name == "__TEXT");
        REQUIRE(seg.protection == 5);
        REQUIRE(seg.section_names.size() == 2);
    }
    
    SECTION("Data segment") {
        seg.name = "__DATA";
        seg.protection = 6;  // R + W
        seg.section_names.push_back(".data");
        seg.section_names.push_back(".bss");
        
        REQUIRE(seg.protection == 6);
    }
}

// ============================================================================
// Alignment Utility Tests
// ============================================================================

TEST_CASE("Linker alignment utilities", "[linker][util]") {
    LinkerConfig config;
    Linker linker(config);
    
    // Test through public interface - alignment is tested indirectly
    // through section layout
}

// ============================================================================
// Target Architecture Tests
// ============================================================================

TEST_CASE("Target architecture configuration", "[linker][config]") {
    LinkerConfig config;
    
    SECTION("x86_64 target") {
        config.target_arch = TargetArch::kX86_64;
        REQUIRE(config.target_arch == TargetArch::kX86_64);
    }
    
    SECTION("AArch64 target") {
        config.target_arch = TargetArch::kAArch64;
        REQUIRE(config.target_arch == TargetArch::kAArch64);
    }
    
    SECTION("x86 target") {
        config.target_arch = TargetArch::kX86;
        REQUIRE(config.target_arch == TargetArch::kX86);
    }
    
    SECTION("ARM target") {
        config.target_arch = TargetArch::kARM;
        REQUIRE(config.target_arch == TargetArch::kARM);
    }
    
    SECTION("RISCV64 target") {
        config.target_arch = TargetArch::kRISCV64;
        REQUIRE(config.target_arch == TargetArch::kRISCV64);
    }
}

// ============================================================================
// Output Format Tests
// ============================================================================

TEST_CASE("Output format configuration", "[linker][config]") {
    LinkerConfig config;
    
    SECTION("Executable output") {
        config.output_format = OutputFormat::kExecutable;
        REQUIRE(config.output_format == OutputFormat::kExecutable);
    }
    
    SECTION("Shared library output") {
        config.output_format = OutputFormat::kSharedLibrary;
        REQUIRE(config.output_format == OutputFormat::kSharedLibrary);
    }
    
    SECTION("Relocatable output") {
        config.output_format = OutputFormat::kRelocatable;
        REQUIRE(config.output_format == OutputFormat::kRelocatable);
    }
    
    SECTION("Static library output") {
        config.output_format = OutputFormat::kStaticLibrary;
        REQUIRE(config.output_format == OutputFormat::kStaticLibrary);
    }
}

// ============================================================================
// Input Section Tests
// ============================================================================

TEST_CASE("InputSection structure", "[linker][section]") {
    InputSection sec;
    
    SECTION("Default values") {
        REQUIRE(sec.name.empty());
        REQUIRE(sec.data.empty());
        REQUIRE(sec.alignment == 1);
    }
    
    SECTION("Section with data") {
        sec.name = ".text";
        sec.data = {0x90, 0x90, 0x90, 0xc3};  // nop, nop, nop, ret
        sec.alignment = 16;
        sec.flags = SectionFlags::kAlloc | SectionFlags::kExecInstr;
        
        REQUIRE(sec.data.size() == 4);
        REQUIRE(sec.alignment == 16);
    }
    
    SECTION("Section with relocations") {
        Relocation reloc;
        reloc.offset = 10;
        reloc.symbol = "printf";
        reloc.type = 2;
        
        sec.relocations.push_back(reloc);
        
        REQUIRE(sec.relocations.size() == 1);
    }
}

// ============================================================================
// Symbol Type and Visibility Tests
// ============================================================================

TEST_CASE("Symbol types", "[linker][symbol]") {
    Symbol sym;
    
    SECTION("Function symbol") {
        sym.type = SymbolType::kFunction;
        REQUIRE(sym.type == SymbolType::kFunction);
    }
    
    SECTION("Object symbol") {
        sym.type = SymbolType::kObject;
        REQUIRE(sym.type == SymbolType::kObject);
    }
    
    SECTION("TLS symbol") {
        sym.type = SymbolType::kTLS;
        REQUIRE(sym.type == SymbolType::kTLS);
    }
}

TEST_CASE("Symbol visibility", "[linker][symbol]") {
    Symbol sym;
    
    SECTION("Default visibility") {
        sym.visibility = SymbolVisibility::kDefault;
        REQUIRE(sym.visibility == SymbolVisibility::kDefault);
    }
    
    SECTION("Hidden visibility") {
        sym.visibility = SymbolVisibility::kHidden;
        REQUIRE(sym.visibility == SymbolVisibility::kHidden);
    }
    
    SECTION("Protected visibility") {
        sym.visibility = SymbolVisibility::kProtected;
        REQUIRE(sym.visibility == SymbolVisibility::kProtected);
    }
}

// ============================================================================
// Error and Warning Handling Tests
// ============================================================================

TEST_CASE("Linker error handling", "[linker][error]") {
    LinkerConfig config;
    config.verbose = false;
    
    Linker linker(config);
    
    SECTION("No errors initially") {
        REQUIRE(linker.GetErrors().empty());
    }
    
    SECTION("No warnings initially") {
        REQUIRE(linker.GetWarnings().empty());
    }
}

// ============================================================================
// Library Path Resolution Tests
// ============================================================================

TEST_CASE("Library path configuration", "[linker][config]") {
    LinkerConfig config;
    
    SECTION("Add library paths") {
        config.library_paths.push_back("/usr/lib");
        config.library_paths.push_back("/usr/local/lib");
        
        REQUIRE(config.library_paths.size() == 2);
    }
    
    SECTION("Add libraries") {
        config.libraries.push_back("c");
        config.libraries.push_back("m");
        config.libraries.push_back("pthread");
        
        REQUIRE(config.libraries.size() == 3);
    }
}

// ============================================================================
// Linker Options Tests
// ============================================================================

TEST_CASE("Linker options", "[linker][config]") {
    LinkerConfig config;
    
    SECTION("GC sections option") {
        config.gc_sections = true;
        REQUIRE(config.gc_sections);
    }
    
    SECTION("Strip options") {
        config.strip_all = true;
        config.strip_debug = true;
        REQUIRE(config.strip_all);
        REQUIRE(config.strip_debug);
    }
    
    SECTION("PIE option") {
        config.pie = true;
        REQUIRE(config.pie);
    }
    
    SECTION("Build ID option") {
        config.build_id = true;
        REQUIRE(config.build_id);
    }
    
    SECTION("ICF option") {
        config.icf = true;
        REQUIRE(config.icf);
    }
    
    SECTION("No undefined option") {
        config.no_undefined = true;
        REQUIRE(config.no_undefined);
    }
    
    SECTION("Allow multiple definition") {
        config.allow_multiple_definition = true;
        REQUIRE(config.allow_multiple_definition);
    }
}

// ============================================================================
// Relocation Type Enum Tests
// ============================================================================

TEST_CASE("Relocation type enums", "[linker][relocation]") {
    SECTION("x86_64 relocation types") {
        REQUIRE(static_cast<int>(RelocationType_x86_64::kR_X86_64_64) == 1);
        REQUIRE(static_cast<int>(RelocationType_x86_64::kR_X86_64_PC32) == 2);
        REQUIRE(static_cast<int>(RelocationType_x86_64::kR_X86_64_PLT32) == 4);
        REQUIRE(static_cast<int>(RelocationType_x86_64::kR_X86_64_COPY) == 5);
    }
    
    SECTION("ARM64 relocation types") {
        REQUIRE(static_cast<int>(RelocationType_ARM64::kR_AARCH64_ABS64) == 257);
        REQUIRE(static_cast<int>(RelocationType_ARM64::kR_AARCH64_CALL26) == 283);
        REQUIRE(static_cast<int>(RelocationType_ARM64::kR_AARCH64_JUMP26) == 282);
    }
    
    SECTION("Mach-O relocation types") {
        REQUIRE(static_cast<int>(RelocationType_MachO::kX86_64_RELOC_UNSIGNED) == 0);
        REQUIRE(static_cast<int>(RelocationType_MachO::kX86_64_RELOC_SIGNED) == 1);
        REQUIRE(static_cast<int>(RelocationType_MachO::kX86_64_RELOC_BRANCH) == 2);
    }
}
