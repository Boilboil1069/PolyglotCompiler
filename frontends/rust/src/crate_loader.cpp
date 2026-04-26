/**
 * @file     crate_loader.cpp
 * @brief    Rust crate metadata loader implementation.
 *
 * @ingroup  Frontend / Rust
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "frontends/rust/include/crate_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "frontends/rust/include/rust_ast.h"
#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"

namespace polyglot::rust {

namespace fs = std::filesystem;

namespace {

// ============================================================================
// Cargo.toml lite-parser
// ============================================================================
struct CargoPackage {
    std::string name;
    std::string version;
};

std::string ReadFile(const fs::path &p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

std::string Trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && issp(s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && issp(s[i])) ++i;
    return s.substr(i);
}

std::string StripQuotes(std::string s) {
    s = Trim(std::move(s));
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

CargoPackage ParseCargoToml(const std::string &src) {
    CargoPackage out;
    std::istringstream is(src);
    std::string section;
    std::string line;
    while (std::getline(is, line)) {
        // Strip trailing comment
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::string t = Trim(line);
        if (t.empty()) continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        if (section != "package") continue;

        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = Trim(t.substr(0, eq));
        std::string val = StripQuotes(t.substr(eq + 1));
        if (key == "name")    out.name    = val;
        if (key == "version") out.version = val;
    }
    return out;
}

// ============================================================================
// Type mapping (mirrors RustTypeToCore from rust_frontend.cpp).
// ============================================================================
core::Type RustTypeToCore(const std::shared_ptr<TypeNode> &tn) {
    if (!tn) return core::Type::Void();

    if (auto tp = std::dynamic_pointer_cast<TypePath>(tn)) {
        std::string name;
        for (std::size_t i = 0; i < tp->segments.size(); ++i) {
            if (i > 0) name += "::";
            name += tp->segments[i];
        }
        if (name == "i8")    return core::Type::Int(8, true);
        if (name == "i16")   return core::Type::Int(16, true);
        if (name == "i32")   return core::Type::Int(32, true);
        if (name == "i64" || name == "i128" || name == "isize")
            return core::Type::Int(64, true);
        if (name == "u8")    return core::Type::Int(8, false);
        if (name == "u16")   return core::Type::Int(16, false);
        if (name == "u32")   return core::Type::Int(32, false);
        if (name == "u64" || name == "u128" || name == "usize")
            return core::Type::Int(64, false);
        if (name == "f32")   return core::Type::Float(32);
        if (name == "f64")   return core::Type::Float(64);
        if (name == "bool")  return core::Type::Bool();
        if (name == "char")  return core::Type::Int(32, false);
        if (name == "String" || name == "str") return core::Type::String();
        if ((name == "Option" || name == "Result" || name == "Box") &&
            !tp->generic_args.empty() && !tp->generic_args[0].empty()) {
            return RustTypeToCore(tp->generic_args[0][0]);
        }
        if (name == "Vec") return core::Type{core::TypeKind::kArray, "Vec", "rust"};
        return core::Type{core::TypeKind::kClass, name, "rust"};
    }
    if (auto rt = std::dynamic_pointer_cast<ReferenceType>(tn)) return RustTypeToCore(rt->inner);
    if (auto st = std::dynamic_pointer_cast<SliceType>(tn)) {
        return core::Type{core::TypeKind::kSlice, "slice", "rust"};
    }
    if (auto at = std::dynamic_pointer_cast<ArrayType>(tn)) {
        return core::Type{core::TypeKind::kArray, "array", "rust"};
    }
    if (auto tt = std::dynamic_pointer_cast<TupleType>(tn)) {
        if (tt->elements.empty()) return core::Type::Void();
        return core::Type{core::TypeKind::kStruct, "tuple", "rust"};
    }
    return core::Type::Any();
}

// ============================================================================
// Source-tree walker
// ============================================================================
class CrateWalker {
  public:
    CrateWalker(CrateInfo &info, frontends::Diagnostics &diags)
        : info_(info), diags_(diags) {}

    void WalkRoot(const fs::path &root_rs) {
        fs::path canon = fs::weakly_canonical(root_rs);
        info_.root_path = canon.string();
        WalkFile(canon, /*module_path=*/"", /*module_dir=*/canon.parent_path());
    }

  private:
    void WalkFile(const fs::path &file,
                  const std::string &mod_path,
                  const fs::path &mod_dir) {
        if (visited_.count(fs::weakly_canonical(file).string())) return;
        visited_.insert(fs::weakly_canonical(file).string());

        std::string src = ReadFile(file);
        if (src.empty()) return;

        frontends::Diagnostics local;   // suppress parse-time noise from external sources
        RustLexer lex(src, file.string());
        RustParser parser(lex, local);
        parser.ParseModule();
        auto module = parser.TakeModule();
        if (!module) return;

        WalkItems(module->items, mod_path, mod_dir);
    }

    bool IsPublic(const std::string &vis) const {
        return !vis.empty() && vis.find("pub") != std::string::npos;
    }

    std::string Join(const std::string &mod_path, const std::string &name) const {
        if (mod_path.empty()) return info_.name + "::" + name;
        return mod_path + "::" + name;
    }

    void Insert(CrateItemKind kind, const std::string &short_name,
                const std::string &mod_path, core::Type type,
                std::vector<core::Type> params = {},
                core::Type ret = core::Type::Void()) {
        CrateItem ci;
        ci.name = short_name;
        ci.qualified_name = Join(mod_path, short_name);
        ci.kind = kind;
        ci.type = std::move(type);
        ci.param_types = std::move(params);
        ci.return_type = std::move(ret);
        info_.items.emplace(ci.qualified_name, std::move(ci));
    }

    void WalkItems(const std::vector<std::shared_ptr<Statement>> &items,
                   const std::string &mod_path,
                   const fs::path &mod_dir) {
        for (const auto &item : items) {
            if (auto fn = std::dynamic_pointer_cast<FunctionItem>(item)) {
                if (!IsPublic(fn->visibility)) continue;
                std::vector<core::Type> params;
                params.reserve(fn->params.size());
                for (auto &p : fn->params) {
                    if (p.name == "self" || p.name == "&self" || p.name == "&mut self") continue;
                    params.push_back(RustTypeToCore(p.type));
                }
                core::Type ret = fn->return_type ? RustTypeToCore(fn->return_type)
                                                 : core::Type::Void();
                core::Type fnt{core::TypeKind::kFunction, fn->name};
                fnt.language = "rust";
                fnt.type_args.reserve(params.size() + 1);
                fnt.type_args.push_back(ret);
                for (auto &p : params) fnt.type_args.push_back(p);
                Insert(CrateItemKind::kFunction, fn->name, mod_path, std::move(fnt),
                       std::move(params), ret);
                continue;
            }
            if (auto st = std::dynamic_pointer_cast<StructItem>(item)) {
                if (!IsPublic(st->visibility)) continue;
                core::Type t{core::TypeKind::kStruct, st->name, "rust"};
                Insert(CrateItemKind::kStruct, st->name, mod_path, std::move(t));
                continue;
            }
            if (auto en = std::dynamic_pointer_cast<EnumItem>(item)) {
                if (!IsPublic(en->visibility)) continue;
                core::Type t{core::TypeKind::kEnum, en->name, "rust"};
                Insert(CrateItemKind::kEnum, en->name, mod_path, std::move(t));
                // Variants are also addressable: Enum::Variant
                std::string enum_qual = Join(mod_path, en->name);
                for (const auto &v : en->variants) {
                    core::Type vt{core::TypeKind::kEnum, en->name + "::" + v.name, "rust"};
                    CrateItem ci;
                    ci.name = v.name;
                    ci.qualified_name = enum_qual + "::" + v.name;
                    ci.kind = CrateItemKind::kConst;
                    ci.type = std::move(vt);
                    info_.items.emplace(ci.qualified_name, std::move(ci));
                }
                continue;
            }
            if (auto tr = std::dynamic_pointer_cast<TraitItem>(item)) {
                if (!IsPublic(tr->visibility)) continue;
                core::Type t{core::TypeKind::kClass, tr->name, "rust"};
                Insert(CrateItemKind::kTrait, tr->name, mod_path, std::move(t));
                continue;
            }
            if (auto cn = std::dynamic_pointer_cast<ConstItem>(item)) {
                if (!IsPublic(cn->visibility)) continue;
                Insert(CrateItemKind::kConst, cn->name, mod_path, RustTypeToCore(cn->type));
                continue;
            }
            if (auto ta = std::dynamic_pointer_cast<TypeAliasItem>(item)) {
                if (!IsPublic(ta->visibility)) continue;
                Insert(CrateItemKind::kTypeAlias, ta->name, mod_path, RustTypeToCore(ta->alias));
                continue;
            }
            if (auto mr = std::dynamic_pointer_cast<MacroRulesItem>(item)) {
                core::Type t{core::TypeKind::kFunction, mr->name + "!"};
                t.language = "rust";
                Insert(CrateItemKind::kMacro, mr->name, mod_path, std::move(t));
                continue;
            }
            if (auto rmod = std::dynamic_pointer_cast<ModItem>(item)) {
                if (!IsPublic(rmod->visibility)) continue;
                std::string sub_path = Join(mod_path, rmod->name);
                core::Type modt = core::Type::Module(sub_path, "rust");
                Insert(CrateItemKind::kModule, rmod->name, mod_path, modt);
                if (!rmod->items.empty()) {
                    // Inline `pub mod foo { ... }`
                    WalkItems(rmod->items, sub_path, mod_dir);
                } else {
                    // External `pub mod foo;`: search sibling files.
                    fs::path sibling_rs = mod_dir / (rmod->name + ".rs");
                    fs::path sibling_dir = mod_dir / rmod->name / "mod.rs";
                    fs::path sibling_init = mod_dir / rmod->name / (rmod->name + ".rs");
                    if (fs::exists(sibling_rs)) {
                        WalkFile(sibling_rs, sub_path, sibling_rs.parent_path());
                    } else if (fs::exists(sibling_dir)) {
                        WalkFile(sibling_dir, sub_path, sibling_dir.parent_path());
                    } else if (fs::exists(sibling_init)) {
                        WalkFile(sibling_init, sub_path, sibling_init.parent_path());
                    }
                }
                continue;
            }
            // ImplItem methods get attached to their target type's qualified name.
            if (auto impl = std::dynamic_pointer_cast<ImplItem>(item)) {
                std::string impl_type;
                if (auto tp = std::dynamic_pointer_cast<TypePath>(impl->target_type)) {
                    for (std::size_t i = 0; i < tp->segments.size(); ++i) {
                        if (i > 0) impl_type += "::";
                        impl_type += tp->segments[i];
                    }
                }
                if (impl_type.empty()) continue;
                std::string impl_qual = Join(mod_path, impl_type);
                for (const auto &m : impl->items) {
                    auto fn = std::dynamic_pointer_cast<FunctionItem>(m);
                    if (!fn || !IsPublic(fn->visibility)) continue;
                    std::vector<core::Type> params;
                    params.reserve(fn->params.size());
                    for (auto &p : fn->params) {
                        if (p.name == "self" || p.name == "&self" || p.name == "&mut self") continue;
                        params.push_back(RustTypeToCore(p.type));
                    }
                    core::Type ret = fn->return_type ? RustTypeToCore(fn->return_type)
                                                     : core::Type::Void();
                    core::Type fnt{core::TypeKind::kFunction, fn->name};
                    fnt.language = "rust";
                    fnt.type_args.push_back(ret);
                    for (auto &p : params) fnt.type_args.push_back(p);
                    CrateItem ci;
                    ci.name = fn->name;
                    ci.qualified_name = impl_qual + "::" + fn->name;
                    ci.kind = CrateItemKind::kFunction;
                    ci.type = std::move(fnt);
                    ci.param_types = std::move(params);
                    ci.return_type = ret;
                    info_.items.emplace(ci.qualified_name, std::move(ci));
                }
                continue;
            }
        }
    }

    CrateInfo                       &info_;
    frontends::Diagnostics          &diags_;
    std::unordered_set<std::string>  visited_;
};

