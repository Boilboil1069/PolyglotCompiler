/**
 * @file     pyi_loader.cpp
 * @brief    Real `.pyi` (typeshed) stub parser.
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * Implements an indentation-aware tokenizer + recursive-descent parser for
 * the subset of Python syntax used by typeshed `.pyi` stub files.  The output
 * is a `PyiModule` whose `exports` map is consumed by the Python semantic
 * analyser to populate the symbol table of an imported module.
 *
 * The grammar handled is intentionally permissive:
 *   - `def` / `async def` signatures with `-> ReturnType`
 *   - `class Name[(Bases, ...)]:` blocks
 *   - module-level `name: Type [= value]` annotated assignments
 *   - `name = TypeExpression` aliases (treated as Any)
 *   - `from X import a, b as c` (transitively re-exports `a`, `c`)
 *   - `import X [as Y]` (records a module-typed binding)
 *   - `if cond:` / `elif:` / `else:` blocks (both branches descended)
 *   - `@overload`, `@staticmethod`, `@classmethod`, `@property` decorators
 *
 * Type expression handling covers `int`, `str`, `float`, `bool`, `bytes`,
 * `None`, `Any`, `object`, dotted class names, generic subscripts
 * (`List[X]`, `Dict[K, V]`, `Tuple[X, ...]`, `Set[X]`, `FrozenSet[X]`,
 * `Optional[X]`, `Type[X]`, `ClassVar[X]`, `Final[X]`, `Literal[...]`,
 * `Annotated[X, ...]`), `Callable[[A, ...], R]`, and PEP 604 `A | B` unions.
 */
#include "frontends/python/include/pyi_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace polyglot::python {

namespace {

namespace fs = std::filesystem;

// ============================================================================
// Tokenizer
// ============================================================================
enum class TokKind {
    kName,
    kNumber,
    kString,
    kOp,
    kNewline,
    kIndent,
    kDedent,
    kEnd,
};

struct Token {
    TokKind     kind;
    std::string text;
    int         line;
    int         col;
};

class Lexer {
  public:
    explicit Lexer(std::string src) : src_(std::move(src)) {}

    std::vector<Token> Tokenize() {
        // Track indent stack; emit INDENT/DEDENT/NEWLINE markers.
        std::vector<int> indent_stack{0};
        bool             at_line_start = true;
        int              paren_depth   = 0;

        while (pos_ < src_.size()) {
            if (at_line_start && paren_depth == 0) {
                int col = 0;
                while (pos_ < src_.size() && (src_[pos_] == ' ' || src_[pos_] == '\t')) {
                    col += (src_[pos_] == '\t') ? 8 - (col % 8) : 1;
                    ++pos_;
                    ++column_;
                }
                // Blank line / comment-only line — skip without emitting INDENT/DEDENT.
                if (pos_ >= src_.size()) break;
                if (src_[pos_] == '\n' || src_[pos_] == '#') {
                    if (src_[pos_] == '#') {
                        while (pos_ < src_.size() && src_[pos_] != '\n') {
                            ++pos_;
                            ++column_;
                        }
                    }
                    if (pos_ < src_.size() && src_[pos_] == '\n') {
                        ++pos_;
                        ++line_;
                        column_ = 1;
                    }
                    continue;
                }
                if (col > indent_stack.back()) {
                    indent_stack.push_back(col);
                    Push(TokKind::kIndent, "");
                } else {
                    while (col < indent_stack.back()) {
                        indent_stack.pop_back();
                        Push(TokKind::kDedent, "");
                    }
                }
                at_line_start = false;
                continue;
            }

            char c = src_[pos_];

            // Comments
            if (c == '#') {
                while (pos_ < src_.size() && src_[pos_] != '\n') {
                    ++pos_;
                    ++column_;
                }
                continue;
            }

            // Newline
            if (c == '\n') {
                ++pos_;
                ++line_;
                column_ = 1;
                if (paren_depth == 0) {
                    Push(TokKind::kNewline, "\n");
                    at_line_start = true;
                }
                continue;
            }

            // Whitespace
            if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
                ++column_;
                continue;
            }

            // Line continuation
            if (c == '\\' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '\n') {
                pos_ += 2;
                ++line_;
                column_ = 1;
                continue;
            }

            // String literals (only the simple cases we need)
            if (c == '"' || c == '\'') {
                LexString();
                continue;
            }
            if ((c == 'r' || c == 'R' || c == 'b' || c == 'B' || c == 'u' || c == 'U' || c == 'f' || c == 'F') &&
                pos_ + 1 < src_.size() && (src_[pos_ + 1] == '"' || src_[pos_ + 1] == '\'')) {
                ++pos_;
                ++column_;
                LexString();
                continue;
            }

            // Identifiers / keywords
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                int start = pos_;
                int cstart = column_;
                while (pos_ < src_.size() &&
                       (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_')) {
                    ++pos_;
                    ++column_;
                }
                tokens_.push_back({TokKind::kName, src_.substr(start, pos_ - start), line_, cstart});
                continue;
            }

            // Numeric
            if (std::isdigit(static_cast<unsigned char>(c))) {
                int start = pos_;
                int cstart = column_;
                while (pos_ < src_.size() &&
                       (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.' ||
                        src_[pos_] == '_')) {
                    ++pos_;
                    ++column_;
                }
                tokens_.push_back({TokKind::kNumber, src_.substr(start, pos_ - start), line_, cstart});
                continue;
            }

            // Multi-char operators
            if (pos_ + 1 < src_.size()) {
                std::string two = src_.substr(pos_, 2);
                if (two == "->" || two == "**" || two == "//" || two == "==" || two == "!=" ||
                    two == "<=" || two == ">=" || two == ":=" || two == "<<" || two == ">>") {
                    tokens_.push_back({TokKind::kOp, two, line_, column_});
                    pos_ += 2;
                    column_ += 2;
                    continue;
                }
            }

            // Single-char operators / punctuation
            if (c == '(' || c == '[' || c == '{') ++paren_depth;
            if (c == ')' || c == ']' || c == '}') --paren_depth;
            tokens_.push_back({TokKind::kOp, std::string(1, c), line_, column_});
            ++pos_;
            ++column_;
        }

