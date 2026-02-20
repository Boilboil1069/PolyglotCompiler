// ============================================================================
// image_processor.cpp — C++ image processing for full pipeline demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <vector>
#include <cmath>
#include <string>

// Enhance image contrast by normalizing pixel values
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

// Apply edge detection filter (simplified Sobel operator)
std::vector<double> edge_detect(const double* data, int width, int height) {
    std::vector<double> result(width * height, 0.0);
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            double gx = -data[(y - 1) * width + (x - 1)]
                        + data[(y - 1) * width + (x + 1)]
                        - 2.0 * data[y * width + (x - 1)]
                        + 2.0 * data[y * width + (x + 1)]
                        - data[(y + 1) * width + (x - 1)]
                        + data[(y + 1) * width + (x + 1)];
            double gy = -data[(y - 1) * width + (x - 1)]
                        - 2.0 * data[(y - 1) * width + x]
                        - data[(y - 1) * width + (x + 1)]
                        + data[(y + 1) * width + (x - 1)]
                        + 2.0 * data[(y + 1) * width + x]
                        + data[(y + 1) * width + (x + 1)];
            result[y * width + x] = std::sqrt(gx * gx + gy * gy);
        }
    }
    return result;
}

class ImageBuffer {
public:
    int width;
    int height;
    int channels;
    std::vector<double> pixels;

    ImageBuffer(int w, int h, int c)
        : width(w), height(h), channels(c), pixels(w * h * c, 0.0) {}

    ~ImageBuffer() {
        pixels.clear();
    }

    void fill(double value) {
        for (auto& p : pixels) p = value;
    }

    double pixel_at(int x, int y, int ch) const {
        return pixels[(y * width + x) * channels + ch];
    }

    void set_pixel(int x, int y, int ch, double value) {
        pixels[(y * width + x) * channels + ch] = value;
    }

    int total_pixels() const {
        return width * height;
    }
};
