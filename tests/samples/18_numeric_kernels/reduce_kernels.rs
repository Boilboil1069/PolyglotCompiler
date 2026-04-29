// reduce_kernels.rs — Dot product and arithmetic-mean reductions.
// Part of the PolyglotCompiler sample matrix.

#![allow(dead_code)]

pub fn dot(lhs: &[f64], rhs: &[f64]) -> f64 {
    debug_assert_eq!(lhs.len(), rhs.len());
    lhs.iter().zip(rhs.iter()).map(|(a, b)| a * b).sum()
}

pub fn mean(values: &[f64]) -> f64 {
    if values.is_empty() {
        0.0
    } else {
        values.iter().sum::<f64>() / values.len() as f64
    }
}

