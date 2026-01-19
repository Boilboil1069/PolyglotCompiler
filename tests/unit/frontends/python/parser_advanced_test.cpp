#include <catch2/catch_test_macros.hpp>

#include "frontends/python/include/python_lexer.h"
#include "frontends/python/include/python_parser.h"

using polyglot::frontends::Diagnostics;
using polyglot::python::PythonLexer;
using polyglot::python::PythonParser;
using namespace polyglot::python;

TEST_CASE("Python parser parses lambda and annotated assignment", "[python][parser][lambda][annotation]") {
  const char *src = R"(value: int = (lambda x, y: x + y)(1, 2))";
  Diagnostics diag;
  PythonLexer lexer(src, "<mem>", &diag);
  PythonParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->body.size() == 1);
  auto assign = std::dynamic_pointer_cast<Assignment>(mod->body[0]);
  REQUIRE(assign);
  REQUIRE(assign->annotation);
  auto ann_ident = std::dynamic_pointer_cast<Identifier>(assign->annotation);
  REQUIRE(ann_ident);
  REQUIRE(ann_ident->name == "int");
  auto call = std::dynamic_pointer_cast<CallExpression>(assign->value);
  REQUIRE(call);
  auto lam = std::dynamic_pointer_cast<LambdaExpression>(call->callee);
  REQUIRE(lam);
  REQUIRE(lam->params.size() == 2);
  REQUIRE(lam->body);
}

TEST_CASE("Python parser parses class, imports, and annotated functions", "[python][parser][class][import][annotation]") {
  const char *src = R"(
import math as m
from pkg import tool as t

class Point(Base):
    def __init__(self, x: int, y: int):
        self.x = x
        self.y = y

def add(a: int, b: int) -> int:
    return a + b
)";
  Diagnostics diag;
  PythonLexer lexer(src, "<mem>", &diag);
  PythonParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->body.size() == 4);

  auto imp1 = std::dynamic_pointer_cast<ImportStatement>(mod->body[0]);
  REQUIRE(imp1);
  REQUIRE_FALSE(imp1->is_from);
  REQUIRE(imp1->name == "math");
  REQUIRE(imp1->alias == "m");

  auto imp2 = std::dynamic_pointer_cast<ImportStatement>(mod->body[1]);
  REQUIRE(imp2);
  REQUIRE(imp2->is_from);
  REQUIRE(imp2->module == "pkg");
  REQUIRE(imp2->name == "tool");
  REQUIRE(imp2->alias == "t");

  auto cls = std::dynamic_pointer_cast<ClassDef>(mod->body[2]);
  REQUIRE(cls);
  REQUIRE(cls->name == "Point");
  REQUIRE(cls->bases.size() == 1);
  auto base_ident = std::dynamic_pointer_cast<Identifier>(cls->bases[0]);
  REQUIRE(base_ident);
  REQUIRE(base_ident->name == "Base");
  REQUIRE(cls->body.size() == 1);
  auto init_fn = std::dynamic_pointer_cast<FunctionDef>(cls->body[0]);
  REQUIRE(init_fn);
  REQUIRE(init_fn->params.size() == 3);
  REQUIRE(init_fn->params[1].annotation != nullptr);
  REQUIRE(init_fn->params[2].annotation != nullptr);
  REQUIRE(init_fn->body.size() == 2);

  auto add_fn = std::dynamic_pointer_cast<FunctionDef>(mod->body[3]);
  REQUIRE(add_fn);
  REQUIRE(add_fn->return_annotation != nullptr);
  REQUIRE(add_fn->params.size() == 2);
}
