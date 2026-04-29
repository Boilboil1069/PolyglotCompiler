// str_tokenizer.rs — Splits ASCII text into lowercase word tokens.
// Part of the PolyglotCompiler sample matrix.

#![allow(dead_code)]

pub fn tokenize(input: &str) -> Vec<String> {
    input
        .split(|c: char| !c.is_ascii_alphanumeric())
        .filter(|s| !s.is_empty())
        .map(|s| s.to_string())
        .collect()
}

pub fn token_count(input: &str) -> usize {
    tokenize(input).len()
}