        // Final NEWLINE & DEDENTs
        if (!tokens_.empty() && tokens_.back().kind != TokKind::kNewline) {
            Push(TokKind::kNewline, "\n");
        }
        while (indent_stack.size() > 1) {
            indent_stack.pop_back();
            Push(TokKind::kDedent, "");
        }
        Push(TokKind::kEnd, "");
        return std::move(tokens_);
    }

  private:
    void Push(TokKind k, std::string t) {
        tokens_.push_back({k, std::move(t), line_, column_});
    }

    void LexString() {
        char quote = src_[pos_];
        bool triple = (pos_ + 2 < src_.size() && src_[pos_ + 1] == quote && src_[pos_ + 2] == quote);
        int  cstart = column_;
        int  start  = pos_;

        if (triple) {
            pos_ += 3;
            column_ += 3;
            while (pos_ + 2 < src_.size()) {
                if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                    pos_ += 2;
                    column_ += 2;
                    continue;
                }
                if (src_[pos_] == quote && src_[pos_ + 1] == quote && src_[pos_ + 2] == quote) {
                    pos_ += 3;
                    column_ += 3;
                    break;
                }
                if (src_[pos_] == '\n') {
                    ++line_;
                    column_ = 1;
                } else {
                    ++column_;
                }
                ++pos_;
            }
        } else {
            ++pos_;
            ++column_;
            while (pos_ < src_.size() && src_[pos_] != quote && src_[pos_] != '\n') {
                if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                    pos_ += 2;
                    column_ += 2;
                    continue;
                }
                ++pos_;
                ++column_;
            }
            if (pos_ < src_.size() && src_[pos_] == quote) {
                ++pos_;
                ++column_;
            }
        }
        tokens_.push_back({TokKind::kString, src_.substr(start, pos_ - start), line_, cstart});
    }

    std::string         src_;
    std::vector<Token>  tokens_;
    std::size_t         pos_{0};
    int                 line_{1};
    int                 column_{1};
};

// ============================================================================
// Type expression parser → core::Type
// ============================================================================
class StubParser {
  public:
    StubParser(std::vector<Token> toks,
               const std::string &module_name,
               PyiLoader &owner,
               frontends::Diagnostics &diags)
        : toks_(std::move(toks)),
          mod_name_(module_name),
          owner_(owner),
          diags_(diags) {}

