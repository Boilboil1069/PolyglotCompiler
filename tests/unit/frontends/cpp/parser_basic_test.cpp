#include <catch2/catch_test_macros.hpp>
#include <string>
#include <unordered_set>
#include <vector>

#include "frontends/cpp/include/cpp_lexer.h"
#include "frontends/cpp/include/cpp_frontend.h"
#include "frontends/cpp/include/cpp_lowering.h"
#include "frontends/cpp/include/cpp_parser.h"
#include "middle/include/ir/ir_context.h"
#include "middle/include/ir/verifier.h"

using polyglot::frontends::Diagnostics;
using polyglot::cpp::CppLexer;
using polyglot::cpp::CppParser;
using namespace polyglot::cpp;

namespace {

polyglot::ir::IRContext ParseAndLowerCpp(const std::string &source, Diagnostics &diag) {
  CppLexer lexer(source, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto module = parser.TakeModule();

  polyglot::ir::IRContext ctx;
  if (module && !diag.HasErrors())
    LowerToIR(*module, ctx, diag);
  return ctx;
}

} // namespace

TEST_CASE("C++ parser parses function and return", "[cpp][parser]") {
  const char *src = "int add(int a, int b) { return a + b; }";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);
  auto fn = std::dynamic_pointer_cast<FunctionDecl>(mod->declarations[0]);
  REQUIRE(fn);
  REQUIRE(fn->params.size() == 2);
}

TEST_CASE("C++ parser parses if/while/for", "[cpp][parser]") {
  const char *src = R"(
int main() {
  int x = 0;
  if (x) { x = 1; } else { x = 2; }
  while (x) { x = x - 1; }
  for (x = 0; x < 3; x = x + 1) { x = x + 2; }
}
)";
  Diagnostics diag;
  CppLexer lexer(src, "<mem>");
  CppParser parser(lexer, diag);
  parser.ParseModule();
  auto mod = parser.TakeModule();
  REQUIRE(mod);
  REQUIRE(mod->declarations.size() == 1);
}

TEST_CASE("C++ lowering emits valid if control flow", "[cpp][lowering][control]") {
  SECTION("if without else keeps the merge path active") {
    Diagnostics diag;
    auto ctx = ParseAndLowerCpp(R"(
int choose(int flag) {
  if (flag) { return 11; }
  return 22;
}
)", diag);

    REQUIRE_FALSE(diag.HasErrors());
    std::string verify_message;
    const bool valid = polyglot::ir::Verify(ctx, &verify_message);
    INFO(verify_message);
    REQUIRE(valid);
  }

  SECTION("if with two terminating branches seals the unreachable merge") {
    Diagnostics diag;
    auto ctx = ParseAndLowerCpp(R"(
int choose(int flag) {
  if (flag) { return 11; } else { return 22; }
}
)", diag);

    REQUIRE_FALSE(diag.HasErrors());
    std::string verify_message;
    const bool valid = polyglot::ir::Verify(ctx, &verify_message);
    INFO(verify_message);
    REQUIRE(valid);
  }
}

TEST_CASE("C++ lowering keeps business integer constants as immediates",
          "[cpp][lowering][literal][control]") {
  Diagnostics diag;
  auto ctx = ParseAndLowerCpp(R"(
int pricing(int unit_price, int quantity) {
  if (unit_price <= 0) { return 0; }
  if (quantity >= 8LL) { return 24; }
  return unit_price * quantity + 15;
}
)", diag);

  REQUIRE_FALSE(diag.HasErrors());
  auto *fn = ctx.FindFunction("pricing");
  REQUIRE(fn != nullptr);

  bool saw_zero_compare = false;
  bool saw_eight_compare = false;
  bool saw_fifteen_add = false;
  std::unordered_set<std::string> definitions;
  for (const auto &block : fn->blocks) {
    for (const auto &instruction : block->instructions) {
      if (!instruction->name.empty())
        REQUIRE(definitions.insert(instruction->name).second);
      auto binary = std::dynamic_pointer_cast<polyglot::ir::BinaryInstruction>(instruction);
      if (!binary)
        continue;
      REQUIRE(binary->operands.size() == 2);
      for (const auto &operand : binary->operands)
        REQUIRE(operand.rfind("cf", 0) != 0);
      if (binary->op == polyglot::ir::BinaryInstruction::Op::kCmpSle &&
          binary->operands[1] == "0") {
        REQUIRE(binary->type.kind == polyglot::ir::IRTypeKind::kI1);
        saw_zero_compare = true;
      }
      if (binary->op == polyglot::ir::BinaryInstruction::Op::kCmpSge &&
          binary->operands[1] == "8") {
        REQUIRE(binary->type.kind == polyglot::ir::IRTypeKind::kI1);
        saw_eight_compare = true;
      }
      if (binary->op == polyglot::ir::BinaryInstruction::Op::kAdd &&
          binary->operands[1] == "15") {
        saw_fifteen_add = true;
      }
    }
    if (block->terminator) {
      for (const auto &operand : block->terminator->operands)
        REQUIRE(operand.rfind("cf", 0) != 0);
    }
  }

  REQUIRE(saw_zero_compare);
  REQUIRE(saw_eight_compare);
  REQUIRE(saw_fifteen_add);
  std::string verify_message;
  const bool valid = polyglot::ir::Verify(ctx, &verify_message);
  INFO(verify_message);
  REQUIRE(valid);
}

