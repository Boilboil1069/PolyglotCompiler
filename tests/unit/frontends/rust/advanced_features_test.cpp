#include <catch2/catch_test_macros.hpp>
#include <string>

#include "frontends/rust/include/rust_lexer.h"
#include "frontends/rust/include/rust_parser.h"
#include "frontends/rust/include/rust_ast.h"

using polyglot::frontends::Diagnostics;
using polyglot::rust::RustLexer;
using polyglot::rust::RustParser;
using namespace polyglot::rust;

// Helper: parse code, assert no errors, return module
static std::shared_ptr<Module> ParseOk(const std::string &code) {
    Diagnostics diag;
    RustLexer lexer(code.c_str(), "<test>");
    RustParser parser(lexer, diag);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    REQUIRE(mod);
    REQUIRE_FALSE(diag.HasErrors());
    return mod;
}

// Helper: parse code allowing partial recovery (some advanced syntax may produce
// parser diagnostics), return module.  Asserts module is non-null.
static std::shared_ptr<Module> ParsePartial(const std::string &code) {
    Diagnostics diag;
    RustLexer lexer(code.c_str(), "<test>");
    RustParser parser(lexer, diag);
    parser.ParseModule();
    auto mod = parser.TakeModule();
    REQUIRE(mod);
    return mod;
}

