#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstring>
#include <limits>
#include "backends/arm64/include/arm64_target.h"
#include "middle/include/ir/ir_builder.h"
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
#include <sys/mman.h>
#include <unistd.h>
#endif

using namespace polyglot::ir;
using polyglot::backends::arm64::Arm64Target;
using polyglot::backends::arm64::RegAllocStrategy;

namespace {
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
struct Executable {
  void *memory{MAP_FAILED};
  std::size_t size{};
  Arm64Target::MCResult code;
  explicit Executable(IRContext &context, RegAllocStrategy strategy) {
    Arm64Target target(&context); target.SetRegAllocStrategy(strategy); code = target.EmitObjectCode();
    REQUIRE(code.relocs.empty());
    auto text = std::find_if(code.sections.begin(), code.sections.end(), [](const auto &s) { return s.name == ".text"; });
    REQUIRE(text != code.sections.end());
    size = (text->data.size() + 16383) & ~std::size_t(16383);
    memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    REQUIRE(memory != MAP_FAILED);
    std::memcpy(memory, text->data.data(), text->data.size());
    __builtin___clear_cache(static_cast<char *>(memory), static_cast<char *>(memory) + size);
    REQUIRE(mprotect(memory, size, PROT_READ | PROT_EXEC) == 0);
  }
  ~Executable() { if (memory != MAP_FAILED) munmap(memory, size); }
  template<class T> T Function(const std::string &name) {
    auto symbol = std::find_if(code.symbols.begin(), code.symbols.end(), [&](const auto &s) { return s.name == name; });
    REQUIRE(symbol != code.symbols.end());
    return reinterpret_cast<T>(static_cast<char *>(memory) + symbol->value);
  }
};
template<class Work> std::string Capture(Work work) {
  int descriptors[2]; REQUIRE(pipe(descriptors) == 0);
  int previous = dup(STDOUT_FILENO); REQUIRE(previous >= 0);
  REQUIRE(dup2(descriptors[1], STDOUT_FILENO) >= 0); close(descriptors[1]);
  work();
  REQUIRE(dup2(previous, STDOUT_FILENO) >= 0); close(previous);
  std::string output; char buffer[256]; ssize_t count;
  while ((count = read(descriptors[0], buffer, sizeof(buffer))) > 0) output.append(buffer, count);
  close(descriptors[0]); return output;
}
#endif
}

TEST_CASE("ARM64 scalar allocation preserves spills across nested calls", "[backends][arm64][native]") {
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
  for (auto strategy : {RegAllocStrategy::kStack, RegAllocStrategy::kLinearScan, RegAllocStrategy::kGraphColoring}) {
    IRContext context; IRBuilder builder(context);
    auto fn = builder.CreateFunction("pressure", IRType::I64(), {{"x", IRType::I64()}, {"unused", IRType::I64()}});
    builder.SetCurrentFunction(fn); builder.SetInsertPoint(builder.CreateBlock("entry"));
    std::vector<std::string> live;
    for (int i = 1; i <= 24; ++i)
      live.push_back(builder.MakeBinary(BinaryInstruction::Op::kMul, "x", std::to_string(i), "live" + std::to_string(i))->name);
    auto result = builder.MakeCall("helper", {"x"}, IRType::I64(), "called")->name;
    for (const auto &value : live)
      result = builder.MakeBinary(BinaryInstruction::Op::kAdd, result, value, "sum")->name;
    builder.MakeReturn(result);
    auto helper = builder.CreateFunction("helper", IRType::I64(), {{"x", IRType::I64()}});
    builder.SetCurrentFunction(helper); builder.SetInsertPoint(builder.CreateBlock("entry"));
    auto doubled = builder.MakeBinary(BinaryInstruction::Op::kMul, "x", "2", "twice");
    builder.MakeReturn(doubled->name);
    Executable code(context, strategy);
    auto calculate = code.Function<std::int64_t (*)(std::int64_t, std::int64_t)>("pressure");
    CHECK(calculate(17, 999) == 5134);
    CHECK(calculate(-23, 123) == -6946);
  }
#else
  SUCCEED("native execution requires ARM64 POSIX");
#endif
}

TEST_CASE("ARM64 phi copies retain loop-carried values", "[backends][arm64][native]") {
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
  for (auto strategy : {RegAllocStrategy::kStack, RegAllocStrategy::kLinearScan, RegAllocStrategy::kGraphColoring}) {
    IRContext context; IRBuilder builder(context);
    auto fn = builder.CreateFunction("swap_loop", IRType::I64(), {{"n", IRType::I64()}});
    builder.SetCurrentFunction(fn);
    auto entry = builder.CreateBlock("entry"), loop = builder.CreateBlock("loop"), done = builder.CreateBlock("done");
    builder.SetInsertPoint(entry); builder.MakeBranch(loop.get());
    builder.SetInsertPoint(loop);
    auto a = builder.MakePhi(IRType::I64(), {{entry.get(), "13"}, {loop.get(), "b"}}, "a");
    auto b = builder.MakePhi(IRType::I64(), {{entry.get(), "29"}, {loop.get(), "a"}}, "b");
    auto i = builder.MakePhi(IRType::I64(), {{entry.get(), "0"}, {loop.get(), "next"}}, "i");
    builder.MakeBinary(BinaryInstruction::Op::kAdd, i->name, "1", "next");
    auto condition = builder.MakeBinary(BinaryInstruction::Op::kCmpSlt, "next", "n", "condition");
    builder.MakeCondBranch(condition->name, loop.get(), done.get());
    builder.SetInsertPoint(done); builder.MakeReturn(a->name);
    Executable code(context, strategy); auto run = code.Function<std::int64_t (*)(std::int64_t)>("swap_loop");
    CHECK(run(1) == 13); CHECK(run(2) == 29); CHECK(run(100) == 29); CHECK(run(101) == 13);
  }
#else
  SUCCEED("native execution requires ARM64 POSIX");
#endif
}

TEST_CASE("ARM64 numeric printing preserves signed boundaries and IEEE values", "[backends][arm64][native]") {
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
  IRContext context; IRBuilder builder(context);
  auto integer = builder.CreateFunction("integer", IRType::Void(), {{"x", IRType::I64()}});
  builder.SetCurrentFunction(integer); builder.SetInsertPoint(builder.CreateBlock("entry"));
  builder.MakeCall("polyrt_print_i64", {"x"}, IRType::Void()); builder.MakeReturn();
  auto floating = builder.CreateFunction("floating", IRType::Void(), {{"x", IRType::F64()}});
  builder.SetCurrentFunction(floating); builder.SetInsertPoint(builder.CreateBlock("entry"));
  builder.MakeCall("polyrt_print_f64", {"x"}, IRType::Void()); builder.MakeReturn();
  Executable code(context, RegAllocStrategy::kGraphColoring);
  auto print_integer = code.Function<void (*)(std::int64_t)>("integer");
  auto print_float = code.Function<void (*)(double)>("floating");
  auto text = Capture([&] {
    print_integer(std::numeric_limits<std::int64_t>::min()); print_integer(0);
    print_integer(std::numeric_limits<std::int64_t>::max());
    print_float(1.5); print_float(-0.0); print_float(std::numeric_limits<double>::denorm_min());
    print_float(std::numeric_limits<double>::infinity()); print_float(std::numeric_limits<double>::quiet_NaN());
  });
  CHECK(text == "-9223372036854775808\n0\n9223372036854775807\n0x1.8000000000000p+0\n-0x0.0000000000000p+0\n0x0.0000000000001p-1022\ninf\nnan\n");
#else
  SUCCEED("native execution requires ARM64 POSIX");
#endif
}