    PyiModule Parse() {
        PyiModule m;
        m.name = mod_name_;
        ParseSuite(m, /*at_module_scope=*/true);
        return m;
    }

  private:
    const Token &Peek(std::size_t off = 0) const {
        std::size_t i = std::min(pos_ + off, toks_.size() - 1);
        return toks_[i];
    }

    bool Match(TokKind k) {
        if (Peek().kind == k) { ++pos_; return true; }
        return false;
    }

    bool MatchOp(const std::string &op) {
        if (Peek().kind == TokKind::kOp && Peek().text == op) { ++pos_; return true; }
        return false;
    }

    bool MatchName(const std::string &name) {
        if (Peek().kind == TokKind::kName && Peek().text == name) { ++pos_; return true; }
        return false;
    }

    bool IsName(const std::string &name) const {
        return Peek().kind == TokKind::kName && Peek().text == name;
    }

    bool IsOp(const std::string &op) const {
        return Peek().kind == TokKind::kOp && Peek().text == op;
    }

    void Skip(TokKind k) {
        while (Peek().kind == k) ++pos_;
    }

    // Parse a suite (block).  At module scope we already start at column 0; for
    // class bodies the caller handles INDENT/DEDENT.
    void ParseSuite(PyiModule &m, bool at_module_scope) {
        (void)at_module_scope;
        while (Peek().kind != TokKind::kEnd && Peek().kind != TokKind::kDedent) {
            // Skip stray newlines
            if (Peek().kind == TokKind::kNewline) { ++pos_; continue; }
            ParseStatement(m);
        }
    }

    void ParseStatement(PyiModule &m) {
        // Decorators
        while (IsOp("@")) {
            ++pos_;
            // Consume the decorator expression up to NEWLINE.
            while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
            Match(TokKind::kNewline);
        }

        // Compound statements
        if (IsName("def") || (IsName("async") && Peek(1).kind == TokKind::kName && Peek(1).text == "def")) {
            ParseDef(m);
            return;
        }
        if (IsName("class")) {
            ParseClass(m);
            return;
        }
        if (IsName("from")) {
            ParseFromImport(m);
            return;
        }
        if (IsName("import")) {
            ParseImport(m);
            return;
        }
        if (IsName("if") || IsName("elif")) {
            ParseConditional(m);
            return;
        }
        if (IsName("else")) {
            // Strip 'else :' header then parse its body
            ++pos_;
            MatchOp(":");
            Skip(TokKind::kNewline);
            if (Match(TokKind::kIndent)) {
                ParseSuite(m, false);
                Match(TokKind::kDedent);
            } else {
                // single-line else body: skip to NEWLINE
                while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
                Match(TokKind::kNewline);
            }
            return;
        }
        if (IsName("try") || IsName("with") || IsName("while") || IsName("for") || IsName("match")) {
            // Skip header and body — these don't normally appear in stubs but
            // we tolerate them if they do.
            while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
            Match(TokKind::kNewline);
            if (Match(TokKind::kIndent)) {
                ParseSuite(m, false);
                Match(TokKind::kDedent);
            }
            return;
        }

        // Annotated assignment / type alias / value assignment
        if (Peek().kind == TokKind::kName) {
            std::size_t save = pos_;
            std::string name = Peek().text;
            ++pos_;

            // name : Type [= value]
            if (IsOp(":")) {
                ++pos_;
                core::Type t = ParseTypeExpr();
                if (IsOp("=")) {
                    ++pos_;
                    SkipExpression();
                }
                ConsumeStatementEnd();
                PyiSymbol sym;
                sym.name = name;
                sym.type = std::move(t);
                m.exports[name] = std::move(sym);
                return;
            }
            // name = Type-or-value (treat as type alias / Any)
            if (IsOp("=")) {
                ++pos_;
                core::Type t = ParseTypeExpr();
                ConsumeStatementEnd();
                PyiSymbol sym;
                sym.name = name;
                sym.type = std::move(t);
                m.exports[name] = std::move(sym);
                return;
            }
            // name.something… or arbitrary expression: skip
            pos_ = save;
        }

        // Default: skip the rest of this logical line.
        while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
        Match(TokKind::kNewline);
    }

