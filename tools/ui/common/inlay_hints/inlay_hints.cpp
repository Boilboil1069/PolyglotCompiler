/**
 * @file     inlay_hints.cpp
 * @brief    Implementation of `inlay_hints.h`.
 *
 * @ingroup  Tool / polyui
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/inlay_hints/inlay_hints.h"

namespace polyglot::tools::ui::inlay {

std::string InlayKindName(InlayKind k) {
  return k == InlayKind::kType ? "type" : "parameter";
}

std::optional<InlayKind> InlayKindFromName(const std::string &name) {
  if (name == "type")      return InlayKind::kType;
  if (name == "parameter") return InlayKind::kParameter;
  return std::nullopt;
}

std::vector<InlayHint> InlayHintProvider::Produce(
    const std::vector<Declaration> &declarations,
    const std::vector<CallArgument> &arguments) const {
  std::vector<InlayHint> out;
  if (settings_.show_type_hints) {
    for (const auto &d : declarations) {
      if (d.inferred_type.empty()) continue;
      InlayHint h;
      h.position = d.name_end;
      h.kind = InlayKind::kType;
      h.label = ": " + d.inferred_type;
      h.padding_left = true;
      out.push_back(std::move(h));
    }
  }
  if (settings_.show_parameter_hints) {
    for (const auto &a : arguments) {
      if (a.parameter_name.empty()) continue;
      InlayHint h;
      h.position = a.argument_start;
      h.kind = InlayKind::kParameter;
      h.label = a.parameter_name + ":";
      h.padding_right = true;
      out.push_back(std::move(h));
    }
  }
  return out;
}

}  // namespace polyglot::tools::ui::inlay
