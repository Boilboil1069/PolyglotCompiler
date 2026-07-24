/**
 * @file local_source_packages.cpp
 * @brief Project-local source package resolver used by both packaging paths.
 */

#include "tools/polyc/src/local_source_packages.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "frontends/common/include/lexer_base.h"
#include "frontends/ploy/include/ploy_lexer.h"

namespace polyglot::tools {
namespace {

namespace fs = std::filesystem;

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string TrimAscii(std::string value) {
  const auto non_space = [](unsigned char c) { return std::isspace(c) == 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), non_space));
  value.erase(std::find_if(value.rbegin(), value.rend(), non_space).base(), value.end());
  return value;
}

std::string ReadFile(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return {};
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

std::optional<std::string> ParseTomlString(std::string value) {
  value = TrimAscii(std::move(value));
  if (value.size() < 2)
    return std::nullopt;
  const char quote = value.front();
  if ((quote != '\'' && quote != '"') || value.back() != quote)
    return std::nullopt;

  std::string result;
  result.reserve(value.size() - 2);
  for (std::size_t i = 1; i + 1 < value.size(); ++i) {
    const char c = value[i];
    if (quote == '"' && c == '\\') {
      if (++i + 1 >= value.size())
        return std::nullopt;
      const char escaped = value[i];
      if (escaped != '\\' && escaped != '"')
        return std::nullopt;
      result.push_back(escaped);
      continue;
    }
    if (c == quote)
      return std::nullopt;
    result.push_back(c);
  }
  return result;
}

struct SourceManifest {
  std::string name;
  std::string language;
  std::string version;
  std::string source;
};

std::optional<SourceManifest> ReadManifest(const fs::path &path) {
  std::ifstream input(path);
  if (!input)
    return std::nullopt;

  SourceManifest manifest;
  std::string line;
  while (std::getline(input, line)) {
    bool single = false;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
      const char c = line[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\' && quoted) {
        escaped = true;
        continue;
      }
      if (c == '\'' && !quoted)
        single = !single;
      else if (c == '"' && !single)
        quoted = !quoted;
      else if (c == '#' && !single && !quoted) {
        line.erase(i);
        break;
      }
    }
    line = TrimAscii(std::move(line));
    if (line.empty())
      continue;
    if (line.front() == '[')
      break;
    const auto equals = line.find('=');
    if (equals == std::string::npos)
      continue;
    const std::string key = TrimAscii(line.substr(0, equals));
    const auto value = ParseTomlString(line.substr(equals + 1));
    if (!value)
      continue;
    if (key == "name")
      manifest.name = *value;
    else if (key == "language")
      manifest.language = LowerAscii(*value);
    else if (key == "version")
      manifest.version = *value;
    else if (key == "source")
      manifest.source = *value;
  }
  if (manifest.name.empty() || manifest.language.empty())
    return std::nullopt;
  return manifest;
}

struct DeclaredPackage {
  std::string name;
  std::string version_op;
  std::string version_constraint;
};

