// scorer.rs — Numerically-stable softmax scorer.
// Part of the PolyglotCompiler sample matrix.

#![allow(dead_code)]

pub fn score(tokens: &[i64]) -> Vec<f64> {
    let mut logits = vec![0.0f64; 4];
    for &tok in tokens {
        let bucket = (tok.unsigned_abs() as usize) % logits.len();
        logits[bucket] += 1.0;
    }
    softmax(&logits)
}

fn softmax(logits: &[f64]) -> Vec<f64> {
    let max = logits.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let exps: Vec<f64> = logits.iter().map(|x| (x - max).exp()).collect();
    let sum: f64 = exps.iter().sum();
    exps.into_iter().map(|x| x / sum).collect()
}

