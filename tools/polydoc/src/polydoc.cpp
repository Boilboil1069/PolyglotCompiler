/**
 * @file     polydoc.cpp
 * @brief    Documentation extractor for `.ploy` source files.
 *
 * @ingroup  Tools / polydoc
 * @author   Manning Cyrus
 *
 * Walks one or more `.ploy` files, extracts `///` doc-comment blocks
 * attached to top-level FUNC / STRUCT / LET / VAR declarations, and
 * emits the result as Markdown or JSON.  Introduced in v1.18.0.
 *
 * Usage:
 *   polydoc <file.ploy> [file2.ploy ...]      # Markdown to stdout
 *   polydoc --json <file.ploy>                # JSON to stdout
 *   polydoc -o out.md <file.ploy>             # write to file
 */
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "frontends/common/include/diagnostics.h"
#include "frontends/ploy/include/ploy_ast.h"
#include "frontends/ploy/include/ploy_lexer.h"
#include "frontends/ploy/include/ploy_parser.h"

namespace {

struct DocEntry {
  std::string kind;        // "func" | "struct" | "let" | "var"
  std::string name;
  std::string signature;   // human-readable signature line
  std::vector<std::string> doc;
};

std::string TypeNodeToString(const std::shared_ptr<polyglot::ploy::TypeNode> &t) {
  using namespace polyglot::ploy;
  if (!t) return "";
  if (auto st = std::dynamic_pointer_cast<SimpleType>(t)) return st->name;
  if (auto pt = std::dynamic_pointer_cast<ParameterizedType>(t)) {
    std::string s = pt->name + "<";
    for (size_t i = 0; i < pt->type_args.size(); ++i) {
      if (i) s += ", ";
      s += TypeNodeToString(pt->type_args[i]);
    }
    s += ">";
    return s;
  }
  return "?";
}

std::string FuncSignature(const polyglot::ploy::FuncDecl &f) {
  std::string s = "FUNC " + f.name + "(";
  for (size_t i = 0; i < f.params.size(); ++i) {
    if (i) s += ", ";
    s += f.params[i].name;
    if (f.params[i].type) s += ": " + TypeNodeToString(f.params[i].type);
  }
  s += ")";
  if (f.return_type) s += " -> " + TypeNodeToString(f.return_type);
  return s;
}

std::vector<DocEntry> Extract(const polyglot::ploy::Module &mod) {
  using namespace polyglot::ploy;
  std::vector<DocEntry> out;
  for (const auto &stmt : mod.declarations) {
    if (auto f = std::dynamic_pointer_cast<FuncDecl>(stmt); f && !f->doc_comment.empty()) {
      out.push_back({"func", f->name, FuncSignature(*f), f->doc_comment});
    } else if (auto s = std::dynamic_pointer_cast<StructDecl>(stmt);
               s && !s->doc_comment.empty()) {
      out.push_back({"struct", s->name, "STRUCT " + s->name, s->doc_comment});
    } else if (auto v = std::dynamic_pointer_cast<VarDecl>(stmt);
               v && !v->doc_comment.empty()) {
      std::string sig = (v->is_mutable ? "VAR " : "LET ") + v->name;
      if (v->type) sig += ": " + TypeNodeToString(v->type);
      out.push_back({v->is_mutable ? "var" : "let", v->name, sig, v->doc_comment});
    }
  }
  return out;
}

std::string RenderMarkdown(const std::string &file, const std::vector<DocEntry> &entries) {
  std::ostringstream os;
  os << "# " << file << "\n\n";
  if (entries.empty()) {
    os << "_No `///` documentation found._\n";
    return os.str();
  }
  for (const auto &e : entries) {
    os << "## `" << e.signature << "`\n\n";
    for (const auto &line : e.doc) os << line << "\n";
    os << "\n";
  }
  return os.str();
}

// Minimal JSON escape (handles the subset that doc text actually contains).
std::string JsonEscape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\u%04x", c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

std::string RenderJson(const std::string &file, const std::vector<DocEntry> &entries) {
  std::ostringstream os;
  os << "{\n  \"file\": \"" << JsonEscape(file) << "\",\n  \"entries\": [";
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto &e = entries[i];
    os << (i ? ",\n    {" : "\n    {");
    os << "\"kind\":\"" << e.kind << "\",";
    os << "\"name\":\"" << JsonEscape(e.name) << "\",";
    os << "\"signature\":\"" << JsonEscape(e.signature) << "\",";
    os << "\"doc\":[";
    for (size_t j = 0; j < e.doc.size(); ++j) {
      if (j) os << ",";
      os << "\"" << JsonEscape(e.doc[j]) << "\"";
    }
    os << "]}";
  }
  os << (entries.empty() ? "]" : "\n  ]");
  os << "\n}\n";
  return os.str();
}

bool ReadFile(const std::string &path, std::string &out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

void PrintUsage() {
  std::cerr << "polydoc — extract `///` doc comments from .ploy sources (since v1.18.0)\n"
               "Usage: polydoc [--json] [-o OUT] FILE [FILE ...]\n";
}

} // namespace

int main(int argc, char **argv) {
  bool emit_json = false;
  std::string out_path;
  std::vector<std::string> files;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-h" || a == "--help") { PrintUsage(); return 0; }
    if (a == "--json") { emit_json = true; continue; }
    if (a == "-o") {
      if (i + 1 >= argc) { PrintUsage(); return 2; }
      out_path = argv[++i];
      continue;
    }
    files.push_back(a);
  }
  if (files.empty()) { PrintUsage(); return 2; }

  std::ostringstream all;
  for (const auto &path : files) {
    std::string source;
    if (!ReadFile(path, source)) {
      std::cerr << "polydoc: cannot read '" << path << "'\n";
      return 1;
    }
    polyglot::frontends::Diagnostics diags;
    polyglot::ploy::PloyLexer lexer(source, path);
    polyglot::ploy::PloyParser parser(lexer, diags);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    auto entries = Extract(*mod);
    if (emit_json) {
      all << RenderJson(path, entries);
    } else {
      all << RenderMarkdown(path, entries);
      all << "\n";
    }
  }

  if (out_path.empty()) {
    std::cout << all.str();
  } else {
    std::ofstream of(out_path);
    if (!of) { std::cerr << "polydoc: cannot write '" << out_path << "'\n"; return 1; }
    of << all.str();
  }
  return 0;
}
