/**
 * @file     lexer.cpp
 * @brief    Python language frontend implementation
 *
 * @ingroup  Frontend / Python
 * @author   Manning Cyrus
 * @date     2026-04-10
 */

#include "frontends/python/include/python_lexer.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace polyglot::python {

void PythonLexer::SkipWhitespace() {
    while (std::isspace(static_cast<unsigned char>(Peek()))) {
        if (Peek() == '\n')
            break;
        Get();
    }
}

void PythonLexer::SkipComment() {
    while (Peek() != '\0' && Peek() != '\n') {
        Get();
    }
}

frontends::Token PythonLexer::LexIdentifier() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    while (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_') {
        lexeme.push_back(Get());
    }
    static const std::unordered_set<std::string> keywords = {
        "False", "None",     "True",  "and",    "as",    "assert", "async",  "await",
        "break", "case",     "class", "continue", "def", "del",    "elif",   "else",
        "except", "finally", "for",   "from",   "global", "if",    "import", "in",
        "is",    "lambda",   "match", "nonlocal", "not", "or",     "pass",   "raise",
        "return", "try",     "while", "with",   "yield"};
    frontends::TokenKind kind =
        keywords.count(lexeme) ? frontends::TokenKind::kKeyword : frontends::TokenKind::kIdentifier;
    return frontends::Token{kind, lexeme, loc};
}

frontends::Token PythonLexer::LexNumber() {
    core::SourceLoc loc = CurrentLoc();
    std::string lexeme;
    auto consume_digits = [&](auto predicate) {
        while (predicate(static_cast<unsigned char>(Peek())) || Peek() == '_') {
            lexeme.push_back(Get());
        }
    };

    if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X')) {
        lexeme.push_back(Get());
        lexeme.push_back(Get());
        consume_digits([](unsigned char c) { return std::isxdigit(c); });
    } else if (Peek() == '0' && (PeekNext() == 'b' || PeekNext() == 'B')) {
        lexeme.push_back(Get());
        lexeme.push_back(Get());
        consume_digits([](unsigned char c) { return c == '0' || c == '1'; });
    } else if (Peek() == '0' && (PeekNext() == 'o' || PeekNext() == 'O')) {
        lexeme.push_back(Get());
        lexeme.push_back(Get());
        consume_digits([](unsigned char c) { return c >= '0' && c <= '7'; });
    } else {
        bool seen_dot = false;
        while (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '.' || Peek() == '_') {
            if (Peek() == '.') {
                if (seen_dot)
                    break;
                seen_dot = true;
            }
            lexeme.push_back(Get());
        }
        if (Peek() == 'e' || Peek() == 'E') {
            lexeme.push_back(Get());
            if (Peek() == '+' || Peek() == '-') {
                lexeme.push_back(Get());
            }
            consume_digits([](unsigned char c) { return std::isdigit(c); });
        }
    }

    if (Peek() == 'j' || Peek() == 'J') {
        lexeme.push_back(Get());
    }
    return frontends::Token{frontends::TokenKind::kNumber, lexeme, loc};
}

frontends::Token PythonLexer::LexString() { return LexStringInternal(true); }

void PythonLexer::Report(const core::SourceLoc &loc, const std::string &message) {
    if (diagnostics_) {
        diagnostics_->Report(loc, message);
    }
}

