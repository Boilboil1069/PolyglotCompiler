/**
 * @file     polyglot_linker.cpp
 * @brief    Polyglot linker implementation
 *
 * @ingroup  Tool / polyld
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include "tools/polyld/include/polyglot_linker.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

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

    // Set of known languages for validation
    static const std::unordered_set<std::string> known_languages = {
        "cpp", "c", "python", "rust", "java", "dotnet", "csharp", "ploy"
    };

    auto is_valid_language = [&](const std::string &lang) {
        return known_languages.count(lang) > 0;
    };

    // Track seen symbols and links for duplicate detection
    std::unordered_set<std::string> seen_link_keys;
    std::unordered_set<std::string> seen_symbol_keys;

    // Parse line-based descriptor format:
    //   LINK <target_lang> <source_lang> <target_sym> <source_sym>
    //   CALL <stub_name> <source_lang> <target_lang> <source_func> <target_func>
    //   SYMBOL <name> <language> <mangled_name>
    std::string line;
    int line_num = 0;
    bool has_errors = false;
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
                ReportError("malformed LINK at " + path + ":" + std::to_string(line_num) +
                            " — expected: LINK <target_lang> <source_lang> <target_sym> <source_sym>");
                has_errors = true;
                continue;
            }

            // Validate languages
            if (!is_valid_language(target_lang)) {
                ReportError("unknown target language '" + target_lang +
                            "' in LINK at " + path + ":" + std::to_string(line_num));
                has_errors = true;
            }
            if (!is_valid_language(source_lang)) {
                ReportError("unknown source language '" + source_lang +
                            "' in LINK at " + path + ":" + std::to_string(line_num));
                has_errors = true;
            }

            // Validate symbol names are non-empty
            if (target_sym.empty() || source_sym.empty()) {
                ReportError("empty symbol name in LINK at " + path + ":" +
                            std::to_string(line_num));
                has_errors = true;
                continue;
            }

            // Check for duplicate LINK entries
            std::string link_key = target_lang + "::" + target_sym + "<->" +
                                   source_lang + "::" + source_sym;
            if (!seen_link_keys.insert(link_key).second) {
                ReportWarning("duplicate LINK entry at " + path + ":" +
                              std::to_string(line_num) + " for " + link_key);
            }

            entry.kind = ploy::LinkDecl::LinkKind::kFunction;
            entry.target_language = target_lang;
            entry.source_language = source_lang;
            entry.target_symbol = target_sym;
            entry.source_symbol = source_sym;
            link_entries_.push_back(entry);
        } else if (kind == "MAP_TYPE") {
            // MAP_TYPE lines follow a LINK entry and attach parameter mappings
            // Format: MAP_TYPE <src_lang>::<src_type> <tgt_lang>::<tgt_type>
            if (link_entries_.empty()) {
                ReportWarning("MAP_TYPE without preceding LINK at " + path + ":" +
                              std::to_string(line_num));
                continue;
            }
            std::string src_spec, tgt_spec;
            if (!(iss >> src_spec >> tgt_spec)) {
                ReportWarning("malformed MAP_TYPE at " + path + ":" +
                              std::to_string(line_num));
                continue;
            }
            ploy::TypeMappingEntry mapping;
            auto src_sep = src_spec.find("::");
            if (src_sep != std::string::npos) {
                mapping.source_language = src_spec.substr(0, src_sep);
                mapping.source_type = src_spec.substr(src_sep + 2);
            } else {
                mapping.source_type = src_spec;
            }
            auto tgt_sep = tgt_spec.find("::");
            if (tgt_sep != std::string::npos) {
                mapping.target_language = tgt_spec.substr(0, tgt_sep);
                mapping.target_type = tgt_spec.substr(tgt_sep + 2);
            } else {
                mapping.target_type = tgt_spec;
            }
            link_entries_.back().param_mappings.push_back(mapping);
        } else if (kind == "CALL") {
            ploy::CrossLangCallDescriptor desc;
            std::string stub, src_lang, tgt_lang, src_func, tgt_func;
            if (!(iss >> stub >> src_lang >> tgt_lang >> src_func >> tgt_func)) {
                ReportError("malformed CALL at " + path + ":" + std::to_string(line_num) +
                            " — expected: CALL <stub> <source_lang> <target_lang> <source_func> <target_func>");
                has_errors = true;
                continue;
            }

            // Validate languages
            if (!is_valid_language(src_lang)) {
                ReportError("unknown source language '" + src_lang +
                            "' in CALL at " + path + ":" + std::to_string(line_num));
                has_errors = true;
            }
            if (!is_valid_language(tgt_lang)) {
                ReportError("unknown target language '" + tgt_lang +
                            "' in CALL at " + path + ":" + std::to_string(line_num));
                has_errors = true;
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
                ReportError("malformed SYMBOL at " + path + ":" + std::to_string(line_num) +
                            " — expected: SYMBOL <name> <language> <mangled_name>");
                has_errors = true;
                continue;
            }

            // Validate language
            if (!is_valid_language(lang)) {
                ReportError("unknown language '" + lang +
                            "' in SYMBOL at " + path + ":" + std::to_string(line_num));
                has_errors = true;
            }

            // Check for duplicate symbols (same name + language)
            std::string sym_key = lang + "::" + name;
            if (!seen_symbol_keys.insert(sym_key).second) {
                ReportWarning("duplicate SYMBOL entry at " + path + ":" +
                              std::to_string(line_num) + " for " + sym_key);
            }

            sym.name = name;
            sym.language = lang;
            sym.mangled_name = mangled;
            cross_lang_symbols_.push_back(sym);
        } else {
            ReportWarning("unknown descriptor kind '" + kind + "' at " +
                          path + ":" + std::to_string(line_num));
        }
    }
    return !has_errors;
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

    // Cross-module signature consistency check: when multiple call descriptors
    // reference the same source function, verify that their parameter counts
    // and types agree.  Mismatches indicate that different .ploy modules have
    // conflicting views of the same foreign function.
    {
        std::unordered_map<std::string, const ploy::CrossLangCallDescriptor *> seen_sigs;
        for (const auto &desc : call_descriptors_) {
            auto it = seen_sigs.find(desc.source_function);
            if (it == seen_sigs.end()) {
                seen_sigs[desc.source_function] = &desc;
                continue;
            }
            const auto &prev = *it->second;

            // Check parameter count consistency
            if (prev.source_param_types.size() != desc.source_param_types.size()) {
                ReportError("cross-module signature mismatch for '" +
                            desc.source_function + "': one module declares " +
                            std::to_string(prev.source_param_types.size()) +
                            " parameter(s) but another declares " +
                            std::to_string(desc.source_param_types.size()));
                success = false;
                continue;
            }

            // Check parameter type consistency
            for (size_t i = 0; i < desc.source_param_types.size(); ++i) {
                if (prev.source_param_types[i].kind != desc.source_param_types[i].kind) {
                    ReportError("cross-module parameter type mismatch for '" +
                                desc.source_function + "' parameter " +
                                std::to_string(i + 1) + ": conflicting IR types across modules");
                    success = false;
                    break;
                }
            }

            // Check return type consistency
            if (prev.source_return_type.kind != desc.source_return_type.kind) {
                ReportError("cross-module return type mismatch for '" +
                            desc.source_function + "': conflicting return types across modules");
                success = false;
            }
        }
    }

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
            // A descriptor is covered if its source function matches either
            // the source or target symbol of a LINK entry.  The CALL directive
            // always stores the callee name in source_function regardless of
            // which side of the LINK declaration it appears on.
            if (desc.source_function == entry.source_symbol ||
                desc.source_function == entry.target_symbol ||
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

    /** @name ABI Validation */
    /** @{ */
    // Validate parameter count compatibility between source and target.
    // When MAP_TYPE entries are present, they declare explicit marshalling, so
    // missing native parameter descriptors can be synthesized from the MAP_TYPE
    // information.  When neither params nor MAP_TYPE are available, emit a
    // warning and proceed with an opaque zero-parameter stub so the link
    // pipeline does not fail for simple declarations.
    if ((source_sym->params.empty() || target_sym->params.empty()) &&
        entry.param_mappings.empty()) {
        ReportWarning("LINK '" + entry.target_symbol +
                      "' <-> '" + entry.source_symbol +
                      "' has no ABI parameter schema and no MAP_TYPE entries; "
                      "generating opaque bridge stub");
    }

    // If symbols lack parameter descriptors but MAP_TYPE entries exist,
    // synthesize minimal descriptors from the MAP_TYPE declarations so that
    // the glue code generator can proceed.
    if (target_sym->params.empty() && !entry.param_mappings.empty()) {
        for (const auto &m : entry.param_mappings) {
            CrossLangSymbol::ParamDesc p;
            p.type_name = m.source_type;  // first MAP_TYPE arg = target's native type
            p.size = 8;                   // default size; ABI check will warn if needed
            p.is_pointer = false;
            target_sym->params.push_back(std::move(p));
        }
    }
    if (source_sym->params.empty() && !entry.param_mappings.empty()) {
        for (const auto &m : entry.param_mappings) {
            CrossLangSymbol::ParamDesc p;
            p.type_name = m.target_type;  // second MAP_TYPE arg = source's native type
            p.size = 8;
            p.is_pointer = false;
            source_sym->params.push_back(std::move(p));
        }
    }

    {
        if (source_sym->params.size() != target_sym->params.size()) {
            ReportError("parameter count mismatch in LINK '" + entry.target_symbol +
                        "' <-> '" + entry.source_symbol + "': target has " +
                        std::to_string(target_sym->params.size()) +
                        " parameter(s) but source has " +
                        std::to_string(source_sym->params.size()));
            return false;
        }

        // Validate individual parameter ABI compatibility
        for (size_t i = 0; i < source_sym->params.size(); ++i) {
            const auto &tp = target_sym->params[i];
            const auto &sp = source_sym->params[i];

            // Size mismatch without explicit marshalling is a hard error
            if (tp.size != 0 && sp.size != 0 && tp.size != sp.size) {
                // Check if there's a MAP_TYPE entry that covers this conversion
                bool has_mapping = false;
                if (i < entry.param_mappings.size() &&
                    !entry.param_mappings[i].source_type.empty() &&
                    !entry.param_mappings[i].target_type.empty()) {
                    has_mapping = true;
                }
                if (!has_mapping) {
                    ReportError("parameter " + std::to_string(i + 1) +
                                " size mismatch in LINK '" + entry.target_symbol +
                                "' <-> '" + entry.source_symbol + "': target expects " +
                                std::to_string(tp.size) + " bytes (" + tp.type_name +
                                ") but source provides " + std::to_string(sp.size) +
                                " bytes (" + sp.type_name + ") with no MAP_TYPE conversion");
                    return false;
                }
            }

            // Pointer vs value passing mismatch
            if (tp.is_pointer != sp.is_pointer) {
                bool has_mapping = false;
                if (i < entry.param_mappings.size() &&
                    !entry.param_mappings[i].source_type.empty()) {
                    has_mapping = true;
                }
                if (!has_mapping) {
                    ReportError("parameter " + std::to_string(i + 1) +
                                " passing convention mismatch in LINK '" +
                                entry.target_symbol + "' <-> '" + entry.source_symbol +
                                "': " + (tp.is_pointer ? "target is pointer" : "target is value") +
                                " vs " + (sp.is_pointer ? "source is pointer" : "source is value"));
                    return false;
                }
            }

            // Type-name mismatch without explicit mapping is also a hard error
            // because it indicates semantic ABI drift across language boundaries.
            if (!tp.type_name.empty() && !sp.type_name.empty() &&
                tp.type_name != sp.type_name) {
                bool has_mapping = false;
                if (i < entry.param_mappings.size() &&
                    !entry.param_mappings[i].source_type.empty() &&
                    !entry.param_mappings[i].target_type.empty()) {
                    has_mapping = true;
                }
                if (!has_mapping) {
                    ReportError("parameter " + std::to_string(i + 1) +
                                " type mismatch in LINK '" + entry.target_symbol +
                                "' <-> '" + entry.source_symbol + "': target type '" +
                                tp.type_name + "' vs source type '" + sp.type_name + "'");
                    return false;
                }
            }
        }

        // Validate return type compatibility.
        // When MAP_TYPE entries are present, return type mismatches are expected
        // because the bridge code handles conversion.  Only validate when no
        // MAP_TYPE entries exist (same-language linking).
        if (target_sym->return_desc.size == 0) {
            target_sym->return_desc.size = 8;  // default pointer-sized
            target_sym->return_desc.type_name = "any";
        }
        if (source_sym->return_desc.size == 0) {
            source_sym->return_desc.size = 8;
            source_sym->return_desc.type_name = "any";
        }

        if (entry.param_mappings.empty()) {
            // Same-language or untyped link — strict return type validation
            if (source_sym->return_desc.size != target_sym->return_desc.size) {
                ReportError("return type size mismatch in LINK '" + entry.target_symbol +
                            "' <-> '" + entry.source_symbol + "': target returns " +
                            std::to_string(target_sym->return_desc.size) + " bytes (" +
                            target_sym->return_desc.type_name + ") but source returns " +
                            std::to_string(source_sym->return_desc.size) + " bytes (" +
                            source_sym->return_desc.type_name + ")");
                return false;
            }

            if (!source_sym->return_desc.type_name.empty() &&
                !target_sym->return_desc.type_name.empty() &&
                source_sym->return_desc.type_name != target_sym->return_desc.type_name) {
                ReportError("return type mismatch in LINK '" + entry.target_symbol +
                            "' <-> '" + entry.source_symbol + "': target type '" +
                            target_sym->return_desc.type_name + "' vs source type '" +
                            source_sym->return_desc.type_name + "'");
                return false;
            }
        }
        // When MAP_TYPE entries are present, cross-language return type
        // differences are handled by the generated marshalling code — no error.
    }

    // Also validate against MAP_TYPE entry count if present
    if (!entry.param_mappings.empty()) {
        if (!target_sym->params.empty() &&
            entry.param_mappings.size() != target_sym->params.size()) {
            ReportError("MAP_TYPE entry count (" +
                        std::to_string(entry.param_mappings.size()) +
                        ") does not match target symbol '" + entry.target_symbol +
                        "' parameter count (" +
                        std::to_string(target_sym->params.size()) + ")");
            return false;
        }
        if (!source_sym->params.empty() &&
            entry.param_mappings.size() != source_sym->params.size()) {
            ReportError("MAP_TYPE entry count (" +
                        std::to_string(entry.param_mappings.size()) +
                        ") does not match source symbol '" + entry.source_symbol +
                        "' parameter count (" +
                        std::to_string(source_sym->params.size()) + ")");
            return false;
        }
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

    // Function prologue: push rbp; mov rbp, rsp
    code.push_back(0x55);                   // push rbp
    code.push_back(0x48); code.push_back(0x89); code.push_back(0xE5); // mov rbp, rsp

    // Allocate stack space for marshalling (64 bytes)
    // Layout: [rbp-0x08..rbp-0x20] saved regs
    //         [rbp-0x28] reserved
    //         [rbp-0x30] JNIEnv* (if Java)
    //         [rbp-0x38] GIL state (if Python)
    //         [rbp-0x40] scratch
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x40);
    // sub rsp, 0x40

    // ── Language-specific pre-call setup ──

    // Python target: acquire the GIL before touching any CPython API
    bool target_is_python = (entry.source_language == "python");
    bool target_is_java   = (entry.source_language == "java");
    // Note: entry.source_language is the *callee* language (the function we call).

    // Save argument registers on the stack so that GIL/JNI acquisition calls
    // do not clobber them.  We save the first 4 integer args.
    // push rdi; push rsi; push rdx; push rcx  (or rcx,rdx,r8,r9 on Win64)
