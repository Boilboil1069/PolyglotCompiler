// Ordinary least-squares training compiled by the built-in Rust frontend.

pub fn ols_numerator(sample_count: i64, sum_xy: i64, sum_x_times_sum_y: i64) -> i64 {
    return sample_count * sum_xy - sum_x_times_sum_y;
}

pub fn ols_denominator(sample_count: i64, sum_x2: i64, sum_x: i64) -> i64 {
    return sample_count * sum_x2 - sum_x * sum_x;
}

pub fn fit_slope(numerator: i64, denominator: i64, fallback: i64) -> i64 {
    if denominator <= 0 {
        return fallback;
    } else {
    };
    if numerator < 0 {
        return fallback;
    } else {
    };

    // Quantized positive division for the first compiler-native ML slice.
    // The model domain caps price slopes at eight units per area bucket.
    if numerator >= denominator * 8 {
        return 8;
    } else {
    };
    if numerator >= denominator * 7 {
        return 7;
    } else {
    };
    if numerator >= denominator * 6 {
        return 6;
    } else {
    };
    if numerator >= denominator * 5 {
        return 5;
    } else {
    };
    if numerator >= denominator * 4 {
        return 4;
    } else {
    };
    if numerator >= denominator * 3 {
        return 3;
    } else {
    };
    if numerator >= denominator * 2 {
        return 2;
    } else {
    };
    if numerator >= denominator {
        return 1;
    } else {
    };
    return 0;
}

pub fn fit_intercept(sum_y: i64, slope_times_sum_x: i64, sample_count: i64) -> i64 {
    if sample_count <= 0 {
        return 0;
    } else {
    };
    let residual = sum_y - slope_times_sum_x;
    if residual < 0 {
        return 0;
    } else {
    };

    // The synthetic price domain bounds the non-negative intercept at 16.
    if residual >= sample_count * 16 {
        return 16;
    } else {
    };
    if residual >= sample_count * 15 {
        return 15;
    } else {
    };
    if residual >= sample_count * 14 {
        return 14;
    } else {
    };
    if residual >= sample_count * 13 {
        return 13;
    } else {
    };
    if residual >= sample_count * 12 {
        return 12;
    } else {
    };
    if residual >= sample_count * 11 {
        return 11;
    } else {
    };
    if residual >= sample_count * 10 {
        return 10;
    } else {
    };
    if residual >= sample_count * 9 {
        return 9;
    } else {
    };
    if residual >= sample_count * 8 {
        return 8;
    } else {
    };
    if residual >= sample_count * 7 {
        return 7;
    } else {
    };
    if residual >= sample_count * 6 {
        return 6;
    } else {
    };
    if residual >= sample_count * 5 {
        return 5;
    } else {
    };
    if residual >= sample_count * 4 {
        return 4;
    } else {
    };
    if residual >= sample_count * 3 {
        return 3;
    } else {
    };
    if residual >= sample_count * 2 {
        return 2;
    } else {
    };
    if residual >= sample_count {
        return 1;
    } else {
    };
    return 0;
}
