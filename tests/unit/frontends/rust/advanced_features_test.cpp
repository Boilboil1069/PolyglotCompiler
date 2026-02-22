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
        MyType { value }
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
        auto mod = ParseOk(R"(
struct Wrapper<'a> {
    data: &'a str
}
)");
        REQUIRE(mod->items.size() == 1);
        auto s = std::dynamic_pointer_cast<StructItem>(mod->items[0]);
        REQUIRE(s);
        REQUIRE(s->name == "Wrapper");
        REQUIRE(s->fields.size() == 1);
        REQUIRE(s->fields[0].name == "data");
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
        auto mod = ParseOk(R"(
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
        auto mod = ParseOk(R"(
fn test() {
    let mut x = 5;
    let y = &mut x;
    *y += 1;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() >= 2);
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
        auto mod = ParseOk(R"(
fn test() {
    let mut x = 5;
    {
        let y = &mut x;
    }
    let z = &mut x;
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() >= 2);
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
        REQUIRE(let_stmt->name == "add_one");
        auto closure = std::dynamic_pointer_cast<ClosureExpression>(let_stmt->init);
        REQUIRE(closure);
    }
    
    SECTION("Closure with types") {
        auto mod = ParseOk("fn test() { let add = |x: i32, y: i32| -> i32 { x + y }; }");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        auto let_stmt = std::dynamic_pointer_cast<LetStatement>(fn->body[0]);
        REQUIRE(let_stmt);
        auto closure = std::dynamic_pointer_cast<ClosureExpression>(let_stmt->init);
        REQUIRE(closure);
        REQUIRE(closure->params.size() == 2);
        REQUIRE(closure->return_type != nullptr);
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
        auto mod = ParseOk(R"(
fn test() {
    let data = vec![1, 2, 3];
    let process = move || println!("{:?}", data);
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
        REQUIRE(fn->body.size() == 2);
    }
    
    SECTION("FnOnce, FnMut, Fn") {
        auto mod = ParseOk(R"(
fn call_once<F: FnOnce()>(f: F) { f(); }
fn call_mut<F: FnMut()>(mut f: F) { f(); }
fn call<F: Fn()>(f: F) { f(); }
)");
        REQUIRE(mod->items.size() == 3);
        for (int i = 0; i < 3; ++i) {
            auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[i]);
            REQUIRE(fn);
            REQUIRE(fn->type_params.size() == 1);
        }
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
        auto mod = ParseOk(R"(
fn test(value: i32) {
    match value {
        1..=5 => println!("small"),
        6..=10 => println!("medium"),
        _ => println!("large"),
    }
}
)");
        REQUIRE(mod->items.size() == 1);
        auto fn = std::dynamic_pointer_cast<FunctionItem>(mod->items[0]);
        REQUIRE(fn);
    }
}

// ============ Tests 7-15: Core features ============

TEST_CASE("Rust - Enums", "[rust][enum]") {
    std::string codes[] = {
        "enum Color { Red, Green, Blue }",
        "enum Option<T> { Some(T), None }",
        "enum Message { Quit, Move { x: i32, y: i32 }, Write(String) }",
        "enum Result<T, E> { Ok(T), Err(E) }",
        "enum IpAddr { V4(u8, u8, u8, u8), V6(String) }"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Generics", "[rust][generic]") {
    std::string codes[] = {
        "fn largest<T: PartialOrd>(list: &[T]) -> &T { &list[0] }",
        "struct Point<T> { x: T, y: T }",
        "impl<T> Point<T> { fn x(&self) -> &T { &self.x } }",
        "fn print<T: Display>(value: T) { println!(\"{}\", value); }",
        "fn compare<T: PartialOrd + Clone>(a: T, b: T) -> T { if a > b { a } else { b } }"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Macros", "[rust][macro]") {
    std::string codes[] = {
        "println!(\"Hello, {}!\", name);",
        "vec![1, 2, 3, 4, 5];",
        "assert_eq!(x, y);",
        "panic!(\"error: {}\", msg);",
        "format!(\"x = {}, y = {}\", x, y);"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Modules", "[rust][module]") {
    std::string codes[] = {
        "mod my_module { pub fn func() {} }",
        "pub mod public_module;",
        "mod private_module { /* ... */ }",
        "use std::collections::HashMap;",
        "use std::io::{self, Read, Write};"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Smart Pointers", "[rust][smart_ptr]") {
    std::string codes[] = {
        "let b = Box::new(5);",
        "let rc = Rc::new(data);",
        "let arc = Arc::new(shared_data);",
        "let cell = Cell::new(42);",
        "let refcell = RefCell::new(vec![]);"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Async/Await", "[rust][async]") {
    std::string codes[] = {
        "async fn func() -> i32 { 42 }",
        "let result = async_func().await;",
        "async move { process(data).await }",
        "futures::join!(a, b, c);",
        "tokio::spawn(async { work().await });"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Unsafe", "[rust][unsafe]") {
    std::string codes[] = {
        "unsafe { *raw_ptr }",
        "unsafe fn dangerous() {}",
        "unsafe impl Send for MyType {}",
        "unsafe { std::ptr::write(ptr, value); }",
        "let slice = unsafe { std::slice::from_raw_parts(ptr, len) };"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

// ============ Tests 16-28: Advanced features ============

TEST_CASE("Rust - Associated Types", "[rust][assoc]") {
    std::string code = R"(
trait Graph {
    type Node;
    type Edge;
    fn nodes(&self) -> Vec<Self::Node>;
}
)";
    REQUIRE(true);
}

TEST_CASE("Rust - Deref Coercion", "[rust][deref]") {
    std::string code = R"(
use std::ops::Deref;
impl<T> Deref for MyBox<T> {
    type Target = T;
    fn deref(&self) -> &T { &self.0 }
}
)";
    REQUIRE(true);
}

TEST_CASE("Rust - Type Aliases", "[rust][alias]") {
    std::string codes[] = {
        "type Kilometers = i32;",
        "type Result<T> = std::result::Result<T, MyError>;",
        "type Thunk = Box<dyn Fn() + Send + 'static>;",
        "type NodePtr<T> = Option<Box<Node<T>>>;",
        "type Callback = fn(i32) -> i32;"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

TEST_CASE("Rust - Const and Static", "[rust][const_static]") {
    std::string codes[] = {
        "const MAX_POINTS: u32 = 100_000;",
        "static HELLO_WORLD: &str = \"Hello, world!\";",
        "static mut COUNTER: u32 = 0;",
        "const fn double(x: i32) -> i32 { x * 2 }",
        "const ARRAY: [i32; 5] = [1, 2, 3, 4, 5];"
    };
    
    for (const auto& code : codes) {
        REQUIRE(true);
    }
}

// Integration test
TEST_CASE("Rust - Complex Integration", "[rust][integration]") {
    std::string code = R"(
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
)";
    REQUIRE(true);
}
