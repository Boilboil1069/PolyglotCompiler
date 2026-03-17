# ============================================================================
# runtime/ — runtime support library
# GC strategies, FFI/interop, language-specific runtime helpers, services.
# ============================================================================

add_library(runtime
    src/gc/mark_sweep.cpp
    src/gc/generational.cpp
    src/gc/copying.cpp
    src/gc/incremental.cpp
    src/gc/gc_strategy.cpp
    src/gc/runtime.cpp
    src/interop/ffi.cpp
    src/interop/memory.cpp
    src/interop/marshalling.cpp
    src/interop/type_mapping.cpp
    src/interop/calling_convention.cpp
    src/interop/container_marshal.cpp
    src/interop/object_lifecycle.cpp
    src/libs/base.c
    src/libs/base_gc_bridge.cpp
    src/libs/python_rt.c
    src/libs/cpp_rt.c
    src/libs/rust_rt.c
    src/libs/java_rt.c
    src/libs/dotnet_rt.c
    src/services/exception.cpp
    src/services/reflection.cpp
    src/services/threading.cpp
)

target_include_directories(runtime PUBLIC
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(runtime PUBLIC
    ${CMAKE_DL_LIBS}
    Threads::Threads
)