#ifdef _WIN32
    code.push_back(0x51); // push rcx
    code.push_back(0x52); // push rdx
    code.push_back(0x41); code.push_back(0x50); // push r8
    code.push_back(0x41); code.push_back(0x51); // push r9
#else
    code.push_back(0x57); // push rdi
    code.push_back(0x56); // push rsi
    code.push_back(0x52); // push rdx
    code.push_back(0x51); // push rcx
#endif

    if (target_is_python) {
        EmitGILAcquire(code);
    }
    if (target_is_java) {
        EmitJNIEnvAcquire(code);
    }

    // Restore argument registers
#ifdef _WIN32
    code.push_back(0x41); code.push_back(0x59); // pop r9
    code.push_back(0x41); code.push_back(0x58); // pop r8
    code.push_back(0x5A); // pop rdx
    code.push_back(0x59); // pop rcx
#else
    code.push_back(0x59); // pop rcx
    code.push_back(0x5A); // pop rdx
    code.push_back(0x5E); // pop rsi
    code.push_back(0x5F); // pop rdi
#endif

    // ── Calling convention adaptation ──
    std::string from_cc = GetCallingConvention(entry.target_language);
    std::string to_cc = GetCallingConvention(entry.source_language);

    if (from_cc != to_cc) {
        EmitCallingConventionAdaptor(code, entry.target_language, entry.source_language);
    }

    // ── Rust borrow annotations (metadata NOPs) ──
    if (entry.source_language == "rust" || entry.target_language == "rust") {
        for (size_t i = 0; i < source_sym.params.size(); ++i) {
            if (source_sym.params[i].is_pointer) {
                // Assume immutable borrow for pointer params by default
                EmitRustBorrowAnnotation(code, i, /*is_mutable=*/false);
            }
        }
    }

    // ── Parameter marshalling ──
    for (size_t i = 0; i < entry.param_mappings.size(); ++i) {
        const auto &mapping = entry.param_mappings[i];
        if (!mapping.source_type.empty() && !mapping.target_type.empty()) {
            // Check if this is a container type conversion
            if (IsContainerType(mapping.source_type) || IsContainerType(mapping.target_type)) {
                EmitContainerMarshal(code, entry.target_language, entry.source_language,
                                     mapping.source_type, mapping.target_type, i);
                continue;
            }

            // Scalar type conversions
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
                EmitStringMarshal(code, entry.target_language, entry.source_language);
            }
        }
    }

    // ── Java JNI: prepend JNIEnv* as first argument ──
    if (target_is_java) {
        // Shift existing args right by one position to make room for JNIEnv*
#ifdef _WIN32
        // Win64: rcx=env, rdx=arg1, r8=arg2, r9=arg3 (arg4 on stack)
        code.push_back(0x4D); code.push_back(0x89); code.push_back(0xC1); // mov r9, r8
        code.push_back(0x49); code.push_back(0x89); code.push_back(0xD0); // mov r8, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xCA); // mov rdx, rcx
        // Load JNIEnv* from [rbp - 0x30] into rcx
        code.push_back(0x48); code.push_back(0x8B); code.push_back(0x4D);
        code.push_back(0xD0);
#else
        // SysV: rdi=env, rsi=arg1, rdx=arg2, rcx=arg3
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xD1); // mov rcx, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xF2); // mov rdx, rsi
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xFE); // mov rsi, rdi
        // Load JNIEnv* from [rbp - 0x30] into rdi
        code.push_back(0x48); code.push_back(0x8B); code.push_back(0x7D);
        code.push_back(0xD0);
