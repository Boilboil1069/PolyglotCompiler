// ============================================================================
// data_converter.cpp — C++ data conversion for type mapping demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <vector>
#include <string>

// A simple 2D point structure
struct Point {
    double x;
    double y;
};

// A bounding box structure
struct BoundingBox {
    double x_min;
    double y_min;
    double x_max;
    double y_max;
};

// Convert a vector of doubles to a single average value
double average(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    double sum = 0.0;
    for (double v : data) sum += v;
    return sum / static_cast<double>(data.size());
}

// Scale all values in a vector by a factor
std::vector<double> scale_vector(const std::vector<double>& data, double factor) {
    std::vector<double> result;
    result.reserve(data.size());
    for (double v : data) {
        result.push_back(v * factor);
    }
    return result;
}

// Compute the centroid of a bounding box
Point centroid(const BoundingBox& box) {
    Point p;
    p.x = (box.x_min + box.x_max) / 2.0;
    p.y = (box.y_min + box.y_max) / 2.0;
    return p;
}

// Compute dot product of two vectors
double dot_product(const std::vector<double>& a, const std::vector<double>& b) {
    double result = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        result += a[i] * b[i];
    }
    return result;
}