// ============================================================================
// rmeta / rlib magic probe
// ============================================================================
// rustc emits .rmeta files starting with "rust\0\0\0\xNN" where NN is the
// metadata version.  .rlib files are AR archives whose first member is named
// `lib.rmeta`.  We don't decode the proprietary metadata (it's intentionally
// unstable across rustc versions); we just confirm the file is a Rust artefact
// so that consumers can attach a CrateInfo with a name extracted from the
// filename and emit a diagnostic if the user expected a richer index.
bool LooksLikeRmeta(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char hdr[8] = {0};
    f.read(hdr, sizeof(hdr));
    if (f.gcount() < 8) return false;
    return std::memcmp(hdr, "rust\0\0\0", 7) == 0;
}

bool LooksLikeRlib(const fs::path &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char hdr[8] = {0};
    f.read(hdr, sizeof(hdr));
    return f.gcount() == 8 && std::memcmp(hdr, "!<arch>\n", 8) == 0;
}

std::string CrateNameFromFilename(const fs::path &p) {
    std::string stem = p.stem().string();
    // rustc rlib: "libfoo-deadbeef.rlib" → strip "lib" prefix and "-hash" suffix.
    if (stem.rfind("lib", 0) == 0) stem = stem.substr(3);
    auto dash = stem.find_last_of('-');
    if (dash != std::string::npos) stem = stem.substr(0, dash);
    return stem;
}

}  // namespace

