#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_sema.h"
#include "tools/polyld/include/linker.h"

namespace polyglot::linker {

// ============================================================================
// Cross-Language Symbol Descriptor
// ============================================================================

// Describes a symbol from a specific language module
struct CrossLangSymbol {
    std::string name;               // Original unmangled name
    std::string mangled_name;       // Language-specific mangled name
    std::string language;           // Source language (cpp, python, rust)
    std::string module_name;        // Module/translation unit
    SymbolType type{SymbolType::kFunction};
    // For functions: parameter and return type descriptors
    struct ParamDesc {
        std::string type_name;
        size_t size{0};
        bool is_pointer{false};
    };
    std::vector<ParamDesc> params;
    ParamDesc return_desc;
};

// ============================================================================
// Glue Code Stub
// ============================================================================

// A generated wrapper function that bridges two language calling conventions
struct GlueStub {
    std::string stub_name;          // Generated symbol name
    std::string target_language;
    std::string source_language;
    std::string target_function;
    std::string source_function;
    // Machine code or IR for the stub
    std::vector<std::uint8_t> code;
    // Relocations needed by the stub
    std::vector<Relocation> relocations;
};

// ============================================================================
// Cross-Language Link Resolver
// ============================================================================

class PolyglotLinker {
  public:
    explicit PolyglotLinker(const LinkerConfig &config);
    ~PolyglotLinker() = default;

    // Register a cross-language call descriptor from the .ploy frontend lowering
    void AddCallDescriptor(const ploy::CrossLangCallDescriptor &desc);

    // Register a validated link entry from the .ploy sema
    void AddLinkEntry(const ploy::LinkEntry &entry);

    // Register symbols discovered from language-specific object files
    void AddCrossLangSymbol(const CrossLangSymbol &sym);

    // Resolve all cross-language links:
    // 1. Match target/source symbols across language boundaries
    // 2. Generate glue stubs with marshalling code
    // 3. Emit relocations for the main linker to process
    bool ResolveLinks();

    // Access generated stubs
    const std::vector<GlueStub> &GetStubs() const { return stubs_; }

    // Access resolved symbol mappings
    const std::unordered_map<std::string, CrossLangSymbol> &GetResolvedSymbols() const {
        return resolved_symbols_;
    }

    // Diagnostics
    const std::vector<std::string> &GetErrors() const { return errors_; }
    const std::vector<std::string> &GetWarnings() const { return warnings_; }

    // Query whether any cross-language link entries have been registered
    bool HasLinkEntries() const { return !link_entries_.empty() || !call_descriptors_.empty(); }

  private:
    // Symbol resolution
    bool ResolveSymbolPair(const ploy::LinkEntry &entry);
    CrossLangSymbol *FindSymbolByName(const std::string &name, const std::string &language);

    // Glue code generation
    GlueStub GenerateGlueStub(const ploy::LinkEntry &entry,
                               const CrossLangSymbol &target_sym,
                               const CrossLangSymbol &source_sym);

    // Architecture-specific stub generation
    void GenerateX86_64Stub(GlueStub &stub, const ploy::LinkEntry &entry,
                            const CrossLangSymbol &target_sym,
                            const CrossLangSymbol &source_sym);
    void GenerateAArch64Stub(GlueStub &stub, const ploy::LinkEntry &entry,
                             const CrossLangSymbol &target_sym,
                             const CrossLangSymbol &source_sym);

    // Marshalling code generation for different type pairs
    void EmitIntToFloatMarshal(std::vector<std::uint8_t> &code, size_t param_idx);
    void EmitFloatToIntMarshal(std::vector<std::uint8_t> &code, size_t param_idx);
    void EmitStringMarshal(std::vector<std::uint8_t> &code, const std::string &from_lang,
                           const std::string &to_lang);
    void EmitDirectCopy(std::vector<std::uint8_t> &code, size_t size);
    void EmitCallingConventionAdaptor(std::vector<std::uint8_t> &code,
                                      const std::string &from_lang,
                                      const std::string &to_lang);

    // Container type marshalling for complex parameter types
    void EmitListMarshal(std::vector<std::uint8_t> &code, const std::string &from_lang,
                         const std::string &to_lang, size_t param_idx);
    void EmitTupleMarshal(std::vector<std::uint8_t> &code, const std::string &from_lang,
                          const std::string &to_lang, size_t param_idx);
    void EmitDictMarshal(std::vector<std::uint8_t> &code, const std::string &from_lang,
                         const std::string &to_lang, size_t param_idx);
    void EmitStructMarshal(std::vector<std::uint8_t> &code, const std::string &from_lang,
                           const std::string &to_lang, size_t param_idx);

    // Detect whether a type name refers to a container type
    static bool IsContainerType(const std::string &type_name);
    static bool IsListType(const std::string &type_name);
    static bool IsDictType(const std::string &type_name);
    static bool IsTupleType(const std::string &type_name);
    static bool IsStructType(const std::string &type_name);

    // Helper: determine the calling convention for a language
    static std::string GetCallingConvention(const std::string &language);

    // Error reporting
    void ReportError(const std::string &msg);
    void ReportWarning(const std::string &msg);

    LinkerConfig config_;
    std::vector<ploy::CrossLangCallDescriptor> call_descriptors_;
    std::vector<ploy::LinkEntry> link_entries_;
    std::vector<CrossLangSymbol> cross_lang_symbols_;
    std::unordered_map<std::string, CrossLangSymbol> resolved_symbols_;
    std::vector<GlueStub> stubs_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;

    // Temporary storage for relocations emitted by marshalling helpers.
    // Flushed into the active GlueStub after stub code generation completes.
    std::vector<Relocation> pending_marshal_relocs_;
};

} // namespace polyglot::linker
