/**
 * @file     parser.cpp
 * @brief    Ruby parser
 *
 * @ingroup  Frontend / Ruby
 * @author   Manning Cyrus
 * @date     2026-04-26
 *
 * A practical recursive-descent parser for the Ruby subset that the
 * polyglot compiler needs.  We deliberately ignore exotic constructs
 * (proc-arg lambda shortcut, BEGIN/END blocks, refinements …) and aim
 * for clean coverage of: top-level methods, classes/modules, common
 * statements (if/unless/while/until/for/case/begin), expression
 * grammar with all the standard operator precedences, blocks `{…}` /
 * `do…end`, YARD type tags via comments, and block parameters.
 */
#include <cctype>
#include <functional>
#include <sstream>
#include <unordered_set>

#include "frontends/ruby/include/ruby_parser.h"

namespace polyglot::ruby {

namespace {

std::string TrimDocLine(const std::string &line) {
  size_t a = 0;
  while (a < line.size() && (line[a] == ' ' || line[a] == '\t' || line[a] == '#'))
    ++a;
  size_t b = line.size();
  while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r'))
    --b;
  return line.substr(a, b - a);
}

} // namespace

void RbParser::Advance() {
  current_ = lexer_.NextToken();
  auto d = lexer_.TakeDocComment();
  if (!d.empty()) {
    if (!pending_doc_.empty())
      pending_doc_.push_back('\n');
    pending_doc_ += d;
  }
}

bool RbParser::IsKeyword(const std::string &k) const {
  return current_.kind == frontends::TokenKind::kKeyword && current_.lexeme == k;
}
bool RbParser::IsSymbol(const std::string &s) const {
  return current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == s;
}
bool RbParser::MatchKeyword(const std::string &k) {
  if (IsKeyword(k)) {
    Advance();
    return true;
  }
  return false;
}
bool RbParser::MatchSymbol(const std::string &s) {
  if (IsSymbol(s)) {
    Advance();
    return true;
  }
  return false;
}
bool RbParser::ExpectKeyword(const std::string &k, const std::string &msg) {
  if (MatchKeyword(k))
    return true;
  diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
  return false;
}
bool RbParser::ExpectSymbol(const std::string &s, const std::string &msg) {
  if (MatchSymbol(s))
    return true;
  diagnostics_.Report(current_.loc, msg + " (got '" + current_.lexeme + "')");
  return false;
}

bool RbParser::AtTerminator() const {
  return current_.kind == frontends::TokenKind::kNewline ||
         (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == ";") ||
         current_.kind == frontends::TokenKind::kEndOfFile;
}

void RbParser::SkipTerminators() {
  while (current_.kind == frontends::TokenKind::kNewline ||
         (current_.kind == frontends::TokenKind::kSymbol && current_.lexeme == ";")) {
    Advance();
  }
}

void RbParser::ConsumeNewlinesBeforeLogicalOperator() {
  if (current_.kind != frontends::TokenKind::kNewline)
    return;
  do {
    Advance();
  } while (current_.kind == frontends::TokenKind::kNewline);
  logical_operator_after_newline_ =
      IsSymbol("&&") || IsSymbol("||") || IsKeyword("and") || IsKeyword("or");
}

void RbParser::GateLineLeadingLogicalOperator(const core::SourceLoc &loc) {
  if (!logical_operator_after_newline_)
    return;
  logical_operator_after_newline_ = false;
  if (!frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby4_0)) {
    diagnostics_.ReportError(
        loc, frontends::ErrorCode::kLangVersionMismatch,
        "line-leading logical operator continuation requires Ruby 4.0 or newer");
  }
}

std::shared_ptr<TypeNode> RbParser::ParseYardType(const std::string &raw) {
  if (raw.empty())
    return nullptr;
  auto t = std::make_shared<TypeNode>();
  // Take everything up to ',' or ' ' as base name; ignore generics for now.
  std::string name;
  for (char c : raw) {
    if (c == ',' || c == ' ' || c == '\t')
      break;
    name.push_back(c);
  }
  t->name = name;
  return t;
}

void RbParser::ParseModule() {
  module_ = std::make_shared<Module>();
  Advance();
  SkipTerminators();
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    auto s = ParseTopLevel();
    if (s)
      module_->body.push_back(s);
    else
      Advance();
    SkipTerminators();
  }
}

std::shared_ptr<Module> RbParser::TakeModule() {
  return std::move(module_);
}

std::shared_ptr<Statement> RbParser::ParseTopLevel() {
  return ParseStatement();
}

