/**
 * @file     tree_sitter_runtime.cpp
 * @brief    Self-contained implementation of the tree-sitter-shaped
 *           runtime used by polyls and the IDE editor.
 *
 * The runtime is a pure-C++ lexer/parser that supports the polyglot
 * languages bundled with PolyglotCompiler (Ploy + C++ + Python +
 * Rust + Java + C#).  It is wire-compatible with the tree-sitter API
 * we use (Parse / Edit / Tokens / Folds / Outline / SmartSelect),
 * which means we can swap individual languages out for real
 * tree-sitter grammars later without touching call sites.
 *
 * @ingroup  Tool / polyls
 * @author   Manning Cyrus
 * @date     2026-05-05
 */
#include "tools/ui/common/syntax/tree_sitter_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "tools/polyls/grammar/grammar_descriptor.h"

namespace polyglot::polyls::ts {

namespace {

namespace gr = polyglot::polyls::grammar;

bool IsIdStart(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}
bool IsIdChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/// Split `text` into lines preserving original byte offsets.
struct LineView {
  std::uint32_t line{0};
  std::size_t start_byte{0};
  std::string_view content;
};
std::vector<LineView> SplitLines(const std::string &text) {
  std::vector<LineView> out;
  std::size_t i = 0;
  std::uint32_t line = 0;
  while (i <= text.size()) {
    std::size_t j = text.find('\n', i);
    if (j == std::string::npos) {
      out.push_back({line, i, std::string_view(text).substr(i)});
      break;
    }
    out.push_back(
        {line, i, std::string_view(text).substr(i, j - i)});
    i = j + 1;
    ++line;
  }
  return out;
}

/// Derive a baseline language identifier from any of the aliases that
/// editors hand us at didOpen time.
std::string CanonicalLanguage(const std::string &lang) {
  if (lang == "c++" || lang == "C++") return "cpp";
  if (lang == "poly") return "ploy";
  if (lang == "c#" || lang == "dotnet" || lang == "C#") return "csharp";
  return lang;
}

/// Look up the type/modifier for a CompilerService-style token kind.
gr::SemanticMapping ResolveMapping(const gr::GrammarDescriptor &g,
                                   const std::string &kind) {
  auto it = g.kind_map.find(kind);
  if (it == g.kind_map.end()) return {4, 0};  // variable as fallback
  return it->second;
}

/// Lex a single line into raw word/comment/string spans.  This is
/// shared across every host language: the per-language descriptor
/// then promotes words to the appropriate semantic-token kind.
struct RawSpan {
  std::uint32_t start{0};
  std::uint32_t length{0};
  std::string text;
  std::string kind;  // "word", "string", "number", "comment", "operator"
};

std::vector<RawSpan> LexLine(std::string_view line,
                             const gr::GrammarDescriptor &g) {
  std::vector<RawSpan> out;
  const std::size_t n = line.size();
  std::size_t i = 0;
  // Pick a comment marker per language family.
  const bool is_python = g.name == "python";
  const bool is_ploy = g.name == "ploy";
  while (i < n) {
    const char c = line[i];
    // Whitespace.
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
      continue;
    }
    // `// …` and `# …` line comments.
    if ((c == '/' && i + 1 < n && line[i + 1] == '/') ||
        ((is_python || is_ploy) && c == '#')) {
      RawSpan s;
      s.start = static_cast<std::uint32_t>(i);
      s.length = static_cast<std::uint32_t>(n - i);
      s.text = std::string(line.substr(i));
      s.kind = "comment";
      out.push_back(std::move(s));
      break;
    }
    // String literals — both " and ' delimited, with backslash
    // escape.  Multi-line strings are not modelled here; the lexer
    // simply terminates at end-of-line which is the same heuristic
    // every editor highlighter in the codebase uses today.
    if (c == '"' || c == '\'') {
      const char quote = c;
      const std::size_t start = i++;
      while (i < n && line[i] != quote) {
        if (line[i] == '\\' && i + 1 < n) ++i;
        ++i;
      }
      if (i < n) ++i;  // consume closing quote
      RawSpan s;
      s.start = static_cast<std::uint32_t>(start);
      s.length = static_cast<std::uint32_t>(i - start);
      s.text = std::string(line.substr(start, i - start));
      s.kind = "string";
      out.push_back(std::move(s));
      continue;
    }
    // Numbers: leading digit or `.<digit>`.
    if (std::isdigit(static_cast<unsigned char>(c)) ||
        (c == '.' && i + 1 < n &&
         std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
      const std::size_t start = i;
      while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) ||
                       line[i] == '.' || line[i] == '_')) {
        ++i;
      }
      RawSpan s;
      s.start = static_cast<std::uint32_t>(start);
      s.length = static_cast<std::uint32_t>(i - start);
      s.text = std::string(line.substr(start, i - start));
      s.kind = "number";
      out.push_back(std::move(s));
      continue;
    }
    // Identifiers / keywords.
    if (IsIdStart(c)) {
      const std::size_t start = i;
      while (i < n && IsIdChar(line[i])) ++i;
      RawSpan s;
      s.start = static_cast<std::uint32_t>(start);
      s.length = static_cast<std::uint32_t>(i - start);
      s.text = std::string(line.substr(start, i - start));
      s.kind = "word";
      out.push_back(std::move(s));
      continue;
    }
    // Single-character operators / punctuation.  We collapse runs of
    // identical operator chars so e.g. `==` is one span.
    const std::size_t start = i;
    const char op = c;
    while (i < n && !std::isalnum(static_cast<unsigned char>(line[i])) &&
           !std::isspace(static_cast<unsigned char>(line[i])) &&
           line[i] != '"' && line[i] != '\'' && line[i] == op) {
      ++i;
    }
    if (i == start) ++i;  // safety: always advance
    RawSpan s;
    s.start = static_cast<std::uint32_t>(start);
    s.length = static_cast<std::uint32_t>(i - start);
    s.text = std::string(line.substr(start, i - start));
    s.kind = "operator";
    out.push_back(std::move(s));
  }
  return out;
}

