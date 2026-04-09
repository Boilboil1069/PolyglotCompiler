#include "tools/polyc/include/compilation_pipeline.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "backends/arm64/include/arm64_target.h"
#include "backends/wasm/include/wasm_target.h"
#include "backends/x86_64/include/x86_target.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/ploy/include/package_indexer.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/passes/opt.h"
#include "middle/include/ir/ssa.h"
#include "middle/include/ir/verifier.h"
#include "middle/include/passes/pass_manager.h"

namespace polyglot::compilation {
namespace {

using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;

constexpr std::uint32_t kPobjSectionFlagBss = 1u << 1;

#pragma pack(push, 1)
struct PobjFileHeader {
    char magic[4];
    std::uint16_t version{1};
    std::uint16_t section_count{0};
    std::uint16_t symbol_count{0};
    std::uint16_t reloc_count{0};
    std::uint64_t strtab_offset{0};
};

struct PobjSectionRecord {
    std::uint32_t name_offset{0};
    std::uint32_t flags{0};
    std::uint64_t offset{0};
    std::uint64_t size{0};
};

struct PobjSymbolRecord {
    std::uint32_t name_offset{0};
    std::uint32_t section_index{0xFFFFFFFF};
    std::uint64_t value{0};
    std::uint64_t size{0};
    std::uint8_t binding{0};
    std::uint8_t reserved[3]{};
};

struct PobjRelocRecord {
    std::uint32_t section_index{0};
    std::uint64_t offset{0};
    std::uint32_t type{0};
    std::uint32_t symbol_index{0};
    std::int64_t addend{0};
};
#pragma pack(pop)

struct InternalSection {
    std::string name;
    std::vector<std::uint8_t> data;
    bool bss{false};
    std::vector<linker::Relocation> relocs;
};

struct InternalSymbol {
    std::string name;
    std::uint32_t section_index{0xFFFFFFFF};
    std::uint64_t value{0};
    std::uint64_t size{0};
    bool global{true};
    bool defined{false};
};

void AppendDiagnostics(frontends::Diagnostics &src, std::vector<frontends::Diagnostic> &dst) {
    const auto &all = src.All();
    dst.insert(dst.end(), all.begin(), all.end());
}

std::string ReadTextFile(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

std::vector<std::uint8_t> BuildPobjBinary(const std::vector<InternalSection> &sections,
                                          const std::vector<InternalSymbol> &symbols) {
    std::vector<std::uint8_t> strtab{0};
    auto add_str = [&](const std::string &s) {
        std::uint32_t off = static_cast<std::uint32_t>(strtab.size());
        strtab.insert(strtab.end(), s.begin(), s.end());
        strtab.push_back(0);
        return off;
    };

    std::vector<PobjSectionRecord> sec_records;
    sec_records.reserve(sections.size());
    for (const auto &s : sections) {
        PobjSectionRecord rec{};
        rec.name_offset = add_str(s.name);
        rec.flags = s.bss ? kPobjSectionFlagBss : 0;
        rec.size = static_cast<std::uint64_t>(s.data.size());
        sec_records.push_back(rec);
    }

    std::vector<PobjSymbolRecord> sym_records;
    sym_records.reserve(symbols.size());
    for (const auto &s : symbols) {
        PobjSymbolRecord rec{};
        rec.name_offset = add_str(s.name);
        rec.section_index = s.defined ? s.section_index : 0xFFFFFFFF;
        rec.value = s.value;
        rec.size = s.size;
        rec.binding = s.global ? 1 : 0;
        sym_records.push_back(rec);
    }

    std::vector<PobjRelocRecord> reloc_records;
    for (std::size_t si = 0; si < sections.size(); ++si) {
        for (const auto &r : sections[si].relocs) {
            PobjRelocRecord rr{};
            rr.section_index = static_cast<std::uint32_t>(si);
            rr.offset = r.offset;
            rr.type = r.type;
            rr.symbol_index = (r.symbol_index >= 0) ? static_cast<std::uint32_t>(r.symbol_index) : 0;
            rr.addend = r.addend;
            reloc_records.push_back(rr);
        }
    }

    std::size_t cursor = sizeof(PobjFileHeader) +
                         sec_records.size() * sizeof(PobjSectionRecord) +
                         sym_records.size() * sizeof(PobjSymbolRecord) +
                         reloc_records.size() * sizeof(PobjRelocRecord);

    for (std::size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].bss) {
            sec_records[i].offset = 0;
        } else {
            sec_records[i].offset = cursor;
            cursor += sections[i].data.size();
        }
    }

    PobjFileHeader hdr{};
    std::memcpy(hdr.magic, "POBJ", 4);
    hdr.version = 1;
    hdr.section_count = static_cast<std::uint16_t>(sec_records.size());
    hdr.symbol_count = static_cast<std::uint16_t>(sym_records.size());
    hdr.reloc_count = static_cast<std::uint16_t>(reloc_records.size());
    hdr.strtab_offset = cursor;

