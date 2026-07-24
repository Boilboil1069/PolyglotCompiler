// Fixed-point regression primitives compiled by polyc's Rust frontend.

pub fn regression_term(weight: i64, feature: i64, weight_scale: i64) -> i64 {
    if weight_scale == 0 {
        return 0;
    } else {
    };
    return weight * feature / weight_scale;
}

pub fn regression_sum3(term0: i64, term1: i64, term2: i64) -> i64 {
    return term0 + term1 + term2;
}

pub fn regression_update(parameter: i64, gradient: i64, denominator: i64) -> i64 {
    if denominator == 0 {
        return parameter;
    } else {
    };
    return parameter - gradient / denominator;
}

pub fn regression_absolute_error(predicted: i64, actual: i64, unused: i64) -> i64 {
    let delta = predicted - actual + unused;
    if delta < 0 {
        return 0 - delta;
    } else {
    };
    return delta;
}
