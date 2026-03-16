#include "tools/polyld/include/polyglot_linker.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace polyglot::linker {

// ============================================================================
// Container type detection helpers
// ============================================================================

bool PolyglotLinker::IsListType(const std::string &type_name) {
    return type_name.find("list") == 0 || type_name.find("List") == 0 ||
           type_name.find("vector") == 0 || type_name.find("Vec") == 0 ||
           type_name.find("std::vector") == 0;
}

bool PolyglotLinker::IsDictType(const std::string &type_name) {
    return type_name.find("dict") == 0 || type_name.find("Dict") == 0 ||
           type_name.find("map") == 0 || type_name.find("Map") == 0 ||
           type_name.find("HashMap") == 0 || type_name.find("std::map") == 0 ||
           type_name.find("std::unordered_map") == 0;
}

bool PolyglotLinker::IsTupleType(const std::string &type_name) {
    return type_name.find("tuple") == 0 || type_name.find("Tuple") == 0 ||
           type_name.find("std::tuple") == 0;
}

bool PolyglotLinker::IsStructType(const std::string &type_name) {
    return type_name.find("struct") == 0 || type_name.find("Struct") == 0;
}

bool PolyglotLinker::IsContainerType(const std::string &type_name) {
    return IsListType(type_name) || IsDictType(type_name) ||
           IsTupleType(type_name) || IsStructType(type_name);
}

// ============================================================================
// Constructor
// ============================================================================

PolyglotLinker::PolyglotLinker(const LinkerConfig &config)
    : config_(config) {}

// ============================================================================
// Registration
// ============================================================================

void PolyglotLinker::AddCallDescriptor(const ploy::CrossLangCallDescriptor &desc) {
    call_descriptors_.push_back(desc);
}

void PolyglotLinker::AddLinkEntry(const ploy::LinkEntry &entry) {
    link_entries_.push_back(entry);
}

void PolyglotLinker::AddCrossLangSymbol(const CrossLangSymbol &sym) {
    cross_lang_symbols_.push_back(sym);
}

// ============================================================================
// Descriptor File Loading
// ============================================================================

bool PolyglotLinker::LoadDescriptorFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        ReportError("cannot open descriptor file: " + path);
        return false;
    }

    // Parse line-based descriptor format:
    //   LINK <target_lang> <source_lang> <target_sym> <source_sym>
    //   CALL <stub_name> <source_lang> <target_lang> <source_func> <target_func>
    //   SYMBOL <name> <language> <mangled_name>
    std::string line;
    int line_num = 0;
    while (std::getline(ifs, line)) {
        ++line_num;
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string kind;
        iss >> kind;

        if (kind == "LINK") {
            ploy::LinkEntry entry;
            std::string target_lang, source_lang, target_sym, source_sym;
            if (!(iss >> target_lang >> source_lang >> target_sym >> source_sym)) {
                ReportWarning("malformed LINK at " + path + ":" + std::to_string(line_num));
                continue;
            }
            entry.kind = ploy::LinkDecl::LinkKind::kFunction;
            entry.target_language = target_lang;
            entry.source_language = source_lang;
            entry.target_symbol = target_sym;
            entry.source_symbol = source_sym;
            link_entries_.push_back(entry);
        } else if (kind == "CALL") {
            ploy::CrossLangCallDescriptor desc;
            std::string stub, src_lang, tgt_lang, src_func, tgt_func;
            if (!(iss >> stub >> src_lang >> tgt_lang >> src_func >> tgt_func)) {
                ReportWarning("malformed CALL at " + path + ":" + std::to_string(line_num));
                continue;
            }
            desc.stub_name = stub;
            desc.source_language = src_lang;
            desc.target_language = tgt_lang;
            desc.source_function = src_func;
            desc.target_function = tgt_func;
            call_descriptors_.push_back(desc);
        } else if (kind == "SYMBOL") {
            CrossLangSymbol sym;
            std::string name, lang, mangled;
            if (!(iss >> name >> lang >> mangled)) {
                ReportWarning("malformed SYMBOL at " + path + ":" + std::to_string(line_num));
                continue;
            }
            sym.name = name;
            sym.language = lang;
            sym.mangled_name = mangled;
            cross_lang_symbols_.push_back(sym);
        }
    }
    return true;
}

void PolyglotLinker::DiscoverDescriptors(const std::string &aux_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(aux_dir, ec)) return;

    for (const auto &entry : fs::directory_iterator(aux_dir, ec)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".paux" &&
            entry.path().stem().string().find("descriptors") != std::string::npos) {
            LoadDescriptorFile(entry.path().string());
        }
    }
}

