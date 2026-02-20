// ============================================================================
// matrix_math.cpp — C++ vector/map math operations
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <cmath>
#include <numeric>
#include <string>
#include <vector>

// Generate a sequence of doubles: [start, start+step, start+2*step, ...]
std::vector<double> generate_sequence(int count, double step) {
    std::vector<double> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        result.push_back(i * step);
    }
    return result;
}

// Flatten a vector of doubles to a vector of ints (truncate)
std::vector<int> flatten(const std::vector<double>& data) {
    std::vector<int> result;
    result.reserve(data.size());
    for (double d : data) {
        result.push_back(static_cast<int>(d));
    }
    return result;
}

// Dot product of a vector with itself (sum of squares)
double dot_product(const std::vector<double>& v) {
    double sum = 0.0;
    for (double x : v) {
        sum += x * x;
    }
    return sum;
}