/// Promote a raw word span to the right CompilerService-style kind
/// using the descriptor's keyword / type / builtin sets.
std::string ClassifyWord(const std::string &text,
                         const gr::GrammarDescriptor &g) {
  if (g.keywords.count(text)) {
    // Ploy directive keywords (LINK / IMPORT / EXPORT …) use the
    // distinct "link" style so the editor can paint them differently.
    static const std::unordered_set<std::string> kDirective = {
        "LINK", "IMPORT", "EXPORT", "MAP_TYPE", "MAP_FUNC",
        "PIPELINE", "CALL", "NEW",  "METHOD",   "GET",
        "SET",  "WITH",   "DELETE","EXTEND",   "CONVERT",
        "CONFIG"};
    if (g.name == "ploy" && kDirective.count(text)) return "link";
    return "keyword";
  }
  if (g.primitive_types.count(text)) return "type";
  if (g.builtins.count(text)) return "builtin";
  return "identifier";
}

/// Lex the whole document and emit absolute-coord semantic tokens.
std::vector<SemanticToken> LexAll(const std::string &source,
                                  const gr::GrammarDescriptor &g) {
  std::vector<SemanticToken> tokens;
  const auto lines = SplitLines(source);
  for (const auto &lv : lines) {
    auto spans = LexLine(lv.content, g);
    for (auto &s : spans) {
      if (s.kind == "word") s.kind = ClassifyWord(s.text, g);
      auto map = ResolveMapping(g, s.kind);
      // Skip pure-operator tokens: editors overwhelmingly leave them
      // unstyled and emitting them inflates wire payloads.
      if (s.kind == "operator") continue;
      SemanticToken tok;
      tok.line = lv.line;
      tok.start_char = s.start;
      tok.length = s.length;
      tok.type_index = map.type_index;
      tok.modifier_mask = map.modifier_mask;
      tokens.push_back(tok);
    }
  }
  return tokens;
}