// ============================================================================
// Link Resolution
// ============================================================================

bool PolyglotLinker::ResolveLinks() {
    bool success = true;

    for (const auto &entry : link_entries_) {
        if (!ResolveSymbolPair(entry)) {
            success = false;
        }
    }

    // Also generate stubs for standalone CALL descriptors that don't have
    // a corresponding LINK entry (ad-hoc cross-language calls in PIPELINE or FUNC)
    for (const auto &desc : call_descriptors_) {
        // Check if already covered by a LINK entry
        bool covered = false;
        for (const auto &entry : link_entries_) {
            if (desc.source_function == entry.source_symbol ||
                desc.target_function == entry.target_symbol) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            // In strict mode (default), ad-hoc calls without a LINK declaration
            // are errors.  Use --allow-adhoc-link to permit them.
            if (!config_.allow_adhoc_link) {
                ReportError("CALL to '" + desc.source_function +
                            "' (language: " + desc.source_language +
                            ") has no corresponding LINK declaration; "
                            "use --allow-adhoc-link to permit ad-hoc stubs");
                success = false;
                continue;
            }

            // Generate a minimal glue stub for this ad-hoc call
            CrossLangSymbol source_sym;
            source_sym.name = desc.source_function;
            source_sym.mangled_name = desc.source_function;
            source_sym.language = desc.source_language;

            CrossLangSymbol target_sym;
            target_sym.name = desc.target_function;
            target_sym.mangled_name = desc.stub_name;
            target_sym.language = desc.target_language;

            ploy::LinkEntry synth_entry;
            synth_entry.kind = ploy::LinkDecl::LinkKind::kFunction;
            synth_entry.target_language = desc.target_language;
            synth_entry.source_language = desc.source_language;
            synth_entry.target_symbol = desc.target_function;
            synth_entry.source_symbol = desc.source_function;

            GlueStub stub = GenerateGlueStub(synth_entry, target_sym, source_sym);
            stubs_.push_back(stub);

            resolved_symbols_[desc.stub_name] = target_sym;

            ReportWarning("generated ad-hoc stub for '" + desc.source_function +
                          "' without explicit LINK declaration");
        }
    }

    return success;
}

bool PolyglotLinker::ResolveSymbolPair(const ploy::LinkEntry &entry) {
    // Find source symbol in the cross-language symbol table
    CrossLangSymbol *source_sym = FindSymbolByName(entry.source_symbol, entry.source_language);
    CrossLangSymbol *target_sym = FindSymbolByName(entry.target_symbol, entry.target_language);

    // If symbols are not found, report hard errors and abort this entry.
    // No placeholder symbols are created — unresolved references are treated
    // as fatal link failures to prevent silent miscompilation.
    if (!source_sym) {
        ReportError("unresolved source symbol '" + entry.source_symbol +
                    "' (language: " + entry.source_language + ")");
    }

    if (!target_sym) {
        ReportError("unresolved target symbol '" + entry.target_symbol +
                    "' (language: " + entry.target_language + ")");
    }

    if (!source_sym || !target_sym) {
        // Hard failure: do not generate a glue stub for incomplete links.
        return false;
    }

    // Generate the glue stub
    GlueStub stub = GenerateGlueStub(entry, *target_sym, *source_sym);
    stubs_.push_back(stub);

    // Register the resolved symbols
    resolved_symbols_[stub.stub_name] = *target_sym;

    return true;
}

CrossLangSymbol *PolyglotLinker::FindSymbolByName(const std::string &name,
                                                   const std::string &language) {
    for (auto &sym : cross_lang_symbols_) {
        if ((sym.name == name || sym.mangled_name == name) && sym.language == language) {
            return &sym;
        }
    }
    return nullptr;
}

// ============================================================================
// Glue Stub Generation
// ============================================================================

GlueStub PolyglotLinker::GenerateGlueStub(const ploy::LinkEntry &entry,
                                            const CrossLangSymbol &target_sym,
                                            const CrossLangSymbol &source_sym) {
    GlueStub stub;
    stub.target_language = entry.target_language;
    stub.source_language = entry.source_language;
    stub.target_function = entry.target_symbol;
    stub.source_function = entry.source_symbol;

    // Generate the stub name
    stub.stub_name = "__ploy_bridge_" + entry.target_language + "_" +
                     entry.source_language + "_" + entry.target_symbol;
    for (char &c : stub.stub_name) {
        if (c == ':') c = '_';
    }

    // Generate stub code based on target architecture
    // The generated code follows this pattern:
    // 1. Save callee-saved registers per target convention
    // 2. Marshal arguments from target convention to source convention
    // 3. Call the source function
    // 4. Marshal return value from source convention to target convention
    // 5. Restore registers and return

    if (config_.target_arch == TargetArch::kX86_64) {
        GenerateX86_64Stub(stub, entry, target_sym, source_sym);
    } else if (config_.target_arch == TargetArch::kAArch64) {
        GenerateAArch64Stub(stub, entry, target_sym, source_sym);
    }

    // Flush any relocations emitted by marshalling helpers into the stub
    for (auto &reloc : pending_marshal_relocs_) {
        stub.relocations.push_back(std::move(reloc));
    }
    pending_marshal_relocs_.clear();

    return stub;
}