#endif
    }

    // ── Call the source function ──
    code.push_back(0xE8); // call rel32
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

    // ── Return value marshalling ──
    // Determine the return type name for marshalling decisions
    std::string ret_type_name;
    if (!target_sym.return_desc.type_name.empty()) {
        ret_type_name = target_sym.return_desc.type_name;
    } else if (!source_sym.return_desc.type_name.empty()) {
        ret_type_name = source_sym.return_desc.type_name;
    } else {
        ret_type_name = "i64"; // default
    }
    EmitReturnMarshal(code, entry.source_language, entry.target_language, ret_type_name);

    // ── Language-specific post-call cleanup ──
    if (target_is_python) {
        // GIL state was saved at [rbp - 0x38] (offset 0x38 = 56)
        EmitGILRelease(code, 0x38);
    }
    if (target_is_java) {
        EmitJNIEnvRelease(code);
    }

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

    bool target_is_python = (entry.source_language == "python");
    bool target_is_java   = (entry.source_language == "java");

    // AArch64 function prologue: stp x29, x30, [sp, #-16]!; mov x29, sp
    // stp x29, x30, [sp, #-16]!
    code.push_back(0xFD); code.push_back(0x7B); code.push_back(0xBF); code.push_back(0xA9);
    // mov x29, sp
    code.push_back(0xFD); code.push_back(0x03); code.push_back(0x00); code.push_back(0x91);

    // Allocate 64 bytes of stack space: sub sp, sp, #64
    code.push_back(0xFF); code.push_back(0x03); code.push_back(0x01); code.push_back(0xD1);

    // Save argument registers x0-x3 to stack for GIL/JNI calls
    if (target_is_python || target_is_java) {
        // stp x0, x1, [sp, #0]
        code.push_back(0xE0); code.push_back(0x07); code.push_back(0x00); code.push_back(0xA9);
        // stp x2, x3, [sp, #16]
        code.push_back(0xE2); code.push_back(0x0F); code.push_back(0x01); code.push_back(0xA9);
    }

    // Python: acquire GIL
    if (target_is_python) {
        // BL PyGILState_Ensure
        size_t gil_off = code.size();
        code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x94);
        {
            Relocation r;
            r.offset = gil_off;
            r.symbol = "PyGILState_Ensure";
            r.type = static_cast<std::uint32_t>(RelocationType_ARM64::kR_AARCH64_CALL26);
            r.addend = 0; r.is_pc_relative = true; r.size = 4;
            pending_marshal_relocs_.push_back(r);
        }
        // Save GIL state: str w0, [sp, #32]
        code.push_back(0xE0); code.push_back(0x23); code.push_back(0x00); code.push_back(0xB9);
    }

    // Java: acquire JNIEnv*
    if (target_is_java) {
        size_t jni_off = code.size();
        code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x94);
        {
            Relocation r;
            r.offset = jni_off;
            r.symbol = "__ploy_rt_jni_get_env";
            r.type = static_cast<std::uint32_t>(RelocationType_ARM64::kR_AARCH64_CALL26);
            r.addend = 0; r.is_pc_relative = true; r.size = 4;
            pending_marshal_relocs_.push_back(r);
        }
        // Save JNIEnv*: str x0, [sp, #40]
        code.push_back(0xE0); code.push_back(0x2B); code.push_back(0x00); code.push_back(0xF9);
    }

    // Restore argument registers
    if (target_is_python || target_is_java) {
        // ldp x0, x1, [sp, #0]
        code.push_back(0xE0); code.push_back(0x07); code.push_back(0x40); code.push_back(0xA9);
        // ldp x2, x3, [sp, #16]
        code.push_back(0xE2); code.push_back(0x0F); code.push_back(0x41); code.push_back(0xA9);
    }

    // Java: shift args right, prepend JNIEnv*
    if (target_is_java) {
        // mov x3, x2; mov x2, x1; mov x1, x0
        code.push_back(0xE3); code.push_back(0x03); code.push_back(0x02); code.push_back(0xAA);
        code.push_back(0xE2); code.push_back(0x03); code.push_back(0x01); code.push_back(0xAA);
        code.push_back(0xE1); code.push_back(0x03); code.push_back(0x00); code.push_back(0xAA);
        // ldr x0, [sp, #40]  — JNIEnv*
        code.push_back(0xE0); code.push_back(0x2B); code.push_back(0x40); code.push_back(0xF9);
    }

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

    // Return value marshalling (AArch64 version uses same runtime helpers,
    // called via BL with the same relocations — the helpers are position-independent)
    std::string ret_type_name;
    if (!target_sym.return_desc.type_name.empty()) {
        ret_type_name = target_sym.return_desc.type_name;
    } else if (!source_sym.return_desc.type_name.empty()) {
        ret_type_name = source_sym.return_desc.type_name;
    }
    // On AArch64, Python unbox: x0 = PyObject* → scalar in x0/d0
    if (target_is_python && !ret_type_name.empty() &&
        (entry.target_language == "cpp" || entry.target_language == "rust")) {
        bool is_float = (ret_type_name == "float" || ret_type_name == "double" ||
                         ret_type_name == "f64" || ret_type_name == "f32");
        std::string helper = is_float ? "PyFloat_AsDouble" : "PyLong_AsLongLong";
        // x0 already holds PyObject*
        size_t unbox_off = code.size();
        code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x94);
        {
            Relocation r;
            r.offset = unbox_off;
            r.symbol = helper;
            r.type = static_cast<std::uint32_t>(RelocationType_ARM64::kR_AARCH64_CALL26);
            r.addend = 0; r.is_pc_relative = true; r.size = 4;
            pending_marshal_relocs_.push_back(r);
        }
    }

    // Python: release GIL
    if (target_is_python) {
        // Save return value: str x0, [sp, #48]
        code.push_back(0xE0); code.push_back(0x33); code.push_back(0x00); code.push_back(0xF9);
        // Load GIL state: ldr w0, [sp, #32]
        code.push_back(0xE0); code.push_back(0x23); code.push_back(0x40); code.push_back(0xB9);
        // BL PyGILState_Release
        size_t rel_off = code.size();
        code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x94);
        {
            Relocation r;
            r.offset = rel_off;
            r.symbol = "PyGILState_Release";
            r.type = static_cast<std::uint32_t>(RelocationType_ARM64::kR_AARCH64_CALL26);
            r.addend = 0; r.is_pc_relative = true; r.size = 4;
            pending_marshal_relocs_.push_back(r);
        }
        // Restore return value: ldr x0, [sp, #48]
        code.push_back(0xE0); code.push_back(0x33); code.push_back(0x40); code.push_back(0xF9);
    }

    // Java: release JNI env
    if (target_is_java) {
        // Save x0
        code.push_back(0xE0); code.push_back(0x33); code.push_back(0x00); code.push_back(0xF9);
        size_t jrel_off = code.size();
        code.push_back(0x00); code.push_back(0x00); code.push_back(0x00); code.push_back(0x94);
        {
            Relocation r;
            r.offset = jrel_off;
            r.symbol = "__ploy_rt_jni_release_env";
            r.type = static_cast<std::uint32_t>(RelocationType_ARM64::kR_AARCH64_CALL26);
            r.addend = 0; r.is_pc_relative = true; r.size = 4;
            pending_marshal_relocs_.push_back(r);
        }
        // Restore x0
        code.push_back(0xE0); code.push_back(0x33); code.push_back(0x40); code.push_back(0xF9);
    }

    // Deallocate stack: add sp, sp, #64
    code.push_back(0xFF); code.push_back(0x03); code.push_back(0x01); code.push_back(0x91);

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
        // System V → Windows x64:
        //   SysV:  rdi, rsi, rdx, rcx, r8, r9  (integer params 1-6)
        //   Win64: rcx, rdx, r8,  r9            (integer params 1-4)
        // We must rearrange without clobbering: use rax as scratch.
        //
        //   mov rax, rdx        ; save original rdx (SysV param 3)
        //   mov rcx, rdi        ; SysV param 1 → Win64 param 1
        //   mov rdx, rsi        ; SysV param 2 → Win64 param 2
        //   mov r8,  rax        ; SysV param 3 → Win64 param 3
        //   ; rcx already holds SysV param 4 → Win64 param 4 (r9)
        //   mov r9,  rcx        ; — but we overwrote rcx above,
        //   ; so we need to grab SysV's original rcx from the stack save area.
        // Save original rdx into rax first to avoid clobber.
        // mov rax, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xD0);
        // mov rcx, rdi
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xF9);
        // mov rdx, rsi
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xF2);
        // mov r8, rax  (original rdx)
        code.push_back(0x49); code.push_back(0x89); code.push_back(0xC0);
        // SysV param 4 was in rcx, but we overwrote rcx.
        // The caller's rcx is saved at [rbp-0x08] by the prologue's push sequence
        // (we saved argument registers in GenerateX86_64Stub).
        // For the common case (≤3 params) r9 is unused, so emit a safe load.
        // mov r9, [rbp-0x20]   ; 4th arg from stack save area
        code.push_back(0x4C); code.push_back(0x8B); code.push_back(0x4D);
        code.push_back(0xE0);
        // Allocate 32-byte shadow space required by Win64
        // sub rsp, 0x20
        code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC);
        code.push_back(0x20);
    } else if (from_cc == "win64" && to_cc == "sysv") {
        // Windows x64 → System V:
        //   Win64: rcx, rdx, r8, r9  (integer params 1-4)
        //   SysV:  rdi, rsi, rdx, rcx, r8, r9
        // mov rdi, rcx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xCF);
        // mov rsi, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xD6);
        // mov rdx, r8
        code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC2);
        // mov rcx, r9
        code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC9);
    }
    // Same convention → no adaptation needed
}