bool PythonLexer::ParseFormatExpression(core::SourceLoc brace_loc) {
    int brace_depth = 1;
    pending_.push_back(frontends::Token{frontends::TokenKind::kSymbol, "{", brace_loc});

    static const std::vector<std::string> fmt_ops = {
        "**=", "//=", "<<=", ">>=", "==", "!=", "<=", ">=", "**", "//", "<<", ">>", "+=",
        "-=",  "*=",  "/=",  "%=",  "&=", "|=", "^=", "@=", ":=", "->", "...", "@",
        ".",   "=",   "+",   "-",  "*",  "/",  "%",  "&",  "|",  "^",  "~",
        "<",   ">",   "(",   ")",   "[",  "]",  ",",  ";"};

    // Track whether we have entered the format-spec portion of the
    // f-string expression (the part after a bare ':' at brace_depth 1).
    // In format-spec mode we stop doing expression parsing because the
    // spec may contain characters like 'f', 'b', 'e', etc. that look
    // like string prefixes or identifiers but are really format codes.
    bool in_format_spec = false;

    while (!Eof() && brace_depth > 0) {
        char c = Peek();
        core::SourceLoc loc = CurrentLoc();

        if (c == '{') {
            Get();
            brace_depth++;
            in_format_spec = false; // nested expression, reset
            pending_.push_back(frontends::Token{frontends::TokenKind::kSymbol, "{", loc});
            continue;
        }
        if (c == '}') {
            Get();
            brace_depth--;
            pending_.push_back(frontends::Token{frontends::TokenKind::kSymbol, "}", loc});
            if (brace_depth == 0) {
                return true;
            }
            in_format_spec = false;
            continue;
        }

        // When inside the format-spec (e.g. {value:.2f}), consume
        // characters raw until the closing '}'. Do NOT try to lex
        // identifiers or strings — 'f', 'b', 'e', etc. are format
        // codes, not language tokens.
        if (in_format_spec) {
            // Just consume the character — it is part of the format spec
            // string that was already started above.
            std::string spec_text;
            while (!Eof() && Peek() != '}' && Peek() != '{') {
                spec_text.push_back(Get());
            }
            if (!spec_text.empty()) {
                pending_.push_back(frontends::Token{
                    frontends::TokenKind::kString, spec_text, loc});
            }
            continue;
        }

        // Detect the format-spec colon at the outermost brace level.
        // ':' inside nested braces or '::' / ':=' are NOT format-spec.
        if (c == ':' && brace_depth == 1) {
            // ':=' is the walrus operator — not a format spec separator
            if (position_ + 1 < source_.size() && source_[position_ + 1] == '=') {
                Get(); Get();
                pending_.push_back(frontends::Token{
                    frontends::TokenKind::kSymbol, ":=", loc});
                continue;
            }
            Get();
            pending_.push_back(frontends::Token{
                frontends::TokenKind::kSymbol, ":", loc});
            in_format_spec = true;
            continue;
        }

        // Python 3.8+ self-documenting expression: f'{name=}' or f'{name=:fmt}'.
        // The '=' right before '}' or ':' at brace_depth 1 is NOT the
        // assignment/comparison operator — it is a debug format marker.
        // Emit it as a string token and switch to format-spec mode so
        // any trailing format spec (e.g. f'{value=:.2f}') is handled.
        if (c == '=' && brace_depth == 1) {
            // Look ahead past optional whitespace
            size_t ahead = position_ + 1;
            while (ahead < source_.size() && source_[ahead] == ' ') {
                ahead++;
            }
            if (ahead < source_.size() &&
                (source_[ahead] == '}' || source_[ahead] == ':')) {
                Get();
                pending_.push_back(frontends::Token{
                    frontends::TokenKind::kString, "=", loc});
                in_format_spec = true;
                continue;
            }
        }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            // Check for string prefix (r/f/b) inside f-string expressions.
            // Only treat as string start if followed by a quote character.
            if (c == 'r' || c == 'R' || c == 'b' || c == 'B' || c == 'f' || c == 'F') {
                char next_ch = (position_ + 1 < source_.size()) ? source_[position_ + 1] : '\0';
                if (next_ch == '"' || next_ch == '\'') {
                    pending_.push_back(LexStringInternal(false));
                    continue;
                }
                // Two-char prefix (rb, br, rf, fr)
                if (next_ch == 'r' || next_ch == 'R' || next_ch == 'b' || next_ch == 'B' ||
                    next_ch == 'f' || next_ch == 'F') {
                    char after = (position_ + 2 < source_.size()) ? source_[position_ + 2] : '\0';
                    if (after == '"' || after == '\'') {
                        pending_.push_back(LexStringInternal(false));
                        continue;
                    }
                }
            }
            pending_.push_back(LexIdentifier());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            pending_.push_back(LexNumber());
            continue;
        }
        if (c == '"' || c == '\'') {
            pending_.push_back(LexStringInternal(false));
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            char consumed = Get();
            if (consumed == '\n') {
                pending_.push_back(frontends::Token{frontends::TokenKind::kNewline, "", loc});
                at_line_start_ = true;
            }
            continue;
        }

        bool matched = false;
        for (const auto &op : fmt_ops) {
            if (source_.compare(position_, op.size(), op) == 0) {
                for (size_t i = 0; i < op.size(); ++i) {
                    Get();
                }
                pending_.push_back(frontends::Token{frontends::TokenKind::kSymbol, op, loc});
                matched = true;
                break;
            }
        }
        if (!matched) {
            Get();
            pending_.push_back(
                frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc});
        }
    }

    pending_.push_back(frontends::Token{frontends::TokenKind::kUnknown,
                                        "unterminated f-string expression", brace_loc});
    Report(brace_loc, "unterminated f-string expression");
    return false;
}

