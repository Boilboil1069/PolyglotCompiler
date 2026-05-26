/**
 * @file     compilation_pipeline.cpp
 * @brief    Compiler driver implementation
 *
 * @ingroup  Tool / polyc
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

#include "middle/include/ir/ir_printer.h"
#include "middle/include/ir/passes/opt.h"
#include "middle/include/ir/ssa.h"
#include "middle/include/ir/verifier.h"
#include "middle/include/passes/pass_manager.h"

#include "backends/arm64/include/arm64_target.h"
#include "backends/common/include/backend_registry.h"
#include "backends/common/include/object_file.h"
#include "backends/common/include/target_backend.h"
#include "backends/wasm/include/wasm_target.h"
#include "backends/x86_64/include/x86_target.h"
#include "frontends/common/include/frontend_registry.h"
#include "frontends/ploy/include/package_indexer.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_lowering.h"
#include "frontends/ploy/include/ploy_parser.h"
#include "tools/polyc/include/compilation_pipeline.h"
#include "tools/polyc/include/linker_probe.h"

namespace polyglot::compilation {
namespace {

using std::chrono::high_resolution_clock;
using std::chrono::milliseconds;
using polyglot::tools::linker_probe::ExpandLinkCommand;
using polyglot::tools::linker_probe::LinkerChoice;
using polyglot::tools::linker_probe::SelectAvailableLinker;

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

// PE-7-D: result of folding a single CompiledObject's section bytes into the
// running per-name section table. We hand it back to the symbol / relocation
// passes so they can shift their offsets by `base_offset` (the size the
// merged section had right before this object's bytes were appended).
struct ObjAbsorbInfo {
  std::string canonical_name;
  std::uint32_t section_index{0xFFFFFFFF};
  std::uint64_t base_offset{0};
};

// PE-7-D: merge one CompiledObject's bytes into the per-name section table.
// Each CompiledObject corresponds to exactly one MC section (the backend's
// `absorb_artifacts` path materialises one per emitted section), so this
// function picks `obj.code` for `.text` and `obj.data` for everything else,
// then either appends to the existing same-name InternalSection or registers
// a fresh entry. The selector-style `sec.data = !obj.code.empty() ? ... :
// obj.data;` shortcut that used to live here silently dropped any non-`.text`
// payload of objects whose `obj.code` happened to be non-empty (none today,
// but a footgun for any future backend that mixes both fields in one object).
ObjAbsorbInfo AbsorbObjectSections(const CompiledObject &obj,
                                   std::vector<InternalSection> &sections,
                                   std::unordered_map<std::string, std::uint32_t> &sec_index) {
  ObjAbsorbInfo info;
  info.canonical_name = obj.name.empty() ? std::string(".text") : obj.name;
  const auto &payload = (info.canonical_name == ".text" && !obj.code.empty()) ? obj.code
                                                                              : obj.data;

  auto it = sec_index.find(info.canonical_name);
  if (it == sec_index.end()) {
    InternalSection sec;
    sec.name = info.canonical_name;
    sec.data = payload;
    info.section_index = static_cast<std::uint32_t>(sections.size());
    info.base_offset = 0;
    sec_index[info.canonical_name] = info.section_index;
    sections.push_back(std::move(sec));
  } else {
    info.section_index = it->second;
    auto &dst = sections[info.section_index];
    info.base_offset = static_cast<std::uint64_t>(dst.data.size());
    dst.data.insert(dst.data.end(), payload.begin(), payload.end());
  }
  return info;
}

// PE-7-D: stable section ordering used by the packaging pass. The ABI-fixed
// loaders downstream (polyld + the native COFF/ELF/Mach-O writers) all expect
// `.text` first, then read-only data, then writable data, then BSS. Anything
// else keeps its original first-seen order so backends that emit custom
// sections stay deterministic.
int SectionPriority(const std::string &name) {
  if (name == ".text") return 0;
  if (name == ".rdata" || name == ".rodata") return 1;
  if (name == ".data") return 2;
  if (name == ".bss") return 3;
  return 4;
}

void AppendDiagnostics(frontends::Diagnostics &src, std::vector<frontends::Diagnostic> &dst) {
  const auto &all = src.All();
  dst.insert(dst.end(), all.begin(), all.end());
}

std::string ReadTextFile(const std::string &path) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs.is_open())
    return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

// Return the default native object format string for the current host.
std::string HostObjectFormat() {
#if defined(_WIN32)
  return "coff";
#elif defined(__APPLE__)
  return "macho";
#else
  return "elf";
#endif
}

// Build a native-format object file (ELF/Mach-O) from internal sections and
// symbols using the backend ObjectFileBuilder API.  Returns the file content
// as a byte vector, or an empty vector on failure.
std::vector<std::uint8_t> BuildNativeObjectBinary(const std::string &format,
                                                  const std::string &arch,
                                                  const std::vector<InternalSection> &sections,
                                                  const std::vector<InternalSymbol> &symbols) {
  bool is_arm64 = (arch == "arm64" || arch == "aarch64" || arch == "armv8");
  std::unique_ptr<backends::ObjectFileBuilder> builder;
  if (format == "macho") {
    builder = std::make_unique<backends::MachOBuilder>(is_arm64);
  } else if (format == "coff") {
    // Windows / PE-style COFF translation unit.
    builder = std::make_unique<backends::COFFBuilder>(is_arm64);
  } else if (format == "elf") {
    builder = std::make_unique<backends::ELFBuilder>(!is_arm64);
  } else {
    // Unknown format string: refuse rather than silently mis-emitting.
    return {};
  }

  for (const auto &sec : sections) {
    backends::Section bs;
    bs.name = sec.name;
    bs.data = sec.data;
    for (const auto &r : sec.relocs) {
      backends::Relocation br;
      br.offset = r.offset;
      br.symbol = r.symbol;
      br.type = r.type;
      br.addend = r.addend;
      bs.relocations.push_back(std::move(br));
    }
    builder->AddSection(bs);
  }
  for (const auto &sym : symbols) {
    backends::Symbol bs;
    bs.name = sym.name;
    bs.section = (sym.section_index < sections.size()) ? sections[sym.section_index].name : "";
    bs.offset = sym.value;
    bs.size = sym.size;
    bs.is_global = sym.global;
    bs.is_function = true;
    builder->AddSymbol(bs);
  }
  return builder->Build();
}

// Determine object file extension for the given format string.
std::string ObjectExtension(const std::string &fmt) {
  if (fmt == "pobj")
    return ".pobj";
  if (fmt == "coff")
    return ".obj";
  return ".o";
}

// Probe-then-invoke linker discovery is shared with stage_packaging.cpp via
// the helpers in tools/polyc/include/linker_probe.h.  See that header for
// the selection rules and command-template contract.

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

  std::size_t cursor = sizeof(PobjFileHeader) + sec_records.size() * sizeof(PobjSectionRecord) +
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
    if (s.bss || s.data.empty())
      continue;
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
          core::SourceLoc{config.source_label.empty() ? config.source_file : config.source_label, 1,
                          1},
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
          core::SourceLoc{config.source_label.empty() ? config.source_file : config.source_label, 1,
                          1},
          frontends::ErrorCode::kMissingExpression, "source text is empty");
      AppendDiagnostics(diagnostics, out.parse_diagnostics);
      out.success = false;
      return out;
    }

    const std::string label =
        config.source_label.empty()
            ? (config.source_file.empty() ? std::string{"<memory>"} : config.source_file)
            : config.source_label;

    {
      ploy::PloyLexer token_lexer(source, label);
      while (true) {
        auto tk = token_lexer.NextToken();
        if (tk.kind == frontends::TokenKind::kEndOfFile)
          break;
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
  explicit DefaultSemanticStage(bool strict_mode = false) : strict_mode_(strict_mode) {}

  SemanticDatabase Run(const FrontendOutput &input, frontends::Diagnostics &diagnostics,
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
    opts.strict_mode = strict_mode_;
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

private:
  bool strict_mode_{false};
};

class DefaultMarshalPlanStage final : public MarshalPlanStage {
public:
  MarshalPlan Run(const SemanticDatabase &input, frontends::Diagnostics &diagnostics) override {
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
      call_plan.lang_version = entry.lang_version;

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
  BridgeGenerationOutput Run(const MarshalPlan &plan, const SemanticDatabase &sema_db,
                             frontends::Diagnostics &diagnostics) override {
    BridgeGenerationOutput out;
    if (!plan.success || !sema_db.success || !sema_db.sema_instance) {
      diagnostics.ReportError(
          core::SourceLoc{"<bridge>", 1, 1}, frontends::ErrorCode::kABIIncompatible,
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
      // Mirror the mangling rule from `MangleStubName` in ploy lowering:
      // weave a `_v<sanitized_version>_` segment into the stub name when a
      // foreign-language version is pinned, so that the linker can route to
      // the matching versioned bridge variant.
      desc.stub_name = "__ploy_bridge_" + cp.target_language + "_" + cp.source_language + "_";
      if (!cp.lang_version.empty()) {
        desc.stub_name += "v";
        for (char c : cp.lang_version) {
          desc.stub_name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        }
        desc.stub_name.push_back('_');
      }
      desc.stub_name += cp.target_function;
      desc.source_language = cp.source_language;
      desc.target_language = cp.target_language;
      desc.source_function = cp.source_function;
      desc.target_function = cp.target_function;
      for ([[maybe_unused]] const auto &pm : cp.param_plans) {
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
      desc.lang_version = cp.lang_version;
      linker.AddCallDescriptor(desc);
    }

    for (const auto &entry : sema_db.link_entries) {
      linker.AddLinkEntry(entry);
    }

    std::unordered_set<std::string> registered;
    auto add_symbol = [&](const std::string &name, const std::string &lang) {
      const std::string key = lang + "::" + name;
      if (registered.count(key) != 0)
        return;
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
                                frontends::ErrorCode::kUnresolvedSymbol, err);
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
  BackendOutput Run(const SemanticDatabase &sema_db, const BridgeGenerationOutput &bridges,
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

    // Translate a TargetArtifacts result from any backend (registered via
    // backends::BackendRegistry) into the pipeline's CompiledObject /
    // linker::{Symbol,Relocation} representation.  Replaces the previous
    // per-architecture if/else dispatch.
    auto absorb_artifacts = [&](const backends::TargetArtifacts &art,
                                const std::string &triple) {
      out.target_triple = triple;
      out.assembly_text = art.assembly_text;

      // For backends that produce a single self-contained binary (e.g. wasm)
      // the section list contains exactly one ".text" section that already
      // holds the final bytes.  Native targets produce one section per
      // emitted MC section.
      for (const auto &sec : art.sections) {
        CompiledObject obj;
        obj.name = sec.name;
        if (sec.name == ".text") {
          obj.code = sec.data;
        } else {
          obj.data = sec.data;
        }
        out.objects.push_back(std::move(obj));
      }

      for (const auto &sym : art.exported_symbols) {
        linker::Symbol ls;
        ls.name = sym.name;
        ls.section = sym.section;
        ls.offset = sym.value;
        ls.size = sym.size;
        ls.value = sym.value;
        ls.binding = sym.is_global ? linker::SymbolBinding::kGlobal
                                   : linker::SymbolBinding::kLocal;
        ls.type = linker::SymbolType::kFunction;
        ls.is_defined = sym.is_defined;

        for (auto &obj : out.objects) {
          if (obj.name == sym.section || (sym.section.empty() && obj.name == ".text")) {
            obj.symbols.push_back(ls);
            break;
          }
        }
      }

      for (const auto &rel : art.relocations) {
        linker::Relocation lr;
        lr.section = rel.section;
        lr.offset = rel.offset;
        lr.type = rel.type;
        lr.symbol = rel.symbol;
        lr.addend = rel.addend;
        lr.is_pc_relative = (rel.type == 1);
        lr.size = (rel.type == 1) ? 4 : 8;
        for (auto &obj : out.objects) {
          if (obj.name == rel.section || (rel.section.empty() && obj.name == ".text")) {
            obj.relocations.push_back(lr);
            break;
          }
        }
      }
    };

    // Resolve the backend through the global registry instead of the
    // architecture-string if/else chain that used to live here.  The triple
    // accepted on the command line may be any canonical triple or alias
    // declared by a registered backend.
    std::string lookup_diag;
    backends::ITargetBackend *backend =
        backends::BackendRegistry::Instance().FindOrDiagnose(config.target_arch, &lookup_diag);
    if (!backend) {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined, lookup_diag);
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    backends::TargetOptions backend_options;
    backend_options.emit = backends::EmitKind::kObject;
    backend_options.opt_level = config.opt_level;
    backend_options.force = config.force;
    // The driver-level --regalloc flag is wired into the backend through
    // CompilationContext::Config in a follow-up sub-need; for now keep the
    // historical default.
    backend_options.reg_alloc = backends::RegAllocStrategy::kLinearScan;

    backends::CompileResult bres = backend->Compile(ir_module, backend_options);

    // Surface every backend diagnostic through the driver's diagnostics sink
    // so that --strict / --force semantics are honoured uniformly.
    for (const auto &d : bres.diagnostics) {
      if (d.severity == backends::BackendDiagnostic::Severity::kError) {
        diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                                frontends::ErrorCode::kLoweringUndefined, d.message);
      } else {
        // Treat info-level entries as warnings so they remain visible without
        // failing the build; the Diagnostics interface has no separate info
        // channel.
        diagnostics.ReportWarning(core::SourceLoc{"<backend>", 1, 1},
                                  frontends::ErrorCode::kLoweringUndefined, d.message);
      }
    }

    if (!bres.ok) {
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    if (bres.artifacts.sections.empty() && bres.artifacts.object_bytes.empty()) {
      diagnostics.ReportError(core::SourceLoc{"<backend>", 1, 1},
                              frontends::ErrorCode::kLoweringUndefined,
                              backend->TargetTriple() + " backend produced no sections");
      AppendDiagnostics(diagnostics, out.backend_diagnostics);
      out.success = false;
      return out;
    }

    absorb_artifacts(bres.artifacts, backend->TargetTriple());
    out.success = true;
    return out;
  }
};

class DefaultPackagingStage final : public PackagingStage {
public:
  PackagingOutput Run(const BackendOutput &input, const CompilationContext::Config &config,
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
    // PE-7-D: per-CompiledObject absorption record so the symbol and
    // relocation passes below can shift offsets when two objects target the
    // same merged section (e.g. two `.text` translation units).
    std::vector<ObjAbsorbInfo> absorb_log;
    absorb_log.reserve(input.objects.size());
    for (const auto &obj : input.objects) {
      absorb_log.push_back(AbsorbObjectSections(obj, sections, sec_index));
    }

    std::vector<InternalSymbol> symbols;
    // Keep deterministic entry symbol.
    symbols.push_back({"_start", sec_index.count(".text") ? sec_index[".text"] : 0, 0,
                       sections.empty() ? 0 : sections[0].data.size(), true, true});

    std::unordered_map<std::string, std::uint32_t> sym_index;
    sym_index["_start"] = 0;

    for (std::size_t oi = 0; oi < input.objects.size(); ++oi) {
      const auto &obj = input.objects[oi];
      const auto &log = absorb_log[oi];
      for (const auto &sym : obj.symbols) {
        // PE-7-D: a symbol's section may be `.text`/`.rdata`/... — look up
        // the merged section by *name*, not by the owning object's slot,
        // and shift the offset by the object-local base.
        const std::string sym_section = sym.section.empty() ? std::string(".text") : sym.section;
        const auto sec_it = sec_index.find(sym_section);
        const std::uint64_t section_base =
            (sec_it != sec_index.end() && sec_it->second == log.section_index) ? log.base_offset
                                                                                : 0;
        auto existing = sym_index.find(sym.name);
        if (existing != sym_index.end()) {
          // If the existing entry is undefined but this one is
          // defined, upgrade it so the object correctly marks
          // the symbol as a local definition.
          auto &prev = symbols[existing->second];
          if (!prev.defined && sym.is_defined && sec_it != sec_index.end()) {
            prev.section_index = sec_it->second;
            prev.defined = true;
            prev.value = sym.offset + section_base;
            prev.size = sym.size;
          }
          continue;
        }
        InternalSymbol s;
        s.name = sym.name;
        if (sym.is_defined && sec_it != sec_index.end()) {
          s.section_index = sec_it->second;
          s.defined = true;
        } else {
          s.section_index = 0xFFFFFFFF;
          s.defined = false;
        }
        s.value = sym.offset + section_base;
        s.size = sym.size;
        s.global = sym.is_global();
        sym_index[s.name] = static_cast<std::uint32_t>(symbols.size());
        symbols.push_back(std::move(s));
      }
    }

    for (std::size_t oi = 0; oi < input.objects.size(); ++oi) {
      const auto &obj = input.objects[oi];
      const auto &log = absorb_log[oi];
      for (auto rel : obj.relocations) {
        // PE-7-D: a relocation belongs to whatever section the backend says
        // it does (typically `.text`). Resolve via section name, then shift
        // its byte offset by this object's base inside the merged section.
        const std::string rel_section = rel.section.empty() ? std::string(".text") : rel.section;
        const auto si_it = sec_index.find(rel_section);
        if (si_it == sec_index.end())
          continue;
        const std::uint64_t section_base =
            (si_it->second == log.section_index) ? log.base_offset : 0;
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
        rel.offset += section_base;
        sections[si_it->second].relocs.push_back(std::move(rel));
      }
    }

    // PE-7-D: stable-sort the merged sections into the canonical loader
    // order (`.text → .rdata → .data → .bss → others`) and rewrite both
    // `sec_index` and every `InternalSymbol::section_index` so consumers
    // that key off raw indices stay correct after the reordering. Stable
    // sort preserves insertion order inside each priority bucket, which is
    // what backends that emit several custom sections rely on.
    std::vector<std::uint32_t> permutation(sections.size());
    for (std::size_t i = 0; i < permutation.size(); ++i)
      permutation[i] = static_cast<std::uint32_t>(i);
    std::stable_sort(permutation.begin(), permutation.end(),
                     [&](std::uint32_t a, std::uint32_t b) {
                       return SectionPriority(sections[a].name) <
                              SectionPriority(sections[b].name);
                     });
    bool needs_reorder = false;
    for (std::size_t i = 0; i < permutation.size(); ++i) {
      if (permutation[i] != i) {
        needs_reorder = true;
        break;
      }
    }
    if (needs_reorder) {
      std::vector<InternalSection> sorted_sections;
      sorted_sections.reserve(sections.size());
      std::vector<std::uint32_t> old_to_new(sections.size(), 0xFFFFFFFFu);
      for (std::size_t i = 0; i < permutation.size(); ++i) {
        old_to_new[permutation[i]] = static_cast<std::uint32_t>(i);
        sorted_sections.push_back(std::move(sections[permutation[i]]));
      }
      sections = std::move(sorted_sections);
      for (auto &kv : sec_index)
        kv.second = old_to_new[kv.second];
      for (auto &sym : symbols) {
        if (sym.section_index != 0xFFFFFFFFu)
          sym.section_index = old_to_new[sym.section_index];
      }
    }

    out.binary_data = BuildPobjBinary(sections, symbols);

    // Determine the effective object format.  If the user requested a
    // native format (elf/macho/coff) we emit that instead of POBJ so
    // that the platform linker can consume the file directly.  The
    // default ("pobj") is replaced with the host-native format.
    std::string effective_fmt = config.object_format;
    if (effective_fmt.empty() || effective_fmt == "pobj") {
      effective_fmt = HostObjectFormat();
    }

    // Build native object binary when a non-POBJ format is selected.
    std::vector<std::uint8_t> native_obj;
    if (effective_fmt != "pobj") {
      native_obj = BuildNativeObjectBinary(effective_fmt, config.target_arch, sections, symbols);
    }
    const auto &obj_data = native_obj.empty() ? out.binary_data : native_obj;

    out.format = OutputFormat::kStaticLib;

    std::string out_path = config.output_file;
    if (out_path.empty())
      out_path = "a.out";

    const std::string ext = ObjectExtension(effective_fmt);
    std::string obj_out = config.emit_obj_path;
    if (obj_out.empty()) {
      if (config.mode == "link") {
        obj_out = out_path + ext;
      } else {
        obj_out = (out_path == "a.out") ? (out_path + ext) : out_path;
      }
    }
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
    ofs.write(reinterpret_cast<const char *>(obj_data.data()),
              static_cast<std::streamsize>(obj_data.size()));
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
        ir_ofs << "; staged pipeline wrote backend output (IR text emitted in driver-level "
                  "diagnostics mode)\n";
      }
    }

    if (config.mode == "link") {
      LinkerChoice choice = SelectAvailableLinker(effective_fmt, config.polyld_path);
      if (choice.command_template.empty()) {
        // No linker available at all; keep the object and fail the requested
        // link operation with a structured diagnostic.
        diagnostics.ReportError(
            core::SourceLoc{"<packaging>", 1, 1}, frontends::ErrorCode::kUnresolvedSymbol,
            "no linker available for format '" + effective_fmt +
                "' (tried platform tools and bundled polyld); object kept at: " + obj_out);
        AppendDiagnostics(diagnostics, out.packaging_diagnostics);
        out.success = false;
        return out;
      } else {
        std::string cmd =
            ExpandLinkCommand(choice, obj_out, out_path, config.ploy_desc_file, config.aux_dir);
        // BIN-7: when the chosen linker is the bundled polyld, forward
        // the resolved target-triple / container / subsystem / entry
        // descriptors so the linker reaches the same conclusion as the
        // driver instead of re-deriving them from the host macros.
        if (choice.display_name.rfind("polyld", 0) == 0) {
          cmd += " --target=" + config.target_triple.str();
          cmd += " --container=";
          switch (config.container) {
            case ::polyglot::common::BinaryContainer::kAuto:  cmd += "auto";  break;
            case ::polyglot::common::BinaryContainer::kELF:   cmd += "elf";   break;
            case ::polyglot::common::BinaryContainer::kPE:    cmd += "pe";    break;
            case ::polyglot::common::BinaryContainer::kMachO: cmd += "macho"; break;
            case ::polyglot::common::BinaryContainer::kWasm:  cmd += "wasm";  break;
          }
          if (!config.subsystem.empty()) {
            cmd += " --subsystem=" + config.subsystem;
          }
          if (!config.entry_symbol.empty()) {
            cmd += " --entry " + config.entry_symbol;
          }
        }
        if (config.verbose) {
          std::cerr << "[polyc] Invoking " << choice.display_name << " -> " << out_path << "\n";
        }
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
          diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                  frontends::ErrorCode::kUnresolvedSymbol,
                                  "linker '" + choice.display_name +
                                      "' returned non-zero (object kept at " + obj_out +
                                      "): " + cmd);
          AppendDiagnostics(diagnostics, out.packaging_diagnostics);
          out.success = false;
          return out;
        }
        std::error_code ec;
        if (!std::filesystem::exists(out_path, ec)) {
          diagnostics.ReportError(core::SourceLoc{"<packaging>", 1, 1},
                                  frontends::ErrorCode::kUnresolvedSymbol,
                                  "linker '" + choice.display_name +
                                      "' completed but did not produce output: " + out_path);
          AppendDiagnostics(diagnostics, out.packaging_diagnostics);
          out.success = false;
          return out;
        }
        out.output_path = out_path;
      }
    }

    std::error_code size_ec;
    if (std::filesystem::exists(out.output_path, size_ec)) {
      const auto disk_size = std::filesystem::file_size(out.output_path, size_ec);
      out.file_size = size_ec ? out.binary_data.size() : static_cast<size_t>(disk_size);
    } else {
      out.file_size = out.binary_data.size();
    }
    out.success = true;
    AppendDiagnostics(diagnostics, out.packaging_diagnostics);
    return out;
  }
};

} // namespace

// ---------------------------------------------------------------------------
// BIN-7: bidirectional sync between the canonical target triple and the
// legacy `target_arch` / `target_os` strings on `Config`.  These exist
// so old callers that still poke the strings stay valid while the new
// pipeline + downstream tools converge on `common::TargetTriple`.
// ---------------------------------------------------------------------------
void CompilationContext::Config::SetTargetTriple(
    const ::polyglot::common::TargetTriple &t) {
  target_triple = t;
  using ::polyglot::common::Arch;
  using ::polyglot::common::OS;
  switch (t.arch) {
    case Arch::kX86_64:  target_arch = "x86_64"; break;
    case Arch::kAArch64: target_arch = "arm64";  break;
    case Arch::kX86:     target_arch = "x86";    break;
    case Arch::kArm:     target_arch = "arm";    break;
    case Arch::kRiscv32: target_arch = "riscv32";break;
    case Arch::kRiscv64: target_arch = "riscv64";break;
    case Arch::kWasm32:  target_arch = "wasm";   break;
    case Arch::kWasm64:  target_arch = "wasm64"; break;
    default: break;
  }
  switch (t.os) {
    case OS::kLinux:   target_os = "linux"; break;
    case OS::kDarwin:  target_os = "macos"; break;
    case OS::kWindows: target_os = "windows"; break;
    case OS::kFreeBSD: target_os = "freebsd"; break;
    case OS::kWasi:    target_os = "wasi"; break;
    case OS::kNone:    target_os = "none"; break;
    case OS::kUnknown: /* leave as-is */ break;
  }
}