    // Consume balanced expression up to NEWLINE / "," / ":" at depth 0.
    void SkipExpression() {
        int depth = 0;
        while (Peek().kind != TokKind::kEnd) {
            const Token &t = Peek();
            if (t.kind == TokKind::kNewline && depth == 0) break;
            if (t.kind == TokKind::kOp) {
                if (t.text == "(" || t.text == "[" || t.text == "{") ++depth;
                else if (t.text == ")" || t.text == "]" || t.text == "}") {
                    if (depth == 0) break;
                    --depth;
                }
            }
            ++pos_;
        }
    }

    void ConsumeStatementEnd() {
        // Skip semicolons + remainder of line.
        while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
        Match(TokKind::kNewline);
    }

    void ParseDef(PyiModule &m) {
        // Optional 'async'
        bool is_async = false;
        if (IsName("async")) { is_async = true; ++pos_; }
        if (!MatchName("def")) {
            ConsumeStatementEnd();
            return;
        }
        if (Peek().kind != TokKind::kName) { ConsumeStatementEnd(); return; }
        std::string name = Peek().text; ++pos_;

        // Optional generic params: def foo[T](…)
        if (IsOp("[")) {
            int depth = 0;
            while (Peek().kind != TokKind::kEnd) {
                if (IsOp("[")) ++depth;
                else if (IsOp("]")) { --depth; if (depth == 0) { ++pos_; break; } }
                ++pos_;
            }
        }
        if (!MatchOp("(")) { ConsumeStatementEnd(); return; }

        std::vector<core::Type> params;
        ParseParamList(params);
        MatchOp(")");

        core::Type ret = core::Type::Any();
        if (MatchOp("->")) {
            ret = ParseTypeExpr();
        }
        MatchOp(":");
        // Skip body
        SkipBody();

        // Build core::Type using existing convention: kFunction with type_args[0]=ret, then params.
        core::Type fn{core::TypeKind::kFunction, name};
        fn.language = "python";
        fn.type_args.push_back(is_async ? MakeCoroutine(ret) : ret);
        for (auto &p : params) fn.type_args.push_back(std::move(p));

        // First-occurrence wins (handles @overload).
        if (m.exports.find(name) == m.exports.end()) {
            PyiSymbol sym;
            sym.name = name;
            sym.is_function = true;
            sym.type = fn;
            sym.return_type = is_async ? MakeCoroutine(ret) : ret;
            sym.param_types = std::move(params);
            m.exports[name] = std::move(sym);
        }
    }

    void ParseParamList(std::vector<core::Type> &out) {
        while (Peek().kind != TokKind::kEnd && !IsOp(")")) {
            // Skip leading '*' / '**' / '/' markers
            if (IsOp("*") || IsOp("**") || IsOp("/")) { ++pos_; }
            // param name (may also be just '*' alone)
            if (Peek().kind == TokKind::kName) {
                ++pos_;
                core::Type pt = core::Type::Any();
                if (MatchOp(":")) pt = ParseTypeExpr();
                if (MatchOp("=")) SkipDefault();
                out.push_back(std::move(pt));
            } else {
                // Unknown token in param list — bail out.
                if (!IsOp(",") && !IsOp(")")) ++pos_;
            }
            if (!MatchOp(",")) break;
        }
    }

    void SkipDefault() {
        int depth = 0;
        while (Peek().kind != TokKind::kEnd) {
            if (IsOp("(") || IsOp("[") || IsOp("{")) { ++depth; ++pos_; continue; }
            if (IsOp(")") || IsOp("]") || IsOp("}")) {
                if (depth == 0) return;
                --depth; ++pos_; continue;
            }
            if (depth == 0 && IsOp(",")) return;
            if (Peek().kind == TokKind::kNewline && depth == 0) return;
            ++pos_;
        }
    }

    void SkipBody() {
        // Either a single-line ': ...' / ': pass' or an indented block.
        // After ':', possibly NEWLINE INDENT … DEDENT
        if (Match(TokKind::kNewline)) {
            if (Match(TokKind::kIndent)) {
                int depth = 1;
                while (Peek().kind != TokKind::kEnd && depth > 0) {
                    if (Match(TokKind::kIndent)) { ++depth; continue; }
                    if (Match(TokKind::kDedent)) { --depth; continue; }
                    ++pos_;
                }
                return;
            }
            return;
        }
        // Single-line body: consume until NEWLINE
        while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
        Match(TokKind::kNewline);
    }

