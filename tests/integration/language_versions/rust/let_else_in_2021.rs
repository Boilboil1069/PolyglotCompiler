// Fixture: Rust source that requires Rust edition 2021 (let-else, RFC 3137).
// On older editions the PolyglotCompiler Rust frontend must emit
// `kLangVersionMismatch`.

pub fn first_or_zero(values: &[i32]) -> i32 {
    let Some(first) = values.first() else {
        return 0;
    };
    *first
}