    std::vector<std::uint8_t> out;
    out.resize(cursor + strtab.size());

    std::size_t w = 0;
    auto write_bytes = [&](const void *ptr, std::size_t n) {
        std::memcpy(out.data() + w, ptr, n);
        w += n;
    };

    write_bytes(&hdr, sizeof(hdr));
    if (!sec_records.empty()) {
        write_bytes(sec_records.data(), sec_records.size() * sizeof(PobjSectionRecord));
    }
    if (!sym_records.empty()) {
        write_bytes(sym_records.data(), sym_records.size() * sizeof(PobjSymbolRecord));
    }
    if (!reloc_records.empty()) {
        write_bytes(reloc_records.data(), reloc_records.size() * sizeof(PobjRelocRecord));
    }

    for (const auto &s : sections) {
        if (s.bss || s.data.empty()) continue;
        write_bytes(s.data.data(), s.data.size());
    }

    write_bytes(strtab.data(), strtab.size());
    return out;
}

class DefaultFrontendStage final : public FrontendStage {
  public:
    FrontendOutput Run(const CompilationContext::Config &config,
                       frontends::Diagnostics &diagnostics) override {
        FrontendOutput out;
        out.source_file = config.source_file;
        out.language = config.source_language;

        if (config.source_language != "ploy") {
            diagnostics.ReportError(
                core::SourceLoc{config.source_label.empty() ? config.source_file : config.source_label, 1, 1},
                frontends::ErrorCode::kInvalidLanguage,
                "staged compilation pipeline currently accepts only '.ploy' sources");
            AppendDiagnostics(diagnostics, out.parse_diagnostics);
            out.success = false;
            return out;
        }

        std::string source = config.source_text;
        if (source.empty() && !config.source_file.empty()) {
            source = ReadTextFile(config.source_file);
        }
        if (source.empty()) {
            diagnostics.ReportError(
                core::SourceLoc{config.source_label.empty() ? config.source_file : config.source_label, 1, 1},
                frontends::ErrorCode::kMissingExpression,
                "source text is empty");
            AppendDiagnostics(diagnostics, out.parse_diagnostics);
            out.success = false;
            return out;
        }

        const std::string label =
            config.source_label.empty() ?
                (config.source_file.empty() ? std::string{"<memory>"} : config.source_file)
                                        : config.source_label;

        {
            ploy::PloyLexer token_lexer(source, label);
            while (true) {
                auto tk = token_lexer.NextToken();
                if (tk.kind == frontends::TokenKind::kEndOfFile) break;
                out.tokens.push_back(std::move(tk));
            }
        }

        ploy::PloyLexer parse_lexer(source, label);
        ploy::PloyParser parser(parse_lexer, diagnostics);
        parser.ParseModule();
        out.ast = parser.TakeModule();

        AppendDiagnostics(diagnostics, out.parse_diagnostics);
        out.success = (out.ast != nullptr) && !diagnostics.HasErrors();
        if (!config.source_file.empty() && std::filesystem::exists(config.source_file)) {
            out.source_mtime = std::filesystem::last_write_time(config.source_file);
        }
        return out;
    }
};

class DefaultSemanticStage final : public SemanticStage {
  public:
    SemanticDatabase Run(const FrontendOutput &input,
                         frontends::Diagnostics &diagnostics,
                         const std::shared_ptr<ploy::PackageDiscoveryCache> &cache) override {
        SemanticDatabase db;
        db.validated_ast = input.ast;
        if (!input.success || !input.ast) {
            diagnostics.ReportError(
                core::SourceLoc{input.source_file.empty() ? "<memory>" : input.source_file, 1, 1},
                frontends::ErrorCode::kUnexpectedToken,
                "semantic stage requires a successfully parsed AST");
            AppendDiagnostics(diagnostics, db.sema_diagnostics);
            db.success = false;
            return db;
        }

        ploy::PloySemaOptions opts;
        opts.strict_mode = true;
        opts.enable_package_discovery = false;
        opts.discovery_cache = cache;
        db.sema_instance = std::make_shared<ploy::PloySema>(diagnostics, opts);

        db.success = db.sema_instance->Analyze(input.ast);

        db.symbols = db.sema_instance->Symbols();
        db.signatures = db.sema_instance->KnownSignatures();
        db.class_schemas = db.sema_instance->ClassSchemas();
        db.link_entries = db.sema_instance->Links();
        db.type_mappings = db.sema_instance->TypeMappings();
        db.packages = db.sema_instance->DiscoveredPackages();
        db.venv_configs = db.sema_instance->VenvConfigs();

        AppendDiagnostics(diagnostics, db.sema_diagnostics);
        return db;
    }
};

class DefaultMarshalPlanStage final : public MarshalPlanStage {
  public:
    MarshalPlan Run(const SemanticDatabase &input,
                    frontends::Diagnostics &diagnostics) override {
        MarshalPlan plan;
        if (!input.success) {
            diagnostics.ReportError(core::SourceLoc{"<marshal>", 1, 1},
                                    frontends::ErrorCode::kTypeMismatch,
                                    "marshal planning requires successful semantic database");
            AppendDiagnostics(diagnostics, plan.plan_diagnostics);
            plan.success = false;
            return plan;
        }

        for (const auto &entry : input.link_entries) {
            CallMarshalPlan call_plan;
            call_plan.link_id = entry.target_symbol + "<-" + entry.source_symbol;
            call_plan.target_language = entry.target_language;
            call_plan.source_language = entry.source_language;
            call_plan.target_function = entry.target_symbol;
            call_plan.source_function = entry.source_symbol;

            const auto sig_it = input.signatures.find(entry.target_symbol);
            if (sig_it != input.signatures.end()) {
                for (std::size_t i = 0; i < sig_it->second.param_types.size(); ++i) {
                    ParamMarshalPlan p;
                    p.param_index = i;
                    p.source_type = sig_it->second.param_types[i];
                    p.target_type = sig_it->second.param_types[i];
                    p.strategy = MarshalStrategy::kDirectCopy;
                    p.size_bytes = 8;
                    p.alignment = 8;
                    call_plan.param_plans.push_back(std::move(p));
                }
                call_plan.return_plan.source_type = sig_it->second.return_type;
                call_plan.return_plan.target_type = sig_it->second.return_type;
                call_plan.return_plan.strategy = MarshalStrategy::kDirectCopy;
                call_plan.return_plan.size_bytes = 8;
            }

            call_plan.target_abi.calling_convention = "sysv64";
            call_plan.target_abi.pointer_size = 8;
            call_plan.target_abi.stack_alignment = 16;
            call_plan.target_abi.int_reg_count = 6;
            call_plan.target_abi.float_reg_count = 8;
            call_plan.source_abi = call_plan.target_abi;
            call_plan.needs_calling_conv_adapt = false;

            plan.call_plans.push_back(std::move(call_plan));
        }

        plan.success = true;
        return plan;
    }
};

class DefaultBridgeGenerationStage final : public BridgeGenerationStage {
  public:
    BridgeGenerationOutput Run(const MarshalPlan &plan,
                               const SemanticDatabase &sema_db,
                               frontends::Diagnostics &diagnostics) override {
        BridgeGenerationOutput out;
        if (!plan.success || !sema_db.success || !sema_db.sema_instance) {
            diagnostics.ReportError(core::SourceLoc{"<bridge>", 1, 1},
                                    frontends::ErrorCode::kABIIncompatible,
                                    "bridge generation requires successful marshal plan and semantic database");
            AppendDiagnostics(diagnostics, out.generation_diagnostics);
            out.success = false;
            return out;
        }

        linker::LinkerConfig cfg;
        cfg.verbose = false;
        linker::PolyglotLinker linker(cfg);

        // Build descriptors from marshal plan.
        for (const auto &cp : plan.call_plans) {
            ploy::CrossLangCallDescriptor desc;
            desc.stub_name = "__ploy_bridge_" + cp.target_language + "_" + cp.source_language + "_" + cp.target_function;
            desc.source_language = cp.source_language;
            desc.target_language = cp.target_language;
            desc.source_function = cp.source_function;
            desc.target_function = cp.target_function;
            for (const auto &pm : cp.param_plans) {
                desc.source_param_types.push_back(ir::IRType::Pointer(ir::IRType::Void()));
                desc.target_param_types.push_back(ir::IRType::Pointer(ir::IRType::Void()));
                ploy::CrossLangCallDescriptor::MarshalOp op;
                op.kind = ploy::CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
                op.from = ir::IRType::Pointer(ir::IRType::Void());
                op.to = ir::IRType::Pointer(ir::IRType::Void());
                desc.param_marshal.push_back(op);
            }
            desc.source_return_type = ir::IRType::Pointer(ir::IRType::Void());
            desc.target_return_type = ir::IRType::Pointer(ir::IRType::Void());
            desc.return_marshal.kind = ploy::CrossLangCallDescriptor::MarshalOp::Kind::kDirect;
            desc.return_marshal.from = desc.source_return_type;
            desc.return_marshal.to = desc.target_return_type;
            linker.AddCallDescriptor(desc);
        }

        for (const auto &entry : sema_db.link_entries) {
            linker.AddLinkEntry(entry);
        }

        std::unordered_set<std::string> registered;
        auto add_symbol = [&](const std::string &name, const std::string &lang) {
            const std::string key = lang + "::" + name;
            if (registered.count(key) != 0) return;
            registered.insert(key);

            linker::CrossLangSymbol s;
            s.name = name;
            s.mangled_name = name;
            s.language = lang;
            s.type = linker::SymbolType::kFunction;

            const auto sig_it = sema_db.signatures.find(name);
            if (sig_it != sema_db.signatures.end()) {
                for (const auto &t : sig_it->second.param_types) {
                    linker::CrossLangSymbol::ParamDesc p;
                    p.type_name = t.ToString();
                    p.size = 8;
                    p.is_pointer = t.IsPointer();
                    s.params.push_back(std::move(p));
                }
                s.return_desc.type_name = sig_it->second.return_type.ToString();
                s.return_desc.size = 8;
                s.return_desc.is_pointer = sig_it->second.return_type.IsPointer();
            }
            linker.AddCrossLangSymbol(s);
        };

        for (const auto &entry : sema_db.link_entries) {
            add_symbol(entry.target_symbol, entry.target_language);
            add_symbol(entry.source_symbol, entry.source_language);
        }

        if (!linker.ResolveLinks()) {
            for (const auto &err : linker.GetErrors()) {
                diagnostics.ReportError(core::SourceLoc{"<bridge>", 1, 1},
                                        frontends::ErrorCode::kUnresolvedSymbol,
                                        err);
            }
            AppendDiagnostics(diagnostics, out.generation_diagnostics);
            out.success = false;
            return out;
        }

        for (const auto &stub : linker.GetStubs()) {
            GeneratedStub g;
            g.stub_name = stub.stub_name;
            g.link_id = stub.target_function + "<-" + stub.source_function;
            g.code = stub.code;
            g.relocations = stub.relocations;
            g.target_symbol = stub.target_function;
            g.source_symbol = stub.source_function;
            out.stubs.push_back(std::move(g));
        }

        out.success = true;
        return out;
    }
};

class DefaultBackendStage final : public BackendStage {
  public:
    BackendOutput Run(const SemanticDatabase &sema_db,
                      const BridgeGenerationOutput &bridges,
                      const CompilationContext::Config &config,
                      frontends::Diagnostics &diagnostics) override {
        BackendOutput out;
        out.target_arch = config.target_arch;
        out.target_os = config.target_os;

        if (!sema_db.success || !sema_db.validated_ast || !sema_db.sema_instance) {
            diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                    frontends::ErrorCode::kLoweringUndefined,
                                    "backend stage requires a successful semantic database");
            AppendDiagnostics(diagnostics, out.backend_diagnostics);
            out.success = false;
            return out;
        }

