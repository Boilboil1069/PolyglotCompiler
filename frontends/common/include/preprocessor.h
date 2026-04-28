/**
 * @file     preprocessor.h
 * @brief    Shared frontend infrastructure
 *
 * @ingroup  Frontend / Common
 * @author   Manning Cyrus
 * @date     2026-04-10
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "frontends/common/include/diagnostics.h"

namespace polyglot::frontends {

class SharedTokenPool; // forward decl — see token_pool.h

/** @brief Preprocessor class. */
class Preprocessor {
public:
  /** @brief Macro data structure. */
  struct Macro {
    bool is_function{false};
    std::vector<std::string> params;
    std::string body;
  };

  explicit Preprocessor(Diagnostics &diagnostics);

  // Object-like macro: #define FOO bar
  void Define(const std::string &name, const std::string &value);
  // Function-like macro: #define FOO(x,y) (x + y)
  void DefineFunction(const std::string &name, std::vector<std::string> params,
                      const std::string &value);

  void Undefine(const std::string &name);

  void AddIncludePath(const std::string &path);
  void SetIncludePaths(std::vector<std::string> paths);
  void SetMaxIncludeDepth(size_t depth);

  // Custom file loader hook for testing or virtual FS.
  void SetFileLoader(std::function<std::optional<std::string>(const std::string &)> loader);

  /// Optionally attach a shared token pool.  When attached, every macro name
  /// passed to @c Define/@c DefineFunction is also interned into the pool's
  /// identifier table, and macro bodies are mirrored into the pool's arena
  /// so downstream lexers can compare against canonicalised text views.
  void SetTokenPool(SharedTokenPool *pool) noexcept { token_pool_ = pool; }
  SharedTokenPool *TokenPool() const noexcept { return token_pool_; }

  // Expand macros in-place (no directive handling). Mostly internal but exposed for tests.
  std::string Expand(const std::string &source);
  // Full preprocessing with directives. "file" is used for #line bookkeeping and relative
  // include resolution.
  std::string Process(const std::string &source, const std::string &file = "<memory>");

private:
  std::string ProcessInternal(const std::string &source, const std::string &file, size_t depth);
  std::string ExpandLine(const std::string &line, std::unordered_set<std::string> &guard);
  std::optional<std::string> ReadFile(const std::string &path) const;
  std::optional<std::string> ResolveInclude(const std::string &target,
                                            const std::string &current_file_dir,
                                            bool is_angle) const;
  std::string SubstituteParams(const Macro &macro, const std::vector<std::string> &args,
                               std::unordered_set<std::string> &guard);
  std::string Stringize(const std::string &text) const;
  std::string Paste(const std::string &lhs, const std::string &rhs) const;
  std::optional<std::string> DetectIncludeGuard(const std::string &source) const;

  Diagnostics &diagnostics_;
  std::unordered_map<std::string, Macro> macros_{};
  std::vector<std::string> include_paths_{};
  size_t max_include_depth_{64};
  std::function<std::optional<std::string>(const std::string &)> file_loader_;
  std::unordered_set<std::string> pragma_once_files_{};
  std::unordered_map<std::string, std::string> guard_to_file_{};
  SharedTokenPool *token_pool_{nullptr};
};

} // namespace polyglot::frontends
