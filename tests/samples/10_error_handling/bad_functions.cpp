// ============================================================================
// bad_functions.cpp — C++ functions with known signatures for error demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <string>
#include <vector>

// A function that takes exactly 3 parameters
double compute(int a, double b, const std::string& label) {
    return a * b;
}

// A function that takes 1 parameter
int simple(int x) {
    return x * 2;
}

// A function that returns a string
std::string format_value(double value) {
    return "value=" + std::to_string(value);
}
