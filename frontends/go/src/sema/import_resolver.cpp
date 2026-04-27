/**
 * @file     import_resolver.cpp
 * @brief    Go package import resolver implementation.
 *
 * @ingroup  Frontend / Go
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "frontends/go/include/go_import_resolver.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

#include "frontends/go/include/go_ast.h"
#include "frontends/go/include/go_lexer.h"
#include "frontends/go/include/go_parser.h"

namespace fs = std::filesystem;

namespace polyglot::go {

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------
namespace {

std::string ReadFileToString(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

bool StartsWithExportedName(const std::string &n) {
    return !n.empty() && std::isupper(static_cast<unsigned char>(n[0]));
}

// Translate a parsed Go TypeNode into a core::Type, mirroring the logic
// already used by GoLanguageFrontend::Lower (kept separate to avoid a
// header cycle through go_frontend.cpp).
core::Type ToCoreType(const std::shared_ptr<TypeNode> &t) {
    if (!t) return core::Type::Any();
    switch (t->kind) {
        case TypeKind::kPointer: {
            core::Type p{core::TypeKind::kPointer, "ptr"};
            p.type_args.push_back(ToCoreType(t->elem));
            p.language = "go";
            return p;
        }
        case TypeKind::kSlice:     return core::Type::Slice(ToCoreType(t->elem));
        case TypeKind::kArray:     return core::Type::Array(ToCoreType(t->elem));
        case TypeKind::kMap:       return core::Type{core::TypeKind::kStruct, "map", "go"};
        case TypeKind::kChan:      return core::Type{core::TypeKind::kClass, "chan", "go"};
        case TypeKind::kFunc:      return core::Type{core::TypeKind::kFunction, "func", "go"};
        case TypeKind::kInterface: return core::Type::Any();
        case TypeKind::kStruct:    return core::Type{core::TypeKind::kStruct, t->name, "go"};
        case TypeKind::kNamed: break;
    }
    const std::string &n = t->name;
    if (n == "bool")    return core::Type::Bool();
    if (n == "int8")    return core::Type::Int(8, true);
    if (n == "int16")   return core::Type::Int(16, true);
    if (n == "int32" || n == "rune") return core::Type::Int(32, true);
    if (n == "int" || n == "int64")  return core::Type::Int(64, true);
    if (n == "uint8" || n == "byte") return core::Type::Int(8, false);
    if (n == "uint16")  return core::Type::Int(16, false);
    if (n == "uint32")  return core::Type::Int(32, false);
    if (n == "uint" || n == "uint64" || n == "uintptr") return core::Type::Int(64, false);
    if (n == "float32") return core::Type::Float(32);
    if (n == "float64") return core::Type::Float(64);
    if (n == "string")  return core::Type::String();
    if (n == "error")   return core::Type{core::TypeKind::kClass, "error", "go"};
    return core::Type{core::TypeKind::kClass, n, "go"};
}

std::string Trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && issp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && issp(static_cast<unsigned char>(s.back())))  s.pop_back();
    return s;
}

// Strip line/block comments preserving newline structure (sufficient for go.mod).
std::string StripComments(const std::string &src) {
    std::string out;
    out.reserve(src.size());
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') ++i;
            if (i < src.size()) out.push_back('\n');
            continue;
        }
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') out.push_back('\n');
                ++i;
            }
            i += 1;  // for-loop will skip the '/'
            continue;
        }
        out.push_back(src[i]);
    }
    return out;
}

// Detect GOROOT / GOPATH from the environment with sensible fallbacks.
std::string DetectGoroot() {
    if (const char *env = std::getenv("GOROOT"); env && *env) return env;
#ifdef _WIN32
    const char *defaults[] = {"C:/Program Files/Go", "C:/Go"};
#elif defined(__APPLE__)
    const char *defaults[] = {"/usr/local/go", "/opt/homebrew/Cellar/go", "/opt/homebrew/opt/go/libexec"};
#else
    const char *defaults[] = {"/usr/local/go", "/usr/lib/go", "/snap/go/current"};
#endif
    for (const char *p : defaults) {
        std::error_code ec;
        if (fs::exists(fs::path(p) / "src", ec)) return p;
    }
    return {};
}

std::string DetectGopath() {
    if (const char *env = std::getenv("GOPATH"); env && *env) return env;
    if (const char *home = std::getenv(
#ifdef _WIN32
            "USERPROFILE"
#else
            "HOME"
#endif
        ); home && *home) {
        return std::string(home) + "/go";
    }
    return {};
}

}  // namespace

// ---------------------------------------------------------------------------
// GoImportResolver — construction
// ---------------------------------------------------------------------------

GoImportResolver::GoImportResolver(std::string project_dir,
                                   std::vector<std::string> extra_module_paths,
                                   frontends::Diagnostics &diagnostics)
    : project_dir_(std::move(project_dir)),
      module_paths_(std::move(extra_module_paths)),
      diagnostics_(diagnostics) {
    // Compose the search precedence list — reverse-order push because we
    // want the configured order to win when the same path is found in
    // multiple locations.
    if (std::string gopath = DetectGopath(); !gopath.empty()) {
        std::string mod_cache = gopath + "/pkg/mod";
        std::error_code ec;
        if (fs::exists(mod_cache, ec)) module_paths_.push_back(mod_cache);
    }
    if (std::string goroot = DetectGoroot(); !goroot.empty()) {
        std::string std_src = goroot + "/src";
        std::error_code ec;
        if (fs::exists(std_src, ec)) module_paths_.push_back(std_src);
    }

    LoadGoMod();
}

// ---------------------------------------------------------------------------
// go.mod parsing
// ---------------------------------------------------------------------------
void GoImportResolver::LoadGoMod() {
    if (project_dir_.empty()) return;
    fs::path mod_path = fs::path(project_dir_) / "go.mod";
    std::error_code ec;
    if (!fs::exists(mod_path, ec)) return;

    std::string raw = ReadFileToString(mod_path);
    if (raw.empty()) return;
    std::string clean = StripComments(raw);

    std::istringstream iss(clean);
    std::string line;
    bool in_require = false;
    bool in_replace = false;
    while (std::getline(iss, line)) {
        std::string t = Trim(line);
        if (t.empty()) continue;

        if (t == "require (") { in_require = true;  continue; }
        if (t == "replace (") { in_replace = true;  continue; }
        if (t == ")")          { in_require = false; in_replace = false; continue; }

        // Single-line forms are also accepted.
        if (t.rfind("module ", 0) == 0) {
            manifest_.module_path = Trim(t.substr(7));
            // Strip surrounding quotes if present.
            if (manifest_.module_path.size() >= 2 &&
                manifest_.module_path.front() == '"' && manifest_.module_path.back() == '"') {
                manifest_.module_path = manifest_.module_path.substr(1, manifest_.module_path.size() - 2);
            }
            manifest_.loaded = true;
            continue;
        }
        if (t.rfind("go ", 0) == 0) {
            manifest_.go_version = Trim(t.substr(3));
            continue;
        }

        auto parse_require_line = [&](const std::string &body) {
            // <path> <version>  (optional `// indirect`)
            std::istringstream ls(body);
            std::string path, version;
            ls >> path >> version;
            if (!path.empty() && !version.empty()) {
                manifest_.requires_[path] = version;
            }
        };
        auto parse_replace_line = [&](const std::string &body) {
            // <orig> [<orig-ver>] => <repl>[ <repl-ver>]
            auto arrow = body.find("=>");
            if (arrow == std::string::npos) return;
            std::string lhs = Trim(body.substr(0, arrow));
            std::string rhs = Trim(body.substr(arrow + 2));
            // First token of each side is the path/dir.
            auto first_tok = [](const std::string &s) {
                std::istringstream l(s); std::string r; l >> r; return r;
            };
            std::string lhs_path = first_tok(lhs);
            std::string rhs_path = first_tok(rhs);
            if (!lhs_path.empty() && !rhs_path.empty())
                manifest_.replaces[lhs_path] = rhs_path;
        };

        if (in_require) { parse_require_line(t); continue; }
        if (in_replace) { parse_replace_line(t); continue; }
        if (t.rfind("require ", 0) == 0)  parse_require_line(t.substr(8));
        else if (t.rfind("replace ", 0) == 0) parse_replace_line(t.substr(8));
    }
}

// ---------------------------------------------------------------------------
// Path location
// ---------------------------------------------------------------------------

// Go module-cache directory escape: every uppercase letter X becomes "!x".
std::string GoImportResolver::EscapeModulePath(const std::string &p) {
    std::string out;
    out.reserve(p.size() + 4);
    for (char c : p) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            out.push_back('!');
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string GoImportResolver::LocatePackageDir(const std::string &import_path) const {
    auto exists_dir = [](const fs::path &p) {
        std::error_code ec; return fs::exists(p, ec) && fs::is_directory(p, ec);
    };

    // 1. Local sub-package within the project (only if go.mod was loaded
    //    and the import begins with the module prefix, OR the import is a
    //    relative-style ./foo path).
    if (!project_dir_.empty()) {
        if (manifest_.loaded && !manifest_.module_path.empty() &&
            import_path.rfind(manifest_.module_path, 0) == 0) {
            std::string rel = import_path.substr(manifest_.module_path.size());
            if (!rel.empty() && rel.front() == '/') rel.erase(rel.begin());
            fs::path candidate = fs::path(project_dir_) / rel;
            if (exists_dir(candidate)) return candidate.string();
        }
        if (import_path.rfind("./", 0) == 0 || import_path.rfind("../", 0) == 0) {
            fs::path candidate = fs::path(project_dir_) / import_path;
            if (exists_dir(candidate)) return candidate.string();
        }
        // 2. vendor/
        fs::path vendor = fs::path(project_dir_) / "vendor" / import_path;
        if (exists_dir(vendor)) return vendor.string();
    }

    // 3. replace directive — straightforward filesystem redirect.
    if (auto it = manifest_.replaces.find(import_path); it != manifest_.replaces.end()) {
        fs::path repl = it->second;
        if (repl.is_relative() && !project_dir_.empty()) repl = fs::path(project_dir_) / repl;
        if (exists_dir(repl)) return repl.string();
    }

    // 4. module_paths_ — GOROOT/src + GOPATH/pkg/mod + extras.
    for (const auto &root : module_paths_) {
        // 4a. Direct (GOROOT/src style — e.g. "fmt", "encoding/json").
        fs::path direct = fs::path(root) / import_path;
        if (exists_dir(direct)) return direct.string();

        // 4b. Module cache style — find a sub-directory matching
        //     "<escaped-import-path>@<version>".
        std::string escaped = EscapeModulePath(import_path);
        // Walk up parent directories of the import path so we can find
        // module roots that contain the import as a sub-package.
        fs::path probe = escaped;
        while (!probe.empty() && probe != probe.root_path()) {
            fs::path parent = fs::path(root) / probe.parent_path();
            std::error_code ec;
            if (fs::exists(parent, ec) && fs::is_directory(parent, ec)) {
                std::string leaf = probe.filename().string();
                for (auto &ent : fs::directory_iterator(parent, ec)) {
                    if (!ent.is_directory(ec)) continue;
                    std::string name = ent.path().filename().string();
                    auto at = name.find('@');
                    if (at == std::string::npos) continue;
                    if (name.substr(0, at) != leaf) continue;
                    fs::path full = ent.path();
                    // Walk down the rest of the import path under this versioned root.
                    fs::path remain = fs::path(escaped).lexically_relative(probe);
                    if (!remain.empty() && remain != ".") full /= remain;
                    if (exists_dir(full)) return full.string();
                    // Some module caches store the import path verbatim
                    // under @version — try direct join too.
                    fs::path join = ent.path() / fs::path(import_path).filename();
                    if (exists_dir(join)) return join.string();
                }
            }
            probe = probe.parent_path();
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Package loading — parse all .go files (excluding _test.go) in a directory
// and extract exported declarations.
// ---------------------------------------------------------------------------
std::unique_ptr<GoPackage> GoImportResolver::LoadPackage(
        const std::string &import_path,
        const std::string &dir,
        const std::string &version,
        bool is_local,
        bool is_stdlib) {
    auto pkg = std::make_unique<GoPackage>();
    pkg->import_path = import_path;
    pkg->root_dir = dir;
    pkg->module_version = version;
    pkg->is_local = is_local;
    pkg->is_stdlib = is_stdlib;

    std::error_code ec;
    fs::path d(dir);
    if (!fs::is_directory(d, ec)) return pkg;

    bool any_source = false;
    for (auto &ent : fs::directory_iterator(d, ec)) {
        if (!ent.is_regular_file(ec)) continue;
        const fs::path &p = ent.path();
        if (p.extension() != ".go") continue;
        std::string fname = p.filename().string();
        if (fname.size() >= 8 &&
            fname.compare(fname.size() - 8, 8, "_test.go") == 0) continue;

        std::string source = ReadFileToString(p);
        if (source.empty()) continue;
        any_source = true;

        // Parse with our own lexer/parser; suppress diagnostics into a
        // local sink because partial / cgo / build-tag-restricted files
        // should not pollute the user's diagnostic stream.
        frontends::Diagnostics local_diag;
        GoLexer lex(source, p.string());
        GoParser parser(lex, local_diag);
        parser.ParseFile();
        auto file = parser.TakeFile();
        if (!file) continue;

        if (pkg->package_name.empty()) pkg->package_name = file->package_name;

        // Top-level value/type/func declarations.
        for (const auto &gd : file->decls) {
            for (const auto &v : gd.values) {
                for (const auto &n : v.names) {
                    if (!StartsWithExportedName(n)) continue;
                    GoSymbol s;
                    s.name = n;
                    s.qualified_name = pkg->package_name + "." + n;
                    s.kind = (gd.keyword == "const") ? GoSymbolKind::kConstant
                                                     : GoSymbolKind::kVariable;
                    s.return_type = ToCoreType(v.type);
                    pkg->exports.emplace(n, std::move(s));
                }
            }
            for (const auto &t : gd.types) {
                if (!StartsWithExportedName(t.name)) continue;
                GoSymbol s;
                s.name = t.name;
                s.qualified_name = pkg->package_name + "." + t.name;
                s.kind = t.is_alias ? GoSymbolKind::kTypeAlias
                       : (t.type && t.type->kind == TypeKind::kInterface
                                ? GoSymbolKind::kInterface
                                : GoSymbolKind::kStruct);
                s.return_type = ToCoreType(t.type);
                pkg->exports.emplace(t.name, std::move(s));
            }
        }
        for (const auto &fn : file->funcs) {
            if (!fn) continue;
            if (!StartsWithExportedName(fn->name)) continue;
            GoSymbol s;
            s.name = fn->name;
            s.kind = GoSymbolKind::kFunction;
            s.is_variadic = fn->is_variadic;
            for (const auto &param : fn->params) {
                s.param_names.push_back(param.first);
                s.param_types.push_back(ToCoreType(param.second));
            }
            if (fn->results.size() == 1) {
                s.return_type = ToCoreType(fn->results.front().second);
            } else if (fn->results.empty()) {
                s.return_type = core::Type::Void();
            } else {
                // Tuple return — surface the first component plus annotate as struct.
                s.return_type = core::Type{core::TypeKind::kStruct, "tuple", "go"};
                for (const auto &r : fn->results)
                    s.return_type.type_args.push_back(ToCoreType(r.second));
            }
            if (fn->receiver && fn->receiver->type) {
                std::string recv = fn->receiver->type->name;
                if (fn->receiver->type->kind == TypeKind::kPointer && fn->receiver->type->elem)
                    recv = fn->receiver->type->elem->name;
                s.receiver_type = recv;
                s.qualified_name = pkg->package_name + "." + recv + "." + fn->name;
            } else {
                s.qualified_name = pkg->package_name + "." + fn->name;
            }
            // For overloaded simple names (rare in Go aside from methods
            // on different receivers), method receiver is appended to the
            // index key.
            std::string key = s.receiver_type.empty()
                                  ? s.name
                                  : (s.receiver_type + "." + s.name);
            pkg->exports.emplace(std::move(key), std::move(s));
        }
    }
    pkg->source_available = any_source;
    return pkg;
}

// ---------------------------------------------------------------------------
// Public Resolve / Lookup
// ---------------------------------------------------------------------------
const GoPackage *GoImportResolver::Resolve(const std::string &import_path) {
    if (auto it = by_path_.find(import_path); it != by_path_.end()) return it->second;

    std::string dir = LocatePackageDir(import_path);
    if (dir.empty()) {
        diagnostics_.Report(
            core::SourceLoc{},
            "go import not found: " + import_path +
                " (configured project_dir='" + project_dir_ +
                "', module_paths=" + std::to_string(module_paths_.size()) + ")");
        return nullptr;
    }

    bool is_local = !manifest_.module_path.empty() &&
                    import_path.rfind(manifest_.module_path, 0) == 0;
    bool is_stdlib = false;
    // Heuristic: when the resolved directory lives under the canonical
    // `<GOROOT>/src/<import>` form, mark as stdlib.
    if (!is_local) {
        std::string goroot = DetectGoroot();
        if (!goroot.empty()) {
            fs::path expect = fs::path(goroot) / "src" / import_path;
            std::error_code ec;
            if (fs::exists(expect, ec) && fs::canonical(expect, ec) == fs::canonical(dir, ec)) {
                is_stdlib = true;
            }
        }
    }

    std::string version;
    if (auto it = manifest_.requires_.find(import_path); it != manifest_.requires_.end()) {
        version = it->second;
    }

    auto owned = LoadPackage(import_path, dir, version, is_local, is_stdlib);
    if (!owned) return nullptr;
    GoPackage *raw = owned.get();
    by_path_[import_path] = raw;
    packages_.push_back(std::move(owned));
    return raw;
}

const GoPackage *GoImportResolver::Lookup(const std::string &import_path) const {
    auto it = by_path_.find(import_path);
    return (it == by_path_.end()) ? nullptr : it->second;
}

}  // namespace polyglot::go