TEST_CASE("C++ frontend lowers a stateful four-field pricing session lifecycle",
          "[cpp][frontend][lowering][oop][lifecycle]") {
  const char *source = R"(
class OrderPricingSession {
public:
  OrderPricingSession(int unit_price, int quantity) {
    this->unit_price_ = unit_price;
    this->quantity_ = quantity;
    this->subtotal_ = unit_price * quantity;
    this->discount_ = 0;
  }

  ~OrderPricingSession() {
    this->unit_price_ = 0;
    this->quantity_ = 0;
    this->subtotal_ = 0;
    this->discount_ = 0;
  }

  int subtotal() { return this->subtotal_; }
  int apply_discount(int loyalty_tier) {
    this->discount_ += loyalty_tier * 2;
    return this->discount_;
  }
  int payable(int shipping_fee) {
    return this->subtotal_ - this->discount_ + shipping_fee;
  }
  int lifecycle_checksum() {
    return this->unit_price_ + this->quantity_ + this->subtotal_ + this->discount_;
  }

private:
  int unit_price_;
  int quantity_;
  int subtotal_;
  int discount_;
};

int pricing_session_payable(int unit_price, int quantity, int shipping_fee) {
  OrderPricingSession session(unit_price, quantity);
  int subtotal = session.subtotal();
  int discount = session.apply_discount(2);
  int payable = session.payable(shipping_fee);
  int lifecycle = session.lifecycle_checksum();
  return payable + lifecycle - lifecycle + subtotal - subtotal + discount - discount;
}
)";

  Diagnostics diagnostics;
  polyglot::ir::IRContext context;
  polyglot::frontends::FrontendOptions options;
  CppLanguageFrontend frontend;
  const auto result =
      frontend.Lower(source, "<pricing-session.cpp>", context, diagnostics, options);
  for (const auto &entry : diagnostics.All())
    UNSCOPED_INFO(Diagnostics::Format(entry));
  REQUIRE(result.success);
  REQUIRE_FALSE(diagnostics.HasErrors());

  std::string verify_message;
  const bool valid = polyglot::ir::Verify(context, &verify_message);
  INFO(verify_message);
  REQUIRE(valid);

  auto *constructor = context.FindFunction("OrderPricingSession::OrderPricingSession");
  auto *destructor = context.FindFunction("OrderPricingSession::~OrderPricingSession");
  auto *subtotal_method = context.FindFunction("OrderPricingSession::subtotal");
  auto *discount_method = context.FindFunction("OrderPricingSession::apply_discount");
  auto *payable_method = context.FindFunction("OrderPricingSession::payable");
  auto *lifecycle_method = context.FindFunction("OrderPricingSession::lifecycle_checksum");
  auto *wrapper = context.FindFunction("pricing_session_payable");
  REQUIRE(constructor);
  REQUIRE(destructor);
  REQUIRE(subtotal_method);
  REQUIRE(discount_method);
  REQUIRE(payable_method);
  REQUIRE(lifecycle_method);
  REQUIRE(wrapper);

  REQUIRE(constructor->param_types.size() == 3);
  const auto &this_type = constructor->param_types.front();
  REQUIRE(this_type.kind == polyglot::ir::IRTypeKind::kPointer);
  REQUIRE(this_type.subtypes.size() == 1);
  REQUIRE(this_type.subtypes.front().kind == polyglot::ir::IRTypeKind::kStruct);
  REQUIRE(this_type.subtypes.front().subtypes.size() == 4);

  auto field_store_count = [](const polyglot::ir::Function &function) {
    std::unordered_set<std::string> field_addresses;
    for (const auto &block : function.blocks)
      for (const auto &instruction : block->instructions)
        if (auto gep =
                std::dynamic_pointer_cast<polyglot::ir::GetElementPtrInstruction>(instruction))
          field_addresses.insert(gep->name);
    std::size_t count = 0;
    for (const auto &block : function.blocks)
      for (const auto &instruction : block->instructions)
        if (auto store = std::dynamic_pointer_cast<polyglot::ir::StoreInstruction>(instruction))
          if (!store->operands.empty() && field_addresses.count(store->operands.front()))
            ++count;
    return count;
  };
  REQUIRE(field_store_count(*constructor) == 4);
  REQUIRE(field_store_count(*destructor) == 4);
  REQUIRE(field_store_count(*discount_method) == 1);

  std::vector<std::string> wrapper_calls;
  std::size_t aggregate_allocas = 0;
  for (const auto &block : wrapper->blocks) {
    for (const auto &instruction : block->instructions) {
      if (auto alloca =
              std::dynamic_pointer_cast<polyglot::ir::AllocaInstruction>(instruction)) {
        REQUIRE(alloca->type.kind == polyglot::ir::IRTypeKind::kPointer);
        if (alloca->type.subtypes.front().kind == polyglot::ir::IRTypeKind::kStruct) {
          REQUIRE(alloca->type.subtypes.front().subtypes.size() == 4);
          ++aggregate_allocas;
        }
      }
      if (auto call = std::dynamic_pointer_cast<polyglot::ir::CallInstruction>(instruction))
        wrapper_calls.push_back(call->callee);
    }
  }
  REQUIRE(aggregate_allocas == 1);
  REQUIRE(wrapper_calls ==
          std::vector<std::string>{"OrderPricingSession::OrderPricingSession",
                                   "OrderPricingSession::subtotal",
                                   "OrderPricingSession::apply_discount",
                                   "OrderPricingSession::payable",
                                   "OrderPricingSession::lifecycle_checksum",
                                   "OrderPricingSession::~OrderPricingSession"});

  // The return value is computed first, then the destructor call is emitted,
  // and only then does the block return the preserved value.
  REQUIRE_FALSE(wrapper->blocks.empty());
  const auto &entry = wrapper->blocks.front();
  REQUIRE_FALSE(entry->instructions.empty());
  auto last_call =
      std::dynamic_pointer_cast<polyglot::ir::CallInstruction>(entry->instructions.back());
  REQUIRE(last_call);
  REQUIRE(last_call->callee == "OrderPricingSession::~OrderPricingSession");
  REQUIRE(std::dynamic_pointer_cast<polyglot::ir::ReturnStatement>(entry->terminator));
}

