/**
 * @file     import_resolver.cpp
 * @brief    JavaScript / TypeScript module resolver implementation.
 *
 * @ingroup  Frontend / JavaScript
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "frontends/javascript/include/javascript_import_resolver.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "frontends/javascript/include/javascript_ast.h"
#include "frontends/javascript/include/javascript_lexer.h"
#include "frontends/javascript/include/javascript_parser.h"

namespace fs = std::filesystem;

namespace polyglot::javascript {

namespace {

constexpr const char *kProbeExtensions[] = {".d.ts", ".ts", ".mjs", ".cjs", ".js", ".json"};

std::string ReadFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

bool FileExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool DirExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// Map a JS/TS TypeNode to a core::Type — kept inline (mirrors the version
// in javascript_frontend.cpp; we keep a private copy to avoid leaking it
// through a header).
core::Type JsTypeToCore(const std::shared_ptr<TypeNode> &t) {
    if (!t) return core::Type::Any();
    if (auto nt = std::dynamic_pointer_cast<NamedType>(t)) {
        const std::string &n = nt->name;
        if (n == "number" || n == "Number") return core::Type::Float(64);
        if (n == "boolean" || n == "Boolean" || n == "bool") return core::Type::Bool();
        if (n == "string" || n == "String") return core::Type::String();
        if (n == "void" || n == "undefined" || n == "null") return core::Type::Void();
        if (n == "any" || n == "unknown" || n == "object" || n == "*") return core::Type::Any();
        return core::Type{core::TypeKind::kClass, n, "javascript"};
    }
    if (auto gt = std::dynamic_pointer_cast<GenericType>(t)) {
        if ((gt->name == "Array" || gt->name == "ReadonlyArray") && !gt->args.empty())
            return core::Type::Array(JsTypeToCore(gt->args[0]));
        if (gt->name == "Promise" && !gt->args.empty()) return JsTypeToCore(gt->args[0]);
        return core::Type{core::TypeKind::kClass, gt->name, "javascript"};
    }
    return core::Type::Any();
}

// Best-effort `package.json` field extraction.  We avoid dragging in the
// project's nlohmann_json dependency at the frontend layer — the fields
// we care about (`name`, `version`, `main`, `module`, `types`, `typings`)
// are always plain top-level strings in well-formed package.json files.
std::string ExtractJsonString(const std::string &json, const std::string &key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        std::size_t after = pos + needle.size();
        // Skip whitespace and ':'.
        while (after < json.size() && std::isspace(static_cast<unsigned char>(json[after]))) ++after;
        if (after >= json.size() || json[after] != ':') { pos = after; continue; }
        ++after;
        while (after < json.size() && std::isspace(static_cast<unsigned char>(json[after]))) ++after;
        if (after >= json.size() || json[after] != '"') { pos = after; continue; }
        std::size_t end = after + 1;
        std::string out;
        while (end < json.size() && json[end] != '"') {
            if (json[end] == '\\' && end + 1 < json.size()) {
                char nx = json[end + 1];
                if (nx == 'n') out.push_back('\n');
                else if (nx == 't') out.push_back('\t');
                else if (nx == '"' || nx == '\\' || nx == '/') out.push_back(nx);
                else out.push_back(nx);
                end += 2; continue;
            }
            out.push_back(json[end]); ++end;
        }
        return out;
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------

JsImportResolver::JsImportResolver(std::string project_dir,
                                   std::vector<std::string> extra_node_modules,
                                   frontends::Diagnostics &diagnostics)
    : project_dir_(std::move(project_dir)),
      extra_node_modules_(std::move(extra_node_modules)),
      diagnostics_(diagnostics) {}

std::string JsImportResolver::CacheKey(const std::string &specifier,
                                       const std::string &importer_dir) {
    if (!specifier.empty() && (specifier[0] == '.' || specifier[0] == '/'))
        return importer_dir + "::" + specifier;
    return "::" + specifier;  // bare specifiers are global per resolver
}

std::string JsImportResolver::ProbeExtensions(const std::string &path_no_ext) const {
    for (const char *ext : kProbeExtensions) {
        fs::path p = path_no_ext + ext;
        if (FileExists(p)) return p.string();
    }
    return {};
}

std::string JsImportResolver::ResolveRelative(const std::string &specifier,
                                               const std::string &importer_dir) const {
    fs::path base = importer_dir.empty() ? fs::path(".") : fs::path(importer_dir);
    fs::path candidate = (base / specifier).lexically_normal();
    if (FileExists(candidate)) return candidate.string();
    if (auto with_ext = ProbeExtensions(candidate.string()); !with_ext.empty()) return with_ext;
    if (DirExists(candidate)) {
        // Try package.json in the directory, then index.*
        if (auto entry = ResolvePackageEntry(candidate.string()); !entry.empty()) return entry;
        if (auto idx = ProbeExtensions((candidate / "index").string()); !idx.empty()) return idx;
    }
    return {};
}

std::string JsImportResolver::ResolvePackageEntry(const std::string &pkg_dir) const {
    fs::path pj = fs::path(pkg_dir) / "package.json";
    if (!FileExists(pj)) return {};
    std::string raw = ReadFile(pj);
    // Prefer .d.ts: types > typings, then module > main.
    for (const char *field : {"types", "typings", "module", "main"}) {
        std::string v = ExtractJsonString(raw, field);
        if (v.empty()) continue;
        fs::path entry = (fs::path(pkg_dir) / v).lexically_normal();
        if (FileExists(entry)) return entry.string();
        if (auto with_ext = ProbeExtensions(entry.string()); !with_ext.empty()) return with_ext;
        if (DirExists(entry)) {
            if (auto idx = ProbeExtensions((entry / "index").string()); !idx.empty()) return idx;
        }
    }
    if (auto idx = ProbeExtensions((fs::path(pkg_dir) / "index").string()); !idx.empty()) return idx;
    return {};
}

std::string JsImportResolver::ResolveBare(const std::string &specifier,
                                           const std::string &importer_dir,
                                           std::string &out_pkg_name,
                                           std::string &out_pkg_version) const {
    // Split scope from sub-path: "@scope/pkg/sub" -> pkg_name="@scope/pkg", remainder="sub".
    std::string pkg_name;
    std::string remainder;
    if (!specifier.empty() && specifier[0] == '@') {
        auto first = specifier.find('/');
        if (first == std::string::npos) { pkg_name = specifier; }
        else {
            auto second = specifier.find('/', first + 1);
            if (second == std::string::npos) pkg_name = specifier;
            else { pkg_name = specifier.substr(0, second); remainder = specifier.substr(second + 1); }
        }
    } else {
        auto first = specifier.find('/');
        if (first == std::string::npos) pkg_name = specifier;
        else { pkg_name = specifier.substr(0, first); remainder = specifier.substr(first + 1); }
    }

    auto walk_node_modules = [&](fs::path start) -> std::string {
        for (fs::path cur = start; ; cur = cur.parent_path()) {
            fs::path nm = cur / "node_modules" / pkg_name;
            if (DirExists(nm)) {
                fs::path pj = nm / "package.json";
                if (FileExists(pj)) {
                    std::string raw = ReadFile(pj);
                    out_pkg_name = ExtractJsonString(raw, "name");
                    if (out_pkg_name.empty()) out_pkg_name = pkg_name;
                    out_pkg_version = ExtractJsonString(raw, "version");
                }
                if (!remainder.empty()) {
                    fs::path full = (nm / remainder).lexically_normal();
                    if (FileExists(full)) return full.string();
                    if (auto we = ProbeExtensions(full.string()); !we.empty()) return we;
                    if (DirExists(full)) {
                        if (auto idx = ProbeExtensions((full / "index").string()); !idx.empty()) return idx;
                    }
                    return {};
                }
                return ResolvePackageEntry(nm.string());
            }
            if (cur == cur.root_path()) break;
            if (cur.parent_path() == cur) break;
        }
        return {};
    };

    if (!importer_dir.empty()) {
        if (auto r = walk_node_modules(importer_dir); !r.empty()) return r;
    }
    if (!project_dir_.empty()) {
        if (auto r = walk_node_modules(project_dir_); !r.empty()) return r;
    }
    for (const auto &root : extra_node_modules_) {
        fs::path nm = fs::path(root) / pkg_name;
        if (DirExists(nm)) {
            fs::path pj = nm / "package.json";
            if (FileExists(pj)) {
                std::string raw = ReadFile(pj);
                out_pkg_name = ExtractJsonString(raw, "name");
                if (out_pkg_name.empty()) out_pkg_name = pkg_name;
                out_pkg_version = ExtractJsonString(raw, "version");
            }
            if (!remainder.empty()) {
                fs::path full = (nm / remainder).lexically_normal();
                if (FileExists(full)) return full.string();
                if (auto we = ProbeExtensions(full.string()); !we.empty()) return we;
                if (DirExists(full)) {
                    if (auto idx = ProbeExtensions((full / "index").string()); !idx.empty()) return idx;
                }
                continue;
            }
            return ResolvePackageEntry(nm.string());
        }
        // Some configurations use a flat layout where each name is direct.
        fs::path direct = fs::path(root) / specifier;
        if (auto we = ProbeExtensions(direct.string()); !we.empty()) {
            out_pkg_name = pkg_name;
            return we;
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Module loading: parse the resolved file and harvest top-level exports.
// ---------------------------------------------------------------------------
std::unique_ptr<JsModule> JsImportResolver::LoadModule(const std::string &specifier,
                                                       const std::string &resolved_path,
                                                       const std::string &pkg_name,
                                                       const std::string &pkg_version) {
    auto m = std::make_unique<JsModule>();
    m->specifier = specifier;
    m->resolved_path = resolved_path;
    m->package_name = pkg_name;
    m->package_version = pkg_version;
    m->is_typescript_decl = (resolved_path.size() >= 5 &&
                             resolved_path.compare(resolved_path.size() - 5, 5, ".d.ts") == 0);
    m->is_commonjs = (resolved_path.size() >= 4 &&
                      resolved_path.compare(resolved_path.size() - 4, 4, ".cjs") == 0);

    std::string source = ReadFile(resolved_path);
    if (source.empty()) return m;

    // Reuse the JS parser to harvest exports.  The same parser handles
    // both .js and .d.ts because TypeScript declarations are a strict
    // syntactic superset of the JS forms we care about (function/class/
    // const declarations with optional type annotations the parser
    // already understands via JSDoc-style comments).
    frontends::Diagnostics local_diag;
    JsLexer lexer(source, resolved_path);
    JsParser parser(lexer, local_diag);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return m;

    auto qualify = [&](const std::string &n) {
        return (pkg_name.empty() ? fs::path(resolved_path).stem().string() : pkg_name) + "." + n;
    };

    auto record_function = [&](const std::string &name,
                               const std::vector<ArrowFunction::Param> &params,
                               const std::shared_ptr<TypeNode> &ret_t,
                               bool is_default) {
        JsSymbol s;
        s.name = name;
        s.qualified_name = qualify(name);
        s.kind = JsSymbolKind::kFunction;
        s.is_default = is_default;
        for (const auto &p : params) {
            s.param_names.push_back(p.name);
            s.param_types.push_back(JsTypeToCore(p.type));
        }
        s.return_type = ret_t ? JsTypeToCore(ret_t) : core::Type::Any();
        m->exports[name] = std::move(s);
    };
    auto record_class = [&](const std::string &name, bool is_default) {
        JsSymbol s;
        s.name = name;
        s.qualified_name = qualify(name);
        s.kind = JsSymbolKind::kClass;
        s.is_default = is_default;
        s.return_type = core::Type{core::TypeKind::kClass, name, "javascript"};
        m->exports[name] = std::move(s);
    };
    auto record_const = [&](const std::string &name, const std::shared_ptr<TypeNode> &t) {
        JsSymbol s;
        s.name = name;
        s.qualified_name = qualify(name);
        s.kind = JsSymbolKind::kConstant;
        s.return_type = JsTypeToCore(t);
        m->exports[name] = std::move(s);
    };

    for (const auto &stmt : module->body) {
        if (auto e = std::dynamic_pointer_cast<ExportDecl>(stmt)) {
            if (e->declaration) {
                if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(e->declaration)) {
                    record_function(fd->name, fd->params, fd->return_type, e->is_default);
                } else if (auto cd = std::dynamic_pointer_cast<ClassDecl>(e->declaration)) {
                    record_class(cd->name, e->is_default);
                } else if (auto vd = std::dynamic_pointer_cast<VariableDecl>(e->declaration)) {
                    for (const auto &d : vd->decls) record_const(d.name, d.type);
                }
            }
            for (const auto &spec : e->specifiers) {
                JsSymbol s;
                s.name = spec.exported;
                s.qualified_name = qualify(spec.exported);
                s.kind = JsSymbolKind::kVariable;
                m->exports[spec.exported] = std::move(s);
            }
        }
        // For .d.ts and TypeScript-style top-level `export function ...`
        // the parser may emit a plain FunctionDecl/ClassDecl whose
        // `exported` flag is set.  Honour that path too.
        if (auto fd = std::dynamic_pointer_cast<FunctionDecl>(stmt)) {
            if (fd->exported || fd->exported_default)
                record_function(fd->name, fd->params, fd->return_type, fd->exported_default);
        }
        if (auto cd = std::dynamic_pointer_cast<ClassDecl>(stmt)) {
            if (cd->exported || cd->exported_default)
                record_class(cd->name, cd->exported_default);
        }
    }
    return m;
}

const JsModule *JsImportResolver::Resolve(const std::string &specifier,
                                          const std::string &importer_dir) {
    std::string key = CacheKey(specifier, importer_dir);
    if (auto it = by_key_.find(key); it != by_key_.end()) return it->second;

    std::string resolved;
    std::string pkg_name;
    std::string pkg_version;
    if (!specifier.empty() && (specifier[0] == '.' || specifier[0] == '/')) {
        resolved = ResolveRelative(specifier, importer_dir);
    } else {
        resolved = ResolveBare(specifier, importer_dir, pkg_name, pkg_version);
    }

    if (resolved.empty()) {
        diagnostics_.Report(core::SourceLoc{},
            "javascript module not found: '" + specifier +
            "' (importer_dir='" + importer_dir +
            "', node_modules roots=" + std::to_string(extra_node_modules_.size()) + ")");
        return nullptr;
    }

    // De-duplicate by absolute path so two different specifiers pointing
    // at the same file share one JsModule entry.
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(resolved, ec);
    std::string canon_str = canonical.empty() ? resolved : canonical.string();
    if (auto it = by_path_.find(canon_str); it != by_path_.end()) {
        by_key_[key] = it->second;
        return it->second;
    }

    auto owned = LoadModule(specifier, canon_str, pkg_name, pkg_version);
    JsModule *raw = owned.get();
    by_path_[canon_str] = raw;
    by_key_[key] = raw;
    modules_.push_back(std::move(owned));
    return raw;
}

const JsModule *JsImportResolver::Lookup(const std::string &specifier,
                                          const std::string &importer_dir) const {
    auto it = by_key_.find(CacheKey(specifier, importer_dir));
    return (it == by_key_.end()) ? nullptr : it->second;
}

}  // namespace polyglot::javascript
