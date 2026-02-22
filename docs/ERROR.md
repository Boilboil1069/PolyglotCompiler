[main] 正在生成文件夹: /home/PolyglotCompiler/build 
[build] 正在启动生成
[proc] 正在执行命令: /usr/bin/cmake --build /home/PolyglotCompiler/build --config Debug --target all --
[build] [18/306   0% :: 1.230] Building CXX object CMakeFiles/frontend_common.dir/frontends/common/src/token_pool.cpp.o
[build] [19/306   0% :: 2.330] Building CXX object CMakeFiles/frontend_cpp.dir/frontends/cpp/src/lexer/lexer.cpp.o
[build] [20/306   0% :: 2.406] Building CXX object CMakeFiles/polyglot_common.dir/backends/common/src/dwarf_builder.cpp.o
[build] [21/306   1% :: 2.474] Building CXX object CMakeFiles/frontend_python.dir/frontends/python/src/lexer/lexer.cpp.o
[build] [22/306   1% :: 2.649] Building CXX object CMakeFiles/frontend_rust.dir/frontends/rust/src/lexer/lexer.cpp.o
[build] /home/PolyglotCompiler/frontends/rust/src/lexer/lexer.cpp:240:18: warning: multi-character character constant [-Wmultichar]
[build]   240 |         if (c == '\\\\') {
[build]       |                  ^~~~~~
[build] /home/PolyglotCompiler/frontends/rust/src/lexer/lexer.cpp: In member function ‘polyglot::frontends::Token polyglot::rust::RustLexer::LexString()’:
[build] /home/PolyglotCompiler/frontends/rust/src/lexer/lexer.cpp:178:10: warning: variable ‘byte_prefix’ set but not used [-Wunused-but-set-variable]
[build]   178 |     bool byte_prefix = false;
[build]       |          ^~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/rust/src/lexer/lexer.cpp: In member function ‘polyglot::frontends::Token polyglot::rust::RustLexer::LexChar()’:
[build] /home/PolyglotCompiler/frontends/rust/src/lexer/lexer.cpp:240:15: warning: comparison is always false due to limited range of data type [-Wtype-limits]
[build]   240 |         if (c == '\\\\') {
[build]       |             ~~^~~~~~~~~
[build] [23/306   1% :: 2.680] Building CXX object CMakeFiles/polyglot_common.dir/common/src/core/symbol_table.cpp.o
[build] In file included from /home/PolyglotCompiler/common/include/core/symbols.h:11,
[build]                  from /home/PolyglotCompiler/common/src/core/symbol_table.cpp:14:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] [24/306   2% :: 3.198] Building CXX object CMakeFiles/polyglot_common.dir/backends/common/src/object_file.cpp.o
[build] /home/PolyglotCompiler/backends/common/src/object_file.cpp: In member function ‘virtual std::vector<unsigned char> polyglot::backends::MachOBuilder::Build()’:
[build] /home/PolyglotCompiler/backends/common/src/object_file.cpp:361:19: warning: unused variable ‘reloc_area_offset’ [-Wunused-variable]
[build]   361 |     std::uint32_t reloc_area_offset = current_offset;
[build]       |                   ^~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/backends/common/src/object_file.cpp:389:19: warning: unused variable ‘symtab_size’ [-Wunused-variable]
[build]   389 |     std::uint32_t symtab_size = static_cast<std::uint32_t>(symbols_.size()) * 16;
[build]       |                   ^~~~~~~~~~~
[build] [25/306   2% :: 3.325] Building CXX object CMakeFiles/polyglot_common.dir/common/src/debug/dwarf5.cpp.o
[build] /home/PolyglotCompiler/common/src/debug/dwarf5.cpp: In member function ‘void polyglot::debug::LineNumberProgram::EncodeSLEB128(std::vector<unsigned char>&, int64_t) const’:
[build] /home/PolyglotCompiler/common/src/debug/dwarf5.cpp:292:10: warning: unused variable ‘negative’ [-Wunused-variable]
[build]   292 |     bool negative = value < 0;
[build]       |          ^~~~~~~~
[build] [26/306   2% :: 3.466] Building CXX object CMakeFiles/frontend_cpp.dir/frontends/cpp/src/constexpr/cpp_constexpr.cpp.o
[build] /home/PolyglotCompiler/frontends/cpp/src/constexpr/cpp_constexpr.cpp: In member function ‘polyglot::cpp::ConstexprValue polyglot::cpp::ConstexprEvaluator::EvaluateCallExpr(polyglot::cpp::CallExpression*)’:
[build] /home/PolyglotCompiler/frontends/cpp/src/constexpr/cpp_constexpr.cpp:284:15: warning: unused variable ‘id’ [-Wunused-variable]
[build]   284 |     if (auto* id = dynamic_cast<Identifier*>(expr->callee.get())) {
[build]       |               ^~
[build] /home/PolyglotCompiler/frontends/cpp/src/constexpr/cpp_constexpr.cpp: In member function ‘bool polyglot::cpp::ConstexprChecker::IsConstexprExpr(polyglot::cpp::Expression*) const’:
[build] /home/PolyglotCompiler/frontends/cpp/src/constexpr/cpp_constexpr.cpp:742:15: warning: unused variable ‘sizeof_expr’ [-Wunused-variable]
[build]   742 |     if (auto* sizeof_expr = dynamic_cast<SizeofExpression*>(expr)) {
[build]       |               ^~~~~~~~~~~
[build] [27/306   3% :: 3.729] Building CXX object CMakeFiles/polyglot_common.dir/common/src/core/type_system.cpp.o
[build] In file included from /home/PolyglotCompiler/common/src/core/type_system.cpp:13:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] [28/306   3% :: 3.911] Building CXX object CMakeFiles/frontend_ploy.dir/frontends/ploy/src/lexer/lexer.cpp.o
[build] [29/306   3% :: 4.277] Building CXX object CMakeFiles/frontend_ploy.dir/frontends/ploy/src/sema/command_runner.cpp.o
[build] [30/306   4% :: 5.176] Building CXX object CMakeFiles/frontend_common.dir/frontends/common/src/preprocessor.cpp.o
[build] [31/306   4% :: 5.409] Building CXX object CMakeFiles/polyglot_common.dir/backends/common/src/debug_emitter.cpp.o
[build] /home/PolyglotCompiler/backends/common/src/debug_emitter.cpp: In member function ‘void polyglot::backends::{anonymous}::DwarfSectionBuilder::EncodeLineStatements(std::vector<unsigned char>&, const std::vector<polyglot::backends::DebugLineInfo>&)’:
[build] /home/PolyglotCompiler/backends/common/src/debug_emitter.cpp:775:10: warning: unused variable ‘is_stmt’ [-Wunused-variable]
[build]   775 |     bool is_stmt = true;
[build]       |          ^~~~~~~
[build] [32/306   4% :: 5.578] Building CXX object CMakeFiles/frontend_cpp.dir/frontends/cpp/src/lowering/lowering.cpp.o
[build] FAILED: CMakeFiles/frontend_cpp.dir/frontends/cpp/src/lowering/lowering.cpp.o 
[build] /usr/bin/g++ -DFMT_HEADER_ONLY=1 -I/home/PolyglotCompiler -I/home/PolyglotCompiler/build/_deps/fmt-src/include -I/home/PolyglotCompiler/build/_deps/nlohmann_json-src/include -g -std=gnu++20 -Wall -Wextra -Wpedantic -Wno-unused-parameter -MD -MT CMakeFiles/frontend_cpp.dir/frontends/cpp/src/lowering/lowering.cpp.o -MF CMakeFiles/frontend_cpp.dir/frontends/cpp/src/lowering/lowering.cpp.o.d -o CMakeFiles/frontend_cpp.dir/frontends/cpp/src/lowering/lowering.cpp.o -c /home/PolyglotCompiler/frontends/cpp/src/lowering/lowering.cpp
[build] In file included from /home/PolyglotCompiler/middle/include/ir/nodes/expressions.h:7,
[build]                  from /home/PolyglotCompiler/middle/include/ir/nodes/statements.h:8,
[build]                  from /home/PolyglotCompiler/middle/include/ir/cfg.h:9,
[build]                  from /home/PolyglotCompiler/middle/include/ir/ir_context.h:8,
[build]                  from /home/PolyglotCompiler/frontends/cpp/include/cpp_lowering.h:5,
[build]                  from /home/PolyglotCompiler/frontends/cpp/src/lowering/lowering.cpp:1:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Invalid()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:35:74: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    35 |   static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I1()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:36:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    36 |   static IRType I1() { IRType t{IRTypeKind::kI1, "i1"}; t.is_signed = false; return t; }
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I8(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:37:94: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    37 |   static IRType I8(bool is_signed = true) { IRType t{IRTypeKind::kI8, is_signed ? "i8" : "u8"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I16(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:38:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    38 |   static IRType I16(bool is_signed = true) { IRType t{IRTypeKind::kI16, is_signed ? "i16" : "u16"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I32(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:39:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    39 |   static IRType I32(bool is_signed = true) { IRType t{IRTypeKind::kI32, is_signed ? "i32" : "u32"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I64(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:40:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    40 |   static IRType I64(bool is_signed = true) { IRType t{IRTypeKind::kI64, is_signed ? "i64" : "u64"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F32()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:41:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    41 |   static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F64()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:42:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    42 |   static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Void()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:43:65: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    43 |   static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Pointer(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:46:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    46 |     IRType t{IRTypeKind::kPointer, pointee.name + "*"};
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Reference(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:52:56: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    52 |     IRType t{IRTypeKind::kReference, pointee.name + "&"};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Array(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:58:75: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    58 |     IRType t{IRTypeKind::kArray, elem.name + "[" + std::to_string(n) + "]"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Vector(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:65:88: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    65 |     IRType t{IRTypeKind::kVector, "<" + std::to_string(lanes) + " x " + elem.name + ">"};
[build]       |                                                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Struct(std::string, std::vector<polyglot::ir::IRType>)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:72:50: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    72 |     IRType t{IRTypeKind::kStruct, std::move(name)};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Function(const polyglot::ir::IRType&, const std::vector<polyglot::ir::IRType>&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:79:41: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    79 |     IRType t{IRTypeKind::kFunction, "fn"};
[build]       |                                         ^
[build] In file included from /home/PolyglotCompiler/frontends/cpp/src/lowering/lowering.cpp:8:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] In file included from /home/PolyglotCompiler/frontends/cpp/src/lowering/lowering.cpp:11:
[build] /home/PolyglotCompiler/middle/include/ir/class_metadata.h: In member function ‘bool polyglot::ir::ClassLayout::IsVirtualBase(const std::string&) const’:
[build] /home/PolyglotCompiler/middle/include/ir/class_metadata.h:87:25: error: no matching function for call to ‘find(std::vector<std::__cxx11::basic_string<char> >::const_iterator, std::vector<std::__cxx11::basic_string<char> >::const_iterator, const std::string&)’
[build]    87 |         return std::find(virtual_bases.begin(), virtual_bases.end(), base_name)
[build]       |                ~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
[build] In file included from /usr/include/c++/13/bits/locale_facets.h:48,
[build]                  from /usr/include/c++/13/bits/basic_ios.h:37,
[build]                  from /usr/include/c++/13/ios:46,
[build]                  from /usr/include/c++/13/ostream:40,
[build]                  from /usr/include/c++/13/bits/unique_ptr.h:42,
[build]                  from /usr/include/c++/13/memory:78,
[build]                  from /home/PolyglotCompiler/frontends/cpp/include/cpp_ast.h:3,
[build]                  from /home/PolyglotCompiler/frontends/cpp/include/cpp_lowering.h:3:
[build] /usr/include/c++/13/bits/streambuf_iterator.h:435:5: note: candidate: ‘template<class _CharT2> typename __gnu_cxx::__enable_if<std::__is_char<_CharT2>::__value, std::istreambuf_iterator<_CharT, std::char_traits<_CharT> > >::__type std::find(istreambuf_iterator<_CharT, char_traits<_CharT> >, istreambuf_iterator<_CharT, char_traits<_CharT> >, const _CharT2&)’
[build]   435 |     find(istreambuf_iterator<_CharT> __first,
[build]       |     ^~~~
[build] /usr/include/c++/13/bits/streambuf_iterator.h:435:5: note:   template argument deduction/substitution failed:
[build] /home/PolyglotCompiler/middle/include/ir/class_metadata.h:87:25: note:   ‘__gnu_cxx::__normal_iterator<const std::__cxx11::basic_string<char>*, std::vector<std::__cxx11::basic_string<char> > >’ is not derived from ‘std::istreambuf_iterator<_CharT, std::char_traits<_CharT> >’
[build]    87 |         return std::find(virtual_bases.begin(), virtual_bases.end(), base_name)
[build]       |                ~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
[build] [32/306   5% :: 6.556] Building CXX object CMakeFiles/frontend_java.dir/frontends/java/src/lexer/lexer.cpp.o
[build] [32/306   5% :: 7.184] Building CXX object CMakeFiles/frontend_ploy.dir/frontends/ploy/src/sema/package_discovery_cache.cpp.o
[build] In file included from /home/PolyglotCompiler/frontends/ploy/include/ploy_sema.h:10,
[build]                  from /home/PolyglotCompiler/frontends/ploy/include/package_discovery_cache.h:9,
[build]                  from /home/PolyglotCompiler/frontends/ploy/src/sema/package_discovery_cache.cpp:1:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] [32/306   5% :: 7.830] Building CXX object CMakeFiles/polyglot_common.dir/backends/common/src/debug_info.cpp.o
[build] [32/306   6% :: 8.044] Building CXX object CMakeFiles/frontend_python.dir/frontends/python/src/parser/parser.cpp.o
[build] [32/306   6% :: 8.160] Building CXX object CMakeFiles/frontend_cpp.dir/frontends/cpp/src/sema/sema.cpp.o
[build] In file included from /home/PolyglotCompiler/common/include/core/symbols.h:11,
[build]                  from /home/PolyglotCompiler/frontends/common/include/sema_context.h:3,
[build]                  from /home/PolyglotCompiler/frontends/cpp/include/cpp_sema.h:3,
[build]                  from /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:6:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp: In member function ‘void polyglot::cpp::{anonymous}::Analyzer::AnalyzeDecl(const std::shared_ptr<polyglot::cpp::Statement>&)’:
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:93:76: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]    93 |             Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "cpp"};
[build]       |                                                                            ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:102:76: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   102 |             Symbol sym{rec->name, t, rec->loc, SymbolKind::kTypeName, "cpp"};
[build]       |                                                                            ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:112:97: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   112 |                 Symbol fsym{field.name, MapType(field.type), rec->loc, SymbolKind::kField, "cpp"};
[build]       |                                                                                                 ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:128:74: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   128 |             Symbol sym{en->name, t, en->loc, SymbolKind::kTypeName, "cpp"};
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:131:72: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   131 |                 Symbol esym{e, t, en->loc, SymbolKind::kVariable, "cpp"};
[build]       |                                                                        ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:150:99: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   150 |                 Symbol ts{p, Type::GenericParam(p, "cpp"), decl->loc, SymbolKind::kTypeName, "cpp"};
[build]       |                                                                                                   ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:163:102: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   163 |             Symbol ms{mod->name, Type::Module(mod->name, "cpp"), mod->loc, SymbolKind::kModule, "cpp"};
[build]       |                                                                                                      ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:191:158: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   191 |                     Symbol ex{c.name, MapType(c.exception_type), c.exception_type ? c.exception_type->loc : c.body.front()->loc, SymbolKind::kVariable, "cpp"};
[build]       |                                                                                                                                                              ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp: In member function ‘void polyglot::cpp::{anonymous}::Analyzer::DeclareFunction(const polyglot::cpp::FunctionDecl&)’:
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:208:70: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   208 |         Symbol sym{fn.name, fnt, fn.loc, SymbolKind::kFunction, "cpp"};
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp: In member function ‘void polyglot::cpp::{anonymous}::Analyzer::AnalyzeFunction(const polyglot::cpp::FunctionDecl&)’:
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:220:88: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   220 |             Symbol param{p.name, MapType(p.type), fn.loc, SymbolKind::kParameter, "cpp"};
[build]       |                                                                                        ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp: In member function ‘void polyglot::cpp::{anonymous}::Analyzer::AnalyzeStmt(const std::shared_ptr<polyglot::cpp::Statement>&)’:
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:241:76: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   241 |             Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "cpp"};
[build]       |                                                                            ^
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp: In member function ‘polyglot::core::Type polyglot::cpp::{anonymous}::Analyzer::AnalyzeExpr(const std::shared_ptr<polyglot::cpp::Expression>&)’:
[build] /home/PolyglotCompiler/frontends/cpp/src/sema/sema.cpp:403:91: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   403 |                 Symbol ps{p.name, MapType(p.type), lam->loc, SymbolKind::kParameter, "cpp"};
[build]       |                                                                                           ^
[build] [32/306   6% :: 8.273] Building CXX object CMakeFiles/frontend_python.dir/frontends/python/src/lowering/lowering.cpp.o
[build] In file included from /home/PolyglotCompiler/middle/include/ir/nodes/expressions.h:7,
[build]                  from /home/PolyglotCompiler/middle/include/ir/nodes/statements.h:8,
[build]                  from /home/PolyglotCompiler/middle/include/ir/cfg.h:9,
[build]                  from /home/PolyglotCompiler/middle/include/ir/ir_context.h:8,
[build]                  from /home/PolyglotCompiler/frontends/python/include/python_lowering.h:7,
[build]                  from /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp:1:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Invalid()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:35:74: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    35 |   static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I1()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:36:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    36 |   static IRType I1() { IRType t{IRTypeKind::kI1, "i1"}; t.is_signed = false; return t; }
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I8(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:37:94: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    37 |   static IRType I8(bool is_signed = true) { IRType t{IRTypeKind::kI8, is_signed ? "i8" : "u8"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I16(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:38:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    38 |   static IRType I16(bool is_signed = true) { IRType t{IRTypeKind::kI16, is_signed ? "i16" : "u16"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I32(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:39:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    39 |   static IRType I32(bool is_signed = true) { IRType t{IRTypeKind::kI32, is_signed ? "i32" : "u32"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I64(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:40:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    40 |   static IRType I64(bool is_signed = true) { IRType t{IRTypeKind::kI64, is_signed ? "i64" : "u64"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F32()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:41:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    41 |   static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F64()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:42:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    42 |   static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Void()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:43:65: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    43 |   static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Pointer(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:46:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    46 |     IRType t{IRTypeKind::kPointer, pointee.name + "*"};
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Reference(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:52:56: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    52 |     IRType t{IRTypeKind::kReference, pointee.name + "&"};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Array(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:58:75: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    58 |     IRType t{IRTypeKind::kArray, elem.name + "[" + std::to_string(n) + "]"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Vector(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:65:88: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    65 |     IRType t{IRTypeKind::kVector, "<" + std::to_string(lanes) + " x " + elem.name + ">"};
[build]       |                                                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Struct(std::string, std::vector<polyglot::ir::IRType>)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:72:50: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    72 |     IRType t{IRTypeKind::kStruct, std::move(name)};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Function(const polyglot::ir::IRType&, const std::vector<polyglot::ir::IRType>&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:79:41: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    79 |     IRType t{IRTypeKind::kFunction, "fn"};
[build]       |                                         ^
[build] In file included from /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp:14:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp: In function ‘polyglot::python::{anonymous}::EvalResult polyglot::python::{anonymous}::EvalComprehension(const std::shared_ptr<polyglot::python::ComprehensionExpression>&, LoweringContext&)’:
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp:675:19: warning: unused variable ‘current_block’ [-Wunused-variable]
[build]   675 |             auto *current_block = filter_block;
[build]       |                   ^~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp: In function ‘bool polyglot::python::{anonymous}::LowerImport(const std::shared_ptr<polyglot::python::ImportStatement>&, LoweringContext&)’:
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp:1880:23: warning: unused variable ‘info’ [-Wunused-variable]
[build]  1880 |                 auto &info = funcs[export_name];
[build]       |                       ^~~~
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp: In function ‘bool polyglot::python::{anonymous}::LowerFunction(const polyglot::python::FunctionDef&, LoweringContext&)’:
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp:2122:19: warning: unused variable ‘check_block’ [-Wunused-variable]
[build]  2122 |             auto *check_block = lc.builder.GetInsertPoint().get();
[build]       |                   ^~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp: At global scope:
[build] /home/PolyglotCompiler/frontends/python/src/lowering/lowering.cpp:164:12: warning: ‘polyglot::python::{anonymous}::EvalResult polyglot::python::{anonymous}::MakeBoolLiteral(bool, LoweringContext&)’ defined but not used [-Wunused-function]
[build]   164 | EvalResult MakeBoolLiteral(bool v, LoweringContext &lc) {
[build]       |            ^~~~~~~~~~~~~~~
[build] [32/306   7% :: 8.860] Building CXX object CMakeFiles/frontend_cpp.dir/frontends/cpp/src/parser/parser.cpp.o
[build] [32/306   7% :: 8.990] Building CXX object CMakeFiles/frontend_rust.dir/frontends/rust/src/lowering/lowering.cpp.o
[build] In file included from /home/PolyglotCompiler/middle/include/ir/nodes/expressions.h:7,
[build]                  from /home/PolyglotCompiler/middle/include/ir/nodes/statements.h:8,
[build]                  from /home/PolyglotCompiler/middle/include/ir/cfg.h:9,
[build]                  from /home/PolyglotCompiler/middle/include/ir/ir_context.h:8,
[build]                  from /home/PolyglotCompiler/frontends/rust/include/rust_lowering.h:7,
[build]                  from /home/PolyglotCompiler/frontends/rust/src/lowering/lowering.cpp:1:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Invalid()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:35:74: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    35 |   static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I1()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:36:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    36 |   static IRType I1() { IRType t{IRTypeKind::kI1, "i1"}; t.is_signed = false; return t; }
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I8(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:37:94: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    37 |   static IRType I8(bool is_signed = true) { IRType t{IRTypeKind::kI8, is_signed ? "i8" : "u8"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I16(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:38:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    38 |   static IRType I16(bool is_signed = true) { IRType t{IRTypeKind::kI16, is_signed ? "i16" : "u16"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I32(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:39:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    39 |   static IRType I32(bool is_signed = true) { IRType t{IRTypeKind::kI32, is_signed ? "i32" : "u32"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I64(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:40:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    40 |   static IRType I64(bool is_signed = true) { IRType t{IRTypeKind::kI64, is_signed ? "i64" : "u64"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F32()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:41:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    41 |   static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F64()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:42:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    42 |   static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Void()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:43:65: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    43 |   static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Pointer(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:46:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    46 |     IRType t{IRTypeKind::kPointer, pointee.name + "*"};
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Reference(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:52:56: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    52 |     IRType t{IRTypeKind::kReference, pointee.name + "&"};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Array(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:58:75: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    58 |     IRType t{IRTypeKind::kArray, elem.name + "[" + std::to_string(n) + "]"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Vector(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:65:88: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    65 |     IRType t{IRTypeKind::kVector, "<" + std::to_string(lanes) + " x " + elem.name + ">"};
[build]       |                                                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Struct(std::string, std::vector<polyglot::ir::IRType>)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:72:50: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    72 |     IRType t{IRTypeKind::kStruct, std::move(name)};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Function(const polyglot::ir::IRType&, const std::vector<polyglot::ir::IRType>&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:79:41: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    79 |     IRType t{IRTypeKind::kFunction, "fn"};
[build]       |                                         ^
[build] In file included from /home/PolyglotCompiler/frontends/rust/src/lowering/lowering.cpp:11:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] [32/306   7% :: 9.073] Building CXX object CMakeFiles/frontend_python.dir/frontends/python/src/sema/sema.cpp.o
[build] In file included from /home/PolyglotCompiler/common/include/core/symbols.h:11,
[build]                  from /home/PolyglotCompiler/frontends/common/include/sema_context.h:3,
[build]                  from /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:6:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::Run()’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:30:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::globals’ [-Wmissing-field-initializers]
[build]    30 |         scope_states_.push_back({ScopeKind::kModule});
[build]       |         ~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:30:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::nonlocals’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In lambda function:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:54:80: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]    54 |             Symbol sym{name, type, builtin_loc, SymbolKind::kTypeName, "python"};
[build]       |                                                                                ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::DeclareBuiltins()’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:80:63: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    80 |             declare_type(exc, Type{core::TypeKind::kClass, exc});
[build]       |                                                               ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:80:63: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:80:63: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In lambda function:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:87:68: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]    87 |                        builtin_loc, SymbolKind::kFunction, "python"};
[build]       |                                                                    ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘std::unordered_map<std::__cxx11::basic_string<char>, std::unordered_map<std::__cxx11::basic_string<char>, polyglot::core::Type> > polyglot::python::{anonymous}::Analyzer::BuiltinModuleExports()’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:145:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   145 |                 {"path", Type{core::TypeKind::kModule, "os.path"}},
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:145:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:145:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::DeclareSimple(const std::string&, polyglot::core::SymbolKind, const polyglot::core::Type&, const polyglot::core::SourceLoc&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:220:51: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   220 |         Symbol sym{name, type, loc, kind, "python"};
[build]       |                                                   ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::AnalyzeImportStatement(const polyglot::python::ImportStatement&, const polyglot::core::SourceLoc&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:362:67: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   362 |                              Type{core::TypeKind::kModule, modname}, loc);
[build]       |                                                                   ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:362:67: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:362:67: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::DeclareFunction(const polyglot::python::FunctionDef&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:443:26: warning: unused variable ‘p’ [-Wunused-variable]
[build]   443 |         for (const auto &p : fn.params) {
[build]       |                          ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:451:28: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   451 |                    "python"};
[build]       |                            ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::AnalyzeFunction(const polyglot::python::FunctionDef&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:461:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::globals’ [-Wmissing-field-initializers]
[build]   461 |         scope_states_.push_back({ScopeKind::kFunction});
[build]       |         ~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:461:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::nonlocals’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:465:87: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   465 |             Symbol param{p.name, Type::Any(), fn.loc, SymbolKind::kParameter, "python"};
[build]       |                                                                                       ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::DeclareClass(const polyglot::python::ClassDef&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:488:67: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   488 |         Symbol sym{cls.name, Type{core::TypeKind::kClass, cls.name}, cls.loc, SymbolKind::kTypeName,
[build]       |                                                                   ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:488:67: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:488:67: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:489:28: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   489 |                    "python"};
[build]       |                            ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::AnalyzeClass(const polyglot::python::ClassDef&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:499:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::globals’ [-Wmissing-field-initializers]
[build]   499 |         scope_states_.push_back({ScopeKind::kClass});
[build]       |         ~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:499:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::nonlocals’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::DeclareName(const std::string&, const polyglot::core::Type&, const polyglot::core::SourceLoc&, polyglot::core::SymbolKind)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:530:51: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   530 |         Symbol sym{name, type, loc, kind, "python"};
[build]       |                                                   ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘polyglot::core::Type polyglot::python::{anonymous}::Analyzer::AnalyzeExpr(const std::shared_ptr<polyglot::python::Expression>&)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:686:36: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::globals’ [-Wmissing-field-initializers]
[build]   686 |             scope_states_.push_back({ScopeKind::kFunction});
[build]       |             ~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:686:36: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::nonlocals’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:689:96: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   689 |                 Symbol param{p.name, Type::Any(), lambda->loc, SymbolKind::kParameter, "python"};
[build]       |                                                                                                ^
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp: In member function ‘void polyglot::python::{anonymous}::Analyzer::EnterBlockScope(polyglot::core::ScopeKind)’:
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:746:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::globals’ [-Wmissing-field-initializers]
[build]   746 |         scope_states_.push_back({kind});
[build]       |         ~~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~
[build] /home/PolyglotCompiler/frontends/python/src/sema/sema.cpp:746:32: warning: missing initializer for member ‘polyglot::python::{anonymous}::ScopeState::nonlocals’ [-Wmissing-field-initializers]
[build] [32/306   8% :: 9.507] Building CXX object CMakeFiles/frontend_ploy.dir/frontends/ploy/src/parser/parser.cpp.o
[build] [32/306   8% :: 9.599] Building CXX object CMakeFiles/frontend_java.dir/frontends/java/src/lowering/lowering.cpp.o
[build] In file included from /home/PolyglotCompiler/middle/include/ir/nodes/expressions.h:7,
[build]                  from /home/PolyglotCompiler/middle/include/ir/nodes/statements.h:8,
[build]                  from /home/PolyglotCompiler/middle/include/ir/cfg.h:9,
[build]                  from /home/PolyglotCompiler/middle/include/ir/ir_context.h:8,
[build]                  from /home/PolyglotCompiler/frontends/java/include/java_lowering.h:5,
[build]                  from /home/PolyglotCompiler/frontends/java/src/lowering/lowering.cpp:1:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Invalid()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:35:74: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    35 |   static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I1()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:36:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    36 |   static IRType I1() { IRType t{IRTypeKind::kI1, "i1"}; t.is_signed = false; return t; }
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I8(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:37:94: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    37 |   static IRType I8(bool is_signed = true) { IRType t{IRTypeKind::kI8, is_signed ? "i8" : "u8"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I16(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:38:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    38 |   static IRType I16(bool is_signed = true) { IRType t{IRTypeKind::kI16, is_signed ? "i16" : "u16"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I32(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:39:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    39 |   static IRType I32(bool is_signed = true) { IRType t{IRTypeKind::kI32, is_signed ? "i32" : "u32"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I64(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:40:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    40 |   static IRType I64(bool is_signed = true) { IRType t{IRTypeKind::kI64, is_signed ? "i64" : "u64"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F32()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:41:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    41 |   static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F64()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:42:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    42 |   static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Void()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:43:65: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    43 |   static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Pointer(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:46:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    46 |     IRType t{IRTypeKind::kPointer, pointee.name + "*"};
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Reference(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:52:56: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    52 |     IRType t{IRTypeKind::kReference, pointee.name + "&"};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Array(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:58:75: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    58 |     IRType t{IRTypeKind::kArray, elem.name + "[" + std::to_string(n) + "]"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Vector(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:65:88: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    65 |     IRType t{IRTypeKind::kVector, "<" + std::to_string(lanes) + " x " + elem.name + ">"};
[build]       |                                                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Struct(std::string, std::vector<polyglot::ir::IRType>)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:72:50: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    72 |     IRType t{IRTypeKind::kStruct, std::move(name)};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Function(const polyglot::ir::IRType&, const std::vector<polyglot::ir::IRType>&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:79:41: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    79 |     IRType t{IRTypeKind::kFunction, "fn"};
[build]       |                                         ^
[build] In file included from /home/PolyglotCompiler/frontends/java/src/lowering/lowering.cpp:7:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/java/src/lowering/lowering.cpp: At global scope:
[build] /home/PolyglotCompiler/frontends/java/src/lowering/lowering.cpp:15:12: warning: ‘polyglot::ir::IRType polyglot::java::{anonymous}::ToIRType(const polyglot::core::Type&)’ defined but not used [-Wunused-function]
[build]    15 | ir::IRType ToIRType(const core::Type &t) {
[build]       |            ^~~~~~~~
[build] [32/306   8% :: 10.260] Building CXX object CMakeFiles/frontend_ploy.dir/frontends/ploy/src/lowering/lowering.cpp.o
[build] In file included from /home/PolyglotCompiler/middle/include/ir/nodes/expressions.h:7,
[build]                  from /home/PolyglotCompiler/middle/include/ir/nodes/statements.h:8,
[build]                  from /home/PolyglotCompiler/middle/include/ir/cfg.h:9,
[build]                  from /home/PolyglotCompiler/middle/include/ir/ir_context.h:8,
[build]                  from /home/PolyglotCompiler/middle/include/ir/ir_builder.h:8,
[build]                  from /home/PolyglotCompiler/frontends/ploy/include/ploy_lowering.h:7,
[build]                  from /home/PolyglotCompiler/frontends/ploy/src/lowering/lowering.cpp:1:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Invalid()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:35:74: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    35 |   static IRType Invalid() { return IRType{IRTypeKind::kInvalid, "invalid"}; }
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I1()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:36:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    36 |   static IRType I1() { IRType t{IRTypeKind::kI1, "i1"}; t.is_signed = false; return t; }
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I8(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:37:94: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    37 |   static IRType I8(bool is_signed = true) { IRType t{IRTypeKind::kI8, is_signed ? "i8" : "u8"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I16(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:38:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    38 |   static IRType I16(bool is_signed = true) { IRType t{IRTypeKind::kI16, is_signed ? "i16" : "u16"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I32(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:39:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    39 |   static IRType I32(bool is_signed = true) { IRType t{IRTypeKind::kI32, is_signed ? "i32" : "u32"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::I64(bool)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:40:98: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    40 |   static IRType I64(bool is_signed = true) { IRType t{IRTypeKind::kI64, is_signed ? "i64" : "u64"}; t.is_signed = is_signed; return t; }
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F32()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:41:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    41 |   static IRType F32() { return IRType{IRTypeKind::kF32, "f32"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::F64()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:42:62: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    42 |   static IRType F64() { return IRType{IRTypeKind::kF64, "f64"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Void()’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:43:65: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    43 |   static IRType Void() { return IRType{IRTypeKind::kVoid, "void"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Pointer(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:46:54: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    46 |     IRType t{IRTypeKind::kPointer, pointee.name + "*"};
[build]       |                                                      ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Reference(const polyglot::ir::IRType&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:52:56: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    52 |     IRType t{IRTypeKind::kReference, pointee.name + "&"};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Array(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:58:75: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    58 |     IRType t{IRTypeKind::kArray, elem.name + "[" + std::to_string(n) + "]"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Vector(const polyglot::ir::IRType&, size_t)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:65:88: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    65 |     IRType t{IRTypeKind::kVector, "<" + std::to_string(lanes) + " x " + elem.name + ">"};
[build]       |                                                                                        ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Struct(std::string, std::vector<polyglot::ir::IRType>)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:72:50: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    72 |     IRType t{IRTypeKind::kStruct, std::move(name)};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h: In static member function ‘static polyglot::ir::IRType polyglot::ir::IRType::Function(const polyglot::ir::IRType&, const std::vector<polyglot::ir::IRType>&)’:
[build] /home/PolyglotCompiler/middle/include/ir/nodes/types.h:79:41: warning: missing initializer for member ‘polyglot::ir::IRType::subtypes’ [-Wmissing-field-initializers]
[build]    79 |     IRType t{IRTypeKind::kFunction, "fn"};
[build]       |                                         ^
[build] In file included from /home/PolyglotCompiler/frontends/ploy/include/ploy_sema.h:10,
[build]                  from /home/PolyglotCompiler/frontends/ploy/include/ploy_lowering.h:10:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] [32/306   9% :: 10.263] Building CXX object CMakeFiles/frontend_rust.dir/frontends/rust/src/parser/parser.cpp.o
[build] [32/306   9% :: 10.333] Building CXX object CMakeFiles/frontend_java.dir/frontends/java/src/sema/sema.cpp.o
[build] In file included from /home/PolyglotCompiler/common/include/core/symbols.h:11,
[build]                  from /home/PolyglotCompiler/frontends/common/include/sema_context.h:3,
[build]                  from /home/PolyglotCompiler/frontends/java/include/java_sema.h:3,
[build]                  from /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:4:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::Run()’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:30:78: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]    30 |                        module_.package_decl->loc, SymbolKind::kModule, "java"};
[build]       |                                                                              ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeImport(const polyglot::java::ImportDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:89:98: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]    89 |         Symbol sym{imp.path, Type::Module(imp.path, "java"), imp.loc, SymbolKind::kModule, "java"};
[build]       |                                                                                                  ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeDecl(const std::shared_ptr<polyglot::java::Statement>&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:131:77: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   131 |             Symbol sym{var->name, t, var->loc, SymbolKind::kVariable, "java"};
[build]       |                                                                             ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeClass(const polyglot::java::ClassDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:143:71: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   143 |         Symbol sym{cls.name, t, cls.loc, SymbolKind::kTypeName, "java"};
[build]       |                                                                       ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:171:93: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   171 |                 Symbol ps{p, Type::Struct(p, "java"), cls.loc, SymbolKind::kTypeName, "java"};
[build]       |                                                                                             ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeInterface(const polyglot::java::InterfaceDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:185:75: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   185 |         Symbol sym{iface.name, t, iface.loc, SymbolKind::kTypeName, "java"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeEnum(const polyglot::java::EnumDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:210:69: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   210 |         Symbol sym{en.name, t, en.loc, SymbolKind::kTypeName, "java"};
[build]       |                                                                     ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:217:71: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   217 |             Symbol cs{c.name, t, en.loc, SymbolKind::kVariable, "java"};
[build]       |                                                                       ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeRecord(const polyglot::java::RecordDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:230:71: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   230 |         Symbol sym{rec.name, t, rec.loc, SymbolKind::kTypeName, "java"};
[build]       |                                                                       ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:239:73: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   239 |             Symbol fs{comp.name, ct, rec.loc, SymbolKind::kField, "java"};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:245:76: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   245 |             Symbol ms{comp.name, ft, rec.loc, SymbolKind::kFunction, "java"};
[build]       |                                                                            ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::DeclareMethod(const polyglot::java::MethodDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:265:79: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   265 |         Symbol sym{method.name, fnt, method.loc, SymbolKind::kFunction, "java"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeMethod(const polyglot::java::MethodDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:277:93: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   277 |             Symbol param{p.name, MapType(p.type), method.loc, SymbolKind::kParameter, "java"};
[build]       |                                                                                             ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeField(const polyglot::java::FieldDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:293:72: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   293 |         Symbol sym{field.name, t, field.loc, SymbolKind::kField, "java"};
[build]       |                                                                        ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeConstructor(const polyglot::java::ConstructorDecl&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:304:91: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   304 |             Symbol param{p.name, MapType(p.type), ctor.loc, SymbolKind::kParameter, "java"};
[build]       |                                                                                           ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘void polyglot::java::{anonymous}::Analyzer::AnalyzeStmt(const std::shared_ptr<polyglot::java::Statement>&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:386:102: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   386 |             Symbol sym{foreach_stmt->var_name, elem, foreach_stmt->loc, SymbolKind::kVariable, "java"};
[build]       |                                                                                                      ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:415:60: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   415 |                               SymbolKind::kVariable, "java"};
[build]       |                                                            ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp: In member function ‘polyglot::core::Type polyglot::java::{anonymous}::Analyzer::AnalyzeExpr(const std::shared_ptr<polyglot::java::Expression>&)’:
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:550:80: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   550 |                 Symbol ps{p.name, pt, expr->loc, SymbolKind::kParameter, "java"};
[build]       |                                                                                ^
[build] /home/PolyglotCompiler/frontends/java/src/sema/sema.cpp:573:90: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   573 |                 Symbol ps{inst->pattern_var, pt, inst->loc, SymbolKind::kVariable, "java"};
[build]       |                                                                                          ^
[build] [32/306   9% :: 10.562] Building CXX object CMakeFiles/frontend_ploy.dir/frontends/ploy/src/sema/sema.cpp.o
[build] In file included from /home/PolyglotCompiler/frontends/ploy/include/ploy_sema.h:10,
[build]                  from /home/PolyglotCompiler/frontends/ploy/src/sema/sema.cpp:1:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] [32/306  10% :: 10.742] Building CXX object CMakeFiles/frontend_rust.dir/frontends/rust/src/sema/sema.cpp.o
[build] In file included from /home/PolyglotCompiler/common/include/core/symbols.h:11,
[build]                  from /home/PolyglotCompiler/frontends/common/include/sema_context.h:3,
[build]                  from /home/PolyglotCompiler/frontends/rust/include/rust_sema.h:3,
[build]                  from /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:7:
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Invalid()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    87 |   static Type Invalid() { return Type{TypeKind::kInvalid, "<invalid>"}; }
[build]       |                                                                      ^
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:87:70: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Void()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    88 |   static Type Void() { return Type{TypeKind::kVoid, "void"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:88:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Bool()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    89 |   static Type Bool() { return Type{TypeKind::kBool, "bool"}; }
[build]       |                                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:89:59: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    90 |   static Type Int() { return Type{TypeKind::kInt, "int"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:90:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    91 |   static Type Float() { return Type{TypeKind::kFloat, "float"}; }
[build]       |                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:91:62: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::String()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    92 |   static Type String() { return Type{TypeKind::kString, "string"}; }
[build]       |                                                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:92:65: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Any()’:
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    93 |   static Type Any() { return Type{TypeKind::kAny, "any"}; }
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:93:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Int(int, bool)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]    97 |     Type t{TypeKind::kInt, sign ? ("i" + std::to_string(bits)) : ("u" + std::to_string(bits))};
[build]       |                                                                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:97:94: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Float(int)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   105 |     Type t{TypeKind::kFloat, "f" + std::to_string(bits)};
[build]       |                                                        ^
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:105:56: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Array(polyglot::core::Type, size_t)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   112 |     Type t{TypeKind::kArray, "array"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:112:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Optional(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   120 |     Type t{TypeKind::kOptional, "optional"};
[build]       |                                           ^
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:120:43: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Slice(polyglot::core::Type)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   127 |     Type t{TypeKind::kSlice, "slice"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:127:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Struct(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   133 |     Type t{TypeKind::kStruct, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:133:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Union(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   138 |     Type t{TypeKind::kUnion, std::move(name)};
[build]       |                                             ^
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:138:45: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Enum(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   143 |     Type t{TypeKind::kEnum, std::move(name)};
[build]       |                                            ^
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:143:44: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Module(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   148 |     Type t{TypeKind::kModule, std::move(name)};
[build]       |                                              ^
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:148:46: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericParam(std::string, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   153 |     Type t{TypeKind::kGenericParam, std::move(name)};
[build]       |                                                    ^
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:153:52: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::Tuple(std::vector<polyglot::core::Type>)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   158 |     Type t{TypeKind::kTuple, "tuple"};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:158:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In static member function ‘static polyglot::core::Type polyglot::core::Type::GenericInstance(std::string, std::vector<polyglot::core::Type>, std::string)’:
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   163 |     Type t{TypeKind::kGenericInstance, std::move(name)};
[build]       |                                                       ^
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:163:55: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerTo(polyglot::core::Type) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   241 |     Type t{TypeKind::kPointer, element.name + "*"};
[build]       |                                                  ^
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:241:50: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::PointerToWithCV(polyglot::core::Type, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   246 |     Type t{TypeKind::kPointer, (is_const ? "const " : "") + element.name + "*"};
[build]       |                                                                               ^
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:246:79: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::ReferenceTo(polyglot::core::Type, bool, bool, bool) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   254 |     Type t{TypeKind::kReference, element.name + (is_rvalue ? "&&" : "&")};
[build]       |                                                                         ^
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:254:73: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::FunctionType(const std::string&, polyglot::core::Type, std::vector<polyglot::core::Type>) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   264 |     Type t{TypeKind::kFunction, name};
[build]       |                                     ^
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:264:37: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h: In member function ‘polyglot::core::Type polyglot::core::TypeSystem::UserType(std::string, std::string, polyglot::core::TypeKind) const’:
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::language’ [-Wmissing-field-initializers]
[build]   288 |     Type t{kind, std::move(name)};
[build]       |                                 ^
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::type_args’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/common/include/core/types.h:288:33: warning: missing initializer for member ‘polyglot::core::Type::lifetime’ [-Wmissing-field-initializers]
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp: In member function ‘void polyglot::rust::{anonymous}::Analyzer::AnalyzeItem(const std::shared_ptr<polyglot::rust::Statement>&)’:
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:253:75: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   253 |             Symbol sym{st->name, t, st->loc, SymbolKind::kTypeName, "rust"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:255:35: warning: missing initializer for member ‘polyglot::rust::{anonymous}::ScopeState::name’ [-Wmissing-field-initializers]
[build]   255 |             scope_stack_.push_back({ScopeKind::kClass});
[build]       |             ~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:259:87: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   259 |                 Symbol fs{f.name, MapType(f.type), st->loc, SymbolKind::kField, "rust"};
[build]       |                                                                                       ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:268:75: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   268 |             Symbol sym{en->name, t, en->loc, SymbolKind::kTypeName, "rust"};
[build]       |                                                                           ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:271:76: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   271 |                 Symbol vs{v.name, t, en->loc, SymbolKind::kVariable, "rust"};
[build]       |                                                                            ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:278:35: warning: missing initializer for member ‘polyglot::rust::{anonymous}::ScopeState::name’ [-Wmissing-field-initializers]
[build]   278 |             scope_stack_.push_back({ScopeKind::kClass});
[build]       |             ~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:291:100: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   291 |             Symbol ts{alias->name, MapType(alias->alias), alias->loc, SymbolKind::kTypeName, "rust"};
[build]       |                                                                                                    ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:296:35: warning: missing initializer for member ‘polyglot::rust::{anonymous}::ScopeState::name’ [-Wmissing-field-initializers]
[build]   296 |             scope_stack_.push_back({ScopeKind::kModule});
[build]       |             ~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:306:107: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   306 |             Symbol us{ruse->path, Type::Module(ruse->path, "rust"), ruse->loc, SymbolKind::kModule, "rust"};
[build]       |                                                                                                           ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:322:90: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   322 |             Symbol cs{cn->name, MapType(cn->type), cn->loc, SymbolKind::kVariable, "rust"};
[build]       |                                                                                          ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:328:84: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   328 |             Symbol ms{mr->name, Type::Any(), mr->loc, SymbolKind::kFunction, "rust"};
[build]       |                                                                                    ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp: In member function ‘void polyglot::rust::{anonymous}::Analyzer::DeclareFunction(const polyglot::rust::FunctionItem&)’:
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:346:71: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   346 |         Symbol sym{fn.name, fnt, fn.loc, SymbolKind::kFunction, "rust"};
[build]       |                                                                       ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp: In member function ‘void polyglot::rust::{anonymous}::Analyzer::AnalyzeFunction(const polyglot::rust::FunctionItem&)’:
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:384:84: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   384 |             Symbol param{p.name, param_type, fn.loc, SymbolKind::kParameter, "rust"};
[build]       |                                                                                    ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp: In member function ‘void polyglot::rust::{anonymous}::Analyzer::DeclarePattern(const std::shared_ptr<polyglot::rust::Pattern>&, const polyglot::core::Type&, const polyglot::core::SourceLoc&)’:
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:606:74: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   606 |             Symbol sym{id->name, type, loc, SymbolKind::kVariable, "rust"};
[build]       |                                                                          ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:612:76: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   612 |             Symbol sym{bind->name, type, loc, SymbolKind::kVariable, "rust"};
[build]       |                                                                            ^
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp: In member function ‘polyglot::core::Type polyglot::rust::{anonymous}::Analyzer::AnalyzeExpr(const std::shared_ptr<polyglot::rust::Expression>&)’:
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:783:35: warning: missing initializer for member ‘polyglot::rust::{anonymous}::ScopeState::name’ [-Wmissing-field-initializers]
[build]   783 |             scope_stack_.push_back({ScopeKind::kFunction});
[build]       |             ~~~~~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~
[build] /home/PolyglotCompiler/frontends/rust/src/sema/sema.cpp:786:92: warning: missing initializer for member ‘polyglot::core::Symbol::access’ [-Wmissing-field-initializers]
[build]   786 |                 Symbol ps{p.name, MapType(p.type), cls->loc, SymbolKind::kParameter, "rust"};
[build]       |                                                                                            ^
[build] [32/306  10% :: 10.797] Building CXX object CMakeFiles/frontend_java.dir/frontends/java/src/parser/parser.cpp.o
[build] ninja: build stopped: subcommand failed.
[proc] 命令“/usr/bin/cmake --build /home/PolyglotCompiler/build --config Debug --target all --”已退出，代码为 1
[driver] 生成完毕: 00:00:10.940
[build] 生成已完成，退出代码为 1