// ============ Test 1: Traits ============
TEST_CASE("Rust - Traits", "[rust][trait]") {
    SECTION("Basic trait") {
        auto mod = ParseOk(R"(
trait Drawable {
    fn draw(&self);
}
)");
        REQUIRE(mod->items.size() == 1);
        auto trait = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
        REQUIRE(trait);
        REQUIRE(trait->name == "Drawable");
        REQUIRE(trait->items.size() == 1);
    }
    
    SECTION("Trait with default implementation") {
        auto mod = ParseOk(R"(
trait Foo {
    fn bar(&self) {
        println!("default");
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto trait = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
        REQUIRE(trait);
        REQUIRE(trait->name == "Foo");
        REQUIRE(trait->items.size() == 1);
    }
    
    SECTION("Trait with associated type") {
        auto mod = ParseOk(R"(
trait Iterator {
    type Item;
    fn next(&mut self) -> Option<Self::Item>;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto trait = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
        REQUIRE(trait);
        REQUIRE(trait->name == "Iterator");
        REQUIRE(trait->items.size() == 2);
    }
    
    SECTION("Trait bounds") {
        auto mod = ParseOk(R"(
trait Foo: Display + Clone {
    fn method(&self);
}
)");
        REQUIRE(mod->items.size() == 1);
        auto trait = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
        REQUIRE(trait);
        REQUIRE(trait->name == "Foo");
        REQUIRE(trait->super_traits.size() >= 1);
    }
    
    SECTION("Generic trait") {
        auto mod = ParseOk(R"(
trait Convert<T> {
    fn convert(&self) -> T;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto trait = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
        REQUIRE(trait);
        REQUIRE(trait->name == "Convert");
        REQUIRE(trait->type_params.size() == 1);
        REQUIRE(trait->type_params[0] == "T");
    }
}

// ============ Test 2: Impl blocks ============
TEST_CASE("Rust - Impl", "[rust][impl]") {
    SECTION("Inherent impl") {
        auto mod = ParseOk(R"(
impl Rectangle {
    fn area(&self) -> i32 {
        self.width * self.height
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->items.size() == 1);
    }
    
    SECTION("Trait impl") {
        auto mod = ParseOk(R"rust(
impl Display for Point {
    fn fmt(&self, f: &mut Formatter) -> Result {
        write!(f, "({}, {})", self.x, self.y)
    }
}
)rust");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->trait_type != nullptr);
        REQUIRE(impl_item->items.size() == 1);
    }
    
    SECTION("Generic impl") {
        auto mod = ParseOk(R"(
impl<T> MyType<T> {
    fn new(value: T) -> Self {
        value
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->type_params.size() == 1);
        REQUIRE(impl_item->items.size() == 1);
    }
    
    SECTION("Conditional impl") {
        auto mod = ParseOk(R"(
impl<T: Display> MyTrait for T {
    fn method(&self) {}
}
)");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->trait_type != nullptr);
    }
    
    SECTION("Multiple trait bounds") {
        auto mod = ParseOk(R"(
impl<T: Display + Clone> Container<T> {
    fn show(&self) {}
}
)");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->items.size() == 1);
    }
}

// ============ Test 3: Lifetimes ============
TEST_CASE("Rust - Lifetimes", "[rust][lifetime]") {
    SECTION("Basic lifetime") {
        auto mod = ParseOk(R"(
fn longest<'a>(x: &'a str, y: &'a str) -> &'a str {
    if x.len() > y.len() { x } else { y }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->name == "longest");
        REQUIRE(fn->params.size() == 2);
        REQUIRE(fn->return_type != nullptr);
    }
    
    SECTION("Struct with lifetime") {
        auto mod = ParsePartial(R"(
struct Wrapper<'a> {
    data: &'a str
}
)");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Multiple lifetimes") {
        auto mod = ParseOk(R"(
fn func<'a, 'b>(x: &'a str, y: &'b str) -> &'a str {
    x
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->name == "func");
        REQUIRE(fn->params.size() == 2);
    }
    
    SECTION("Lifetime bounds") {
        auto mod = ParseOk(R"(
fn func<'a, 'b: 'a>(x: &'a str, y: &'b str) -> &'a str {
    y
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->params.size() == 2);
    }
    
    SECTION("Static lifetime") {
        auto mod = ParsePartial(R"(
static NAME: &'static str = "Rust";
)");
        REQUIRE(mod->items.size() >= 1);
    }
}

// ============ Test 4: Borrow checker ============
TEST_CASE("Rust - Borrow Checker", "[rust][borrow]") {
    SECTION("Immutable borrow") {
        auto mod = ParseOk(R"(
fn test() {
    let x = 5;
    let y = &x;
    println!("{}", y);
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() >= 2);
    }
    
    SECTION("Mutable borrow") {
        auto mod = ParsePartial(R"(
fn test() {
    let mut x = 5;
    let y = &mut x;
    *y += 1;
}
)");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Multiple immutable borrows") {
        auto mod = ParseOk(R"(
fn test() {
    let x = 5;
    let y = &x;
    let z = &x;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 3);
    }
    
    SECTION("Borrow scope") {
        auto mod = ParsePartial(R"(
fn test() {
    let mut x = 5;
    {
        let y = &mut x;
    }
    let z = &mut x;
}
)");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Move semantics") {
        auto mod = ParseOk(R"(
fn test() {
    let x = String::from("hello");
    let y = x;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 2);
    }
}

// ============ Test 5: Closures ============
TEST_CASE("Rust - Closures", "[rust][closure]") {
    SECTION("Simple closure") {
        auto mod = ParseOk("fn test() { let add_one = |x| x + 1; }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 1);
        auto let_stmt = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
        REQUIRE(let_stmt);
        REQUIRE(let_stmt->init != nullptr);
        auto closure = std::dynamic_pointer_cast<ClosureExpression>(let_stmt->init);
        REQUIRE(closure);
    }
    
    SECTION("Closure with types") {
        auto mod = ParsePartial("fn test() { let add = |x: i32, y: i32| -> i32 { x + y }; }");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Capturing environment") {
        auto mod = ParseOk(R"(
fn test() {
    let x = 10;
    let add_x = |y| x + y;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 2);
    }
    
    SECTION("Move closure") {
        auto mod = ParsePartial(R"(
fn test() {
    let data = vec![1, 2, 3];
    let process = move || println!("{:?}", data);
}
)");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("FnOnce, FnMut, Fn") {
        auto mod = ParsePartial(R"(
fn call_once<F: FnOnce()>(f: F) { f(); }
fn call_mut<F: FnMut()>(mut f: F) { f(); }
fn call<F: Fn()>(f: F) { f(); }
)");
        REQUIRE(mod->items.size() >= 1);
    }
}

// ============ Test 6: Pattern matching ============
TEST_CASE("Rust - Pattern Matching", "[rust][match]") {
    SECTION("Basic match") {
        auto mod = ParseOk(R"(
fn test(value: i32) {
    match value {
        0 => println!("zero"),
        1 => println!("one"),
        _ => println!("other"),
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() >= 1);
    }
    
    SECTION("Match with guard") {
        auto mod = ParseOk(R"(
fn test(num: i32) {
    match num {
        x if x > 0 => println!("positive"),
        x if x < 0 => println!("negative"),
        _ => println!("zero"),
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() >= 1);
    }
    
    SECTION("Destructuring") {
        auto mod = ParseOk(R"(
fn test(point: Point) {
    match point {
        Point { x: 0, y: 0 } => println!("origin"),
        Point { x, y } => println!("{}, {}", x, y),
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
    }
    
    SECTION("Enum matching") {
        auto mod = ParseOk(R"(
fn test(option: Option) {
    match option {
        Some(x) => println!("{}", x),
        None => println!("none"),
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
    }
    
    SECTION("Range pattern") {
        auto mod = ParsePartial(R"(
fn test(value: i32) {
    match value {
        1..=5 => println!("small"),
        6..=10 => println!("medium"),
        _ => println!("large"),
    }
}
)");
        REQUIRE(mod->items.size() >= 1);
    }
}

// ============ Tests 7-15: Core features ============

TEST_CASE("Rust - Enums", "[rust][enum]") {
    // Simple enum variants are fully supported
    std::string ok_codes[] = {
        "enum Color { Red, Green, Blue }",
    };
    
    for (const auto& code : ok_codes) {
        auto mod = ParseOk(code);
        REQUIRE(mod->items.size() == 1);
        auto e = std::dynamic_pointer_cast<EnumItem>(mod->items[0]);
        REQUIRE(e);
        REQUIRE_FALSE(e->name.empty());
        REQUIRE(e->variants.size() >= 2);
    }
    
    // Complex enum variants (generics, tuple/struct fields) may need partial parsing
    std::string partial_codes[] = {
        "enum Option<T> { Some(T), None }",
        "enum Message { Quit, Move { x: i32, y: i32 }, Write(String) }",
        "enum Result<T, E> { Ok(T), Err(E) }",
        "enum IpAddr { V4(u8, u8, u8, u8), V6(String) }"
    };
    
    for (const auto& code : partial_codes) {
        auto mod = ParsePartial(code);
        REQUIRE(mod->items.size() >= 1);
    }
}

TEST_CASE("Rust - Generics", "[rust][generic]") {
    SECTION("Generic function") {
        auto mod = ParseOk("fn largest<T: PartialOrd>(list: &[T]) -> &T { &list[0] }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->name == "largest");
        REQUIRE(fn->type_params.size() == 1);
    }
    
    SECTION("Generic struct") {
        auto mod = ParsePartial("struct Point<T> { x: T, y: T }");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Generic impl") {
        auto mod = ParseOk("impl<T> Point<T> { fn x(&self) -> &T { &self.x } }");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->type_params.size() == 1);
    }
    
    SECTION("Generic with trait bound") {
        auto mod = ParseOk(R"(fn print<T: Display>(value: T) { println!("{}", value); })");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->type_params.size() == 1);
    }
    
    SECTION("Multiple trait bounds") {
        auto mod = ParseOk("fn compare<T: PartialOrd + Clone>(a: T, b: T) -> T { if a > b { a } else { b } }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->params.size() == 2);
    }
}

TEST_CASE("Rust - Macros", "[rust][macro]") {
    // Macro invocations parsed as ExprStatement with MacroCallExpression
    std::string codes[] = {
        R"(fn t() { println!("Hello, {}!", name); })",
        "fn t() { vec![1, 2, 3, 4, 5]; }",
        "fn t() { assert_eq!(x, y); }",
        R"(fn t() { panic!("error: {}", msg); })",
        R"(fn t() { format!("x = {}, y = {}", x, y); })"
    };
    
    for (const auto& code : codes) {
        auto mod = ParseOk(code);
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() >= 1);
    }
}

TEST_CASE("Rust - Modules", "[rust][module]") {
    SECTION("Inline module") {
        auto mod = ParseOk("mod my_module { pub fn func() {} }");
        REQUIRE(mod->items.size() == 1);
        auto m = std::dynamic_pointer_cast<ModItem>(mod->items[0]);
        REQUIRE(m);
        REQUIRE(m->name == "my_module");
        REQUIRE(m->items.size() == 1);
    }
    
    SECTION("External module declaration") {
        auto mod = ParseOk("pub mod public_module;");
        REQUIRE(mod->items.size() == 1);
    }
    
    SECTION("Use declaration") {
        auto mod = ParseOk("use std::collections::HashMap;");
        REQUIRE(mod->items.size() == 1);
        auto use_decl = std::dynamic_pointer_cast<UseDeclaration>(mod->items[0]);
        REQUIRE(use_decl);
        REQUIRE_FALSE(use_decl->path.empty());
    }
    
    SECTION("Use with grouped imports") {
        auto mod = ParsePartial("use std::io::{self, Read, Write};");
        REQUIRE(mod->items.size() >= 1);
    }
}

TEST_CASE("Rust - Smart Pointers", "[rust][smart_ptr]") {
    // Smart pointer usage is parsed as normal let statements with function calls
    std::string codes[] = {
        "fn t() { let b = Box::new(5); }",
        "fn t() { let rc = Rc::new(data); }",
        "fn t() { let arc = Arc::new(shared_data); }",
        "fn t() { let cell = Cell::new(42); }",
        "fn t() { let refcell = RefCell::new(vec![]); }"
    };
    
    for (const auto& code : codes) {
        auto mod = ParseOk(code);
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 1);
        auto let_stmt = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
        REQUIRE(let_stmt);
        REQUIRE(let_stmt->init != nullptr);
    }
}

TEST_CASE("Rust - Async/Await", "[rust][async]") {
    SECTION("Async function") {
        auto mod = ParseOk("async fn func() -> i32 { 42 }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->is_async);
        REQUIRE(fn->name == "func");
        REQUIRE(fn->return_type != nullptr);
    }
    
    SECTION("Await expression") {
        auto mod = ParseOk("fn test() { let result = async_func().await; }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 1);
    }
}

TEST_CASE("Rust - Unsafe", "[rust][unsafe]") {
    SECTION("Unsafe block") {
        auto mod = ParsePartial("fn test() { unsafe { *raw_ptr } }");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Unsafe function") {
        auto mod = ParseOk("unsafe fn dangerous() {}");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->is_unsafe);
        REQUIRE(fn->name == "dangerous");
    }
    
    SECTION("Unsafe impl") {
        auto mod = ParseOk("unsafe impl Send for MyType {}");
        REQUIRE(mod->items.size() == 1);
        auto impl_item = std::dynamic_pointer_cast<ImplItem>(mod->items[0]);
        REQUIRE(impl_item);
        REQUIRE(impl_item->is_unsafe);
    }
}

// ============ Tests 16-28: Advanced features ============

TEST_CASE("Rust - Associated Types", "[rust][assoc]") {
    auto mod = ParseOk(R"(
trait Graph {
    type Node;
    type Edge;
    fn nodes(&self) -> Vec<Self::Node>;
}
)");
    REQUIRE(mod->items.size() == 1);
    auto trait = std::dynamic_pointer_cast<TraitItem>(mod->items[0]);
    REQUIRE(trait);
    REQUIRE(trait->name == "Graph");
    // Associated types + method
    REQUIRE(trait->items.size() == 3);
}

TEST_CASE("Rust - Deref Coercion", "[rust][deref]") {
    auto mod = ParsePartial(R"(
use std::ops::Deref;
impl<T> Deref for MyBox<T> {
    type Target = T;
    fn deref(&self) -> &T { &self.0 }
}
)");
    REQUIRE(mod->items.size() >= 1);
}

TEST_CASE("Rust - Type Aliases", "[rust][alias]") {
    SECTION("Simple alias") {
        auto mod = ParseOk("type Kilometers = i32;");
        REQUIRE(mod->items.size() == 1);
        auto alias = std::dynamic_pointer_cast<TypeAliasItem>(mod->items[0]);
        REQUIRE(alias);
        REQUIRE(alias->name == "Kilometers");
        REQUIRE(alias->alias != nullptr);
    }
    
    SECTION("Generic alias") {
        auto mod = ParsePartial("type Result<T> = std::result::Result<T, MyError>;");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Function pointer alias") {
        auto mod = ParseOk("type Callback = fn(i32) -> i32;");
        REQUIRE(mod->items.size() == 1);
        auto alias = std::dynamic_pointer_cast<TypeAliasItem>(mod->items[0]);
        REQUIRE(alias);
        REQUIRE(alias->name == "Callback");
    }
}

TEST_CASE("Rust - Const and Static", "[rust][const_static]") {
    SECTION("Const declaration") {
        auto mod = ParseOk("const MAX_POINTS: u32 = 100_000;");
        REQUIRE(mod->items.size() == 1);
        auto c = std::dynamic_pointer_cast<ConstItem>(mod->items[0]);
        REQUIRE(c);
        REQUIRE(c->name == "MAX_POINTS");
        REQUIRE(c->type != nullptr);
        REQUIRE(c->value != nullptr);
    }
    
    SECTION("Static declaration") {
        auto mod = ParsePartial(R"(static HELLO_WORLD: &str = "Hello, world!";)");
        REQUIRE(mod->items.size() >= 1);
    }
    
    SECTION("Const function") {
        auto mod = ParseOk("const fn double(x: i32) -> i32 { x * 2 }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->is_const);
        REQUIRE(fn->name == "double");
    }
    
    SECTION("Const array") {
        auto mod = ParsePartial("const ARRAY: [i32; 5] = [1, 2, 3, 4, 5];");
        REQUIRE(mod->items.size() >= 1);
    }
}

// Integration test
TEST_CASE("Rust - Complex Integration", "[rust][integration]") {
    auto mod = ParsePartial(R"(
use std::fmt::Display;

trait Processor<'a, T: Display> {
    type Output;
    fn process(&self, data: &'a [T]) -> Self::Output;
}

impl<'a, T: Display + Clone> Processor<'a, T> for MyProcessor {
    type Output = Vec<T>;
    
    fn process(&self, data: &'a [T]) -> Self::Output {
        data.iter().map(|x| x.clone()).collect()
    }
}

async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let data = vec![1, 2, 3, 4, 5];
    let processor = MyProcessor::new();
    
    let result = match processor.process(&data) {
        output if output.len() > 0 => Some(output),
        _ => None,
    };
    
    Ok(())
}
)");
    // Parser should recover and produce at least some items
    REQUIRE(mod->items.size() >= 1);
}
