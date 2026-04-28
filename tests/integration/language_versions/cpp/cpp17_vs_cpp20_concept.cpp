// Fixture: C++ source that compiles only on c++20 and newer.
// Used by tests/integration/language_versions/lang_versions_test.cpp.
//
// The `requires`-clause and the `concept` declaration are both C++20
// features.  The PolyglotCompiler frontend must accept this translation
// unit when the active dialect is c++20+ and emit
// `kLangVersionMismatch` otherwise.

#include <type_traits>

template <typename T>
concept Integral = std::is_integral_v<T>;

template <typename T>
    requires Integral<T>
T add(T a, T b) {
    return a + b;
}

int driver() {
    return add<int>(1, 2);
}
