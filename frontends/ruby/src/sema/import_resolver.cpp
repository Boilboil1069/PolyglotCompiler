/**
 * @file     import_resolver.cpp
 * @brief    Ruby require / require_relative / load resolver implementation.
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-27
 */
#include "frontends/ruby/include/ruby_import_resolver.h"

#include <array>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "frontends/ruby/include/ruby_ast.h"
#include "frontends/ruby/include/ruby_lexer.h"
#include "frontends/ruby/include/ruby_parser.h"

namespace fs = std::filesystem;

namespace polyglot::ruby {

namespace {

bool FileExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

bool DirExists(const fs::path &p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

std::string ReadFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

#ifdef _WIN32
constexpr char kPathSep = ';';
#else
constexpr char kPathSep = ':';
#endif

void SplitPathList(const std::string &v, std::vector<std::string> &out) {
    std::string cur;
    for (char c : v) {
        if (c == kPathSep) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else { cur.push_back(c); }
    }
    if (!cur.empty()) out.push_back(cur);
}

core::Type RbTypeNodeToCore(const std::shared_ptr<TypeNode> &t) {
    if (!t) return core::Type::Any();
    const std::string &n = t->name;
    if (n == "Integer" || n == "Fixnum" || n == "Bignum") return core::Type::Int(64, true);
    if (n == "Float" || n == "Numeric") return core::Type::Float(64);
    if (n == "String" || n == "Symbol") return core::Type::String();
    if (n == "TrueClass" || n == "FalseClass" || n == "Boolean") return core::Type::Bool();
    if (n == "NilClass" || n == "Nil") return core::Type::Void();
    if (n == "Object") return core::Type::Any();
    return core::Type{core::TypeKind::kClass, n, "ruby"};
}

}  // namespace

// ---------------------------------------------------------------------------

RbImportResolver::RbImportResolver(std::string project_dir,
                                   std::vector<std::string> gem_paths,
                                   frontends::Diagnostics &diagnostics)
    : project_dir_(std::move(project_dir)),
      gem_paths_(std::move(gem_paths)),
      diagnostics_(diagnostics) {
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable: 4996)
#endif
    if (const char *rl = std::getenv("RUBYLIB")) SplitPathList(rl, rubylib_paths_);
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
}

void RbImportResolver::EnsureDefaultLoadPaths() {
    if (default_paths_probed_) return;
    default_paths_probed_ = true;
    // Best-effort probe: ask the host Ruby (if any) for its $LOAD_PATH.
    // Failure is silent — most projects supply their own `--gem-path`.
#ifdef _WIN32
    FILE *pipe = _popen("ruby -e \"puts $LOAD_PATH\" 2>NUL", "r");
#else
    FILE *pipe = popen("ruby -e 'puts $LOAD_PATH' 2>/dev/null", "r");
#endif
    if (!pipe) return;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), pipe)) {
        std::string line(buf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty() && line != ".") default_load_paths_.push_back(line);
    }
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
}

std::string RbImportResolver::ProbeRb(const std::string &path_no_ext) const {
    fs::path with = path_no_ext + ".rb";
    if (FileExists(with)) return with.string();
    fs::path raw = path_no_ext;
    if (FileExists(raw)) return raw.string();
    return {};
}

std::string RbImportResolver::ResolveRelative(const std::string &specifier,
                                               const std::string &importer_dir) const {
    fs::path base = importer_dir.empty() ? fs::path(".") : fs::path(importer_dir);
    fs::path candidate = (base / specifier).lexically_normal();
    return ProbeRb(candidate.string());
}

std::string RbImportResolver::ResolveBare(const std::string &specifier,
                                           std::string &out_gem_name) const {
    auto try_in = [&](const fs::path &root) -> std::string {
        // 1. Direct probe: <root>/<specifier>.rb
        if (auto p = ProbeRb((root / specifier).string()); !p.empty()) return p;
        // 2. Bundler / RubyGems vendor layout: <root>/<gem>/lib/<specifier>.rb
        std::error_code ec;
        if (!DirExists(root)) return {};
        for (auto &entry : fs::directory_iterator(root, ec)) {
            if (!entry.is_directory(ec)) continue;
            fs::path lib = entry.path() / "lib";
            if (!DirExists(lib)) continue;
            if (auto p = ProbeRb((lib / specifier).string()); !p.empty()) {
                out_gem_name = entry.path().filename().string();
                return p;
            }
        }
        return {};
    };

    for (const auto &r : rubylib_paths_) {
        if (auto p = try_in(r); !p.empty()) return p;
    }
    for (const auto &r : gem_paths_) {
        if (auto p = try_in(r); !p.empty()) return p;
    }
    if (!project_dir_.empty()) {
        if (auto p = try_in(fs::path(project_dir_) / "lib"); !p.empty()) return p;
        if (auto p = try_in(project_dir_); !p.empty()) return p;
    }
    for (const auto &r : default_load_paths_) {
        if (auto p = try_in(r); !p.empty()) return p;
    }
    return {};
}