std::string PolyglotLinker::GetCallingConvention(const std::string &language) {
    // Language-specific calling convention overrides:
    //   - Java uses JNI which wraps the platform C ABI but requires JNIEnv* as
    //     the first argument — effectively extending the native convention.
    //   - Python uses the platform C ABI but requires GIL management around
    //     every call into CPython.
    //   - Rust uses the platform C ABI for extern "C" functions.
    //   - .NET/C# uses the platform C ABI for P/Invoke (similar to Win64 on
    //     Windows, SysV on Linux).
    // The returned string identifies the *base* calling convention; language-
    // specific wrappers (GIL, JNI) are emitted separately by the stub generator.
    if (language == "java") {
        // JNI calls use the platform convention but prepend JNIEnv* and jobject
#ifdef _WIN32
        return "win64";
#else
        return "sysv";
#endif
    }
    // All other languages use the platform-native C calling convention
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
    // Generate a call to the appropriate runtime conversion function.
    // On x86_64 the source container pointer is in the register for param_idx
    // (rdi/rcx = 0, rsi/rdx = 1, …).  Before the call we must move it into
    // the first-argument register if it is not already there.
    //
    // Each from_lang / to_lang pair has a dedicated runtime helper that
    // performs a deep copy or creates a shared-memory view, depending on
    // memory-model compatibility.

    // Ensure the source pointer is in rdi (SysV) / rcx (Win64) — param 0
    // position — since the runtime helpers expect it there.
#ifdef _WIN32
    // Win64: params are in rcx, rdx, r8, r9
    if (param_idx == 1) {
        // mov rcx, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xD1);
    } else if (param_idx == 2) {
        // mov rcx, r8
        code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC1);
    } else if (param_idx == 3) {
        // mov rcx, r9
        code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC9);
    }
    // param_idx == 0 → already in rcx