std::shared_ptr<Statement> RbParser::ParseStatement() {
  if (IsKeyword("def"))
    return ParseDef();
  if (IsKeyword("class"))
    return ParseClass();
  if (IsKeyword("module"))
    return ParseModuleStmt();
  if (IsKeyword("if"))
    return ParseIf(false);
  if (IsKeyword("unless"))
    return ParseIf(true);
  if (IsKeyword("while"))
    return ParseWhile(false);
  if (IsKeyword("until"))
    return ParseWhile(true);
  if (IsKeyword("for"))
    return ParseFor();
  if (IsKeyword("case"))
    return ParseCase();
  if (IsKeyword("begin"))
    return ParseBegin();
  if (IsKeyword("return")) {
    auto loc = current_.loc;
    Advance();
    auto s = std::make_shared<ReturnStmt>();
    s->loc = loc;
    if (!AtTerminator() && !IsKeyword("end"))
      s->value = ParseExpression();
    return s;
  }
  if (IsKeyword("yield")) {
    auto loc = current_.loc;
    Advance();
    auto s = std::make_shared<YieldStmt>();
    s->loc = loc;
    if (!AtTerminator()) {
      s->args.push_back(ParseExpression());
      while (MatchSymbol(","))
        s->args.push_back(ParseExpression());
    }
    return s;
  }
  if (IsKeyword("break")) {
    auto loc = current_.loc;
    Advance();
    auto s = std::make_shared<BreakStmt>();
    s->loc = loc;
    if (!AtTerminator())
      s->value = ParseExpression();
    return s;
  }
  if (IsKeyword("next")) {
    auto loc = current_.loc;
    Advance();
    auto s = std::make_shared<NextStmt>();
    s->loc = loc;
    if (!AtTerminator())
      s->value = ParseExpression();
    return s;
  }
  if (IsKeyword("redo")) {
    auto s = std::make_shared<RedoStmt>();
    s->loc = current_.loc;
    Advance();
    return s;
  }
  if (IsKeyword("retry")) {
    auto s = std::make_shared<RetryStmt>();
    s->loc = current_.loc;
    Advance();
    return s;
  }

  auto loc = current_.loc;
  auto e = ParseExpression();
  auto stmt = std::make_shared<ExprStmt>();
  stmt->loc = loc;
  stmt->expr = e;
  return stmt;
}

std::shared_ptr<Block> RbParser::ParseBlockUntil(std::initializer_list<std::string> terminators) {
  auto block = std::make_shared<Block>();
  block->loc = current_.loc;
  SkipTerminators();
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    bool stop = false;
    for (auto &t : terminators) {
      if (IsKeyword(t) || IsSymbol(t)) {
        stop = true;
        break;
      }
    }
    if (stop)
      break;
    auto s = ParseStatement();
    if (s)
      block->stmts.push_back(s);
    else
      Advance();
    SkipTerminators();
  }
  return block;
}

std::vector<Param> RbParser::ParseDefParams() {
  std::vector<Param> params;
  bool had_paren = MatchSymbol("(");

  // Build YARD param-type map from pending_doc_.
  std::unordered_map<std::string, std::shared_ptr<TypeNode>> ptypes;
  std::shared_ptr<TypeNode> rtype;
  if (!pending_doc_.empty()) {
    std::istringstream is(pending_doc_);
    std::string line;
    while (std::getline(is, line)) {
      line = TrimDocLine(line);
      // @param name [Type] desc   OR   @param [Type] name desc
      if (line.rfind("@param", 0) == 0) {
        std::string rest = line.substr(6);
        size_t i = 0;
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
          ++i;
        std::string a, type_str;
        if (i < rest.size() && rest[i] == '[') {
          size_t j = rest.find(']', i);
          if (j != std::string::npos) {
            type_str = rest.substr(i + 1, j - i - 1);
            i = j + 1;
            while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
              ++i;
            while (i < rest.size() &&
                   (std::isalnum(static_cast<unsigned char>(rest[i])) || rest[i] == '_'))
              a.push_back(rest[i++]);
          }
        } else {
          while (i < rest.size() &&
                 (std::isalnum(static_cast<unsigned char>(rest[i])) || rest[i] == '_'))
            a.push_back(rest[i++]);
          while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
            ++i;
          if (i < rest.size() && rest[i] == '[') {
            size_t j = rest.find(']', i);
            if (j != std::string::npos)
              type_str = rest.substr(i + 1, j - i - 1);
          }
        }
        if (!a.empty())
          ptypes[a] = ParseYardType(type_str);
      } else if (line.rfind("@return", 0) == 0) {
        size_t i = line.find('[');
        size_t j = (i == std::string::npos) ? std::string::npos : line.find(']', i);
        if (i != std::string::npos && j != std::string::npos) {
          rtype = ParseYardType(line.substr(i + 1, j - i - 1));
        }
      }
    }
  }
  pending_doc_.clear();

  auto stop_at = [&]() {
    if (had_paren)
      return IsSymbol(")");
    return AtTerminator() || IsSymbol("|") || (params.empty() && IsSymbol("="));
  };

  while (!stop_at() && current_.kind != frontends::TokenKind::kEndOfFile) {
    Param p;
    if (MatchSymbol("...")) {
      p.name = "...";
      p.forwarding = true;
      if (!frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby2_7)) {
        diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                                 "anonymous argument forwarding requires Ruby 2.7 or newer");
      }
    } else if (MatchSymbol("**"))
      p.double_splat = true;
    else if (MatchSymbol("*"))
      p.splat = true;
    else if (MatchSymbol("&"))
      p.block = true;
    if (!p.forwarding && current_.kind == frontends::TokenKind::kIdentifier) {
      p.name = current_.lexeme;
      Advance();
    }
    if (!p.forwarding && MatchSymbol("="))
      p.default_value = ParseExpression();
    else if (MatchSymbol(":")) {
      // keyword argument; default is optional
      if (!IsSymbol(",") && !stop_at())
        p.default_value = ParseExpression();
    }
    auto it = ptypes.find(p.name);
    if (it != ptypes.end())
      p.type = it->second;
    params.push_back(p);
    if (!MatchSymbol(","))
      break;
  }
  if (had_paren)
    ExpectSymbol(")", "expected ')'");

  if (rtype) {
    Param dummy;
    dummy.name = "$return$";
    dummy.type = rtype;
    params.push_back(dummy);
  }
  return params;
}

