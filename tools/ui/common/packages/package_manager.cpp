/**
 * @file     package_manager.cpp
 * @brief    Concrete backends + service implementation.
 *
 * Each backend is a focused, single-class implementation of
 * `PackageManagerBackend`.  Lockfile parsers are deliberately
 * lenient: they extract the bits the dependency-graph and
 * vulnerability-scan layers need (name, version, optional source)
 * without imposing schema validation.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/packages/package_manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <sstream>

#include <nlohmann/json.hpp>

namespace polyglot::tools::ui::packages {
namespace {

using Json = nlohmann::json;

std::string Trim(std::string s) {
  auto not_ws = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
  return s;
}

std::string Unquote(std::string s) {
  s = Trim(std::move(s));
  if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') &&
      s.back() == s.front()) {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

// --- Backends ---------------------------------------------------------------

class PipBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kVenv; }
  std::string manifest_filename() const override { return "requirements.txt"; }
  std::string lockfile_filename() const override { return "requirements.lock"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"pip", "install", v.empty() ? p : p + "==" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"pip", "install", "--upgrade", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"pip", "uninstall", "-y", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    while (std::getline(ss, line)) {
      line = Trim(std::move(line));
      if (line.empty() || line[0] == '#') continue;
      auto eq = line.find("==");
      Package p;
      if (eq == std::string::npos) {
        p.name = line;
      } else {
        p.name = line.substr(0, eq);
        p.version = line.substr(eq + 2);
        // Strip ; markers / hash trailers.
        auto sc = p.version.find(';');
        if (sc != std::string::npos) p.version.resize(sc);
        auto sp = p.version.find(' ');
        if (sp != std::string::npos) p.version.resize(sp);
        p.version = Trim(p.version);
      }
      p.is_direct = true;
      out.push_back(std::move(p));
    }
    return out;
  }
};

class CondaBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kConda; }
  std::string manifest_filename() const override { return "environment.yml"; }
  std::string lockfile_filename() const override { return "conda-lock.yml"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"conda", "install", "-y", v.empty() ? p : p + "=" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"conda", "update", "-y", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"conda", "remove", "-y", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // Look for entries like "  - name=version=build" or
    // "url: https://.../foo-1.2.3-py.tar.bz2".
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    while (std::getline(ss, line)) {
      auto t = Trim(line);
      if (t.size() < 2 || t[0] != '-') continue;
      t = Trim(t.substr(1));
      // form: name=version=build
      auto first_eq = t.find('=');
      if (first_eq == std::string::npos) continue;
      auto second_eq = t.find('=', first_eq + 1);
      Package p;
      p.name = t.substr(0, first_eq);
      p.version = t.substr(
          first_eq + 1,
          (second_eq == std::string::npos ? std::string::npos
                                          : second_eq - first_eq - 1));
      out.push_back(std::move(p));
    }
    return out;
  }
};

class UvBackend : public PipBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kUv; }
  std::string manifest_filename() const override { return "pyproject.toml"; }
  std::string lockfile_filename() const override { return "uv.lock"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"uv", "add", v.empty() ? p : p + "==" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"uv", "lock", "--upgrade-package", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"uv", "remove", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // uv.lock is TOML; parse [[package]] blocks for name + version.
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    Package cur;
    bool in_pkg = false;
    auto flush = [&] {
      if (in_pkg && !cur.name.empty()) out.push_back(cur);
      cur = {};
      in_pkg = false;
    };
    while (std::getline(ss, line)) {
      auto t = Trim(line);
      if (t == "[[package]]") { flush(); in_pkg = true; continue; }
      if (!in_pkg) continue;
      if (t.rfind("name", 0) == 0) {
        auto eq = t.find('=');
        if (eq != std::string::npos) cur.name = Unquote(t.substr(eq + 1));
      } else if (t.rfind("version", 0) == 0) {
        auto eq = t.find('=');
        if (eq != std::string::npos) cur.version = Unquote(t.substr(eq + 1));
      }
    }
    flush();
    return out;
  }
};

class PipenvBackend : public PipBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kPipenv; }
  std::string manifest_filename() const override { return "Pipfile"; }
  std::string lockfile_filename() const override { return "Pipfile.lock"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"pipenv", "install", v.empty() ? p : p + "==" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"pipenv", "update", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"pipenv", "uninstall", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    auto j = Json::parse(c, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    std::vector<Package> out;
    for (const char *section : {"default", "develop"}) {
      if (!j.contains(section)) continue;
      for (const auto &kv : j[section].items()) {
        Package p;
        p.name = kv.key();
        if (kv.value().is_object() && kv.value().contains("version")) {
          std::string v = kv.value()["version"].get<std::string>();
          if (v.rfind("==", 0) == 0) v.erase(0, 2);
          p.version = v;
        }
        p.is_direct = true;
        out.push_back(std::move(p));
      }
    }
    return out;
  }
};

class PoetryBackend : public UvBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kPoetry; }
  std::string manifest_filename() const override { return "pyproject.toml"; }
  std::string lockfile_filename() const override { return "poetry.lock"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"poetry", "add", v.empty() ? p : p + "==" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"poetry", "update", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"poetry", "remove", p};
  }
};

class CargoBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kCargo; }
  std::string manifest_filename() const override { return "Cargo.toml"; }
  std::string lockfile_filename() const override { return "Cargo.lock"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    if (v.empty()) return {"cargo", "add", p};
    return {"cargo", "add", p, "--vers", v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"cargo", "update", "-p", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"cargo", "remove", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // TOML [[package]] blocks with name = "..." version = "..."
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    Package cur;
    bool in_pkg = false;
    auto flush = [&] {
      if (in_pkg && !cur.name.empty()) out.push_back(cur);
      cur = {};
      in_pkg = false;
    };
    while (std::getline(ss, line)) {
      auto t = Trim(line);
      if (t == "[[package]]") { flush(); in_pkg = true; continue; }
      if (!in_pkg) continue;
      if (t.rfind("name", 0) == 0) {
        auto eq = t.find('=');
        if (eq != std::string::npos) cur.name = Unquote(t.substr(eq + 1));
      } else if (t.rfind("version", 0) == 0) {
        auto eq = t.find('=');
        if (eq != std::string::npos) cur.version = Unquote(t.substr(eq + 1));
      } else if (t.rfind("source", 0) == 0) {
        auto eq = t.find('=');
        if (eq != std::string::npos) cur.source = Unquote(t.substr(eq + 1));
      }
    }
    flush();
    return out;
  }
};

class NpmBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kNpm; }
  std::string manifest_filename() const override { return "package.json"; }
  std::string lockfile_filename() const override { return "package-lock.json"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"npm", "install", v.empty() ? p : p + "@" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"npm", "update", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"npm", "uninstall", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    auto j = Json::parse(c, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    std::vector<Package> out;
    if (j.contains("packages") && j["packages"].is_object()) {
      for (const auto &kv : j["packages"].items()) {
        if (kv.key().empty()) continue;       // root entry
        Package p;
        // path is "node_modules/<name>" or "node_modules/<a>/node_modules/<b>".
        auto pos = kv.key().rfind("node_modules/");
        p.name = (pos == std::string::npos)
                     ? kv.key()
                     : kv.key().substr(pos + std::string("node_modules/").size());
        if (kv.value().is_object() && kv.value().contains("version"))
          p.version = kv.value()["version"].get<std::string>();
        out.push_back(std::move(p));
      }
    } else if (j.contains("dependencies") && j["dependencies"].is_object()) {
      for (const auto &kv : j["dependencies"].items()) {
        Package p;
        p.name = kv.key();
        if (kv.value().is_object() && kv.value().contains("version"))
          p.version = kv.value()["version"].get<std::string>();
        out.push_back(std::move(p));
      }
    }
    return out;
  }
};

class MavenBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kMaven; }
  std::string manifest_filename() const override { return "pom.xml"; }
  std::string lockfile_filename() const override { return "pom.xml"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &) const override {
    return {"mvn", "dependency:get", "-Dartifact=" + p};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"mvn", "versions:use-latest-releases", "-Dincludes=" + p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"mvn", "dependency:purge-local-repository", "-DmanualInclude=" + p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // Walk <dependency>...<groupId>...<artifactId>...<version>.
    std::vector<Package> out;
    std::regex dep(R"(<dependency>([\s\S]*?)</dependency>)");
    auto begin = std::sregex_iterator(c.begin(), c.end(), dep);
    auto end = std::sregex_iterator{};
    static const std::regex re_g(R"(<groupId>([^<]+)</groupId>)");
    static const std::regex re_a(R"(<artifactId>([^<]+)</artifactId>)");
    static const std::regex re_v(R"(<version>([^<]+)</version>)");
    for (auto it = begin; it != end; ++it) {
      std::string body = (*it)[1];
      std::smatch m;
      Package p;
      std::string g, a;
      if (std::regex_search(body, m, re_g)) g = m[1];
      if (std::regex_search(body, m, re_a)) a = m[1];
      if (std::regex_search(body, m, re_v)) p.version = m[1];
      p.name = g.empty() ? a : g + ":" + a;
      if (!p.name.empty()) out.push_back(std::move(p));
    }
    return out;
  }
};

class GradleBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kGradle; }
  std::string manifest_filename() const override { return "build.gradle"; }
  std::string lockfile_filename() const override { return "gradle.lockfile"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &) const override {
    return {"gradle", "dependencies", "--write-locks", "--refresh-dependencies",
            "-Pdep=" + p};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"gradle", "dependencyUpdates", "-Pdep=" + p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"gradle", "dependencies", "--remove=" + p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // Lines like "group:artifact:version=conf1,conf2".
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    while (std::getline(ss, line)) {
      auto t = Trim(line);
      if (t.empty() || t[0] == '#') continue;
      auto eq = t.find('=');
      if (eq != std::string::npos) t.resize(eq);
      auto first = t.find(':');
      if (first == std::string::npos) continue;
      auto second = t.find(':', first + 1);
      if (second == std::string::npos) continue;
      Package p;
      p.name = t.substr(0, second);
      p.version = t.substr(second + 1);
      out.push_back(std::move(p));
    }
    return out;
  }
};

class NugetBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kNuget; }
  std::string manifest_filename() const override { return "packages.config"; }
  std::string lockfile_filename() const override { return "packages.lock.json"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    if (v.empty()) return {"dotnet", "add", "package", p};
    return {"dotnet", "add", "package", p, "--version", v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"dotnet", "add", "package", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"dotnet", "remove", "package", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    auto j = Json::parse(c, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    std::vector<Package> out;
    if (!j.contains("dependencies")) return out;
    for (const auto &target : j["dependencies"].items()) {
      if (!target.value().is_object()) continue;
      for (const auto &dep : target.value().items()) {
        Package p;
        p.name = dep.key();
        if (dep.value().is_object() && dep.value().contains("resolved"))
          p.version = dep.value()["resolved"].get<std::string>();
        out.push_back(std::move(p));
      }
    }
    return out;
  }
};

class GemBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kGem; }
  std::string manifest_filename() const override { return "Gemfile"; }
  std::string lockfile_filename() const override { return "Gemfile.lock"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    if (v.empty()) return {"bundle", "add", p};
    return {"bundle", "add", p, "--version", v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"bundle", "update", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"bundle", "remove", p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // Lines of the form "    name (version)" under a GEM section.
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    static const std::regex re(R"(^\s{4}([A-Za-z0-9_\-\.]+)\s+\(([^\)]+)\)\s*$)");
    while (std::getline(ss, line)) {
      std::smatch m;
      if (std::regex_match(line, m, re)) {
        Package p;
        p.name = m[1];
        p.version = m[2];
        out.push_back(std::move(p));
      }
    }
    return out;
  }
};

class GoModBackend : public PackageManagerBackend {
 public:
  Ecosystem ecosystem() const override { return Ecosystem::kGoMod; }
  std::string manifest_filename() const override { return "go.mod"; }
  std::string lockfile_filename() const override { return "go.sum"; }
  std::vector<std::string> InstallArgv(const std::string &p,
                                       const std::string &v) const override {
    return {"go", "get", v.empty() ? p : p + "@" + v};
  }
  std::vector<std::string> UpgradeArgv(const std::string &p) const override {
    return {"go", "get", "-u", p};
  }
  std::vector<std::string> RemoveArgv(const std::string &p) const override {
    return {"go", "mod", "edit", "-droprequire=" + p};
  }
  std::vector<Package> ParseLockfile(const std::string &c) const override {
    // Lines like "module v1.2.3 h1:hash" — keep first field per (mod,ver).
    std::vector<Package> out;
    std::stringstream ss(c);
    std::string line;
    std::string last_key;
    while (std::getline(ss, line)) {
      auto t = Trim(line);
      if (t.empty()) continue;
      std::istringstream ls(t);
      std::string mod, ver;
      if (!(ls >> mod >> ver)) continue;
      // go.sum has both "module ver" and "module ver/go.mod" entries;
      // collapse on the bare "module ver" record.
      if (ver.size() > 7 && ver.substr(ver.size() - 7) == "/go.mod") continue;
      std::string key = mod + "@" + ver;
      if (key == last_key) continue;
      last_key = key;
      Package p;
      p.name = mod;
      p.version = ver;
      out.push_back(std::move(p));
    }
    return out;
  }
};

}  // namespace

std::string EcosystemName(Ecosystem e) {
  switch (e) {
    case Ecosystem::kVenv:   return "venv";
    case Ecosystem::kConda:  return "conda";
    case Ecosystem::kUv:     return "uv";
    case Ecosystem::kPipenv: return "pipenv";
    case Ecosystem::kPoetry: return "poetry";
    case Ecosystem::kCargo:  return "cargo";
    case Ecosystem::kNpm:    return "npm";
    case Ecosystem::kMaven:  return "maven";
    case Ecosystem::kGradle: return "gradle";
    case Ecosystem::kNuget:  return "nuget";
    case Ecosystem::kGem:    return "gem";
    case Ecosystem::kGoMod:  return "gomod";
  }
  return "unknown";
}

std::optional<Ecosystem> EcosystemFromName(const std::string &name) {
  static const std::unordered_map<std::string, Ecosystem> kMap = {
      {"venv", Ecosystem::kVenv},   {"conda", Ecosystem::kConda},
      {"uv", Ecosystem::kUv},       {"pipenv", Ecosystem::kPipenv},
      {"poetry", Ecosystem::kPoetry}, {"cargo", Ecosystem::kCargo},
      {"npm", Ecosystem::kNpm},     {"maven", Ecosystem::kMaven},
      {"gradle", Ecosystem::kGradle}, {"nuget", Ecosystem::kNuget},
      {"gem", Ecosystem::kGem},     {"gomod", Ecosystem::kGoMod},
  };
  auto it = kMap.find(name);
  if (it == kMap.end()) return std::nullopt;
  return it->second;
}

std::string PackageManagerBackend::ToConfigRequirement(const Package &p) const {
  return p.version.empty() ? p.name : p.name + "==" + p.version;
}

Package PackageManagerBackend::FromConfigRequirement(
    const std::string &requirement) const {
  Package p;
  auto eq = requirement.find("==");
  if (eq == std::string::npos) {
    p.name = requirement;
  } else {
    p.name = requirement.substr(0, eq);
    p.version = requirement.substr(eq + 2);
  }
  return p;
}

PackageManagerRegistry::PackageManagerRegistry() {
  auto add = [&](std::unique_ptr<PackageManagerBackend> b) {
    auto eco = static_cast<int>(b->ecosystem());
    by_manifest_[b->manifest_filename()] = b.get();
    by_eco_[eco] = std::move(b);
  };
  add(std::make_unique<PipBackend>());
  add(std::make_unique<CondaBackend>());
  add(std::make_unique<UvBackend>());
  add(std::make_unique<PipenvBackend>());
  add(std::make_unique<PoetryBackend>());
  add(std::make_unique<CargoBackend>());
  add(std::make_unique<NpmBackend>());
  add(std::make_unique<MavenBackend>());
  add(std::make_unique<GradleBackend>());
  add(std::make_unique<NugetBackend>());
  add(std::make_unique<GemBackend>());
  add(std::make_unique<GoModBackend>());
}

PackageManagerBackend *PackageManagerRegistry::Get(Ecosystem e) const {
  auto it = by_eco_.find(static_cast<int>(e));
  return it == by_eco_.end() ? nullptr : it->second.get();
}

PackageManagerBackend *PackageManagerRegistry::FindByManifest(
    const std::string &filename) const {
  auto it = by_manifest_.find(filename);
  return it == by_manifest_.end() ? nullptr : it->second;
}

std::vector<Ecosystem> PackageManagerRegistry::All() const {
  std::vector<Ecosystem> out;
  out.reserve(by_eco_.size());
  for (const auto &kv : by_eco_) out.push_back(kv.second->ecosystem());
  std::sort(out.begin(), out.end(),
            [](Ecosystem a, Ecosystem b) {
              return static_cast<int>(a) < static_cast<int>(b);
            });
  return out;
}

PackageManagerService::PackageManagerService(CommandExecutor executor)
    : executor_(std::move(executor)) {}

std::size_t PackageManagerService::Discover(
    const std::string &workspace_root,
    const std::vector<std::string> &candidate_dirs) {
  std::size_t added = 0;
  std::vector<std::string> to_scan = candidate_dirs;
  if (to_scan.empty()) to_scan.push_back(workspace_root);
  for (const auto &dir : to_scan) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) continue;
    for (const auto &entry :
         std::filesystem::directory_iterator(dir, ec)) {
      if (!entry.is_regular_file()) continue;
      auto fname = entry.path().filename().string();
      auto *backend = registry_.FindByManifest(fname);
      if (!backend) continue;
      Environment env;
      env.ecosystem = backend->ecosystem();
      env.root = dir;
      env.display_name =
          EcosystemName(env.ecosystem) + ":" +
          std::filesystem::path(dir).filename().string();
      auto already = std::find_if(envs_.begin(), envs_.end(),
                                  [&](const Environment &e) {
                                    return e.root == env.root &&
                                           e.ecosystem == env.ecosystem;
                                  });
      if (already == envs_.end()) {
        envs_.push_back(std::move(env));
        ++added;
      }
    }
  }
  return added;
}

void PackageManagerService::Activate(const std::string &root) {
  std::optional<Ecosystem> target_eco;
  for (const auto &env : envs_) {
    if (env.root == root) { target_eco = env.ecosystem; break; }
  }
  if (!target_eco) return;
  for (auto &env : envs_) {
    if (env.ecosystem == *target_eco) env.active = (env.root == root);
  }
}

const Environment *PackageManagerService::ActiveFor(Ecosystem e) const {
  for (const auto &env : envs_)
    if (env.ecosystem == e && env.active) return &env;
  return nullptr;
}

CommandResult PackageManagerService::Install(const Environment &env,
                                             const std::string &package,
                                             const std::string &version) {
  auto *b = registry_.Get(env.ecosystem);
  if (!b) return {127, {}, "no backend"};
  return executor_(b->InstallArgv(package, version), env.root);
}

CommandResult PackageManagerService::Upgrade(const Environment &env,
                                             const std::string &package) {
  auto *b = registry_.Get(env.ecosystem);
  if (!b) return {127, {}, "no backend"};
  return executor_(b->UpgradeArgv(package), env.root);
}

CommandResult PackageManagerService::Remove(const Environment &env,
                                            const std::string &package) {
  auto *b = registry_.Get(env.ecosystem);
  if (!b) return {127, {}, "no backend"};
  return executor_(b->RemoveArgv(package), env.root);
}

std::optional<std::vector<Package>> PackageManagerService::ReadLockfile(
    const Environment &env,
    const std::function<std::optional<std::string>(const std::string &)>
        &reader) const {
  auto *b = registry_.Get(env.ecosystem);
  if (!b) return std::nullopt;
  std::string lockpath = env.root;
  if (!lockpath.empty() && lockpath.back() != '/' && lockpath.back() != '\\')
    lockpath += '/';
  lockpath += b->lockfile_filename();
  auto text = reader(lockpath);
  if (!text) return std::nullopt;
  return b->ParseLockfile(*text);
}

PackageManagerService::ConfigSyncReport
PackageManagerService::SyncWithConfig(
    const Environment &env, const std::vector<std::string> &config_block,
    const std::vector<Package> &resolved) const {
  ConfigSyncReport out;
  auto *b = registry_.Get(env.ecosystem);
  if (!b) return out;
  std::unordered_map<std::string, std::string> resolved_map;
  for (const auto &p : resolved) resolved_map[p.name] = p.version;

  for (const auto &req_str : config_block) {
    auto req = b->FromConfigRequirement(req_str);
    auto it = resolved_map.find(req.name);
    if (it == resolved_map.end()) {
      out.missing_in_lockfile.push_back(req_str);
      continue;
    }
    if (!req.version.empty() && req.version != it->second) {
      out.missing_in_lockfile.push_back(req_str);
    }
  }

  std::unordered_map<std::string, bool> in_config;
  for (const auto &req_str : config_block) {
    in_config[b->FromConfigRequirement(req_str).name] = true;
  }
  for (const auto &p : resolved) {
    if (!in_config.count(p.name))
      out.missing_in_config.push_back(b->ToConfigRequirement(p));
  }
  return out;
}

}  // namespace polyglot::tools::ui::packages
