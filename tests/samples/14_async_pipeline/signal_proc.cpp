// ============================================================================
// signal_proc.cpp — C++ signal processing functions
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

// Simple band-pass filter (keep frequencies between lo and hi)
// This is a naive FIR stub for demonstration purposes.
std::vector<double> bandpass_filter(const std::vector<double>& signal,
                                    double lo, double hi) {
    std::vector<double> out(signal.size());
    // Moving-average smoothing as a stand-in for real DSP
    int window = static_cast<int>(1.0 / lo);
    if (window < 1) window = 1;
    for (size_t i = 0; i < signal.size(); ++i) {
        double sum = 0.0;
        int count = 0;
        for (int j = -window; j <= window; ++j) {
            int idx = static_cast<int>(i) + j;
            if (idx >= 0 && idx < static_cast<int>(signal.size())) {
                sum += signal[idx];
                ++count;
            }
        }
        out[i] = sum / count;
    }
    return out;
}

// Root-mean-square of a signal
double rms(const std::vector<double>& signal) {
    if (signal.empty()) return 0.0;
    double sum_sq = 0.0;
    for (double s : signal) sum_sq += s * s;
    return std::sqrt(sum_sq / signal.size());
}

// Peak absolute value
double peak(const std::vector<double>& signal) {
    double mx = 0.0;
    for (double s : signal) {
        double a = std::abs(s);
        if (a > mx) mx = a;
    }
    return mx;
}

// Merge three signals by interleaving
std::vector<double> merge_signals(const std::vector<double>& a,
                                   const std::vector<double>& b,
                                   const std::vector<double>& c) {
    std::vector<double> merged;
    size_t n = std::max({a.size(), b.size(), c.size()});
    merged.reserve(n * 3);
    for (size_t i = 0; i < n; ++i) {
        if (i < a.size()) merged.push_back(a[i]);
        if (i < b.size()) merged.push_back(b[i]);
        if (i < c.size()) merged.push_back(c[i]);
    }
    return merged;
}

// Return the length of a signal vector
int signal_length(const std::vector<double>& signal) {
    return static_cast<int>(signal.size());
}
