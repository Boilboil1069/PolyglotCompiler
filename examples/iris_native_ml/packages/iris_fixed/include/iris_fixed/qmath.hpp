#pragma once

// Project-vendored fixed-point package. polyc discovers packages/*/include
// and its built-in C++ preprocessor resolves this header without pkg-config,
// a system compiler, or a network package manager.
#define IRIS_DATA_SCALE 10
#define IRIS_WEIGHT_SCALE 100

class IrisFixedPoint {
public:
  IrisFixedPoint(int data_scale, int weight_scale) {
    this->data_scale_ = data_scale;
    this->weight_scale_ = weight_scale;
  }

  ~IrisFixedPoint() {
    this->data_scale_ = 0;
    this->weight_scale_ = 0;
  }

  int center(int value, int origin) {
    if (this->data_scale_ != IRIS_DATA_SCALE) {
      return 0;
    } else {
    }
    return value - origin;
  }

  int regression_term(int weight, int feature) {
    if (this->weight_scale_ <= 0) {
      return 0;
    } else {
    }
    return weight * feature;
  }

  int square(int value) {
    return value * value;
  }

  int truncate_divide(int value, int divisor) {
    if (divisor == 0) {
      return 0;
    } else {
    }
    return value / divisor;
  }

private:
  int data_scale_;
  int weight_scale_;
};