        ir::IRContext ir_module;
        ploy::PloyLowering lowering(ir_module, diagnostics, *sema_db.sema_instance);
        if (!lowering.Lower(sema_db.validated_ast)) {
            AppendDiagnostics(diagnostics, out.backend_diagnostics);
            out.success = false;
            return out;
        }

        // Inject resolved bridge stubs into IR before backend emission.
        for (const auto &stub : bridges.stubs) {
            auto fn = ir_module.CreateFunction(stub.stub_name);
            fn->is_external = false;
            fn->is_bridge_stub = true;
            fn->precompiled_code = stub.code;
            for (const auto &rel : stub.relocations) {
                ir::StubRelocation sr;
                sr.offset = static_cast<std::size_t>(rel.offset);
                sr.symbol = rel.symbol;
                sr.type = rel.type;
                sr.addend = rel.addend;
                sr.is_pc_relative = rel.is_pc_relative;
                sr.size = static_cast<std::uint8_t>(rel.size);
                fn->precompiled_relocs.push_back(sr);
            }
        }

        for (auto &fn : ir_module.Functions()) {
            ir::ConvertToSSA(*fn);
        }

        std::string verify_msg;
        ir::VerifyOptions verify_opts;
        verify_opts.strict = config.strict_mode;
        if (!ir::Verify(ir_module, verify_opts, &verify_msg)) {
            diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                    frontends::ErrorCode::kLoweringUndefined,
                                    "IR verification failed: " + verify_msg);
            AppendDiagnostics(diagnostics, out.backend_diagnostics);
            out.success = false;
            return out;
        }

