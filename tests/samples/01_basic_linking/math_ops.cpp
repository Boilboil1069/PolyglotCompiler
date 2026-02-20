// ============================================================================
// math_ops.cpp — C++ math operations for cross-language linking demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <cmath>

// Add two integers and return the result
int add(int a, int b) {
    return a + b;
}

// Multiply two doubles and return the result
double multiply(double a, double b) {
    return a * b;
}

// Compute the Euclidean distance between two 2D points
double distance(double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;
    return std::sqrt(dx * dx + dy * dy);
}

// Clamp a value to the range [lo, hi]
int clamp(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}