// ============================================================================
// x86_64 Stub Generation
// ============================================================================

void PolyglotLinker::GenerateX86_64Stub(GlueStub &stub, const ploy::LinkEntry &entry,
                                         const CrossLangSymbol &target_sym,
                                         const CrossLangSymbol &source_sym) {
    auto &code = stub.code;
    (void)target_sym;
    (void)source_sym;

    // Function prologue: push rbp; mov rbp, rsp
    code.push_back(0x55);                   // push rbp
    code.push_back(0x48); code.push_back(0x89); code.push_back(0xE5); // mov rbp, rsp

    // Allocate stack space for marshalling (64 bytes)
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x40);
    // sub rsp, 0x40

    // Save argument registers (System V ABI: rdi, rsi, rdx, rcx, r8, r9)
    // For calling convention adaptation, we may need to rearrange registers
    std::string from_cc = GetCallingConvention(entry.target_language);
    std::string to_cc = GetCallingConvention(entry.source_language);

    if (from_cc != to_cc) {
        // Generate calling convention adaptation code
        EmitCallingConventionAdaptor(code, entry.target_language, entry.source_language);
    }

    // Apply type marshalling for each parameter
    for (size_t i = 0; i < entry.param_mappings.size(); ++i) {
        const auto &mapping = entry.param_mappings[i];
        if (!mapping.source_type.empty() && !mapping.target_type.empty()) {
            // Determine conversion type
            bool src_is_int = (mapping.source_type == "int" || mapping.source_type == "i64" ||
                              mapping.source_type == "i32");
            bool dst_is_float = (mapping.target_type == "float" || mapping.target_type == "double" ||
                                mapping.target_type == "f64");
            bool src_is_float = (mapping.source_type == "float" || mapping.source_type == "double" ||
                                mapping.source_type == "f64");
            bool dst_is_int = (mapping.target_type == "int" || mapping.target_type == "i64" ||
                              mapping.target_type == "i32");

            if (src_is_int && dst_is_float) {
                EmitIntToFloatMarshal(code, i);
            } else if (src_is_float && dst_is_int) {
                EmitFloatToIntMarshal(code, i);
            } else if (mapping.source_type == "str" || mapping.source_type == "std::string" ||
                       mapping.source_type == "String") {
                EmitStringMarshal(code, entry.source_language, entry.target_language);
            } else if (IsListType(mapping.source_type) || IsListType(mapping.target_type)) {
                EmitListMarshal(code, entry.source_language, entry.target_language, i);
            } else if (IsDictType(mapping.source_type) || IsDictType(mapping.target_type)) {
                EmitDictMarshal(code, entry.source_language, entry.target_language, i);
            } else if (IsTupleType(mapping.source_type) || IsTupleType(mapping.target_type)) {
                EmitTupleMarshal(code, entry.source_language, entry.target_language, i);
            } else if (IsStructType(mapping.source_type) || IsStructType(mapping.target_type)) {
                EmitStructMarshal(code, entry.source_language, entry.target_language, i);
            }
        }
    }

    // Call the source function
    // Generate a CALL instruction with a relocation to the source symbol
    code.push_back(0xE8); // call rel32
    // Placeholder for 32-bit PC-relative offset (will be filled by linker)
    size_t call_offset = code.size();
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x00);

    // Add relocation for the call
    Relocation reloc;
    reloc.offset = call_offset;
    reloc.symbol = source_sym.mangled_name.empty() ? source_sym.name : source_sym.mangled_name;
    reloc.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
    reloc.addend = -4;
    reloc.is_pc_relative = true;
    reloc.size = 4;
    stub.relocations.push_back(reloc);

    // Marshal return value if needed (currently assumes direct pass-through)

    // Function epilogue: mov rsp, rbp; pop rbp; ret
    code.push_back(0x48); code.push_back(0x89); code.push_back(0xEC); // mov rsp, rbp
    code.push_back(0x5D);                   // pop rbp
    code.push_back(0xC3);                   // ret
}