#else
    // SysV: params are in rdi, rsi, rdx, rcx, r8, r9
    if (param_idx == 1) {
        // mov rdi, rsi
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xF7);
    } else if (param_idx == 2) {
        // mov rdi, rdx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xD7);
    } else if (param_idx == 3) {
        // mov rdi, rcx
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xCF);
    }
    // param_idx == 0 → already in rdi
#endif

    // Select the runtime helper based on the language pair
    std::string target_sym;
    if (from_lang == "cpp" && to_lang == "python") {
        target_sym = "__ploy_rt_convert_cppvec_to_pylist";
    } else if (from_lang == "python" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_pylist_to_cppvec";
    } else if (from_lang == "rust" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_rustvec_to_cppvec";
    } else if (from_lang == "cpp" && to_lang == "rust") {
        target_sym = "__ploy_rt_convert_cppvec_to_rustvec";
    } else if (from_lang == "rust" && to_lang == "python") {
        target_sym = "__ploy_rt_convert_rustvec_to_pylist";
    } else if (from_lang == "python" && to_lang == "rust") {
        target_sym = "__ploy_rt_convert_pylist_to_rustvec";
    } else if (from_lang == "java" && (to_lang == "cpp" || to_lang == "python")) {
        target_sym = "__ploy_rt_convert_jarray_to_list";
    } else if ((from_lang == "cpp" || from_lang == "python") && to_lang == "java") {
        target_sym = "__ploy_rt_convert_list_to_jarray";
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

    // The runtime helper returns the converted pointer in rax.
    // Move it back into the parameter register for the downstream call.
#ifdef _WIN32
    if (param_idx == 0) {
        // mov rcx, rax
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1);
    } else if (param_idx == 1) {
        // mov rdx, rax
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xC2);
    } else if (param_idx == 2) {
        // mov r8, rax
        code.push_back(0x49); code.push_back(0x89); code.push_back(0xC0);
    } else if (param_idx == 3) {
        // mov r9, rax
        code.push_back(0x49); code.push_back(0x89); code.push_back(0xC1);
    }