TEST_CASE("C++ scalar parameter mutation and nested logic form valid SSA control flow",
          "[cpp][lowering][control][shortcircuit]") {
  Diagnostics diag;
  auto ctx = ParseAndLowerCpp(R"(
    bool classify(int value) {
      value = value + 1;
      return (value > 0 && (value < 4 || value == 7)) || value == 9;
    }
    int negate(int value) { value = -value; return value; }
    bool invert(int value) { return !value; }
  )", diag);
  for (const auto &diagnostic : diag.All()) UNSCOPED_INFO(Diagnostics::Format(diagnostic));
  REQUIRE_FALSE(diag.HasErrors());
  std::string error;
  const bool valid = polyglot::ir::Verify(ctx, &error);
  INFO(error); CHECK(valid);
  size_t phi_count = 0;
  for (const auto &fn : ctx.Functions())
    for (const auto &block : fn->blocks) phi_count += block->phis.size();
  CHECK(phi_count == 3);
}

TEST_CASE("C++ native text literals preserve escaped and raw bytes", "[cpp][lowering][native-text]") {
  Diagnostics diagnostics;
  auto context = ParseAndLowerCpp(R"cpp(
    int main() {
      print_text("line\n");
      print_text(R"(raw\n)");
      return 0;
    }
  )cpp", diagnostics);
  for (const auto &diagnostic : diagnostics.All()) UNSCOPED_INFO(Diagnostics::Format(diagnostic));
  REQUIRE_FALSE(diagnostics.HasErrors());
  bool escaped = false, raw = false;
  for (const auto &global : context.Globals()) {
    if (auto text = std::dynamic_pointer_cast<polyglot::ir::ConstantString>(global->initializer)) {
      escaped |= text->data == "line\n";
      raw |= text->data == "raw\\n";
    }
  }
  CHECK(escaped); CHECK(raw);
}