std::shared_ptr<Statement> RbParser::ParseDef() {
  auto loc = current_.loc;
  Advance(); // 'def'
  auto m = std::make_shared<MethodDecl>();
  m->loc = loc;
  if (IsKeyword("self") && current_.lexeme == "self") {
    m->is_self = true;
    Advance();
    ExpectSymbol(".", "expected '.' after 'self'");
  }
  if (current_.kind == frontends::TokenKind::kIdentifier ||
      current_.kind == frontends::TokenKind::kKeyword) {
    m->name = current_.lexeme;
    Advance();
  }
  m->params = ParseDefParams();
  if (!m->params.empty() && m->params.back().name == "$return$") {
    m->return_type = m->params.back().type;
    m->params.pop_back();
  }
  if (MatchSymbol("=")) {
    if (!frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby3_0)) {
      diagnostics_.ReportError(loc, frontends::ErrorCode::kLangVersionMismatch,
                               "endless method definitions require Ruby 3.0 or newer");
    }
    auto expression = ParseExpression();
    auto statement = std::make_shared<ExprStmt>();
    statement->loc = expression ? expression->loc : loc;
    statement->expr = expression;
    auto body = std::make_shared<Block>();
    body->loc = loc;
    body->stmts.push_back(std::move(statement));
    m->body = std::move(body);
    return m;
  }
  SkipTerminators();
  auto body = ParseBlockUntil({"end", "rescue", "ensure"});
  // If rescue/ensure follow, wrap as begin-rescue.
  if (IsKeyword("rescue") || IsKeyword("ensure")) {
    auto bs = std::make_shared<BeginStmt>();
    bs->loc = loc;
    bs->body = body;
    while (IsKeyword("rescue")) {
      Advance();
      BeginStmt::Rescue r;
      // optional: ClassName[, ClassName] [=> var]
      while (current_.kind == frontends::TokenKind::kIdentifier && !IsKeyword("then")) {
        auto t = std::make_shared<TypeNode>();
        t->name = current_.lexeme;
        r.classes.push_back(t);
        Advance();
        if (!MatchSymbol(","))
          break;
      }
      if (MatchSymbol("=>")) {
        if (current_.kind == frontends::TokenKind::kIdentifier) {
          r.var = current_.lexeme;
          Advance();
        }
      }
      MatchKeyword("then");
      SkipTerminators();
      r.body = ParseBlockUntil({"end", "rescue", "ensure"});
      bs->rescues.push_back(r);
    }
    if (MatchKeyword("ensure")) {
      SkipTerminators();
      bs->ensure_branch = ParseBlockUntil({"end"});
    }
    m->body = bs;
  } else {
    m->body = body;
  }
  ExpectKeyword("end", "expected 'end' after def");
  return m;
}

