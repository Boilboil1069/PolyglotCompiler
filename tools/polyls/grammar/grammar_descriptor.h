/**
 * @file     grammar_descriptor.h
 * @brief    Grammar descriptors for the PolyglotCompiler tree-sitter
 *           runtime adapter (demand 2026-04-28-24).
 *
 * Each descriptor binds one editor language id (e.g. "ploy", "cpp",
 * "python", "rust", "java", "csharp") to:
 *
 *   • a stable name (used by the runtime to pick the right grammar);
 *   • a list of keyword/type/builtin lexemes that the descriptor can
 *     recognise without needing the full compiler frontend (used as a
 *     fallback when the lexer service is unavailable);
 *   • a mapping from the @ref tools::ui::TokenInfo `kind` strings the
 *     CompilerService emits to the LSP semantic-token `tokenTypes`
 *     legend index, plus optional modifier bits.
 *
 * The descriptors are intentionally header-only data tables so they
 * are safe to include from both the polyls server library and the
 * editor process.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace polyglot::polyls::grammar {

// ── LSP semantic-token legend (3.16) ─────────────────────────────────
// We expose a stable subset that maps cleanly onto our compiler-driven
// token kinds.  Order MUST match the indices used by the runtime.
inline constexpr std::array<std::string_view, 11> kTokenTypes = {
    "namespace",  // 0
    "type",       // 1
    "struct",     // 2
    "function",   // 3
    "variable",   // 4
    "parameter",  // 5
    "keyword",    // 6
    "comment",    // 7
    "string",     // 8
    "number",     // 9
    "operator",   // 10
};

inline constexpr std::array<std::string_view, 5> kTokenModifiers = {
    "declaration",  // bit 0
    "readonly",     // bit 1
    "static",       // bit 2
    "deprecated",   // bit 3
    "definition",   // bit 4
};

inline constexpr std::uint32_t kModDeclaration = 1u << 0;
inline constexpr std::uint32_t kModReadonly = 1u << 1;
inline constexpr std::uint32_t kModStatic = 1u << 2;
inline constexpr std::uint32_t kModDeprecated = 1u << 3;
inline constexpr std::uint32_t kModDefinition = 1u << 4;

/// Mapping of an internal CompilerService token kind to a semantic
/// token index plus modifier bitmask.
struct SemanticMapping {
  std::uint32_t type_index{0};
  std::uint32_t modifier_mask{0};
};

/// One grammar descriptor.  The runtime holds one per supported
/// language id.
struct GrammarDescriptor {
  std::string name;
  std::string display_name;
  std::vector<std::string> file_extensions;
  std::unordered_set<std::string> keywords;
  std::unordered_set<std::string> primitive_types;
  std::unordered_set<std::string> builtins;
  std::unordered_map<std::string, SemanticMapping> kind_map;
};

/// Built-in grammar table.  Returns a stable reference; safe to keep
/// for the lifetime of the process.
const std::unordered_map<std::string, GrammarDescriptor> &
KnownGrammars();

/// Resolve a descriptor by language id.  Returns nullptr when the
/// language is not registered.
const GrammarDescriptor *FindGrammar(const std::string &language_id);

}  // namespace polyglot::polyls::grammar