/// Compute folding regions by tracking brace / indentation depth.
/// For brace-style languages we fold on `{` … `}`; for Python we fold
/// on indent runs after lines ending with `:`.
std::vector<FoldingRange> ComputeFolds(const std::string &source,
                                       const gr::GrammarDescriptor &g) {
  std::vector<FoldingRange> out;
  const auto lines = SplitLines(source);
  if (g.name == "python") {
    // Indentation-based folding.
    auto indent_of = [](std::string_view s) {
      std::size_t i = 0;
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
      return i;
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
      const auto &line = lines[i].content;
      if (line.empty()) continue;
      if (line.find(':') == std::string_view::npos) continue;
      // Trim trailing comments before checking colon position.
      const std::size_t hash = line.find('#');
      const std::string_view code =
          hash == std::string_view::npos ? line : line.substr(0, hash);
      std::size_t k = code.size();
      while (k > 0 &&
             std::isspace(static_cast<unsigned char>(code[k - 1])))
        --k;
      if (k == 0 || code[k - 1] != ':') continue;
      const std::size_t base = indent_of(line);
      std::size_t end = i;
      for (std::size_t j = i + 1; j < lines.size(); ++j) {
        const auto &lj = lines[j].content;
        if (lj.empty()) continue;
        if (indent_of(lj) <= base) break;
        end = j;
      }
      if (end > i) {
        FoldingRange fr;
        fr.start_line = lines[i].line;
        fr.start_character = static_cast<std::uint32_t>(line.size());
        fr.end_line = lines[end].line;
        fr.end_character =
            static_cast<std::uint32_t>(lines[end].content.size());
        fr.kind = "region";
        out.push_back(fr);
      }
    }
    return out;
  }
  // Brace-style folding.  Walk byte by byte but track newlines.
  std::vector<std::size_t> stack;        // open positions (byte)
  std::vector<std::uint32_t> stack_line; // open lines
  std::vector<std::uint32_t> stack_col;  // open column on that line
  std::uint32_t cur_line = 0;
  std::uint32_t cur_col = 0;
  bool in_string = false;
  char quote = '"';
  bool in_line_comment = false;
  for (std::size_t i = 0; i < source.size(); ++i) {
    const char c = source[i];
    if (c == '\n') {
      ++cur_line;
      cur_col = 0;
      in_line_comment = false;
      continue;
    }
    if (in_line_comment) {
      ++cur_col;
      continue;
    }
    if (in_string) {
      if (c == '\\' && i + 1 < source.size()) {
        ++i;
        cur_col += 2;
        continue;
      }
      if (c == quote) in_string = false;
      ++cur_col;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
      in_line_comment = true;
      cur_col += 2;
      ++i;
      continue;
    }
    if (c == '"' || c == '\'') {
      in_string = true;
      quote = c;
      ++cur_col;
      continue;
    }
    if (c == '{') {
      stack.push_back(i);
      stack_line.push_back(cur_line);
      stack_col.push_back(cur_col);
    } else if (c == '}') {
      if (!stack.empty()) {
        FoldingRange fr;
        fr.start_line = stack_line.back();
        fr.start_character = stack_col.back();
        fr.end_line = cur_line;
        fr.end_character = cur_col;
        fr.kind = "region";
        if (fr.end_line > fr.start_line) out.push_back(fr);
        stack.pop_back();
        stack_line.pop_back();
        stack_col.pop_back();
      }
    }
    ++cur_col;
  }
  return out;
}