        if (config.opt_level > 0) {
            passes::PassManager pm(static_cast<passes::PassManager::OptLevel>(config.opt_level));
            pm.Build();
            pm.RunOnModule(ir_module, config.verbose);
        }

        auto absorb_mc = [&](const auto &mc, const std::string &triple, const std::string &asm_text) {
            out.target_triple = triple;
            out.assembly_text = asm_text;
            // One CompiledObject per section for deterministic packaging.
            for (const auto &sec : mc.sections) {
                CompiledObject obj;
                obj.name = sec.name;
                if (sec.name == ".text") {
                    obj.code = sec.data;
                } else {
                    obj.data = sec.data;
                }
                out.objects.push_back(std::move(obj));
            }

            for (const auto &sym : mc.symbols) {
                linker::Symbol ls;
                ls.name = sym.name;
                ls.section = sym.section;
                ls.offset = sym.value;
                ls.size = sym.size;
                ls.value = sym.value;
                ls.binding = sym.global ? linker::SymbolBinding::kGlobal : linker::SymbolBinding::kLocal;
                ls.type = linker::SymbolType::kFunction;
                ls.is_defined = sym.defined;

                for (auto &obj : out.objects) {
                    if (obj.name == sym.section || (sym.section.empty() && obj.name == ".text")) {
                        obj.symbols.push_back(ls);
                        break;
                    }
                }
            }

            for (const auto &rel : mc.relocs) {
                linker::Relocation lr;
                lr.section = rel.section;
                lr.offset = rel.offset;
                lr.type = rel.type;
                lr.symbol = rel.symbol;
                lr.addend = rel.addend;
                lr.is_pc_relative = false;
                lr.size = 4;
                for (auto &obj : out.objects) {
                    if (obj.name == rel.section || (rel.section.empty() && obj.name == ".text")) {
                        obj.relocations.push_back(lr);
                        break;
                    }
                }
            }
        };