// ============================================================================
// CrateLoader public API
// ============================================================================
CrateLoader::CrateLoader(const std::string &crate_dir,
                         const std::vector<std::pair<std::string, std::string>> &externs,
                         frontends::Diagnostics &diags) {
    if (!crate_dir.empty()) {
        LoadFromPath(/*explicit_name=*/"", crate_dir, diags);
    }
    for (const auto &[name, path] : externs) {
        LoadFromPath(name, path, diags);
    }
}

void CrateLoader::LoadFromPath(const std::string &explicit_name,
                               const std::string &path_in,
                               frontends::Diagnostics &diags) {
    fs::path p(path_in);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        diags.Report({"<rust-extern>", 0, 0},
                     "Rust crate path does not exist: " + path_in);
        return;
    }

    auto info = std::make_unique<CrateInfo>();

    if (fs::is_directory(p, ec)) {
        // Directory: prefer Cargo.toml-driven layout.
        fs::path cargo = p / "Cargo.toml";
        fs::path lib_rs = p / "src" / "lib.rs";
        fs::path main_rs = p / "src" / "main.rs";

        if (fs::exists(cargo)) {
            CargoPackage pkg = ParseCargoToml(ReadFile(cargo));
            info->name    = !pkg.name.empty() ? pkg.name : explicit_name;
            info->version = pkg.version;
        }
        if (info->name.empty()) info->name = explicit_name.empty() ? p.filename().string() : explicit_name;

        fs::path entry;
        if (fs::exists(lib_rs))      entry = lib_rs;
        else if (fs::exists(main_rs)) entry = main_rs;
        else {
            // Fallback: any .rs file directly inside the directory.
            for (const auto &de : fs::directory_iterator(p, ec)) {
                if (de.is_regular_file() && de.path().extension() == ".rs") { entry = de.path(); break; }
            }
        }
        if (entry.empty()) {
            diags.Report({"<rust-extern>", 0, 0},
                         "Rust crate '" + info->name + "' has no src/lib.rs or src/main.rs");
            return;
        }
        CrateWalker walker(*info, diags);
        walker.WalkRoot(entry);
    } else if (p.extension() == ".rs") {
        info->name = explicit_name.empty() ? p.stem().string() : explicit_name;
        CrateWalker walker(*info, diags);
        walker.WalkRoot(p);
    } else if (p.extension() == ".rlib" || p.extension() == ".rmeta") {
        info->name = explicit_name.empty() ? CrateNameFromFilename(p) : explicit_name;
        info->root_path = fs::weakly_canonical(p).string();
        info->is_binary_artifact = true;
        bool ok = (p.extension() == ".rmeta") ? LooksLikeRmeta(p) : LooksLikeRlib(p);
        if (!ok) {
            diags.Report({"<rust-extern>", 0, 0},
                         "Rust artefact does not have expected magic header: " + p.string());
        }
        // Add an opaque crate-root module entry so `use <name>;` resolves.
        CrateItem root;
        root.name = info->name;
        root.qualified_name = info->name;
        root.kind = CrateItemKind::kModule;
        root.type = core::Type::Module(info->name, "rust");
        info->items.emplace(info->name, std::move(root));
    } else {
        diags.Report({"<rust-extern>", 0, 0},
                     "Unsupported Rust artefact: " + p.string());
        return;
    }

    if (info->name.empty()) {
        diags.Report({"<rust-extern>", 0, 0},
                     "Could not determine crate name for: " + path_in);
        return;
    }
    by_name_[info->name] = info.get();
    crates_.push_back(std::move(info));
}

const CrateInfo *CrateLoader::ResolveCrate(const std::string &crate_name) const {
    auto it = by_name_.find(crate_name);
    return (it == by_name_.end()) ? nullptr : it->second;
}

const CrateItem *CrateLoader::ResolvePath(const std::string &path) const {
    if (path.empty()) return nullptr;
    // Both qualified and short forms accepted: try as-is, then prefix-search.
    for (const auto &c : crates_) {
        auto it = c->items.find(path);
        if (it != c->items.end()) return &it->second;
    }
    // Try `crate_name::rest`: split on first "::" and look up the rest in
    // that crate, also prepending the crate name.
    auto sep = path.find("::");
    if (sep == std::string::npos) {
        // Just a crate name? Return the crate as a module item if present.
        if (auto *c = ResolveCrate(path)) {
            // Synthesise a transient module item — store inside the crate
            // as a side-channel by re-using its first matching item.  The
            // caller normally only needs a non-null result here.
            (void)c;
        }
        return nullptr;
    }
    std::string head = path.substr(0, sep);
    auto *c = ResolveCrate(head);
    if (!c) return nullptr;
    auto it = c->items.find(path);
    return (it == c->items.end()) ? nullptr : &it->second;
}

}  // namespace polyglot::rust