frontends::Token PythonLexer::LexStringInternal(bool allow_formatting) {
    core::SourceLoc loc = CurrentLoc();
    bool raw = false;
    bool formatted = false;
    bool bytes = false;

    for (;;) {
        char p = Peek();
        if (p == 'r' || p == 'R') {
            raw = true;
            Get();
            continue;
        }
        if (p == 'f' || p == 'F') {
            if (allow_formatting) {
                formatted = true;
            }
            Get();
            continue;
        }
        if (p == 'b' || p == 'B') {
            bytes = true;
            Get();
            continue;
        }
        break;
    }

    (void)bytes;
    char quote = Get();
    bool triple = false;
    if (Peek() == quote && PeekNext() == quote) {
        Get();
        Get();
        triple = true;
    }

    std::string segment;
    core::SourceLoc segment_loc = CurrentLoc();
    auto flush_segment = [&]() {
        if (!segment.empty()) {
            pending_.push_back(
                frontends::Token{frontends::TokenKind::kString, segment, segment_loc});
            segment.clear();
        }
        segment_loc = CurrentLoc();
    };

    while (!Eof()) {
        core::SourceLoc ch_loc = CurrentLoc();
        char c = Get();

        if (!raw && c == '\\') {
            if (Peek() == '\n') {
                // escaped newline inside string
                Get();
                at_line_start_ = true;
                continue;
            }
            segment.push_back(c);
            if (!Eof())
                segment.push_back(Get());
            continue;
        }

        if (formatted && c == '{') {
            if (Peek() == '{') {
                segment.push_back('{');
                Get();
                continue;
            }
            flush_segment();
            ParseFormatExpression(ch_loc);
            continue;
        }
        if (formatted && c == '}') {
            if (Peek() == '}') {
                segment.push_back('}');
                Get();
                continue;
            }
            flush_segment();
            pending_.push_back(frontends::Token{frontends::TokenKind::kUnknown,
                                                "unmatched } in f-string", ch_loc});
            Report(ch_loc, "unmatched } in f-string");
            continue;
        }

        if (triple) {
            if (c == quote && Peek() == quote && PeekNext() == quote) {
                Get();
                Get();
                flush_segment();
                break;
            }
            segment.push_back(c);
            continue;
        }

        if (c == quote) {
            flush_segment();
            break;
        }

        segment.push_back(c);
    }

    if (pending_.empty()) {
        return frontends::Token{frontends::TokenKind::kString, "", loc};
    }
    auto tok = pending_.front();
    pending_.erase(pending_.begin());
    return tok;
}

frontends::Token PythonLexer::LexOperator() {
    static const std::vector<std::string> operators = {
        "**=", "//=", "<<=", ">>=", "==", "!=", "<=", ">=", "**", "//", "<<", ">>", "+=",
        "-=",  "*=",  "/=",  "%=",  "&=", "|=", "^=", "@=", ":=", "->", "...", "@",
        ":",   ".",   "=",   "+",   "-",  "*",  "/",  "%",  "&",  "|",  "^",  "~",
        "<",   ">",   "(",   ")",   "[",  "]",  "{",  "}",  ",",  ";"};
    for (const auto &op : operators) {
        if (source_.compare(position_, op.size(), op) == 0) {
            core::SourceLoc loc = CurrentLoc();
            for (size_t i = 0; i < op.size(); ++i) {
                Get();
            }
            return frontends::Token{frontends::TokenKind::kSymbol, op, loc};
        }
    }
    core::SourceLoc loc = CurrentLoc();
    char c = Get();
    return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
}