        const bool use_arm64 = (config.target_arch == "arm64" || config.target_arch == "aarch64" ||
                                config.target_arch == "armv8");
        const bool use_wasm = (config.target_arch == "wasm" || config.target_arch == "wasm32" ||
                               config.target_arch == "wasm64");

        if (use_wasm) {
            backends::wasm::WasmTarget target(&ir_module);
            out.target_triple = target.TargetTriple();
            out.assembly_text = target.EmitAssembly();

            auto bin = target.EmitWasmBinary();
            if (bin.empty()) {
                diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                        frontends::ErrorCode::kLoweringUndefined,
                                        "WASM backend produced empty binary");
                AppendDiagnostics(diagnostics, out.backend_diagnostics);
                out.success = false;
                return out;
            }
            CompiledObject obj;
            obj.name = ".text";
            obj.code = std::move(bin);
            out.objects.push_back(std::move(obj));
            out.success = true;
            return out;
        }

        if (use_arm64) {
            backends::arm64::Arm64Target target(&ir_module);
            target.SetRegAllocStrategy(backends::arm64::RegAllocStrategy::kLinearScan);
            auto asm_text = target.EmitAssembly();
            auto mc = target.EmitObjectCode();
            if (mc.sections.empty()) {
                diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                        frontends::ErrorCode::kLoweringUndefined,
                                        "ARM64 backend produced no sections");
                AppendDiagnostics(diagnostics, out.backend_diagnostics);
                out.success = false;
                return out;
            }
            absorb_mc(mc, target.TargetTriple(), asm_text);
            out.success = true;
            return out;
        }

        backends::x86_64::X86Target target(&ir_module);
        target.SetRegAllocStrategy(backends::x86_64::RegAllocStrategy::kLinearScan);
        auto asm_text = target.EmitAssembly();
        auto mc = target.EmitObjectCode();
        if (mc.sections.empty()) {
            diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                    frontends::ErrorCode::kLoweringUndefined,
                                    "x86_64 backend produced no sections");
            AppendDiagnostics(diagnostics, out.backend_diagnostics);
            out.success = false;
            return out;
        }
        absorb_mc(mc, target.TargetTriple(), asm_text);
        out.success = true;
        return out;
    }
};

class DefaultPackagingStage final : public PackagingStage {
  public:
    PackagingOutput Run(const BackendOutput &input,
                        const CompilationContext::Config &config,
                        frontends::Diagnostics &diagnostics) override {
        PackagingOutput out;

        if (!input.success || input.objects.empty()) {
            diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                    frontends::ErrorCode::kLoweringUndefined,
                                    "packaging requires backend objects");
            AppendDiagnostics(diagnostics, out.packaging_diagnostics);
            out.success = false;
            return out;
        }

