// blas_kernels.cpp — Vector-scaled-add and L2 norm helpers.
// Part of the PolyglotCompiler sample matrix.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {

double polyglot_axpy(double a, const double *x, double *y, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        y[i] = a * x[i] + y[i];
        acc += y[i];
    }
    return acc;
}

double polyglot_l2(const double *x, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        acc += x[i] * x[i];
    }
    return acc;
}


}  // extern "C"
