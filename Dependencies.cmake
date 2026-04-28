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

# ---------------------------------------------------------------------------
# Offline dependency cache.
#
# When a third-party dependency has been pre-fetched into
# "<repo>/.cache/deps/<name>/" (typically by running
# scripts/fetch_deps.ps1 or scripts/fetch_deps.sh), redirect FetchContent to
# that local directory by populating FETCHCONTENT_SOURCE_DIR_<UPPER>. This
# allows every consumer of this file (IDE, CLI build, packaging script) to
# skip the network entirely without touching any other call site, and avoids
# repeated git-clone timeouts on flaky connections.
#
# A user-provided value of FETCHCONTENT_SOURCE_DIR_<UPPER> always wins.
# ---------------------------------------------------------------------------
set(_POLYGLOT_DEPS_CACHE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.cache/deps")

function(_polyglot_use_cached_dep dep_dir fc_name)
    string(TOUPPER "${fc_name}" _upper)
    set(_var "FETCHCONTENT_SOURCE_DIR_${_upper}")
    if(DEFINED ${_var} AND NOT "${${_var}}" STREQUAL "")
        # Caller pinned a source directory; respect it without modification.
        return()
    endif()
    set(_cache_dir "${_POLYGLOT_DEPS_CACHE_ROOT}/${dep_dir}")
    if(IS_DIRECTORY "${_cache_dir}" AND EXISTS "${_cache_dir}/CMakeLists.txt")
        set(${_var} "${_cache_dir}" CACHE PATH "Local cached source for ${fc_name}" FORCE)
        message(STATUS "[deps] Using cached source for ${fc_name}: ${_cache_dir}")
    endif()
endfunction()

_polyglot_use_cached_dep(fmt           fmt)
_polyglot_use_cached_dep(nlohmann_json nlohmann_json)
_polyglot_use_cached_dep(Catch2        Catch2)
_polyglot_use_cached_dep(mimalloc      mimalloc)

# fmt
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.2.0
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
# Do not override the standard malloc/free entry points. The project links
# mimalloc-static into a single shared library (libruntime), but PolyglotCompiler
# ships several independent dylibs (frontends, backends, runtime). When mimalloc
# overrides malloc/free only in the dylib that contains it, allocations made on
# one side of a dylib boundary can be freed on the other side, producing
# "pointer being freed was not allocated" aborts on macOS (especially with
# MI_OSX_INTERPOSE / MI_OSX_ZONE). Keep the mi_* symbols available for explicit
# use, but route the global new/delete and malloc/free through libSystem so all
# dylibs agree on a single allocator.
set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
set(MI_OSX_ZONE OFF CACHE BOOL "" FORCE)
set(MI_OSX_INTERPOSE OFF CACHE BOOL "" FORCE)
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