std::shared_ptr<Statement> RbParser::ParseClass() {
  auto loc = current_.loc;
  Advance();
  auto c = std::make_shared<ClassDecl>();
  c->loc = loc;
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    c->name = current_.lexeme;
    Advance();
  }
  if (MatchSymbol("<")) {
    c->superclass = ParseExpression();
  }
  SkipTerminators();
  while (current_.kind != frontends::TokenKind::kEndOfFile && !IsKeyword("end")) {
    auto s = ParseStatement();
    if (s)
      c->body.push_back(s);
    else
      Advance();
    SkipTerminators();
  }
  ExpectKeyword("end", "expected 'end'");
  return c;
}

std::shared_ptr<Statement> RbParser::ParseModuleStmt() {
  auto loc = current_.loc;
  Advance();
  auto m = std::make_shared<ModuleDecl>();
  m->loc = loc;
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    m->name = current_.lexeme;
    Advance();
  }
  SkipTerminators();
  while (current_.kind != frontends::TokenKind::kEndOfFile && !IsKeyword("end")) {
    auto s = ParseStatement();
    if (s)
      m->body.push_back(s);
    else
      Advance();
    SkipTerminators();
  }
  ExpectKeyword("end", "expected 'end'");
  return m;
}

std::shared_ptr<Statement> RbParser::ParseIf(bool is_unless) {
  auto loc = current_.loc;
  Advance();
  auto s = std::make_shared<IfStmt>();
  s->loc = loc;
  s->unless = is_unless;
  s->cond = ParseExpression();
  MatchKeyword("then");
  SkipTerminators();
  s->then_branch = ParseBlockUntil({"end", "else", "elsif"});
  if (MatchKeyword("elsif")) {
    // Recurse: parse rest as nested if
    auto nested = std::make_shared<IfStmt>();
    nested->loc = current_.loc;
    nested->cond = ParseExpression();
    MatchKeyword("then");
    SkipTerminators();
    nested->then_branch = ParseBlockUntil({"end", "else", "elsif"});
    if (MatchKeyword("else")) {
      SkipTerminators();
      nested->else_branch = ParseBlockUntil({"end"});
    } else if (IsKeyword("elsif")) {
      // Re-enter: simulate by wrapping
      // Approach: treat rest by recursing with a synthetic ParseIf
      // We rewind by treating elsif chain iteratively:
      // (the simpler approach: build nested IfStmts directly)
    }
    s->else_branch = nested;
  } else if (MatchKeyword("else")) {
    SkipTerminators();
    s->else_branch = ParseBlockUntil({"end"});
  }
  ExpectKeyword("end", "expected 'end'");
  return s;
}

std::shared_ptr<Statement> RbParser::ParseWhile(bool is_until) {
  auto loc = current_.loc;
  Advance();
  auto s = std::make_shared<WhileStmt>();
  s->loc = loc;
  s->until = is_until;
  s->cond = ParseExpression();
  MatchKeyword("do");
  SkipTerminators();
  s->body = ParseBlockUntil({"end"});
  ExpectKeyword("end", "expected 'end'");
  return s;
}

std::shared_ptr<Statement> RbParser::ParseFor() {
  auto loc = current_.loc;
  Advance();
  auto s = std::make_shared<ForStmt>();
  s->loc = loc;
  if (current_.kind == frontends::TokenKind::kIdentifier) {
    s->var = current_.lexeme;
    Advance();
  }
  ExpectKeyword("in", "expected 'in'");
  s->iterable = ParseExpression();
  MatchKeyword("do");
  SkipTerminators();
  s->body = ParseBlockUntil({"end"});
  ExpectKeyword("end", "expected 'end'");
  return s;
}

std::shared_ptr<Statement> RbParser::ParseCase() {
  auto loc = current_.loc;
  Advance();
  auto s = std::make_shared<CaseStmt>();
  s->loc = loc;
  if (!AtTerminator() && !IsKeyword("when"))
    s->subject = ParseExpression();
  SkipTerminators();
  while (IsKeyword("when") || IsKeyword("in")) {
    CaseStmt::When w;
    w.is_pattern = IsKeyword("in");
    Advance();
    if (w.is_pattern &&
        !frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby3_0)) {
      diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kLangVersionMismatch,
                               "case/in pattern matching requires Ruby 3.0 or newer");
    }
    bool old_pattern_state = parsing_pattern_;
    parsing_pattern_ = w.is_pattern;
    w.tests.push_back(ParseExpression());
    while (!w.is_pattern && MatchSymbol(","))
      w.tests.push_back(ParseExpression());
    parsing_pattern_ = old_pattern_state;
    if (IsKeyword("if") || IsKeyword("unless")) {
      w.guard_unless = IsKeyword("unless");
      Advance();
      w.guard = ParseExpression();
    }
    MatchKeyword("then");
    SkipTerminators();
    w.body = ParseBlockUntil({"end", "when", "in", "else"});
    s->whens.push_back(w);
  }
  if (MatchKeyword("else")) {
    SkipTerminators();
    s->else_branch = ParseBlockUntil({"end"});
  }
  ExpectKeyword("end", "expected 'end'");
  return s;
}

