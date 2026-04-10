#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace polyglot::linker {

// ============================================================================
// Object File Format Detection
// ============================================================================

// Supported object file formats
enum class ObjectFormat {
    kUnknown,
    kELF,           // ELF (Linux, BSD, etc.)
    kMachO,         // Mach-O (macOS, iOS)
    kCOFF,          // COFF/PE (Windows)
    kPOBJ,          // POBJ (PolyglotCompiler portable object)
    kArchive,       // Static library archive (.a, .lib)
    kLLVMBitcode    // LLVM bitcode for LTO
};

// Detect object file format from file header
ObjectFormat DetectObjectFormat(const std::vector<std::uint8_t> &data);
ObjectFormat DetectObjectFormatFromPath(const std::string &path);

// ============================================================================
// Symbol Types and Binding
// ============================================================================

// Symbol binding (visibility)
enum class SymbolBinding {
    kLocal,         // Local symbol, not exported
    kGlobal,        // Global symbol, exported
    kWeak,          // Weak symbol, can be overridden
    kGNU_Unique     // GNU unique symbol (for vague linkage)
};

// Symbol type
enum class SymbolType {
    kNoType,        // Unspecified type
    kObject,        // Data object (variable)
    kFunction,      // Function entry point
    kSection,       // Section symbol
    kFile,          // Source file symbol
    kCommon,        // Common data (uninitialized)
    kTLS,           // Thread-local storage
    kIFunc          // Indirect function (GNU extension)
};

// Symbol visibility
enum class SymbolVisibility {
    kDefault,       // Normal visibility
    kInternal,      // Internal, same module only
    kHidden,        // Hidden, not exported from DSO
    kProtected      // Protected, cannot be preempted
};

// Symbol information
struct Symbol {
    std::string name;
    std::string section;
    std::uint64_t offset{0};
    std::uint64_t size{0};
    std::uint64_t value{0};           // Resolved virtual address
    SymbolBinding binding{SymbolBinding::kLocal};
    SymbolType type{SymbolType::kNoType};
    SymbolVisibility visibility{SymbolVisibility::kDefault};
    bool is_defined{false};
    bool is_common{false};
    bool is_absolute{false};          // Absolute address, no relocation
    int object_file_index{-1};        // Source object file index
    int section_index{-1};            // Section index in object file

    bool is_global() const { 
        return binding == SymbolBinding::kGlobal || binding == SymbolBinding::kWeak; 
    }
    bool is_weak() const { return binding == SymbolBinding::kWeak; }
};

// ============================================================================
// Relocation Types
// ============================================================================

// Relocation type (ELF x86_64)
enum class RelocationType_x86_64 {
    kNone = 0,
    kR_X86_64_64 = 1,           // Direct 64-bit
    kR_X86_64_PC32 = 2,         // PC-relative 32-bit signed
    kR_X86_64_GOT32 = 3,        // 32-bit GOT entry
    kR_X86_64_PLT32 = 4,        // 32-bit PLT offset
    kR_X86_64_COPY = 5,         // Copy symbol at runtime
    kR_X86_64_GLOB_DAT = 6,     // Create GOT entry
    kR_X86_64_JUMP_SLOT = 7,    // Create PLT entry
    kR_X86_64_RELATIVE = 8,     // Adjust by program base
    kR_X86_64_GOTPCREL = 9,     // 32-bit signed PC-relative to GOT
    kR_X86_64_32 = 10,          // Direct 32-bit zero-extend
    kR_X86_64_32S = 11,         // Direct 32-bit sign-extend
    kR_X86_64_16 = 12,          // Direct 16-bit
    kR_X86_64_PC16 = 13,        // 16-bit PC-relative
    kR_X86_64_8 = 14,           // Direct 8-bit
    kR_X86_64_PC8 = 15,         // 8-bit PC-relative
    kR_X86_64_PC64 = 24,        // PC-relative 64-bit
    kR_X86_64_GOTOFF64 = 25,    // 64-bit offset to GOT
    kR_X86_64_GOTPC32 = 26,     // 32-bit signed PC-relative to GOT
    kR_X86_64_SIZE32 = 32,      // Size of symbol plus addend
    kR_X86_64_SIZE64 = 33       // Size of symbol plus addend
};