std::string UnquoteToken(std::string value) {
  if (value.size() >= 2 &&
      ((value.front() == '"' && value.back() == '"') ||
       (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

std::vector<DeclaredPackage> DeclaredPackages(const fs::path &poly_entry,
                                              const std::string &language) {
  const std::string source = ReadFile(poly_entry);
  if (source.empty())
    return {};

  std::vector<frontends::Token> tokens;
  ploy::PloyLexer lexer(source, poly_entry.string());
  for (;;) {
    auto token = lexer.NextToken();
    const bool done = token.kind == frontends::TokenKind::kEndOfFile;
    tokens.push_back(std::move(token));
    if (done)
      break;
  }

  const std::string requested_language = LowerAscii(language);
  std::vector<DeclaredPackage> result;
  for (std::size_t i = 0; i + 3 < tokens.size(); ++i) {
    if (tokens[i].kind != frontends::TokenKind::kKeyword ||
        tokens[i].lexeme != "IMPORT")
      continue;
    if ((tokens[i + 1].kind != frontends::TokenKind::kIdentifier &&
         tokens[i + 1].kind != frontends::TokenKind::kKeyword) ||
        LowerAscii(tokens[i + 1].lexeme) != requested_language)
      continue;
    if (tokens[i + 2].kind != frontends::TokenKind::kKeyword ||
        tokens[i + 2].lexeme != "PACKAGE" ||
        tokens[i + 3].kind != frontends::TokenKind::kIdentifier)
      continue;

    std::string name = tokens[i + 3].lexeme;
    std::size_t cursor = i + 4;
    while (cursor + 1 < tokens.size() &&
           tokens[cursor].kind == frontends::TokenKind::kSymbol &&
           tokens[cursor].lexeme == "." &&
           tokens[cursor + 1].kind == frontends::TokenKind::kIdentifier) {
      name += "." + tokens[cursor + 1].lexeme;
      cursor += 2;
    }
    DeclaredPackage package;
    package.name = std::move(name);

    // Match the version surface accepted by PloyParser: >=, <=, ==, >, <,
    // and ~= followed by a numeric/identifier/string version, optionally
    // split across dot tokens.
    while (cursor < tokens.size() &&
           !(tokens[cursor].kind == frontends::TokenKind::kSymbol &&
             tokens[cursor].lexeme == ";") &&
           tokens[cursor].kind != frontends::TokenKind::kEndOfFile) {
      const bool is_version_op =
          tokens[cursor].kind == frontends::TokenKind::kSymbol &&
          (tokens[cursor].lexeme == ">=" || tokens[cursor].lexeme == "<=" ||
           tokens[cursor].lexeme == "==" || tokens[cursor].lexeme == ">" ||
           tokens[cursor].lexeme == "<" || tokens[cursor].lexeme == "~=");
      if (!is_version_op) {
        ++cursor;
        continue;
      }

      package.version_op = tokens[cursor].lexeme;
      ++cursor;
      if (cursor >= tokens.size())
        break;
      if (tokens[cursor].kind == frontends::TokenKind::kString) {
        package.version_constraint = UnquoteToken(tokens[cursor].lexeme);
        break;
      }
      if (tokens[cursor].kind != frontends::TokenKind::kNumber &&
          tokens[cursor].kind != frontends::TokenKind::kIdentifier) {
        break;
      }
      package.version_constraint = tokens[cursor].lexeme;
      ++cursor;
      while (cursor + 1 < tokens.size() &&
             tokens[cursor].kind == frontends::TokenKind::kSymbol &&
             tokens[cursor].lexeme == "." &&
             (tokens[cursor + 1].kind == frontends::TokenKind::kNumber ||
              tokens[cursor + 1].kind == frontends::TokenKind::kIdentifier)) {
        package.version_constraint += "." + tokens[cursor + 1].lexeme;
        cursor += 2;
      }
      break;
    }
    result.push_back(std::move(package));
  }
  return result;
}

bool SafeRelative(const fs::path &path) {
  if (path.empty() || path.is_absolute() || path.has_root_name() ||
      path.has_root_directory())
    return false;
  return std::none_of(path.begin(), path.end(),
                      [](const fs::path &part) { return part == ".."; });
}

bool Within(const fs::path &child, const fs::path &parent) {
  auto child_it = child.begin();
  for (auto parent_it = parent.begin(); parent_it != parent.end();
       ++parent_it, ++child_it) {
    if (child_it == child.end() || *child_it != *parent_it)
      return false;
  }
  return true;
}

std::optional<std::vector<std::uint64_t>> ParseVersionParts(std::string version) {
  version = TrimAscii(std::move(version));
  if (!version.empty() && (version.front() == 'v' || version.front() == 'V'))
    version.erase(version.begin());
  if (version.empty())
    return std::nullopt;

  std::vector<std::uint64_t> parts;
  std::uint64_t current = 0;
  bool have_digit = false;
  for (const char c : version) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      const unsigned digit = static_cast<unsigned>(c - '0');
      if (current > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
        return std::nullopt;
      current = current * 10 + digit;
      have_digit = true;
      continue;
    }
    if (c == '.') {
      if (!have_digit)
        return std::nullopt;
      parts.push_back(current);
      current = 0;
      have_digit = false;
      continue;
    }
    // SemVer pre-release/build metadata does not change the numeric
    // compatibility surface used by Poly package constraints.
    if (c == '-' || c == '+')
      break;
    return std::nullopt;
  }
  if (!have_digit)
    return std::nullopt;
  parts.push_back(current);
  return parts;
}

int CompareVersionParts(std::vector<std::uint64_t> lhs,
                        std::vector<std::uint64_t> rhs) {
  const std::size_t width = std::max(lhs.size(), rhs.size());
  lhs.resize(width, 0);
  rhs.resize(width, 0);
  for (std::size_t i = 0; i < width; ++i) {
    if (lhs[i] < rhs[i])
      return -1;
    if (lhs[i] > rhs[i])
      return 1;
  }
  return 0;
}

bool VersionSatisfies(const std::string &installed, const std::string &required,
                      const std::string &op) {
  auto installed_parts = ParseVersionParts(installed);
  auto required_parts = ParseVersionParts(required);
  if (!installed_parts || !required_parts)
    return false;

  const int comparison = CompareVersionParts(*installed_parts, *required_parts);
  if (op == ">=")
    return comparison >= 0;
  if (op == "<=")
    return comparison <= 0;
  if (op == "==")
    return comparison == 0;
  if (op == ">")
    return comparison > 0;
  if (op == "<")
    return comparison < 0;
  if (op == "~=") {
    if (comparison < 0)
      return false;
    auto upper = *required_parts;
    if (upper.size() < 2)
      return true;
    ++upper[upper.size() - 2];
    upper.back() = 0;
    return CompareVersionParts(*installed_parts, std::move(upper)) < 0;
  }
  return false;
}

std::string Sanitize(std::string value) {
  for (char &c : value) {
    if (!std::isalnum(static_cast<unsigned char>(c)))
      c = '_';
  }
  return value;
}

std::string StripGoPackageLine(const std::string &source, std::string *package_line) {
  std::istringstream input(source);
  std::ostringstream output;
  std::string line;
  bool removed = false;
  while (std::getline(input, line)) {
    const std::string trimmed = TrimAscii(line);
    if (!removed && trimmed.rfind("package ", 0) == 0) {
      if (package_line && package_line->empty())
        *package_line = trimmed;
      removed = true;
      continue;
    }
    output << line << '\n';
  }
  return output.str();
}

} // namespace

std::string BuildVendoredSourceBundle(const std::string &poly_entry,
                                      const std::string &language,
                                      const std::string &consumer_source,
                                      const std::string &aux_dir,
                                      std::string *error,
                                      bool require_local) {
  if (error)
    error->clear();
  if (poly_entry.empty() || consumer_source.empty() || aux_dir.empty()) {
    if (require_local) {
      if (error)
        *error = "strict local package resolution requires a Poly entry, consumer source, and "
                 "auxiliary directory";
      return {};
    }
    return consumer_source;
  }

  const fs::path entry(poly_entry);
  const fs::path project_root = entry.parent_path();
  const auto declared = DeclaredPackages(entry, language);
  if (declared.empty())
    return consumer_source;

  std::map<std::string, std::vector<const DeclaredPackage *>> declarations_by_name;
  for (const auto &package : declared)
    declarations_by_name[package.name].push_back(&package);

  std::vector<std::pair<std::string, fs::path>> sources;
  std::error_code ec;
  const fs::path packages_root = project_root / "packages";
  if (!fs::is_directory(packages_root, ec)) {
    if (require_local && error) {
      *error = "required local " + LowerAscii(language) + " package '" +
               declarations_by_name.begin()->first +
               "' cannot be resolved because the project has no packages directory";
    }
    if (require_local)
      return {};
    return consumer_source;
  }

  const fs::path canonical_packages_root = fs::weakly_canonical(packages_root, ec);
  if (ec) {
    if (error)
      *error = "cannot canonicalize project packages directory";
    return {};
  }

  std::vector<fs::path> package_roots;
  for (fs::directory_iterator it(packages_root, ec), end; !ec && it != end;
       it.increment(ec)) {
    if (it->is_directory(ec))
      package_roots.push_back(it->path());
  }
  if (ec && require_local) {
    if (error)
      *error = "cannot enumerate project packages directory";
    return {};
  }
  std::sort(package_roots.begin(), package_roots.end());

  struct ManifestCandidate {
    SourceManifest manifest;
    fs::path canonical_root;
  };
  std::map<std::string, std::vector<ManifestCandidate>> candidates;
  for (const auto &package_root : package_roots) {
    std::error_code canonical_ec;
    const fs::path canonical_root = fs::weakly_canonical(package_root, canonical_ec);
    if (canonical_ec) {
      if (require_local) {
        if (error)
          *error = "cannot canonicalize local package directory '" +
                   package_root.string() + "'";
        return {};
      }
      continue;
    }
    if (!Within(canonical_root, canonical_packages_root)) {
      if (require_local) {
        if (error)
          *error = "local package directory '" + package_root.string() +
                   "' escapes the canonical project packages root";
        return {};
      }
      continue;
    }

    const auto manifest = ReadManifest(package_root / "poly.package.toml");
    if (!manifest || manifest->language != LowerAscii(language) ||
        declarations_by_name.count(manifest->name) == 0)
      continue;

    candidates[manifest->name].push_back(ManifestCandidate{*manifest, canonical_root});
  }

  if (require_local) {
    for (const auto &[name, declarations] : declarations_by_name) {
      (void)declarations;
      const auto found = candidates.find(name);
      const std::size_t count = found == candidates.end() ? 0 : found->second.size();
      if (count != 1) {
        if (error) {
          *error = "required local " + LowerAscii(language) + " package '" + name +
                   "' must resolve to exactly one matching poly.package.toml manifest; found " +
                   std::to_string(count);
        }
        return {};
      }
      // C++ packages may be header-only: their include_dir is resolved by the
      // dedicated C++ manifest path before this source bundler runs. Other
      // built-in source languages need an explicit source unit here.
      if (found->second.front().manifest.source.empty() &&
          LowerAscii(language) != "cpp") {
        if (error)
          *error = "required local " + LowerAscii(language) + " package '" + name +
                   "' manifest must declare a source file";
        return {};
      }
    }
  }

  for (const auto &[name, matching_candidates] : candidates) {
    for (const auto &candidate : matching_candidates) {
      const auto &manifest = candidate.manifest;
      for (const DeclaredPackage *declaration : declarations_by_name.at(name)) {
        if (declaration->version_op.empty())
          continue;
        if (declaration->version_constraint.empty()) {
          if (error)
            *error = "local package '" + name + "' has an empty version constraint";
          return {};
        }
        if (manifest.version.empty()) {
          if (error)
            *error = "local package '" + name +
                     "' must declare a manifest version to satisfy " +
                     declaration->version_op + " " + declaration->version_constraint;
          return {};
        }
        if (!VersionSatisfies(manifest.version, declaration->version_constraint,
                              declaration->version_op)) {
          if (error)
            *error = "local package '" + name + "' version " + manifest.version +
                     " does not satisfy " + declaration->version_op + " " +
                     declaration->version_constraint;
          return {};
        }
      }

      if (manifest.source.empty())
        continue;

      const fs::path relative(manifest.source);
      if (!SafeRelative(relative)) {
        if (error)
          *error = "vendored package '" + manifest.name +
                   "' has an unsafe source path";
        return {};
      }
      const fs::path source_path =
          fs::weakly_canonical(candidate.canonical_root / relative, ec);
      if (ec || !Within(source_path, candidate.canonical_root) ||
          !fs::is_regular_file(source_path, ec)) {
        if (error)
          *error = "vendored package '" + manifest.name +
                   "' source file is missing or escapes the package root";
        return {};
      }
      std::ifstream source_probe(source_path, std::ios::binary);
      if (!source_probe) {
        if (error)
          *error = "vendored package '" + manifest.name +
                   "' source file cannot be read";
        return {};
      }
      sources.emplace_back(manifest.name, source_path);
    }
  }

  if (sources.empty())
    return consumer_source;

  const std::string consumer_text = ReadFile(consumer_source);
  if (consumer_text.empty()) {
    if (error)
      *error = "cannot read foreign consumer source '" + consumer_source + "'";
    return {};
  }

  fs::create_directories(aux_dir, ec);
  if (ec) {
    if (error)
      *error = "cannot create auxiliary directory for vendored source bundle";
    return {};
  }
  const fs::path input_path(consumer_source);
  const fs::path bundle_path =
      fs::path(aux_dir) /
      (Sanitize(LowerAscii(language) + "_" + input_path.stem().string() +
                "_vendored") + input_path.extension().string());
  std::ofstream output(bundle_path, std::ios::binary | std::ios::trunc);
  if (!output) {
    if (error)
      *error = "cannot create vendored source bundle '" + bundle_path.string() + "'";
    return {};
  }

  if (LowerAscii(language) == "go") {
    std::string package_line;
    const std::string consumer_body = StripGoPackageLine(consumer_text, &package_line);
    if (package_line.empty()) {
      if (error)
        *error = "Go consumer source has no package declaration";
      return {};
    }
    output << package_line << "\n\n";
    for (const auto &[name, path] : sources) {
      std::string ignored_package;
      output << "// polyc vendored package: " << name << "\n"
             << StripGoPackageLine(ReadFile(path), &ignored_package) << "\n";
    }
    output << "// polyc consumer: " << input_path.filename().string() << "\n"
           << consumer_body;
  } else {
    const std::string comment = LowerAscii(language) == "python" ? "# " : "// ";
    for (const auto &[name, path] : sources) {
      output << comment << "polyc vendored package: " << name << "\n"
             << ReadFile(path) << "\n";
    }
    output << comment << "polyc consumer: " << input_path.filename().string() << "\n"
           << consumer_text;
  }

  if (!output.good()) {
    if (error)
      *error = "failed while writing vendored source bundle '" + bundle_path.string() + "'";
    return {};
  }
  return bundle_path.string();
}

} // namespace polyglot::tools
