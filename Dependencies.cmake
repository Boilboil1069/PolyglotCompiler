# Third-party dependency setup for PolyglotCompiler
# Uses FetchContent to download sources into ${FETCHCONTENT_BASE_DIR} (defaults to dependencies/).

cmake_minimum_required(VERSION 3.20)
include_guard(GLOBAL)

include(FetchContent)

# Allow the user to override where dependencies are stored; default to the repo's dependencies/ folder.
if(NOT DEFINED FETCHCONTENT_BASE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dependencies" CACHE PATH "Base directory for fetched dependencies sources")
endif()
file(MAKE_DIRECTORY "${FETCHCONTENT_BASE_DIR}")

# fmt
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.0.2
)

# nlohmann/json (header-only)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# Catch2 (for unit tests)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)

# mimalloc (allocator)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG v2.1.7
)

# # ANTLR4 C++ runtime
# set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
# set(ANTLR_BUILD_CPP_EXAMPLES OFF CACHE BOOL "" FORCE)
# set(WITH_STATIC_CRT OFF CACHE BOOL "" FORCE)
# FetchContent_Declare(
#     antlr4
#     GIT_REPOSITORY https://github.com/antlr/antlr4.git
#     GIT_TAG 4.13.1
#     SOURCE_SUBDIR runtime/Cpp
# )

# Download (if missing) and make the packages available with their standard targets:
# - fmt::fmt / fmt::fmt-header-only
# - nlohmann_json::nlohmann_json
# - Catch2::Catch2WithMain / Catch2::Catch2
# - mimalloc-static / mimalloc
# - antlr4_shared / antlr4_static
FetchContent_MakeAvailable(fmt nlohmann_json Catch2 mimalloc)
