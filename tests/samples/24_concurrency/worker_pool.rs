// worker_pool.rs — Sequential reducer that mimics a fork/join parallel pool.
// Part of the PolyglotCompiler sample matrix.

#![allow(dead_code)]

pub fn reduce(values: &[i64]) -> i64 {
    let mut acc = 0i64;
    for &v in values {
        acc = acc.saturating_add(v);
    }
    acc
}

pub fn map_reduce<F: Fn(i64) -> i64>(values: &[i64], f: F) -> i64 {
    values.iter().map(|&v| f(v)).sum()
}

