# ============================================================================
# frontends/ — language frontend libraries
# Each frontend provides lexer, parser, sema, lowering, and a unified adapter.
# ============================================================================

# -- frontend_common (shared utilities: token pool, preprocessor, registry) ----

add_library(frontend_common
    common/src/token_pool.cpp
    common/src/preprocessor.cpp
    common/src/frontend_registry.cpp
)

target_include_directories(frontend_common PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(frontend_common PUBLIC polyglot_common middle_ir)

# -- Individual language frontends ---------------------------------------------

add_library(frontend_python
    python/src/lexer/lexer.cpp
    python/src/parser/parser.cpp
    python/src/sema/sema.cpp
    python/src/lowering/lowering.cpp
    python/src/python_frontend.cpp
    python/src/pyi_loader.cpp
)

target_include_directories(frontend_python PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_python PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_cpp
    cpp/src/lexer/lexer.cpp
    cpp/src/parser/parser.cpp
    cpp/src/sema/sema.cpp
    cpp/src/lowering/lowering.cpp
    cpp/src/constexpr/cpp_constexpr.cpp
    cpp/src/cpp_frontend.cpp
)

target_include_directories(frontend_cpp PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_cpp PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_rust
    rust/src/lexer/lexer.cpp
    rust/src/parser/parser.cpp
    rust/src/sema/sema.cpp
    rust/src/lowering/lowering.cpp
    rust/src/rust_frontend.cpp
    rust/src/crate_loader.cpp
)

target_include_directories(frontend_rust PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_rust PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_ploy
    ploy/src/lexer/lexer.cpp
    ploy/src/parser/parser.cpp
    ploy/src/sema/sema.cpp
    ploy/src/sema/package_discovery_cache.cpp
    ploy/src/sema/command_runner.cpp
    ploy/src/sema/package_indexer.cpp
    ploy/src/lowering/lowering.cpp
    ploy/src/ploy_frontend.cpp
)

target_include_directories(frontend_ploy PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_ploy PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_java
    java/src/lexer/lexer.cpp
    java/src/parser/parser.cpp
    java/src/sema/sema.cpp
    java/src/lowering/lowering.cpp
    java/src/java_frontend.cpp
    java/src/class_file_reader.cpp
)

target_include_directories(frontend_java PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_java PUBLIC frontend_common polyglot_common middle_ir)

add_library(frontend_dotnet
    dotnet/src/lexer/lexer.cpp
    dotnet/src/parser/parser.cpp
    dotnet/src/sema/sema.cpp
    dotnet/src/lowering/lowering.cpp
    dotnet/src/dotnet_frontend.cpp
    dotnet/src/metadata_reader.cpp
)

target_include_directories(frontend_dotnet PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_dotnet PUBLIC frontend_common polyglot_common middle_ir)

# -- JavaScript frontend (ES2020+) ---------------------------------------------
add_library(frontend_javascript
    javascript/src/lexer/lexer.cpp
    javascript/src/parser/parser.cpp
    javascript/src/sema/sema.cpp
    javascript/src/lowering/lowering.cpp
    javascript/src/javascript_frontend.cpp
)

target_include_directories(frontend_javascript PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_javascript PUBLIC frontend_common polyglot_common middle_ir)

# -- Ruby frontend (2.7+ / 3.x) ------------------------------------------------
add_library(frontend_ruby
    ruby/src/lexer/lexer.cpp
    ruby/src/parser/parser.cpp
    ruby/src/sema/sema.cpp
    ruby/src/lowering/lowering.cpp
    ruby/src/ruby_frontend.cpp
)

target_include_directories(frontend_ruby PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_ruby PUBLIC frontend_common polyglot_common middle_ir)

# -- Go frontend (1.18+) -------------------------------------------------------
add_library(frontend_go
    go/src/lexer/lexer.cpp
    go/src/parser/parser.cpp
    go/src/sema/sema.cpp
    go/src/lowering/lowering.cpp
    go/src/go_frontend.cpp
)

target_include_directories(frontend_go PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(frontend_go PUBLIC frontend_common polyglot_common middle_ir)