        // WASM is emitted as a final binary directly.
        if (config.target_arch == "wasm" || config.target_arch == "wasm32" ||
            config.target_arch == "wasm64") {
            out.format = OutputFormat::kWasm;
            out.binary_data = input.objects.front().code;
            out.output_path = config.output_file;
            std::ofstream ofs(out.output_path, std::ios::binary | std::ios::trunc);
            if (!ofs.is_open()) {
                diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                        frontends::ErrorCode::kUnresolvedSymbol,
                                        "failed to open output file: " + out.output_path);
                AppendDiagnostics(diagnostics, out.packaging_diagnostics);
                out.success = false;
                return out;
            }
            ofs.write(reinterpret_cast<const char *>(out.binary_data.data()),
                      static_cast<std::streamsize>(out.binary_data.size()));
            out.file_size = out.binary_data.size();
            out.success = ofs.good();
            AppendDiagnostics(diagnostics, out.packaging_diagnostics);
            return out;
        }

        std::vector<InternalSection> sections;
        std::unordered_map<std::string, std::uint32_t> sec_index;
        for (const auto &obj : input.objects) {
            InternalSection sec;
            sec.name = obj.name.empty() ? ".text" : obj.name;
            sec.data = !obj.code.empty() ? obj.code : obj.data;
            sec_index[sec.name] = static_cast<std::uint32_t>(sections.size());
            sections.push_back(std::move(sec));
        }

        std::vector<InternalSymbol> symbols;
        // Keep deterministic entry symbol.
        symbols.push_back({"_start", sec_index.count(".text") ? sec_index[".text"] : 0, 0,
                           sections.empty() ? 0 : sections[0].data.size(), true, true});

        std::unordered_map<std::string, std::uint32_t> sym_index;
        sym_index["_start"] = 0;

        for (const auto &obj : input.objects) {
            for (const auto &sym : obj.symbols) {
                if (sym_index.count(sym.name) != 0) continue;
                InternalSymbol s;
                s.name = sym.name;
                if (sym.is_defined && sec_index.count(sym.section) != 0) {
                    s.section_index = sec_index[sym.section];
                    s.defined = true;
                } else {
                    s.section_index = 0xFFFFFFFF;
                    s.defined = false;
                }
                s.value = sym.offset;
                s.size = sym.size;
                s.global = sym.is_global();
                sym_index[s.name] = static_cast<std::uint32_t>(symbols.size());
                symbols.push_back(std::move(s));
            }
        }

        for (const auto &obj : input.objects) {
            const auto si_it = sec_index.find(obj.name.empty() ? ".text" : obj.name);
            if (si_it == sec_index.end()) continue;
            auto &dst_relocs = sections[si_it->second].relocs;
            for (auto rel : obj.relocations) {
                if (sym_index.count(rel.symbol) == 0) {
                    InternalSymbol ext;
                    ext.name = rel.symbol;
                    ext.defined = false;
                    ext.global = true;
                    ext.section_index = 0xFFFFFFFF;
                    sym_index[ext.name] = static_cast<std::uint32_t>(symbols.size());
                    symbols.push_back(std::move(ext));
                }
                rel.symbol_index = static_cast<int>(sym_index[rel.symbol]);
                dst_relocs.push_back(std::move(rel));
            }
        }

        out.binary_data = BuildPobjBinary(sections, symbols);
        out.format = OutputFormat::kStaticLib;

        std::string out_path = config.output_file;
        if (out_path.empty()) out_path = "a.out";
        const std::string obj_out = config.emit_obj_path.empty() ? (out_path + ".pobj") : config.emit_obj_path;
        out.output_path = obj_out;

        std::ofstream ofs(obj_out, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                    frontends::ErrorCode::kUnresolvedSymbol,
                                    "failed to create object output: " + obj_out);
            AppendDiagnostics(diagnostics, out.packaging_diagnostics);
            out.success = false;
            return out;
        }
        ofs.write(reinterpret_cast<const char *>(out.binary_data.data()),
                  static_cast<std::streamsize>(out.binary_data.size()));
        ofs.close();

        if (!config.emit_asm_path.empty()) {
            std::ofstream asm_ofs(config.emit_asm_path, std::ios::binary | std::ios::trunc);
            if (asm_ofs.is_open()) {
                asm_ofs << input.assembly_text;
            }
        }

        if (!config.emit_ir_path.empty()) {
            // IR text is generated in backend stage only through internal context;
            // staged pipeline persists assembly/object at this stage.
            std::ofstream ir_ofs(config.emit_ir_path, std::ios::binary | std::ios::trunc);
            if (ir_ofs.is_open()) {
                ir_ofs << "; staged pipeline wrote backend output (IR text emitted in driver-level diagnostics mode)\n";
            }
        }

        if (config.mode == "link") {
            std::string cmd = config.polyld_path + " " + obj_out + " -o " + out_path;
            // Pass the serialized cross-language descriptor file so that polyld
            // can call AddCallDescriptor/AddLinkEntry/AddCrossLangSymbol before
            // ResolveLinks() — closing the cross-language link pipeline gap.
            if (!config.ploy_desc_file.empty()) {
                cmd += " --ploy-desc " + config.ploy_desc_file;
            }
            if (!config.aux_dir.empty()) {
                cmd += " --aux-dir " + config.aux_dir;
            }
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                diagnostics.ReportWarning(core::SourceLoc{"<packaging>", 1, 1},
                                          frontends::ErrorCode::kUnresolvedSymbol,
                                          "polyld invocation failed (object file was generated): " + cmd);
            }
        }

        out.file_size = out.binary_data.size();
        out.success = true;
        AppendDiagnostics(diagnostics, out.packaging_diagnostics);
        return out;
    }
};

}  // namespace