// ============================================================================
// AArch64 Stub Generation
// ============================================================================

void PolyglotLinker::GenerateAArch64Stub(GlueStub &stub, const ploy::LinkEntry &entry,
                                          const CrossLangSymbol &target_sym,
                                          const CrossLangSymbol &source_sym) {
    auto &code = stub.code;
    (void)target_sym;
    (void)entry;

    // AArch64 function prologue: stp x29, x30, [sp, #-16]!; mov x29, sp
    // stp x29, x30, [sp, #-16]!
    code.push_back(0xFD); code.push_back(0x7B); code.push_back(0xBF); code.push_back(0xA9);
    // mov x29, sp
    code.push_back(0xFD); code.push_back(0x03); code.push_back(0x00); code.push_back(0x91);

    // BL <source_function> — branch with link (call)
    size_t call_offset = code.size();
    code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x94);

    // Relocation for BL
    Relocation reloc;
    reloc.offset = call_offset;
    reloc.symbol = source_sym.mangled_name.empty() ? source_sym.name : source_sym.mangled_name;
    reloc.type = static_cast<std::uint32_t>(RelocationType_ARM64::kR_AARCH64_CALL26);
    reloc.addend = 0;
    reloc.is_pc_relative = true;
    reloc.size = 4;
    stub.relocations.push_back(reloc);

    // Function epilogue: ldp x29, x30, [sp], #16; ret
    code.push_back(0xFD); code.push_back(0x7B); code.push_back(0xC1); code.push_back(0xA8);
    code.push_back(0xC0); code.push_back(0x03); code.push_back(0x5F); code.push_back(0xD6);
}

// ============================================================================
// Marshalling Code Emission
// ============================================================================

void PolyglotLinker::EmitIntToFloatMarshal(std::vector<std::uint8_t> &code, size_t param_idx) {
    // x86_64: Convert integer in GPR to XMM register
    // cvtsi2sd xmmN, rdi/rsi/rdx/rcx/r8/r9 (depending on param_idx)
    // REX.W prefix for 64-bit integer
    static const std::uint8_t gpr_regs[] = {0xC7, 0xC6, 0xC2, 0xC1}; // rdi, rsi, rdx, rcx modrm
    if (param_idx >= 4) return;

    code.push_back(0xF2); // REPNE prefix for SD
    code.push_back(0x48); // REX.W
    code.push_back(0x0F);
    code.push_back(0x2A); // cvtsi2sd
    // ModR/M: xmm0+param_idx, gpr
    std::uint8_t modrm = static_cast<std::uint8_t>(0xC0 | (param_idx << 3) |
                                                    (gpr_regs[param_idx] & 0x07));
    code.push_back(modrm);
}

void PolyglotLinker::EmitFloatToIntMarshal(std::vector<std::uint8_t> &code, size_t param_idx) {
    // x86_64: Convert XMM register to integer in GPR
    // cvttsd2si rdi/rsi/rdx/rcx, xmmN
    if (param_idx >= 4) return;

    code.push_back(0xF2);
    code.push_back(0x48); // REX.W
    code.push_back(0x0F);
    code.push_back(0x2C); // cvttsd2si
    std::uint8_t modrm = static_cast<std::uint8_t>(0xC0 | (param_idx << 3) | param_idx);
    code.push_back(modrm);
}

void PolyglotLinker::EmitStringMarshal(std::vector<std::uint8_t> &code,
                                        const std::string &from_lang,
                                        const std::string &to_lang) {
    // String conversion requires a runtime call to __ploy_rt_string_convert.
    // The source pointer is already in the first argument register.
    // We emit a CALL with a relocation to be patched by the linker.
    (void)from_lang;
    (void)to_lang;

    code.push_back(0xE8); // call rel32
    size_t call_offset = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);

    // The relocation will be stored on the containing GlueStub.
    // We record it in a thread-local so GenerateX86_64Stub can pick it up.
    Relocation r;
    r.offset = call_offset;
    r.symbol = "__ploy_rt_string_convert";
    r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
    r.addend = -4;
    r.is_pc_relative = true;
    r.size = 4;
    pending_marshal_relocs_.push_back(r);
}

void PolyglotLinker::EmitDirectCopy(std::vector<std::uint8_t> &code, size_t size) {
    // Direct memory copy of 'size' bytes — no conversion needed.
    // For register-sized values this is a no-op (already in correct register).
    // For larger values, generate a runtime memcpy call.
    if (size <= 8) {
        // Value fits in a register — no marshalling required.
        return;
    }
    // Emit call to __ploy_rt_memcpy for larger structs
    code.push_back(0xE8); // call rel32
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "__ploy_rt_memcpy";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
}

