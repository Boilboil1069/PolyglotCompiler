// Sufficient-statistics kernels compiled by the built-in C++ frontend.

int64_t stat_square(int64_t value, int64_t scale, int64_t offset) {
  return value * value * scale + offset;
}

int64_t stat_cross_product(int64_t feature, int64_t target, int64_t scale) {
  return feature * target * scale;
}