CompilationPipeline::CompilationPipeline(CompilationContext::Config config) {
    context_.config = std::move(config);

    if (!context_.config.target_os.empty()) {
        // keep caller supplied value
    } else {
#if defined(_WIN32)
        context_.config.target_os = "windows";
#elif defined(__APPLE__)
        context_.config.target_os = "macos";
#else
        context_.config.target_os = "linux";
#endif
    }

    context_.package_cache = std::make_shared<ploy::PackageDiscoveryCache>();
    frontend_stage_ = CreateFrontendStage();
    semantic_stage_ = CreateSemanticStage();
    marshal_plan_stage_ = CreateMarshalPlanStage();
    bridge_generation_stage_ = CreateBridgeGenerationStage();
    backend_stage_ = CreateBackendStage(context_.config.target_arch);
    packaging_stage_ = CreatePackagingStage();
}

bool CompilationPipeline::RunAll() {
    return RunFrontend() && RunSemantic() && RunMarshalPlan() && RunBridgeGeneration() &&
           RunBackend() && RunPackaging();
}

bool CompilationPipeline::RunFrontend() {
    auto start = high_resolution_clock::now();
    context_.frontend_output = frontend_stage_->Run(context_.config, *context_.diagnostics);
    auto end = high_resolution_clock::now();
    context_.timings.push_back({"frontend", std::chrono::duration<double, std::milli>(end - start).count()});
    return context_.frontend_output->success;
}

bool CompilationPipeline::RunSemantic() {
    if (!context_.frontend_output.has_value()) return false;

    if (context_.config.package_index && context_.frontend_output->ast) {
        std::unordered_set<std::string> seen_languages;
        std::vector<std::string> languages;
        std::vector<ploy::VenvConfig> venvs;

        for (const auto &decl : context_.frontend_output->ast->declarations) {
            auto import = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
            if (!import || import->language.empty() || import->package_name.empty()) continue;
            if (seen_languages.insert(import->language).second) {
                languages.push_back(import->language);
            }
        }

        if (!languages.empty()) {
            ploy::PackageIndexerOptions idx_opts;
            idx_opts.command_timeout = milliseconds{context_.config.package_index_timeout_ms};
            idx_opts.verbose = context_.config.verbose;

            auto runner = std::make_shared<ploy::DefaultCommandRunner>(idx_opts.command_timeout);
            ploy::PackageIndexer indexer(context_.package_cache, runner, idx_opts);
            indexer.BuildIndex(languages, venvs);
        }
    }

    auto start = high_resolution_clock::now();
    context_.semantic_db = semantic_stage_->Run(*context_.frontend_output,
                                                *context_.diagnostics,
                                                context_.package_cache);
    auto end = high_resolution_clock::now();
    context_.timings.push_back({"semantic-db", std::chrono::duration<double, std::milli>(end - start).count()});
    return context_.semantic_db->success;
}

bool CompilationPipeline::RunMarshalPlan() {
    if (!context_.semantic_db.has_value()) return false;
    auto start = high_resolution_clock::now();
    context_.marshal_plan =
        marshal_plan_stage_->Run(*context_.semantic_db, *context_.diagnostics);
    auto end = high_resolution_clock::now();
    context_.timings.push_back({"marshal-plan", std::chrono::duration<double, std::milli>(end - start).count()});
    return context_.marshal_plan->success;
}