// Relocation type (ELF AArch64)
enum class RelocationType_ARM64 {
    kNone = 0,
    kR_AARCH64_ABS64 = 257,     // Direct 64-bit
    kR_AARCH64_ABS32 = 258,     // Direct 32-bit
    kR_AARCH64_ABS16 = 259,     // Direct 16-bit
    kR_AARCH64_PREL64 = 260,    // PC-relative 64-bit
    kR_AARCH64_PREL32 = 261,    // PC-relative 32-bit
    kR_AARCH64_PREL16 = 262,    // PC-relative 16-bit
    kR_AARCH64_CALL26 = 283,    // Function call within 128MB
    kR_AARCH64_JUMP26 = 282,    // Unconditional branch within 128MB
    kR_AARCH64_ADR_PREL_PG_HI21 = 275,  // Page-relative ADRP
    kR_AARCH64_ADD_ABS_LO12_NC = 277,   // Low 12 bits for ADD
    kR_AARCH64_LDST64_ABS_LO12_NC = 286 // Low 12 bits for LDR/STR 64-bit
};

// Relocation type (Mach-O)
enum class RelocationType_MachO {
    kX86_64_RELOC_UNSIGNED = 0,     // Absolute address
    kX86_64_RELOC_SIGNED = 1,       // Signed PC-relative
    kX86_64_RELOC_BRANCH = 2,       // Branch target
    kX86_64_RELOC_GOT_LOAD = 3,     // GOT load
    kX86_64_RELOC_GOT = 4,          // GOT entry
    kX86_64_RELOC_SUBTRACTOR = 5,   // Subtractor relocation
    kX86_64_RELOC_SIGNED_1 = 6,     // Signed + 1 displacement
    kX86_64_RELOC_SIGNED_2 = 7,     // Signed + 2 displacement
    kX86_64_RELOC_SIGNED_4 = 8,     // Signed + 4 displacement
    kX86_64_RELOC_TLV = 9,          // Thread-local variable
    kARM64_RELOC_UNSIGNED = 0,      // Absolute address
    kARM64_RELOC_SUBTRACTOR = 1,    // Subtractor
    kARM64_RELOC_BRANCH26 = 2,      // 26-bit branch
    kARM64_RELOC_PAGE21 = 3,        // Page-relative
    kARM64_RELOC_PAGEOFF12 = 4,     // Page offset
    kARM64_RELOC_GOT_LOAD_PAGE21 = 5,
    kARM64_RELOC_GOT_LOAD_PAGEOFF12 = 6,
    kARM64_RELOC_POINTER_TO_GOT = 7,
    kARM64_RELOC_TLVP_LOAD_PAGE21 = 8,
    kARM64_RELOC_TLVP_LOAD_PAGEOFF12 = 9,
    kARM64_RELOC_ADDEND = 10
};

// Unified relocation entry
struct Relocation {
    std::uint64_t offset{0};        // Offset within section to apply relocation
    std::string symbol;              // Target symbol name
    std::uint32_t type{0};           // Architecture-specific relocation type
    std::int64_t addend{0};          // Addend value for RELA relocations
    std::string section;             // Source section containing this relocation
    bool is_pc_relative{false};      // PC-relative relocation
    int size{0};                     // Relocation size in bytes (1, 2, 4, 8)
    int symbol_index{-1};            // Index into symbol table

    // For Mach-O scattered relocations
    bool is_scattered{false};
    std::uint32_t scattered_value{0};
};

// ============================================================================
// Section Types and Flags
// ============================================================================

// Section type
enum class SectionType {
    kNull,          // Inactive section
    kProgbits,      // Program-defined data
    kSymtab,        // Symbol table
    kStrtab,        // String table
    kRela,          // Relocation entries with addends
    kRel,           // Relocation entries without addends
    kHash,          // Symbol hash table
    kDynamic,       // Dynamic linking info
    kNote,          // Note section
    kNobits,        // No data in file (BSS)
    kDynsym,        // Dynamic symbol table
    kInitArray,     // Array of constructors
    kFiniArray,     // Array of destructors
    kGNUHash,       // GNU-style hash table
    kGNUVerneed,    // Version requirements
    kGNUVersym      // Symbol versions
};