#else
    if (param_idx == 0) {
        // mov rdi, rax
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xC7);
    } else if (param_idx == 1) {
        // mov rsi, rax
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xC6);
    } else if (param_idx == 2) {
        // mov rdx, rax
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xC2);
    } else if (param_idx == 3) {
        // mov rcx, rax
        code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1);
    }
#endif
}

void PolyglotLinker::EmitTupleMarshal(std::vector<std::uint8_t> &code,
                                       const std::string &from_lang,
                                       const std::string &to_lang,
                                       size_t param_idx) {
    // Tuples are passed as pointers to a packed struct.
    // Different languages lay out tuple fields in different orders / with
    // different padding, so we call a language-pair-specific runtime helper.

    std::string target_sym;
    if (from_lang == "python" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_pytuple_to_cpptuple";
    } else if (from_lang == "cpp" && to_lang == "python") {
        target_sym = "__ploy_rt_convert_cpptuple_to_pytuple";
    } else if (from_lang == "rust" && (to_lang == "cpp" || to_lang == "python")) {
        target_sym = "__ploy_rt_convert_rusttuple_to_tuple";
    } else {
        target_sym = "__ploy_rt_convert_tuple";
    }

    // Move param into first-arg register if necessary (same pattern as list)
#ifdef _WIN32
    if (param_idx == 1) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xD1); }
    else if (param_idx == 2) { code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC1); }
    else if (param_idx == 3) { code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC9); }
#else
    if (param_idx == 1) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xF7); }
    else if (param_idx == 2) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xD7); }
    else if (param_idx == 3) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xCF); }
#endif

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

void PolyglotLinker::EmitDictMarshal(std::vector<std::uint8_t> &code,
                                      const std::string &from_lang,
                                      const std::string &to_lang,
                                      size_t param_idx) {
    // Dict conversion is delegated to a language-pair-specific runtime helper.
    // The source pointer is in the register for param_idx; we move it into
    // the first-arg register, call the helper, then place the result back.

    std::string target_sym;
    if (from_lang == "python" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_pydict_to_cppmap";
    } else if (from_lang == "cpp" && to_lang == "python") {
        target_sym = "__ploy_rt_convert_cppmap_to_pydict";
    } else if (from_lang == "java" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_jmap_to_cppmap";
    } else if (from_lang == "cpp" && to_lang == "java") {
        target_sym = "__ploy_rt_convert_cppmap_to_jmap";
    } else {
        target_sym = "__ploy_rt_dict_convert";
    }

    // Move param into first-arg register
#ifdef _WIN32
    if (param_idx == 1) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xD1); }
    else if (param_idx == 2) { code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC1); }
    else if (param_idx == 3) { code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC9); }
#else
    if (param_idx == 1) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xF7); }
    else if (param_idx == 2) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xD7); }
    else if (param_idx == 3) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xCF); }
#endif

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

void PolyglotLinker::EmitStructMarshal(std::vector<std::uint8_t> &code,
                                        const std::string &from_lang,
                                        const std::string &to_lang,
                                        size_t param_idx) {
    // Struct marshalling copies fields between potentially different layouts.
    // The runtime helper reads StructFieldDesc metadata (embedded by the
    // frontend in the descriptor section) to perform field-by-field copying
    // with type widening/narrowing as needed.

    std::string target_sym;
    if (from_lang == "python" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_pyobj_to_cppstruct";
    } else if (from_lang == "cpp" && to_lang == "python") {
        target_sym = "__ploy_rt_convert_cppstruct_to_pyobj";
    } else if (from_lang == "rust" && to_lang == "cpp") {
        target_sym = "__ploy_rt_convert_ruststruct_to_cppstruct";
    } else {
        target_sym = "__ploy_rt_convert_struct";
    }

    // Move param into first-arg register
#ifdef _WIN32
    if (param_idx == 1) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xD1); }
    else if (param_idx == 2) { code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC1); }
    else if (param_idx == 3) { code.push_back(0x4C); code.push_back(0x89); code.push_back(0xC9); }
#else
    if (param_idx == 1) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xF7); }
    else if (param_idx == 2) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xD7); }
    else if (param_idx == 3) { code.push_back(0x48); code.push_back(0x89); code.push_back(0xCF); }
#endif

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

// ============================================================================
// High-level Container Marshal Dispatcher
// ============================================================================

void PolyglotLinker::EmitContainerMarshal(std::vector<std::uint8_t> &code,
                                           const std::string &from_lang,
                                           const std::string &to_lang,
                                           const std::string &from_type,
                                           const std::string &to_type,
                                           size_t param_idx) {
    // Dispatch to the correct container-specific marshaller based on the
    // source / target type names.  This is the single entry-point called
    // from GenerateX86_64Stub for every parameter with a MAP_TYPE entry
    // that involves a container type.
    if (IsListType(from_type) || IsListType(to_type)) {
        EmitListMarshal(code, from_lang, to_lang, param_idx);
    } else if (IsDictType(from_type) || IsDictType(to_type)) {
        EmitDictMarshal(code, from_lang, to_lang, param_idx);
    } else if (IsTupleType(from_type) || IsTupleType(to_type)) {
        EmitTupleMarshal(code, from_lang, to_lang, param_idx);
    } else if (IsStructType(from_type) || IsStructType(to_type)) {
        EmitStructMarshal(code, from_lang, to_lang, param_idx);
    }
    // Non-container types are handled by scalar marshal (EmitIntToFloat etc.)
}