    void ParseClass(PyiModule &m) {
        ++pos_; // 'class'
        if (Peek().kind != TokKind::kName) { ConsumeStatementEnd(); return; }
        std::string name = Peek().text; ++pos_;
        // Optional generic params
        if (IsOp("[")) {
            int depth = 0;
            while (Peek().kind != TokKind::kEnd) {
                if (IsOp("[")) ++depth;
                else if (IsOp("]")) { --depth; if (depth == 0) { ++pos_; break; } }
                ++pos_;
            }
        }
        // Optional base list
        if (MatchOp("(")) {
            int depth = 1;
            while (Peek().kind != TokKind::kEnd && depth > 0) {
                if (IsOp("(")) { ++depth; ++pos_; continue; }
                if (IsOp(")")) { --depth; ++pos_; continue; }
                ++pos_;
            }
        }
        MatchOp(":");

        // Build class symbol now (so name is visible).
        core::Type ct{core::TypeKind::kClass, name};
        ct.language = "python";
        PyiSymbol &cls_sym = m.exports[name];
        cls_sym.name      = name;
        cls_sym.is_class  = true;
        cls_sym.type      = ct;

        // Body: either '...' / 'pass' on same line, or NEWLINE INDENT … DEDENT.
        if (Match(TokKind::kNewline)) {
            if (Match(TokKind::kIndent)) {
                // Skip the class body: we don't lift methods to module scope,
                // but we still need to balance INDENT/DEDENT.
                int depth = 1;
                while (Peek().kind != TokKind::kEnd && depth > 0) {
                    if (Match(TokKind::kIndent)) { ++depth; continue; }
                    if (Match(TokKind::kDedent)) { --depth; continue; }
                    ++pos_;
                }
            }
            return;
        }
        // Single-line body
        while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
        Match(TokKind::kNewline);
    }

    void ParseFromImport(PyiModule &m) {
        ++pos_; // 'from'
        // Module path: '.' '.' name '.' name …
        std::string mod;
        while (IsOp(".")) { mod += '.'; ++pos_; }
        while (Peek().kind == TokKind::kName) {
            mod += Peek().text;
            ++pos_;
            if (IsOp(".")) { mod += '.'; ++pos_; } else break;
        }
        if (!MatchName("import")) { ConsumeStatementEnd(); return; }

        bool   is_star  = false;
        bool   wrapped  = MatchOp("(");
        std::vector<std::pair<std::string, std::string>> names;
        if (IsOp("*")) { is_star = true; ++pos_; }
        else {
            while (Peek().kind == TokKind::kName) {
                std::string n = Peek().text; ++pos_;
                std::string a;
                if (MatchName("as")) {
                    if (Peek().kind == TokKind::kName) { a = Peek().text; ++pos_; }
                }
                names.emplace_back(n, a);
                if (!MatchOp(",")) break;
                Skip(TokKind::kNewline);  // allow line continuation inside parens
            }
        }
        if (wrapped) MatchOp(")");
        ConsumeStatementEnd();

        // Best-effort transitive resolution: only for absolute, non-relative.
        if (mod.empty() || mod[0] == '.') return;
        const PyiModule *src = owner_.Resolve(mod);
        if (!src) return;
        if (is_star) {
            for (const auto &kv : src->exports) {
                if (m.exports.find(kv.first) == m.exports.end()) {
                    m.exports[kv.first] = kv.second;
                }
            }
            return;
        }
        for (const auto &[orig, alias] : names) {
            const std::string &bind = alias.empty() ? orig : alias;
            auto it = src->exports.find(orig);
            if (it != src->exports.end()) {
                PyiSymbol sym = it->second;
                sym.name = bind;
                m.exports[bind] = std::move(sym);
            } else {
                // Maybe the imported name refers to a submodule.
                const PyiModule *sub = owner_.Resolve(mod + "." + orig);
                if (sub) {
                    PyiSymbol sym;
                    sym.name = bind;
                    sym.is_module = true;
                    sym.type = core::Type::Module(sub->name, "python");
                    m.exports[bind] = std::move(sym);
                }
            }
        }
    }