// Section flags (bitfield)
enum class SectionFlags : std::uint64_t {
    kNone = 0,
    kWrite = 1 << 0,        // Writable data
    kAlloc = 1 << 1,        // Occupies memory at runtime
    kExecInstr = 1 << 2,    // Executable instructions
    kMerge = 1 << 4,        // Can merge identical data
    kStrings = 1 << 5,      // Contains null-terminated strings
    kInfoLink = 1 << 6,     // sh_info holds section index
    kLinkOrder = 1 << 7,    // Preserve link order
    kOsNonConforming = 1 << 8,  // OS-specific handling
    kGroup = 1 << 9,        // COMDAT group
    kTLS = 1 << 10,         // Thread-local storage
    kCompressed = 1 << 11   // Compressed data
};

inline SectionFlags operator|(SectionFlags a, SectionFlags b) {
    return static_cast<SectionFlags>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}

inline SectionFlags operator&(SectionFlags a, SectionFlags b) {
    return static_cast<SectionFlags>(static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}

// Section from object file
struct InputSection {
    std::string name;
    std::vector<std::uint8_t> data;
    std::uint64_t alignment{1};
    SectionFlags flags{SectionFlags::kNone};
    SectionType type{SectionType::kProgbits};
    int object_file_index{-1};
    int section_index{-1};
    std::uint64_t original_addr{0};      // Original address in object file
    std::uint64_t assigned_addr{0};      // Assigned address in output
    std::string segment;                  // Mach-O segment name
    std::vector<Relocation> relocations;  // Relocations for this section
};

// ============================================================================
// Archive (Static Library) Support
// ============================================================================

// Archive member (object file within .a)
struct ArchiveMember {
    std::string name;                     // Member filename
    std::uint64_t offset{0};              // Offset in archive
    std::uint64_t size{0};                // Size of member
    std::vector<std::uint8_t> data;       // Member data
};

// Archive symbol table entry
struct ArchiveSymbol {
    std::string name;                     // Symbol name
    std::uint64_t member_offset{0};       // Offset of containing member
};

// Static library archive
struct Archive {
    std::string path;
    std::vector<ArchiveMember> members;
    std::vector<ArchiveSymbol> symbols;   // Symbol index
    bool is_thin{false};                  // GNU thin archive
};

// ============================================================================
// Object File Representation
// ============================================================================

// Object file representation
struct ObjectFile {
    std::string path;
    ObjectFormat format{ObjectFormat::kUnknown};
    std::vector<Symbol> symbols;
    std::vector<InputSection> sections;
    std::vector<Relocation> relocations;
    
    // ELF-specific
    std::uint16_t machine{0};             // EM_X86_64, EM_AARCH64, etc.
    std::uint8_t elf_class{0};            // 1=32-bit, 2=64-bit
    std::uint8_t endian{0};               // 1=little, 2=big
    
    // Mach-O-specific
    std::uint32_t cpu_type{0};
    std::uint32_t cpu_subtype{0};
    std::uint32_t filetype{0};
    
    // Common data
    bool is_64bit{true};
    bool is_little_endian{true};
    std::string source_file;              // Original source filename
    
    // Helper to find section by name
    InputSection* FindSection(const std::string &name);
    const InputSection* FindSection(const std::string &name) const;
    
    // Helper to find symbol by name
    Symbol* FindSymbol(const std::string &name);
    const Symbol* FindSymbol(const std::string &name) const;
};

// ============================================================================
// Output Sections and Segments
// ============================================================================

// Output segment (for executables/shared libraries)
struct OutputSegment {
    std::string name;                     // Segment name (__TEXT, __DATA, etc.)
    std::uint64_t virtual_address{0};
    std::uint64_t virtual_size{0};
    std::uint64_t file_offset{0};
    std::uint64_t file_size{0};
    std::uint32_t protection{0};          // Read/Write/Execute flags
    std::uint64_t alignment{4096};        // Page alignment
    std::vector<std::string> section_names;  // Sections in this segment
};

// Output section (merged from input sections)
struct OutputSection {
    std::string name;
    std::string segment;                  // Parent segment name
    std::uint64_t virtual_address{0};
    std::uint64_t file_offset{0};
    std::vector<std::uint8_t> data;
    SectionFlags flags{SectionFlags::kNone};
    SectionType type{SectionType::kProgbits};
    std::uint64_t alignment{16};
    std::uint64_t entry_size{0};          // For sections with fixed-size entries
    
    // Section contributions from input files
    struct Contribution {
        int object_index{-1};
        int section_index{-1};
        std::uint64_t input_offset{0};
        std::uint64_t output_offset{0};
        std::uint64_t size{0};
    };
    std::vector<Contribution> contributions;
};

// ============================================================================
// GOT/PLT Support for Dynamic Linking
// ============================================================================

// Global Offset Table entry
struct GOTEntry {
    std::string symbol;
    std::uint64_t address{0};
    int index{-1};
    bool is_resolved{false};
};

// Procedure Linkage Table entry
struct PLTEntry {
    std::string symbol;
    std::uint64_t address{0};
    std::uint64_t got_offset{0};
    int index{-1};
};

// ============================================================================
// Linker Configuration
// ============================================================================

// Output format
enum class OutputFormat {
    kExecutable,        // Static executable
    kSharedLibrary,     // Dynamic shared object (.so, .dylib)
    kRelocatable,       // Relocatable object file (partial link)
    kStaticLibrary      // Static library archive
};

// Target architecture
enum class TargetArch {
    kX86_64,
    kAArch64,
    kX86,
    kARM,
    kRISCV64
};

// Linker configuration
struct LinkerConfig {
    // Input/output
    std::vector<std::string> input_files;
    std::string output_file{"a.out"};
    OutputFormat output_format{OutputFormat::kExecutable};
    TargetArch target_arch{TargetArch::kX86_64};
    
    // Memory layout
    std::uint64_t base_address{0x400000};     // ELF default base for x86_64
    std::uint64_t text_segment_addr{0};       // 0 = auto
    std::uint64_t data_segment_addr{0};       // 0 = auto
    std::uint64_t page_size{4096};
    
    // Symbol handling
    std::string entry_point{"_start"};
    std::vector<std::string> undefined_symbols;   // -u flag
    std::vector<std::string> exported_symbols;    // Symbols to export
    std::vector<std::string> exclude_libs;        // Libraries to exclude from export
    
    // Library search
    std::vector<std::string> library_paths;       // -L paths
    std::vector<std::string> libraries;           // -l libraries
    std::vector<std::string> rpath;               // Runtime library path
    std::string sysroot;                          // Sysroot path
    
    // Options
    bool strip_all{false};                        // Strip all symbols
    bool strip_debug{false};                      // Strip debug symbols only
    bool gc_sections{false};                      // Garbage collect unused sections
    bool as_needed{false};                        // Only link needed libraries
    bool no_undefined{false};                     // Error on undefined symbols
    bool allow_multiple_definition{false};        // Allow multiple definitions
    bool pie{false};                              // Position-independent executable
    bool static_link{true};                       // Static linking (no dynamic)
    bool emit_relocs{false};                      // Emit relocations in output
    bool build_id{false};                         // Generate build ID
    bool hash_style_gnu{true};                    // Use GNU hash style
    bool warn_common{false};                      // Warn on common symbols
    bool trace{false};                            // Trace file loading
    bool verbose{false};                          // Verbose output
    
    // Debug info
    bool keep_debug{true};                        // Keep debug sections
    std::string separate_debug;                   // Separate debug file path
    
    // Optimization
    bool optimize{false};                         // Enable linker optimizations
    bool icf{false};                              // Identical code folding
    bool merge_strings{true};                     // Merge identical strings
    int threads{1};                               // Parallel linking threads
    
    // Version script
    std::string version_script;                   // Symbol version script
    
    // Linker script
    std::string linker_script;                    // Custom linker script

    // Cross-language linking
    std::vector<std::string> ploy_descriptor_files;  // --ploy-desc files (aux descriptors)
    std::string aux_dir;                              // --aux-dir path for auto-discovery
    bool allow_adhoc_link{false};                     // --allow-adhoc-link: permit ad-hoc stubs
};

// ============================================================================
// Linker Statistics
// ============================================================================

struct LinkerStats {
    int objects_loaded{0};
    int archives_loaded{0};
    int symbols_defined{0};
    int symbols_undefined{0};
    int symbols_weak{0};
    int relocations_processed{0};
    int sections_merged{0};
    int sections_discarded{0};
    std::uint64_t code_size{0};
    std::uint64_t data_size{0};
    std::uint64_t bss_size{0};
    std::uint64_t total_output_size{0};
    double link_time_ms{0};
};

// ============================================================================
// Main Linker Class
// ============================================================================

class Linker {
public:
    explicit Linker(const LinkerConfig &config);
    ~Linker() = default;
    
    // Main linking pipeline
    bool Link();
    
    // Individual stages (can be called separately)
    bool LoadObjectFiles();
    bool LoadArchives();
    bool ResolveSymbols();
    bool LayoutSections();
    bool ApplyRelocations();
    bool GenerateOutput();
    
    // Symbol table operations
    bool AddSymbol(const Symbol &symbol);
    Symbol* LookupSymbol(const std::string &name);
    const Symbol* LookupSymbol(const std::string &name) const;
    std::vector<const Symbol*> GetUndefinedSymbols() const;
    std::vector<const Symbol*> GetExportedSymbols() const;
    
    // Section operations
    OutputSection* GetOutputSection(const std::string &name);
    void CreateOutputSection(const std::string &name, SectionFlags flags);
    
    // Diagnostics
    const std::vector<std::string> &GetErrors() const { return errors_; }
    const std::vector<std::string> &GetWarnings() const { return warnings_; }
    const LinkerStats &GetStats() const { return stats_; }
    
    // Access to internals (for testing)
    const std::vector<ObjectFile> &GetObjects() const { return objects_; }
    const std::vector<OutputSection> &GetOutputSections() const { return output_sections_; }
    const std::unordered_map<std::string, Symbol> &GetSymbolTable() const { return symbol_table_; }

private:
    LinkerConfig config_;
    std::vector<ObjectFile> objects_;
    std::vector<Archive> archives_;
    std::unordered_map<std::string, Symbol> symbol_table_;
    std::vector<OutputSection> output_sections_;
    std::vector<OutputSegment> segments_;
    
    // GOT/PLT for dynamic linking
    std::vector<GOTEntry> got_entries_;
    std::vector<PLTEntry> plt_entries_;
    
    // Diagnostics
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    LinkerStats stats_;
    
    // Object file loading
    bool LoadELF(const std::string &path, ObjectFile &obj);
    bool LoadMachO(const std::string &path, ObjectFile &obj);
    bool LoadCOFF(const std::string &path, ObjectFile &obj);
    bool LoadPOBJ(const std::string &path, ObjectFile &obj);
    bool LoadArchive(const std::string &path, Archive &archive);
    bool LoadArchiveMember(const Archive &archive, const ArchiveMember &member);
    
    // ELF-specific parsing
    bool ParseELFSections(std::ifstream &file, ObjectFile &obj);
    bool ParseELFSymbols(std::ifstream &file, ObjectFile &obj);
    bool ParseELFRelocations(std::ifstream &file, ObjectFile &obj);
    
    // Mach-O-specific parsing
    bool ParseMachOLoadCommands(std::ifstream &file, ObjectFile &obj);
    bool ParseMachOSegments(std::ifstream &file, ObjectFile &obj, 
                            std::uint64_t offset, std::uint32_t size);
    bool ParseMachOSymtab(std::ifstream &file, ObjectFile &obj,
                          std::uint32_t symoff, std::uint32_t nsyms,
                          std::uint32_t stroff, std::uint32_t strsize);
    bool ParseMachORelocations(std::ifstream &file, ObjectFile &obj, InputSection &section,
                               std::uint32_t reloff, std::uint32_t nreloc);
    
    // Archive parsing
    bool ParseArchiveSymbolTable(std::ifstream &file, Archive &archive);
    bool ParseArchiveMembers(std::ifstream &file, Archive &archive);
    
    // Symbol resolution
    bool ResolveSymbolReferences();
    bool ResolveFromArchives();
    bool CheckUndefinedSymbols();
    void MergeCommonSymbols();
    
    // Section layout
    void CreateStandardSections();
    void MergeInputSections();
    void AssignSectionAddresses();
    void CreateSegments();
    void AssignSymbolAddresses();
    
    // Relocation processing
    bool ApplyRelocation(const Relocation &reloc, OutputSection &section,
                         std::uint64_t section_base);
    std::int64_t CalculateRelocationValue(const Relocation &reloc, 
                                          std::uint64_t reloc_addr);
    bool ApplyELFRelocation_x86_64(const Relocation &reloc, 
                                   std::vector<std::uint8_t> &data,
                                   std::uint64_t offset, std::int64_t value);
    bool ApplyELFRelocation_ARM64(const Relocation &reloc,
                                  std::vector<std::uint8_t> &data,
                                  std::uint64_t offset, std::int64_t value);
    bool ApplyMachORelocation(const Relocation &reloc,
                              std::vector<std::uint8_t> &data,
                              std::uint64_t offset, std::int64_t value);
    
    // Output generation
    bool GenerateELFExecutable();
    bool GenerateELFSharedLibrary();
    bool GenerateMachOExecutable();
    bool GenerateMachODylib();
    bool GenerateRelocatable();
    bool GenerateStaticLibrary();
    
    // GOT/PLT generation
    void CreateGOT();
    void CreatePLT();
    void AddGOTEntry(const std::string &symbol);
    void AddPLTEntry(const std::string &symbol);
    
    // Utility functions
    std::uint64_t AlignTo(std::uint64_t value, std::uint64_t align);
    std::string ResolveLibraryPath(const std::string &name);
    void ReportError(const std::string &msg);
    void ReportWarning(const std::string &msg);
    void Trace(const std::string &msg);
    
    // File I/O helpers
    template<typename T>
    bool ReadValue(std::ifstream &file, T &value);
    template<typename T>
    void WriteValue(std::vector<std::uint8_t> &out, T value);
    void WriteBytes(std::vector<std::uint8_t> &out, const void *data, std::size_t size);
    void WritePadding(std::vector<std::uint8_t> &out, std::size_t count);
};

// ============================================================================
// Linker Script Support
// ============================================================================

// Linker script expression value
using ScriptValue = std::variant<std::uint64_t, std::string>;

// Linker script command
struct ScriptCommand {
    enum class Type {
        kEntry,             // ENTRY(symbol)
        kSections,          // SECTIONS { ... }
        kMemory,            // MEMORY { ... }
        kAssert,            // ASSERT(condition, message)
        kProvide,           // PROVIDE(symbol = expr)
        kHidden,            // HIDDEN(symbol = expr)
        kInclude            // INCLUDE filename
    };
    Type type;
    std::string name;
    std::vector<ScriptCommand> children;
    ScriptValue value;
};

// Linker script section rule
struct ScriptSectionRule {
    std::string output_section;           // Output section name
    std::uint64_t address{0};             // Fixed address (0 = auto)
    std::vector<std::string> input_patterns;  // Input section patterns
    std::string fill_pattern;             // Fill pattern for gaps
    std::uint64_t alignment{0};           // Section alignment
};

// Simple linker script parser
class LinkerScriptParser {
public:
    explicit LinkerScriptParser(const std::string &script);
    
    bool Parse();
    const std::vector<ScriptCommand> &GetCommands() const { return commands_; }
    const std::vector<ScriptSectionRule> &GetSectionRules() const { return section_rules_; }
    std::string GetEntryPoint() const { return entry_point_; }
    const std::string &GetError() const { return error_; }

private:
    std::string script_;
    std::size_t pos_{0};
    std::vector<ScriptCommand> commands_;
    std::vector<ScriptSectionRule> section_rules_;
    std::string entry_point_;
    std::string error_;
    
    bool ParseCommand();
    bool ParseSections();
    bool ParseEntry();
    ScriptValue ParseExpression();
    std::string ParseIdentifier();
    bool SkipWhitespace();
    bool Match(const std::string &s);
    bool Expect(const std::string &s);
};

// ============================================================================
// Utility Functions
// ============================================================================

// Demangle symbol name (C++, Rust)
std::string DemangleSymbol(const std::string &name);

// Check if symbol name matches pattern (with wildcards)
bool SymbolMatchesPattern(const std::string &name, const std::string &pattern);

// Get section type from name
SectionType GetSectionTypeFromName(const std::string &name);

// Get default section flags from name
SectionFlags GetDefaultSectionFlags(const std::string &name);

} // namespace polyglot::linker