// ============================================================================
// Return Value Marshalling
// ============================================================================

void PolyglotLinker::EmitReturnMarshal(std::vector<std::uint8_t> &code,
                                        const std::string &from_lang,
                                        const std::string &to_lang,
                                        const std::string &return_type) {
    // After the callee returns, the result is in rax (integer) or xmm0 (float).
    // We may need to unbox (Python PyObject* → C++ scalar) or box
    // (C++ scalar → Python PyObject*).
    //
    // Python → C++: rax holds a PyObject*.  We must call PyLong_AsLongLong /
    //   PyFloat_AsDouble to extract the C value.
    // C++ → Python: rax holds a C scalar.  We must call PyLong_FromLongLong /
    //   PyFloat_FromDouble to create a PyObject*.
    // Rust / Java → C++: typically no conversion needed (same C ABI), but
    //   Java objects are jobject handles that require JNI unwrapping.

    if (from_lang == "python" && (to_lang == "cpp" || to_lang == "rust")) {
        // Unbox Python return value: rax = PyObject* → scalar in rax
        bool is_float = (return_type == "float" || return_type == "double" ||
                         return_type == "f64" || return_type == "f32");
        if (is_float) {
            // Call PyFloat_AsDouble(rax) — result in xmm0
            // rax already holds the PyObject*; move it to first-arg register
#ifdef _WIN32
            code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1); // mov rcx, rax
#else
            code.push_back(0x48); code.push_back(0x89); code.push_back(0xC7); // mov rdi, rax
#endif
            code.push_back(0xE8); // call
            size_t off = code.size();
            code.push_back(0x00); code.push_back(0x00);
            code.push_back(0x00); code.push_back(0x00);
            {
                Relocation r;
                r.offset = off;
                r.symbol = "PyFloat_AsDouble";
                r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
                r.addend = -4;
                r.is_pc_relative = true;
                r.size = 4;
                pending_marshal_relocs_.push_back(r);
            }
        } else {
            // Call PyLong_AsLongLong(rax) — result in rax
#ifdef _WIN32
            code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1); // mov rcx, rax
#else
            code.push_back(0x48); code.push_back(0x89); code.push_back(0xC7); // mov rdi, rax
#endif
            code.push_back(0xE8); // call
            size_t off = code.size();
            code.push_back(0x00); code.push_back(0x00);
            code.push_back(0x00); code.push_back(0x00);
            {
                Relocation r;
                r.offset = off;
                r.symbol = "PyLong_AsLongLong";
                r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
                r.addend = -4;
                r.is_pc_relative = true;
                r.size = 4;
                pending_marshal_relocs_.push_back(r);
            }
        }
    } else if ((from_lang == "cpp" || from_lang == "rust") && to_lang == "python") {
        // Box C++ return value: scalar in rax → PyObject* in rax
        bool is_float = (return_type == "float" || return_type == "double" ||
                         return_type == "f64" || return_type == "f32");
        if (is_float) {
            // xmm0 already holds the float result; call PyFloat_FromDouble
            // (xmm0 is already the first float arg on both SysV and Win64)
            code.push_back(0xE8); // call
            size_t off = code.size();
            code.push_back(0x00); code.push_back(0x00);
            code.push_back(0x00); code.push_back(0x00);
            {
                Relocation r;
                r.offset = off;
                r.symbol = "PyFloat_FromDouble";
                r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
                r.addend = -4;
                r.is_pc_relative = true;
                r.size = 4;
                pending_marshal_relocs_.push_back(r);
            }
        } else {
            // rax holds the integer result; call PyLong_FromLongLong(rax)
#ifdef _WIN32
            code.push_back(0x48); code.push_back(0x89); code.push_back(0xC1); // mov rcx, rax
#else
            code.push_back(0x48); code.push_back(0x89); code.push_back(0xC7); // mov rdi, rax
#endif
            code.push_back(0xE8); // call
            size_t off = code.size();
            code.push_back(0x00); code.push_back(0x00);
            code.push_back(0x00); code.push_back(0x00);
            {
                Relocation r;
                r.offset = off;
                r.symbol = "PyLong_FromLongLong";
                r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
                r.addend = -4;
                r.is_pc_relative = true;
                r.size = 4;
                pending_marshal_relocs_.push_back(r);
            }
        }
    } else if (from_lang == "java" && to_lang == "cpp") {
        // JNI return: for object returns, call JNI NewGlobalRef to prevent GC
        // For scalars the JNI layer already returns native types — no conversion.
        if (return_type == "jobject" || return_type == "jstring") {
            // Call (*env)->NewGlobalRef(env, rax)
            // Not emitted for scalar types (jint, jlong etc.)
            code.push_back(0xE8); // call
            size_t off = code.size();
            code.push_back(0x00); code.push_back(0x00);
            code.push_back(0x00); code.push_back(0x00);
            {
                Relocation r;
                r.offset = off;
                r.symbol = "__ploy_rt_jni_new_global_ref";
                r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
                r.addend = -4;
                r.is_pc_relative = true;
                r.size = 4;
                pending_marshal_relocs_.push_back(r);
            }
        }
    }
    // Same-language returns (cpp→cpp, rust→rust) need no marshalling
}

// ============================================================================
// Python GIL Acquire / Release
// ============================================================================

void PolyglotLinker::EmitGILAcquire(std::vector<std::uint8_t> &code) {
    // Call PyGILState_Ensure().
    // Returns a PyGILState_STATE value in eax that must be passed to
    // PyGILState_Release() when the Python call completes.
    // We save the result on the stack at [rbp - 0x38] (inside the 0x40
    // bytes allocated by the prologue).
    code.push_back(0xE8); // call PyGILState_Ensure
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "PyGILState_Ensure";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
    // Save GIL state: mov [rbp - 0x38], eax
    code.push_back(0x89); code.push_back(0x45); code.push_back(0xC8);
}

void PolyglotLinker::EmitGILRelease(std::vector<std::uint8_t> &code,
                                     size_t gil_state_stack_offset) {
    // Restore the PyGILState_STATE from the stack and call PyGILState_Release().
    // We must preserve rax (the callee's return value) across this call.

    // Save return value: push rax
    code.push_back(0x50);

    // Load GIL state into first-arg register:
    //   mov edi, [rbp - offset]   (SysV)
    //   mov ecx, [rbp - offset]   (Win64)
    std::uint8_t disp = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(256 - gil_state_stack_offset));