std::shared_ptr<Statement> RbParser::ParseBegin() {
  auto loc = current_.loc;
  Advance();
  auto s = std::make_shared<BeginStmt>();
  s->loc = loc;
  SkipTerminators();
  s->body = ParseBlockUntil({"end", "rescue", "else", "ensure"});
  while (IsKeyword("rescue")) {
    Advance();
    BeginStmt::Rescue r;
    while (current_.kind == frontends::TokenKind::kIdentifier && !IsKeyword("then")) {
      auto t = std::make_shared<TypeNode>();
      t->name = current_.lexeme;
      r.classes.push_back(t);
      Advance();
      if (!MatchSymbol(","))
        break;
    }
    if (MatchSymbol("=>")) {
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        r.var = current_.lexeme;
        Advance();
      }
    }
    MatchKeyword("then");
    SkipTerminators();
    r.body = ParseBlockUntil({"end", "rescue", "else", "ensure"});
    s->rescues.push_back(r);
  }
  if (MatchKeyword("else")) {
    SkipTerminators();
    s->else_branch = ParseBlockUntil({"end", "ensure"});
  }
  if (MatchKeyword("ensure")) {
    SkipTerminators();
    s->ensure_branch = ParseBlockUntil({"end"});
  }
  ExpectKeyword("end", "expected 'end'");
  return s;
}

// ============================================================================
// Expressions
// ============================================================================

std::shared_ptr<Expression> RbParser::ParseExpression() {
  return ParseAssignment();
}

std::shared_ptr<Expression> RbParser::ParseAssignment() {
  auto left = ParseTernary();
  static const std::vector<std::string> ops = {
      "=", "+=", "-=", "*=", "/=", "%=", "**=", "|=", "&=", "^=", "||=", "&&=", "<<=", ">>="};
  for (auto &op : ops) {
    if (IsSymbol(op)) {
      Advance();
      auto rhs = ParseAssignment();
      auto a = std::make_shared<AssignExpr>();
      a->loc = left ? left->loc : current_.loc;
      a->op = op;
      a->target = left;
      a->value = rhs;
      return a;
    }
  }
  if (MatchSymbol("=>")) {
    if (!frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby3_0)) {
      diagnostics_.ReportError(left ? left->loc : current_.loc,
                               frontends::ErrorCode::kLangVersionMismatch,
                               "rightward assignment requires Ruby 3.0 or newer");
    }
    auto target = ParseTernary();
    auto assignment = std::make_shared<AssignExpr>();
    assignment->loc = left ? left->loc : current_.loc;
    assignment->op = "=>";
    assignment->target = target;
    assignment->value = left;
    return assignment;
  }
  return left;
}

std::shared_ptr<Expression> RbParser::ParseTernary() {
  auto cond = ParseRange();
  if (MatchSymbol("?")) {
    auto t = ParseAssignment();
    ExpectSymbol(":", "expected ':'");
    auto e = ParseAssignment();
    auto x = std::make_shared<TernaryExpr>();
    x->loc = cond ? cond->loc : current_.loc;
    x->cond = cond;
    x->then_e = t;
    x->else_e = e;
    return x;
  }
  return cond;
}

std::shared_ptr<Expression> RbParser::ParseRange() {
  auto a = ParseOrExpr();
  if (IsSymbol("..") || IsSymbol("...")) {
    bool exc = current_.lexeme == "...";
    Advance();
    auto b = ParseOrExpr();
    auto r = std::make_shared<RangeExpr>();
    r->loc = a ? a->loc : current_.loc;
    r->from = a;
    r->to = b;
    r->exclusive = exc;
    return r;
  }
  return a;
}