void PolyglotLinker::EmitCallingConventionAdaptor(std::vector<std::uint8_t> &code,
                                                   const std::string &from_lang,
                                                   const std::string &to_lang) {
    std::string from_cc = GetCallingConvention(from_lang);
    std::string to_cc = GetCallingConvention(to_lang);

    if (from_cc == "sysv" && to_cc == "win64") {
        // System V → Windows x64: move rdi→rcx, rsi→rdx, rdx→r8, rcx→r9
        // mov rcx, rdi
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xF9);
        // mov rdx, rsi
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xF2);
        // mov r8, rdx (save original rdx first)
        code.push_back(0x49); code.push_back(0x89); code.push_back(0xD0);
        // mov r9, rcx
        code.push_back(0x49); code.push_back(0x89); code.push_back(0xC9);
    } else if (from_cc == "win64" && to_cc == "sysv") {
        // Windows x64 → System V: move rcx→rdi, rdx→rsi, r8→rdx, r9→rcx
        // mov rdi, rcx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xCF);
        // mov rsi, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xD6);
        // mov rdx, r8
        code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC2);
        // mov rcx, r9
        code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC9);
    }
    // Same convention — no adaptation needed
}

std::string PolyglotLinker::GetCallingConvention(const std::string &language) {
    // All supported languages use the platform's native C calling convention
    // On Linux/macOS: System V AMD64 ABI
    // On Windows: Microsoft x64 calling convention
    (void)language;
#ifdef _WIN32
    return "win64";
#else
    return "sysv";
#endif
}

void PolyglotLinker::ReportError(const std::string &msg) {
    errors_.push_back("polyglot-linker: error: " + msg);
}

void PolyglotLinker::ReportWarning(const std::string &msg) {
    warnings_.push_back("polyglot-linker: warning: " + msg);
}

// ============================================================================
// Container Type Marshalling Emission
// ============================================================================

void PolyglotLinker::EmitListMarshal(std::vector<std::uint8_t> &code,
                                      const std::string &from_lang,
                                      const std::string &to_lang,
                                      size_t param_idx) {
    (void)param_idx;

    // Generate a call to the appropriate runtime conversion function.
    // The argument register already holds a pointer to the source container.
    // We emit a CALL with a relocation to the conversion helper.

    std::string target_sym;
    if (from_lang == "rust" && (to_lang == "cpp" || to_lang == "python")) {
        target_sym = "__ploy_rt_convert_vec_to_list";
    } else if (from_lang == "python" && (to_lang == "cpp" || to_lang == "rust")) {
        target_sym = "__ploy_rt_convert_pylist_to_list";
    } else if (from_lang == "cpp" && (to_lang == "python" || to_lang == "rust")) {
        target_sym = "__ploy_rt_convert_cppvec_to_list";
    } else {
        target_sym = "__ploy_rt_convert_list_generic";
    }

    code.push_back(0xE8);
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = target_sym;
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
}

void PolyglotLinker::EmitTupleMarshal(std::vector<std::uint8_t> &code,
                                       const std::string &from_lang,
                                       const std::string &to_lang,
                                       size_t param_idx) {
    (void)from_lang;
    (void)to_lang;
    (void)param_idx;

    // Tuples are passed as pointers to a packed struct.
    // Emit a call to __ploy_rt_convert_tuple with a proper relocation.
    code.push_back(0xE8);
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "__ploy_rt_convert_tuple";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
}

void PolyglotLinker::EmitDictMarshal(std::vector<std::uint8_t> &code,
                                      const std::string &from_lang,
                                      const std::string &to_lang,
                                      size_t param_idx) {
    (void)from_lang;
    (void)to_lang;
    (void)param_idx;

    // Dict conversion is delegated to the runtime helper.
    // The source pointer is already in the argument register;
    // after the call the result pointer will be in rax.
    code.push_back(0xE8);
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "__ploy_rt_dict_convert";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
}

void PolyglotLinker::EmitStructMarshal(std::vector<std::uint8_t> &code,
                                        const std::string &from_lang,
                                        const std::string &to_lang,
                                        size_t param_idx) {
    (void)from_lang;
    (void)to_lang;
    (void)param_idx;

    // Struct marshalling copies fields between potentially different layouts.
    // The runtime helper __ploy_rt_convert_struct reads StructFieldDesc
    // metadata to perform field-by-field copying.
    code.push_back(0xE8);
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "__ploy_rt_convert_struct";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
}

} // namespace polyglot::linker