// ---------------------------------------------------------------------------

namespace {

// Walk a class/module body once to collect qualified exports.
void Harvest(const std::vector<std::shared_ptr<Statement>> &body,
             const std::string &prefix,
             std::unordered_map<std::string, RbSymbol> &out) {
    for (const auto &stmt : body) {
        if (auto md = std::dynamic_pointer_cast<MethodDecl>(stmt)) {
            RbSymbol s;
            s.name = md->name;
            s.qualified_name = prefix.empty() ? md->name : (prefix + (md->is_self ? "." : "#") + md->name);
            s.kind = RbSymbolKind::kMethod;
            s.is_singleton = md->is_self;
            for (const auto &p : md->params) {
                if (p.splat || p.double_splat || p.block) continue;
                s.param_names.push_back(p.name);
                s.param_types.push_back(RbTypeNodeToCore(p.type));
            }
            s.return_type = RbTypeNodeToCore(md->return_type);
            out[s.qualified_name] = std::move(s);
        } else if (auto cd = std::dynamic_pointer_cast<ClassDecl>(stmt)) {
            std::string qn = prefix.empty() ? cd->name : (prefix + "::" + cd->name);
            RbSymbol s;
            s.name = cd->name;
            s.qualified_name = qn;
            s.kind = RbSymbolKind::kClass;
            s.return_type = core::Type{core::TypeKind::kClass, qn, "ruby"};
            out[qn] = std::move(s);
            Harvest(cd->body, qn, out);
        } else if (auto mo = std::dynamic_pointer_cast<ModuleDecl>(stmt)) {
            std::string qn = prefix.empty() ? mo->name : (prefix + "::" + mo->name);
            RbSymbol s;
            s.name = mo->name;
            s.qualified_name = qn;
            s.kind = RbSymbolKind::kModule;
            s.return_type = core::Type{core::TypeKind::kModule, qn, "ruby"};
            out[qn] = std::move(s);
            Harvest(mo->body, qn, out);
        }
    }
}

}  // namespace

std::unique_ptr<RbFile> RbImportResolver::LoadFile(const std::string &requested,
                                                    const std::string &resolved_path,
                                                    const std::string &gem_name) {
    auto rf = std::make_unique<RbFile>();
    rf->requested = requested;
    rf->resolved_path = resolved_path;
    rf->gem_name = gem_name;

    std::string source = ReadFile(resolved_path);
    if (source.empty()) return rf;

    frontends::Diagnostics local_diag;
    RbLexer lex(source, resolved_path);
    RbParser parser(lex, local_diag);
    parser.ParseModule();
    auto module = parser.TakeModule();
    if (!module) return rf;

    Harvest(module->body, std::string{}, rf->exports);
    return rf;
}

const RbFile *RbImportResolver::Resolve(const std::string &specifier,
                                         const std::string &importer_dir,
                                         bool relative) {
    std::string key = (relative ? "rel::" : "abs::") + importer_dir + "::" + specifier;
    if (auto it = by_key_.find(key); it != by_key_.end()) return it->second;

    std::string resolved;
    std::string gem_name;
    if (relative) {
        resolved = ResolveRelative(specifier, importer_dir);
    } else {
        resolved = ResolveBare(specifier, gem_name);
        if (resolved.empty()) {
            EnsureDefaultLoadPaths();
            resolved = ResolveBare(specifier, gem_name);
        }
    }

    if (resolved.empty()) {
        diagnostics_.Report(core::SourceLoc{},
            "ruby require not resolved: '" + specifier + "' (relative=" +
            (relative ? "true" : "false") + ", gem_paths=" +
            std::to_string(gem_paths_.size()) + ")");
        return nullptr;
    }

    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(resolved, ec);
    std::string canon_str = canonical.empty() ? resolved : canonical.string();
    if (auto it = by_path_.find(canon_str); it != by_path_.end()) {
        by_key_[key] = it->second;
        return it->second;
    }

    auto owned = LoadFile(specifier, canon_str, gem_name);
    RbFile *raw = owned.get();
    by_path_[canon_str] = raw;
    by_key_[key] = raw;
    files_.push_back(std::move(owned));
    return raw;
}

}  // namespace polyglot::ruby