bool CompilationPipeline::RunBridgeGeneration() {
    if (!context_.marshal_plan.has_value() || !context_.semantic_db.has_value()) return false;
    auto start = high_resolution_clock::now();
    context_.bridge_output = bridge_generation_stage_->Run(*context_.marshal_plan,
                                                           *context_.semantic_db,
                                                           *context_.diagnostics);
    auto end = high_resolution_clock::now();
    context_.timings.push_back({"bridge-generation", std::chrono::duration<double, std::milli>(end - start).count()});
    if (!context_.bridge_output->success) return false;

    // ── Serialize cross-language descriptors to a PAUX text file ──────────
    // The file uses the same text format that PolyglotLinker::LoadDescriptorFile()
    // understands (LINK / CALL / SYMBOL lines), so polyld can ingest it via
    // --ploy-desc without any additional parsing logic.
    if (!context_.config.aux_dir.empty() && context_.semantic_db.has_value()) {
        namespace fs = std::filesystem;
        std::string stem;
        if (!context_.config.source_file.empty()) {
            stem = fs::path(context_.config.source_file).stem().string();
        } else {
            stem = "output";
        }

        fs::path desc_path = fs::path(context_.config.aux_dir) / (stem + "_link_descriptors.paux");

        std::ofstream ofs(desc_path);
        if (ofs.is_open()) {
            ofs << "# PolyglotCompiler cross-language descriptor file\n";
            ofs << "# Generated by polyc bridge stage — do not edit manually\n";

            // Emit LINK entries (from sema)
            for (const auto &entry : context_.semantic_db->link_entries) {
                ofs << "LINK " << entry.target_language
                    << " " << entry.source_language
                    << " " << entry.target_symbol
                    << " " << entry.source_symbol << "\n";
            }

            // Emit CALL descriptors (from lowering, carried through marshal plan)
            for (const auto &cp : context_.marshal_plan->call_plans) {
                std::string stub_name = "__ploy_bridge_" + cp.target_language + "_" +
                                        cp.source_language + "_" + cp.target_function;
                ofs << "CALL " << stub_name
                    << " " << cp.source_language
                    << " " << cp.target_language
                    << " " << cp.source_function
                    << " " << cp.target_function << "\n";
            }

            // Emit SYMBOL entries derived from sema signatures so polyld can
            // reconstruct CrossLangSymbol without re-running sema.
            std::unordered_set<std::string> seen;
            auto emit_sym = [&](const std::string &name, const std::string &lang) {
                std::string key = lang + "::" + name;
                if (!seen.insert(key).second) return;
                ofs << "SYMBOL " << name << " " << lang << " " << name << "\n";
            };
            for (const auto &entry : context_.semantic_db->link_entries) {
                emit_sym(entry.target_symbol, entry.target_language);
                emit_sym(entry.source_symbol, entry.source_language);
            }
            for (const auto &cp : context_.marshal_plan->call_plans) {
                emit_sym(cp.target_function, cp.target_language);
                emit_sym(cp.source_function, cp.source_language);
            }

            ofs.close();
            context_.config.ploy_desc_file = desc_path.string();
            if (context_.config.verbose) {
                std::cerr << "[pipeline] link descriptors -> " << desc_path.string() << "\n";
            }
        }
    }

    return true;
}

bool CompilationPipeline::RunBackend() {
    if (!context_.semantic_db.has_value() || !context_.bridge_output.has_value()) return false;
    auto start = high_resolution_clock::now();
    context_.backend_output = backend_stage_->Run(*context_.semantic_db,
                                                  *context_.bridge_output,
                                                  context_.config,
                                                  *context_.diagnostics);
    auto end = high_resolution_clock::now();
    context_.timings.push_back({"backend", std::chrono::duration<double, std::milli>(end - start).count()});
    return context_.backend_output->success;
}

bool CompilationPipeline::RunPackaging() {
    if (!context_.backend_output.has_value()) return false;
    auto start = high_resolution_clock::now();
    context_.packaging_output = packaging_stage_->Run(*context_.backend_output,
                                                      context_.config,
                                                      *context_.diagnostics);
    auto end = high_resolution_clock::now();
    context_.timings.push_back({"packaging", std::chrono::duration<double, std::milli>(end - start).count()});
    return context_.packaging_output->success;
}

PackagingOutput *CompilationPipeline::GetFinalOutput() {
    if (!context_.packaging_output.has_value()) return nullptr;
    return &(*context_.packaging_output);
}

const FrontendOutput *CompilationPipeline::GetFrontendOutput() const {
    return context_.frontend_output ? &(*context_.frontend_output) : nullptr;
}

const SemanticDatabase *CompilationPipeline::GetSemanticDb() const {
    return context_.semantic_db ? &(*context_.semantic_db) : nullptr;
}

const MarshalPlan *CompilationPipeline::GetMarshalPlan() const {
    return context_.marshal_plan ? &(*context_.marshal_plan) : nullptr;
}

const BridgeGenerationOutput *CompilationPipeline::GetBridgeOutput() const {
    return context_.bridge_output ? &(*context_.bridge_output) : nullptr;
}

const BackendOutput *CompilationPipeline::GetBackendOutput() const {
    return context_.backend_output ? &(*context_.backend_output) : nullptr;
}

std::unique_ptr<FrontendStage> CreateFrontendStage() {
    return std::make_unique<DefaultFrontendStage>();
}

std::unique_ptr<SemanticStage> CreateSemanticStage() {
    return std::make_unique<DefaultSemanticStage>();
}

std::unique_ptr<MarshalPlanStage> CreateMarshalPlanStage() {
    return std::make_unique<DefaultMarshalPlanStage>();
}

std::unique_ptr<BridgeGenerationStage> CreateBridgeGenerationStage() {
    return std::make_unique<DefaultBridgeGenerationStage>();
}

std::unique_ptr<BackendStage> CreateBackendStage(const std::string &target_arch) {
    (void)target_arch;
    return std::make_unique<DefaultBackendStage>();
}

std::unique_ptr<PackagingStage> CreatePackagingStage() {
    return std::make_unique<DefaultPackagingStage>();
}

}  // namespace polyglot::compilation