    void ParseImport(PyiModule &m) {
        ++pos_;  // 'import'
        while (true) {
            std::string mod;
            while (Peek().kind == TokKind::kName) {
                mod += Peek().text; ++pos_;
                if (IsOp(".")) { mod += '.'; ++pos_; } else break;
            }
            std::string alias;
            if (MatchName("as") && Peek().kind == TokKind::kName) {
                alias = Peek().text;
                ++pos_;
            }
            if (!mod.empty()) {
                std::string bind = alias.empty() ? mod : alias;
                PyiSymbol sym;
                sym.name = bind;
                sym.is_module = true;
                sym.type = core::Type::Module(mod, "python");
                m.exports[bind] = std::move(sym);
            }
            if (!MatchOp(",")) break;
        }
        ConsumeStatementEnd();
    }

    void ParseConditional(PyiModule &m) {
        // Skip header to ':'
        while (Peek().kind != TokKind::kEnd) {
            if (IsOp(":")) { ++pos_; break; }
            ++pos_;
        }
        Skip(TokKind::kNewline);
        if (Match(TokKind::kIndent)) {
            ParseSuite(m, false);
            Match(TokKind::kDedent);
        } else {
            while (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
            Match(TokKind::kNewline);
        }
        // 'elif' / 'else' will be parsed by the outer ParseStatement loop.
    }

    // --------------------------------------------------------------------
    // Type expression parser
    // --------------------------------------------------------------------
    core::Type ParseTypeExpr() {
        core::Type lhs = ParseTypeUnary();
        // PEP 604: A | B | C
        while (IsOp("|")) {
            ++pos_;
            ParseTypeUnary();   // discard rhs — represent union as Any
            lhs = core::Type::Any();
        }
        return lhs;
    }

    core::Type ParseTypeUnary() {
        // Atom
        core::Type t = ParseTypeAtom();
        // Subscript chain
        while (IsOp("[")) {
            ++pos_;
            std::vector<core::Type> args;
            ParseTypeArgs(args);
            MatchOp("]");
            t = ApplySubscript(t, args);
        }
        return t;
    }

    void ParseTypeArgs(std::vector<core::Type> &out) {
        while (Peek().kind != TokKind::kEnd && !IsOp("]")) {
            // Nested bracket list: e.g. Callable's first arg is "[A, B]"
            if (IsOp("[")) {
                ++pos_;
                std::vector<core::Type> nested;
                ParseTypeArgs(nested);
                MatchOp("]");
                core::Type tup{core::TypeKind::kTuple, "params"};
                tup.type_args = std::move(nested);
                out.push_back(std::move(tup));
            } else if (IsOp("...")) {
                ++pos_;
                out.push_back(core::Type::Any());
            } else if (IsOp(".")) {
                // Sometimes `...` is tokenized as three '.' ops.
                int dots = 0;
                while (IsOp(".")) { ++pos_; ++dots; }
                out.push_back(core::Type::Any());
                (void)dots;
            } else {
                out.push_back(ParseTypeExpr());
            }
            if (!MatchOp(",")) break;
            Skip(TokKind::kNewline);
        }
    }

    core::Type ParseTypeAtom() {
        // None / True / False / numbers / strings → Any/Void
        if (Peek().kind == TokKind::kNumber) { ++pos_; return core::Type::Any(); }
        if (Peek().kind == TokKind::kString) { ++pos_; return core::Type::String(); }
        // Parenthesized type
        if (MatchOp("(")) {
            core::Type t = ParseTypeExpr();
            MatchOp(")");
            return t;
        }
        // Three dots or ellipsis
        if (IsOp("...")) { ++pos_; return core::Type::Any(); }
        if (IsOp(".")) {
            while (IsOp(".")) ++pos_;
            return core::Type::Any();
        }

        if (Peek().kind != TokKind::kName) {
            // Skip something we don't understand and yield Any.
            if (Peek().kind != TokKind::kNewline && Peek().kind != TokKind::kEnd) ++pos_;
            return core::Type::Any();
        }

        // Dotted name
        std::string name = Peek().text; ++pos_;
        while (IsOp(".") && Peek(1).kind == TokKind::kName) {
            ++pos_; // '.'
            name += '.';
            name += Peek().text;
            ++pos_;
        }

        return MapNamedType(name);
    }

    static core::Type MakeCoroutine(core::Type ret) {
        core::Type t{core::TypeKind::kGenericInstance, "coroutine"};
        t.language = "python";
        t.type_args.push_back(std::move(ret));
        return t;
    }

    static core::Type MapNamedType(const std::string &qname) {
        // Strip leading "typing." / "builtins." / "collections.abc." prefixes.
        std::string n = qname;
        for (const char *prefix : {"typing.", "typing_extensions.", "builtins.", "collections.abc."}) {
            std::size_t plen = std::char_traits<char>::length(prefix);
            if (n.size() > plen && n.compare(0, plen, prefix) == 0) {
                n = n.substr(plen);
                break;
            }
        }

        if (n == "int" || n == "long" || n == "Int") return core::Type::Int();
        if (n == "float" || n == "double") return core::Type::Float();
        if (n == "str" || n == "unicode" || n == "Str") return core::Type::String();
        if (n == "bool") return core::Type::Bool();
        if (n == "None" || n == "NoneType") return core::Type::Void();
        if (n == "bytes" || n == "bytearray" || n == "memoryview" ||
            n == "complex" || n == "object" || n == "Any" ||
            n == "AnyStr" || n == "Never" || n == "NoReturn") {
            return core::Type::Any();
        }
        if (n == "list" || n == "List") return MakeGeneric("list", {core::Type::Any()});
        if (n == "dict" || n == "Dict") return MakeGeneric("dict", {core::Type::Any(), core::Type::Any()});
        if (n == "set" || n == "Set" || n == "frozenset" || n == "FrozenSet") return MakeGeneric("set", {core::Type::Any()});
        if (n == "tuple" || n == "Tuple") return MakeTuple({});
        if (n == "Callable") return MakeFunction(core::Type::Any(), {});

        // Fall through: treat as a class type qualified by the original name.
        core::Type t{core::TypeKind::kClass, qname};
        t.language = "python";
        return t;
    }

    static core::Type ApplySubscript(core::Type base, std::vector<core::Type> args) {
        const std::string &n = base.name;
        if (n == "Optional" || n == "optional") {
            core::Type inner = args.empty() ? core::Type::Any() : std::move(args[0]);
            return core::Type::Optional(std::move(inner));
        }
        if (n == "list" || n == "List") return MakeGeneric("list", args.empty() ? std::vector<core::Type>{core::Type::Any()} : std::move(args));
        if (n == "dict" || n == "Dict") {
            std::vector<core::Type> a = std::move(args);
            while (a.size() < 2) a.push_back(core::Type::Any());
            return MakeGeneric("dict", std::move(a));
        }
        if (n == "set" || n == "Set" || n == "frozenset" || n == "FrozenSet") {
            return MakeGeneric("set", args.empty() ? std::vector<core::Type>{core::Type::Any()} : std::move(args));
        }
        if (n == "tuple" || n == "Tuple") return MakeTuple(std::move(args));
        if (n == "Callable") {
            // Callable[[A,B], R]: args[0] is a tuple of params, args[1] is return.
            core::Type ret = args.size() >= 2 ? args[1] : core::Type::Any();
            std::vector<core::Type> params;
            if (!args.empty() && args[0].kind == core::TypeKind::kTuple) {
                params = std::move(args[0].type_args);
            }
            return MakeFunction(std::move(ret), std::move(params));
        }
        if (n == "Type" || n == "type" || n == "ClassVar" || n == "Final" ||
            n == "Annotated") {
            return args.empty() ? core::Type::Any() : std::move(args[0]);
        }
        if (n == "Literal") {
            // Literal["x", 1] → fall back to Any (or string / int if uniform).
            return core::Type::Any();
        }
        if (n == "Union") {
            return core::Type::Any();
        }

        // Generic user-defined class instance.
        core::Type t{core::TypeKind::kGenericInstance, base.name};
        t.language = base.language.empty() ? "python" : base.language;
        t.type_args = std::move(args);
        return t;
    }

    static core::Type MakeGeneric(const std::string &name, std::vector<core::Type> args) {
        core::Type t{core::TypeKind::kGenericInstance, name};
        t.language = "python";
        t.type_args = std::move(args);
        return t;
    }

    static core::Type MakeTuple(std::vector<core::Type> elems) {
        core::Type t{core::TypeKind::kTuple, "tuple"};
        t.type_args = std::move(elems);
        return t;
    }

    static core::Type MakeFunction(core::Type ret, std::vector<core::Type> params) {
        core::Type t{core::TypeKind::kFunction, "callable"};
        t.language = "python";
        t.type_args.reserve(params.size() + 1);
        t.type_args.push_back(std::move(ret));
        for (auto &p : params) t.type_args.push_back(std::move(p));
        return t;
    }

    std::vector<Token>      toks_;
    std::size_t             pos_{0};
    std::string             mod_name_;
    PyiLoader              &owner_;
    frontends::Diagnostics &diags_;
};

// ============================================================================
// Helpers
// ============================================================================
std::string ReadFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

}  // namespace