std::shared_ptr<Expression> RbParser::ParseOrExpr() {
  auto l = ParseAndExpr();
  while (IsSymbol("||") || IsKeyword("or")) {
    auto operator_loc = current_.loc;
    GateLineLeadingLogicalOperator(operator_loc);
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseAndExpr();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseAndExpr() {
  auto l = ParseNotExpr();
  while (true) {
    ConsumeNewlinesBeforeLogicalOperator();
    if (!IsSymbol("&&") && !IsKeyword("and"))
      break;
    auto operator_loc = current_.loc;
    GateLineLeadingLogicalOperator(operator_loc);
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseNotExpr();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseNotExpr() {
  if (IsKeyword("not") || IsSymbol("!")) {
    auto loc = current_.loc;
    std::string op = current_.lexeme;
    Advance();
    auto u = std::make_shared<UnaryExpr>();
    u->loc = loc;
    u->op = op;
    u->operand = ParseNotExpr();
    return u;
  }
  return ParseDefined();
}
std::shared_ptr<Expression> RbParser::ParseDefined() {
  if (IsKeyword("defined?")) {
    auto loc = current_.loc;
    Advance();
    auto u = std::make_shared<UnaryExpr>();
    u->loc = loc;
    u->op = "defined?";
    u->operand = ParseUnary();
    return u;
  }
  return ParseEquality();
}
std::shared_ptr<Expression> RbParser::ParseEquality() {
  auto l = ParseComparison();
  while (IsSymbol("==") || IsSymbol("!=") || IsSymbol("===") || IsSymbol("=~") || IsSymbol("!~") ||
         IsSymbol("<=>")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseComparison();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseComparison() {
  auto l = ParseBitOr();
  while (IsSymbol("<") || IsSymbol("<=") || IsSymbol(">") || IsSymbol(">=")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseBitOr();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseBitOr() {
  auto l = ParseBitAnd();
  while (IsSymbol("|") || IsSymbol("^")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseBitAnd();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseBitAnd() {
  auto l = ParseShift();
  while (IsSymbol("&")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseShift();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseShift() {
  auto l = ParseAdditive();
  while (IsSymbol("<<") || IsSymbol(">>")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseAdditive();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseAdditive() {
  auto l = ParseMultiplicative();
  while (IsSymbol("+") || IsSymbol("-")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseMultiplicative();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseMultiplicative() {
  auto l = ParseUnary();
  while (IsSymbol("*") || IsSymbol("/") || IsSymbol("%")) {
    std::string op = current_.lexeme;
    Advance();
    auto r = ParseUnary();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = op;
    b->left = l;
    b->right = r;
    l = b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParseUnary() {
  if (IsSymbol("-") || IsSymbol("+") || IsSymbol("~")) {
    auto loc = current_.loc;
    std::string op = current_.lexeme;
    Advance();
    auto u = std::make_shared<UnaryExpr>();
    u->loc = loc;
    u->op = op;
    u->operand = ParseUnary();
    return u;
  }
  return ParsePower();
}
std::shared_ptr<Expression> RbParser::ParsePower() {
  auto l = ParsePostfix();
  if (IsSymbol("**")) {
    Advance();
    auto r = ParseUnary();
    auto b = std::make_shared<BinaryExpr>();
    b->loc = l ? l->loc : current_.loc;
    b->op = "**";
    b->left = l;
    b->right = r;
    return b;
  }
  return l;
}
std::shared_ptr<Expression> RbParser::ParsePostfix() {
  return ParseCallTail(ParsePrimary());
}

std::vector<std::shared_ptr<Expression>> RbParser::ParseCallArgs(bool until_paren) {
  std::vector<std::shared_ptr<Expression>> args;
  while (current_.kind != frontends::TokenKind::kEndOfFile) {
    if (until_paren && IsSymbol(")"))
      break;
    if (!until_paren && AtTerminator())
      break;
    args.push_back(ParseExpression());
    if (!MatchSymbol(","))
      break;
  }
  return args;
}

void RbParser::ParseAttachedBlock(const std::shared_ptr<CallExpr> &call) {
  bool brace_block = MatchSymbol("{");
  bool do_block = false;
  if (!brace_block)
    do_block = MatchKeyword("do");
  if (!brace_block && !do_block)
    return;

  bool explicit_empty_params = MatchSymbol("||");
  bool explicit_params = explicit_empty_params;
  if (!explicit_empty_params && MatchSymbol("|")) {
    explicit_params = true;
    while (current_.kind == frontends::TokenKind::kIdentifier) {
      call->block_params.push_back(current_.lexeme);
      Advance();
      if (!MatchSymbol(","))
        break;
    }
    ExpectSymbol("|", "expected '|'");
  }

  if (do_block)
    SkipTerminators();
  block_it_usage_.push_back(explicit_params ? -1 : 0);
  call->block = ParseBlockUntil(brace_block ? std::initializer_list<std::string>{"}"}
                                             : std::initializer_list<std::string>{"end"});
  int implicit_it_usage = block_it_usage_.back();
  block_it_usage_.pop_back();
  if (implicit_it_usage == 1) {
    call->uses_implicit_it = true;
    call->block_params.push_back("it");
    if (!frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby3_4)) {
      diagnostics_.ReportError(call->loc, frontends::ErrorCode::kLangVersionMismatch,
                               "implicit block parameter 'it' requires Ruby 3.4 or newer");
    }
  }

  if (brace_block)
    ExpectSymbol("}", "expected '}'");
  else
    ExpectKeyword("end", "expected 'end'");
}

std::shared_ptr<Expression> RbParser::ParseCallTail(std::shared_ptr<Expression> e) {
  while (true) {
    if (IsSymbol(".") || IsSymbol("&.")) {
      bool safe = IsSymbol("&.");
      auto operator_loc = current_.loc;
      Advance();
      if (safe &&
          !frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby2_7)) {
        diagnostics_.ReportError(operator_loc, frontends::ErrorCode::kLangVersionMismatch,
                                 "safe navigation requires Ruby 2.3 or newer");
      }
      auto m = std::make_shared<MemberExpr>();
      m->loc = e ? e->loc : current_.loc;
      m->obj = e;
      m->safe = safe;
      if (current_.kind == frontends::TokenKind::kIdentifier ||
          current_.kind == frontends::TokenKind::kKeyword) {
        m->member = current_.lexeme;
        Advance();
      }
      // Optional call: foo.bar(args) or foo.bar arg1, arg2
      if (MatchSymbol("(")) {
        auto call = std::make_shared<CallExpr>();
        call->loc = m->loc;
        call->receiver = e;
        call->method = m->member;
        call->safe = safe;
        call->args = ParseCallArgs(true);
        ExpectSymbol(")", "expected ')'");
        ParseAttachedBlock(call);
        e = call;
      } else if (IsSymbol("{") || IsKeyword("do")) {
        auto call = std::make_shared<CallExpr>();
        call->loc = m->loc;
        call->receiver = e;
        call->method = m->member;
        call->safe = safe;
        ParseAttachedBlock(call);
        e = call;
      } else {
        e = m;
      }
    } else if (MatchSymbol("[")) {
      auto ix = std::make_shared<IndexExpr>();
      ix->loc = e ? e->loc : current_.loc;
      ix->obj = e;
      while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
        ix->idx.push_back(ParseExpression());
        if (!MatchSymbol(","))
          break;
      }
      ExpectSymbol("]", "expected ']'");
      e = ix;
    } else if (MatchSymbol("::")) {
      auto m = std::make_shared<MemberExpr>();
      m->loc = e ? e->loc : current_.loc;
      m->obj = e;
      if (current_.kind == frontends::TokenKind::kIdentifier) {
        m->member = current_.lexeme;
        Advance();
      }
      e = m;
    } else {
      break;
    }
  }
  return e;
}

std::shared_ptr<Expression> RbParser::ParsePrimary() {
  auto loc = current_.loc;
  if (current_.kind == frontends::TokenKind::kNumber) {
    auto l = std::make_shared<Literal>();
    l->loc = loc;
    l->value = current_.lexeme;
    l->kind = (current_.lexeme.find('.') != std::string::npos ||
               current_.lexeme.find('e') != std::string::npos)
                  ? Literal::Kind::kFloat
                  : Literal::Kind::kInt;
    Advance();
    return l;
  }
  if (current_.kind == frontends::TokenKind::kString) {
    auto l = std::make_shared<Literal>();
    l->loc = loc;
    l->value = current_.lexeme;
    l->kind = (!current_.lexeme.empty() && current_.lexeme[0] == ':') ? Literal::Kind::kSymbol
                                                                      : Literal::Kind::kString;
    if (current_.raw_lexeme == "__polyglot_ruby_heredoc__" ||
        current_.raw_lexeme == "__polyglot_ruby_interpolated_heredoc__" ||
        current_.raw_lexeme == "__polyglot_ruby_unterminated_heredoc__") {
      l->is_heredoc = true;
      l->heredoc_allows_interpolation =
          current_.raw_lexeme == "__polyglot_ruby_interpolated_heredoc__";
      if (current_.raw_lexeme == "__polyglot_ruby_interpolated_heredoc__") {
        diagnostics_.ReportError(loc, frontends::ErrorCode::kUnsupportedSyntax,
                                 "interpolated Ruby heredocs are not yet represented faithfully");
      } else if (current_.raw_lexeme == "__polyglot_ruby_unterminated_heredoc__") {
        diagnostics_.ReportError(loc, frontends::ErrorCode::kUnsupportedSyntax,
                                 "unterminated Ruby heredoc");
      }
    }
    Advance();
    return l;
  }
  if (IsKeyword("true") || IsKeyword("false")) {
    auto l = std::make_shared<Literal>();
    l->loc = loc;
    l->kind = Literal::Kind::kBool;
    l->value = current_.lexeme;
    Advance();
    return l;
  }
  if (IsKeyword("nil")) {
    auto l = std::make_shared<Literal>();
    l->loc = loc;
    l->kind = Literal::Kind::kNil;
    l->value = "nil";
    Advance();
    return l;
  }
  if (IsKeyword("self") || IsKeyword("super")) {
    auto id = std::make_shared<Identifier>();
    id->loc = loc;
    id->name = current_.lexeme;
    Advance();
    return id;
  }
  if (MatchSymbol("(")) {
    auto e = ParseExpression();
    ExpectSymbol(")", "expected ')'");
    return e;
  }
  if (IsSymbol("["))
    return ParseArray();
  if (IsSymbol("{"))
    return ParseHash();

  if (IsSymbol("...")) {
    auto forwarding = std::make_shared<Identifier>();
    forwarding->loc = loc;
    forwarding->name = "...";
    if (!frontends::RubyVersionAtLeast(ruby_version_, frontends::RubyVersion::kRuby2_7)) {
      diagnostics_.ReportError(loc, frontends::ErrorCode::kLangVersionMismatch,
                               "anonymous argument forwarding requires Ruby 2.7 or newer");
    }
    Advance();
    return forwarding;
  }

  if (current_.kind == frontends::TokenKind::kIdentifier) {
    std::string name = current_.lexeme;
    if (name == "it" && !block_it_usage_.empty() && block_it_usage_.back() == 0)
      block_it_usage_.back() = 1;
    Advance();
    // Method call: name(args), name arg1, arg2 (no paren) — heuristic: if next
    // token is '(' or starts an expression on the same logical line we treat
    // it as a call.  For simplicity here we only honour the parenthesised form.
    if (MatchSymbol("(")) {
      auto c = std::make_shared<CallExpr>();
      c->loc = loc;
      c->method = name;
      c->args = ParseCallArgs(true);
      ExpectSymbol(")", "expected ')'");
      ParseAttachedBlock(c);
      return c;
    }
    if (IsSymbol("{") || IsKeyword("do")) {
      auto c = std::make_shared<CallExpr>();
      c->loc = loc;
      c->method = name;
      ParseAttachedBlock(c);
      return c;
    }
    auto id = std::make_shared<Identifier>();
    id->loc = loc;
    id->name = name;
    return id;
  }

  diagnostics_.Report(loc, "unexpected token '" + current_.lexeme + "'");
  auto err = std::make_shared<Identifier>();
  err->loc = loc;
  err->name = current_.lexeme;
  Advance();
  return err;
}

std::shared_ptr<Expression> RbParser::ParseArray() {
  auto loc = current_.loc;
  Advance(); // '['
  auto a = std::make_shared<ArrayLit>();
  a->loc = loc;
  while (!IsSymbol("]") && current_.kind != frontends::TokenKind::kEndOfFile) {
    a->elems.push_back(ParseExpression());
    if (!MatchSymbol(","))
      break;
  }
  ExpectSymbol("]", "expected ']'");
  return a;
}

std::shared_ptr<Expression> RbParser::ParseHash() {
  auto loc = current_.loc;
  Advance(); // '{'
  auto h = std::make_shared<HashLit>();
  h->loc = loc;
  while (!IsSymbol("}") && current_.kind != frontends::TokenKind::kEndOfFile) {
    HashLit::Pair p;
    p.key = ParseExpression();
    bool rocket = MatchSymbol("=>");
    bool label = !rocket && MatchSymbol(":");
    if ((rocket || label) && !IsSymbol(",") && !IsSymbol("}")) {
      p.value = ParseExpression();
    } else if (label) {
      auto key_name = std::dynamic_pointer_cast<Identifier>(p.key);
      if (!key_name) {
        diagnostics_.ReportError(p.key ? p.key->loc : loc,
                                 frontends::ErrorCode::kInvalidExpression,
                                 "hash value omission requires an identifier key");
      } else {
        auto value = std::make_shared<Identifier>();
        value->loc = key_name->loc;
        value->name = key_name->name;
        p.value = value;
      }
      auto required = parsing_pattern_ ? frontends::RubyVersion::kRuby3_0
                                       : frontends::RubyVersion::kRuby3_1;
      if (!frontends::RubyVersionAtLeast(ruby_version_, required)) {
        diagnostics_.ReportError(p.key ? p.key->loc : loc,
                                 frontends::ErrorCode::kLangVersionMismatch,
                                 parsing_pattern_
                                     ? "hash capture patterns require Ruby 3.0 or newer"
                                     : "hash value omission requires Ruby 3.1 or newer");
      }
    } else if (!rocket) {
      diagnostics_.ReportError(current_.loc, frontends::ErrorCode::kUnexpectedToken,
                               "expected '=>' or ':' in hash literal");
    }
    h->pairs.push_back(p);
    if (!MatchSymbol(","))
      break;
  }
  ExpectSymbol("}", "expected '}'");
  return h;
}

} // namespace polyglot::ruby