void PythonLexer::HandleIndentation() {
    if (!at_line_start_ || paren_level_ > 0)
        return;

    int indent = 0;
    size_t saved_pos = position_;
    size_t saved_col = column_;
    while (Peek() == ' ' || Peek() == '\t') {
        indent += (Peek() == '\t') ? static_cast<int>(tab_width_) : 1;
        Get();
    }
    if (Peek() == '\n' || Peek() == '\0') {
        position_ = saved_pos;
        column_ = saved_col;
        return;
    }

    int current = indent_stack_.back();
    if (indent > current) {
        indent_stack_.push_back(indent);
        pending_.push_back(frontends::Token{frontends::TokenKind::kIndent, "", CurrentLoc()});
    } else if (indent < current) {
        while (indent < indent_stack_.back()) {
            indent_stack_.pop_back();
            pending_.push_back(frontends::Token{frontends::TokenKind::kDedent, "", CurrentLoc()});
        }
    }
    at_line_start_ = false;
}

frontends::Token PythonLexer::NextToken() {
    if (!pending_.empty()) {
        auto tok = pending_.front();
        pending_.erase(pending_.begin());
        return tok;
    }

    HandleIndentation();
    if (!pending_.empty()) {
        auto tok = pending_.front();
        pending_.erase(pending_.begin());
        return tok;
    }

    SkipWhitespace();
    core::SourceLoc loc = CurrentLoc();
    char c = Peek();
    if (c == '\0') {
        if (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            return frontends::Token{frontends::TokenKind::kDedent, "", loc};
        }
        return frontends::Token{frontends::TokenKind::kEndOfFile, "", loc};
    }

    if (c == '\\' && PeekNext() == '\n') {
        Get();
        Get();
        at_line_start_ = true;
        return NextToken();
    }

    if (c == '\n') {
        Get();
        at_line_start_ = true;
        if (paren_level_ == 0) {
            return frontends::Token{frontends::TokenKind::kNewline, "", loc};
        }
        return NextToken();
    }

    if (c == '#') {
        Get();
        std::string text;
        while (Peek() != '\0' && Peek() != '\n') {
            text.push_back(Get());
        }
        bool is_doc = !text.empty() && (text[0] == '#' || text[0] == ':');
        return frontends::Token{frontends::TokenKind::kComment, text, loc, is_doc};
    }

    if (c == '(' || c == '[' || c == '{') {
        paren_level_++;
        Get();
        return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
    }
    if (c == ')' || c == ']' || c == '}') {
        paren_level_ = std::max(0, paren_level_ - 1);
        Get();
        return frontends::Token{frontends::TokenKind::kSymbol, std::string(1, c), loc};
    }

    // Check for string prefix characters (r/f/b) before general identifier
    // lexing so that f-strings, raw strings, and byte strings are handled
    // correctly.  Valid Python string prefixes are at most two characters
    // (e.g. r, b, f, rb, br, rf, fr) and the prefix MUST be immediately
    // followed by a quote character (' or ").  We must not mistake ordinary
    // identifiers like "break" (b + r + ...) for string literals.
    if (c == 'r' || c == 'R' || c == 'f' || c == 'F' || c == 'b' || c == 'B') {
        char next = PeekNext();
        if (next == '"' || next == '\'') {
            // Single-char prefix directly followed by quote: r"...", f'...'
            return LexString();
        }
        // Two-char prefix: rb, br, rf, fr (and case variants)
        if (next == 'r' || next == 'R' || next == 'f' || next == 'F' ||
            next == 'b' || next == 'B') {
            // Peek two characters ahead to verify a quote follows
            if (position_ + 2 < source_.size()) {
                char after = source_[position_ + 2];
                if (after == '"' || after == '\'') {
                    return LexString();
                }
            }
        }
    }
    if (c == '"' || c == '\'') {
        return LexString();
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        return LexIdentifier();
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        return LexNumber();
    }
    (void)loc;
    return LexOperator();
}

} // namespace polyglot::python
