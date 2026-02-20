// ============================================================================
// image_processor.cpp — C++ image processing for pipeline demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <vector>
#include <cmath>
#include <algorithm>

// Apply Gaussian blur to a 1D signal (simplified for demonstration)
void gaussian_blur(double* data, int size, double sigma) {
    std::vector<double> temp(size);
    int radius = static_cast<int>(3.0 * sigma);
    for (int i = 0; i < size; ++i) {
        double sum = 0.0;
        double weight_sum = 0.0;
        for (int j = -radius; j <= radius; ++j) {
            int idx = i + j;
            if (idx >= 0 && idx < size) {
                double w = std::exp(-(j * j) / (2.0 * sigma * sigma));
                sum += data[idx] * w;
                weight_sum += w;
            }
        }
        temp[i] = sum / weight_sum;
    }
    for (int i = 0; i < size; ++i) {
        data[i] = temp[i];
    }
}

// Enhance image contrast by stretching the value range
void enhance(double* data, int size) {
    double min_val = data[0];
    double max_val = data[0];
    for (int i = 1; i < size; ++i) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }
    double range = max_val - min_val;
    if (range > 0.0) {
        for (int i = 0; i < size; ++i) {
            data[i] = (data[i] - min_val) / range;
        }
    }
}

// Compute histogram of data values into 'bins' buckets
std::vector<int> histogram(const double* data, int size, int bins) {
    std::vector<int> hist(bins, 0);
    for (int i = 0; i < size; ++i) {
        int bucket = static_cast<int>(data[i] * (bins - 1));
        if (bucket >= 0 && bucket < bins) {
            hist[bucket]++;
        }
    }
    return hist;
}

// Threshold the data: values above threshold become 1.0, others become 0.0
void threshold(double* data, int size, double thresh) {
    for (int i = 0; i < size; ++i) {
        data[i] = (data[i] >= thresh) ? 1.0 : 0.0;
    }
}
