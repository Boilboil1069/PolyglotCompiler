/**
 * @file     inlay_hints.h
 * @brief    LSP `textDocument/inlayHint` value model for polyls.
 *
 * Two hint families are supported: type hints (rendered after a
 * declaration with leading colon) and parameter-name hints
 * (rendered before the argument expression).  The provider is
 * value-only — the LSP layer wraps the produced hints into LSP
 * `InlayHint` objects.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace polyglot::tools::ui::inlay {

enum class InlayKind {
  kType,
  kParameter,
};

std::string InlayKindName(InlayKind k);
std::optional<InlayKind> InlayKindFromName(const std::string &name);

struct Position {
  int line{0};                 ///< 0-based line, LSP convention.
  int character{0};            ///< 0-based character.
};

struct InlayHint {
  Position position;
  InlayKind kind{InlayKind::kType};
  std::string label;           ///< e.g. ": HANDLE<python::torch::nn::Linear>".
  bool padding_left{false};
  bool padding_right{false};
};

/// A single declaration discovered by polyls' analyser.
struct Declaration {
  std::string name;
  std::string inferred_type;   ///< e.g. "HANDLE<python::torch::nn::Linear>".
  Position name_end;           ///< Position right after the identifier.
};

/// A single call-site argument discovered by polyls.
struct CallArgument {
  std::string parameter_name;  ///< Formal parameter name.
  Position argument_start;     ///< Position where the argument starts.
};

struct InlayHintSettings {
  bool show_type_hints{true};
  bool show_parameter_hints{true};
};

class InlayHintProvider {
 public:
  void set_settings(InlayHintSettings s) { settings_ = s; }
  const InlayHintSettings &settings() const { return settings_; }

  /// Produce the hints for the currently visible range.  Either
  /// `declarations` or `arguments` may be empty.
  std::vector<InlayHint> Produce(
      const std::vector<Declaration> &declarations,
      const std::vector<CallArgument> &arguments) const;

 private:
  InlayHintSettings settings_{};
};

}  // namespace polyglot::tools::ui::inlay
