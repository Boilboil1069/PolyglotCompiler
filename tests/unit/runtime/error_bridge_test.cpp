/**
 * @file     error_bridge_test.cpp
 * @brief    Unit tests for the cross-language structured exception
 *           bridge runtime service.  The bridge's data plane stores
 *           the most recent Error payload thread-locally and raises
 *           a C++ `RuntimeError` exception that any enclosing native
 *           frame can catch.
 *
 * @ingroup  Tests / Unit / Runtime
 * @author   Manning Cyrus
 * @date     2026-05-04
 */
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "runtime/include/services/error_bridge.h"
#include "runtime/include/services/exception.h"

using polyglot::runtime::services::RuntimeError;

TEST_CASE("error_bridge: throw inside try populates the payload",
          "[runtime][error-bridge]") {
    REQUIRE(__ploy_rt_try_begin() == 0);
    bool caught = false;
    try {
        __ploy_rt_throw("boom");
        FAIL("__ploy_rt_throw must not return");
    } catch (const RuntimeError &) {
        caught = true;
    }
    __ploy_rt_try_end();
    REQUIRE(caught);

    const char *msg = __ploy_rt_current_error_message();
    REQUIRE(msg != nullptr);
    CHECK(std::string(msg) == "boom");
    const char *src = __ploy_rt_current_error_source_lang();
    REQUIRE(src != nullptr);
    CHECK(std::string(src) == "ploy");
    __ploy_rt_clear_error();
}

TEST_CASE("error_bridge: clear_error resets accessors",
          "[runtime][error-bridge]") {
    REQUIRE(__ploy_rt_try_begin() == 0);
    try { __ploy_rt_throw("x"); } catch (const RuntimeError &) {}
    __ploy_rt_try_end();
    __ploy_rt_clear_error();
    CHECK(__ploy_rt_current_error_message() == nullptr);
    CHECK(__ploy_rt_current_error_source_lang() == nullptr);
    CHECK(__ploy_rt_current_error_stacktrace_count() == 0);
}

TEST_CASE("error_bridge: throw_from tags the source language",
          "[runtime][error-bridge]") {
    REQUIRE(__ploy_rt_try_begin() == 0);
    try {
        __ploy_rt_throw_from("py boom", "python");
        FAIL("unreachable");
    } catch (const RuntimeError &) {}
    __ploy_rt_try_end();
    CHECK(std::string(__ploy_rt_current_error_message()) == "py boom");
    CHECK(std::string(__ploy_rt_current_error_source_lang()) == "python");
    __ploy_rt_clear_error();
}

TEST_CASE("error_bridge: nested try-catch surfaces the inner error",
          "[runtime][error-bridge]") {
    REQUIRE(__ploy_rt_try_begin() == 0);
    bool inner_caught = false;
    REQUIRE(__ploy_rt_try_begin() == 0);
    try {
        __ploy_rt_throw("inner");
        FAIL("unreachable");
    } catch (const RuntimeError &) {
        inner_caught = true;
    }
    __ploy_rt_try_end();
    CHECK(inner_caught);
    CHECK(std::string(__ploy_rt_current_error_message()) == "inner");
    __ploy_rt_clear_error();
    __ploy_rt_try_end();
}

TEST_CASE("error_bridge: stacktrace accessor is safe to call",
          "[runtime][error-bridge]") {
    REQUIRE(__ploy_rt_try_begin() == 0);
    try { __ploy_rt_throw("trace me"); } catch (const RuntimeError &) {}
    __ploy_rt_try_end();
    auto count = __ploy_rt_current_error_stacktrace_count();
    if (count > 0) {
        CHECK(__ploy_rt_current_error_stacktrace_at(0) != nullptr);
    }
    __ploy_rt_clear_error();
}