/// Outline = top-level declarations.  Cheap heuristic: scan each line
/// for the language's "introducer" keyword (FUNC / fn / def / class /
/// struct / void / public).  Good enough for the IDE outline panel
/// and matches what tree-sitter grammars surface for most languages.
std::vector<OutlineNode> ComputeOutline(const std::string &source,
                                        const gr::GrammarDescriptor &g) {
  std::vector<OutlineNode> out;
  const auto lines = SplitLines(source);
  for (const auto &lv : lines) {
    const auto trimmed_start =
        lv.content.find_first_not_of(" \t");
    if (trimmed_start == std::string_view::npos) continue;
    std::string_view t = lv.content.substr(trimmed_start);
    auto try_emit = [&](const std::string &intro,
                        const std::string &kind) -> bool {
      if (t.size() <= intro.size()) return false;
      if (t.compare(0, intro.size(), intro) != 0) return false;
      const char after = t[intro.size()];
      if (!std::isspace(static_cast<unsigned char>(after))) return false;
      std::size_t k = intro.size();
      while (k < t.size() &&
             std::isspace(static_cast<unsigned char>(t[k])))
        ++k;
      const std::size_t name_start = k;
      while (k < t.size() && IsIdChar(t[k])) ++k;
      if (k == name_start) return false;
      OutlineNode n;
      n.name = std::string(t.substr(name_start, k - name_start));
      n.detail = intro;
      n.kind = kind;
      n.line = lv.line;
      n.start_character = static_cast<std::uint32_t>(trimmed_start);
      n.end_line = lv.line;
      n.end_character = static_cast<std::uint32_t>(lv.content.size());
      out.push_back(std::move(n));
      return true;
    };
    if (g.name == "ploy") {
      if (try_emit("FUNC", "function")) continue;
      if (try_emit("PIPELINE", "function")) continue;
      if (try_emit("STRUCT", "struct")) continue;
      if (try_emit("LET", "variable")) continue;
      if (try_emit("VAR", "variable")) continue;
    } else if (g.name == "python") {
      if (try_emit("def", "function")) continue;
      if (try_emit("class", "struct")) continue;
    } else if (g.name == "rust") {
      if (try_emit("fn", "function")) continue;
      if (try_emit("struct", "struct")) continue;
      if (try_emit("enum", "struct")) continue;
      if (try_emit("trait", "struct")) continue;
    } else {
      if (try_emit("class", "struct")) continue;
      if (try_emit("struct", "struct")) continue;
      if (try_emit("namespace", "module")) continue;
      if (try_emit("void", "function")) continue;
      if (try_emit("public", "function")) continue;
    }
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Tree
// ---------------------------------------------------------------------------

Tree::Tree(std::string language, std::string source)
    : language_(std::move(language)), source_(std::move(source)) {
  Reparse();
}

void Tree::Edit(std::size_t start_byte, std::size_t old_end_byte,
                const std::string &new_text) {
  if (start_byte > source_.size()) start_byte = source_.size();
  if (old_end_byte > source_.size()) old_end_byte = source_.size();
  if (old_end_byte < start_byte) old_end_byte = start_byte;
  source_.replace(start_byte, old_end_byte - start_byte, new_text);
  Reparse();
}

void Tree::Reparse() {
  tokens_.clear();
  folds_.clear();
  outline_.clear();
  const std::string canon = CanonicalLanguage(language_);
  const gr::GrammarDescriptor *g = gr::FindGrammar(canon);
  if (!g) return;
  tokens_ = LexAll(source_, *g);
  folds_ = ComputeFolds(source_, *g);
  outline_ = ComputeOutline(source_, *g);
}

std::vector<SelectionRange> Tree::SmartSelect(
    std::uint32_t line, std::uint32_t character) const {
  std::vector<SelectionRange> out;
  // Innermost: the token under the caret.
  for (const auto &t : tokens_) {
    if (t.line == line && character >= t.start_char &&
        character <= t.start_char + t.length) {
      SelectionRange r;
      r.start_line = t.line;
      r.start_character = t.start_char;
      r.end_line = t.line;
      r.end_character = t.start_char + t.length;
      out.push_back(r);
      break;
    }
  }
  // Next: the whole line.
  const auto lines = SplitLines(source_);
  if (line < lines.size()) {
    SelectionRange r;
    r.start_line = line;
    r.start_character = 0;
    r.end_line = line;
    r.end_character =
        static_cast<std::uint32_t>(lines[line].content.size());
    out.push_back(r);
  }
  // Next: the smallest enclosing fold.
  const FoldingRange *best = nullptr;
  for (const auto &f : folds_) {
    if (f.start_line <= line && line <= f.end_line) {
      if (!best || (f.end_line - f.start_line) <
                       (best->end_line - best->start_line)) {
        best = &f;
      }
    }
  }
  if (best) {
    SelectionRange r;
    r.start_line = best->start_line;
    r.start_character = best->start_character;
    r.end_line = best->end_line;
    r.end_character = best->end_character;
    out.push_back(r);
  }
  // Outermost: the whole document.
  if (!lines.empty()) {
    SelectionRange r;
    r.start_line = 0;
    r.start_character = 0;
    r.end_line = lines.back().line;
    r.end_character =
        static_cast<std::uint32_t>(lines.back().content.size());
    out.push_back(r);
  }
  return out;
}

std::unique_ptr<Tree> Parse(const std::string &language_id,
                            const std::string &source) {
  if (!gr::FindGrammar(CanonicalLanguage(language_id))) return nullptr;
  return std::make_unique<Tree>(language_id, source);
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

std::vector<std::uint32_t> EncodeSemanticTokens(
    const std::vector<SemanticToken> &tokens) {
  std::vector<std::uint32_t> out;
  out.reserve(tokens.size() * 5);
  std::uint32_t prev_line = 0;
  std::uint32_t prev_char = 0;
  for (const auto &t : tokens) {
    const std::uint32_t delta_line = t.line - prev_line;
    const std::uint32_t delta_char =
        delta_line == 0 ? t.start_char - prev_char : t.start_char;
    out.push_back(delta_line);
    out.push_back(delta_char);
    out.push_back(t.length);
    out.push_back(t.type_index);
    out.push_back(t.modifier_mask);
    prev_line = t.line;
    prev_char = t.start_char;
  }
  return out;
}

std::vector<std::uint32_t> EncodeSemanticTokensRange(
    const std::vector<SemanticToken> &tokens, std::uint32_t start_line,
    std::uint32_t end_line) {
  std::vector<SemanticToken> sub;
  sub.reserve(tokens.size());
  for (const auto &t : tokens) {
    if (t.line >= start_line && t.line <= end_line) sub.push_back(t);
  }
  return EncodeSemanticTokens(sub);
}

std::vector<SemanticToken> DecodeSemanticTokens(
    const std::vector<std::uint32_t> &data) {
  std::vector<SemanticToken> out;
  if (data.size() < 5) return out;
  out.reserve(data.size() / 5);
  std::uint32_t line = 0;
  std::uint32_t character = 0;
  for (std::size_t i = 0; i + 4 < data.size(); i += 5) {
    const std::uint32_t delta_line = data[i];
    const std::uint32_t delta_char = data[i + 1];
    line += delta_line;
    character = (delta_line == 0) ? character + delta_char : delta_char;
    SemanticToken t;
    t.line = line;
    t.start_char = character;
    t.length = data[i + 2];
    t.type_index = data[i + 3];
    t.modifier_mask = data[i + 4];
    out.push_back(t);
  }
  return out;
}

}  // namespace polyglot::polyls::ts
