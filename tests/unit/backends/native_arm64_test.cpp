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

TEST_CASE("ARM64 aggregate GEP follows pointer, aligned field and array indices",
          "[backends][arm64][native][gep]") {
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
  struct Record {
    std::uint8_t flag;
    std::int64_t total;
    std::int32_t items[3];
  };
  struct Records {
    std::uint8_t prefix;
    Record records[2];
  };
  for (auto strategy : {RegAllocStrategy::kStack, RegAllocStrategy::kLinearScan,
                        RegAllocStrategy::kGraphColoring}) {
    IRContext context;
    IRBuilder builder(context);
    auto record = IRType::Struct(
        "Record", {IRType::I8(false), IRType::I64(), IRType::Array(IRType::I32(), 3)});
    auto records = IRType::Struct("Records", {IRType::I8(false), IRType::Array(record, 2)});
    REQUIRE(context.Layout().SizeOf(record) == sizeof(Record));
    REQUIRE(context.Layout().SizeOf(records) == sizeof(Records));
    auto fn = builder.CreateFunction("update_record", IRType::I64(),
                                     {{"base", IRType::Pointer(records)}});
    builder.SetCurrentFunction(fn);
    builder.SetInsertPoint(builder.CreateBlock("entry"));
    auto total = builder.MakeGEP("base", records, {0, 1, 1, 1});
    auto item = builder.MakeGEP("base", IRType::Pointer(records), {0, 1, 1, 2, 1});
    auto flag = builder.MakeGEP("base", records, {0, 1, 1, 0});
    builder.MakeStore(total->name, "91");
    builder.MakeStore(item->name, "-7");
    builder.MakeStore(flag->name, "3");
    auto loaded_total = builder.MakeLoad(total->name, IRType::I64());
    auto loaded_item = builder.MakeLoad(item->name, IRType::I32());
    auto signed_item =
        builder.MakeCast(CastInstruction::CastKind::kSExt, loaded_item->name, IRType::I64());
    auto sum = builder.MakeBinary(BinaryInstruction::Op::kAdd, loaded_total->name,
                                  signed_item->name, "sum");
    builder.MakeReturn(sum->name);
    Executable code(context, strategy);
    auto update = code.Function<std::int64_t (*)(Records *)>("update_record");
    Records data{11, {{17, 19, {23, 29, 31}}, {37, 41, {43, 47, 53}}}};
    CHECK(update(&data) == 84);
    CHECK(data.prefix == 11);
    CHECK(data.records[0].total == 19);
    CHECK(data.records[1].flag == 3);
    CHECK(data.records[1].total == 91);
    CHECK(data.records[1].items[0] == 43);
    CHECK(data.records[1].items[1] == -7);
    CHECK(data.records[1].items[2] == 53);
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

TEST_CASE("ARM64 integer-to-float casts preserve signedness and IEEE rounding",
          "[backends][arm64][native][numeric-cast]") {
#if defined(__aarch64__) && (defined(__APPLE__) || defined(__linux__))
  for (auto strategy : {RegAllocStrategy::kStack, RegAllocStrategy::kLinearScan,
                        RegAllocStrategy::kGraphColoring}) {
    for (bool sign : {true, false})
      for (bool wide : {true, false}) {
        IRContext context;
        IRBuilder builder(context);
        auto source = wide ? IRType::I64() : IRType::I32();
        source.is_signed = sign;
        for (bool single : {true, false}) {
          auto destination = single ? IRType::F32() : IRType::F64();
          auto fn = builder.CreateFunction(single ? "to_float" : "to_double", destination,
                                           {{"input", source}});
          builder.SetCurrentFunction(fn);
          builder.SetInsertPoint(builder.CreateBlock("entry"));
          auto cast = builder.MakeCast(sign ? CastInstruction::CastKind::kSiToFp
                                            : CastInstruction::CastKind::kUiToFp,
                                       "input", destination, "converted");
          builder.MakeReturn(cast->name);
        }
        Executable code(context, strategy);
        auto to_float = code.Function<float (*)(std::uint64_t)>("to_float");
        auto to_double = code.Function<double (*)(std::uint64_t)>("to_double");
        for (std::uint64_t input :
             {0ULL, 1ULL, 16777217ULL, 9007199254740993ULL, 0x7fffffffULL, 0x80000000ULL,
              0xffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 0xffffffffffffffffULL}) {
          CAPTURE(sign, wide, input);
          if (sign) {
            const auto value = wide ? static_cast<std::int64_t>(input)
                                    : static_cast<std::int64_t>(static_cast<std::int32_t>(input));
            CHECK(to_float(input) == static_cast<float>(value));
            CHECK(to_double(input) == static_cast<double>(value));
          } else {
            const auto value =
                wide ? input : static_cast<std::uint64_t>(static_cast<std::uint32_t>(input));
            CHECK(to_float(input) == static_cast<float>(value));
            CHECK(to_double(input) == static_cast<double>(value));
          }
        }
      }
  }
#else
  SUCCEED("native execution requires ARM64 POSIX");
#endif
}
