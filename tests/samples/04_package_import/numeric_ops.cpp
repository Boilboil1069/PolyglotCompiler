// ============================================================================
// numeric_ops.cpp — C++ numeric operations for package import demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <vector>
#include <cmath>
#include <numeric>

// Compute the sum of all elements
double sum_elements(const std::vector<double>& data) {
    return std::accumulate(data.begin(), data.end(), 0.0);
}

// Compute element-wise product of two vectors
std::vector<double> element_multiply(const std::vector<double>& a,
                                     const std::vector<double>& b) {
    std::vector<double> result;
    size_t n = std::min(a.size(), b.size());
    result.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        result.push_back(a[i] * b[i]);
    }
    return result;
}

// Compute the L2 norm of a vector
double l2_norm(const std::vector<double>& data) {
    double sum_sq = 0.0;
    for (double v : data) {
        sum_sq += v * v;
    }
    return std::sqrt(sum_sq);
}

// Apply a simple moving average filter
std::vector<double> moving_average(const std::vector<double>& data, int window) {
    std::vector<double> result;
    int n = static_cast<int>(data.size());
    for (int i = 0; i < n; ++i) {
        double sum = 0.0;
        int count = 0;
        for (int j = std::max(0, i - window + 1); j <= i; ++j) {
            sum += data[j];
            ++count;
        }
        result.push_back(sum / count);
    }
    return result;
}
