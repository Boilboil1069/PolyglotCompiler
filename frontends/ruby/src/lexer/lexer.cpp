/**
 * @file     lexer.cpp
 * @brief    Ruby tokenizer (2.7+ / 3.x)
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 */
#include "frontends/ruby/include/ruby_lexer.h"

#include <cctype>
#include <unordered_set>

namespace polyglot::ruby {

namespace {

const std::unordered_set<std::string> &Keywords() {
    static const std::unordered_set<std::string> kw = {
        "BEGIN","END","alias","and","begin","break","case","class","def","defined?",
        "do","else","elsif","end","ensure","false","for","if","in","module","next",
        "nil","not","or","redo","rescue","retry","return","self","super","then",
        "true","undef","unless","until","when","while","yield","__FILE__","__LINE__","__ENCODING__"
    };
    return kw;
}

bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool IsIdentPart(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

}  // namespace

bool RbLexer::AtLineStart() const {
    if (position_ == 0) return true;
    size_t p = position_;
    while (p > 0) {
        char c = source_[p - 1];
        if (c == '\n') return true;
        if (c != ' ' && c != '\t') return false;
        --p;
    }
    return true;
}

void RbLexer::SkipSpacesAndContinuations() {
    while (!Eof()) {
        char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r') { Get(); continue; }
        if (c == '\\' && PeekNext() == '\n') { Get(); Get(); continue; }
        break;
    }
}

void RbLexer::SkipLineComment(bool *is_yard) {
    // Already on '#'
    Get();
    std::string body;
    while (!Eof() && Peek() != '\n') body.push_back(Get());
    // YARD tags begin with @param / @return / @yield etc.
    auto pos = body.find_first_not_of(" \t");
    if (pos != std::string::npos && body[pos] == '@') {
        if (is_yard) *is_yard = true;
        if (!pending_doc_.empty()) pending_doc_.push_back('\n');
        pending_doc_ += body;
    } else if (is_yard) {
        *is_yard = false;
    }
}

frontends::Token RbLexer::LexIdentifierOrKeyword() {
    auto loc = CurrentLoc();
    std::string s;
    if (Peek() == '@') { s.push_back(Get()); if (Peek() == '@') s.push_back(Get()); }
    if (Peek() == '$') s.push_back(Get());
    while (!Eof() && IsIdentPart(Peek())) s.push_back(Get());
    // method names may end with ? or !
    if (!Eof() && (Peek() == '?' || Peek() == '!')) s.push_back(Get());
    frontends::Token t;
    t.loc = loc;
    t.lexeme = s;
    if (s == "defined?") {
        t.kind = frontends::TokenKind::kKeyword;
    } else if (Keywords().count(s) > 0) {
        t.kind = frontends::TokenKind::kKeyword;
    } else {
        t.kind = frontends::TokenKind::kIdentifier;
    }
    prev_allows_unary_ = (t.kind == frontends::TokenKind::kKeyword);
    return t;
}

frontends::Token RbLexer::LexNumber() {
    auto loc = CurrentLoc();
    std::string s;
    bool is_float = false;
    if (Peek() == '0' && (PeekNext() == 'x' || PeekNext() == 'X' ||
                          PeekNext() == 'b' || PeekNext() == 'B' ||
                          PeekNext() == 'o' || PeekNext() == 'O')) {
        s.push_back(Get()); s.push_back(Get());
        while (!Eof() && (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
            if (Peek() != '_') s.push_back(Peek());
            Get();
        }
    } else {
        while (!Eof() && (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
            if (Peek() != '_') s.push_back(Peek());
            Get();
        }
        if (Peek() == '.' && std::isdigit(static_cast<unsigned char>(PeekNext()))) {
            is_float = true;
            s.push_back(Get());
            while (!Eof() && (std::isdigit(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
                if (Peek() != '_') s.push_back(Peek());
                Get();
            }
        }
        if (Peek() == 'e' || Peek() == 'E') {
            is_float = true;
            s.push_back(Get());
            if (Peek() == '+' || Peek() == '-') s.push_back(Get());
            while (!Eof() && std::isdigit(static_cast<unsigned char>(Peek()))) s.push_back(Get());
        }
    }
    (void)is_float;
    frontends::Token t;
    t.kind = frontends::TokenKind::kNumber;
    t.loc = loc;
    t.lexeme = s;
    prev_allows_unary_ = false;
    return t;
}

frontends::Token RbLexer::LexString(char quote) {
    auto loc = CurrentLoc();
    std::string s; s.push_back(Get());  // opening quote
    while (!Eof() && Peek() != quote) {
        if (Peek() == '\\') { s.push_back(Get()); if (!Eof()) s.push_back(Get()); continue; }
        if (quote == '"' && Peek() == '#' && PeekNext() == '{') {
            // Capture interpolation block opaquely
            s.push_back(Get()); s.push_back(Get());
            int depth = 1;
            while (!Eof() && depth > 0) {
                if (Peek() == '{') depth++;
                else if (Peek() == '}') depth--;
                if (depth > 0) s.push_back(Get());
                else { s.push_back(Get()); break; }
            }
            continue;
        }
        s.push_back(Get());
    }
    if (!Eof()) s.push_back(Get());
    frontends::Token t;
    t.kind = frontends::TokenKind::kString;
    t.loc = loc;
    t.lexeme = s;
    prev_allows_unary_ = false;
    return t;
}

frontends::Token RbLexer::LexSymbol() {
    auto loc = CurrentLoc();
    std::string s; s.push_back(Get());  // ':'
    if (Peek() == '"') {
        // :"quoted symbol"
        s.push_back(Get());
        while (!Eof() && Peek() != '"') {
            if (Peek() == '\\') { s.push_back(Get()); if (!Eof()) s.push_back(Get()); continue; }
            s.push_back(Get());
        }
        if (!Eof()) s.push_back(Get());
    } else {
        while (!Eof() && (IsIdentPart(Peek()) || Peek() == '?' || Peek() == '!' || Peek() == '=')) {
            s.push_back(Get());
        }
    }
    frontends::Token t;
    t.kind = frontends::TokenKind::kString;
    t.loc = loc;
    t.lexeme = s;
    prev_allows_unary_ = false;
    return t;
}

frontends::Token RbLexer::LexOperator() {
    auto loc = CurrentLoc();
    static const std::vector<std::string> three = {"**=", "<<=", ">>=", "===", "<=>", "&&=", "||=", "..."};
    static const std::vector<std::string> two   = {
        "**","==","!=","<=",">=","<<",">>","&&","||","+=","-=","*=","/=","%=","|=","&=","^=","::","..","=>","->","::"};
    if (position_ + 2 < source_.size()) {
        std::string s3 = source_.substr(position_, 3);
        for (auto &op : three) if (s3 == op) {
            Get(); Get(); Get();
            frontends::Token t; t.kind = frontends::TokenKind::kSymbol; t.lexeme = s3; t.loc = loc;
            prev_allows_unary_ = true; return t;
        }
    }
    if (position_ + 1 < source_.size()) {
        std::string s2 = source_.substr(position_, 2);
        for (auto &op : two) if (s2 == op) {
            Get(); Get();
            frontends::Token t; t.kind = frontends::TokenKind::kSymbol; t.lexeme = s2; t.loc = loc;
            prev_allows_unary_ = true; return t;
        }
    }
    char c = Get();
    frontends::Token t;
    t.kind = frontends::TokenKind::kSymbol;
    t.lexeme = std::string(1, c);
    t.loc = loc;
    // After closing bracket / identifier, '-' is binary; we set prev_allows_unary_
    // accordingly in the dispatcher.
    prev_allows_unary_ = (c != ')' && c != ']' && c != '}');
    return t;
}

frontends::Token RbLexer::NextToken() {
    while (true) {
        SkipSpacesAndContinuations();
        if (Eof()) {
            frontends::Token t; t.kind = frontends::TokenKind::kEndOfFile; t.loc = CurrentLoc(); return t;
        }
        char c = Peek();
        if (c == '#') { bool is_yard; SkipLineComment(&is_yard); continue; }
        if (c == '\n') {
            auto loc = CurrentLoc(); Get();
            frontends::Token t; t.kind = frontends::TokenKind::kNewline; t.lexeme = "\n"; t.loc = loc;
            prev_allows_unary_ = true;
            return t;
        }
        if (c == ';') {
            auto loc = CurrentLoc(); Get();
            frontends::Token t; t.kind = frontends::TokenKind::kSymbol; t.lexeme = ";"; t.loc = loc;
            prev_allows_unary_ = true;
            return t;
        }
        if (IsIdentStart(c) || c == '@' || c == '$') return LexIdentifierOrKeyword();
        if (std::isdigit(static_cast<unsigned char>(c))) return LexNumber();
        if (c == '"' || c == '\'') return LexString(c);
        if (c == ':' && (IsIdentStart(PeekNext()) || PeekNext() == '"')) return LexSymbol();
        return LexOperator();
    }
}

}  // namespace polyglot::ruby
