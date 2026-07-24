// C++ adapter compiled by PolyglotCompiler's built-in C++ frontend.
// The angle include proves the vendored PACKAGE include root is actually used.
#include <iris_fixed/qmath.hpp>

int fixed_center(int value, int origin) {
  IrisFixedPoint fixed(IRIS_DATA_SCALE, IRIS_WEIGHT_SCALE);
  return fixed.center(value, origin);
}

int fixed_term(int weight, int feature) {
  // Keep the binary kernel a free function: the current built-in C++ class
  // ABI is deliberately exercised by fixed_center/fixed_square, while scalar
  // binary arithmetic follows the already-stable free-function ABI.
  return weight * feature;
}

int fixed_square(int value) {
  IrisFixedPoint fixed(IRIS_DATA_SCALE, IRIS_WEIGHT_SCALE);
  return fixed.square(value);
}

int fixed_divide(int value, int divisor) {
  if (divisor == 0) {
    return 0;
  } else {
  }
  return value / divisor;
}

int stat_centered_square_120(int sum, int sum_of_squares) {
  return 120 * sum_of_squares - sum * sum;
}

int stat_centered_cross_120(int sum_x, int sum_y, int sum_xy) {
  return 120 * sum_xy - sum_x * sum_y;
}

int stat_pearson_strong(int covariance, int variance_product, int percent) {
  if (100 * covariance * covariance >= percent * variance_product) {
    return 1;
  } else {
  }
  return 0;
}
