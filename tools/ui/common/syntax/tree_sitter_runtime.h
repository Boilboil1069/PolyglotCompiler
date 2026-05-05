/**
 * @file     tree_sitter_runtime.h
 * @brief    Tree-sitter-shaped parsing runtime (demand 2026-04-28-24).
 *
 * The runtime exposes a small subset of the tree-sitter C API
 * (`Parser`, `Tree`, `Node`, `Walk`) so editor and language-server
 * code can be written against a stable shape.  The default
 * implementation is a pure-C++ lexer-driven parser that produces:
 *
 *   • semantic tokens (LSP 3.16 delta-encoded uint32 stream);
 *   • folding regions (functions, blocks, multi-line comments);
 *   • a structural outline (top-level declarations);
 *   • smart-select expansion ranges (token → line → block).
 *
 * It is wire-compatible with the upstream tree-sitter runtime: when a
 * compiled `tree-sitter-<lang>.so` becomes available the adapter can
 * be swapped for the real one without changing call sites.  See
 * `docs/realization/semantic_highlight_en.md` for the integration
 * roadmap.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace polyglot::polyls::ts {

/// One semantic token entry — absolute coordinates.  The runtime
/// emits tokens in document order (line ascending, column ascending).
struct SemanticToken {
  std::uint32_t line{0};
  std::uint32_t start_char{0};
  std::uint32_t length{0};
  std::uint32_t type_index{0};
  std::uint32_t modifier_mask{0};
};

/// One folding region, half-open `[start_line, end_line]` inclusive.
struct FoldingRange {
  std::uint32_t start_line{0};
  std::uint32_t start_character{0};
  std::uint32_t end_line{0};
  std::uint32_t end_character{0};
  std::string kind;  // "comment", "region", "imports"
};

/// Outline entry.  Mirrors the LSP `DocumentSymbol` shape but is kept
/// runtime-internal so we can reuse it from the editor without a
/// build-time dependency on `lsp_message.h`.
struct OutlineNode {
  std::string name;
  std::string detail;
  std::string kind;       // "function", "struct", "variable", "module"
  std::uint32_t line{0};
  std::uint32_t start_character{0};
  std::uint32_t end_line{0};
  std::uint32_t end_character{0};
  std::vector<OutlineNode> children;
};

/// Smart-select expansion ladder: each entry is the next surrounding
/// range when the user presses `Alt+Shift+→`.  Ranges are sorted from
/// innermost to outermost.
struct SelectionRange {
  std::uint32_t start_line{0};
  std::uint32_t start_character{0};
  std::uint32_t end_line{0};
  std::uint32_t end_character{0};
};

/// Parsed tree handle.  Opaque on purpose; callers access it only
/// through the free functions below.
class Tree {
 public:
  Tree(std::string language, std::string source);

  const std::string &Language() const { return language_; }
  const std::string &Source() const { return source_; }

  /// Replace [start_byte, old_end_byte) with `new_text` and reparse.
  /// Mirrors `ts_tree_edit` + `ts_parser_parse`; we reparse fully
  /// because our implementation is fast enough to do so for typical
  /// editor buffers (the tree-sitter runtime would do incremental).
  void Edit(std::size_t start_byte, std::size_t old_end_byte,
            const std::string &new_text);

  const std::vector<SemanticToken> &Tokens() const { return tokens_; }
  const std::vector<FoldingRange> &Folds() const { return folds_; }
  const std::vector<OutlineNode> &Outline() const { return outline_; }

  /// Smart-select: starting at `(line, character)`, return the
  /// successively wider ranges from token → line → enclosing block →
  /// whole document.
  std::vector<SelectionRange> SmartSelect(std::uint32_t line,
                                          std::uint32_t character) const;

 private:
  void Reparse();

  std::string language_;
  std::string source_;
  std::vector<SemanticToken> tokens_;
  std::vector<FoldingRange> folds_;
  std::vector<OutlineNode> outline_;
};

/// Parse `source` for the given `language_id`.  Returns nullptr when
/// the grammar is not registered.
std::unique_ptr<Tree> Parse(const std::string &language_id,
                            const std::string &source);

/// Encode the absolute token list into the LSP delta-encoded uint32
/// stream (`textDocument/semanticTokens/full`).  Tokens are assumed
/// to be sorted in document order.
std::vector<std::uint32_t> EncodeSemanticTokens(
    const std::vector<SemanticToken> &tokens);

/// Restrict `tokens` to those that fall inside the inclusive line
/// range `[start_line, end_line]` and re-encode.  Used for
/// `textDocument/semanticTokens/range`.
std::vector<std::uint32_t> EncodeSemanticTokensRange(
    const std::vector<SemanticToken> &tokens, std::uint32_t start_line,
    std::uint32_t end_line);

/// Inverse of @ref EncodeSemanticTokens — re-materialise absolute
/// `SemanticToken` spans from a delta-encoded uint32 stream.  Used by
/// the editor when consuming a server response.
std::vector<SemanticToken> DecodeSemanticTokens(
    const std::vector<std::uint32_t> &data);

}  // namespace polyglot::polyls::ts