void CompilationContext::Config::SetTargetOs(const std::string &os) {
  target_os = os;
  // Fold the legacy short OS name into a canonical triple via the
  // shared parser so we never duplicate the OS-name table.  Pick a
  // sensible default arch when target_arch is empty.
  std::string arch_part = target_arch.empty() ? std::string{"x86_64"} : target_arch;
  if (arch_part == "arm64") arch_part = "aarch64";
  std::string spec;
  if (os == "windows")     spec = arch_part + "-pc-windows-msvc";
  else if (os == "macos" || os == "darwin")
                           spec = arch_part + "-apple-darwin";
  else if (os == "linux")  spec = arch_part + "-unknown-linux-gnu";
  else if (os == "wasi" || os == "wasm")
                           spec = "wasm32-wasi";
  if (!spec.empty()) {
    auto r = ::polyglot::common::ParseTargetTriple(spec);
    if (r.ok()) target_triple = *r.triple;
  }
}

CompilationPipeline::CompilationPipeline(CompilationContext::Config config) {
  context_.config = std::move(config);

  // ---- BIN-7: keep target_triple / target_os / target_arch in sync.
  // The driver may have populated any subset of those.  Resolve to a
  // single canonical triple via the dedicated setters defined just
  // below, then derive the legacy strings from that.
  using ::polyglot::common::Arch;
  using ::polyglot::common::HostTriple;
  using ::polyglot::common::OS;
  if (context_.config.target_triple.arch != Arch::kUnknown ||
      context_.config.target_triple.os   != OS::kUnknown) {
    // Caller already provided a triple — fold it into legacy fields.
    context_.config.SetTargetTriple(context_.config.target_triple);
  } else if (!context_.config.target_os.empty()) {
    context_.config.SetTargetOs(context_.config.target_os);
  } else {
    context_.config.SetTargetTriple(HostTriple());
  }

  context_.package_cache = std::make_shared<ploy::PackageDiscoveryCache>();
  frontend_stage_ = CreateFrontendStage();
  semantic_stage_ = std::make_unique<DefaultSemanticStage>(context_.config.strict_mode);
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
  context_.timings.push_back(
      {"frontend", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.frontend_output->success;
}

bool CompilationPipeline::RunSemantic() {
  if (!context_.frontend_output.has_value())
    return false;

  if (context_.config.package_index && context_.frontend_output->ast) {
    std::unordered_set<std::string> seen_languages;
    std::vector<std::string> languages;
    std::vector<ploy::VenvConfig> venvs;

    for (const auto &decl : context_.frontend_output->ast->declarations) {
      auto import = std::dynamic_pointer_cast<ploy::ImportDecl>(decl);
      if (!import || import->language.empty() || import->package_name.empty())
        continue;
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
      // Forward CLI-supplied per-language project roots through the
      // VenvConfig channel so cargo metadata / mvn / gradle pick the
      // right working directory.
      if (!context_.config.rust_crate_dir.empty()) {
        ploy::VenvConfig vc;
        vc.language = "rust";
        vc.venv_path = context_.config.rust_crate_dir;
        vc.manager = ploy::VenvConfigDecl::ManagerKind::kVenv;
        venvs.push_back(std::move(vc));
      }
      indexer.BuildIndex(languages, venvs);
    }
  }

  auto start = high_resolution_clock::now();
  context_.semantic_db = semantic_stage_->Run(*context_.frontend_output, *context_.diagnostics,
                                              context_.package_cache);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"semantic-db", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.semantic_db->success;
}

bool CompilationPipeline::RunMarshalPlan() {
  if (!context_.semantic_db.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.marshal_plan = marshal_plan_stage_->Run(*context_.semantic_db, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"marshal-plan", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.marshal_plan->success;
}

bool CompilationPipeline::RunBridgeGeneration() {
  if (!context_.marshal_plan.has_value() || !context_.semantic_db.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.bridge_output = bridge_generation_stage_->Run(
      *context_.marshal_plan, *context_.semantic_db, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"bridge-generation", std::chrono::duration<double, std::milli>(end - start).count()});
  if (!context_.bridge_output->success)
    return false;

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

      // Emit LINK entries (from sema) with MAP_TYPE sub-entries
      for (const auto &entry : context_.semantic_db->link_entries) {
        ofs << "LINK " << entry.target_language << " " << entry.source_language << " "
            << entry.target_symbol << " " << entry.source_symbol << "\n";
        for (const auto &m : entry.param_mappings) {
          ofs << "MAP_TYPE";
          if (!m.source_language.empty())
            ofs << " " << m.source_language;
          else
            ofs << " " << entry.target_language;
          ofs << "::" << m.source_type;
          if (!m.target_language.empty())
            ofs << " " << m.target_language;
          else
            ofs << " " << entry.source_language;
          ofs << "::" << m.target_type << "\n";
        }
      }

      // Emit CALL descriptors (from lowering, carried through marshal plan)
      for (const auto &cp : context_.marshal_plan->call_plans) {
        // Mirror the mangling rule from `MangleStubName` in ploy lowering:
        // include a `_v<sanitized_version>_` segment when a version is
        // pinned so polyld can resolve the right versioned bridge.
        std::string stub_name =
            "__ploy_bridge_" + cp.target_language + "_" + cp.source_language + "_";
        if (!cp.lang_version.empty()) {
          stub_name += "v";
          for (char c : cp.lang_version) {
            stub_name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
          }
          stub_name.push_back('_');
        }
        stub_name += cp.target_function;
        ofs << "CALL " << stub_name << " " << cp.source_language << " " << cp.target_language << " "
            << cp.source_function << " " << cp.target_function << "\n";
        if (!cp.lang_version.empty()) {
          ofs << "VERSION " << cp.source_language << " " << cp.lang_version << "\n";
        }
      }

      // Emit SYMBOL entries derived from sema signatures so polyld can
      // reconstruct CrossLangSymbol without re-running sema.
      std::unordered_set<std::string> seen;
      auto emit_sym = [&](const std::string &name, const std::string &lang) {
        std::string key = lang + "::" + name;
        if (!seen.insert(key).second)
          return;
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
  if (!context_.semantic_db.has_value() || !context_.bridge_output.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.backend_output = backend_stage_->Run(*context_.semantic_db, *context_.bridge_output,
                                                context_.config, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"backend", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.backend_output->success;
}

bool CompilationPipeline::RunPackaging() {
  if (!context_.backend_output.has_value())
    return false;
  auto start = high_resolution_clock::now();
  context_.packaging_output =
      packaging_stage_->Run(*context_.backend_output, context_.config, *context_.diagnostics);
  auto end = high_resolution_clock::now();
  context_.timings.push_back(
      {"packaging", std::chrono::duration<double, std::milli>(end - start).count()});
  return context_.packaging_output->success;
}

PackagingOutput *CompilationPipeline::GetFinalOutput() {
  if (!context_.packaging_output.has_value())
    return nullptr;
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

} // namespace polyglot::compilation