#ifdef _WIN32
    code.push_back(0x8B); code.push_back(0x4D); code.push_back(disp); // mov ecx, [rbp-disp]
#else
    code.push_back(0x8B); code.push_back(0x7D); code.push_back(disp); // mov edi, [rbp-disp]
#endif

    code.push_back(0xE8); // call PyGILState_Release
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "PyGILState_Release";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }

    // Restore return value: pop rax
    code.push_back(0x58);
}

// ============================================================================
// Java JNI Environment Acquire / Release
// ============================================================================

void PolyglotLinker::EmitJNIEnvAcquire(std::vector<std::uint8_t> &code) {
    // Obtain a JNIEnv* for the current thread by calling
    // __ploy_rt_jni_get_env(), a runtime helper that wraps
    // JavaVM::AttachCurrentThread / GetEnv.
    // Result in rax → save to [rbp - 0x30].
    code.push_back(0xE8); // call
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "__ploy_rt_jni_get_env";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }
    // Save JNIEnv*: mov [rbp - 0x30], rax
    code.push_back(0x48); code.push_back(0x89); code.push_back(0x45);
    code.push_back(0xD0);
}

void PolyglotLinker::EmitJNIEnvRelease(std::vector<std::uint8_t> &code) {
    // Detach the current thread from the JVM if we attached it.
    // Save rax first (return value from the JNI call).
    code.push_back(0x50); // push rax

    code.push_back(0xE8); // call __ploy_rt_jni_release_env
    size_t off = code.size();
    code.push_back(0x00); code.push_back(0x00);
    code.push_back(0x00); code.push_back(0x00);
    {
        Relocation r;
        r.offset = off;
        r.symbol = "__ploy_rt_jni_release_env";
        r.type = static_cast<std::uint32_t>(RelocationType_x86_64::kR_X86_64_PLT32);
        r.addend = -4;
        r.is_pc_relative = true;
        r.size = 4;
        pending_marshal_relocs_.push_back(r);
    }

    code.push_back(0x58); // pop rax
}

// ============================================================================
// Rust Borrow Annotation
// ============================================================================

void PolyglotLinker::EmitRustBorrowAnnotation(std::vector<std::uint8_t> &code,
                                               size_t param_idx,
                                               bool is_mutable) {
    // Emit a NOP-encoded metadata marker that the Polyglot runtime can use
    // to enforce borrow semantics at the FFI boundary.
    // Format: 0x0F 0x1F <modrm> where modrm encodes:
    //   bits [7:6] = 00 (no displacement)
    //   bits [5:3] = param_idx (0-7)
    //   bits [2:0] = is_mutable ? 1 : 0
    // This is a 3-byte NOP (canonical x86-64 NOP) that the runtime interprets
    // as a borrow annotation when scanning the stub prologue.
    code.push_back(0x0F);
    code.push_back(0x1F);
    std::uint8_t modrm = static_cast<std::uint8_t>(
        ((param_idx & 0x07) << 3) | (is_mutable ? 0x01 : 0x00));
    code.push_back(modrm);
}

// ============================================================================
// ABI Validation
// ============================================================================

ABIDescriptor PolyglotLinker::GetABIDescriptor(const std::string &language) {
    ABIDescriptor abi;
    abi.pointer_size = 8;  // 64-bit targets
    abi.stack_alignment = 16;

    // Most languages use the platform-native calling convention.
    // On Linux/macOS (SysV ABI): 6 integer registers, 8 XMM registers.
    // On Windows (Win64 ABI): 4 integer registers, 4 XMM registers + shadow space.
    std::string cc = GetCallingConvention(language);
    abi.calling_convention = cc;

    if (cc == "win64") {
        abi.int_reg_count = 4;
        abi.float_reg_count = 4;
        abi.requires_shadow_space = true;
    } else if (cc == "aapcs64") {
        abi.int_reg_count = 8;
        abi.float_reg_count = 8;
        abi.requires_shadow_space = false;
    } else {
        // Default: SysV AMD64
        abi.int_reg_count = 6;
        abi.float_reg_count = 8;
        abi.requires_shadow_space = false;
    }

    return abi;
}

bool PolyglotLinker::ValidateABICompatibility(const CrossLangSymbol &target,
                                              const CrossLangSymbol &source) {
    // Validate parameter count match.
    if (target.params.size() != source.params.size()) {
        ReportError("ABI mismatch: '" + target.name + "' (" + target.language +
                    ") expects " + std::to_string(target.params.size()) +
                    " parameter(s) but '" + source.name + "' (" + source.language +
                    ") provides " + std::to_string(source.params.size()));
        return false;
    }

    bool compatible = true;

    // Validate each parameter: size and pointer compatibility.
    for (size_t i = 0; i < target.params.size(); ++i) {
        const auto &tp = target.params[i];
        const auto &sp = source.params[i];

        // Size mismatch (e.g. i32 vs i64) can cause data truncation.
        if (tp.size != 0 && sp.size != 0 && tp.size != sp.size) {
            ReportWarning("ABI warning: parameter " + std::to_string(i + 1) +
                          " of '" + target.name + "' has size " +
                          std::to_string(tp.size) + " but source provides size " +
                          std::to_string(sp.size));
        }

        // Pointer vs non-pointer mismatch.
        if (tp.is_pointer != sp.is_pointer) {
            ReportError("ABI mismatch: parameter " + std::to_string(i + 1) +
                        " of '" + target.name + "' is " +
                        (tp.is_pointer ? "a pointer" : "not a pointer") +
                        " but source '" + source.name + "' provides " +
                        (sp.is_pointer ? "a pointer" : "a value"));
            compatible = false;
        }
    }

    // Validate return type compatibility.
    if (target.return_desc.size != 0 && source.return_desc.size != 0 &&
        target.return_desc.size != source.return_desc.size) {
        ReportWarning("ABI warning: return type of '" + target.name +
                      "' has size " + std::to_string(target.return_desc.size) +
                      " but source return size is " +
                      std::to_string(source.return_desc.size));
    }

    if (target.return_desc.is_pointer != source.return_desc.is_pointer) {
        ReportError("ABI mismatch: '" + target.name + "' returns " +
                    (target.return_desc.is_pointer ? "a pointer" : "a value") +
                    " but source '" + source.name + "' returns " +
                    (source.return_desc.is_pointer ? "a pointer" : "a value"));
        compatible = false;
    }

    return compatible;
}

} // namespace polyglot::linker

/** @} */