// ============================================================================
// PyiLoader
// ============================================================================
PyiLoader::PyiLoader(std::vector<std::string> stub_paths,
                     frontends::Diagnostics &diags)
    : search_paths_(std::move(stub_paths)),
      diags_(diags) {
    // Best-effort: also probe the typeshed location bundled by user-provided
    // PYTHON_STUBS environment variable, if set.
    if (const char *env = std::getenv("PYTHON_STUBS")) {
        if (env && *env) {
            // Split by ';' (Windows) or ':' (Unix).  We accept both for
            // simplicity since '.' is never a separator on either.
            std::string s = env;
            std::string cur;
            for (char c : s) {
                if (c == ';' || c == ':') {
                    if (!cur.empty()) search_paths_.push_back(cur);
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) search_paths_.push_back(cur);
        }
    }
}

const PyiModule *PyiLoader::Resolve(const std::string &module_name) {
    if (module_name.empty()) return nullptr;
    if (auto it = cache_.find(module_name); it != cache_.end()) {
        return it->second.get();
    }
    if (missing_.count(module_name)) return nullptr;

    auto path = LocateStubFile(module_name);
    if (!path) {
        missing_[module_name] = true;
        return nullptr;
    }

    // Pre-insert sentinel to break cycles.
    cache_.emplace(module_name, std::unique_ptr<PyiModule>{});
    auto m = ParseStubFile(*path, module_name);
    if (!m) {
        cache_.erase(module_name);
        missing_[module_name] = true;
        return nullptr;
    }
    PyiModule *raw = m.get();
    cache_[module_name] = std::move(m);
    return raw;
}

std::optional<std::string>
PyiLoader::LocateStubFile(const std::string &module_name) const {
    if (search_paths_.empty()) return std::nullopt;

    // Translate dotted name into relative path.
    std::string rel;
    rel.reserve(module_name.size());
    for (char c : module_name) rel.push_back(c == '.' ? '/' : c);

    std::error_code ec;
    for (const auto &dir : search_paths_) {
        // Direct: <dir>/<rel>.pyi
        fs::path p1 = fs::path(dir) / (rel + ".pyi");
        if (fs::exists(p1, ec)) return p1.string();
        // Package: <dir>/<rel>/__init__.pyi
        fs::path p2 = fs::path(dir) / rel / "__init__.pyi";
        if (fs::exists(p2, ec)) return p2.string();
        // Stubs convention: <dir>/<top>-stubs/<sub>.pyi
        auto first_dot = module_name.find('.');
        std::string top = (first_dot == std::string::npos) ? module_name : module_name.substr(0, first_dot);
        std::string sub = (first_dot == std::string::npos) ? std::string{} : module_name.substr(first_dot + 1);
        fs::path stubs_pkg = fs::path(dir) / (top + "-stubs");
        if (fs::exists(stubs_pkg, ec)) {
            if (sub.empty()) {
                fs::path p3 = stubs_pkg / "__init__.pyi";
                if (fs::exists(p3, ec)) return p3.string();
            } else {
                std::string srel = sub;
                std::replace(srel.begin(), srel.end(), '.', '/');
                fs::path p3 = stubs_pkg / (srel + ".pyi");
                if (fs::exists(p3, ec)) return p3.string();
                fs::path p4 = stubs_pkg / srel / "__init__.pyi";
                if (fs::exists(p4, ec)) return p4.string();
            }
        }
    }
    return std::nullopt;
}

std::unique_ptr<PyiModule>
PyiLoader::ParseStubFile(const std::string &path, const std::string &module_name) {
    std::string src = ReadFile(path);
    if (src.empty()) return nullptr;
    Lexer lex(src);
    auto tokens = lex.Tokenize();

    StubParser parser(std::move(tokens), module_name, *this, diags_);
    auto mod = std::make_unique<PyiModule>(parser.Parse());
    mod->source_path = path;
    return mod;
}

}  // namespace polyglot::python
