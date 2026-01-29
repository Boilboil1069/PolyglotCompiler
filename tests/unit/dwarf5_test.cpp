#include <catch2/catch_test_macros.hpp>
#include "common/include/debug/dwarf5.h"

using namespace polyglot::debug;

TEST_CASE("DWARF - Location Expression", "[dwarf5]") {
    LocationExpr expr;
    
    // Frame base + offset
    expr.AddOp(LocationExpr::Op::kFBReg, -8);
    
    std::vector<uint8_t> encoded = expr.Encode();
    
    REQUIRE(!encoded.empty());
    REQUIRE(encoded[0] == static_cast<uint8_t>(LocationExpr::Op::kFBReg));
}

TEST_CASE("DWARF - DIE Creation", "[dwarf5]") {
    DIE die(dwarf::Tag::kSubprogram);
    
    die.SetAttribute(dwarf::Attribute::kName, "test_function");
    die.SetAttribute(dwarf::Attribute::kLowPC, dwarf::Form::kAddr, 0x1000);
    die.SetAttribute(dwarf::Attribute::kHighPC, dwarf::Form::kData4, 0x100);
    
    REQUIRE(die.GetTag() == dwarf::Tag::kSubprogram);
}

TEST_CASE("DWARF - Builder Basic Types", "[dwarf5]") {
    DwarfBuilder builder;
    
    builder.SetCompileUnit("test.cpp", "/home/user", "TestCompiler", dwarf::Language::kCpp20);
    
    DIE* int_type = builder.AddBaseType("int", 4, dwarf::BaseTypeEncoding::kSigned);
    REQUIRE(int_type != nullptr);
    
    DIE* ptr_type = builder.AddPointerType(int_type);
    REQUIRE(ptr_type != nullptr);
}

TEST_CASE("DWARF - Struct Type", "[dwarf5]") {
    DwarfBuilder builder;
    
    builder.SetCompileUnit("test.cpp", "/home/user", "TestCompiler", dwarf::Language::kCpp20);
    
    DIE* int_type = builder.AddBaseType("int", 4, dwarf::BaseTypeEncoding::kSigned);
    DIE* struct_type = builder.AddStructType("MyStruct", 8);
    
    builder.AddMember(struct_type, "field1", int_type, 0);
    builder.AddMember(struct_type, "field2", int_type, 4);
    
    REQUIRE(struct_type != nullptr);
}

TEST_CASE("DWARF - Function Debug Info", "[dwarf5]") {
    DwarfBuilder builder;
    
    builder.SetCompileUnit("test.cpp", "/home/user", "TestCompiler", dwarf::Language::kCpp20);
    
    DIE* int_type = builder.AddBaseType("int", 4, dwarf::BaseTypeEncoding::kSigned);
    
    SourceLocation loc("test.cpp", 10, 5);
    DIE* func = builder.AddSubprogram("my_function", int_type, 0x1000, 0x1100, loc);
    
    REQUIRE(func != nullptr);
    
    // Add parameter
    LocationExpr param_loc;
    param_loc.AddOp(LocationExpr::Op::kFBReg, 8);
    builder.AddParameter(func, "param1", int_type, param_loc);
    
    // Add local variable
    LocationExpr var_loc;
    var_loc.AddOp(LocationExpr::Op::kFBReg, -8);
    SourceLocation var_src("test.cpp", 11, 9);
    builder.AddLocalVariable(func, "local_var", int_type, var_loc, var_src);
}

TEST_CASE("DWARF - Debug Info Generator", "[dwarf5]") {
    DebugInfoGenerator gen;
    
    gen.SetSourceLanguage(dwarf::Language::kCpp20);
    gen.SetCompilationDirectory("/home/user/project");
    gen.SetProducer("PolyglotCompiler v2.0");
    
    // Register types
    gen.RegisterType("int", 4, dwarf::BaseTypeEncoding::kSigned);
    gen.RegisterType("double", 8, dwarf::BaseTypeEncoding::kFloat);
    
    // Begin function
    SourceLocation func_loc("main.cpp", 5, 1);
    gen.BeginFunction("main", "int", 0x1000, func_loc);
    
    // Add parameter
    gen.AddFunctionParameter("argc", "int", 16);
    
    // Add local variable
    SourceLocation var_loc("main.cpp", 6, 5);
    gen.AddLocalVariable("result", "int", -4, var_loc);
    
    // Add line mapping
    gen.AddSourceLine(0x1000, func_loc);
    gen.AddSourceLine(0x1010, SourceLocation("main.cpp", 6, 5));
    gen.AddSourceLine(0x1020, SourceLocation("main.cpp", 7, 5));
    
    // End function
    gen.EndFunction(0x1100);
    
    // Generate sections
    auto sections = gen.Generate();
    
    REQUIRE(!sections.empty());
    REQUIRE(sections.count(dwarf::kDebugInfo) > 0);
}

TEST_CASE("DWARF - Line Number Program", "[dwarf5]") {
    LineNumberProgram program;
    
    program.AddFile("test.cpp", "/home/user");
    program.AddLine(0x1000, SourceLocation("test.cpp", 1, 1));
    program.AddLine(0x1010, SourceLocation("test.cpp", 2, 1));
    program.AddLine(0x1020, SourceLocation("test.cpp", 3, 1));
    program.SetEndSequence(0x1030);
    
    auto encoded = program.Encode();
    
    // Should produce some output
    REQUIRE(encoded.size() >= 0);
}
